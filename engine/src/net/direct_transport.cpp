#include "net/direct_transport.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace chq::net {

namespace {

void set_nonblocking(int fd) {
    if (fd < 0) return;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void set_nodelay(int fd) {
    if (fd < 0) return;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

}  // namespace

DirectSocketTransport::DirectSocketTransport(int listen_port)
    : role_(TransportRole::Host), port_(listen_port) {}

DirectSocketTransport::DirectSocketTransport(std::string remote_host, int remote_port)
    : role_(TransportRole::Guest), host_(std::move(remote_host)), port_(remote_port) {}

DirectSocketTransport::~DirectSocketTransport() {
    stop();
}

bool DirectSocketTransport::start() {
    if (running_.load()) return true;
    running_.store(true);
    state_.store(role_ == TransportRole::Host ? MultiplayerState::Hosting : MultiplayerState::Connecting);
    status_msg_ = (role_ == TransportRole::Host) ? "Listening on port " + std::to_string(port_)
                                                 : "Connecting to " + host_ + ":" + std::to_string(port_);
    worker_thread_ = std::thread(&DirectSocketTransport::worker_loop, this);
    return true;
}

void DirectSocketTransport::stop() {
    running_.store(false);
    close_sockets();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    state_.store(MultiplayerState::Disconnected);
    status_msg_ = "Disconnected";
}

void DirectSocketTransport::close_sockets() {
    if (client_fd_ >= 0) {
        ::shutdown(client_fd_, SHUT_RDWR);
        ::close(client_fd_);
        client_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

MultiplayerState DirectSocketTransport::state() const {
    return state_.load();
}

std::string DirectSocketTransport::status_description() const {
    return status_msg_;
}

std::string DirectSocketTransport::room_code() const {
    if (role_ == TransportRole::Host) {
        return "PORT:" + std::to_string(port_);
    } else {
        return host_ + ":" + std::to_string(port_);
    }
}

std::size_t DirectSocketTransport::send(const void* data, std::size_t len) {
    if (!data || len == 0 || state_.load() != MultiplayerState::Connected) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::lock_guard<std::mutex> lock(tx_mutex_);
    tx_buffer_.insert(tx_buffer_.end(), bytes, bytes + len);
    bytes_sent_ += len;
    return len;
}

std::size_t DirectSocketTransport::receive(void* buffer, std::size_t max_len) {
    if (!buffer || max_len == 0) return 0;
    std::lock_guard<std::mutex> lock(rx_mutex_);
    const std::size_t count = std::min(max_len, rx_buffer_.size());
    if (count == 0) return 0;

    auto* dst = static_cast<std::uint8_t*>(buffer);
    for (std::size_t i = 0; i < count; ++i) {
        dst[i] = rx_buffer_.front();
        rx_buffer_.pop_front();
    }
    bytes_recv_ += count;
    return count;
}

std::size_t DirectSocketTransport::available() const {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    return rx_buffer_.size();
}

void DirectSocketTransport::flush() {
    // Background worker flushes tx_buffer continuously
}

void DirectSocketTransport::worker_loop() {
    if (role_ == TransportRole::Host) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            state_.store(MultiplayerState::Faulted);
            status_msg_ = "Socket creation failed";
            return;
        }
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        set_nonblocking(listen_fd_);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port_));

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            state_.store(MultiplayerState::Faulted);
            status_msg_ = "Port bind failed (port " + std::to_string(port_) + ")";
            return;
        }

        if (listen(listen_fd_, 1) < 0) {
            state_.store(MultiplayerState::Faulted);
            status_msg_ = "Listen failed";
            return;
        }

        while (running_.load() && client_fd_ < 0) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (fd >= 0) {
                set_nonblocking(fd);
                set_nodelay(fd);
                client_fd_ = fd;
                state_.store(MultiplayerState::Connected);
                char ip_buf[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
                status_msg_ = std::string("Connected to ") + ip_buf;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } else {
        // Guest mode
        while (running_.load() && client_fd_ < 0) {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* res = nullptr;
            const std::string port_str = std::to_string(port_);
            if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res) == 0 && res) {
                int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
                if (fd >= 0) {
                    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
                        set_nonblocking(fd);
                        set_nodelay(fd);
                        client_fd_ = fd;
                        state_.store(MultiplayerState::Connected);
                        status_msg_ = "Connected to " + host_ + ":" + port_str;
                    } else {
                        ::close(fd);
                    }
                }
                freeaddrinfo(res);
            }
            if (client_fd_ >= 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    // Active I/O loop
    std::uint8_t temp_buf[2048];
    while (running_.load() && client_fd_ >= 0) {
        // 1. Drain TX buffer to socket
        {
            std::vector<std::uint8_t> out_chunk;
            {
                std::lock_guard<std::mutex> lock(tx_mutex_);
                if (!tx_buffer_.empty()) {
                    const std::size_t to_send = std::min<std::size_t>(tx_buffer_.size(), sizeof(temp_buf));
                    out_chunk.assign(tx_buffer_.begin(), tx_buffer_.begin() + to_send);
                    tx_buffer_.erase(tx_buffer_.begin(), tx_buffer_.begin() + to_send);
                }
            }
            if (!out_chunk.empty()) {
                ssize_t sent = ::send(client_fd_, out_chunk.data(), out_chunk.size(), 0);
                if (sent < 0) {
                    // Put unsent bytes back
                    std::lock_guard<std::mutex> lock(tx_mutex_);
                    tx_buffer_.insert(tx_buffer_.begin(), out_chunk.begin(), out_chunk.end());
                } else if (static_cast<std::size_t>(sent) < out_chunk.size()) {
                    std::lock_guard<std::mutex> lock(tx_mutex_);
                    tx_buffer_.insert(tx_buffer_.begin(), out_chunk.begin() + sent, out_chunk.end());
                }
            }
        }

        // 2. Read incoming data from socket
        ssize_t n = ::recv(client_fd_, temp_buf, sizeof(temp_buf), 0);
        if (n > 0) {
            std::lock_guard<std::mutex> lock(rx_mutex_);
            rx_buffer_.insert(rx_buffer_.end(), temp_buf, temp_buf + n);
        } else if (n == 0) {
            // Peer disconnected
            state_.store(MultiplayerState::Disconnected);
            status_msg_ = "Peer disconnected";
            break;
        } else {
            // n < 0, check errno
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                state_.store(MultiplayerState::Faulted);
                status_msg_ = "Connection reset";
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    close_sockets();
}

}  // namespace chq::net
