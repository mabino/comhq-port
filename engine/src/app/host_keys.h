#pragma once

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <optional>

#include "dos/keymap.h"

namespace chq {

// Host key press to the BIOS keystroke an IBM PC keyboard would produce.
std::optional<dos::BiosKey> host_key(SDL_Keycode key, SDL_Keymod mod, SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN);
// BIOS shift-state byte (0040:0017) for the current modifiers.
std::uint8_t host_shift_flags(SDL_Keymod mod);

}  // namespace chq
