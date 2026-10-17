#include "dos/ega.h"

#include <algorithm>
#include <stdexcept>

namespace chq::dos {

namespace {
// 200-line EGA default palette registers: colors 0-7, and 16-23 (bit 4 set for intensity).
constexpr std::uint8_t kDefaultPalette[16] = {0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23};

// 64-entry EGA DAC palette for 200-line modes (Mode 0Dh, 0Eh, and text mode 3).
// In 200-line EGA/CGA modes, bit 4 acts as the global intensity bit matching
// the IBM 5153/CGA RGBI standard with the hardware brown fix (color 6 = 0xAA5500).
constexpr std::array<std::uint32_t, 64> kEgaPalette64 = {
    // 0x00..0x07 (Standard dark colors)
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu, 0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    // 0x08..0x0F (Alias of dark colors)
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu, 0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    // 0x10..0x17 (Standard bright colors - bit 4 intensity)
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu, 0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu,
    // 0x18..0x1F (Alias of bright colors)
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu, 0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu,
    // 0x20..0x27 (Alias of dark colors)
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu, 0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    // 0x28..0x2F (Alias of dark colors)
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu, 0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    // 0x30..0x37 (Alias of bright colors)
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu, 0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu,
    // 0x38..0x3F (Alias of bright colors - 350-line default indices 56..63)
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu, 0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu,
};

std::uint8_t rotate_right(std::uint8_t v, int n) {
    n &= 7;
    return static_cast<std::uint8_t>((v >> n) | (v << (8 - n)));
}
}  // namespace

Ega::Ega() {
    for (auto& p : planes_) p.assign(0x10000, 0);
    reset_palette();
}

void Ega::reset_palette() { std::copy(std::begin(kDefaultPalette), std::end(kDefaultPalette), attr_.begin()); }

void Ega::set_mode(std::uint8_t mode, bool clear) {
    mode_ = mode & 0x7F;
    start_ = 0;
    map_mask_ = 0x0F;
    set_reset_ = enable_set_reset_ = color_compare_ = rotate_ = read_map_ = gc_mode_ = 0;
    color_dont_care_ = 0x0F;
    bit_mask_ = 0xFF;
    reset_palette();
    border_ = 0;
    if (clear)
        for (auto& p : planes_) std::fill(p.begin(), p.end(), 0);
}

void Ega::set_page(int n) {
    if (!graphics()) {
        start_ = static_cast<std::uint16_t>(n * 0x0800);
    } else if (mode_ == 0x0D) {
        start_ = static_cast<std::uint16_t>(n * 0x2000);
    } else if (mode_ == 0x0E) {
        start_ = static_cast<std::uint16_t>(n * 0x4000);
    } else {
        start_ = static_cast<std::uint16_t>(n * 0x8000);
    }
}

std::uint8_t Ega::read(std::uint16_t off) {
    for (int p = 0; p < 4; ++p) latch_[p] = planes_[p][off];
    if (!(gc_mode_ & 0x08)) return latch_[read_map_ & 3];
    // Read mode 1: a 1 bit for each pixel whose colour matches Color Compare
    // in the planes selected by Color Don't Care.
    std::uint8_t match = 0xFF;
    for (int p = 0; p < 4; ++p) {
        if (!(color_dont_care_ & (1 << p))) continue;
        const std::uint8_t want = (color_compare_ & (1 << p)) ? 0xFF : 0x00;
        match &= static_cast<std::uint8_t>(~(latch_[p] ^ want));
    }
    return match;
}

void Ega::write(std::uint16_t off, std::uint8_t v) {
    const int wmode = gc_mode_ & 3;
    const int func = (rotate_ >> 3) & 3;
    for (int p = 0; p < 4; ++p) {
        if (!(map_mask_ & (1 << p))) continue;
        std::uint8_t data;
        if (wmode == 1) {
            planes_[p][off] = latch_[p];
            continue;
        }
        if (wmode == 2) {
            data = (v & (1 << p)) ? 0xFF : 0x00;
        } else {
            data = rotate_right(v, rotate_ & 7);
            if (enable_set_reset_ & (1 << p)) data = (set_reset_ & (1 << p)) ? 0xFF : 0x00;
        }
        switch (func) {
            case 1: data &= latch_[p]; break;
            case 2: data |= latch_[p]; break;
            case 3: data ^= latch_[p]; break;
            default: break;
        }
        planes_[p][off] = static_cast<std::uint8_t>((data & bit_mask_) | (latch_[p] & ~bit_mask_));
    }
}

void Ega::port_out(std::uint16_t port, std::uint8_t v) {
    switch (port) {
        case 0x3C0:
            if (!attr_flip_) {
                attr_index_ = v & 0x1F;
            } else if (attr_index_ < 16) {
                attr_[attr_index_] = v & 0x3F;
            } else if (attr_index_ == 0x11) {
                border_ = v & 0x3F;
            }
            attr_flip_ = !attr_flip_;
            break;
        case 0x3C4: seq_index_ = v; break;
        case 0x3C5:
            if (seq_index_ == 2) map_mask_ = v & 0x0F;
            break;
        case 0x3CE: gc_index_ = v; break;
        case 0x3CF:
            switch (gc_index_ & 0x0F) {
                case 0: set_reset_ = v & 0x0F; break;
                case 1: enable_set_reset_ = v & 0x0F; break;
                case 2: color_compare_ = v & 0x0F; break;
                case 3: rotate_ = v & 0x1F; break;
                case 4: read_map_ = v & 3; break;
                case 5: gc_mode_ = v; break;
                case 7: color_dont_care_ = v & 0x0F; break;
                case 8: bit_mask_ = v; break;
                default: break;
            }
            break;
        case 0x3D4: crtc_index_ = v; break;
        case 0x3D5:
            if (crtc_index_ == 0x0C) start_ = static_cast<std::uint16_t>((start_ & 0x00FF) | v << 8);
            if (crtc_index_ == 0x0D) start_ = static_cast<std::uint16_t>((start_ & 0xFF00) | v);
            if (crtc_index_ == 0x0E) cursor_ = static_cast<std::uint16_t>((cursor_ & 0x00FF) | v << 8);
            if (crtc_index_ == 0x0F) cursor_ = static_cast<std::uint16_t>((cursor_ & 0xFF00) | v);
            break;
        default: break;
    }
}

std::uint8_t Ega::port_in(std::uint16_t port) {
    switch (port) {
        case 0x3DA:
        case 0x3BA:
            // Input status 1: reading resets the attribute flip-flop. Toggle
            // retrace and display-enable so polling loops always finish.
            attr_flip_ = false;
            retrace_ = !retrace_;
            return retrace_ ? 0x09 : 0x00;
        case 0x3C4: return seq_index_;
        case 0x3C5: return map_mask_;
        case 0x3CE: return gc_index_;
        case 0x3CF:
            switch (gc_index_ & 0x0F) {
                case 0: return set_reset_;
                case 1: return enable_set_reset_;
                case 2: return color_compare_;
                case 3: return rotate_;
                case 4: return read_map_;
                case 5: return gc_mode_;
                case 7: return color_dont_care_;
                case 8: return bit_mask_;
                default: return 0xFF;
            }
        case 0x3D4: return crtc_index_;
        case 0x3D5:
            if (crtc_index_ == 0x0C) return static_cast<std::uint8_t>(start_ >> 8);
            if (crtc_index_ == 0x0D) return static_cast<std::uint8_t>(start_ & 0xFF);
            if (crtc_index_ == 0x0E) return static_cast<std::uint8_t>(cursor_ >> 8);
            if (crtc_index_ == 0x0F) return static_cast<std::uint8_t>(cursor_ & 0xFF);
            return 0xFF;
        default: return 0xFF;
    }
}

std::uint32_t Ega::rgb(std::uint8_t c) {
    return kEgaPalette64[c & 0x3F];
}

int Ega::pixel(int x, int y) const {
    const int stride = bytes_per_row();
    const std::uint16_t off = static_cast<std::uint16_t>(start_ + y * stride + (x >> 3));
    const int bit = 7 - (x & 7);
    int c = 0;
    for (int p = 0; p < 4; ++p) c |= ((planes_[p][off] >> bit) & 1) << p;
    return c;
}

void Ega::render(std::span<std::uint32_t> argb, const std::uint8_t* text_mem, const std::uint8_t* font8x8,
                 bool blink_on) const {
    if (argb.size() < static_cast<std::size_t>(kWidth * kHeight)) throw std::length_error("Ega::render: buffer");
    std::array<std::uint32_t, 16> pal;
    for (int i = 0; i < 16; ++i) pal[i] = rgb(attr_[i]);

    if (graphics()) {
        if (mode_ == 0x0D) {
            // Mode 0Dh: 320x200x16 (40 bytes/row), integer-upscaled 2x2 to 640x400.
            for (int y = 0; y < kNativeHeight; ++y) {
                std::uint32_t* row0 = argb.data() + (y * 2) * kWidth;
                std::uint32_t* row1 = row0 + kWidth;
                for (int xb = 0; xb < 40; ++xb) {
                    const std::uint16_t off = static_cast<std::uint16_t>(start_ + y * 40 + xb);
                    const std::uint8_t b0 = planes_[0][off], b1 = planes_[1][off], b2 = planes_[2][off],
                                       b3 = planes_[3][off];
                    for (int bit = 7; bit >= 0; --bit) {
                        const int c = ((b0 >> bit) & 1) | ((b1 >> bit) & 1) << 1 | ((b2 >> bit) & 1) << 2 |
                                      ((b3 >> bit) & 1) << 3;
                        const std::uint32_t px = pal[c];
                        *row0++ = px;
                        *row0++ = px;
                        *row1++ = px;
                        *row1++ = px;
                    }
                }
            }
            return;
        }
        if (mode_ == 0x0E) {
            // Mode 0Eh: 640x200x16 (80 bytes/row), line-doubled to 640x400.
            for (int y = 0; y < kNativeHeight; ++y) {
                std::uint32_t* row0 = argb.data() + (y * 2) * kWidth;
                std::uint32_t* row1 = row0 + kWidth;
                for (int xb = 0; xb < 80; ++xb) {
                    const std::uint16_t off = static_cast<std::uint16_t>(start_ + y * 80 + xb);
                    const std::uint8_t b0 = planes_[0][off], b1 = planes_[1][off], b2 = planes_[2][off],
                                       b3 = planes_[3][off];
                    for (int bit = 7; bit >= 0; --bit) {
                        const int c = ((b0 >> bit) & 1) | ((b1 >> bit) & 1) << 1 | ((b2 >> bit) & 1) << 2 |
                                      ((b3 >> bit) & 1) << 3;
                        const std::uint32_t px = pal[c];
                        *row0++ = px;
                        *row1++ = px;
                    }
                }
            }
            return;
        }
        // Mode 10h: 640x350x16 centered in 640x400.
        std::fill(argb.begin(), argb.begin() + kWidth * kHeight, pal[0]);
        for (int y = 0; y < 350; ++y) {
            std::uint32_t* row = argb.data() + (y + 25) * kWidth;
            for (int xb = 0; xb < 80; ++xb) {
                const std::uint16_t off = static_cast<std::uint16_t>(start_ + y * 80 + xb);
                const std::uint8_t b0 = planes_[0][off], b1 = planes_[1][off], b2 = planes_[2][off],
                                   b3 = planes_[3][off];
                for (int bit = 7; bit >= 0; --bit) {
                    const int c = ((b0 >> bit) & 1) | ((b1 >> bit) & 1) << 1 | ((b2 >> bit) & 1) << 2 |
                                  ((b3 >> bit) & 1) << 3;
                    *row++ = pal[c];
                }
            }
        }
        return;
    }

    // Text mode: 80x25 cells of 8x16 pixels; the 8x8 ROM glyphs are centred vertically.
    std::fill(argb.begin(), argb.begin() + kWidth * kHeight, pal[0]);
    if (!text_mem) return;
    const int cols = (mode_ <= 1) ? 40 : 80;
    const int cw = kWidth / cols;
    for (int row = 0; row < 25; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int cell = (start_ + row * cols + col) * 2;
            const std::uint8_t ch = text_mem[cell & 0x7FFF], at = text_mem[(cell + 1) & 0x7FFF];
            const std::uint32_t fg = pal[at & 0x0F], bg = pal[(at >> 4) & 0x07];
            const bool hide = (at & 0x80) && !blink_on;
            for (int py = 0; py < 16; ++py) {
                const int gy = py - 4;
                const std::uint8_t bits = (gy >= 0 && gy < 8 && ch < 128 && font8x8) ? font8x8[ch * 8 + gy] : 0;
                for (int px = 0; px < cw; ++px) {
                    const bool on = !hide && (bits & (0x80 >> (px * 8 / cw)));
                    argb[(row * 16 + py) * kWidth + col * cw + px] = on ? fg : bg;
                }
            }
        }
    }
    const int cur = cursor_ - start_;
    if (blink_on && cur >= 0 && cur < cols * 25) {
        const int cx = (cur % cols) * cw, cy = (cur / cols) * 16;
        const std::uint32_t fg = pal[text_mem[(cursor_ * 2 + 1) & 0x7FFF] & 0x0F];
        for (int py = 12; py < 14; ++py)
            for (int px = 0; px < cw; ++px) argb[(cy + py) * kWidth + cx + px] = fg;
    }
}

}  // namespace chq::dos
