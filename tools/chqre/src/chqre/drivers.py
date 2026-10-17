"""Command HQ overlay drivers (EGRAPHIC.EXE, TGRAPHIC.EXE, SERIAL.OVL),
sound drivers (IBM.DRV, ADLIB.DRV, TANDY.DRV, ROLAND.DRV), and FONTS.CHQ.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .mz import Executable


class DriverError(ValueError):
    pass


@dataclass(frozen=True)
class OverlayDriver:
    name: str
    version_date: str
    target_segment: int
    first_index: int
    entry_count: int
    entries: list[int]       # 16-bit offsets in the overlay segment
    code_entries: list[int]  # valid code offsets (excluding banner slot 15)


@dataclass(frozen=True)
class SoundDriver:
    name: str
    image_size: int
    header_size: int
    dispatch_offsets: list[int]


@dataclass(frozen=True)
class BitmapFont:
    first_char: int
    last_char: int
    bytes_per_row: int
    char_width: int
    height: int
    spacing_x: int
    spacing_y: int
    widths: list[int]              # one width per glyph from first_char..last_char
    rows: dict[int, list[int]]     # char code -> list of `height` row bitmasks


def parse_overlay_driver(raw: bytes) -> OverlayDriver:
    """Parse an Ozark Softscape MZ overlay driver (EGRAPHIC.EXE / TGRAPHIC.EXE / SERIAL.OVL)."""
    exe = Executable.parse(raw)
    img = exe.image
    if len(img) < 0x34:
        raise DriverError("overlay image too short for dispatch header")
    banner = img[0x10:0x28].split(b"\x00", 1)[0].decode("ascii", errors="replace")
    name = banner[:12].strip()
    date = banner[12:].strip()
    target_seg, _, _, first_idx, count = struct.unpack_from("<5H", img, 0x28)
    if 0x32 + 2 * count > len(img):
        raise DriverError("overlay dispatch table overruns image")
    entries = list(struct.unpack_from(f"<{count}H", img, 0x32))
    # Slot 15 in EGRAPHIC.EXE / TGRAPHIC.EXE points to 0x000E (the ASCII banner string).
    code_entries = [off for idx, off in enumerate(entries) if not (idx == 15 and off == 0x000E)]
    return OverlayDriver(
        name=name,
        version_date=date,
        target_segment=target_seg,
        first_index=first_idx,
        entry_count=count,
        entries=entries,
        code_entries=code_entries,
    )


def parse_sound_driver(raw: bytes, name: str = "IBM.DRV") -> SoundDriver:
    """Parse an Ozark Softscape .DRV sound driver (16-byte header + jump table at +0x25)."""
    if len(raw) < 0x10 + 0x45:
        raise DriverError("sound driver too short")
    total_len, hdr_len = struct.unpack_from("<HH", raw, 0)
    if hdr_len != 0x10 or total_len > len(raw):
        raise DriverError("invalid sound driver header")
    body = raw[hdr_len:]
    # At body[0x00..0x06]: 2E FF 97 25 00 (`call word ptr cs:[bx+0x25]`).
    if body[:5] != b"\x2e\xff\x97\x25\x00":
        raise DriverError("missing sound driver dispatch prologue")
    offsets = list(struct.unpack_from("<16H", body, 0x25))
    return SoundDriver(name=name, image_size=total_len, header_size=hdr_len, dispatch_offsets=offsets)


def parse_fonts_chq(raw: bytes) -> BitmapFont:
    """Decode FONTS.CHQ (proportional 5-scanline bitmap font for 0x20..0x7B)."""
    if len(raw) < 12:
        raise DriverError("FONTS.CHQ too short")
    count, table_off = struct.unpack_from("<HH", raw, 0)
    if count < 1 or table_off < 12 or table_off > len(raw):
        raise DriverError("invalid FONTS.CHQ directory")
    first_c, last_c, bpr, char_w, height, sp_x, sp_y, _ = raw[table_off - 8:table_off]
    if last_c < first_c or bpr < 1 or height < 1:
        raise DriverError("invalid FONTS.CHQ header metrics")
    num_glyphs = last_c - first_c + 1
    bitmap_off = table_off + num_glyphs
    stride = num_glyphs * bpr
    if bitmap_off + stride * height > len(raw):
        raise DriverError("truncated FONTS.CHQ glyph bitmaps")
    # Widths in FONTS.CHQ are stored in reverse character order (last_char down to first_char).
    rev_widths = list(raw[table_off:bitmap_off])
    widths = list(reversed(rev_widths))
    rows: dict[int, list[int]] = {}
    for idx in range(num_glyphs):
        ch = first_c + idx
        # Glyphs in each scanline are also indexed in reverse (last_char - ch).
        rev_idx = last_c - ch
        glyph_rows = [raw[bitmap_off + y * stride + rev_idx * bpr] for y in range(height)]
        rows[ch] = glyph_rows
    return BitmapFont(
        first_char=first_c,
        last_char=last_c,
        bytes_per_row=bpr,
        char_width=char_w,
        height=height,
        spacing_x=sp_x,
        spacing_y=sp_y,
        widths=widths,
        rows=rows,
    )
