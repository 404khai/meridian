#include "meridian/net/tcp_server.hpp"

#include <charconv>
#include <csignal>
#include <exception>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
}

int main(int argc, char** argv) {
    unsigned port = 9000;
    if (argc > 2) {
        std::cerr << "usage: meridian_tcp_server [port]\n";
        return 1;
    }
    if (argc == 2) {
        const std::string_view value{argv[1]};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || port > 65535) {
            std::cerr << "invalid TCP port\n";
            return 1;
        }
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    try {
        meridian::net::TcpServer server{static_cast<std::uint16_t>(port)};
        std::cout << "listening on 127.0.0.1:" << server.port() << std::endl;
        while (stopped == 0) {
            server.poll_once();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
