#pragma once

#include "net/modem_bridge.h"
#include "net/multiplayer_transport.h"

#include <memory>
#include <mutex>
#include <string>

namespace chq::net {

class RelayServer;

class MultiplayerManager {
public:
    static MultiplayerManager& instance();

    MultiplayerManager();
    ~MultiplayerManager();

    std::shared_ptr<ModemBridge> modem_bridge() const { return modem_bridge_; }
    std::shared_ptr<IMultiplayerTransport> active_transport() const { return active_transport_; }

    // Internet Session (NAT Traversal via Relay)
    bool host_internet_game(const std::string& relay_host = "127.0.0.1", int relay_port = 7190,
                            const std::string& custom_room_code = "");
    bool join_internet_game(const std::string& relay_host, int relay_port,
                            const std::string& room_code);

    // Web Session (WASM WebSocket Relay)
    bool host_web_game(const std::string& custom_room_code = "");
    bool join_web_game(const std::string& room_code);

    // Direct LAN / IP Session
    bool host_lan_game(int port = 7190);
    bool join_lan_game(const std::string& remote_host, int port = 7190);

    // Loopback Session (Local Testing)
    void start_loopback(std::shared_ptr<IMultiplayerTransport> transport);

    void disconnect();

    // Status queries
    MultiplayerState state() const;
    std::string status_description() const;
    std::string room_code() const;
    double round_trip_time_ms() const;
    bool is_connected() const;

    // Per-frame update
    void update();

private:
    std::shared_ptr<ModemBridge> modem_bridge_;
    std::shared_ptr<IMultiplayerTransport> active_transport_;
    std::unique_ptr<RelayServer> embedded_relay_;
    mutable std::mutex mutex_;
};

}  // namespace chq::net
