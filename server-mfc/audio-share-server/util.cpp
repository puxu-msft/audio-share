#include "util.hpp"
#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <cwctype>

namespace util {

    PortValidationResult validate_port(const std::wstring& port_str)
    {
        PortValidationResult result;
        result.is_valid = false;
        result.port = 0;

        // Check for empty string
        if (port_str.empty()) {
            result.error_message = L"Port number cannot be empty";
            return result;
        }

        // Check for whitespace only
        bool all_whitespace = std::all_of(port_str.begin(), port_str.end(), 
            [](wchar_t c) { return std::iswspace(c); });
        if (all_whitespace) {
            result.error_message = L"Port number cannot be whitespace";
            return result;
        }

        // Check for valid characters (digits only, with optional leading/trailing whitespace)
        std::wstring trimmed;
        bool started = false;
        for (wchar_t c : port_str) {
            if (!started && std::iswspace(c)) continue;
            started = true;
            if (std::iswspace(c)) break;
            if (!std::iswdigit(c)) {
                result.error_message = L"Port number must contain only digits";
                return result;
            }
            trimmed += c;
        }

        if (trimmed.empty()) {
            result.error_message = L"Port number cannot be empty";
            return result;
        }

        // Check for leading zeros (except for "0" itself)
        if (trimmed.length() > 1 && trimmed[0] == L'0') {
            result.error_message = L"Port number cannot have leading zeros";
            return result;
        }

        // Parse the number
        int port_val;
        try {
            port_val = std::stoi(trimmed);
        } catch (const std::invalid_argument&) {
            result.error_message = L"Invalid port number format";
            return result;
        } catch (const std::out_of_range&) {
            result.error_message = L"Port number is too large";
            return result;
        }

        // Validate range
        if (port_val < MIN_PORT || port_val > MAX_PORT) {
            result.error_message = L"Port must be between " + std::to_wstring(MIN_PORT) + 
                                   L" and " + std::to_wstring(MAX_PORT);
            return result;
        }

        result.is_valid = true;
        result.port = static_cast<uint16_t>(port_val);
        return result;
    }

    bool is_newer_version(const std::string& lhs, const std::string& rhs)
    {
        if (lhs.empty() || rhs.empty() || lhs[0] != 'v' || rhs[0] != 'v') {
            throw std::exception("is_newer_version: bad arguments");
        }

        auto lhs_substrs = split_string(lhs.substr(1), '.');
        auto rhs_substrs = split_string(rhs.substr(1), '.');

        if (lhs_substrs.size() != 3 || rhs_substrs.size() != 3) {
            throw std::exception("is_newer_version: bad arguments");
        }

        std::vector<int> lhs_vec, rhs_vec;
        auto to_int = [&](const std::string& s) {
            return std::stoi(s);
            };
        std::ranges::transform(lhs_substrs, std::back_inserter(lhs_vec), to_int);
        std::ranges::transform(rhs_substrs, std::back_inserter(rhs_vec), to_int);

        for (size_t i = 0; i < lhs_vec.size(); i++)
        {
            if (lhs_vec[i] == rhs_vec[i]) {
                continue;
            }
            else {
                return lhs_vec[i] > rhs_vec[i];
            }
        }

        return false;
    }

    // empty substring will be ignored
    std::vector<std::string> split_string(const std::string& src, char delimiter)
    {
        std::vector<std::string> result;
        size_t begin = 0, end = 0;
        while (begin < src.length()) {
            end = src.find(delimiter, begin);
            if (end == src.npos) {
                result.push_back(src.substr(begin, src.npos));
                break;
            }
            if (end - begin > 0) {
                result.push_back(src.substr(begin, end - begin));
            }
            begin = end + 1;
        }

        return result;
    }
}