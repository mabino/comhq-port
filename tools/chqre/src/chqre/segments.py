"""Assign each segment of CHQ.EXE (Microsoft C 5.1) to a role."""

from __future__ import annotations

from dataclasses import dataclass

from . import cstrings
from .mz import Executable

SIGNATURES = (
    (b"MS Run-Time Library - Copyright (c) 1988, Microsoft Corp", "data"),
    (b"EGRAPHIC.EXE", "data"),
)


@dataclass(frozen=True)
class Segment:
    base: int
    size: int
    role: str
    strings: int

    def to_json(self) -> dict:
        return {"base": f"{self.base:04x}", "size": self.size, "role": self.role, "strings": self.strings}


def classify(exe: Executable, dgroup: int | None = None) -> list[Segment]:
    """Classify each segment of CHQ.EXE by role."""
    segs = exe.segments()
    if dgroup is None and segs:
        dgroup = segs[-1][0]
    out: list[Segment] = []
    for base, size in segs:
        chunk = exe.image[base * 16:base * 16 + size]
        if base == dgroup:
            role = "data"
        elif base == 0x0000:
            role = "main"
        elif base == exe.header.cs:
            role = "crt"
        elif base == 0x2CF5:
            role = "overlay-loader"
        else:
            role = next((r for sig, r in SIGNATURES if sig in chunk), "module")
        n = len(cstrings.find(chunk, 4))
        out.append(Segment(base, size, role, n))
    return out
