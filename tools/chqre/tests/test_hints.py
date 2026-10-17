import hashlib

import pytest

from chqre import cfg, hints, x86
from chqre.mz import Executable


@pytest.mark.gamedata
def test_hints_match_the_binary(real_exe, real_overlay):
    assert hashlib.sha256(real_exe).hexdigest().startswith(hints.SHA256)
    exe = Executable.parse(real_exe)
    ovl = Executable.parse(real_overlay)
    prog = cfg.load(exe, moved=hints.MOVED, overlay=ovl)
    for seg, off in hints.seeds(exe, ovl) + list(hints.FOUND_AT_RUN_TIME):
        x86.decode(prog.image, seg * 16 + off, seg * 16)
