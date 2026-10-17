#include "core/mz_image.h"

#include <fstream>
#include <iterator>

#include "core/byte_reader.h"

namespace chq {

MzImage MzImage::parse(std::span<const std::byte> file) {
    ByteReader r(file);
    const auto sig = r.fixed_string(2);
    if (sig != "MZ" && sig != "ZM") throw ReadError("not an MZ executable");
    const std::size_t last = r.u16(), pages = r.u16(), relocs = r.u16(), header = r.u16() * 16u;
    r.skip(4);  // min/max alloc
    MzImage out;
    out.ss_ = r.u16();
    out.sp_ = r.u16();
    r.skip(2);  // checksum
    out.ip_ = r.u16();
    out.cs_ = r.u16();
    out.relocations_ = relocs;
    const std::size_t size = last ? (pages - 1) * 512 + last : pages * 512;
    if (size > file.size() || header > size) throw ReadError("truncated MZ executable");
    auto img = file.subspan(header, size - header);
    out.image_.assign(img.begin(), img.end());
    return out;
}

MzImage MzImage::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ReadError("cannot open " + path.string());
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(std::as_bytes(std::span(raw)));
}

std::span<const std::byte> MzImage::at(std::uint16_t segment, std::uint16_t offset, std::size_t size) const {
    const std::size_t linear = segment * 16u + offset;
    if (linear > image_.size() || size > image_.size() - linear)
        throw ReadError("segment range outside the load image");
    return std::span(image_).subspan(linear, size);
}

std::vector<std::uint8_t> MzImage::relocate_overlay(std::span<const std::byte> file, std::uint16_t reloc_factor) {
    ByteReader r(file);
    const auto sig = r.fixed_string(2);
    if (sig != "MZ" && sig != "ZM") throw ReadError("not an MZ executable");
    const std::size_t last = r.u16(), pages = r.u16(), relocs = r.u16(), header = r.u16() * 16u;
    r.skip(14);  // min/max alloc, SS, SP, checksum, IP, CS
    const std::size_t reloc_off = r.u16();
    const std::size_t size = last ? (pages - 1) * 512 + last : pages * 512;
    if (size > file.size() || header > size) throw ReadError("truncated MZ executable");
    std::vector<std::uint8_t> out(size - header);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = std::to_integer<std::uint8_t>(file[header + i]);
    }
    r.seek(reloc_off);
    for (std::size_t i = 0; i < relocs; ++i) {
        const std::uint16_t off = r.u16();
        const std::uint16_t seg = r.u16();
        const std::size_t at = static_cast<std::size_t>(seg) * 16u + off;
        if (at + 2 <= out.size()) {
            const auto word = static_cast<std::uint16_t>((out[at] | (out[at + 1] << 8)) + reloc_factor);
            out[at] = static_cast<std::uint8_t>(word & 0xFF);
            out[at + 1] = static_cast<std::uint8_t>(word >> 8);
        }
    }
    return out;
}

}  // namespace chq
