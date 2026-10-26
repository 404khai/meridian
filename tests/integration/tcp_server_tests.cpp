#include "meridian/net/tcp_server.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;
using meridian::net::TcpServer;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class Client final {
public:
    explicit Client(std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        require(fd_ >= 0, "create client socket");
        const timeval timeout{2, 0};
        require(::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
                    ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
                "bound client test I/O waits");
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        require(::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0,
                "suppress test-client SIGPIPE");
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        require(::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
                "connect to ephemeral loopback server");
    }
    ~Client() { ::close(fd_); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void send(std::string_view text) {
        while (!text.empty()) {
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const auto count = ::send(fd_, text.data(), text.size(), flags);
            if (count < 0 && errno == EINTR) { continue; }
            require(count > 0, "send client request");
            text.remove_prefix(static_cast<std::size_t>(count));
        }
    }
    std::string line() {
        std::string response;
        while (response.size() < 1024) {
            char character = 0;
            const auto count = ::recv(fd_, &character, 1, 0);
            if (count < 0 && errno == EINTR) { continue; }
            require(count == 1, "receive complete response before timeout");
            response += character;
            if (character == '\n') { return response; }
        }
        require(false, "response line bounded");
        return {};
    }
    void eof() {
        char byte = 0;
        const auto count = ::recv(fd_, &byte, 1, 0);
        require(count == 0 || (count < 0 && errno == ECONNRESET), "connection closed safely");
    }
    void half_close() { require(::shutdown(fd_, SHUT_WR) == 0, "half close client write side"); }
    void reset() {
        const linger immediate{1, 0};
        require(::setsockopt(fd_, SOL_SOCKET, SO_LINGER, &immediate, sizeof(immediate)) == 0,
                "configure client reset");
    }
private:
    int fd_{-1};
};

class RunningServer final {
public:
    RunningServer() : worker_([this](std::stop_token stop) {
        try { server.run(stop); }
        catch (...) { failure_ = std::current_exception(); }
    }) {}
    ~RunningServer() {
        worker_.request_stop();
        worker_.join();
        require(!failure_, "server thread completed without failure");
    }
    TcpServer server;
private:
    std::exception_ptr failure_;
    std::jthread worker_;
};

void framing_and_shared_book() {
    RunningServer fixture;
    Client first{fixture.server.port()};
    Client second{fixture.server.port()};
    first.send("LIMIT 1 SE"); // Fragmented request has no effect until LF.
    second.send("BOOK\n");
    require(second.line() == "BOOK NONE NONE 0\n", "incomplete input is isolated");
    first.send("LL 100 3\r\nPING\n");
    require(first.line() == "OK 1 3 0\n" && first.line() == "END\n" &&
                first.line() == "PONG\n", "fragmented and pipelined lines");
    second.send("LIMIT 2 BUY 100 2\nBOOK\n");
    require(second.line() == "OK 2 0 1\n" && second.line() == "FILL 2 1 100 2\n" &&
                second.line() == "END\n" && second.line() == "BOOK NONE 100 1\n",
            "clients share deterministic matching state");
    second.send("LIMIT 1 BUY 100 1\nLIMIT -3 BUY 100 2\nPING\n");
    require(second.line() == "REJECT 1\n" && second.line() == "ERR BAD_REQUEST\n" &&
                second.line() == "PONG\n", "errors do not desynchronize the stream");
    first.send("MARKET 3 BUY 2\n");
    first.half_close();
    require(first.line() == "OK 3 1 1\n" && first.line() == "FILL 3 1 100 1\n" &&
                first.line() == "END\n", "half close still receives queued replies");
    first.eof();
}

void disconnects_and_bad_clients() {
    RunningServer fixture;
    Client healthy{fixture.server.port()};
    healthy.send("LIMIT 1 BUY 100 5\n");
    require(healthy.line() == "OK 1 5 0\n" && healthy.line() == "END\n", "persistent maker");
    {
        Client incomplete{fixture.server.port()};
        incomplete.send("LIMIT 2 SELL 100 5");
        incomplete.half_close();
        incomplete.eof();
    }
    {
        Client oversized{fixture.server.port()};
        oversized.send(std::string(257, 'x'));
        oversized.eof();
    }
    {
        Client reset{fixture.server.port()};
        reset.send("PING\n");
        require(reset.line() == "PONG\n", "reset fixture connected");
        reset.send(std::string(100, ' ') + "PING\nPING\nPING\n");
        reset.reset();
    }
    Client stalled{fixture.server.port()};
    stalled.send("LIMIT 3 SELL"); // Leave a slow writer connected indefinitely.
    healthy.send("BOOK\nPING\n");
    require(healthy.line() == "BOOK 100 NONE 1\n" && healthy.line() == "PONG\n",
            "disconnect/reset/oversized/slow clients preserve engine availability and state");
}

void oversized_reply_keeps_executed_state() {
    RunningServer fixture;
    Client maker{fixture.server.port()};
    std::string batch;
    constexpr unsigned makers = 4500;
    for (unsigned i = 1; i <= makers; ++i) {
        batch += "LIMIT " + std::to_string(i) + " SELL 100 1\n";
    }
    maker.send(batch);
    for (unsigned i = 1; i <= makers; ++i) {
        require(maker.line() == "OK " + std::to_string(i) + " 1 0\n" && maker.line() == "END\n",
                "pipelined resting makers acknowledged");
    }
    Client healthy{fixture.server.port()};
    healthy.send("PING\n");
    require(healthy.line() == "PONG\n", "second client ready");
    maker.send("MARKET 5000 BUY 1000\n");
    healthy.send("PING\n");
    require(healthy.line() == "PONG\n", "reader delay does not block another client");
    require(maker.line() == "OK 5000 0 1000\n", "large bounded reply header");
    for (unsigned i = 1; i <= 1000; ++i) {
        require(maker.line() == "FILL 5000 " + std::to_string(i) + " 100 1\n",
                "large bounded response is delivered in FIFO order");
    }
    require(maker.line() == "END\n", "large bounded response complete");
    maker.send("MARKET 5001 BUY 3500\n");
    maker.eof();
    Client observer{fixture.server.port()};
    observer.send("BOOK\nPING\n");
    require(observer.line() == "BOOK NONE NONE 0\n" && observer.line() == "PONG\n",
            "oversized reply drops connection without reverting fills or stopping listener");
}

void client_capacity_is_bounded() {
    RunningServer fixture;
    std::vector<std::unique_ptr<Client>> clients;
    for (std::size_t i = 0; i < TcpServer::max_clients; ++i) {
        auto client = std::make_unique<Client>(fixture.server.port());
        client->send("PING\n");
        require(client->line() == "PONG\n", "client admitted within capacity");
        clients.push_back(std::move(client));
    }
    Client excess{fixture.server.port()};
    excess.eof();
    clients.front()->send("PING\nBOOK\n");
    require(clients.front()->line() == "PONG\n" &&
                clients.front()->line() == "BOOK NONE NONE 0\n",
            "excess connections do not disturb admitted clients or source state");
}

void startup_failure_and_stop() {
    TcpServer listener;
    bool failed = false;
    try { TcpServer conflicting{listener.port()}; }
    catch (const std::system_error&) { failed = true; }
    require(failed, "bind failure is reported without terminating process");
    bool invalid_timeout = false;
    try { listener.poll_once(-1ms); }
    catch (const std::invalid_argument&) { invalid_timeout = true; }
    require(invalid_timeout, "negative poll timeout rejected");
    const auto started = std::chrono::steady_clock::now();
    {
        RunningServer idle;
        Client incomplete{idle.server.port()};
        incomplete.send("LIMIT");
    }
    require(std::chrono::steady_clock::now() - started < 2s, "stop is bounded with an idle client");
}
} // namespace

int main() {
    framing_and_shared_book();
    disconnects_and_bad_clients();
    oversized_reply_keeps_executed_state();
    client_capacity_is_bounded();
    startup_failure_and_stop();
    std::cout << "All TCP integration tests passed\n";
}
