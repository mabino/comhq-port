#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chq::dos {

// A BIOS keystroke or scripted mouse event.
struct BiosKey {
    std::uint8_t scan = 0, ascii = 0;
    std::int16_t mouse_x = -1, mouse_y = -1;
    std::uint16_t mouse_buttons = 0;
    bool operator==(const BiosKey&) const = default;
};

// US-layout ASCII character to BIOS keystroke.
std::optional<BiosKey> key_for_char(char ch);

// Named keys: "F1".."F10", "SF1".."SF10" (shifted), "UP", "DOWN", "LEFT",
// "RIGHT", "HOME", "END", "PGUP", "PGDN", "INS", "DEL", "ESC", "ENTER",
// "BS", "TAB", "SPACE".
std::optional<BiosKey> key_for_name(std::string_view name);

// A scripted input sequence: plain characters, "\n" for Enter, "{NAME}" for
// named keys, "{WAIT n}" to idle n frames, and "{CLICK x y}" / "{RCLICK x y}"
// / "{MOUSE x y}" for 320x200 mouse events.
std::vector<BiosKey> parse_script(std::string_view script);

}  // namespace chq::dos
