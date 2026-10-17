import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))


@pytest.fixture
def data_dir() -> Path:
    folder = os.environ.get("CHQ_DATA") or os.environ.get("COMHQ_DATA")
    if not folder:
        pytest.skip("CHQ_DATA not set")
    p = Path(folder)
    if not p.is_dir() or not any(c.name.upper() == "CHQ.EXE" for c in p.iterdir()):
        pytest.skip("CHQ.EXE missing from CHQ_DATA")
    return p


@pytest.fixture
def real_exe(data_dir: Path) -> bytes:
    path = next(p for p in data_dir.iterdir() if p.name.upper() == "CHQ.EXE")
    return path.read_bytes()


@pytest.fixture
def real_overlay(data_dir: Path) -> bytes:
    path = next((p for p in data_dir.iterdir() if p.name.upper() == "EGRAPHIC.EXE"), None)
    if path is None:
        pytest.skip("EGRAPHIC.EXE missing from CHQ_DATA")
    return path.read_bytes()
