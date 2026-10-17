#include "core/data_path.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <system_error>

namespace chq {
namespace fs = std::filesystem;

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

bool DataPath::looks_like_install(const fs::path& root) { return DataPath(root).resolve("CHQ.EXE").has_value(); }

std::optional<DataPath> DataPath::from_environment() {
    const char* env = std::getenv("CHQ_DATA");
    if (!env || !*env) env = std::getenv("COMHQ_DATA");
    if (!env || !*env || !looks_like_install(env)) return std::nullopt;
    return DataPath(env);
}

std::optional<fs::path> DataPath::resolve(std::string_view name) const {
    if (name.empty() || name.find_first_of("\\/") != std::string_view::npos || name == "." || name == "..")
        return std::nullopt;
    std::error_code ec;
    std::optional<fs::path> match;
    for (const auto& entry : fs::directory_iterator(root_, ec)) {
        const std::string file = entry.path().filename().string();
        if (file == name) return entry.path();
        if (!match && iequals(file, name)) match = entry.path();
    }
    return match;
}

}  // namespace chq
