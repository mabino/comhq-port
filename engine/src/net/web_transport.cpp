#include "net/web_transport.h"

#include "net/multiplayer_manager.h"

#include <algorithm>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(void, js_send_relay_frame, (const uint8_t* ptr, int len), {
    if (typeof Module !== 'undefined' && Module.chqRelaySend) {
        Module.chqRelaySend(HEAPU8.subarray(ptr, ptr + len));
    }
});

EM_JS(void, js_request_disconnect, (), {
    if (typeof Module !== 'undefined' && Module.chqRelayDisconnect) {
        Module.chqRelayDisconnect();
    }
});
#endif

namespace chq::net {

namespace {

constexpr std::uint8_t kFrameMagic = 0x55;
constexpr std::uint8_t kFrameTypeData = 0x01;
constexpr std::uint8_t kFrameTypePing = 0x02;
constexpr std::uint8_t kFrameTypePong = 0x03;

}  // namespace

WebTransport::WebTransport(TransportRole role, std::string room_code)
    : role_(role), room_code_(std::move(room_code)) {}

WebTransport::~WebTransport() {
    stop();
}

bool WebTransport::start() {
    state_.store(role_ == TransportRole::Host ? MultiplayerState::Hosting : MultiplayerState::Connecting);
    std::lock_guard<std::mutex> lock(mutex_);
    status_msg_ = (role_ == TransportRole::Host) ? "Hosting match" : "Connecting to match";
    return true;
}

void WebTransport::stop() {
    state_.store(MultiplayerState::Disconnected);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_msg_ = "Disconnected";
        rx_buffer_.clear();
    }
#if defined(__EMSCRIPTEN__)
    js_request_disconnect();
#endif
}

MultiplayerState WebTransport::state() const {
    return state_.load();
}

std::string WebTransport::status_description() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_msg_;
}

std::string WebTransport::room_code() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return room_code_;
}

std::size_t WebTransport::send(const void* data, std::size_t len) {
    if (!data || len == 0 || state_.load() != MultiplayerState::Connected) return 0;

    std::vector<std::uint8_t> frame(4 + len);
    frame[0] = kFrameMagic;
    frame[1] = kFrameTypeData;
    frame[2] = static_cast<std::uint8_t>((len >> 8) & 0xFF);
    frame[3] = static_cast<std::uint8_t>(len & 0xFF);
    std::memcpy(&frame[4], data, len);

#if defined(__EMSCRIPTEN__)
    js_send_relay_frame(frame.data(), static_cast<int>(frame.size()));
#endif

    bytes_sent_ += len;
    return len;
}

std::size_t WebTransport::receive(void* buffer, std::size_t max_len) {
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

std::size_t WebTransport::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rx_buffer_.size();
}

void WebTransport::set_peer_connected(bool connected) {
    if (connected) {
        state_.store(MultiplayerState::Connected);
        std::lock_guard<std::mutex> lock(mutex_);
        status_msg_ = "Opponent connected";
    } else {
        state_.store(MultiplayerState::Disconnected);
        std::lock_guard<std::mutex> lock(mutex_);
        status_msg_ = "Opponent disconnected";
    }
}

void WebTransport::set_room_code(const std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    room_code_ = code;
}

void WebTransport::handle_rx_frame(const std::uint8_t* data, std::size_t len) {
    if (!data || len < 4) return;
    if (data[0] != kFrameMagic) return;

    const std::uint8_t type = data[1];
    const std::size_t payload_len = (static_cast<std::size_t>(data[2]) << 8) | data[3];
    if (len < 4 + payload_len) return;

    const std::uint8_t* payload = &data[4];
    if (type == kFrameTypeData) {
        std::lock_guard<std::mutex> lock(mutex_);
        rx_buffer_.insert(rx_buffer_.end(), payload, payload + payload_len);
    } else if (type == kFrameTypePing && payload_len >= 8) {
        std::vector<std::uint8_t> pong(12);
        pong[0] = kFrameMagic;
        pong[1] = kFrameTypePong;
        pong[2] = 0;
        pong[3] = 8;
        std::memcpy(&pong[4], payload, 8);
#if defined(__EMSCRIPTEN__)
        js_send_relay_frame(pong.data(), static_cast<int>(pong.size()));
#endif
    } else if (type == kFrameTypePong && payload_len >= 8) {
        // Pong received from peer
    }
}

}  // namespace chq::net

// C bridge for Emscripten JavaScript caller
extern "C" {

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define CHQ_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define CHQ_EXPORT
#endif

CHQ_EXPORT void chq_web_init_session(int is_host, const char* room_code) {
    auto& mgr = chq::net::MultiplayerManager::instance();
    std::string code = room_code ? room_code : "";
    if (is_host) {
        mgr.host_web_game(code);
    } else {
        mgr.join_web_game(code);
    }
}

CHQ_EXPORT void chq_web_set_connected(int connected) {
    auto& mgr = chq::net::MultiplayerManager::instance();
    auto transport = std::dynamic_pointer_cast<chq::net::WebTransport>(mgr.active_transport());
    if (transport) {
        transport->set_peer_connected(connected != 0);
    }
}

CHQ_EXPORT void chq_web_rx_frame(const std::uint8_t* data, int len) {
    if (!data || len <= 0) return;
    auto& mgr = chq::net::MultiplayerManager::instance();
    auto transport = std::dynamic_pointer_cast<chq::net::WebTransport>(mgr.active_transport());
    if (transport) {
        transport->handle_rx_frame(data, static_cast<std::size_t>(len));
    }
}

CHQ_EXPORT void chq_web_disconnect() {
    auto& mgr = chq::net::MultiplayerManager::instance();
    mgr.disconnect();
}

}
