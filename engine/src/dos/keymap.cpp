#include "dos/keymap.h"

#include <cctype>
#include <charconv>
#include <cstring>

namespace chq::dos {

namespace {
// Scan codes for unshifted US keys, indexed by ASCII.
constexpr struct {
    char lower, upper;
    std::uint8_t scan;
} kKeys[] = {
    {'1', '!', 0x02}, {'2', '@', 0x03}, {'3', '#', 0x04}, {'4', '$', 0x05}, {'5', '%', 0x06},
    {'6', '^', 0x07}, {'7', '&', 0x08}, {'8', '*', 0x09}, {'9', '(', 0x0A}, {'0', ')', 0x0B},
    {'-', '_', 0x0C}, {'=', '+', 0x0D}, {'q', 'Q', 0x10}, {'w', 'W', 0x11}, {'e', 'E', 0x12},
    {'r', 'R', 0x13}, {'t', 'T', 0x14}, {'y', 'Y', 0x15}, {'u', 'U', 0x16}, {'i', 'I', 0x17},
    {'o', 'O', 0x18}, {'p', 'P', 0x19}, {'[', '{', 0x1A}, {']', '}', 0x1B}, {'a', 'A', 0x1E},
    {'s', 'S', 0x1F}, {'d', 'D', 0x20}, {'f', 'F', 0x21}, {'g', 'G', 0x22}, {'h', 'H', 0x23},
    {'j', 'J', 0x24}, {'k', 'K', 0x25}, {'l', 'L', 0x26}, {';', ':', 0x27}, {'\'', '"', 0x28},
    {'`', '~', 0x29}, {'\\', '|', 0x2B}, {'z', 'Z', 0x2C}, {'x', 'X', 0x2D}, {'c', 'C', 0x2E},
    {'v', 'V', 0x2F}, {'b', 'B', 0x30}, {'n', 'N', 0x31}, {'m', 'M', 0x32}, {',', '<', 0x33},
    {'.', '>', 0x34}, {'/', '?', 0x35}, {' ', ' ', 0x39},
};
}  // namespace

std::optional<BiosKey> key_for_char(char ch) {
    if (ch == '\n' || ch == '\r') return BiosKey{0x1C, 0x0D};
    if (ch == '\b') return BiosKey{0x0E, 0x08};
    if (ch == '\t') return BiosKey{0x0F, 0x09};
    if (ch == 27) return BiosKey{0x01, 0x1B};
    for (const auto& k : kKeys)
        if (k.lower == ch || k.upper == ch) return BiosKey{k.scan, static_cast<std::uint8_t>(ch)};
    return std::nullopt;
}

std::optional<BiosKey> key_for_name(std::string_view name) {
    struct Named {
        const char* name;
        BiosKey key;
    };
    static constexpr Named kNamed[] = {
        {"UP", {0x48, 0}},     {"DOWN", {0x50, 0}},  {"LEFT", {0x4B, 0}},    {"RIGHT", {0x4D, 0}},
        {"HOME", {0x47, 0}},   {"END", {0x4F, 0}},   {"PGUP", {0x49, 0}},    {"PGDN", {0x51, 0}},
        {"INS", {0x52, 0}},    {"DEL", {0x53, 0}},   {"ESC", {0x01, 0x1B}},  {"ENTER", {0x1C, 0x0D}},
        {"BS", {0x0E, 0x08}},  {"TAB", {0x0F, 0x09}}, {"SPACE", {0x39, 0x20}},
    };
    for (const auto& n : kNamed)
        if (name == n.name) return n.key;
    const bool shifted = name.size() > 1 && name[0] == 'S' && name[1] == 'F';
    if (shifted) name.remove_prefix(1);
    if (name.size() >= 2 && name[0] == 'F') {
        int n = 0;
        auto [p, ec] = std::from_chars(name.data() + 1, name.data() + name.size(), n);
        if (ec == std::errc() && p == name.data() + name.size() && n >= 1 && n <= 10)
            return BiosKey{static_cast<std::uint8_t>((shifted ? 0x54 : 0x3B) + n - 1), 0};
    }
    return std::nullopt;
}

std::vector<BiosKey> parse_script(std::string_view s) {
    std::vector<BiosKey> out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') {
            const auto end = s.find('}', i);
            if (end == std::string_view::npos) break;
            const std::string_view tok = s.substr(i + 1, end - i - 1);
            i = end;
            if (tok.rfind("WAIT", 0) == 0) {
                int n = 1;
                std::from_chars(tok.data() + 4 + (tok.size() > 4), tok.data() + tok.size(), n);
                out.insert(out.end(), static_cast<std::size_t>(n), BiosKey{});
            } else if (tok.rfind("CLICK ", 0) == 0 || tok.rfind("RCLICK ", 0) == 0 || tok.rfind("MOUSE ", 0) == 0) {
                const bool right = tok[0] == 'R';
                const bool move_only = tok[0] == 'M';
                const std::size_t prefix = right ? 7 : 6;
                int x = 0, y = 0;
                const char* p = tok.data() + prefix;
                const char* e = tok.data() + tok.size();
                while (p < e && *p == ' ') ++p;
                auto [p2, ec1] = std::from_chars(p, e, x);
                while (p2 < e && *p2 == ' ') ++p2;
                auto [p3, ec2] = std::from_chars(p2, e, y);
                if (ec1 == std::errc() && ec2 == std::errc()) {
                    const std::int16_t mx = static_cast<std::int16_t>(x);
                    const std::int16_t my = static_cast<std::int16_t>(y);
                    if (move_only) {
                        out.push_back(BiosKey{0, 0, mx, my, 0});
                    } else {
                        out.push_back(BiosKey{0, 0, mx, my, static_cast<std::uint16_t>(right ? 2 : 1)});
                        out.push_back(BiosKey{0, 0, mx, my, 0});
                    }
                }
            } else if (auto k = key_for_name(tok)) {
                out.push_back(*k);
            }
            continue;
        }
        if (auto k = key_for_char(s[i])) out.push_back(*k);
    }
    return out;
}

}  // namespace chq::dos
