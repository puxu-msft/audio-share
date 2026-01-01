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

#ifndef WEBSOCKET_MANAGER_HPP
#define WEBSOCKET_MANAGER_HPP

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <atomic>
#include <queue>

#include "pre_asio.hpp"
#include <asio.hpp>
#include <asio/use_awaitable.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>

#include "audio_broadcaster.hpp"
#include "audio_manager.hpp"
#include "constants.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

/**
 * @brief WebSocket manager for web browser clients
 * 
 * Provides WebSocket server functionality for Audio Share, enabling
 * web browsers to connect and receive audio streams.
 */
class websocket_manager : public audio_broadcaster, public std::enable_shared_from_this<websocket_manager>
{
    using default_token = asio::as_tuple_t<asio::use_awaitable_t<>>;
    using tcp_acceptor = default_token::as_default_on_t<asio::ip::tcp::acceptor>;
    
    // WebSocket session info
    struct ws_session {
        std::shared_ptr<websocket::stream<beast::tcp_stream>> ws;
        std::queue<std::vector<uint8_t>> audio_queue;
        std::mutex queue_mutex;
        std::atomic<bool> is_sending{false};
        std::atomic<std::chrono::steady_clock::time_point> last_tick{std::chrono::steady_clock::now()};
        
        ws_session() = default;
    };

    using session_map_t = std::map<uintptr_t, std::shared_ptr<ws_session>>;

public:
    explicit websocket_manager(std::shared_ptr<audio_manager>& audio_manager);
    ~websocket_manager();

    /**
     * @brief Start WebSocket server on specified host and port
     * @param host Host address to bind to
     * @param port Port to listen on (will use a different port than TCP/UDP)
     * @param capture_config Audio capture configuration
     */
    void start_server(const std::string& host, uint16_t port, const audio_manager::capture_config& capture_config);
    
    /**
     * @brief Stop WebSocket server
     */
    void stop_server();
    
    /**
     * @brief Wait for server thread to complete
     */
    void wait_server();
    
    /**
     * @brief Check if server is running
     */
    bool is_running() const;

    /**
     * @brief Broadcast audio data to all connected WebSocket clients
     * @param data Audio PCM data
     * @param count Size of data in bytes
     * @param block_align Block alignment for audio data
     */
    void broadcast_audio_data(const char* data, size_t count, int block_align) override;

private:
    asio::awaitable<void> accept_loop(tcp_acceptor acceptor);
    asio::awaitable<void> handle_session(std::shared_ptr<websocket::stream<beast::tcp_stream>> ws);
    asio::awaitable<void> send_audio_format(std::shared_ptr<ws_session> session);
    asio::awaitable<void> heartbeat_loop(std::shared_ptr<ws_session> session);
    asio::awaitable<void> send_loop(std::shared_ptr<ws_session> session);
    
    void add_session(std::shared_ptr<ws_session> session);
    void remove_session(std::shared_ptr<ws_session> session);
    
    std::string build_format_json() const;

private:
    std::shared_ptr<asio::io_context> _ioc;
    std::shared_ptr<audio_manager> _audio_manager;
    std::thread _server_thread;
    session_map_t _sessions;
    mutable std::mutex _sessions_mutex;
    audio_manager::capture_config _capture_config;
    std::atomic<bool> _is_running{false};
    
    static constexpr auto _heartbeat_interval = std::chrono::seconds(10);
    static constexpr auto _heartbeat_timeout = std::chrono::seconds(30);
};

#endif // !WEBSOCKET_MANAGER_HPP
