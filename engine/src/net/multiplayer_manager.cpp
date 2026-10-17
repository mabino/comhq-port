#include "net/multiplayer_manager.h"

#include "net/direct_transport.h"
#include "net/relay_server.h"
#include "net/relay_transport.h"
#include "net/web_transport.h"

namespace chq::net {

MultiplayerManager& MultiplayerManager::instance() {
    static MultiplayerManager s_instance;
    return s_instance;
}

MultiplayerManager::MultiplayerManager() {
    modem_bridge_ = std::make_shared<ModemBridge>();
}

MultiplayerManager::~MultiplayerManager() {
    disconnect();
}

bool MultiplayerManager::host_internet_game(const std::string& relay_host, int relay_port,
                                            const std::string& custom_room_code) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);

    std::string host = relay_host;
    int port = relay_port;

#if !defined(__EMSCRIPTEN__)
    if (host.empty() || host == "127.0.0.1" || host == "localhost") {
        embedded_relay_ = std::make_unique<RelayServer>(port);
        if (embedded_relay_->start()) {
            port = embedded_relay_->port();
            host = "127.0.0.1";
        } else {
            embedded_relay_.reset();
        }
    }
#endif

    auto transport = std::make_shared<RelayTransport>(host, port, custom_room_code);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    return transport->start();
}

bool MultiplayerManager::join_internet_game(const std::string& relay_host, int relay_port,
                                            const std::string& room_code) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    auto transport = std::make_shared<RelayTransport>(relay_host, relay_port, room_code, TransportRole::Guest);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    return transport->start();
}

bool MultiplayerManager::host_web_game(const std::string& custom_room_code) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    auto transport = std::make_shared<WebTransport>(TransportRole::Host, custom_room_code);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    return transport->start();
}

bool MultiplayerManager::join_web_game(const std::string& room_code) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    auto transport = std::make_shared<WebTransport>(TransportRole::Guest, room_code);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    return transport->start();
}

bool MultiplayerManager::host_lan_game(int port) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    auto transport = std::make_shared<DirectSocketTransport>(port);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    return transport->start();
}

bool MultiplayerManager::join_lan_game(const std::string& remote_host, int port) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    auto transport = std::make_shared<DirectSocketTransport>(remote_host, port);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    return transport->start();
}

void MultiplayerManager::start_loopback(std::shared_ptr<IMultiplayerTransport> transport) {
    disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    active_transport_ = transport;
    modem_bridge_->set_transport(transport);
    if (active_transport_) {
        active_transport_->start();
    }
}

void MultiplayerManager::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_transport_) {
        active_transport_->stop();
        active_transport_.reset();
    }
#if !defined(__EMSCRIPTEN__)
    if (embedded_relay_) {
        embedded_relay_->stop();
        embedded_relay_.reset();
    }
#endif
    if (modem_bridge_) {
        modem_bridge_->set_transport(nullptr);
    }
}

MultiplayerState MultiplayerManager::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_transport_) return MultiplayerState::Disconnected;
    return active_transport_->state();
}

std::string MultiplayerManager::status_description() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_transport_) return "Multiplayer Inactive";
    return active_transport_->status_description();
}

std::string MultiplayerManager::room_code() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_transport_) return "";
    return active_transport_->room_code();
}

double MultiplayerManager::round_trip_time_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_transport_) return 0.0;
    return active_transport_->round_trip_time_ms();
}

bool MultiplayerManager::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_transport_ && active_transport_->is_connected();
}

void MultiplayerManager::update() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (modem_bridge_) {
        modem_bridge_->update();
    }
}

}  // namespace chq::net
