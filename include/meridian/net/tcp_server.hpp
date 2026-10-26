#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

#include "meridian/net/protocol.hpp"

namespace meridian::net {

// POSIX loopback-only server. poll_once/run and destruction require one owner.
// port() is immutable and may be read after construction by a client thread.
class TcpServer final {
public:
    static constexpr std::size_t max_clients = 64;
    static constexpr std::size_t max_output_bytes = 65536;

    explicit TcpServer(std::uint16_t port = 0);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
    void poll_once(std::chrono::milliseconds timeout = std::chrono::milliseconds{50});
    void run(std::stop_token stop);

private:
    struct Client final {
        int socket;
        std::string input;
        std::string output;
        bool input_closed{false};
    };
    void accept_clients();
    [[nodiscard]] bool read_client(Client& client);
    [[nodiscard]] static bool write_client(Client& client);

    int listener_{-1};
    std::uint16_t port_{0};
    std::vector<Client> clients_;
    ProtocolSession session_;
};

} // namespace meridian::net
