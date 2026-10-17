"""Command-line entry point: `chqre {inventory,strings,font,dis,recomp,corpus}`."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

from . import cstrings, drivers, segments
from .mz import Executable


def _find_case_insensitive(folder: Path, filename: str) -> Path | None:
    if not folder.is_dir():
        return None
    target = filename.upper()
    for child in folder.iterdir():
        if child.name.upper() == target:
            return child
    return None


def _exe_path(arg: str | None) -> Path:
    value = arg or os.environ.get("CHQ_DATA") or os.environ.get("COMHQ_DATA")
    if not value:
        sys.exit("error: pass --data or set CHQ_DATA to the folder holding CHQ.EXE")
    folder = Path(value).expanduser().resolve()
    found = _find_case_insensitive(folder, "CHQ.EXE")
    if found is not None:
        return found
    sys.exit(f"error: {folder} does not contain CHQ.EXE")


def _write(path: str, obj) -> None:
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(obj, indent=1))
    print(f"Wrote {out}")


def inventory(raw: bytes, folder: Path | None = None) -> dict:
    exe = Executable.parse(raw)
    h = exe.header
    segs = segments.classify(exe)
    drv_info = []
    if folder is not None:
        for ovl_name in ("EGRAPHIC.EXE", "TGRAPHIC.EXE", "SERIAL.OVL"):
            p = _find_case_insensitive(folder, ovl_name)
            if p is not None:
                d = drivers.parse_overlay_driver(p.read_bytes())
                drv_info.append({
                    "kind": "overlay",
                    "name": d.name or ovl_name,
                    "version_date": d.version_date,
                    "entry_count": d.entry_count,
                    "code_entries": len(d.code_entries),
                })
        for snd_name in ("IBM.DRV", "ADLIB.DRV", "TANDY.DRV", "ROLAND.DRV"):
            p = _find_case_insensitive(folder, snd_name)
            if p is not None:
                sd = drivers.parse_sound_driver(p.read_bytes(), snd_name)
                drv_info.append({
                    "kind": "sound",
                    "name": sd.name,
                    "size": sd.image_size,
                    "dispatch_count": len(sd.dispatch_offsets),
                })
    return {
        "sha256": hashlib.sha256(raw).hexdigest(),
        "file_size": len(raw),
        "image_size": len(exe.image),
        "entry": f"{h.cs:04x}:{h.ip:04x}",
        "stack": f"{h.ss:04x}:{h.sp:04x}",
        "relocations": len(exe.relocations),
        "segments": [s.to_json() for s in segs],
        "drivers": drv_info,
    }


def _cmd_inventory(args) -> int:
    exe_file = _exe_path(args.data)
    result = inventory(exe_file.read_bytes(), exe_file.parent)
    print(f"CHQ.EXE  sha256 {result['sha256'][:16]}…  entry {result['entry']}  "
          f"relocations {result['relocations']}")
    for s in result["segments"]:
        print(f"  {s['base']}  {s['size']:6d}  {s['role']:14}  {s['strings'] or ''}")
    for d in result["drivers"]:
        print(f"  {d['kind']:8} {d['name']:12}  {d}")
    _write(args.out, result)
    return 0


def _cmd_strings(args) -> int:
    exe = Executable.parse(_exe_path(args.data).read_bytes())
    rows = []
    for seg in segments.classify(exe):
        chunk = exe.image[seg.base * 16:seg.base * 16 + seg.size]
        rows += [{"segment": f"{seg.base:04x}", "offset": f"{o:04x}", "text": t}
                 for o, t in cstrings.find(chunk, args.min)]
    print(f"{len(rows)} strings")
    _write(args.out, rows)
    return 0


def _cmd_font(args) -> int:
    exe_file = _exe_path(args.data)
    font_file = _find_case_insensitive(exe_file.parent, "FONTS.CHQ")
    if font_file is None:
        sys.exit(f"error: {exe_file.parent} does not contain FONTS.CHQ")
    f = drivers.parse_fonts_chq(font_file.read_bytes())
    print(f"FONTS.CHQ: {len(f.widths)} glyphs ({chr(f.first_char)!r}..{chr(f.last_char)!r}), "
          f"{f.char_width}x{f.height}")
    _write(args.out, {
        "first_char": f.first_char,
        "last_char": f.last_char,
        "char_width": f.char_width,
        "height": f.height,
        "spacing_x": f.spacing_x,
        "spacing_y": f.spacing_y,
        "widths": f.widths,
        "rows": {str(k): v for k, v in f.rows.items()},
    })
    return 0


def _cmd_dis(args) -> int:
    from . import cfg, x86

    exe_file = _exe_path(args.data)
    ovl_file = _find_case_insensitive(exe_file.parent, "EGRAPHIC.EXE")
    ovl = Executable.parse(ovl_file.read_bytes()) if ovl_file else None
    prog = cfg.load(Executable.parse(exe_file.read_bytes()), overlay=ovl)
    seg, off = (int(x, 16) for x in args.address.split(":"))
    at = seg * 16 + off
    for _ in range(args.count):
        try:
            insn = x86.decode(prog.image, at, seg * 16)
        except x86.DecodeError as e:
            print(f"{seg:04x}:{at - seg * 16:04x}  {prog.image[at]:02x}             ?? {e}")
            at += 1
            continue
        print(f"{seg:04x}:{at - seg * 16:04x}  {prog.image[at:at + insn.size].hex():14} {insn}")
        at += insn.size
    return 0


def recompile(raw: bytes, out_dir: Path, extra_seeds: list[tuple[int, int]] = (),
              overlay_raw: bytes | None = None) -> dict:
    """extra_seeds: (segment, offset) pairs relative to the load segment, as
    chq_headless --dump-interpreted writes them."""
    from . import cfg, codegen, hints

    sha = hashlib.sha256(raw).hexdigest()
    if not sha.startswith(hints.SHA256):
        print(f"warning: CHQ.EXE sha256 {sha[:16]}… differs from the version the hints were made for",
              file=sys.stderr)
    exe = Executable.parse(raw)
    ovl = Executable.parse(overlay_raw) if overlay_raw is not None else None
    prog = cfg.load(exe, moved=hints.MOVED, overlay=ovl)
    L = prog.load_segment
    absolute = lambda pairs: {(L + s, o) for s, o in pairs}  # noqa: E731
    exclude = {(L + s) * 16 + o for s, o in hints.DYNAMIC}
    found = {(L + sg, off) for sg, off in list(extra_seeds) + list(hints.FOUND_AT_RUN_TIME)}
    max_rel_seg = len(prog.image) // 16
    valid_found = {(s, o) for s, o in found if L <= s <= L + max_rel_seg and (s * 16 + o) not in exclude}
    seed_list = sorted(absolute(hints.seeds(exe, ovl)) | valid_found)
    cfg.discover(prog, seeds=seed_list, noreturn=absolute(hints.NORETURN))
    prog.block_starts |= {lin for lin in exclude if lin in prog.insns}
    paths = codegen.generate(prog, out_dir, exclude=exclude, source_sha256=sha)
    return {"instructions": len(prog.insns), "blocks": len(prog.block_starts - exclude),
            "indirect_branches": len(prog.indirect), "decode_errors": len(prog.errors),
            "files": [p.name for p in paths]}


def _cmd_recomp(args) -> int:
    extra = []
    if args.extra_seeds:
        extra = [tuple(int(x, 16) for x in line.split(":"))
                 for line in Path(args.extra_seeds).read_text().split() if ":" in line]
    exe_file = _exe_path(args.data)
    ovl_file = _find_case_insensitive(exe_file.parent, "EGRAPHIC.EXE")
    ovl_bytes = ovl_file.read_bytes() if ovl_file is not None else None
    stats = recompile(exe_file.read_bytes(), Path(args.out), extra, overlay_raw=ovl_bytes)
    print(f"{stats['instructions']} instructions in {stats['blocks']} blocks -> {len(stats['files'])} files "
          f"in {args.out} ({stats['decode_errors']} decode errors)")
    return 0


def _cmd_corpus(args) -> int:
    from . import corpus

    cases = corpus.generate(seed=args.seed)
    corpus.write(cases, Path(args.out))
    print(f"{len(cases)} cases -> {args.out}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="chqre")
    sub = parser.add_subparsers(dest="command", required=True)
    for name, func, default, help_ in (
        ("inventory", _cmd_inventory, "out/inventory.json", "segment map and overlay/sound drivers"),
        ("strings", _cmd_strings, "out/strings.json", "C string constants by segment"),
        ("font", _cmd_font, "out/fonts-chq.json", "decode the FONTS.CHQ bitmap font"),
    ):
        p = sub.add_parser(name, help=help_)
        p.add_argument("--data", help="folder holding CHQ.EXE (default: $CHQ_DATA)")
        p.add_argument("--out", default=default)
        if name == "strings":
            p.add_argument("--min", type=int, default=4, help="minimum string length")
        p.set_defaults(func=func)
    d = sub.add_parser("dis", help="linear disassembly at SEG:OFF (segments unrelocated)")
    d.add_argument("address")
    d.add_argument("count", type=int, nargs="?", default=40)
    d.add_argument("--data", help="folder holding CHQ.EXE (default: $CHQ_DATA)")
    d.set_defaults(func=_cmd_dis)
    rc = sub.add_parser("recomp", help="recompile CHQ.EXE (+ EGRAPHIC.EXE) to C++ for the engine")
    rc.add_argument("--data", help="folder holding CHQ.EXE (default: $CHQ_DATA)")
    rc.add_argument("--out", default="build/generated")
    rc.add_argument("--extra-seeds", help="file of SEG:OFF entry points found at run time")
    rc.set_defaults(func=_cmd_recomp)
    co = sub.add_parser("corpus", help="instruction test corpus from Unicorn (engine/tests/generated)")
    co.add_argument("--out", default="../../engine/tests/generated")
    co.add_argument("--seed", type=int, default=1990)
    co.set_defaults(func=_cmd_corpus)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
