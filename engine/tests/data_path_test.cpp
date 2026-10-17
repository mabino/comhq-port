#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "core/data_path.h"

namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path root;
    TempDir() {
        root = fs::temp_directory_path() / ("chq-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(root);
        std::ofstream(root / "Chq.Exe") << "x";
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};
}  // namespace

TEST_CASE("resolves DOS names case-insensitively") {
    TempDir t;
    chq::DataPath data(t.root);
    auto hit = data.resolve("CHQ.EXE");
    REQUIRE(hit.has_value());
    CHECK(hit->filename() == "Chq.Exe");
    CHECK(data.resolve("chq.exe") == hit);
}

TEST_CASE("missing files and paths are rejected") {
    TempDir t;
    chq::DataPath data(t.root);
    CHECK_FALSE(data.resolve("CHQ-00.SG").has_value());
    CHECK_FALSE(data.resolve("..").has_value());
    CHECK_FALSE(data.resolve("..\\etc\\passwd").has_value());
    CHECK_FALSE(data.resolve("sub/CHQ.EXE").has_value());
    CHECK_FALSE(data.resolve("").has_value());
}

TEST_CASE("install detection") {
    TempDir t;
    CHECK(chq::DataPath::looks_like_install(t.root));
    fs::remove(t.root / "Chq.Exe");
    CHECK_FALSE(chq::DataPath::looks_like_install(t.root));
}
