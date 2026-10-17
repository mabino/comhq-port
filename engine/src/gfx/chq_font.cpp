#include "gfx/chq_font.h"

#include <algorithm>

#include "core/byte_reader.h"

namespace chq {

ChqFont ChqFont::parse(std::span<const std::byte> data) {
    ByteReader r(data);
    if (r.size() < 12) throw ReadError("FONTS.CHQ too short");
    const std::uint16_t count = r.u16();
    const std::uint16_t table_off = r.u16();
    if (count < 1 || table_off < 12 || table_off > r.size()) throw ReadError("invalid FONTS.CHQ directory");

    r.seek(table_off - 8);
    ChqFont f;
    f.first_ = r.u8();
    f.last_ = r.u8();
    const int bpr = r.u8();
    f.char_width_ = r.u8();
    f.height_ = r.u8();
    f.spacing_x_ = r.u8();
    f.spacing_y_ = r.u8();
    (void)r.u8();  // flags

    if (f.last_ < f.first_ || bpr != 1 || f.height_ < 1) throw ReadError("invalid FONTS.CHQ metrics");
    const int num_glyphs = f.last_ - f.first_ + 1;
    const std::size_t bitmap_off = static_cast<std::size_t>(table_off) + static_cast<std::size_t>(num_glyphs);
    const std::size_t stride = static_cast<std::size_t>(num_glyphs * bpr);
    if (bitmap_off + stride * static_cast<std::size_t>(f.height_) > r.size()) {
        throw ReadError("truncated FONTS.CHQ glyph bitmaps");
    }

    r.seek(table_off);
    std::vector<std::uint8_t> rev_widths(num_glyphs);
    for (int i = 0; i < num_glyphs; ++i) rev_widths[i] = r.u8();
    f.widths_.assign(rev_widths.rbegin(), rev_widths.rend());

    f.glyphs_.resize(num_glyphs);
    for (int i = 0; i < num_glyphs; ++i) {
        const int rev_idx = num_glyphs - 1 - i;
        auto& rows = f.glyphs_[i];
        rows.resize(f.height_);
        for (int y = 0; y < f.height_; ++y) {
            r.seek(bitmap_off + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(rev_idx));
            rows[y] = r.u8();
        }
    }
    return f;
}

int ChqFont::width(unsigned char c) const {
    const int i = static_cast<int>(c) - first_;
    if (i < 0 || i >= glyph_count()) return 0;
    return widths_[i];
}

std::span<const std::uint8_t> ChqFont::glyph_rows(unsigned char c) const {
    const int i = static_cast<int>(c) - first_;
    if (i < 0 || i >= glyph_count()) return {};
    return glyphs_[i];
}

int ChqFont::text_width(std::string_view text) const {
    int w = 0;
    bool any = false;
    for (unsigned char c : text) {
        const int gw = width(c);
        if (gw <= 0) continue;
        if (any) w += spacing_x_;
        w += gw;
        any = true;
    }
    return w;
}

void ChqFont::draw(EgaSurface& s, int x, int y, std::string_view text, std::uint8_t color) const {
    int pen = x;
    for (unsigned char c : text) {
        const auto rows = glyph_rows(c);
        const int gw = width(c);
        for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
            const std::uint8_t bits = rows[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (0x80u >> col)) s.put(pen + col, y + row, color);
            }
        }
        pen += gw + spacing_x_;
    }
}

}  // namespace chq
