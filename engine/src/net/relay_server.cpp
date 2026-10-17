#include "net/relay_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <random>

namespace chq::net {

namespace {

void set_sock_opts(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

std::string generate_random_room_code() {
    static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    std::string s = "WAR-";
    for (int i = 0; i < 4; ++i) {
        s += chars[dis(gen)];
    }
    return s;
}

}  // namespace

RelayServer::RelayServer(int port) : listen_port_(port), port_(port) {}

RelayServer::~RelayServer() {
    stop();
}

bool RelayServer::start() {
    if (running_.load()) return true;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(listen_port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 16) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // Resolve assigned port if dynamically chosen (port == 0)
    sockaddr_in assigned{};
    socklen_t len = sizeof(assigned);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&assigned), &len) == 0) {
        port_ = ntohs(assigned.sin_port);
    }

    int flags = fcntl(listen_fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    running_.store(true);
    listen_thread_ = std::thread(&RelayServer::accept_loop, this);
    return true;
}

void RelayServer::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        for (auto& [code, host_sess] : rooms_) {
            if (host_sess && host_sess->fd >= 0) {
                ::shutdown(host_sess->fd, SHUT_RDWR);
                ::close(host_sess->fd);
                host_sess->fd = -1;
            }
            if (host_sess && host_sess->peer && host_sess->peer->fd >= 0) {
                ::shutdown(host_sess->peer->fd, SHUT_RDWR);
                ::close(host_sess->peer->fd);
                host_sess->peer->fd = -1;
            }
        }
        rooms_.clear();
    }

    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }

    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    client_threads_.clear();
}

std::size_t RelayServer::active_rooms_count() const {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    return rooms_.size();
}

void RelayServer::accept_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (fd < 0) {
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }

        set_sock_opts(fd);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100'000;  // 100ms
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        auto session = std::make_shared<Session>();
        session->fd = fd;

        std::lock_guard<std::mutex> lock(rooms_mutex_);
        client_threads_.emplace_back(&RelayServer::client_loop, this, session);
    }
}

void RelayServer::client_loop(std::shared_ptr<Session> session) {
    char line_buf[256];
    std::size_t line_idx = 0;
    bool handshaked = false;

    // Phase 1: Text Handshake (CREATE or JOIN)
    while (running_.load() && !handshaked) {
        char ch;
        ssize_t n = ::recv(session->fd, &ch, 1, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        if (n == 0) break;

        if (ch == '\n' || ch == '\r') {
            if (line_idx > 0) {
                line_buf[line_idx] = '\0';
                std::string cmd(line_buf);

                if (cmd.rfind("CREATE", 0) == 0) {
                    std::string code;
                    if (cmd.size() > 7) {
                        code = cmd.substr(7);
                    } else {
                        code = generate_random_room_code();
                    }
                    session->room_code = code;
                    session->is_host = true;

                    {
                        std::lock_guard<std::mutex> lock(rooms_mutex_);
                        rooms_[code] = session;
                    }

                    std::string resp = "CREATED " + code + "\n";
                    ::send(session->fd, resp.data(), resp.size(), 0);
                    handshaked = true;
                } else if (cmd.rfind("JOIN", 0) == 0) {
                    std::string code;
                    if (cmd.size() > 5) {
                        code = cmd.substr(5);
                    }
                    session->room_code = code;
                    session->is_host = false;

                    std::shared_ptr<Session> host_sess;
                    {
                        std::lock_guard<std::mutex> lock(rooms_mutex_);
                        auto it = rooms_.find(code);
                        if (it != rooms_.end()) {
                            host_sess = it->second;
                        }
                    }

                    if (host_sess && host_sess->fd >= 0) {
                        session->peer = host_sess;
                        host_sess->peer = session;

                        std::string resp = "JOINED " + code + "\n";
                        ::send(session->fd, resp.data(), resp.size(), 0);

                        std::string notify = "PEER_JOINED\n";
                        ::send(host_sess->fd, notify.data(), notify.size(), 0);
                        handshaked = true;
                    } else {
                        std::string err = "ERROR ROOM_NOT_FOUND\n";
                        ::send(session->fd, err.data(), err.size(), 0);
                        break;
                    }
                }
                line_idx = 0;
            }
        } else if (line_idx + 1 < sizeof(line_buf)) {
            line_buf[line_idx++] = ch;
        }
    }

    // Phase 2: Binary Relaying
    std::uint8_t buf[2048];
    while (running_.load() && handshaked) {
        ssize_t n = ::recv(session->fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        if (n == 0) break;
        route_packet(session, buf, static_cast<std::size_t>(n));
    }

    // Cleanup session
    if (session->fd >= 0) {
        ::close(session->fd);
        session->fd = -1;
    }

    if (session->peer && session->peer->fd >= 0) {
        const char msg[] = "PEER_LEFT\n";
        ::send(session->peer->fd, msg, sizeof(msg) - 1, 0);
        session->peer->peer.reset();
    }

    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        if (session->is_host) {
            rooms_.erase(session->room_code);
        }
    }
}

void RelayServer::route_packet(std::shared_ptr<Session> sender, const std::uint8_t* data, std::size_t len) {
    if (!sender || !sender->peer || sender->peer->fd < 0 || !data || len == 0) return;
    ::send(sender->peer->fd, data, len, 0);
}

}  // namespace chq::net
