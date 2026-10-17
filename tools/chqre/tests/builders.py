"""Build tiny synthetic MZ executables, overlay drivers, and FONTS.CHQ files for unit tests."""

from __future__ import annotations

import struct


def build_mz(segments: list[bytes], *, far_refs: list[tuple[int, int]] = (), cs: int = 0, ip: int = 0,
             ss_size: int = 16) -> bytes:
    """Lay out paragraph-aligned segments followed by a stack segment."""
    bases, tails, image = [], [], b""
    for s in segments:
        bases.append(len(image) // 16)
        image += s.ljust((len(s) + 15) // 16 * 16, b"\x00")
        tails.append(len(image) - bases[-1] * 16)
        image += b"\x00" * 16
    ss = len(image) // 16
    image += b"\x00" * ss_size
    image = bytearray(image)
    relocs = []
    for frm, to in far_refs:
        struct.pack_into("<H", image, bases[frm] * 16 + tails[frm], bases[to])
        relocs.append((bases[frm], tails[frm]))
    header_size = (28 + 4 * len(relocs) + 15) // 16 * 16
    total = header_size + len(image)
    pages, last = (total + 511) // 512, total % 512
    head = b"MZ" + struct.pack("<13H", last, pages, len(relocs), header_size // 16, 0, 0xFFFF,
                               ss, ss_size, 0, ip, bases[cs], 28, 0)
    head += b"".join(struct.pack("<HH", off, seg) for seg, off in relocs)
    return head.ljust(header_size, b"\x00") + bytes(image)


def build_overlay(entries: list[int], *, name: str = "EGRAPHIC.EXE", date: str = "02-20-89") -> bytes:
    """Build a synthetic Ozark Softscape MZ overlay driver."""
    img = bytearray(0x100)
    banner = (name.ljust(12) + date).encode("ascii")[:24]
    img[0x10:0x10 + len(banner)] = banner
    struct.pack_into("<5H", img, 0x28, 0, 0, 0, 0, len(entries))
    for i, off in enumerate(entries):
        struct.pack_into("<H", img, 0x32 + 2 * i, off)
        if off != 0x000E and off < len(img):
            img[off] = 0xCB  # retf
    return build_mz([bytes(img)])


def build_fonts_chq(first_char: int = 65, last_char: int = 66, height: int = 5) -> bytes:
    """Build a synthetic FONTS.CHQ proportional bitmap font."""
    count = last_char - first_char + 1
    header = struct.pack("<HH", 1, 12) + bytes([first_char, last_char, 1, 6, height, 1, 1, 0])
    # Widths in reverse order (last_char down to first_char)
    widths = bytes([4 + (i % 3) for i in range(count)])
    bitmaps = bytes([(y + i + 1) & 0xFF for y in range(height) for i in range(count)])
    return header + widths + bitmaps
