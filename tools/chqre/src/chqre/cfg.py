"""Control-flow discovery over a relocated MZ load image (plus optional overlay).

Code is found by recursive descent from the entry point and a list of seed
addresses (targets only reachable through pointers: interrupt handlers,
function pointer tables in DGROUP, and EGRAPHIC.EXE overlay entry points).
Every branch target, and every instruction following a call, an interrupt or
a conditional branch, starts a basic block. Blocks are keyed by linear address
so that the same code reached through a different CS:IP pair maps to one block.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

from . import x86
from .mz import Executable

# DOS loads the image one paragraph-aligned PSP (256 bytes) after the PSP.
LOAD_SEGMENT = 0x1000
PSP_SEGMENT = LOAD_SEGMENT - 0x10
# First MCB allocation at runtime (after _astart shrinks CHQ.EXE to 0x4F5A)
# places EGRAPHIC.EXE deterministically at segment 0x5000.
OVERLAY_SEGMENT = 0x5000


@dataclass
class Program:
    image: bytes                       # relocated load image as code sees it (after run-time moves/overlay bind)
    load_segment: int
    segments: list[tuple[int, int]]    # (absolute segment, size) of each segment, load order
    entry: tuple[int, int]             # absolute CS, IP
    stack: tuple[int, int]             # absolute SS, SP
    min_alloc: int                     # paragraphs, from the MZ header
    original: bytes = b""              # relocated load image as DOS loads it
    insns: dict[int, x86.Insn] = field(default_factory=dict)       # by linear address
    block_starts: set[int] = field(default_factory=set)
    indirect: list[int] = field(default_factory=list)              # linear addresses of indirect branches
    errors: dict[int, str] = field(default_factory=dict)

    @property
    def base(self) -> int:
        return self.load_segment * 16

    def segment_of(self, lin: int) -> int:
        """Absolute segment whose range contains the linear address."""
        best = self.segments[0][0]
        for seg, _ in self.segments:
            if seg * 16 <= lin:
                best = seg
        return best

    def blocks(self) -> list[tuple[int, list[x86.Insn]]]:
        """Each block start with its instructions, ending at a control transfer
        or just before the next block start."""
        out = []
        for start in sorted(self.block_starts):
            body, at = [], start
            while at in self.insns:
                insn = self.insns[at]
                body.append(insn)
                at = insn.next
                if insn.op in x86.ENDS_BLOCK or at in self.block_starts:
                    break
            if body:
                out.append((start, body))
        return out


def load(exe: Executable, load_segment: int = LOAD_SEGMENT,
         moved: list[tuple[tuple[int, int], tuple[int, int], int]] = (),
         overlay: Executable | None = None,
         overlay_segment: int = OVERLAY_SEGMENT,
         dgroup_jump_table: tuple[int, int] | None = (0x2F5A, 0x5CEE)) -> Program:
    """Relocate `exe` at `load_segment`, apply `moved` byte copies, and optionally
    attach `overlay` (EGRAPHIC.EXE) at `overlay_segment` and bind its jump table."""
    image = bytearray(exe.image)
    for seg, off in exe.relocations:
        lin = seg * 16 + off
        word = image[lin] | image[lin + 1] << 8
        word = (word + load_segment) & 0xFFFF
        image[lin], image[lin + 1] = word & 0xFF, word >> 8
    original = bytearray(image)
    for (sseg, soff), (dseg, doff), n in moved:
        src, dst = sseg * 16 + soff, dseg * 16 + doff
        image[dst:dst + n] = original[src:src + n] if src >= dst else image[src:src + n]

    segs = [(load_segment + b, size) for b, size in exe.segments()]

    if overlay is not None:
        ovl_rel_bytes = (overlay_segment - load_segment) * 16
        if ovl_rel_bytes > len(image):
            pad = ovl_rel_bytes - len(image)
            image.extend(b"\x00" * pad)
            original.extend(b"\x00" * pad)
        ovl_img = bytearray(overlay.image)
        for seg, off in overlay.relocations:
            lin = seg * 16 + off
            word = (ovl_img[lin] | (ovl_img[lin + 1] << 8)) + overlay_segment
            ovl_img[lin] = word & 0xFF
            ovl_img[lin + 1] = (word >> 8) & 0xFF
        image.extend(ovl_img)
        original.extend(b"\x00" * len(ovl_img))
        segs.append((overlay_segment, len(ovl_img)))

    h = exe.header
    return Program(
        image=bytes(image),
        original=bytes(original),
        load_segment=load_segment,
        segments=segs,
        entry=(load_segment + h.cs, h.ip),
        stack=(load_segment + h.ss, h.sp),
        min_alloc=h.min_alloc,
    )


def discover(prog: Program, seeds: list[tuple[int, int]] = (),
             noreturn: set[tuple[int, int]] = frozenset()) -> Program:
    """seeds: absolute (segment, offset) pairs in addition to the entry point.
    noreturn: absolute far-call targets that never return (no fall-through block)."""
    base, end = prog.base, prog.base + len(prog.image)
    dgroup_seg = prog.load_segment + 0x2F5A
    work: list[tuple[int, int]] = []  # (linear, cs base)

    def add(seg: int, off: int) -> None:
        if seg == dgroup_seg and len(prog.image) > 0x2F5A * 16:
            return
        lin = seg * 16 + off
        if base <= lin < end:
            prog.block_starts.add(lin)
            work.append((lin, seg * 16))
        else:
            prog.errors[lin] = f"branch target {seg:04x}:{off:04x} outside the image"

    add(*prog.entry)
    for seg, off in seeds:
        add(seg, off)

    seen: set[int] = set()
    while work:
        lin, cs_base = work.pop()
        while base <= lin < end and lin not in seen:
            seen.add(lin)
            try:
                insn = x86.decode(prog.image, lin - base, cs_base - base)
            except x86.DecodeError as e:
                prog.errors[lin] = str(e)
                break
            insn.lin = lin
            if insn.target is not None:
                # Relative branches never wrap around the segment in practice, so
                # keep the target near the branch even if the CS guess is off.
                insn_target = cs_base + insn.target
                if insn_target - lin > 0x8000:
                    insn_target -= 0x10000
                elif lin - insn_target > 0x8000:
                    insn_target += 0x10000
                insn.target_lin = insn_target
            prog.insns[lin] = insn
            op = insn.op
            if op in ("jmp", "call") or op in x86.CONDITIONAL:
                prog.block_starts.add(insn_target)
                work.append((insn_target, cs_base))
            elif op in ("jmpf", "callf"):
                add(*insn.far)
            elif op in ("jmpi", "jmpfi", "calli", "callfi"):
                prog.indirect.append(lin)
            if op in x86.ENDS_BLOCK:
                if op in ("jmp", "jmpf", "jmpi", "jmpfi", "ret", "retf", "iret", "hlt"):
                    break
                if op == "callf" and insn.far in noreturn:
                    break
                prog.block_starts.add(insn.next)  # return site or fall-through
            lin = insn.next
    return prog
