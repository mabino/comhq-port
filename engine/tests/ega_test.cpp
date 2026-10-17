#include <doctest/doctest.h>

#include <vector>

#include "dos/ega.h"
#include "dos/keymap.h"
#include "gfx/ega_surface.h"

using chq::dos::Ega;

namespace {
void gc(Ega& e, int index, int value) {
    e.port_out(0x3CE, static_cast<std::uint8_t>(index));
    e.port_out(0x3CF, static_cast<std::uint8_t>(value));
}
void map_mask(Ega& e, int value) {
    e.port_out(0x3C4, 2);
    e.port_out(0x3C5, static_cast<std::uint8_t>(value));
}
}  // namespace

TEST_CASE("write mode 0 with set/reset and bit mask in Mode 0Dh (320x200)") {
    Ega e;
    e.set_mode(0x0D, true);
    gc(e, 0, 0x0E);  // set/reset colour 14
    gc(e, 1, 0x0F);  // enable for all planes
    gc(e, 8, 0x80);  // leftmost pixel only
    e.read(0);       // load latches
    e.write(0, 0xFF);
    CHECK(e.pixel(0, 0) == 14);
    CHECK(e.pixel(1, 0) == 0);
}

TEST_CASE("map mask limits the planes written in 40-byte stride Mode 0Dh") {
    Ega e;
    e.set_mode(0x0D, true);
    map_mask(e, 0x01);
    e.write(40, 0xFF);  // row 1 in 320x200 (40 bytes per scanline)
    CHECK(e.pixel(0, 1) == 1);
    CHECK(e.plane(1, 40) == 0);
}

TEST_CASE("write mode 1 copies latches") {
    Ega e;
    e.set_mode(0x0D, true);
    gc(e, 0, 0x05);
    gc(e, 1, 0x0F);
    e.write(10, 0xFF);
    gc(e, 1, 0x00);
    gc(e, 5, 0x01);  // write mode 1
    e.read(10);
    e.write(20, 0x00);
    CHECK(e.pixel(20 * 8, 0) == 5);
}

TEST_CASE("write mode 2 and logical functions") {
    Ega e;
    e.set_mode(0x0D, true);
    gc(e, 5, 0x02);  // write mode 2: CPU byte is a colour
    e.read(0);
    e.write(0, 0x09);
    CHECK(e.pixel(3, 0) == 9);
    gc(e, 3, 0x18);  // XOR with latches
    e.read(0);
    e.write(0, 0x0F);
    CHECK(e.pixel(3, 0) == 6);
}

TEST_CASE("read mode 1: colour compare") {
    Ega e;
    e.set_mode(0x0D, true);
    gc(e, 5, 0x02);
    gc(e, 8, 0xF0);
    e.read(0);
    e.write(0, 0x03);
    gc(e, 8, 0xFF);
    gc(e, 5, 0x08);  // read mode 1
    gc(e, 2, 0x03);  // compare against colour 3
    CHECK(e.read(0) == 0xF0);
}

TEST_CASE("CRTC start address and color display detection readback (3D4h/3D5h)") {
    Ega e;
    e.set_mode(0x0D, true);
    // EGRAPHIC.EXE 29b0:0134 writes 0x2D to CRTC index 0x0F and reads it back
    e.port_out(0x3D4, 0x0F);
    e.port_out(0x3D5, 0x2D);
    CHECK(e.port_in(0x3D5) == 0x2D);

    // Page flipping via CRTC start address high (0x0C) = 0x20 -> offset 0x2000
    e.port_out(0x3D4, 0x0C);
    e.port_out(0x3D5, 0x20);
    e.port_out(0x3D4, 0x0D);
    e.port_out(0x3D5, 0x00);
    map_mask(e, 0x0F);
    e.write(0x2000, 0xFF);
    CHECK(e.pixel(0, 0) == 15);
}

TEST_CASE("input status register toggles retrace so polling loops end") {
    Ega e;
    const auto a = e.port_in(0x3DA), b = e.port_in(0x3DA);
    CHECK((a & 8) != (b & 8));
}

TEST_CASE("key and mouse scripts") {
    using chq::dos::BiosKey;
    auto keys = chq::dos::parse_script("Ab\n{F1}{SF2}{UP}{WAIT 2}{CLICK 160 100}{RCLICK 40 50}{MOUSE 10 20}{BOGUS}x");
    REQUIRE(keys.size() == 14);
    CHECK(keys[0] == BiosKey{0x1E, 'A'});
    CHECK(keys[1] == BiosKey{0x30, 'b'});
    CHECK(keys[2] == BiosKey{0x1C, 0x0D});
    CHECK(keys[3] == BiosKey{0x3B, 0});
    CHECK(keys[4] == BiosKey{0x55, 0});
    CHECK(keys[5] == BiosKey{0x48, 0});
    CHECK(keys[6] == BiosKey{});
    CHECK(keys[7] == BiosKey{});
    CHECK(keys[8] == BiosKey{0, 0, 160, 100, 1});
    CHECK(keys[9] == BiosKey{0, 0, 160, 100, 0});
    CHECK(keys[10] == BiosKey{0, 0, 40, 50, 2});
    CHECK(keys[11] == BiosKey{0, 0, 40, 50, 0});
    CHECK(keys[12] == BiosKey{0, 0, 10, 20, 0});
    CHECK(keys[13] == BiosKey{0x2D, 'x'});
    CHECK(chq::dos::key_for_char('?')->scan == 0x35);
    CHECK_FALSE(chq::dos::key_for_name("F11").has_value());
}

TEST_CASE("EGA palette mapping for 200-line modes") {
    // 200-line EGA palette mappings (CGA compatibility RGBI with brown fix)
    CHECK(Ega::rgb(0) == 0xFF000000u);     // Black
    CHECK(Ega::rgb(1) == 0xFF0000AAu);     // Blue
    CHECK(Ega::rgb(2) == 0xFF00AA00u);     // Green
    CHECK(Ega::rgb(3) == 0xFF00AAAAu);     // Cyan
    CHECK(Ega::rgb(4) == 0xFFAA0000u);     // Red
    CHECK(Ega::rgb(5) == 0xFFAA00AAu);     // Magenta
    CHECK(Ega::rgb(6) == 0xFFAA5500u);     // Brown
    CHECK(Ega::rgb(7) == 0xFFAAAAAAu);     // Light Gray
    CHECK(Ega::rgb(0x10) == 0xFF555555u);  // Dark Gray (bit 4 intensity)
    CHECK(Ega::rgb(0x11) == 0xFF5555FFu);  // Bright Blue
    CHECK(Ega::rgb(0x12) == 0xFF55FF55u);  // Bright Green
    CHECK(Ega::rgb(0x13) == 0xFF55FFFFu);  // Bright Cyan
    CHECK(Ega::rgb(0x14) == 0xFFFF5555u);  // Bright Red
    CHECK(Ega::rgb(0x15) == 0xFFFF55FFu);  // Bright Magenta
    CHECK(Ega::rgb(0x16) == 0xFFFFFF55u);  // Bright Yellow
    CHECK(Ega::rgb(0x17) == 0xFFFFFFFFu);  // Bright White

    // 350-line default indices 56..63 also map cleanly to the 8 bright colors
    CHECK(Ega::rgb(56) == 0xFF555555u);
    CHECK(Ega::rgb(63) == 0xFFFFFFFFu);

    // Default palette registers match standard 16 colors
    Ega e;
    for (int i = 0; i < 16; ++i) {
        CHECK(Ega::rgb(e.palette(i)) == chq::EgaSurface::default_palette()[i]);
    }
}
