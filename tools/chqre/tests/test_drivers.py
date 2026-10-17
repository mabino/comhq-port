import pytest

from builders import build_fonts_chq, build_overlay
from chqre import drivers


def test_parse_overlay_driver():
    entries = [0x00A0 + i for i in range(16)]
    entries[15] = 0x000E  # banner slot
    raw = build_overlay(entries, name="EGRAPHIC.EXE", date="02-20-89")
    ovl = drivers.parse_overlay_driver(raw)
    assert ovl.name == "EGRAPHIC.EXE"
    assert ovl.version_date == "02-20-89"
    assert ovl.entry_count == 16
    assert len(ovl.code_entries) == 15
    assert 0x000E not in ovl.code_entries


def test_parse_fonts_chq():
    raw = build_fonts_chq(first_char=65, last_char=67, height=5)
    f = drivers.parse_fonts_chq(raw)
    assert (f.first_char, f.last_char, f.height) == (65, 67, 5)
    assert len(f.widths) == 3
    assert set(f.rows.keys()) == {65, 66, 67}
    assert len(f.rows[65]) == 5


def test_rejects_invalid_driver_and_font():
    with pytest.raises(drivers.DriverError):
        drivers.parse_fonts_chq(b"short")
    with pytest.raises(drivers.DriverError):
        drivers.parse_sound_driver(b"short")


@pytest.mark.gamedata
def test_real_drivers_and_font(data_dir, real_overlay):
    ovl = drivers.parse_overlay_driver(real_overlay)
    assert ovl.name == "EGRAPHIC.EXE"
    assert ovl.entry_count == 53
    assert len(ovl.code_entries) == 52

    font_path = next(p for p in data_dir.iterdir() if p.name.upper() == "FONTS.CHQ")
    f = drivers.parse_fonts_chq(font_path.read_bytes())
    assert (f.first_char, f.last_char, f.char_width, f.height) == (32, 123, 6, 5)
    assert len(f.widths) == 92
