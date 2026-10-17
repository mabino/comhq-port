"""Facts about CHQ.EXE (v1.97) and EGRAPHIC.EXE that static analysis cannot find on its own.

Segments and offsets are as in docs/findings.md (unrelocated, with EGRAPHIC.EXE
mapped at relative segment 0x4000 -> runtime segment 0x5000). Anything not
listed here is still handled at run time by the interpreter; these hints make
the full game and graphics overlay run as natively recompiled C++.
"""

from __future__ import annotations

import struct

from .mz import Executable

SHA256 = "8b6dabd3bafeb491"  # prefix of the CHQ.EXE these hints were made for

# Far routines that never return (MSC 5.1 _amsg_exit, _exit, _cexit).
NORETURN: set[tuple[int, int]] = {
    (0x29FA, 0x00B2),
    (0x29FA, 0x019A),
}

# Instructions rewritten at run time (if any). Each is split into its own block
# that is never recompiled, so the interpreter runs whatever bytes are there.
DYNAMIC: set[tuple[int, int]] = set()

# Code moved at run time before it executes.
MOVED: list[tuple[tuple[int, int], tuple[int, int], int]] = []

# Switch / jump tables inside CHQ.EXE code segments: (seg, table_offset, entry_count).
SWITCH_TABLES: tuple[tuple[int, int, int], ...] = (
    (0x10F1, 0x2EFA, 8),
    (0x10F1, 0x2FC6, 16),
    (0x2741, 0x135E, 6),
    (0x2741, 0x148C, 9),
    (0x29FA, 0x1116, 22),
    (0x2E32, 0x077E, 24),
    (0x2E32, 0x0C94, 11),
)

# Fixed entry points: main(), hardware interrupt handlers, mouse callback, CRT helpers,
# and EGRAPHIC.EXE internal indirect targets.
STATIC_SEEDS: tuple[tuple[int, int], ...] = (
    (0x0000, 0x0000),  # _main
    (0x1753, 0x01D1),  # INT 24h critical error handler
    (0x1786, 0x00AD),  # INT 09h keyboard ISR
    (0x24B0, 0x01A6),  # INT 08h PIT timer ISR
    (0x24B0, 0x0427),  # INT 33h mouse event callback
    (0x29FA, 0x00B2),  # INT 00h divide error handler
    (0x29FA, 0x019A),  # __exit via ds:[0x6aa8]
    (0x2D0B, 0x000C),  # EGA helper module
    (0x2E06, 0x0000),  # .PIC LZW image decoder entry
    (0x2E06, 0x000E),  # .PIC LZW row reader
)

# Internal indirect targets in EGRAPHIC.EXE (mapped at relative segment 0x4000).
OVERLAY_INTERNAL_SEEDS: tuple[tuple[int, int], ...] = (
    (0x4000, 0x1487),
    (0x4000, 0x14D3),
    (0x4000, 0x1507),
)


def seeds(exe: Executable, overlay: Executable | None = None) -> list[tuple[int, int]]:
    """Collect unrelocated (seg, off) code entry points from CHQ.EXE and EGRAPHIC.EXE."""
    found: set[tuple[int, int]] = set(STATIC_SEEDS)
    code_bases = {b for b in exe.segment_bases() if b < 0x2F5A}
    img = exe.image

    # 1. Far call / jump targets and function pointers from the MZ relocation table.
    for rseg, roff in exe.relocations:
        lin = rseg * 16 + roff
        if lin + 2 > len(img):
            continue
        tseg = exe.word(lin)
        if tseg in code_bases and lin >= 2:
            if lin >= 3 and img[lin - 3] in (0x9A, 0xEA):
                found.add((tseg, exe.word(lin - 2)))
            elif lin >= 4 and img[lin - 4] == 0xB8 and img[lin - 1] == 0xBA:
                found.add((tseg, exe.word(lin - 3)))
            elif lin >= 10 and img[lin - 10] == 0xC7 and img[lin - 9] == 0x06 and img[lin - 4] == 0xC7 and img[lin - 3] == 0x06:
                found.add((tseg, exe.word(lin - 6)))
            elif lin >= 8 and img[lin - 8] == 0xB8 and img[lin - 1] == 0xB8 and tseg == 0x1587:
                found.add((tseg, exe.word(lin - 7)))
            elif rseg == 0x2F5A:
                toff = exe.word(lin - 2)
                if tseg * 16 + toff < 0x2F5A * 16:
                    found.add((tseg, toff))

    # 2. Switch tables in CHQ.EXE.
    for seg, tbl_off, count in SWITCH_TABLES:
        base = seg * 16 + tbl_off
        if base + 2 * count <= len(img):
            for off in struct.unpack_from(f"<{count}H", img, base):
                found.add((seg, off))

    # 3. EGRAPHIC.EXE overlay entry points (relative segment 0x4000 -> runtime 0x5000).
    if overlay is not None and len(overlay.image) >= 0x34:
        count = struct.unpack_from("<H", overlay.image, 0x30)[0]
        if 0x32 + 2 * count <= len(overlay.image):
            for idx, off in enumerate(struct.unpack_from(f"<{count}H", overlay.image, 0x32)):
                # Slot 15 (0x000E) is the ASCII version banner, not code.
                if not (idx == 15 and off == 0x000E):
                    found.add((0x4000, off))
        found.update(OVERLAY_INTERNAL_SEEDS)

    return sorted(found - DYNAMIC)


# Entry points reached only through pointers built at run time, found by
# running the game (chq_headless --dump-interpreted) and verified as code.
# (segment, offset) as the code runs, segments unrelocated.
FOUND_AT_RUN_TIME: tuple[tuple[int, int], ...] = (
    (0x0EE3, 0x091A),
    (0x10F1, 0x1E6C),
    (0x10F1, 0x1EC0),
    (0x10F1, 0x2084),
    (0x10F1, 0x219A),
    (0x13F0, 0x0BE6),
    (0x2E06, 0x0162),
    (0x2E06, 0x01AE),
    (0x2E06, 0x0261),
    (0x2E32, 0x01D0),
    (0x2E32, 0x0212),
    (0x2E32, 0x0256),
)
