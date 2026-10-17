#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace chq {

// Locates the original game files (CHQ.EXE, EGRAPHIC.EXE, IBM.DRV, FONTS.CHQ, etc.).
// DOS names are case-insensitive; Linux and the wasm virtual FS are not.
class DataPath {
public:
    explicit DataPath(std::filesystem::path root) : root_(std::move(root)) {}

    // Root from $CHQ_DATA (or $COMHQ_DATA), else nullopt. Requires CHQ.EXE to be present.
    static std::optional<DataPath> from_environment();
    static bool looks_like_install(const std::filesystem::path& root);

    const std::filesystem::path& root() const { return root_; }

    // Resolve a file in the root, matching the name case-insensitively.
    std::optional<std::filesystem::path> resolve(std::string_view name) const;

private:
    std::filesystem::path root_;
};

}  // namespace chq
