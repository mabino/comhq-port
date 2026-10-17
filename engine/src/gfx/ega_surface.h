#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace chq {

// EGA 16-colour drawing surface (320x200 native mode 0Dh resolution).
// Pixels hold palette indices; convert to ARGB when presenting.
class EgaSurface {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;

    EgaSurface() : pixels_(kWidth * kHeight, 0) {}

    std::uint8_t get(int x, int y) const;
    void put(int x, int y, std::uint8_t color);  // clipped
    void clear(std::uint8_t color = 0);

    // Bresenham line, both endpoints drawn.
    void line(int x1, int y1, int x2, int y2, std::uint8_t color);
    // Rectangle outline.
    void rectangle(int x1, int y1, int x2, int y2, std::uint8_t color);
    // Solid filled bar: inclusive corners, any corner order.
    void bar(int x1, int y1, int x2, int y2, std::uint8_t color);

    std::span<const std::uint8_t> pixels() const { return pixels_; }

    // Default EGA 16-colour palette, 0xAARRGGBB.
    static const std::array<std::uint32_t, 16>& default_palette();
    // Converts to 32-bit pixels with the given palette, row-major, kWidth wide.
    void to_argb(std::span<std::uint32_t> out, const std::array<std::uint32_t, 16>& palette) const;

private:
    std::vector<std::uint8_t> pixels_;
};

}  // namespace chq
