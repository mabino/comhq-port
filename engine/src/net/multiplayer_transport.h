#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chq::net {

enum class MultiplayerState {
    Disconnected,
    Hosting,     // Waiting for peer to connect (or room created on relay)
    Connecting,  // Actively establishing connection to peer or relay
    Connected,   // Opponent connected; data exchange active
    Faulted      // Connection lost or encountered an error
};

enum class TransportRole {
    None,
    Host,
    Guest
};

// Abstract transport layer for peer-to-peer or relayed multiplayer.
class IMultiplayerTransport {
public:
    virtual ~IMultiplayerTransport() = default;

    // Start / stop connection
    virtual bool start() = 0;
    virtual void stop() = 0;

    // State & Role
    virtual MultiplayerState state() const = 0;
    virtual TransportRole role() const = 0;
    virtual bool is_connected() const { return state() == MultiplayerState::Connected; }
    virtual std::string status_description() const = 0;
    virtual std::string room_code() const = 0;

    // Data transfer
    virtual std::size_t send(const void* data, std::size_t len) = 0;
    virtual std::size_t receive(void* buffer, std::size_t max_len) = 0;
    virtual std::size_t available() const = 0;
    virtual void flush() = 0;

    // Diagnostics
    virtual double round_trip_time_ms() const = 0;
    virtual std::uint64_t bytes_sent() const = 0;
    virtual std::uint64_t bytes_received() const = 0;
};

}  // namespace chq::net
