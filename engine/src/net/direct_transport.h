#pragma once

#include "net/multiplayer_transport.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace chq::net {

class DirectSocketTransport : public IMultiplayerTransport {
public:
    // Host configuration
    DirectSocketTransport(int listen_port);
    // Guest configuration
    DirectSocketTransport(std::string remote_host, int remote_port);
    ~DirectSocketTransport() override;

    bool start() override;
    void stop() override;

    MultiplayerState state() const override;
    TransportRole role() const override { return role_; }
    std::string status_description() const override;
    std::string room_code() const override;

    std::size_t send(const void* data, std::size_t len) override;
    std::size_t receive(void* buffer, std::size_t max_len) override;
    std::size_t available() const override;
    void flush() override;

    double round_trip_time_ms() const override { return rtt_ms_; }
    std::uint64_t bytes_sent() const override { return bytes_sent_; }
    std::uint64_t bytes_received() const override { return bytes_recv_; }

    int port() const { return port_; }
    const std::string& host() const { return host_; }

private:
    void worker_loop();
    void close_sockets();

    TransportRole role_ = TransportRole::None;
    std::string host_;
    int port_ = 7190;

    std::atomic<MultiplayerState> state_{MultiplayerState::Disconnected};
    std::string status_msg_ = "Disconnected";

    int listen_fd_ = -1;
    int client_fd_ = -1;

    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex rx_mutex_;
    std::deque<std::uint8_t> rx_buffer_;

    mutable std::mutex tx_mutex_;
    std::deque<std::uint8_t> tx_buffer_;

    std::atomic<std::uint64_t> bytes_sent_{0};
    std::atomic<std::uint64_t> bytes_recv_{0};
    std::atomic<double> rtt_ms_{0.0};
};

}  // namespace chq::net
