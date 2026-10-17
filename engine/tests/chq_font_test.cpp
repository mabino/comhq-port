#include <doctest/doctest.h>

#include <fstream>
#include <vector>

#include "core/byte_reader.h"
#include "core/data_path.h"
#include "gfx/chq_font.h"
#include "test_bytes.h"

using chq::ChqFont;

TEST_CASE("parses a synthetic FONTS.CHQ bitmap font") {
    auto raw = make_chq_font();
    auto f = ChqFont::parse(raw.v);
    CHECK(f.first_char() == 65);
    CHECK(f.last_char() == 66);
    CHECK(f.height() == 7);
    CHECK(f.spacing_x() == 1);
    CHECK(f.glyph_count() == 2);
    CHECK(f.width('A') == 4);
    CHECK(f.width('B') == 5);
    CHECK(f.width('Z') == 0);
    CHECK(f.text_width("AB") == 4 + 1 + 5);
}

TEST_CASE("draws bitmap glyphs to EgaSurface") {
    auto raw = make_chq_font();
    auto f = ChqFont::parse(raw.v);
    chq::EgaSurface s;
    f.draw(s, 10, 20, "AB", 14);
    // 'A' top row 0xF0 sets 4 pixels at x=10..13, y=20
    CHECK(s.get(10, 20) == 14);
    CHECK(s.get(13, 20) == 14);
    CHECK(s.get(14, 20) == 0);
    // 'B' starts at x=10 + 4 + 1 = 15, top row 0xE0 sets 3 pixels at x=15..17
    CHECK(s.get(15, 20) == 14);
    CHECK(s.get(17, 20) == 14);
    CHECK(s.get(18, 20) == 0);
}

TEST_CASE("rejects truncated or malformed font headers") {
    Bytes tiny;
    tiny.u8(32).u8(122).u8(8);
    CHECK_THROWS_AS(ChqFont::parse(tiny.v), chq::ReadError);
}

TEST_CASE("real FONTS.CHQ from Command HQ (skipped without CHQ_DATA)") {
    auto data = chq::DataPath::from_environment();
    if (!data) return;
    auto path = data->resolve("FONTS.CHQ");
    REQUIRE(path.has_value());
    std::ifstream in(*path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) bytes[i] = static_cast<std::byte>(chars[i]);
    auto f = ChqFont::parse(bytes);
    CHECK(f.first_char() == 32);
    CHECK(f.last_char() == 123);
    CHECK(f.glyph_count() == 92);
    CHECK(f.height() == 5);
    CHECK(f.width('A') > 0);
}
