#include <doctest/doctest.h>

#include "net/loopback_transport.h"
#include "net/modem_bridge.h"
#include "net/multiplayer_manager.h"
#include "net/multiplayer_transport.h"

#if !defined(__EMSCRIPTEN__)
#include "net/direct_transport.h"
#include "net/relay_server.h"
#include "net/relay_transport.h"
#endif

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

TEST_SUITE("multiplayer") {

TEST_CASE("LoopbackTransport creation and point-to-point communication") {
    auto [host, guest] = chq::net::LoopbackTransport::create_pair("ROOM-TEST");
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);

    CHECK_EQ(host->role(), chq::net::TransportRole::Host);
    CHECK_EQ(guest->role(), chq::net::TransportRole::Guest);
    CHECK_EQ(host->room_code(), "ROOM-TEST");
    CHECK_EQ(guest->room_code(), "ROOM-TEST");

    CHECK_EQ(host->state(), chq::net::MultiplayerState::Disconnected);
    CHECK_EQ(guest->state(), chq::net::MultiplayerState::Disconnected);

    // Starting host and guest connects the pair
    CHECK(host->start());
    CHECK_EQ(host->state(), chq::net::MultiplayerState::Connected);
    CHECK_EQ(guest->state(), chq::net::MultiplayerState::Connected);
    CHECK(host->is_connected());
    CHECK(guest->is_connected());

    // Send data host -> guest
    const char msg1[] = "HELLO_WAR_COMMANDER";
    const std::size_t sent1 = host->send(msg1, sizeof(msg1));
    CHECK_EQ(sent1, sizeof(msg1));
    CHECK_EQ(host->bytes_sent(), sizeof(msg1));
    CHECK_EQ(guest->available(), sizeof(msg1));

    char rx1[64] = {0};
    const std::size_t rec1 = guest->receive(rx1, sizeof(rx1));
    CHECK_EQ(rec1, sizeof(msg1));
    CHECK_EQ(guest->bytes_received(), sizeof(msg1));
    CHECK_EQ(std::strcmp(rx1, msg1), 0);
    CHECK_EQ(guest->available(), 0);

    // Send data guest -> host
    const char msg2[] = "BATTLE_ORDERS_CONFIRMED";
    const std::size_t sent2 = guest->send(msg2, sizeof(msg2));
    CHECK_EQ(sent2, sizeof(msg2));
    CHECK_EQ(guest->bytes_sent(), sizeof(msg2));
    CHECK_EQ(host->available(), sizeof(msg2));

    char rx2[64] = {0};
    const std::size_t rec2 = host->receive(rx2, sizeof(rx2));
    CHECK_EQ(rec2, sizeof(msg2));
    CHECK_EQ(host->bytes_received(), sizeof(msg2));
    CHECK_EQ(std::strcmp(rx2, msg2), 0);

    // Disconnect
    host->stop();
    CHECK_EQ(host->state(), chq::net::MultiplayerState::Disconnected);
    CHECK_EQ(guest->state(), chq::net::MultiplayerState::Disconnected);
    CHECK_FALSE(host->is_connected());
    CHECK_FALSE(guest->is_connected());
}

TEST_CASE("ModemBridge UART registers and Hayes AT command processing") {
    chq::net::ModemBridge modem;

    // Line Status Register default: TX empty (0x60)
    CHECK_EQ(modem.uart_read(1, 5), 0x60);
    // Interrupt Identification Register default: no interrupt (0x01)
    CHECK_EQ(modem.uart_read(1, 2), 0x01);
    // Scratch register read/write
    modem.uart_write(1, 7, 0xAB);
    CHECK_EQ(modem.uart_read(1, 7), 0xAB);

    // Line Control Register (DLAB bit controls divisor latch access)
    modem.uart_write(1, 3, 0x83);  // 8N1 + DLAB = 1
    modem.uart_write(1, 0, 0x60);  // Divisor low byte (96 for 1200 baud)
    modem.uart_write(1, 1, 0x00);  // Divisor high byte
    CHECK_EQ(modem.uart_read(1, 0), 0x60);
    CHECK_EQ(modem.uart_read(1, 1), 0x00);
    modem.uart_write(1, 3, 0x03);  // DLAB = 0

    // Send "AT\r" and verify "OK\r\n" response
    // Drain echo and response bytes helper
    auto drain_uart = [&modem]() -> std::string {
        std::string s;
        while (modem.uart_read(1, 5) & 0x01) {
            s.push_back(static_cast<char>(modem.uart_read(1, 0)));
        }
        return s;
    };

    const std::string at_cmd = "AT\r";
    for (char c : at_cmd) {
        modem.uart_write(1, 0, static_cast<std::uint8_t>(c));
    }
    std::string resp = drain_uart();
    CHECK(resp.find("OK\r\n") != std::string::npos);

    // Disable echo with ATE0
    const std::string ate0 = "ATE0\r";
    for (char c : ate0) {
        modem.uart_write(1, 0, static_cast<std::uint8_t>(c));
    }
    resp = drain_uart();
    CHECK(resp.find("OK\r\n") != std::string::npos);

    // Next command should not be echoed
    const std::string atz = "ATZ\r";
    for (char c : atz) {
        modem.uart_write(1, 0, static_cast<std::uint8_t>(c));
    }
    resp = drain_uart();
    CHECK(resp.find("OK\r\n") != std::string::npos);
}

TEST_CASE("ModemBridge handshake, carrier detection, and IRQ delivery") {
    auto [host_lp, guest_lp] = chq::net::LoopbackTransport::create_pair("BRIDGE-TEST");
    chq::net::ModemBridge modem_host;
    chq::net::ModemBridge modem_guest;

    modem_host.set_transport(host_lp);
    modem_guest.set_transport(guest_lp);

    // Both modems start offline
    CHECK_FALSE(modem_host.is_online());
    CHECK_FALSE(modem_guest.is_online());
    // MSR should not have Carrier Detect (0x80)
    CHECK_EQ(modem_host.uart_read(1, 6) & 0x80, 0);

    // Host answers: "ATA\r"
    const std::string ata = "ATA\r";
    for (char c : ata) modem_host.uart_write(1, 0, static_cast<std::uint8_t>(c));

    // Guest dials: "ATD5551212\r"
    const std::string atd = "ATD5551212\r";
    for (char c : atd) modem_guest.uart_write(1, 0, static_cast<std::uint8_t>(c));

    modem_host.update();
    modem_guest.update();

    CHECK(modem_host.is_online());
    CHECK(modem_guest.is_online());

    // Drain CONNECT responses from rx_fifo
    while (modem_host.uart_read(1, 5) & 0x01) {
        modem_host.uart_read(1, 0);
    }
    while (modem_guest.uart_read(1, 5) & 0x01) {
        modem_guest.uart_read(1, 0);
    }

    // MSR now reports Carrier Detect (0x80), DSR (0x20), and CTS (0x10)
    CHECK((modem_host.uart_read(1, 6) & 0x80) != 0);
    CHECK((modem_guest.uart_read(1, 6) & 0x80) != 0);

    // Online data exchange through COM1
    // Host writes byte 0x7E (packet framing in SERIAL.OVL)
    modem_host.uart_write(1, 0, 0x7E);
    modem_guest.update();

    // Configure COM1 interrupt on guest: IER = 0x01 (data ready), MCR = 0x08 (OUT2)
    modem_guest.uart_write(1, 1, 0x01);
    modem_guest.uart_write(1, 4, 0x08);

    // Guest should have IRQ 4 pending
    CHECK(modem_guest.has_pending_irq(1));
    CHECK_EQ(modem_guest.irq_for_port(1), 4);

    // Read the byte
    const std::uint8_t val = modem_guest.uart_read(1, 0);
    CHECK_EQ(val, 0x7E);

    // Once FIFO drained, IRQ clears
    CHECK_FALSE(modem_guest.has_pending_irq(1));

    // Hangup
    modem_host.force_disconnect();
    CHECK_FALSE(modem_host.is_online());
}

TEST_CASE("MultiplayerManager singleton and loopback session lifecycle") {
    auto& mgr = chq::net::MultiplayerManager::instance();
    CHECK_EQ(mgr.state(), chq::net::MultiplayerState::Disconnected);
    CHECK_FALSE(mgr.is_connected());

    auto [host_lp, guest_lp] = chq::net::LoopbackTransport::create_pair("MGR-TEST");
    mgr.start_loopback(host_lp);

    CHECK(mgr.is_connected());
    CHECK_EQ(mgr.state(), chq::net::MultiplayerState::Connected);
    CHECK_EQ(mgr.room_code(), "MGR-TEST");

    mgr.disconnect();
    CHECK_FALSE(mgr.is_connected());
    CHECK_EQ(mgr.state(), chq::net::MultiplayerState::Disconnected);
}

#if !defined(__EMSCRIPTEN__)

TEST_CASE("RelayServer and RelayTransport NAT traversal broker") {
    // 1. Start RelayServer on dynamic port
    chq::net::RelayServer server(0);
    REQUIRE(server.start());
    CHECK(server.is_running());
    const int port = server.port();
    REQUIRE(port > 0);

    // 2. Host creates room "WAR-TEST"
    auto host_transport = std::make_shared<chq::net::RelayTransport>("127.0.0.1", port, "WAR-TEST");
    REQUIRE(host_transport->start());

    // Wait for host to finish room handshake
    auto host_wait = std::chrono::steady_clock::now();
    while (host_transport->status_description().find("created") == std::string::npos &&
           std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - host_wait).count() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 3. Guest joins room "WAR-TEST"
    auto guest_transport = std::make_shared<chq::net::RelayTransport>(
        "127.0.0.1", port, "WAR-TEST", chq::net::TransportRole::Guest);
    REQUIRE(guest_transport->start());

    // 4. Wait for both to connect (up to 3 seconds)
    auto start_time = std::chrono::steady_clock::now();
    bool connected = false;
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start_time).count() < 3) {
        if (host_transport->is_connected() && guest_transport->is_connected()) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    INFO("Host state: ", static_cast<int>(host_transport->state()), " (", host_transport->status_description(), ")");
    INFO("Guest state: ", static_cast<int>(guest_transport->state()), " (", guest_transport->status_description(), ")");
    CHECK(connected);

    if (connected) {
        // Exchange binary framed data
        const char host_msg[] = "LAUNCH_FLEET_SECTOR_4";
        host_transport->send(host_msg, sizeof(host_msg));

        // Wait for guest reception
        start_time = std::chrono::steady_clock::now();
        while (guest_transport->available() < sizeof(host_msg) &&
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        char guest_rx[64] = {0};
        const std::size_t g_read = guest_transport->receive(guest_rx, sizeof(guest_rx));
        CHECK_EQ(g_read, sizeof(host_msg));
        CHECK_EQ(std::strcmp(guest_rx, host_msg), 0);

        // Send reply guest -> host
        const char guest_msg[] = "AIR_STRIKE_DEFLECTED";
        guest_transport->send(guest_msg, sizeof(guest_msg));

        start_time = std::chrono::steady_clock::now();
        while (host_transport->available() < sizeof(guest_msg) &&
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        char host_rx[64] = {0};
        const std::size_t h_read = host_transport->receive(host_rx, sizeof(host_rx));
        CHECK_EQ(h_read, sizeof(guest_msg));
        CHECK_EQ(std::strcmp(host_rx, guest_msg), 0);
    }

    guest_transport->stop();
    host_transport->stop();
    server.stop();
    CHECK_FALSE(server.is_running());
}

TEST_CASE("DirectSocketTransport LAN / IP peer-to-peer") {
    // Host on port 7198
    const int test_port = 7198;
    auto host = std::make_shared<chq::net::DirectSocketTransport>(test_port);
    REQUIRE(host->start());

    // Guest connects to 127.0.0.1:7198
    auto guest = std::make_shared<chq::net::DirectSocketTransport>("127.0.0.1", test_port);
    REQUIRE(guest->start());

    // Wait for connection
    auto start_time = std::chrono::steady_clock::now();
    bool connected = false;
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start_time).count() < 3) {
        if (host->is_connected() && guest->is_connected()) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(connected);

    if (connected) {
        const char pings[] = "PING_DIRECT";
        host->send(pings, sizeof(pings));

        start_time = std::chrono::steady_clock::now();
        while (guest->available() < sizeof(pings) &&
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        char pongs[32] = {0};
        guest->receive(pongs, sizeof(pongs));
        CHECK_EQ(std::strcmp(pongs, pings), 0);
    }

    guest->stop();
    host->stop();
}

#endif  // !defined(__EMSCRIPTEN__)

}  // TEST_SUITE("multiplayer")
