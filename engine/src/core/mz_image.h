#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace chq {

// The load image of an MS-DOS MZ executable, addressed by segment:offset
// relative to the start of the image.
class MzImage {
public:
    static MzImage parse(std::span<const std::byte> file);  // throws ReadError
    static MzImage load(const std::filesystem::path& path);
    static std::vector<std::uint8_t> relocate_overlay(std::span<const std::byte> file, std::uint16_t reloc_factor);

    std::span<const std::byte> image() const { return image_; }
    std::span<const std::byte> at(std::uint16_t segment, std::uint16_t offset, std::size_t size) const;
    std::uint16_t entry_cs() const { return cs_; }
    std::uint16_t entry_ip() const { return ip_; }
    std::uint16_t stack_ss() const { return ss_; }
    std::uint16_t stack_sp() const { return sp_; }
    std::size_t relocation_count() const { return relocations_; }

private:
    std::vector<std::byte> image_;
    std::uint16_t cs_ = 0, ip_ = 0, ss_ = 0, sp_ = 0;
    std::size_t relocations_ = 0;
};

}  // namespace chq
