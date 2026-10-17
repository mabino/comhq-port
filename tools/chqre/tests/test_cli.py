import json

import pytest

from builders import build_fonts_chq, build_mz, build_overlay
from chqre import segments
from chqre.cli import inventory, main
from chqre.mz import Executable

DATA_SEG = b"MS Run-Time Library - Copyright (c) 1988, Microsoft Corp\x00EGRAPHIC.EXE\x00"


@pytest.fixture
def fake_exe() -> bytes:
    main_seg = b"\x55\x8b\xecCommand HQ test\x00" + b"\x90" * 4
    crt_seg = b"\xb4\x30\xcd\x21\xcb" + b"\x90" * 11
    return build_mz([main_seg, crt_seg, DATA_SEG], far_refs=[(0, 1), (1, 2)], cs=1, ip=0)


def test_classify(fake_exe):
    roles = [s.role for s in segments.classify(Executable.parse(fake_exe))]
    assert roles == ["main", "crt", "data", "data"]


def test_inventory_and_cli(fake_exe, tmp_path):
    result = inventory(fake_exe)
    assert result["entry"] == "0003:0000"
    (tmp_path / "chq.exe").write_bytes(fake_exe)
    (tmp_path / "egraphic.exe").write_bytes(build_overlay([0x0080, 0x0082]))
    (tmp_path / "fonts.chq").write_bytes(build_fonts_chq())
    out = tmp_path / "o" / "strings.json"
    assert main(["strings", "--data", str(tmp_path), "--out", str(out)]) == 0
    assert {"segment": "0000", "offset": "0003", "text": "Command HQ test"} in json.loads(out.read_text())
    assert main(["font", "--data", str(tmp_path), "--out", str(tmp_path / "f.json")]) == 0
    assert main(["inventory", "--data", str(tmp_path), "--out", str(tmp_path / "inv.json")]) == 0


def test_missing_exe(tmp_path):
    with pytest.raises(SystemExit):
        main(["inventory", "--data", str(tmp_path)])


@pytest.mark.gamedata
def test_real_segment_roles(real_exe):
    roles = {s.base: s.role for s in segments.classify(Executable.parse(real_exe))}
    assert roles[0x0000] == "main"
    assert roles[0x29FA] == "crt"
    assert roles[0x2CF5] == "overlay-loader"
    assert roles[0x2F5A] == "data"
