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

#include "network_manager.hpp"
#include "formatter.hpp"
#include "audio_manager.hpp"
#include "constants.hpp"

#include <list>
#include <ranges>
#include <coroutine>
#include <memory>
#include <set>

#ifdef _WINDOWS
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#endif // _WINDOWS

#ifdef linux
#include <sys/types.h>
#include <ifaddrs.h>
#endif

#include <spdlog/spdlog.h>
#include <fmt/ranges.h>

namespace ip = asio::ip;
using namespace std::chrono_literals;
using namespace audio_share::constants;

network_manager::network_manager(std::shared_ptr<audio_manager>& audio_manager)
    : _audio_manager(audio_manager)
    , _buffer_pool(std::make_unique<audio_share::buffer_pool>(MAX_UDP_PAYLOAD_SIZE, 16, 128))
{
}

std::vector<std::string> network_manager::get_address_list()
{
    std::vector<std::string> address_list;

#ifdef _WINDOWS
    ULONG family = AF_INET;
    ULONG flags = GAA_FLAG_INCLUDE_ALL_INTERFACES;

    // First call to get required buffer size
    ULONG size = 0;
    ULONG ret = GetAdaptersAddresses(family, flags, nullptr, nullptr, &size);
    if (ret != ERROR_BUFFER_OVERFLOW) {
        spdlog::warn("GetAdaptersAddresses failed to get buffer size, error: {}", ret);
        return address_list;
    }

    // RAII wrapper for adapter addresses memory
    auto addresses_deleter = [](IP_ADAPTER_ADDRESSES* p) { free(p); };
    std::unique_ptr<IP_ADAPTER_ADDRESSES, decltype(addresses_deleter)> pAddresses(
        static_cast<PIP_ADAPTER_ADDRESSES>(malloc(size)), addresses_deleter);
    
    if (!pAddresses) {
        spdlog::error("Failed to allocate memory for adapter addresses");
        return address_list;
    }

    ret = GetAdaptersAddresses(family, flags, nullptr, pAddresses.get(), &size);
    if (ret != ERROR_SUCCESS) {
        spdlog::warn("GetAdaptersAddresses failed, error: {}", ret);
        return address_list;
    }

    for (auto pCurrentAddress = pAddresses.get(); pCurrentAddress; pCurrentAddress = pCurrentAddress->Next) {
        if (pCurrentAddress->OperStatus != IfOperStatusUp || pCurrentAddress->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }

        for (auto pUnicast = pCurrentAddress->FirstUnicastAddress; pUnicast; pUnicast = pUnicast->Next) {
            auto sockaddr = reinterpret_cast<sockaddr_in*>(pUnicast->Address.lpSockaddr);
            char buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &sockaddr->sin_addr, buf, sizeof(buf))) {
                address_list.emplace_back(buf);
            }
        }
    }
#endif

#ifdef linux
    struct ifaddrs* ifaddrs_raw = nullptr;
    if (getifaddrs(&ifaddrs_raw) == -1) {
        spdlog::warn("getifaddrs failed");
        return address_list;
    }

    // RAII wrapper for ifaddrs
    auto ifaddrs_deleter = [](struct ifaddrs* p) { freeifaddrs(p); };
    std::unique_ptr<struct ifaddrs, decltype(ifaddrs_deleter)> ifaddrs(ifaddrs_raw, ifaddrs_deleter);

    for (auto ifa = ifaddrs.get(); ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }
        auto sockaddr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &sockaddr->sin_addr, buf, sizeof(buf))) {
            address_list.emplace_back(buf);
        }
    }
#endif

    return address_list;
}

std::string network_manager::get_default_address()
{
    return select_default_address(get_address_list());
}

std::string network_manager::select_default_address(const std::vector<std::string>& address_list)
{
    if (address_list.empty()) {
        return {};
    }

    auto is_private_address = [](const std::string& address) {
        constexpr uint32_t private_addr_list[] = {
            0x0a000000,
            0xac100000,
            0xc0a80000,
        };

        uint32_t addr;
        inet_pton(AF_INET, address.c_str(), &addr);
        addr = ntohl(addr);
        for (auto&& private_addr : private_addr_list) {
            if ((addr & private_addr) == private_addr) {
                return true;
            }
        }

        return false;
    };

    for (auto&& address : address_list) {
        if (is_private_address(address)) {
            return address;
        }
    }
    return address_list.front();
}

void network_manager::start_server(const std::string& host, uint16_t port, const audio_manager::capture_config& capture_config)
{
    _ioc = std::make_shared<asio::io_context>();
    {
        ip::tcp::endpoint endpoint { ip::make_address(host), port };

        ip::tcp::acceptor acceptor(*_ioc, endpoint.protocol());
        acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        _audio_manager->start_loopback_recording(shared_from_this(), capture_config);
        asio::co_spawn(*_ioc, accept_tcp_loop(std::move(acceptor)), asio::detached);

        // start tcp success
        spdlog::info("tcp listen success on {}", endpoint);
    }

    {
        ip::udp::endpoint endpoint { ip::make_address(host), port };
        _udp_server = std::make_unique<udp_socket>(*_ioc, endpoint.protocol());
        _udp_server->bind(endpoint);
        asio::co_spawn(*_ioc, accept_udp_loop(), asio::detached);

        // start udp success
        spdlog::info("udp listen success on {}", endpoint);
    }

    _net_thread = std::thread([self = shared_from_this()] {
        self->_ioc->run();
    });

    spdlog::info("server started");
}

void network_manager::stop_server()
{
    if (_ioc) {
        _ioc->stop();
    }
    _net_thread.join();
    _audio_manager->stop();
    _playing_peer_list.clear();
    _udp_server = nullptr;
    _ioc = nullptr;
    spdlog::info("server stopped");
}

void network_manager::wait_server()
{
    _net_thread.join();
}

bool network_manager::is_running() const
{
    return _ioc != nullptr;
}

asio::awaitable<void> network_manager::read_loop(std::shared_ptr<tcp_socket> peer)
{
    while (true) {
        cmd_t cmd = cmd_t::cmd_none;
        auto [ec, _] = co_await asio::async_read(*peer, asio::buffer(&cmd, sizeof(cmd)));
        if (ec) {
            close_session(peer);
            spdlog::trace("{} {}", __func__, ec);
            break;
        }

        spdlog::trace("cmd {}", (uint32_t)cmd);

        if (cmd == cmd_t::cmd_get_format) {
            auto format = _audio_manager->get_format_binary();
            auto size = (uint32_t)format.size();
            std::array<asio::const_buffer, 3> buffers = {
                asio::buffer(&cmd, sizeof(cmd)),
                asio::buffer(&size, sizeof(size)),
                asio::buffer(format),
            };
            auto [ec, _] = co_await asio::async_write(*peer, buffers);
            if (ec) {
                close_session(peer);
                spdlog::trace("{} {}", __func__, ec);
                break;
            }
        } else if (cmd == cmd_t::cmd_start_play) {
            int id = add_playing_peer(peer);
            if (id <= 0) {
                spdlog::error("{} id error", __func__);
                close_session(peer);
                spdlog::trace("{} {}", __func__, ec);
                break;
            }
            std::array<asio::const_buffer, 2> buffers = {
                asio::buffer(&cmd, sizeof(cmd)),
                asio::buffer(&id, sizeof(id)),
            };
            auto [ec, _] = co_await asio::async_write(*peer, buffers);
            if (ec) {
                spdlog::trace("{} {}", __func__, ec);
                close_session(peer);
                break;
            }
            asio::co_spawn(*_ioc, heartbeat_loop(peer), asio::detached);
        } else if (cmd == cmd_t::cmd_heartbeat) {
            std::lock_guard<std::mutex> lock(_peer_list_mutex);
            auto it = _playing_peer_list.find(peer);
            if (it != _playing_peer_list.end()) {
                it->second->last_tick.store(std::chrono::steady_clock::now());
            }
        } else {
            spdlog::error("{} error cmd", __func__);
            close_session(peer);
            break;
        }
    }
    spdlog::trace("stop {}", __func__);
}

asio::awaitable<void> network_manager::heartbeat_loop(std::shared_ptr<tcp_socket> peer)
{
    std::error_code ec;
    size_t _;

    steady_timer timer(*_ioc);
    while (true) {
        timer.expires_after(HEARTBEAT_INTERVAL);
        std::tie(ec) = co_await timer.async_wait();
        if (ec) {
            break;
        }

        if (!peer->is_open()) {
            break;
        }

        // Check peer status with thread-safe access
        bool should_close = false;
        bool peer_not_found = false;
        {
            std::lock_guard<std::mutex> lock(_peer_list_mutex);
            auto it = _playing_peer_list.find(peer);
            if (it == _playing_peer_list.end()) {
                spdlog::trace("{} it == _playing_peer_list.end()", __func__);
                peer_not_found = true;
            } else if (std::chrono::steady_clock::now() - it->second->last_tick.load() > _heartbeat_timeout) {
                spdlog::info("{} timeout", it->first->remote_endpoint());
                should_close = true;
            }
        }

        if (peer_not_found || should_close) {
            close_session(peer);
            break;
        }

        auto cmd = cmd_t::cmd_heartbeat;
        std::tie(ec, _) = co_await asio::async_write(*peer, asio::buffer(&cmd, sizeof(cmd)));
        if (ec) {
            spdlog::trace("{} {}", __func__, ec);
            close_session(peer);
            break;
        }
    }
    spdlog::trace("stop {}", __func__);
}

asio::awaitable<void> network_manager::accept_tcp_loop(tcp_acceptor acceptor)
{
    while (true) {
        auto peer = std::make_shared<tcp_socket>(acceptor.get_executor());
        auto [ec] = co_await acceptor.async_accept(*peer);
        if (ec) {
            spdlog::error("{} {}", __func__, ec);
            co_return;
        }

        spdlog::info("accept {}", peer->remote_endpoint());

        // No-Delay
        peer->set_option(ip::tcp::no_delay(true), ec);
        if (ec) {
            spdlog::info("{} {}", __func__, ec);
        }

        asio::co_spawn(acceptor.get_executor(), read_loop(peer), asio::detached);
    }
}

asio::awaitable<void> network_manager::accept_udp_loop()
{
    while (true) {
        int id = 0;
        ip::udp::endpoint udp_peer;
        auto [ec, _] = co_await _udp_server->async_receive_from(asio::buffer(&id, sizeof(id)), udp_peer);
        if (ec) {
            spdlog::info("{} {}", __func__, ec);
            co_return;
        }

        fill_udp_peer(id, udp_peer);
    }
}

auto network_manager::close_session(std::shared_ptr<tcp_socket>& peer) -> playing_peer_list_t::iterator
{
    spdlog::info("close {}", peer->remote_endpoint());
    auto it = remove_playing_peer(peer);
    peer->shutdown(ip::tcp::socket::shutdown_both);
    peer->close();
    return it;
}

int network_manager::add_playing_peer(std::shared_ptr<tcp_socket>& peer)
{
    std::lock_guard<std::mutex> lock(_peer_list_mutex);
    if (_playing_peer_list.contains(peer)) {
        spdlog::error("{} repeat add tcp://{}", __func__, peer->remote_endpoint());
        return 0;
    }

    auto info = _playing_peer_list[peer] = std::make_shared<peer_info_t>();
    static int g_id = 0;
    info->id = ++g_id;
    info->last_tick.store(std::chrono::steady_clock::now());

    spdlog::trace("{} add id:{} tcp://{}", __func__, info->id, peer->remote_endpoint());
    return info->id;
}

auto network_manager::remove_playing_peer(std::shared_ptr<tcp_socket>& peer) -> playing_peer_list_t::iterator
{
    std::lock_guard<std::mutex> lock(_peer_list_mutex);
    auto it = _playing_peer_list.find(peer);
    if (it == _playing_peer_list.end()) {
        spdlog::error("{} repeat remove tcp://{}", __func__, peer->remote_endpoint());
        return it;
    }

    it = _playing_peer_list.erase(it);
    spdlog::trace("{} remove tcp://{}", __func__, peer->remote_endpoint());
    return it;
}

void network_manager::fill_udp_peer(int id, asio::ip::udp::endpoint udp_peer)
{
    std::lock_guard<std::mutex> lock(_peer_list_mutex);
    auto it = std::find_if(_playing_peer_list.begin(), _playing_peer_list.end(), [id](const playing_peer_list_t::value_type& e) {
        return e.second->id == id;
    });

    if (it == _playing_peer_list.cend()) {
        spdlog::error("{} no tcp peer id:{} udp://{}", __func__, id, udp_peer);
        return;
    }

    // Handle IPv4-mapped IPv6 addresses (::ffff:x.x.x.x)
    // If the UDP server is IPv4 but client sends from IPv6-mapped address, extract the IPv4 part
    auto address = udp_peer.address();
    if (address.is_v6()) {
        auto v6_addr = address.to_v6();
        if (v6_addr.is_v4_mapped()) {
            // Convert IPv4-mapped IPv6 address to IPv4
            auto v4_addr = asio::ip::make_address_v4(asio::ip::v4_mapped, v6_addr);
            udp_peer = ip::udp::endpoint(v4_addr, udp_peer.port());
            spdlog::debug("{} converted IPv4-mapped IPv6 to IPv4: {}", __func__, udp_peer);
        }
    }

    it->second->udp_peer = udp_peer;
    spdlog::info("{} fill udp peer id:{} tcp://{} udp://{}", __func__, id, it->first->remote_endpoint(), udp_peer);
}

void network_manager::broadcast_audio_data(const char* data, size_t count, int block_align)
{
    if (count <= 0) {
        return;
    }
    // spdlog::trace("broadcast_audio_data count: {}", count);

    // Forward to additional broadcasters (e.g., WebSocket)
    {
        std::lock_guard<std::mutex> lock(_broadcasters_mutex);
        for (const auto& broadcaster : _additional_broadcasters) {
            if (broadcaster) {
                broadcaster->broadcast_audio_data(data, count, block_align);
            }
        }
    }

    // divide udp frame using constants
    int max_seg_size = MAX_UDP_PAYLOAD_SIZE;
    max_seg_size -= max_seg_size % block_align; // one single sample can't be divided

    // Use buffer pool for reduced allocation overhead
    std::vector<audio_share::buffer_pool::buffer_ptr> seg_list;
    seg_list.reserve((count + max_seg_size - 1) / max_seg_size);

    for (size_t begin_pos = 0; begin_pos < count;) {
        const size_t real_seg_size = std::min(count - begin_pos, static_cast<size_t>(max_seg_size));
        auto seg = _buffer_pool->acquire();
        seg->resize(real_seg_size);
        std::copy((const uint8_t*)data + begin_pos, (const uint8_t*)data + begin_pos + real_seg_size, seg->begin());
        seg_list.push_back(std::move(seg));
        begin_pos += real_seg_size;
    }

    // Create a thread-safe copy of UDP endpoints
    // Filter out endpoints with incompatible address families
    std::vector<asio::ip::udp::endpoint> udp_peers;
    {
        std::lock_guard<std::mutex> lock(_peer_list_mutex);
        udp_peers.reserve(_playing_peer_list.size());
        bool is_server_v4 = _udp_server->local_endpoint().address().is_v4();
        for (const auto& [peer, info] : _playing_peer_list) {
            const auto& udp_ep = info->udp_peer;
            // Check address family compatibility
            if ((is_server_v4 && udp_ep.address().is_v4()) ||
                (!is_server_v4 && udp_ep.address().is_v6())) {
                udp_peers.push_back(udp_ep);
            } else {
                // Log mismatch only once per session (avoid spamming)
                static std::set<int> logged_ids;
                if (logged_ids.find(info->id) == logged_ids.end()) {
                    spdlog::warn("Address family mismatch for peer id:{} - server is {}, client UDP is {}",
                        info->id, is_server_v4 ? "IPv4" : "IPv6", 
                        udp_ep.address().is_v4() ? "IPv4" : "IPv6");
                    logged_ids.insert(info->id);
                }
            }
        }
    }

    _ioc->post([seg_list = std::move(seg_list), udp_peers = std::move(udp_peers), self = shared_from_this()] {
        for (const auto& seg : seg_list) {
            for (const auto& udp_peer : udp_peers) {
                self->_udp_server->async_send_to(asio::buffer(*seg), udp_peer, [seg](const asio::error_code& ec, std::size_t bytes_transferred) { 
                    if (ec) {
                        spdlog::trace("UDP send error: {}", ec.message());
                    }
                });
            }
        }
    });
}

void network_manager::add_broadcaster(std::shared_ptr<audio_broadcaster> broadcaster)
{
    std::lock_guard<std::mutex> lock(_broadcasters_mutex);
    _additional_broadcasters.push_back(broadcaster);
    spdlog::info("Added additional broadcaster, total: {}", _additional_broadcasters.size());
}
