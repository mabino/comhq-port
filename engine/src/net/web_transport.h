#pragma once

#include "net/multiplayer_transport.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace chq::net {

class WebTransport : public IMultiplayerTransport {
public:
    WebTransport(TransportRole role = TransportRole::Host, std::string room_code = "");
    ~WebTransport() override;

    bool start() override;
    void stop() override;

    MultiplayerState state() const override;
    TransportRole role() const override { return role_; }
    std::string status_description() const override;
    std::string room_code() const override;

    std::size_t send(const void* data, std::size_t len) override;
    std::size_t receive(void* buffer, std::size_t max_len) override;
    std::size_t available() const override;
    void flush() override {}

    double round_trip_time_ms() const override { return rtt_ms_.load(); }
    std::uint64_t bytes_sent() const override { return bytes_sent_.load(); }
    std::uint64_t bytes_received() const override { return bytes_recv_.load(); }

    // State updates called by JavaScript bridge
    void set_peer_connected(bool connected);
    void set_room_code(const std::string& code);
    void handle_rx_frame(const std::uint8_t* data, std::size_t len);

private:
    TransportRole role_ = TransportRole::Host;
    std::string room_code_;
    std::atomic<MultiplayerState> state_{MultiplayerState::Disconnected};
    mutable std::mutex mutex_;
    std::string status_msg_ = "Disconnected";

    std::deque<std::uint8_t> rx_buffer_;
    std::atomic<std::uint64_t> bytes_sent_{0};
    std::atomic<std::uint64_t> bytes_recv_{0};
    std::atomic<double> rtt_ms_{0.0};
};

}  // namespace chq::net
