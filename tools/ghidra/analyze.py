"""Headless Ghidra analysis of CHQ.EXE (run inside the chq-analysis container).

Imports the 16-bit MZ executable, then before auto-analysis:
  * renames each memory block after its role (main, module, crt, overlay-loader, data),
    using the segment map from chqre;
  * pins DS to DGROUP (0x2F5A) for all code segments, as Microsoft C's startup does,
    so global variables and string literals resolve in the decompiler;
  * types NUL-terminated C string literals in DGROUP.

Exports:
  functions.json  every function: address, name, segment role, size, callers/callees
  strings.json    C string literals with the functions that reference them
  summary.json    counts per segment role
  decomp/         optional C output per game segment (--decompile)

Usage: python analyze.py --binary /data/CHQ.EXE --out /work/out/ghidra [--decompile]
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "chqre" / "src"))
from chqre import cstrings, segments  # noqa: E402
from chqre.mz import Executable  # noqa: E402

GAME_ROLES = ("main", "module", "overlay-loader")


def seg_space(program):
    return program.getAddressFactory().getDefaultAddressSpace()


def seg_addr(space, segment: int, offset: int):
    from jpype import JInt

    return space.getAddress(JInt(segment), JInt(offset))


def load_base(program) -> int:
    space = seg_space(program)
    return min(b.getStart().getSegment() for b in program.getMemory().getBlocks()
               if b.getStart().getAddressSpace() == space)


def _big(v: int):
    from java.math import BigInteger  # type: ignore

    return BigInteger.valueOf(v)


def prepare(flat, exe: Executable, segs):
    from ghidra.program.model.data import TerminatedStringDataType  # type: ignore
    from ghidra.program.model.symbol import SourceType  # type: ignore

    program = flat.getCurrentProgram()
    space = seg_space(program)
    bias = load_base(program)
    mem, listing = program.getMemory(), program.getListing()
    ctx = program.getProgramContext()
    ds_reg = program.getRegister("DS")
    data = next(s for s in segs if s.role == "data")

    typed = 0
    tx = program.startTransaction("Prepare CHQ.EXE")
    try:
        for s in segs:
            start = seg_addr(space, bias + s.base, 0)
            block = mem.getBlock(start)
            if block is None:
                continue
            block.setName(f"{s.role}_{s.base:04x}")
            ctx.setValue(ds_reg, block.getStart(), block.getEnd(), _big(bias + data.base))
            if s.role == "data":
                chunk = exe.image[s.base * 16:s.base * 16 + s.size]
                for off, _ in cstrings.find(chunk, 4):
                    addr = seg_addr(space, bias + s.base, off)
                    try:
                        listing.createData(addr, TerminatedStringDataType.dataType)
                        typed += 1
                    except Exception:
                        pass
        entry = seg_addr(space, bias + exe.header.cs, exe.header.ip)
        program.getSymbolTable().createLabel(entry, "program_entry", SourceType.ANALYSIS)
    finally:
        program.endTransaction(tx, True)
    return typed


def name_functions(program, segs):
    from ghidra.program.model.symbol import SourceType  # type: ignore

    bias = load_base(program)
    role_of = {s.base: s.role for s in segs}
    tx = program.startTransaction("Name functions")
    try:
        for f in program.getFunctionManager().getFunctions(True):
            if not f.getName().startswith("FUN_"):
                continue
            a = f.getEntryPoint()
            seg = a.getSegment() - bias
            f.setName(f"{role_of.get(seg, 'seg')}_{seg:04x}_{a.getSegmentOffset():04x}", SourceType.ANALYSIS)
    finally:
        program.endTransaction(tx, True)


def export(program, segs, monitor):
    bias = load_base(program)
    role_of = {s.base: s.role for s in segs}
    fm, listing, refman = program.getFunctionManager(), program.getListing(), program.getReferenceManager()

    def ours(addr) -> str:
        return f"{addr.getSegment() - bias:04x}:{addr.getSegmentOffset():04x}"

    rows = []
    for f in fm.getFunctions(True):
        a = f.getEntryPoint()
        rows.append({
            "address": ours(a),
            "name": f.getName(),
            "role": role_of.get(a.getSegment() - bias),
            "size": int(f.getBody().getNumAddresses()),
            "callers": len(f.getCallingFunctions(monitor)),
            "callees": len(f.getCalledFunctions(monitor)),
        })

    strings = []
    for d in listing.getDefinedData(True):
        if "String" not in d.getDataType().getName():
            continue
        users = sorted({fm.getFunctionContaining(r.getFromAddress()).getName()
                        for r in refman.getReferencesTo(d.getAddress())
                        if fm.getFunctionContaining(r.getFromAddress()) is not None})
        strings.append({"address": ours(d.getAddress()), "text": str(d.getValue()), "used_by": users})
    return rows, strings


def decompile(program, rows, out_dir: Path, monitor):
    from ghidra.app.decompiler import DecompInterface  # type: ignore

    iface = DecompInterface()
    iface.openProgram(program)
    fm = program.getFunctionManager()
    by_seg: dict[str, list[str]] = defaultdict(list)
    failures = 0
    for f in fm.getFunctions(True):
        name = f.getName()
        role = name.split("_")[0]
        if role not in GAME_ROLES and name != "program_entry":
            continue
        res = iface.decompileFunction(f, 120, monitor)
        if not res.decompileCompleted():
            failures += 1
            by_seg[name.split("_")[1] if "_" in name else "entry"].append(f"/* {name}: decompile failed */\n")
            continue
        seg = f"{f.getEntryPoint().getSegment() - load_base(program):04x}"
        by_seg[seg].append(res.getDecompiledFunction().getC())
    target = out_dir / "decomp"
    target.mkdir(parents=True, exist_ok=True)
    for seg, chunks in by_seg.items():
        (target / f"seg_{seg}.c").write_text("\n".join(chunks))
    iface.dispose()
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--project", default=Path("/work/ghidra-projects"), type=Path)
    ap.add_argument("--project-name", default="chq")
    ap.add_argument("--decompile", action="store_true")
    args = ap.parse_args()

    exe = Executable.parse(args.binary.read_bytes())
    segs = segments.classify(exe)

    import pyghidra

    pyghidra.start()
    from ghidra.util.task import ConsoleTaskMonitor  # type: ignore

    monitor = ConsoleTaskMonitor()
    args.out.mkdir(parents=True, exist_ok=True)
    args.project.mkdir(parents=True, exist_ok=True)

    with pyghidra.open_program(
        str(args.binary), project_location=str(args.project), project_name=args.project_name, analyze=False
    ) as flat:
        program = flat.getCurrentProgram()
        typed = prepare(flat, exe, segs)
        flat.analyzeAll(program)
        name_functions(program, segs)
        rows, strings = export(program, segs, monitor)

        (args.out / "functions.json").write_text(json.dumps(rows, indent=1))
        (args.out / "strings.json").write_text(json.dumps(strings, indent=1))
        summary = {
            "functions": len(rows),
            "functions_by_role": dict(Counter(r["role"] for r in rows).most_common()),
            "c_strings_typed": typed,
            "strings_with_users": sum(1 for s in strings if s["used_by"]),
        }
        if args.decompile:
            summary["decompile_failures"] = decompile(program, rows, args.out, monitor)
        (args.out / "summary.json").write_text(json.dumps(summary, indent=1))
        print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
