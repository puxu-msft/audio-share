#include "config.h"
#include "audio_manager.hpp"
#include "network_manager.hpp"
#include "websocket_manager.hpp"
#include "constants.hpp"

#include <cxxopts.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <limits>

using string = std::string;
using namespace audio_share::constants;

int main(int argc, char* argv[])
{
    auto default_address = network_manager::get_default_address();

    std::string help_string = "Example:\n";
    help_string += fmt::format("  {} -b\n", AUDIO_SHARE_BIN_NAME);
    help_string += fmt::format("  {} --bind={}\n", AUDIO_SHARE_BIN_NAME, default_address.empty() ? "192.168.3.2": default_address);
    help_string += fmt::format("  {} --bind={} --encoding=f32 --channels=2 --sample-rate=48000\n", AUDIO_SHARE_BIN_NAME, default_address.empty() ? "192.168.3.2": default_address);
    help_string += fmt::format("  {} -l\n", AUDIO_SHARE_BIN_NAME);
    help_string += fmt::format("  {} --list-encoding\n", AUDIO_SHARE_BIN_NAME);
    cxxopts::Options options(AUDIO_SHARE_BIN_NAME, help_string);

    // clang-format off
    options.add_options()
        ("h,help", "Print usage")
        ("l,list-endpoint", "List available endpoints")
        ("b,bind", "The server bind address. If not set, will use default", cxxopts::value<string>()->implicit_value(default_address), "[host][:<port>]")
        ("w,websocket-port", "WebSocket server port for web browser clients (default: main port + 1)", cxxopts::value<int>()->default_value("0"), "[port]")
        ("e,endpoint", "Specify the endpoint id. If not set or set \"default\", will use default", cxxopts::value<string>()->default_value("default"), "[endpoint]")
        ("encoding", "Specify the capture encoding. If not set or set \"default\", will use default", cxxopts::value<audio_manager::encoding_t>()->default_value("default"), "[encoding]")
        ("list-encoding", "List available encoding")
        ("channels", "Specify the capture channels. If not set or set \"0\", will use default", cxxopts::value<int>()->default_value("0"), "[channels]")
        ("sample-rate", "Specify the capture sample rate(Hz). If not set or set \"0\", will use default. The common values are 44100, 48000, etc.", cxxopts::value<int>()->default_value("0"), "[sample_rate]")
        ("V,verbose", "Set log level to \"trace\"")
        ("v,version", "Show version")
        ;
    // clang-format on

    try {
        auto result = options.parse(argc, argv);
        if (result.count("help")) {
            std::cout << options.help();
            return EXIT_SUCCESS;
        }

        if (result.count("version")) {
            fmt::println("{}\nversion: {}\nurl: {}\n", AUDIO_SHARE_BIN_NAME, AUDIO_SHARE_VERSION, AUDIO_SHARE_HOMEPAGE);
            return EXIT_SUCCESS;
        }

        if (result.count("verbose")) {
            spdlog::set_level(spdlog::level::trace);
        }

        if (result.count("list-endpoint")) {
            auto audio_manager = std::make_shared<class audio_manager>();
            auto endpoint_list = audio_manager->get_endpoint_list();
            auto default_endpoint = audio_manager->get_default_endpoint();
            auto s = fmt::format("endpoint list:\n");
            for (auto&& [id, name] : endpoint_list) {
                s += fmt::format("\t{} id: {:4} name: {}\n", (id == default_endpoint ? '*' : ' '), id, name);
            }
            s += fmt::format("total: {}\n", endpoint_list.size());
            std::cout << s;
            return EXIT_SUCCESS;
        }

        if (result.count("list-encoding")) {
            std::vector<std::pair<string, string>> array = {
                { "default", "Default encoding" },
                { "f32", "32 bit floating-point PCM" },
                { "s8", "8 bit integer PCM" },
                { "s16", "16 bit integer PCM" },
                { "s24", "24 bit integer PCM" },
                { "s32", "32 bit integer PCM" },
            };
            fmt::println("encoding list:");
            for(auto&& e : array) {
                fmt::println("\t{}\t\t{}", e.first, e.second);
            }
            return EXIT_SUCCESS;
        }

        if (result.count("bind")) {
            auto s = result["bind"].as<string>();
            size_t pos = s.find(':');
            string host = s.substr(0, pos);
            uint16_t port;
            if (pos == string::npos) {
                port = DEFAULT_PORT;
            } else {
                string port_str = s.substr(pos + 1);
                if (port_str.empty()) {
                    spdlog::error("Port number cannot be empty");
                    return EXIT_FAILURE;
                }
                
                int port_val;
                try {
                    size_t parsed_chars = 0;
                    port_val = std::stoi(port_str, &parsed_chars);
                    if (parsed_chars != port_str.size()) {
                        spdlog::error("Invalid port number format: '{}'", port_str);
                        return EXIT_FAILURE;
                    }
                } catch (const std::invalid_argument&) {
                    spdlog::error("Invalid port number: '{}'", port_str);
                    return EXIT_FAILURE;
                } catch (const std::out_of_range&) {
                    spdlog::error("Port number out of range: '{}'", port_str);
                    return EXIT_FAILURE;
                }
                
                if (port_val < MIN_PORT || port_val > MAX_PORT) {
                    spdlog::error("Port must be between {} and {}, got {}", MIN_PORT, MAX_PORT, port_val);
                    return EXIT_FAILURE;
                }
                port = static_cast<uint16_t>(port_val);
            }

            if (host.empty()) {
                host = network_manager::get_default_address();
                if (host.empty()) {
                    spdlog::error("No valid network address found. Please specify a host address.");
                    return EXIT_FAILURE;
                }
            }

            auto audio_manager = std::make_shared<class audio_manager>();

            audio_manager::capture_config capture_config;

            capture_config.endpoint_id = result["endpoint"].as<string>();
            capture_config.encoding = result["encoding"].as<audio_manager::encoding_t>();
            capture_config.channels = result["channels"].as<int>();
            capture_config.sample_rate = result["sample-rate"].as<int>();

            auto network_manager = std::make_shared<class network_manager>(audio_manager);

            // Start WebSocket server for web clients
            auto ws_manager = std::make_shared<class websocket_manager>(audio_manager);
            int ws_port_val = result["websocket-port"].as<int>();
            uint16_t ws_port = (ws_port_val > 0 && ws_port_val <= MAX_PORT) 
                ? static_cast<uint16_t>(ws_port_val) 
                : static_cast<uint16_t>(port + 1);
            
            spdlog::info("Starting WebSocket server on {}:{}", host, ws_port);
            ws_manager->start_server(host, ws_port, capture_config);
            
            // Register WebSocket manager as additional broadcaster
            network_manager->add_broadcaster(ws_manager);

            network_manager->start_server(host, port, capture_config);
            network_manager->wait_server();
            ws_manager->stop_server();

            return EXIT_SUCCESS;
        }

        // no arg
        std::cerr << options.help();
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << e.what() << '\n'
                  << options.help();
        return EXIT_FAILURE;
    } catch (const std::exception& ec) {
        std::cerr << ec.what() << '\n';
        return EXIT_FAILURE;
    }
}
