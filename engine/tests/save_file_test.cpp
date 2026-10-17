#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/data_path.h"
#include "core/save_file.h"
#include "dos/machine.h"

namespace fs = std::filesystem;

namespace {
void write_synthetic_save(const fs::path& path, const char* desc, std::uint16_t magic = chq::kSaveHeaderMagic,
                          std::size_t size = chq::kMinSaveFileSize) {
    std::vector<std::uint8_t> buf(size, 0);
    if (size >= 2) {
        buf[0] = static_cast<std::uint8_t>(magic & 0xFF);
        buf[1] = static_cast<std::uint8_t>(magic >> 8);
    }
    if (desc && size >= 28) {
        std::strncpy(reinterpret_cast<char*>(&buf[8]), desc, 19);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
}
}  // namespace

TEST_SUITE("save_file") {

TEST_CASE("validates real CHQ-00.SG when CHQ_DATA is available") {
    auto data = chq::DataPath::from_environment();
    if (!data) return;
    if (auto p = data->resolve("CHQ-00.SG")) {
        CHECK(chq::validate_save_file(*p));
        CHECK(chq::read_save_description(*p) == "WORLDWAR3");
    }
}

TEST_CASE("rejects non-existent or empty file") {
    CHECK_FALSE(chq::validate_save_file("/non/existent/path/CHQ-00.SG"));
    const fs::path empty = fs::temp_directory_path() / "chq_empty.sg";
    { std::ofstream out(empty, std::ios::binary); }
    CHECK_FALSE(chq::validate_save_file(empty));
    fs::remove(empty);
}

TEST_CASE("rejects wrong magic or truncated save file") {
    const fs::path bad_magic = fs::temp_directory_path() / "chq_bad_magic.sg";
    write_synthetic_save(bad_magic, "WORLD WAR II", 0x1234, chq::kMinSaveFileSize);
    CHECK_FALSE(chq::validate_save_file(bad_magic));
    fs::remove(bad_magic);

    const fs::path too_short = fs::temp_directory_path() / "chq_too_short.sg";
    write_synthetic_save(too_short, "WORLD WAR II", chq::kSaveHeaderMagic, 1024);
    CHECK_FALSE(chq::validate_save_file(too_short));
    fs::remove(too_short);
}

TEST_CASE("accepts valid synthetic save file and reads description") {
    const fs::path valid = fs::temp_directory_path() / "CHQ-01.SG";
    write_synthetic_save(valid, "1986 NATO DEFENSE", chq::kSaveHeaderMagic, chq::kMinSaveFileSize + 256);
    CHECK(chq::validate_save_file(valid));
    CHECK(chq::read_save_description(valid) == "1986 NATO DEFENSE");
    fs::remove(valid);
}

TEST_CASE("Machine unsaved progress and host_path routing") {
    fs::path temp_dir = fs::temp_directory_path() / "chq_test_machine_saves";
    fs::create_directories(temp_dir);

    std::vector<std::uint8_t> image(0x4000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {{}, temp_dir});

    CHECK_FALSE(m.has_unsaved_progress());
    m.mark_unsaved();
    CHECK(m.has_unsaved_progress());
    m.mark_saved();
    CHECK_FALSE(m.has_unsaved_progress());

    auto write_path = m.host_path("CHQ-02.SG", true);
    REQUIRE(write_path.has_value());
    CHECK_EQ(write_path->parent_path(), temp_dir);

    fs::remove_all(temp_dir);
}

TEST_CASE("Machine is_in_game detection and trigger_save") {
    std::vector<std::uint8_t> image(0x4000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {});

    CHECK_FALSE(m.is_in_game());

    // In-game state is indicated by non-zero active scenario/game mode in DGROUP (0x147c or 0x147d)
    m.wr8(chq::dos::Machine::kDgroupSeg, 0x147c, 1);
    CHECK(m.is_in_game());

    m.wr8(chq::dos::Machine::kDgroupSeg, 0x147c, 0);
    m.wr8(chq::dos::Machine::kDgroupSeg, 0x147d, 2);
    CHECK(m.is_in_game());

    m.wr8(chq::dos::Machine::kDgroupSeg, 0x147d, 0);
    CHECK_FALSE(m.is_in_game());

    m.trigger_save();
}

TEST_CASE("Machine active save path overrides default folder") {
    fs::path temp_dir = fs::temp_directory_path() / "chq_test_machine_saves_def";
    fs::create_directories(temp_dir);

    std::vector<std::uint8_t> image(0x4000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {{}, temp_dir});

    fs::path custom_dir = fs::temp_directory_path() / "custom_dir";
    fs::create_directories(custom_dir);
    fs::path custom_save = custom_dir / "MY_GAME.SG";
    { std::ofstream out(custom_save, std::ios::binary); out << "SAVE_DATA"; }

    m.set_active_save_path(custom_save);
    CHECK(m.active_save_path().has_value());
    CHECK_EQ(*m.active_save_path(), custom_save);

    auto routed_write = m.host_path("CHQ-00.SG", true);
    REQUIRE(routed_write.has_value());
    CHECK_EQ(*routed_write, custom_save);

    auto routed_read = m.host_path("CHQ-00.SG", false);
    REQUIRE(routed_read.has_value());
    CHECK_EQ(*routed_read, custom_save);

    fs::remove_all(temp_dir);
    fs::remove_all(custom_dir);
}

TEST_CASE("Options menu patches remove Exit to DOS and resize box") {
    std::vector<std::uint8_t> image(0x40000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {});

    // Linear 0x2F993 is patched to 0x07 (7 menu options instead of 8)
    // Linear 0x2F99B is patched to 0x00 (mode 0 without Exit to DOS instead of mode 2)
    CHECK_EQ(m.mem()[0x2F993], 0x07);
    CHECK_EQ(m.mem()[0x2F99B], 0x00);
}

TEST_CASE("Machine resume_saved_war_hook integration") {
    std::vector<std::uint8_t> image(0x40000, 0);
    chq::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    chq::dos::Machine m(prog, {});

    bool hook_invoked = false;
    m.resume_saved_war_hook = [&](chq::dos::Machine& mach) -> int {
        hook_invoked = true;
        mach.set_active_save_path(fs::path("/tmp/test_war.sg"));
        return 1;
    };

    REQUIRE(m.resume_saved_war_hook);
    int res = m.resume_saved_war_hook(m);
    CHECK(hook_invoked);
    CHECK_EQ(res, 1);
    CHECK_EQ(m.active_save_path(), fs::path("/tmp/test_war.sg"));
}

}  // TEST_SUITE("save_file")
