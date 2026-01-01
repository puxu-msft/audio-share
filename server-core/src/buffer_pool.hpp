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

#ifndef BUFFER_POOL_HPP
#define BUFFER_POOL_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <stack>

namespace audio_share {

/**
 * @brief A thread-safe buffer pool for reducing memory allocation overhead
 *        in high-frequency UDP packet broadcasting.
 * 
 * The pool maintains a stack of reusable buffers. When a buffer is requested,
 * it returns an existing buffer from the pool if available, or creates a new one.
 * Buffers are automatically returned to the pool when the shared_ptr is destroyed.
 */
class buffer_pool {
public:
    using buffer_t = std::vector<uint8_t>;
    using buffer_ptr = std::shared_ptr<buffer_t>;

    /**
     * @brief Construct a buffer pool with specified buffer size and initial capacity
     * @param buffer_size The fixed size of each buffer in the pool
     * @param initial_capacity Number of buffers to pre-allocate
     * @param max_pool_size Maximum number of buffers to keep in the pool
     */
    explicit buffer_pool(size_t buffer_size, size_t initial_capacity = 16, size_t max_pool_size = 128)
        : _buffer_size(buffer_size)
        , _max_pool_size(max_pool_size)
    {
        // Pre-allocate buffers
        for (size_t i = 0; i < initial_capacity; ++i) {
            _pool.push(std::make_unique<buffer_t>(buffer_size));
        }
    }

    /**
     * @brief Acquire a buffer from the pool
     * @return A shared_ptr to a buffer that will be automatically returned to the pool
     */
    buffer_ptr acquire()
    {
        std::unique_ptr<buffer_t> buffer;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_pool.empty()) {
                buffer = std::move(_pool.top());
                _pool.pop();
            }
        }

        if (!buffer) {
            buffer = std::make_unique<buffer_t>(_buffer_size);
        }

        // Create a shared_ptr with custom deleter that returns the buffer to the pool
        return buffer_ptr(buffer.release(), [this](buffer_t* ptr) {
            return_buffer(std::unique_ptr<buffer_t>(ptr));
        });
    }

    /**
     * @brief Get the current number of buffers in the pool
     */
    size_t pool_size() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _pool.size();
    }

private:
    void return_buffer(std::unique_ptr<buffer_t> buffer)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pool.size() < _max_pool_size) {
            // Clear and return to pool for reuse
            buffer->clear();
            buffer->resize(_buffer_size);
            _pool.push(std::move(buffer));
        }
        // If pool is full, buffer will be deleted automatically
    }

    size_t _buffer_size;
    size_t _max_pool_size;
    mutable std::mutex _mutex;
    std::stack<std::unique_ptr<buffer_t>> _pool;
};

} // namespace audio_share

#endif // BUFFER_POOL_HPP
