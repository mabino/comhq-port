#pragma once

#include "net/multiplayer_transport.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace chq::net {

class RelayTransport : public IMultiplayerTransport {
public:
    // Host configuration: connects to relay and creates room
    RelayTransport(std::string relay_host, int relay_port, std::string requested_room_code = "");
    // Guest configuration: connects to relay and joins room
    RelayTransport(std::string relay_host, int relay_port, std::string target_room_code, TransportRole role);
    ~RelayTransport() override;

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

    double round_trip_time_ms() const override { return rtt_ms_.load(); }
    std::uint64_t bytes_sent() const override { return bytes_sent_.load(); }
    std::uint64_t bytes_received() const override { return bytes_recv_.load(); }

    const std::string& relay_host() const { return relay_host_; }
    int relay_port() const { return relay_port_; }

private:
    void worker_loop();
    void close_socket();
    bool process_incoming_frame(std::vector<std::uint8_t>& stream_buf);

    TransportRole role_ = TransportRole::None;
    std::string relay_host_;
    int relay_port_ = 7190;
    std::string room_code_;

    std::atomic<MultiplayerState> state_{MultiplayerState::Disconnected};
    mutable std::mutex status_mutex_;
    std::string status_msg_ = "Disconnected";

    int sock_fd_ = -1;
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
