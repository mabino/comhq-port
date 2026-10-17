#include "app/mouse_control.h"

#include <algorithm>

#include "app/window_geometry.h"

namespace chq::mouse {

std::optional<std::pair<int, int>> logical_to_ega(float logical_x, float logical_y) {
    if (logical_x < 0.0f || logical_x >= static_cast<float>(WindowGeometry::kGameWidth) ||
        logical_y < 0.0f || logical_y >= static_cast<float>(WindowGeometry::kGameHeight)) {
        return std::nullopt;
    }
    const int ega_x = static_cast<int>(logical_x * (static_cast<float>(WindowGeometry::kEgaWidth) /
                                                    static_cast<float>(WindowGeometry::kGameWidth)));
    const int ega_y = static_cast<int>(logical_y * (static_cast<float>(WindowGeometry::kEgaHeight) /
                                                    static_cast<float>(WindowGeometry::kGameHeight)));
    return clamp_ega_coordinates(ega_x, ega_y);
}

std::pair<int, int> clamp_ega_coordinates(int ega_x, int ega_y) {
    return {
        std::clamp(ega_x, 0, WindowGeometry::kEgaWidth - 1),
        std::clamp(ega_y, 0, WindowGeometry::kEgaHeight - 1),
    };
}

std::uint16_t sdl_buttons_to_dos(std::uint32_t sdl_button_mask) {
    std::uint16_t dos_mask = 0;
    if (sdl_button_mask & 0x01u) dos_mask |= 0x01u;  // Left button (SDL_BUTTON_LMASK)
    if (sdl_button_mask & 0x04u) dos_mask |= 0x02u;  // Right button (SDL_BUTTON_RMASK)
    if (sdl_button_mask & 0x02u) dos_mask |= 0x04u;  // Middle button (SDL_BUTTON_MMASK)
    return dos_mask;
}

}  // namespace chq::mouse
