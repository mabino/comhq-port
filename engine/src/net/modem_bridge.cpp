#include "net/modem_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace chq::net {

ModemBridge::ModemBridge() = default;

ModemBridge::~ModemBridge() = default;

void ModemBridge::set_transport(std::shared_ptr<IMultiplayerTransport> transport) {
    std::lock_guard<std::mutex> lock(mutex_);
    transport_ = std::move(transport);
    if (!transport_) {
        online_mode_ = false;
    }
}

void ModemBridge::force_disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transport_) {
        transport_->stop();
    }
    online_mode_ = false;
    queue_modem_response("NO CARRIER\r\n");
}

void ModemBridge::queue_modem_response(const std::string& resp) {
    auto& port = get_port(active_port_);
    for (char c : resp) {
        port.rx_fifo.push_back(static_cast<std::uint8_t>(c));
    }
}

void ModemBridge::handle_at_command(const std::string& raw_cmd) {
    std::string cmd;
    for (char c : raw_cmd) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            cmd.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }

    if (cmd.empty() || cmd == "AT") {
        queue_modem_response("OK\r\n");
        return;
    }

    if (cmd == "ATZ") {
        echo_enabled_ = true;
        online_mode_ = false;
        if (transport_) transport_->stop();
        queue_modem_response("OK\r\n");
        return;
    }

    if (cmd == "ATE0") {
        echo_enabled_ = false;
        queue_modem_response("OK\r\n");
        return;
    }

    if (cmd == "ATE1") {
        echo_enabled_ = true;
        queue_modem_response("OK\r\n");
        return;
    }

    if (cmd == "ATH0") {
        online_mode_ = false;
        if (transport_) transport_->stop();
        queue_modem_response("OK\r\n");
        return;
    }

    if (cmd == "ATH1") {
        queue_modem_response("OK\r\n");
        return;
    }

    if (cmd == "ATA") {
        // Answer mode
        if (transport_) {
            transport_->start();
            if (transport_->is_connected()) {
                online_mode_ = true;
                queue_modem_response("CONNECT 1200\r\n");
            } else {
                // Will enter online mode once peer connects in update()
                queue_modem_response("RING\r\n");
            }
        } else {
            queue_modem_response("NO CARRIER\r\n");
        }
        return;
    }

    if (cmd.rfind("ATD", 0) == 0) {
        // Dial mode
        if (transport_) {
            transport_->start();
            if (transport_->is_connected()) {
                online_mode_ = true;
                queue_modem_response("CONNECT 1200\r\n");
            } else {
                // Connecting
                queue_modem_response("OK\r\n");
            }
        } else {
            queue_modem_response("NO CARRIER\r\n");
        }
        return;
    }

    // Default response for setup / initialization strings (e.g. AT&Q6S37=9N0&K0 from MODEM.INI)
    queue_modem_response("OK\r\n");
}

void ModemBridge::update() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!transport_) return;

    if (online_mode_) {
        if (!transport_->is_connected()) {
            online_mode_ = false;
            queue_modem_response("NO CARRIER\r\n");
            return;
        }

        // Receive data from transport into active port rx_fifo
        auto& port = get_port(active_port_);
        std::uint8_t temp[512];
        std::size_t n = transport_->receive(temp, sizeof(temp));
        if (n > 0) {
            for (std::size_t i = 0; i < n; ++i) {
                port.rx_fifo.push_back(temp[i]);
            }
        }
    } else {
        // In command mode: check if transport newly connected while waiting in answer/dial
        if (transport_->is_connected()) {
            online_mode_ = true;
            queue_modem_response("CONNECT 1200\r\n");
        }
    }
}

std::uint8_t ModemBridge::uart_read(int port_num, int reg_offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& port = get_port(port_num);

    switch (reg_offset) {
        case 0: {  // Receiver Buffer Register or Divisor Latch Low
            if (port.dlab) {
                return static_cast<std::uint8_t>(port.divisor & 0xFF);
            }
            if (!port.rx_fifo.empty()) {
                std::uint8_t val = port.rx_fifo.front();
                port.rx_fifo.pop_front();
                return val;
            }
            return 0x00;
        }
        case 1: {  // Interrupt Enable Register or Divisor Latch High
            if (port.dlab) {
                return static_cast<std::uint8_t>((port.divisor >> 8) & 0xFF);
            }
            return port.ier;
        }
        case 2: {  // Interrupt Identification Register
            std::uint8_t iir = 0x01;  // No interrupt pending
            if ((port.ier & 0x01) && !port.rx_fifo.empty()) {
                iir = 0x04;  // Received data available
            } else if ((port.ier & 0x02) && port.tx_fifo.empty()) {
                iir = 0x02;  // Transmitter Holding Register Empty
            }
            return iir;
        }
        case 3: return port.lcr;  // Line Control Register
        case 4: return port.mcr;  // Modem Control Register
        case 5: {                 // Line Status Register
            std::uint8_t lsr = 0x60;  // TX empty & TX holding empty
            if (!port.rx_fifo.empty()) {
                lsr |= 0x01;  // Data Ready
            }
            return lsr;
        }
        case 6: {                 // Modem Status Register
            std::uint8_t msr = 0x00;
            if (online_mode_ || (transport_ && transport_->is_connected())) {
                msr |= 0x80;  // Carrier Detect (CD)
                msr |= 0x20;  // Data Set Ready (DSR)
                msr |= 0x10;  // Clear to Send (CTS)
            }
            return msr;
        }
        case 7: return port.scr;  // Scratch Register
        default: return 0xFF;
    }
}

void ModemBridge::uart_write(int port_num, int reg_offset, std::uint8_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_port_ = port_num;
    auto& port = get_port(port_num);

    switch (reg_offset) {
        case 0: {  // Transmitter Holding Register or Divisor Latch Low
            if (port.dlab) {
                port.divisor = static_cast<std::uint16_t>((port.divisor & 0xFF00) | value);
                return;
            }

            if (online_mode_) {
                if (transport_ && transport_->is_connected()) {
                    transport_->send(&value, 1);
                }
            } else {
                // Command mode
                char c = static_cast<char>(value);
                if (c == '\r' || c == '\n') {
                    if (echo_enabled_) {
                        port.rx_fifo.push_back('\r');
                        port.rx_fifo.push_back('\n');
                    }
                    if (!at_cmd_buffer_.empty()) {
                        handle_at_command(at_cmd_buffer_);
                        at_cmd_buffer_.clear();
                    }
                } else if (c == 0x08 || c == 0x7F) {  // Backspace
                    if (!at_cmd_buffer_.empty()) {
                        at_cmd_buffer_.pop_back();
                        if (echo_enabled_) {
                            port.rx_fifo.push_back(0x08);
                            port.rx_fifo.push_back(' ');
                            port.rx_fifo.push_back(0x08);
                        }
                    }
                } else if (std::isprint(static_cast<unsigned char>(c))) {
                    at_cmd_buffer_.push_back(c);
                    if (echo_enabled_) {
                        port.rx_fifo.push_back(value);
                    }
                }
            }
            break;
        }
        case 1: {  // Interrupt Enable Register or Divisor Latch High
            if (port.dlab) {
                port.divisor = static_cast<std::uint16_t>((port.divisor & 0x00FF) | (static_cast<std::uint16_t>(value) << 8));
                return;
            }
            port.ier = value;
            break;
        }
        case 2: port.fcr = value; break;  // FIFO Control Register
        case 3: {                          // Line Control Register
            port.lcr = value;
            port.dlab = (value & 0x80) != 0;
            break;
        }
        case 4: port.mcr = value; break;  // Modem Control Register
        case 5: port.lsr = value; break;  // Line Status Register
        case 6: port.msr = value; break;  // Modem Status Register
        case 7: port.scr = value; break;  // Scratch Register
        default: break;
    }
}

bool ModemBridge::has_pending_irq(int port_num) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& port = get_port(port_num);

    // OUT2 bit (0x08) in MCR must be set to gate interrupts to the PC bus
    if (!(port.mcr & 0x08)) return false;

    // Data ready interrupt enabled and data available in rx_fifo
    if ((port.ier & 0x01) && !port.rx_fifo.empty()) {
        return true;
    }
    return false;
}

}  // namespace chq::net
