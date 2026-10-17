#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace chq::mouse {

// Maps 640x480 4:3 logical presentation coordinates to Command HQ's 320x200
// Mode 0Dh EGA coordinate space. Returns std::nullopt if outside [0, 640) x [0, 480).
std::optional<std::pair<int, int>> logical_to_ega(float logical_x, float logical_y);

// Clamps arbitrary EGA coordinates to [0, 319] x [0, 199].
std::pair<int, int> clamp_ega_coordinates(int ega_x, int ega_y);

// Converts SDL mouse button bitmask (bit 0 = left, bit 1 = middle, bit 2 = right)
// to Microsoft Mouse INT 33h button bitmask (bit 0 = left, bit 1 = right, bit 2 = middle).
std::uint16_t sdl_buttons_to_dos(std::uint32_t sdl_button_mask);

}  // namespace chq::mouse
