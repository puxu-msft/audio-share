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

#ifndef AUDIO_SHARE_CONSTANTS_HPP
#define AUDIO_SHARE_CONSTANTS_HPP

#include <chrono>
#include <cstdint>

namespace audio_share {
namespace constants {

    // Network constants
    constexpr uint16_t DEFAULT_PORT = 65530;
    constexpr uint16_t MIN_PORT = 1;
    constexpr uint16_t MAX_PORT = 65535;
    
    // MTU and packet size
    constexpr int DEFAULT_MTU = 1492;
    constexpr int IPV4_HEADER_SIZE = 20;
    constexpr int IPV6_HEADER_SIZE = 40;
    constexpr int UDP_HEADER_SIZE = 8;
    
    // Use the smaller payload size (IPv6) to ensure compatibility with both protocols
    constexpr int MAX_UDP_PAYLOAD_SIZE = DEFAULT_MTU - IPV6_HEADER_SIZE - UDP_HEADER_SIZE;
    
    // Heartbeat timing
    constexpr auto HEARTBEAT_INTERVAL = std::chrono::seconds(3);
    constexpr auto HEARTBEAT_TIMEOUT = std::chrono::seconds(5);
    
    // Protocol limits
    constexpr int MAX_AUDIO_FORMAT_SIZE = 1024;  // Maximum size of AudioFormat message
    constexpr int MAX_CLIENTS = 100;  // Maximum number of concurrent clients

} // namespace constants
} // namespace audio_share

#endif // AUDIO_SHARE_CONSTANTS_HPP
