#include "net/loopback_transport.h"

#include <algorithm>
#include <cstring>

namespace chq::net {

LoopbackTransport::LoopbackTransport(TransportRole role, std::string room_code)
    : role_(role), room_code_(std::move(room_code)) {}

LoopbackTransport::~LoopbackTransport() {
    stop();
}

std::pair<std::shared_ptr<LoopbackTransport>, std::shared_ptr<LoopbackTransport>>
LoopbackTransport::create_pair(const std::string& room_code) {
    auto host = std::make_shared<LoopbackTransport>(TransportRole::Host, room_code);
    auto guest = std::make_shared<LoopbackTransport>(TransportRole::Guest, room_code);
    host->link_peer(guest);
    guest->link_peer(host);
    return {host, guest};
}

void LoopbackTransport::link_peer(std::shared_ptr<LoopbackTransport> peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    peer_ = peer;
}

bool LoopbackTransport::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto p = peer_.lock();
    if (!p) {
        state_ = (role_ == TransportRole::Host) ? MultiplayerState::Hosting : MultiplayerState::Connecting;
        return true;
    }
    state_ = MultiplayerState::Connected;
    {
        std::lock_guard<std::mutex> peer_lock(p->mutex_);
        p->state_ = MultiplayerState::Connected;
    }
    return true;
}

void LoopbackTransport::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = MultiplayerState::Disconnected;
    auto p = peer_.lock();
    if (p) {
        std::lock_guard<std::mutex> peer_lock(p->mutex_);
        if (p->state_ == MultiplayerState::Connected) {
            p->state_ = MultiplayerState::Disconnected;
        }
    }
}

MultiplayerState LoopbackTransport::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string LoopbackTransport::status_description() const {
    switch (state()) {
        case MultiplayerState::Disconnected: return "Disconnected";
        case MultiplayerState::Hosting: return "Waiting for peer (Room: " + room_code_ + ")";
        case MultiplayerState::Connecting: return "Connecting to " + room_code_ + "...";
        case MultiplayerState::Connected: return "Connected to peer";
        case MultiplayerState::Faulted: return "Connection error";
    }
    return "Unknown";
}

std::size_t LoopbackTransport::send(const void* data, std::size_t len) {
    if (!data || len == 0) return 0;
    auto p = peer_.lock();
    if (!p) return 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != MultiplayerState::Connected) return 0;
        bytes_sent_ += len;
    }

    p->inject_bytes(data, len);
    return len;
}

void LoopbackTransport::inject_bytes(const void* data, std::size_t len) {
    if (!data || len == 0) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::lock_guard<std::mutex> lock(mutex_);
    rx_buffer_.insert(rx_buffer_.end(), bytes, bytes + len);
}

std::size_t LoopbackTransport::receive(void* buffer, std::size_t max_len) {
    if (!buffer || max_len == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
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

std::size_t LoopbackTransport::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rx_buffer_.size();
}

void LoopbackTransport::flush() {
    // In-memory writes are immediate
}

}  // namespace chq::net
