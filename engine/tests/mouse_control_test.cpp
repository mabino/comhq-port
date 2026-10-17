#include <doctest/doctest.h>

#include "app/mouse_control.h"
#include "dos/machine.h"

TEST_SUITE("mouse_control") {

TEST_CASE("maps 640x480 logical coordinates to 320x200 EGA coordinates") {
    auto origin = chq::mouse::logical_to_ega(0.0f, 0.0f);
    REQUIRE(origin.has_value());
    CHECK_EQ(origin->first, 0);
    CHECK_EQ(origin->second, 0);

    auto center = chq::mouse::logical_to_ega(320.0f, 240.0f);
    REQUIRE(center.has_value());
    CHECK_EQ(center->first, 160);
    CHECK_EQ(center->second, 100);

    auto bottom_right = chq::mouse::logical_to_ega(639.0f, 479.0f);
    REQUIRE(bottom_right.has_value());
    CHECK_EQ(bottom_right->first, 319);
    CHECK_EQ(bottom_right->second, 199);

    // Out of bounds
    CHECK_FALSE(chq::mouse::logical_to_ega(-1.0f, 100.0f).has_value());
    CHECK_FALSE(chq::mouse::logical_to_ega(640.0f, 100.0f).has_value());
    CHECK_FALSE(chq::mouse::logical_to_ega(100.0f, -0.5f).has_value());
    CHECK_FALSE(chq::mouse::logical_to_ega(100.0f, 480.0f).has_value());
}

TEST_CASE("clamps EGA coordinates to [0, 319] x [0, 199]") {
    CHECK_EQ(chq::mouse::clamp_ega_coordinates(-10, -5), std::make_pair(0, 0));
    CHECK_EQ(chq::mouse::clamp_ega_coordinates(160, 100), std::make_pair(160, 100));
    CHECK_EQ(chq::mouse::clamp_ega_coordinates(500, 300), std::make_pair(319, 199));
}

TEST_CASE("converts SDL mouse button bitmask to DOS INT 33h button mask") {
    CHECK_EQ(chq::mouse::sdl_buttons_to_dos(0), 0);
    CHECK_EQ(chq::mouse::sdl_buttons_to_dos(1), 1);  // Left -> bit 0
    CHECK_EQ(chq::mouse::sdl_buttons_to_dos(4), 2);  // Right (SDL bit 2) -> DOS bit 1
    CHECK_EQ(chq::mouse::sdl_buttons_to_dos(2), 4);  // Middle (SDL bit 1) -> DOS bit 2
    CHECK_EQ(chq::mouse::sdl_buttons_to_dos(5), 3);  // Left + Right -> bits 0 & 1
}

TEST_CASE("Machine manual copy-protection prefill hook") {
    std::vector<std::uint8_t> image(0x4000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {});

    CHECK(m.startup_settings().prefill_manual_word);
    CHECK_FALSE(m.startup_settings().auto_submit_manual_word);

    // Simulate 1aa3:11af unpacking "STRATEGY" into DS:0xE73A (DGROUP = 0x3F5A)
    const char* word = "STRATEGY";
    for (int i = 0; i < 9; ++i) {
        m.wr8(chq::dos::Machine::kDgroupSeg, static_cast<std::uint16_t>(0xE73A + i),
              static_cast<std::uint8_t>(word[i]));
    }
    // Simulate 1aa3:115f writing the 1st char to DS:0xCA86 and NUL to DS:0xCA87 while CS = 0x2AA3
    m.cpu.s[chq::dos::CS] = 0x2AA3;
    m.wr8(chq::dos::Machine::kDgroupSeg, 0xCA86, 'S');
    m.wr8(chq::dos::Machine::kDgroupSeg, 0xCA87, 0);

    // Verify that DS:0xCA86 now contains the full manual word "STRATEGY"
    std::string prefilled;
    for (int i = 0; i < 9; ++i) {
        char c = static_cast<char>(m.rd8(chq::dos::Machine::kDgroupSeg, static_cast<std::uint16_t>(0xCA86 + i)));
        if (!c) break;
        prefilled.push_back(c);
    }
    CHECK_EQ(prefilled, "STRATEGY");
}

}  // TEST_SUITE("mouse_control")
