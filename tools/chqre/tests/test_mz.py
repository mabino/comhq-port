import pytest

from builders import build_mz
from chqre.mz import Executable, MZError


def test_segments_from_relocations():
    raw = build_mz([b"A" * 40, b"B" * 20, b"C" * 5], far_refs=[(0, 1), (1, 2)])
    exe = Executable.parse(raw)
    assert exe.header.relocations == 2
    assert exe.segment_bases() == [0, 4, 7, 9]
    assert exe.segments() == [(0, 64), (4, 48), (7, 32), (9, 16)]
    assert exe.segment_bytes(4).startswith(b"B" * 20)
    assert exe.word(4 * 16 + 32) == 7


def test_rejects_garbage():
    with pytest.raises(MZError):
        Executable.parse(b"not an exe")
    raw = build_mz([b"x" * 16])
    with pytest.raises(MZError):
        Executable.parse(raw[:-8])


@pytest.mark.gamedata
def test_real_exe(real_exe):
    exe = Executable.parse(real_exe)
    assert (exe.header.cs, exe.header.ip) == (0x29FA, 0x0010)
    assert exe.header.relocations == 3367
    assert len(exe.segments()) == 55
