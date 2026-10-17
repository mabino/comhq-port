#pragma once

#include "net/multiplayer_transport.h"

#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace chq::net {

class LoopbackTransport : public IMultiplayerTransport {
public:
    LoopbackTransport(TransportRole role, std::string room_code = "LOCAL-TEST");
    ~LoopbackTransport() override;

    // Connects two loopback transports to each other
    static std::pair<std::shared_ptr<LoopbackTransport>, std::shared_ptr<LoopbackTransport>> create_pair(
        const std::string& room_code = "LOCAL-TEST");

    void link_peer(std::shared_ptr<LoopbackTransport> peer);

    bool start() override;
    void stop() override;

    MultiplayerState state() const override;
    TransportRole role() const override { return role_; }
    std::string status_description() const override;
    std::string room_code() const override { return room_code_; }

    std::size_t send(const void* data, std::size_t len) override;
    std::size_t receive(void* buffer, std::size_t max_len) override;
    std::size_t available() const override;
    void flush() override;

    double round_trip_time_ms() const override { return 1.0; }
    std::uint64_t bytes_sent() const override { return bytes_sent_; }
    std::uint64_t bytes_received() const override { return bytes_recv_; }

    // Direct byte injection into inbound buffer
    void inject_bytes(const void* data, std::size_t len);

private:
    TransportRole role_ = TransportRole::None;
    std::string room_code_;
    std::weak_ptr<LoopbackTransport> peer_;

    mutable std::mutex mutex_;
    std::deque<std::uint8_t> rx_buffer_;
    MultiplayerState state_ = MultiplayerState::Disconnected;
    std::uint64_t bytes_sent_ = 0;
    std::uint64_t bytes_recv_ = 0;
};

}  // namespace chq::net
