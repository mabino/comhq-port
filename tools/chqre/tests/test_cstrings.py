from chqre import cstrings


def test_back_to_back_cstrings():
    data = b"\x00Hello\x00World!\x00ok\x00\xff"
    assert cstrings.find(data) == [(1, "Hello"), (7, "World!")]
    assert cstrings.find(data, 2) == [(1, "Hello"), (7, "World!"), (14, "ok")]


def test_rejects_unprintable_and_unterminated():
    assert cstrings.find(b"Hel\x01lo\x00") == []
    assert cstrings.find(b"unterminated") == []
