#include "core/save_file.h"

#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

namespace chq {

bool validate_save_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    const auto fsize = std::filesystem::file_size(path, ec);
    if (ec || fsize < kMinSaveFileSize) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::array<unsigned char, 28> hdr{};
    if (!file.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()))) {
        return false;
    }

    const std::uint16_t magic = static_cast<std::uint16_t>(hdr[0] | (hdr[1] << 8));
    if (magic != kSaveHeaderMagic) return false;

    // Bytes 8..27 hold a 20-byte NUL-padded ASCII description string.
    bool seen_nul = false;
    for (std::size_t i = 8; i < 28; ++i) {
        const unsigned char c = hdr[i];
        if (c == 0) {
            seen_nul = true;
        } else if (seen_nul || c < 0x20 || c > 0x7E) {
            return false;
        }
    }
    return seen_nul;
}

std::string read_save_description(const std::filesystem::path& path) {
    if (!validate_save_file(path)) return "";
    std::ifstream file(path, std::ios::binary);
    std::array<char, 28> hdr{};
    if (!file.read(hdr.data(), static_cast<std::streamsize>(hdr.size()))) return "";
    return std::string(hdr.data() + 8, strnlen(hdr.data() + 8, 20));
}

}  // namespace chq
