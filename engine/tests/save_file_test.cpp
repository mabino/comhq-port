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

}  // TEST_SUITE("save_file")
