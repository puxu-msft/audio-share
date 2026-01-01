#ifndef UTIL_HPP
#define UTIL_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace util
{
    // Network constants
    constexpr uint16_t DEFAULT_PORT = 65530;
    constexpr uint16_t MIN_PORT = 1;
    constexpr uint16_t MAX_PORT = 65535;

    bool is_newer_version(const std::string& lhs, const std::string& rhs);

    // empty substring will be removed
    std::vector<std::string> split_string(const std::string& src, char delimiter);

    // Port validation result
    struct PortValidationResult {
        bool is_valid;
        uint16_t port;
        std::wstring error_message;
    };

    // Validate and parse port number from string
    PortValidationResult validate_port(const std::wstring& port_str);
}

#endif // !UTIL_HPP
