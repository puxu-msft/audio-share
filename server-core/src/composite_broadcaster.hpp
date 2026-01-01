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

#ifndef COMPOSITE_BROADCASTER_HPP
#define COMPOSITE_BROADCASTER_HPP

#include "audio_broadcaster.hpp"
#include <vector>
#include <memory>

/**
 * @brief Composite broadcaster that forwards audio to multiple broadcasters
 * 
 * This allows audio data to be sent to both TCP/UDP clients and WebSocket clients
 * through a single broadcast call.
 */
class composite_broadcaster : public audio_broadcaster {
public:
    void add_broadcaster(std::shared_ptr<audio_broadcaster> broadcaster) {
        _broadcasters.push_back(broadcaster);
    }

    void broadcast_audio_data(const char* data, size_t count, int block_align) override {
        for (auto& broadcaster : _broadcasters) {
            if (broadcaster) {
                broadcaster->broadcast_audio_data(data, count, block_align);
            }
        }
    }

private:
    std::vector<std::shared_ptr<audio_broadcaster>> _broadcasters;
};

#endif // !COMPOSITE_BROADCASTER_HPP
