#!/usr/bin/env python3
"""Generates the macOS app icon for Command HQ following Tahoe / modern macOS squircle guidelines."""
import math
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter


def make_icon() -> Image.Image:
    # Render at 2048x2048 supersampled, then downsample to 1024x1024.
    # For macOS Tahoe+, fill the entire continuous squircle (p=4.6).
    S = 2048
    cx, cy = S / 2.0, S / 2.0
    # Full-bleed Tahoe squircle radius (1024 in 2048 space -> 512 in 1024 space, filling the squircle edge-to-edge)
    rx, ry = 1024.0, 1024.0

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 1. Strategic Command War-Room background gradient
    for y in range(S):
        t = y / S
        r = int(8 + t * 10)
        g = int(20 + t * 16)
        b = int(44 - t * 14)
        draw.line([(0, y), (S, y)], fill=(r, g, b, 255))

    # Subtle background tactical grid
    grid_minor = (0, 140, 180, 38)
    for g_pos in range(128, S, 128):
        draw.line([(g_pos, 0), (g_pos, S)], fill=grid_minor, width=3)
        draw.line([(0, g_pos), (S, g_pos)], fill=grid_minor, width=3)

    # 2. Central Strategic Globe (slightly above vertical center to leave room for command banner)
    gx, gy = cx, cy - 110
    gr = 620

    # Outer radar / atmosphere glow
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g_draw = ImageDraw.Draw(glow)
    g_draw.ellipse([gx - gr - 36, gy - gr - 36, gx + gr + 36, gy + gr + 36], fill=(0, 210, 255, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(24))
    img.alpha_composite(glow)

    # Globe ocean fill
    draw.ellipse([gx - gr, gy - gr, gx + gr, gy + gr],
                 fill=(10, 42, 82, 255), outline=(0, 220, 255, 255), width=12)

    # Stylized continents on the strategic projection (EGA green / teal tactical aesthetic)
    continent_fill = (24, 118, 84, 240)
    continent_edge = (85, 255, 170, 220)

    # North America
    na_pts = [
        (gx - 430, gy - 290), (gx - 250, gy - 380), (gx - 120, gy - 260),
        (gx - 170, gy - 110), (gx - 260, gy - 40), (gx - 340, gy - 120),
    ]
    draw.polygon(na_pts, fill=continent_fill, outline=continent_edge, width=5)

    # South America
    sa_pts = [
        (gx - 220, gy + 10), (gx - 110, gy + 60), (gx - 140, gy + 260),
        (gx - 210, gy + 380), (gx - 260, gy + 200),
    ]
    draw.polygon(sa_pts, fill=continent_fill, outline=continent_edge, width=5)

    # Europe / Eurasia
    eu_pts = [
        (gx + 30, gy - 330), (gx + 260, gy - 390), (gx + 460, gy - 250),
        (gx + 410, gy - 70), (gx + 210, gy - 50), (gx + 50, gy - 160),
    ]
    draw.polygon(eu_pts, fill=continent_fill, outline=continent_edge, width=5)

    # Africa
    af_pts = [
        (gx + 30, gy - 60), (gx + 190, gy - 30), (gx + 240, gy + 150),
        (gx + 150, gy + 340), (gx + 50, gy + 190),
    ]
    draw.polygon(af_pts, fill=continent_fill, outline=continent_edge, width=5)

    # Latitude & Longitude rings across globe
    lat_col = (0, 210, 255, 95)
    for frac in (-0.6, -0.3, 0.0, 0.3, 0.6):
        ly = gy + frac * gr
        half_w = math.sqrt(max(0.0, 1.0 - frac * frac)) * gr
        draw.line([(gx - half_w, ly), (gx + half_w, ly)], fill=lat_col, width=4)

    for rx_frac in (0.32, 0.68):
        mw = gr * rx_frac
        draw.ellipse([gx - mw, gy - gr, gx + mw, gy + gr], outline=lat_col, width=4)
    draw.line([(gx, gy - gr), (gx, gy + gr)], fill=lat_col, width=4)

    # 3. Strategic Command Trajectories (ICBM / Air / Naval vectors in high-contrast EGA Red & Yellow)
    arc_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    a_draw = ImageDraw.Draw(arc_layer)
    # Trans-Atlantic strategic arc
    a_draw.arc([gx - 310, gy - 360, gx + 210, gy + 60], start=195, end=345, fill=(255, 85, 85, 255), width=12)
    # Polar strategic arc
    a_draw.arc([gx - 240, gy - 470, gx + 340, gy - 30], start=190, end=350, fill=(255, 235, 85, 255), width=10)
    img.alpha_composite(arc_layer)

    # Command HQ target nodes (pulsing concentric squares/circles)
    nodes = [
        (gx - 260, gy - 190, (85, 255, 255, 255)),   # Allied HQ
        (gx + 190, gy - 210, (255, 85, 85, 255)),    # Eastern Front HQ
        (gx + 70, gy - 170, (255, 235, 85, 255)),    # Central Theater
    ]
    for nx, ny, col in nodes:
        draw.rectangle([nx - 26, ny - 26, nx + 26, ny + 26], fill=(10, 20, 35, 230), outline=col, width=7)
        draw.ellipse([nx - 10, ny - 10, nx + 10, ny + 10], fill=col)

    # Central tactical crosshair reticle
    ret_r = 120
    draw.ellipse([gx - ret_r, gy - ret_r, gx + ret_r, gy + ret_r], outline=(255, 235, 85, 220), width=6)
    draw.line([(gx - ret_r - 35, gy), (gx - ret_r + 25, gy)], fill=(255, 235, 85, 240), width=6)
    draw.line([(gx + ret_r - 25, gy), (gx + ret_r + 35, gy)], fill=(255, 235, 85, 240), width=6)
    draw.line([(gx, gy - ret_r - 35), (gx, gy - ret_r + 25)], fill=(255, 235, 85, 240), width=6)
    draw.line([(gx, gy + ret_r - 25), (gx, gy + ret_r + 35)], fill=(255, 235, 85, 240), width=6)

    # 4. Command HQ Banner at bottom
    bar_y = S - 360
    draw.rounded_rectangle(
        [cx - 640, bar_y - 105, cx + 640, bar_y + 105],
        radius=36,
        fill=(12, 24, 48, 245),
        outline=(255, 215, 60, 255),
        width=10,
    )
    draw.rounded_rectangle(
        [cx - 620, bar_y - 87, cx + 620, bar_y + 87],
        radius=26,
        outline=(0, 210, 255, 180),
        width=4,
    )

    # Vector block glyphs for "COMMAND HQ"
    def draw_glyph(lines, ox, oy, scale, color=(255, 255, 255, 255)):
        for (x1, y1, x2, y2) in lines:
            draw.line(
                [(ox + x1 * scale, oy + y1 * scale), (ox + x2 * scale, oy + y2 * scale)],
                fill=color,
                width=14,
            )

    C = [(4, 0, 0, 0), (0, 0, 0, 6), (0, 6, 4, 6)]
    O = [(0, 0, 4, 0), (4, 0, 4, 6), (4, 6, 0, 6), (0, 6, 0, 0)]
    M = [(0, 6, 0, 0), (0, 0, 2, 3), (2, 3, 4, 0), (4, 0, 4, 6)]
    A = [(0, 6, 0, 2), (0, 2, 2, 0), (2, 0, 4, 2), (4, 2, 4, 6), (0, 4, 4, 4)]
    N = [(0, 6, 0, 0), (0, 0, 4, 6), (4, 6, 4, 0)]
    D = [(0, 0, 0, 6), (0, 0, 3, 0), (3, 0, 4, 1.5), (4, 1.5, 4, 4.5), (4, 4.5, 3, 6), (3, 6, 0, 6)]
    H = [(0, 0, 0, 6), (4, 0, 4, 6), (0, 3, 4, 3)]
    Q = [(0, 0, 4, 0), (4, 0, 4, 5), (4, 5, 0, 5), (0, 5, 0, 0), (2.5, 3.5, 4.5, 6)]

    letters = [C, O, M, M, A, N, D, None, H, Q]
    scale = 16
    spacing = 108
    start_x = cx - (len(letters) - 1) * spacing / 2.0 - 2 * scale
    for idx, glyph in enumerate(letters):
        if glyph:
            col = (255, 225, 85, 255) if idx >= 8 else (255, 255, 255, 255)
            draw_glyph(glyph, start_x + idx * spacing, bar_y - 3 * scale, scale, col)

    # 5. Build continuous squircle mask (superellipse p=4.6)
    mask = Image.new("L", (S, S), 0)
    m_draw = ImageDraw.Draw(mask)
    for y in range(S):
        dy = abs(y - cy) / ry
        if dy <= 1.0:
            rem = 1.0 - dy**4.6
            if rem > 0:
                dx = (rem**(1.0 / 4.6)) * rx
                m_draw.line([(int(cx - dx), y), (int(cx + dx), y)], fill=255)

    final_2k = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    final_2k.paste(img, (0, 0), mask=mask)

    # Rim highlight around the squircle border
    border_mask = mask.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.GaussianBlur(3))
    border_overlay = Image.new("RGBA", (S, S), (0, 220, 255, 160))
    final_2k.paste(border_overlay, (0, 0), mask=border_mask)

    return final_2k.resize((1024, 1024), Image.Resampling.LANCZOS)


def main():
    root = Path(__file__).resolve().parent.parent
    res_dir = root / "engine" / "resources"
    res_dir.mkdir(parents=True, exist_ok=True)

    master = make_icon()
    master.save(res_dir / "AppIcon-1024.png", "PNG")

    with tempfile.TemporaryDirectory() as tmpdir:
        iconset_dir = Path(tmpdir) / "AppIcon.iconset"
        iconset_dir.mkdir()

        sizes = [
            (16, 1, "icon_16x16.png"),
            (16, 2, "icon_16x16@2x.png"),
            (32, 1, "icon_32x32.png"),
            (32, 2, "icon_32x32@2x.png"),
            (128, 1, "icon_128x128.png"),
            (128, 2, "icon_128x128@2x.png"),
            (256, 1, "icon_256x256.png"),
            (256, 2, "icon_256x256@2x.png"),
            (512, 1, "icon_512x512.png"),
            (512, 2, "icon_512x512@2x.png"),
        ]

        for base_sz, scale, filename in sizes:
            dim = base_sz * scale
            resized = master.resize((dim, dim), Image.Resampling.LANCZOS)
            resized.save(iconset_dir / filename, "PNG")

        icns_path = res_dir / "AppIcon.icns"
        subprocess.run(["iconutil", "-c", "icns", str(iconset_dir), "-o", str(icns_path)], check=True)
        print(f"Generated {icns_path} ({icns_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
