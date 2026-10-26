#include "meridian/net/tcp_server.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace meridian::net {
namespace {

bool configure(int socket) {
    const int flags = ::fcntl(socket, F_GETFL, 0);
    const int descriptor_flags = ::fcntl(socket, F_GETFD, 0);
    if (flags < 0 || descriptor_flags < 0 ||
        ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0 ||
        ::fcntl(socket, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        return false;
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        return false;
    }
#endif
    return true;
}

[[noreturn]] void socket_error(const char* operation) {
    throw std::system_error{errno, std::generic_category(), operation};
}

} // namespace

TcpServer::TcpServer(std::uint16_t port) {
    // Reserve before acquiring descriptors, avoiding allocation during accept.
    clients_.reserve(max_clients);
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) {
        socket_error("socket");
    }
    try {
        if (!configure(listener_)) {
            socket_error("configure listener");
        }
        const int enabled = 1;
        if (::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
            socket_error("setsockopt");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (::bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
            ::listen(listener_, static_cast<int>(max_clients)) < 0) {
            socket_error("bind/listen");
        }
        socklen_t length = sizeof(address);
        if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
            socket_error("getsockname");
        }
        port_ = ntohs(address.sin_port);
    } catch (...) {
        ::close(listener_);
        throw;
    }
}

TcpServer::~TcpServer() {
    for (const auto& client : clients_) {
        ::close(client.socket);
    }
    ::close(listener_);
}

void TcpServer::accept_clients() {
    for (std::size_t attempt = 0; attempt < max_clients; ++attempt) {
        const int socket = ::accept(listener_, nullptr, nullptr);
        if (socket < 0) {
            // Includes EINTR/EAGAIN and transient resource failures. Retry on
            // the next poll; no accepted order is affected by accept failures.
            return;
        }
        if (clients_.size() == max_clients || !configure(socket)) {
            ::close(socket);
            continue;
        }
        clients_.push_back(Client{socket, {}, {}, false});
    }
}

bool TcpServer::read_client(Client& client) {
    std::array<char, 4096> buffer{};
    const auto received = ::recv(client.socket, buffer.data(), buffer.size(), 0);
    if (received < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
    if (received == 0) {
        client.input_closed = true;
        client.input.clear(); // Incomplete requests never enter the engine.
        return true;
    }
    for (std::size_t index = 0; index < static_cast<std::size_t>(received); ++index) {
        const char character = buffer[index];
        if (character == '\n') {
            auto response = session_.execute(client.input, max_output_bytes - client.output.size());
            client.input.clear();
            if (!response) {
                return false;
            }
            client.output += *response;
        } else {
            if (client.input.size() == ProtocolSession::max_line_bytes) {
                return false;
            }
            client.input.push_back(character);
        }
    }
    return true;
}

bool TcpServer::write_client(Client& client) {
    if (client.output.empty()) {
        return true;
    }
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    const auto sent = ::send(client.socket, client.output.data(), client.output.size(), flags);
    if (sent < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
    if (sent == 0) {
        return false;
    }
    client.output.erase(0, static_cast<std::size_t>(sent));
    return true;
}

void TcpServer::poll_once(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > INT_MAX) {
        throw std::invalid_argument{"invalid poll timeout"};
    }
    std::array<pollfd, max_clients + 1> descriptors{};
    descriptors[0] = pollfd{listener_, POLLIN, 0};
    const auto polled_clients = clients_.size();
    for (std::size_t i = 0; i < polled_clients; ++i) {
        const auto& client = clients_[i];
        descriptors[i + 1] = pollfd{client.socket, static_cast<short>(
            (client.input_closed ? 0 : POLLIN) | (client.output.empty() ? 0 : POLLOUT)), 0};
    }
    const auto ready = ::poll(descriptors.data(), static_cast<nfds_t>(polled_clients + 1),
                              static_cast<int>(timeout.count()));
    if (ready < 0) {
        if (errno == EINTR) {
            return;
        }
        socket_error("poll");
    }
    if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        throw std::runtime_error{"TCP listener failed"};
    }
    // Reverse iteration permits erasure without invalidating unprocessed indices.
    for (std::size_t i = polled_clients; i > 0; --i) {
        auto& client = clients_[i - 1];
        const auto events = descriptors[i].revents;
        bool keep = (events & (POLLERR | POLLNVAL)) == 0;
        if (keep && !client.input_closed && (events & (POLLIN | POLLHUP)) != 0) {
            keep = read_client(client);
        }
        if (keep) {
            keep = write_client(client);
        }
        if (!keep || (client.input_closed && client.output.empty())) {
            ::close(client.socket);
            clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i - 1));
        }
    }
    if ((descriptors[0].revents & POLLIN) != 0) {
        accept_clients();
    }
}

void TcpServer::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        poll_once();
    }
}

} // namespace meridian::net
