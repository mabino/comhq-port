#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chq::net {

class RelayServer {
public:
    RelayServer(int port = 0);
    ~RelayServer();

    bool start();
    void stop();

    int port() const { return port_; }
    bool is_running() const { return running_.load(); }
    std::size_t active_rooms_count() const;

private:
    struct Session {
        int fd = -1;
        std::string room_code;
        bool is_host = false;
        std::shared_ptr<Session> peer;
        std::vector<std::uint8_t> rx_buf;
    };

    void accept_loop();
    void client_loop(std::shared_ptr<Session> session);
    void route_packet(std::shared_ptr<Session> sender, const std::uint8_t* data, std::size_t len);

    int listen_port_ = 0;
    int port_ = 0;
    int listen_fd_ = -1;

    std::atomic<bool> running_{false};
    std::thread listen_thread_;

    mutable std::mutex rooms_mutex_;
    std::map<std::string, std::shared_ptr<Session>> rooms_;  // room_code -> host session
    std::vector<std::thread> client_threads_;
};

}  // namespace chq::net
