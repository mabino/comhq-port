#include "net/relay_transport.h"

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

constexpr std::uint8_t kFrameMagic = 0x55;
constexpr std::uint8_t kFrameTypeData = 0x01;
constexpr std::uint8_t kFrameTypePing = 0x02;
constexpr std::uint8_t kFrameTypePong = 0x03;

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

std::uint64_t current_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

RelayTransport::RelayTransport(std::string relay_host, int relay_port, std::string requested_room_code)
    : role_(TransportRole::Host),
      relay_host_(std::move(relay_host)),
      relay_port_(relay_port),
      room_code_(std::move(requested_room_code)) {}

RelayTransport::RelayTransport(std::string relay_host, int relay_port, std::string target_room_code, TransportRole role)
    : role_(role),
      relay_host_(std::move(relay_host)),
      relay_port_(relay_port),
      room_code_(std::move(target_room_code)) {}

RelayTransport::~RelayTransport() {
    stop();
}

bool RelayTransport::start() {
    if (running_.load()) return true;
    running_.store(true);
    state_.store(role_ == TransportRole::Host ? MultiplayerState::Hosting : MultiplayerState::Connecting);
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_msg_ = "Connecting to relay " + relay_host_ + ":" + std::to_string(relay_port_);
    }
    worker_thread_ = std::thread(&RelayTransport::worker_loop, this);
    return true;
}

void RelayTransport::stop() {
    running_.store(false);
    close_socket();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    state_.store(MultiplayerState::Disconnected);
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_msg_ = "Disconnected";
    }
}

void RelayTransport::close_socket() {
    if (sock_fd_ >= 0) {
        ::shutdown(sock_fd_, SHUT_RDWR);
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
}

MultiplayerState RelayTransport::state() const {
    return state_.load();
}

std::string RelayTransport::status_description() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_msg_;
}

std::string RelayTransport::room_code() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return room_code_;
}

std::size_t RelayTransport::send(const void* data, std::size_t len) {
    if (!data || len == 0 || state_.load() != MultiplayerState::Connected) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(data);

    // Frame the data: [0x55][0x01][len_hi][len_lo][payload]
    std::vector<std::uint8_t> frame(4 + len);
    frame[0] = kFrameMagic;
    frame[1] = kFrameTypeData;
    frame[2] = static_cast<std::uint8_t>((len >> 8) & 0xFF);
    frame[3] = static_cast<std::uint8_t>(len & 0xFF);
    std::memcpy(&frame[4], bytes, len);

    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        tx_buffer_.insert(tx_buffer_.end(), frame.begin(), frame.end());
    }
    bytes_sent_ += len;
    return len;
}

std::size_t RelayTransport::receive(void* buffer, std::size_t max_len) {
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

std::size_t RelayTransport::available() const {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    return rx_buffer_.size();
}

void RelayTransport::flush() {
    // Non-blocking worker drains tx_buffer_ continuously
}

bool RelayTransport::process_incoming_frame(std::vector<std::uint8_t>& stream_buf) {
    while (stream_buf.size() >= 4) {
        if (stream_buf[0] != kFrameMagic) {
            // Drop invalid byte until sync byte found
            stream_buf.erase(stream_buf.begin());
            continue;
        }

        const std::uint8_t type = stream_buf[1];
        const std::size_t payload_len = (static_cast<std::size_t>(stream_buf[2]) << 8) | stream_buf[3];

        if (stream_buf.size() < 4 + payload_len) {
            // Need more bytes for full frame
            return true;
        }

        const std::uint8_t* payload = &stream_buf[4];
        if (type == kFrameTypeData) {
            std::lock_guard<std::mutex> lock(rx_mutex_);
            rx_buffer_.insert(rx_buffer_.end(), payload, payload + payload_len);
        } else if (type == kFrameTypePing && payload_len >= 8) {
            // Echo ping back as pong
            std::vector<std::uint8_t> pong(12);
            pong[0] = kFrameMagic;
            pong[1] = kFrameTypePong;
            pong[2] = 0;
            pong[3] = 8;
            std::memcpy(&pong[4], payload, 8);
            std::lock_guard<std::mutex> lock(tx_mutex_);
            tx_buffer_.insert(tx_buffer_.end(), pong.begin(), pong.end());
        } else if (type == kFrameTypePong && payload_len >= 8) {
            std::uint64_t sent_ts = 0;
            std::memcpy(&sent_ts, payload, 8);
            const std::uint64_t now_ts = current_timestamp_ms();
            if (now_ts >= sent_ts) {
                rtt_ms_.store(static_cast<double>(now_ts - sent_ts));
            }
        }

        stream_buf.erase(stream_buf.begin(), stream_buf.begin() + 4 + payload_len);
    }
    return true;
}

void RelayTransport::worker_loop() {
    // 1. Connect to Relay Server
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(relay_port_);

    if (getaddrinfo(relay_host_.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        state_.store(MultiplayerState::Faulted);
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_msg_ = "Unable to resolve relay server " + relay_host_;
        return;
    }

    sock_fd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_fd_ < 0) {
        freeaddrinfo(res);
        state_.store(MultiplayerState::Faulted);
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_msg_ = "Socket creation failed";
        return;
    }

    if (connect(sock_fd_, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close_socket();
        state_.store(MultiplayerState::Faulted);
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_msg_ = "Connection to relay " + relay_host_ + ":" + port_str + " failed";
        return;
    }
    freeaddrinfo(res);
    set_nodelay(sock_fd_);

    // 2. Perform Handshake
    if (role_ == TransportRole::Host) {
        std::string cmd = room_code_.empty() ? "CREATE\n" : "CREATE " + room_code_ + "\n";
        ::send(sock_fd_, cmd.data(), cmd.size(), 0);

        char resp_buf[128];
        std::size_t r_idx = 0;
        bool created = false;
        while (running_.load() && !created) {
            char ch;
            if (::recv(sock_fd_, &ch, 1, 0) <= 0) break;
            if (ch == '\n' || ch == '\r') {
                if (r_idx > 0) {
                    resp_buf[r_idx] = '\0';
                    std::string line(resp_buf);
                    if (line.rfind("CREATED ", 0) == 0) {
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        room_code_ = line.substr(8);
                        state_.store(MultiplayerState::Hosting);
                        status_msg_ = "Room " + room_code_ + " created; waiting for opponent to join...";
                        created = true;
                    }
                    r_idx = 0;
                }
            } else if (r_idx + 1 < sizeof(resp_buf)) {
                resp_buf[r_idx++] = ch;
            }
        }
        if (!created) {
            close_socket();
            state_.store(MultiplayerState::Faulted);
            return;
        }

        // Wait for PEER_JOINED notification
        bool peer_joined = false;
        while (running_.load() && !peer_joined) {
            char ch;
            if (::recv(sock_fd_, &ch, 1, 0) <= 0) break;
            if (ch == '\n' || ch == '\r') {
                if (r_idx > 0) {
                    resp_buf[r_idx] = '\0';
                    if (std::strcmp(resp_buf, "PEER_JOINED") == 0) {
                        peer_joined = true;
                        state_.store(MultiplayerState::Connected);
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        status_msg_ = "Opponent connected in Room " + room_code_;
                    }
                    r_idx = 0;
                }
            } else if (r_idx + 1 < sizeof(resp_buf)) {
                resp_buf[r_idx++] = ch;
            }
        }
        if (!peer_joined) {
            close_socket();
            state_.store(MultiplayerState::Disconnected);
            return;
        }
    } else {
        // Guest mode: JOIN
        std::string cmd = "JOIN " + room_code_ + "\n";
        ::send(sock_fd_, cmd.data(), cmd.size(), 0);

        char resp_buf[128];
        std::size_t r_idx = 0;
        bool joined = false;
        while (running_.load() && !joined) {
            char ch;
            if (::recv(sock_fd_, &ch, 1, 0) <= 0) break;
            if (ch == '\n' || ch == '\r') {
                if (r_idx > 0) {
                    resp_buf[r_idx] = '\0';
                    std::string line(resp_buf);
                    if (line.rfind("JOINED ", 0) == 0) {
                        joined = true;
                        state_.store(MultiplayerState::Connected);
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        status_msg_ = "Connected to opponent in Room " + room_code_;
                    } else if (line.rfind("ERROR", 0) == 0) {
                        state_.store(MultiplayerState::Faulted);
                        std::lock_guard<std::mutex> lock(status_mutex_);
                        status_msg_ = "Room not found or game already full: " + room_code_;
                        close_socket();
                        return;
                    }
                    r_idx = 0;
                }
            } else if (r_idx + 1 < sizeof(resp_buf)) {
                resp_buf[r_idx++] = ch;
            }
        }
        if (!joined) {
            close_socket();
            state_.store(MultiplayerState::Faulted);
            return;
        }
    }

    set_nonblocking(sock_fd_);

    // 3. Relayed Binary Data Loop with Ping Keepalive
    std::vector<std::uint8_t> in_stream;
    std::uint8_t read_chunk[2048];
    auto last_ping_time = std::chrono::steady_clock::now();

    while (running_.load() && sock_fd_ >= 0) {
        // Send periodic Ping (every 2 seconds)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time).count() >= 2) {
            last_ping_time = now;
            std::uint64_t ts = current_timestamp_ms();
            std::vector<std::uint8_t> ping(12);
            ping[0] = kFrameMagic;
            ping[1] = kFrameTypePing;
            ping[2] = 0;
            ping[3] = 8;
            std::memcpy(&ping[4], &ts, 8);
            std::lock_guard<std::mutex> lock(tx_mutex_);
            tx_buffer_.insert(tx_buffer_.end(), ping.begin(), ping.end());
        }

        // Drain TX buffer
        {
            std::vector<std::uint8_t> to_send;
            {
                std::lock_guard<std::mutex> lock(tx_mutex_);
                if (!tx_buffer_.empty()) {
                    const std::size_t take = std::min<std::size_t>(tx_buffer_.size(), sizeof(read_chunk));
                    to_send.assign(tx_buffer_.begin(), tx_buffer_.begin() + take);
                    tx_buffer_.erase(tx_buffer_.begin(), tx_buffer_.begin() + take);
                }
            }
            if (!to_send.empty()) {
                ssize_t sent = ::send(sock_fd_, to_send.data(), to_send.size(), 0);
                if (sent < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        state_.store(MultiplayerState::Faulted);
                        break;
                    }
                    std::lock_guard<std::mutex> lock(tx_mutex_);
                    tx_buffer_.insert(tx_buffer_.begin(), to_send.begin(), to_send.end());
                } else if (static_cast<std::size_t>(sent) < to_send.size()) {
                    std::lock_guard<std::mutex> lock(tx_mutex_);
                    tx_buffer_.insert(tx_buffer_.begin(), to_send.begin() + sent, to_send.end());
                }
            }
        }

        // Read incoming from socket
        ssize_t n = ::recv(sock_fd_, read_chunk, sizeof(read_chunk), 0);
        if (n > 0) {
            in_stream.insert(in_stream.end(), read_chunk, read_chunk + n);
            process_incoming_frame(in_stream);
        } else if (n == 0) {
            state_.store(MultiplayerState::Disconnected);
            std::lock_guard<std::mutex> lock(status_mutex_);
            status_msg_ = "Disconnected from relay";
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                state_.store(MultiplayerState::Faulted);
                std::lock_guard<std::mutex> lock(status_mutex_);
                status_msg_ = "Relay connection error";
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    close_socket();
}

}  // namespace chq::net
