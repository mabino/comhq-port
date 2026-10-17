#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gfx/ega_surface.h"

namespace chq {

// Command HQ proportional bitmap font (FONTS.CHQ, loaded by EGRAPHIC.EXE).
class ChqFont {
public:
    static ChqFont parse(std::span<const std::byte> data);  // throws ReadError

    int first_char() const { return first_; }
    int last_char() const { return last_; }
    int height() const { return height_; }
    int spacing_x() const { return spacing_x_; }
    int glyph_count() const { return static_cast<int>(widths_.size()); }

    int width(unsigned char c) const;
    std::span<const std::uint8_t> glyph_rows(unsigned char c) const;
    int text_width(std::string_view text) const;
    void draw(EgaSurface& s, int x, int y, std::string_view text, std::uint8_t color) const;

private:
    int first_ = 0, last_ = 0, char_width_ = 0, height_ = 0, spacing_x_ = 0, spacing_y_ = 0;
    std::vector<std::uint8_t> widths_;
    std::vector<std::vector<std::uint8_t>> glyphs_;
};

}  // namespace chq
