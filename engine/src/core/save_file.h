#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace chq {

// Command HQ save files (CHQ-00.SG .. CHQ-99.SG) begin with a 16-bit block size
// header equal to 0x13EB (5099 bytes), a 20-byte NUL-padded ASCII description
// at offset 8, and at least 5099 bytes of DGROUP state (plus optional war film data).
constexpr std::uint16_t kSaveHeaderMagic = 0x13EB;
constexpr std::size_t kMinSaveFileSize = 0x13EB;

bool validate_save_file(const std::filesystem::path& path);
std::string read_save_description(const std::filesystem::path& path);

}  // namespace chq
