/*
   Copyright 2022-2024 mkckr0 <https://github.com/mkckr0>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "websocket_manager.hpp"

#include <spdlog/spdlog.h>

websocket_manager::websocket_manager(std::shared_ptr<audio_manager>& audio_manager)
    : _audio_manager(audio_manager)
{
}

websocket_manager::~websocket_manager()
{
    stop_server();
}

void websocket_manager::start_server(const std::string& host, uint16_t port, const audio_manager::capture_config& capture_config)
{
    if (_is_running) {
        spdlog::warn("[WS] Server already running");
        return;
    }

    _capture_config = capture_config;
    _ioc = std::make_shared<asio::io_context>();

    _server_thread = std::thread([this, host, port]() {
        try {
            // Resolve the host
            asio::ip::tcp::resolver resolver(*_ioc);
            auto endpoints = resolver.resolve(host, std::to_string(port));
            
            if (endpoints.empty()) {
                throw std::runtime_error("Failed to resolve host: " + host);
            }

            // Get endpoint
            auto endpoint = endpoints.begin()->endpoint();
            
            // Create acceptor
            tcp_acceptor acceptor(*_ioc);
            acceptor.open(endpoint.protocol());
            acceptor.set_option(asio::socket_base::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.listen(asio::socket_base::max_listen_connections);

            spdlog::info("[WS] WebSocket server started on {}:{}", host, port);
            _is_running = true;

            // Run accept loop
            asio::co_spawn(*_ioc, accept_loop(std::move(acceptor)), asio::detached);
            _ioc->run();
        } catch (const std::exception& e) {
            spdlog::error("[WS] Server error: {}", e.what());
        }

        _is_running = false;
        spdlog::info("[WS] WebSocket server stopped");
    });
}

void websocket_manager::stop_server()
{
    if (!_is_running) return;

    _is_running = false;

    // Close all sessions
    {
        std::lock_guard<std::mutex> lock(_sessions_mutex);
        for (auto& [key, session] : _sessions) {
            if (session && session->ws) {
                beast::error_code ec;
                session->ws->close(websocket::close_code::going_away, ec);
            }
        }
        _sessions.clear();
    }

    if (_ioc) {
        _ioc->stop();
    }

    if (_server_thread.joinable()) {
        _server_thread.join();
    }
}

void websocket_manager::wait_server()
{
    if (_server_thread.joinable()) {
        _server_thread.join();
    }
}

bool websocket_manager::is_running() const
{
    return _is_running;
}

asio::awaitable<void> websocket_manager::accept_loop(tcp_acceptor acceptor)
{
    while (_is_running) {
        auto [ec, socket] = co_await acceptor.async_accept();
        
        if (ec) {
            if (ec == asio::error::operation_aborted) {
                break;
            }
            spdlog::warn("[WS] Accept error: {}", ec.message());
            continue;
        }

        // Create WebSocket stream
        auto ws = std::make_shared<websocket::stream<beast::tcp_stream>>(std::move(socket));
        
        // Spawn session handler
        asio::co_spawn(*_ioc, handle_session(ws), asio::detached);
    }
}

asio::awaitable<void> websocket_manager::handle_session(std::shared_ptr<websocket::stream<beast::tcp_stream>> ws)
{
    beast::error_code ec;
    
    try {
        // Set WebSocket options
        ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws->set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
            res.set(http::field::server, "AudioShare-WebSocket/1.0");
        }));

        // Read HTTP upgrade request
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        
        // Accept the WebSocket handshake
        co_await ws->async_accept(asio::use_awaitable);
        
        spdlog::info("[WS] New WebSocket connection");

        // Create session
        auto session = std::make_shared<ws_session>();
        session->ws = ws;
        add_session(session);

        // Send audio format
        co_await send_audio_format(session);

        // Start heartbeat and send loops
        asio::co_spawn(*_ioc, heartbeat_loop(session), asio::detached);
        asio::co_spawn(*_ioc, send_loop(session), asio::detached);

        // Read loop for receiving client messages
        while (_is_running) {
            beast::flat_buffer read_buffer;
            auto [read_ec, bytes_read] = co_await ws->async_read(read_buffer);
            
            if (read_ec) {
                if (read_ec == websocket::error::closed) {
                    spdlog::info("[WS] Client closed connection");
                } else {
                    spdlog::warn("[WS] Read error: {}", read_ec.message());
                }
                break;
            }

            // Update heartbeat timestamp
            session->last_tick.store(std::chrono::steady_clock::now());

            // Parse client message (heartbeat, etc.)
            std::string msg = beast::buffers_to_string(read_buffer.data());
            if (msg == "ping") {
                // Send pong
                ws->text(true);
                co_await ws->async_write(asio::buffer("pong"));
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[WS] Session error: {}", e.what());
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(_sessions_mutex);
        for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
            if (it->second->ws == ws) {
                _sessions.erase(it);
                break;
            }
        }
    }

    spdlog::info("[WS] Session ended");
}

asio::awaitable<void> websocket_manager::send_audio_format(std::shared_ptr<ws_session> session)
{
    if (!session || !session->ws) co_return;

    try {
        std::string format_json = build_format_json();
        
        session->ws->text(true);
        co_await session->ws->async_write(asio::buffer(format_json));
        
        spdlog::debug("[WS] Sent audio format: {}", format_json);
    } catch (const std::exception& e) {
        spdlog::warn("[WS] Failed to send format: {}", e.what());
    }
}

asio::awaitable<void> websocket_manager::heartbeat_loop(std::shared_ptr<ws_session> session)
{
    asio::steady_timer timer(*_ioc);
    
    while (_is_running && session && session->ws && session->ws->is_open()) {
        timer.expires_after(_heartbeat_interval);
        co_await timer.async_wait(asio::use_awaitable);

        auto now = std::chrono::steady_clock::now();
        auto last = session->last_tick.load();
        
        if (now - last > _heartbeat_timeout) {
            spdlog::info("[WS] Client heartbeat timeout");
            beast::error_code ec;
            session->ws->close(websocket::close_code::going_away, ec);
            break;
        }
    }
}

asio::awaitable<void> websocket_manager::send_loop(std::shared_ptr<ws_session> session)
{
    asio::steady_timer timer(*_ioc);
    
    while (_is_running && session && session->ws && session->ws->is_open()) {
        // Check for queued audio data
        std::vector<uint8_t> audio_data;
        {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            if (!session->audio_queue.empty()) {
                audio_data = std::move(session->audio_queue.front());
                session->audio_queue.pop();
            }
        }

        if (!audio_data.empty()) {
            try {
                session->ws->binary(true);
                co_await session->ws->async_write(asio::buffer(audio_data));
            } catch (const std::exception& e) {
                spdlog::warn("[WS] Send error: {}", e.what());
                break;
            }
        } else {
            // No data, wait a bit
            timer.expires_after(std::chrono::milliseconds(5));
            co_await timer.async_wait(asio::use_awaitable);
        }
    }
}

void websocket_manager::add_session(std::shared_ptr<ws_session> session)
{
    std::lock_guard<std::mutex> lock(_sessions_mutex);
    _sessions[reinterpret_cast<uintptr_t>(session.get())] = session;
    spdlog::info("[WS] Added session, total: {}", _sessions.size());
}

void websocket_manager::remove_session(std::shared_ptr<ws_session> session)
{
    std::lock_guard<std::mutex> lock(_sessions_mutex);
    _sessions.erase(reinterpret_cast<uintptr_t>(session.get()));
    spdlog::info("[WS] Removed session, total: {}", _sessions.size());
}

void websocket_manager::broadcast_audio_data(const char* data, size_t count, [[maybe_unused]] int block_align)
{
    if (!_is_running || count == 0) return;

    std::lock_guard<std::mutex> lock(_sessions_mutex);
    
    for (auto& [key, session] : _sessions) {
        if (session && session->ws && session->ws->is_open()) {
            std::lock_guard<std::mutex> queue_lock(session->queue_mutex);
            
            // Limit queue size to prevent memory buildup
            if (session->audio_queue.size() < 50) {
                session->audio_queue.emplace(
                    reinterpret_cast<const uint8_t*>(data),
                    reinterpret_cast<const uint8_t*>(data) + count
                );
            }
        }
    }
}

std::string websocket_manager::build_format_json() const
{
    // Map encoding enum to string
    std::string encoding_str;
    switch (_capture_config.encoding) {
        case audio_manager::encoding_t::encoding_f32:
            encoding_str = "f32";
            break;
        case audio_manager::encoding_t::encoding_s8:
            encoding_str = "s8";
            break;
        case audio_manager::encoding_t::encoding_s16:
            encoding_str = "s16";
            break;
        case audio_manager::encoding_t::encoding_s24:
            encoding_str = "s24";
            break;
        case audio_manager::encoding_t::encoding_s32:
            encoding_str = "s32";
            break;
        default:
            encoding_str = "s16";
    }

    // Calculate bits per sample
    int bits_per_sample;
    switch (_capture_config.encoding) {
        case audio_manager::encoding_t::encoding_f32:
        case audio_manager::encoding_t::encoding_s32:
            bits_per_sample = 32;
            break;
        case audio_manager::encoding_t::encoding_s24:
            bits_per_sample = 24;
            break;
        case audio_manager::encoding_t::encoding_s16:
            bits_per_sample = 16;
            break;
        case audio_manager::encoding_t::encoding_s8:
            bits_per_sample = 8;
            break;
        default:
            bits_per_sample = 16;
    }

    // Build JSON (simple format, no external dependency)
    std::string json = "{";
    json += "\"type\":\"format\",";
    json += "\"encoding\":\"" + encoding_str + "\",";
    json += "\"channels\":" + std::to_string(_capture_config.channels) + ",";
    json += "\"sampleRate\":" + std::to_string(_capture_config.sample_rate) + ",";
    json += "\"bitsPerSample\":" + std::to_string(bits_per_sample);
    json += "}";

    return json;
}
