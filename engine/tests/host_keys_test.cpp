#include "app/host_keys.h"

#include <doctest/doctest.h>

#include "dos/machine.h"

TEST_SUITE("host_keys") {

TEST_CASE("host_shift_flags converts SDL modifier bits to PC BIOS shift flags") {
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_NONE), 0x00);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_RSHIFT), 0x01);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_LSHIFT), 0x02);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_CTRL), 0x04);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_LCTRL), 0x04);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_ALT), 0x08);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_LALT), 0x08);
    CHECK_EQ(chq::host_shift_flags(SDL_KMOD_CAPS), 0x40);

    const SDL_Keymod combo = static_cast<SDL_Keymod>(SDL_KMOD_LSHIFT | SDL_KMOD_ALT);
    CHECK_EQ(chq::host_shift_flags(combo), 0x0A);
}

TEST_CASE("translates Option+P (macOS unicode pi) to PC BIOS Alt+P") {
    // On macOS, Option+P generates unicode character pi (0x03c0) with SDL_KMOD_ALT
    // and physical scancode SDL_SCANCODE_P.
    auto opt_p = chq::host_key(static_cast<SDL_Keycode>(0x03c0), SDL_KMOD_ALT, SDL_SCANCODE_P);
    REQUIRE(opt_p.has_value());
    CHECK_EQ(opt_p->scan, 0x19);   // Scancode for 'P'
    CHECK_EQ(opt_p->ascii, 0x00);  // Alt+letter produces ASCII 0 in PC BIOS

    // Also works when key is passed as 'p' with Alt modifier
    auto alt_p = chq::host_key(SDLK_P, SDL_KMOD_ALT, SDL_SCANCODE_P);
    REQUIRE(alt_p.has_value());
    CHECK_EQ(alt_p->scan, 0x19);
    CHECK_EQ(alt_p->ascii, 0x00);
}

TEST_CASE("translates other Option+letters correctly") {
    // Option+S (macOS ß / 0x00df) -> Alt+S
    auto opt_s = chq::host_key(static_cast<SDL_Keycode>(0x00df), SDL_KMOD_ALT, SDL_SCANCODE_S);
    REQUIRE(opt_s.has_value());
    CHECK_EQ(opt_s->scan, 0x1F);   // Scancode for 'S'
    CHECK_EQ(opt_s->ascii, 0x00);

    // Option+A (macOS å / 0x00e5) -> Alt+A
    auto opt_a = chq::host_key(static_cast<SDL_Keycode>(0x00e5), SDL_KMOD_ALT, SDL_SCANCODE_A);
    REQUIRE(opt_a.has_value());
    CHECK_EQ(opt_a->scan, 0x1E);   // Scancode for 'A'
    CHECK_EQ(opt_a->ascii, 0x00);
}

TEST_CASE("regular and special keys map accurately") {
    auto regular_p = chq::host_key(SDLK_P, SDL_KMOD_NONE, SDL_SCANCODE_P);
    REQUIRE(regular_p.has_value());
    CHECK_EQ(regular_p->scan, 0x19);
    CHECK_EQ(regular_p->ascii, 'p');

    auto shift_p = chq::host_key(SDLK_P, SDL_KMOD_LSHIFT, SDL_SCANCODE_P);
    REQUIRE(shift_p.has_value());
    CHECK_EQ(shift_p->scan, 0x19);
    CHECK_EQ(shift_p->ascii, 'P');

    auto esc = chq::host_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    REQUIRE(esc.has_value());
    CHECK_EQ(esc->scan, 0x01);
    CHECK_EQ(esc->ascii, 0x1B);

    auto enter = chq::host_key(SDLK_RETURN, SDL_KMOD_NONE);
    REQUIRE(enter.has_value());
    CHECK_EQ(enter->scan, 0x1C);
    CHECK_EQ(enter->ascii, 0x0D);
}

TEST_CASE("Machine emits modifier make/break scancodes for Alt+P") {
    std::vector<std::uint8_t> image(0x4000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {});

    // Set Alt modifier active in machine shift flags
    m.set_shift_state(chq::host_shift_flags(SDL_KMOD_ALT));

    auto k = chq::host_key(static_cast<SDL_Keycode>(0x03c0), SDL_KMOD_ALT, SDL_SCANCODE_P);
    REQUIRE(k.has_value());

    m.key_down(k->scan, k->ascii);

    const auto& drained = m.pending_scancodes();
    REQUIRE_GE(drained.size(), 4);
    CHECK_EQ(drained[0], 0x38);  // Left Alt make
    CHECK_EQ(drained[1], 0x19);  // P make
    CHECK_EQ(drained[2], 0x99);  // P break (0x19 | 0x80)
    CHECK_EQ(drained[3], 0xB8);  // Left Alt break
}

}  // TEST_SUITE("host_keys")
