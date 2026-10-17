#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

// Little helpers for building synthetic binaries in tests (no game data needed).
struct Bytes {
    std::vector<std::byte> v;
    Bytes& u8(unsigned x) { v.push_back(std::byte(x & 0xFF)); return *this; }
    Bytes& u16(unsigned x) { return u8(x).u8(x >> 8); }
    Bytes& str(std::string_view s) { for (char c : s) u8(static_cast<unsigned char>(c)); return *this; }
    Bytes& pad_to(std::size_t n) { while (v.size() < n) u8(0); return *this; }
    Bytes& append(const Bytes& o) { v.insert(v.end(), o.v.begin(), o.v.end()); return *this; }
};

// A synthetic FONTS.CHQ bitmap font with two glyphs: 'A' (width 4, height 7) and 'B' (width 5, height 7).
inline Bytes make_chq_font() {
    Bytes f;
    // Directory: count=1, table_off=12
    f.u16(1).u16(12);
    // 8-byte metrics at table_off - 8: first='A'(65), last='B'(66), bpr=1, char_width=8, height=7, spacing_x=1, spacing_y=1, flags=0
    f.u8(65).u8(66).u8(1).u8(8).u8(7).u8(1).u8(1).u8(0);
    // Reverse-indexed widths at table_off: 'B' (5), 'A' (4)
    f.u8(5).u8(4);
    // 7 scanlines, each storing reverse-indexed glyph row bytes ('B' then 'A')
    const std::uint8_t rows_a[7] = {0xF0, 0x90, 0xF0, 0x90, 0x90, 0x00, 0x00};
    const std::uint8_t rows_b[7] = {0xE0, 0x90, 0xE0, 0x90, 0xE0, 0x00, 0x00};
    for (int y = 0; y < 7; ++y) f.u8(rows_b[y]).u8(rows_a[y]);
    return f;
}
