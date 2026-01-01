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

#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <memory>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <atomic>

#include "pre_asio.hpp"
#include <asio.hpp>
#include <asio/use_awaitable.hpp>

#include "buffer_pool.hpp"

#include "audio_broadcaster.hpp"
#include "audio_manager.hpp"
#include "constants.hpp"

class network_manager : public audio_broadcaster, public std::enable_shared_from_this<network_manager>
{
    using default_token = asio::as_tuple_t<asio::use_awaitable_t<>>;
    using tcp_acceptor = default_token::as_default_on_t<asio::ip::tcp::acceptor>;
    using tcp_socket = default_token::as_default_on_t<asio::ip::tcp::socket>;
    using udp_socket = default_token::as_default_on_t<asio::ip::udp::socket>;
    using steady_timer = default_token::as_default_on_t<asio::steady_timer>;

    struct peer_info_t {
        int id = 0;
        asio::ip::udp::endpoint udp_peer;
        // Use atomic for thread-safe access to last_tick
        std::atomic<std::chrono::steady_clock::time_point> last_tick{std::chrono::steady_clock::now()};
        
        peer_info_t() = default;
        peer_info_t(const peer_info_t& other) 
            : id(other.id), udp_peer(other.udp_peer), last_tick(other.last_tick.load()) {}
        peer_info_t& operator=(const peer_info_t& other) {
            if (this != &other) {
                id = other.id;
                udp_peer = other.udp_peer;
                last_tick.store(other.last_tick.load());
            }
            return *this;
        }
    };

    using playing_peer_list_t = std::map<std::shared_ptr<tcp_socket>, std::shared_ptr<peer_info_t>>;

    enum class cmd_t : uint32_t {
        cmd_none = 0,
        cmd_get_format = 1,
        cmd_start_play = 2,
        cmd_heartbeat = 3,
    };

public:

    explicit network_manager(std::shared_ptr<audio_manager>& audio_manager);

    static std::vector<std::string> get_address_list();
    static std::string get_default_address();
private:
    static std::string select_default_address(const std::vector<std::string>& address_list);

public:
    void start_server(const std::string& host, uint16_t port, const audio_manager::capture_config& capture_config);
    void stop_server();
    void wait_server();
    bool is_running() const;

private:
    asio::awaitable<void> accept_tcp_loop(tcp_acceptor acceptor);
    asio::awaitable<void> read_loop(std::shared_ptr<tcp_socket> peer);
    asio::awaitable<void> heartbeat_loop(std::shared_ptr<tcp_socket> peer);
    asio::awaitable<void> accept_udp_loop();
    
    playing_peer_list_t::iterator close_session(std::shared_ptr<tcp_socket>& peer);
    int add_playing_peer(std::shared_ptr<tcp_socket>& peer);
    playing_peer_list_t::iterator remove_playing_peer(std::shared_ptr<tcp_socket>& peer);
    void fill_udp_peer(int id, asio::ip::udp::endpoint udp_peer);

public:
    void broadcast_audio_data(const char* data, size_t count, int block_align) override;
    
    /**
     * @brief Register an additional broadcaster to receive audio data
     * @param broadcaster Shared pointer to the broadcaster
     */
    void add_broadcaster(std::shared_ptr<audio_broadcaster> broadcaster);
    
    std::shared_ptr<asio::io_context> _ioc;

private:
    std::shared_ptr<audio_manager> _audio_manager;
    std::thread _net_thread;
    std::unique_ptr<udp_socket> _udp_server;
    playing_peer_list_t _playing_peer_list;
    mutable std::mutex _peer_list_mutex;  // Protects _playing_peer_list
    constexpr static auto _heartbeat_timeout = audio_share::constants::HEARTBEAT_TIMEOUT;
    std::unique_ptr<audio_share::buffer_pool> _buffer_pool;  // Buffer pool for UDP packets
    std::vector<std::shared_ptr<audio_broadcaster>> _additional_broadcasters;  // Additional broadcasters (e.g., WebSocket)
    mutable std::mutex _broadcasters_mutex;  // Protects _additional_broadcasters
};

#endif // !NETWORK_MANAGER_HPP