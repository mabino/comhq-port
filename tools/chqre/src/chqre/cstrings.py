"""Scan binary chunks for printable NUL-terminated C string literals."""

from __future__ import annotations


def _is_printable(b: int) -> bool:
    return 0x20 <= b <= 0x7E or b in (0x09, 0x0A, 0x0D)


def find(data: bytes, min_length: int = 4) -> list[tuple[int, str]]:
    """Return (offset, text) for NUL-terminated printable ASCII strings."""
    out: list[tuple[int, str]] = []
    i = 0
    n = len(data)
    while i < n:
        if _is_printable(data[i]):
            start = i
            while i < n and _is_printable(data[i]):
                i += 1
            if i < n and data[i] == 0 and (i - start) >= min_length:
                text = data[start:i].decode("ascii", errors="replace")
                if any(c.isalpha() for c in text):
                    out.append((start, text))
                i += 1
                continue
        i += 1
    return out
