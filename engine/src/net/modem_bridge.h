#pragma once

#include "net/multiplayer_transport.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace chq::net {

class ModemBridge {
public:
    ModemBridge();
    ~ModemBridge();

    void set_transport(std::shared_ptr<IMultiplayerTransport> transport);
    std::shared_ptr<IMultiplayerTransport> transport() const { return transport_; }

    // 16550 UART I/O interface (called by Machine::in8 and Machine::out8)
    std::uint8_t uart_read(int port_num, int reg_offset);
    void uart_write(int port_num, int reg_offset, std::uint8_t value);

    // Hardware interrupt status
    bool has_pending_irq(int port_num) const;
    int irq_for_port(int port_num) const { return (port_num == 1) ? 4 : 3; }

    // Tick to service transport and transfer bytes
    void update();

    // Direct modem state control
    bool is_online() const { return online_mode_; }
    int active_port() const { return active_port_; }
    void force_disconnect();

private:
    void handle_at_command(const std::string& cmd);
    void queue_modem_response(const std::string& resp);

    std::shared_ptr<IMultiplayerTransport> transport_;

    mutable std::mutex mutex_;

    // UART 16550 state for COM1/COM2
    struct UartPort {
        std::uint8_t ier = 0x00;        // Interrupt Enable
        std::uint8_t iir = 0x01;        // Interrupt ID (0x01 = no interrupt pending)
        std::uint8_t fcr = 0x00;        // FIFO Control
        std::uint8_t lcr = 0x03;        // Line Control (8N1)
        std::uint8_t mcr = 0x00;        // Modem Control
        std::uint8_t lsr = 0x60;        // Line Status (0x60 = Transmitter empty)
        std::uint8_t msr = 0x00;        // Modem Status
        std::uint8_t scr = 0x00;        // Scratch Register
        std::uint16_t divisor = 0x0060; // Baud rate divisor (96 = 1200 baud)
        bool dlab = false;
        std::deque<std::uint8_t> rx_fifo;
        std::deque<std::uint8_t> tx_fifo;
    };

    UartPort com1_;
    UartPort com2_;

    UartPort& get_port(int num) { return (num == 1) ? com1_ : com2_; }
    const UartPort& get_port(int num) const { return (num == 1) ? com1_ : com2_; }

    // AT Command processor
    int active_port_ = 1;
    bool online_mode_ = false;
    bool echo_enabled_ = true;
    std::string at_cmd_buffer_;
    std::deque<std::uint8_t> modem_resp_queue_;
};

}  // namespace chq::net
