// DOS services (INT 21h): the subset Microsoft C 5.1's runtime and Command HQ use.
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <system_error>

#include "core/mz_image.h"
#include "dos/dos_file.h"
#include "dos/machine.h"

namespace chq::dos {

namespace fs = std::filesystem;

namespace {
constexpr int kFirstFileHandle = 5;
constexpr double kTicksPerMsDos = 1193182.0 / 65536.0 / 1000.0;
constexpr std::uint16_t kHeapStartSeg = 0x5000;
constexpr std::uint16_t kHeapEndSeg = 0xA000;

std::string upper(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

bool match_dos_part(std::string_view pat, std::string_view str) {
    std::size_t pi = 0, si = 0;
    while (pi < pat.size()) {
        if (pat[pi] == '*') return true;
        if (pat[pi] == '?') {
            if (si < str.size()) ++si;
            ++pi;
        } else {
            if (si >= str.size() || pat[pi] != str[si]) return false;
            ++pi;
            ++si;
        }
    }
    return si == str.size();
}

bool match_dos_wildcard(std::string_view pattern, std::string_view filename) {
    const auto split83 = [](std::string_view s) -> std::pair<std::string_view, std::string_view> {
        const auto dot = s.find('.');
        if (dot == std::string_view::npos) return {s, {}};
        return {s.substr(0, dot), s.substr(dot + 1)};
    };
    const auto [p_stem, p_ext] = split83(pattern);
    const auto [f_stem, f_ext] = split83(filename);
    return match_dos_part(p_stem, f_stem) && match_dos_part(p_ext, f_ext);
}

// DOS error codes.
constexpr std::uint16_t kFileNotFound = 2, kTooManyFiles = 4, kAccessDenied = 5, kBadHandle = 6,
                        kOutOfMemory = 8, kInvalidBlock = 9, kNoMoreFiles = 18;
}  // namespace

std::string Machine::read_asciiz(std::uint16_t seg, std::uint16_t off) const {
    std::string s;
    for (int i = 0; i < 128; ++i) {
        const std::uint8_t ch = mem_[lin(seg, static_cast<std::uint16_t>(off + i))];
        if (!ch) break;
        s.push_back(static_cast<char>(ch));
    }
    return s;
}

std::optional<fs::path> Machine::host_path(const std::string& dos_name, bool for_write) const {
    // Strip any drive or directory prefix (e.g. "C:\ANIMS\LOGO.FLI" -> "LOGO.FLI").
    std::string name = dos_name;
    if (auto cut = name.find_last_of(":\\/"); cut != std::string::npos) name = name.substr(cut + 1);
    name = upper(name);
    if (name.empty() || name == "." || name == "..") return std::nullopt;

    if (active_save_path_ && upper(name.size() >= 3 ? name.substr(name.size() - 3) : "") == ".SG") {
        if (!for_write) {
            std::error_code ec;
            if (fs::exists(*active_save_path_, ec)) return *active_save_path_;
        } else {
            return *active_save_path_;
        }
    }

    const auto find_in = [&](const fs::path& dir) -> std::optional<fs::path> {
        std::error_code ec;
        if (dir.empty()) return std::nullopt;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (upper(e.path().filename().string()) == name) return e.path();
        return std::nullopt;
    };
    if (for_write) {
        if (auto hit = find_in(fs_.save_dir)) return hit;
        if (fs_.save_dir.empty()) return std::nullopt;
        return fs_.save_dir / name;
    }
    if (auto hit = find_in(fs_.save_dir)) return hit;
    return find_in(fs_.data_dir);
}

void Machine::int21_memory(std::uint8_t ah) {
    if (ah == 0x48) {  // Allocate memory: BX = paragraphs
        const std::uint16_t want = cpu.r[BX];
        std::uint16_t cur = kHeapStartSeg;
        std::uint16_t chosen = 0;
        std::uint16_t max_free = 0;
        for (const auto& [b_seg, b_sz] : dos_blocks_) {
            if (b_seg > cur) {
                const std::uint16_t gap = static_cast<std::uint16_t>(b_seg - cur);
                max_free = std::max(max_free, gap);
                if (!chosen && gap >= want) chosen = cur;
            }
            cur = static_cast<std::uint16_t>(b_seg + b_sz);
        }
        if (kHeapEndSeg > cur) {
            const std::uint16_t gap = static_cast<std::uint16_t>(kHeapEndSeg - cur);
            max_free = std::max(max_free, gap);
            if (!chosen && gap >= want) chosen = cur;
        }
        if (chosen && want > 0) {
            dos_blocks_[chosen] = want;
            cpu.cf = false;
            cpu.r[AX] = chosen;
        } else {
            cpu.cf = true;
            cpu.r[AX] = kOutOfMemory;
            cpu.r[BX] = max_free;
        }
        return;
    }
    if (ah == 0x49) {  // Free memory: ES = segment
        dos_blocks_.erase(cpu.s[ES]);
        cpu.cf = false;
        return;
    }
    if (ah == 0x4A) {  // Resize memory: ES = segment, BX = new paragraphs
        const std::uint16_t seg = cpu.s[ES];
        const std::uint16_t want = cpu.r[BX];
        if (seg == psp_seg_) {
            const std::uint16_t limit = dos_blocks_.empty() ? kHeapEndSeg : dos_blocks_.begin()->first;
            const std::uint16_t avail = static_cast<std::uint16_t>(limit - psp_seg_);
            if (want > avail) {
                cpu.cf = true;
                cpu.r[AX] = kOutOfMemory;
                cpu.r[BX] = avail;
                return;
            }
            psp_paras_ = want;
            cpu.cf = false;
            return;
        }
        auto it = dos_blocks_.find(seg);
        if (it == dos_blocks_.end()) {
            cpu.cf = false;
            return;
        }
        auto next_it = std::next(it);
        const std::uint16_t limit = next_it == dos_blocks_.end() ? kHeapEndSeg : next_it->first;
        const std::uint16_t avail = static_cast<std::uint16_t>(limit - seg);
        if (want > avail) {
            cpu.cf = true;
            cpu.r[AX] = kOutOfMemory;
            cpu.r[BX] = avail;
            return;
        }
        it->second = want;
        cpu.cf = false;
        return;
    }
}

void Machine::int21_exec() {
    const std::uint8_t al = cpu.get8(0);
    if (al != 0x03) {  // Only AL=03h (Load Overlay) is used by CHQ.EXE
        cpu.cf = true;
        cpu.r[AX] = kInvalidBlock;
        return;
    }
    const std::string name = read_asciiz(cpu.s[DS], cpu.r[DX]);
    const std::uint16_t load_seg = rd16(cpu.s[ES], cpu.r[BX]);
    const std::uint16_t reloc_factor = rd16(cpu.s[ES], static_cast<std::uint16_t>(cpu.r[BX] + 2));
    const auto path = host_path(name, false);
    if (!path) {
        cpu.cf = true;
        cpu.r[AX] = kFileNotFound;
        return;
    }
    std::ifstream in(*path, std::ios::binary);
    if (!in) {
        cpu.cf = true;
        cpu.r[AX] = kFileNotFound;
        return;
    }
    const std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        const auto image = MzImage::relocate_overlay(std::as_bytes(std::span(raw)), reloc_factor);
        const std::uint32_t dst = lin(load_seg, 0);
        if (dst + image.size() > kMemSize) {
            cpu.cf = true;
            cpu.r[AX] = kOutOfMemory;
            return;
        }
        // Copy directly into mem_ so loading EGRAPHIC.EXE at 0x29B0 matches
        // program_.code_image without flagging the overlay as self-modified.
        std::memcpy(&mem_[dst], image.data(), image.size());
        cpu.cf = false;
    } catch (...) {
        cpu.cf = true;
        cpu.r[AX] = kFileNotFound;
    }
}

void Machine::write_find_result() {
    if (find_index_ >= find_matches_.size()) {
        cpu.cf = true;
        cpu.r[AX] = kNoMoreFiles;
        return;
    }
    const auto& entry = find_matches_[find_index_++];
    const std::uint16_t dta_off = rd16(0x40, 0xF0);
    const std::uint16_t dta_seg = rd16(0x40, 0xF2);
    wr8(dta_seg, static_cast<std::uint16_t>(dta_off + 0x15), 0x20);  // archive attribute
    wr16(dta_seg, static_cast<std::uint16_t>(dta_off + 0x16), 0);    // time
    wr16(dta_seg, static_cast<std::uint16_t>(dta_off + 0x18), 0);    // date
    wr16(dta_seg, static_cast<std::uint16_t>(dta_off + 0x1A), static_cast<std::uint16_t>(entry.size & 0xFFFF));
    wr16(dta_seg, static_cast<std::uint16_t>(dta_off + 0x1C), static_cast<std::uint16_t>(entry.size >> 16));
    for (int i = 0; i < 13; ++i) {
        const std::uint8_t ch = (static_cast<std::size_t>(i) < entry.name.size() && i < 12)
                                    ? static_cast<std::uint8_t>(entry.name[static_cast<std::size_t>(i)])
                                    : 0;
        wr8(dta_seg, static_cast<std::uint16_t>(dta_off + 0x1E + i), ch);
    }
    cpu.cf = false;
    cpu.r[AX] = 0;
}

void Machine::int21_find(bool first) {
    if (first) {
        find_matches_.clear();
        find_index_ = 0;
        std::string pat = read_asciiz(cpu.s[DS], cpu.r[DX]);
        if (auto cut = pat.find_last_of(":\\/"); cut != std::string::npos) pat = pat.substr(cut + 1);
        pat = upper(pat);
        if (!pat.empty()) {
            std::map<std::string, std::uint32_t> seen;
            const auto scan_dir = [&](const fs::path& dir) {
                std::error_code ec;
                if (dir.empty()) return;
                for (const auto& e : fs::directory_iterator(dir, ec)) {
                    if (!e.is_regular_file(ec)) continue;
                    const std::string fname = upper(e.path().filename().string());
                    if (fname.size() <= 12 && match_dos_wildcard(pat, fname) && !seen.count(fname)) {
                        const auto sz = e.file_size(ec);
                        seen[fname] = ec ? 0u : static_cast<std::uint32_t>(sz);
                    }
                }
            };
            scan_dir(fs_.save_dir);
            scan_dir(fs_.data_dir);
            for (const auto& [k, sz] : seen) find_matches_.push_back({k, sz});
        }
    }
    write_find_result();
}

void Machine::int21_files(std::uint8_t ah) {
    auto error = [this](std::uint16_t code) {
        cpu.cf = true;
        cpu.r[AX] = code;
    };
    auto ok = [this] { cpu.cf = false; };
    auto handle = [this]() -> DosFile* {
        auto it = files_.find(cpu.r[BX]);
        return it == files_.end() ? nullptr : it->second.get();
    };
    auto new_handle = [this]() -> int {
        for (int h = kFirstFileHandle; h < 32; ++h)
            if (!files_.count(h)) return h;
        return -1;
    };

    switch (ah) {
        case 0x3C:  // create
        case 0x3D: {  // open
            const std::string name = read_asciiz(cpu.s[DS], cpu.r[DX]);
            const int mode = cpu.get8(0) & 3;
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "DOS %s \"%s\" mode %d", ah == 0x3C ? "create" : "open",
                         name.c_str(), mode);
            const bool create = ah == 0x3C;
            const auto path = host_path(name, create || mode != 0);
            if (!path) return error(kFileNotFound);
            std::error_code ec;
            if (!create && !fs::exists(*path, ec)) return error(kFileNotFound);
            if (create) fs::create_directories(path->parent_path(), ec);
            const int h = new_handle();
            if (h < 0) return error(kTooManyFiles);
            auto f = std::make_unique<DosFile>();
            f->path = *path;
            f->fp = std::fopen(path->string().c_str(), create ? "w+b" : mode == 0 ? "rb" : "r+b");
            if (!f->fp && !create && mode != 0) {
                if (auto orig = host_path(name, false); orig && *orig != *path) {
                    fs::create_directories(path->parent_path(), ec);
                    fs::copy_file(*orig, *path, fs::copy_options::overwrite_existing, ec);
                    f->fp = std::fopen(path->string().c_str(), "r+b");
                }
            }
            if (!f->fp) return error(create ? kAccessDenied : kFileNotFound);
            files_[h] = std::move(f);
            cpu.r[AX] = static_cast<std::uint16_t>(h);
            return ok();
        }
        case 0x3E: {  // close
            if (cpu.r[BX] < kFirstFileHandle) return ok();
            auto it = files_.find(cpu.r[BX]);
            if (it == files_.end()) return error(kBadHandle);
            const fs::path closed_path = it->second->path;
            files_.erase(it);
            ++file_writes_;
            if (upper(closed_path.extension().string()) == ".SG") {
                active_save_path_ = closed_path;
                unsaved_progress_ = false;
                if (save_hook_) save_hook_(*this, closed_path);
            }
            return ok();
        }
        case 0x3F: {  // read
            if (cpu.r[BX] == 0) {
                cpu.r[AX] = 0;
                return ok();
            }
            DosFile* f = handle();
            if (!f) return error(kBadHandle);
            std::uint16_t n = 0;
            for (; n < cpu.r[CX]; ++n) {
                const int ch = std::fgetc(f->fp);
                if (ch == EOF) break;
                wr8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + n), static_cast<std::uint8_t>(ch));
            }
            cpu.r[AX] = n;
            return ok();
        }
        case 0x40: {  // write
            if (cpu.r[BX] == 1 || cpu.r[BX] == 2) {
                for (std::uint16_t i = 0; i < cpu.r[CX]; ++i) {
                    const std::uint16_t ax = cpu.r[AX];
                    cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | rd8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + i)));
                    int10();
                    cpu.r[AX] = ax;
                }
                cpu.r[AX] = cpu.r[CX];
                return ok();
            }
            DosFile* f = handle();
            if (!f) return error(kBadHandle);
            if (cpu.r[CX] == 0) {
                std::fflush(f->fp);
                std::error_code ec;
                fs::resize_file(f->path, static_cast<std::uintmax_t>(std::ftell(f->fp)), ec);
                cpu.r[AX] = 0;
                return ok();
            }
            std::uint16_t n = 0;
            for (; n < cpu.r[CX]; ++n)
                if (std::fputc(rd8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + n)), f->fp) == EOF) break;
            std::fflush(f->fp);
            cpu.r[AX] = n;
            return ok();
        }
        case 0x41: {  // delete
            const auto path = host_path(read_asciiz(cpu.s[DS], cpu.r[DX]), true);
            std::error_code ec;
            if (!path || !fs::remove(*path, ec)) return error(kFileNotFound);
            return ok();
        }
        case 0x42: {  // seek
            DosFile* f = handle();
            if (!f) return error(kBadHandle);
            const long off = static_cast<long>(static_cast<std::int32_t>(cpu.r[CX] << 16 | cpu.r[DX]));
            const int whence = cpu.get8(0) == 0 ? SEEK_SET : cpu.get8(0) == 1 ? SEEK_CUR : SEEK_END;
            if (std::fseek(f->fp, off, whence) != 0) return error(0x19);
            const long pos = std::ftell(f->fp);
            cpu.r[AX] = static_cast<std::uint16_t>(pos);
            cpu.r[DX] = static_cast<std::uint16_t>(pos >> 16);
            return ok();
        }
        case 0x43: {  // get/set attributes
            const auto path = host_path(read_asciiz(cpu.s[DS], cpu.r[DX]), false);
            if (!path) return error(kFileNotFound);
            cpu.r[CX] = 0x20;
            return ok();
        }
        case 0x44:  // IOCTL: device information
            if (cpu.get8(0) == 0x00) {
                if (cpu.r[BX] < kFirstFileHandle) cpu.r[DX] = cpu.r[BX] == 0 ? 0x80D1 : 0x80D2;
                else if (handle()) cpu.r[DX] = 0x0002;
                else return error(kBadHandle);
                return ok();
            }
            return error(1);
        default: return error(1);
    }
}

bool Machine::int21() {
    const std::uint8_t ah = cpu.get8(4);
    switch (ah) {
        case 0x01:  // character input with echo
        case 0x07:  // direct character input without echo
        case 0x08:  // character input without echo
            if (keys_.empty()) {
                waiting_key_ = true;
                budget = 0;
                return false;
            }
            cpu.set8(0, static_cast<std::uint8_t>(keys_.front() & 0xFF));
            keys_.pop_front();
            if (ah == 0x01) {
                const std::uint16_t ax = cpu.r[AX];
                cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | cpu.get8(0));
                int10();
                cpu.r[AX] = ax;
            }
            break;
        case 0x02: {
            const std::uint16_t ax = cpu.r[AX];
            cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | cpu.get8(2));
            int10();
            cpu.r[AX] = ax;
            break;
        }
        case 0x06:
            if (cpu.get8(2) == 0xFF) {
                if (keys_.empty()) {
                    cpu.zf = true;
                    cpu.set8(0, 0);
                    waiting_key_ = true;
                } else {
                    cpu.zf = false;
                    cpu.set8(0, static_cast<std::uint8_t>(keys_.front() & 0xFF));
                    keys_.pop_front();
                }
            } else {
                const std::uint16_t ax = cpu.r[AX];
                cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | cpu.get8(2));
                int10();
                cpu.r[AX] = ax;
            }
            break;
        case 0x09: {
            for (std::uint16_t i = 0; i < 4096; ++i) {
                const std::uint8_t ch = rd8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + i));
                if (ch == '$') break;
                const std::uint16_t ax = cpu.r[AX];
                cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | ch);
                int10();
                cpu.r[AX] = ax;
            }
            break;
        }
        case 0x0B:
            if (keys_.empty()) {
                cpu.set8(0, 0x00);
                waiting_key_ = true;
            } else {
                cpu.set8(0, 0xFF);
            }
            break;
        case 0x0C: {
            keys_.clear();
            scancodes_.clear();
            const std::uint8_t sub = cpu.get8(0);
            if (sub == 1 || sub == 6 || sub == 7 || sub == 8 || sub == 0x0A) {
                cpu.set8(4, sub);
                return int21();
            }
            cpu.set8(0, 0);
            break;
        }
        case 0x0E: cpu.set8(0, 26); break;
        case 0x19: cpu.set8(0, 2); break;  // current drive C:
        case 0x1A: wr16(0x40, 0xF0, cpu.r[DX]), wr16(0x40, 0xF2, cpu.s[DS]); break;
        case 0x25:
            wr16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4), cpu.r[DX]);
            wr16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4 + 2), cpu.s[DS]);
            break;
        case 0x2A:
        case 0x2C: {
            std::tm lt{};
            if (clock_override_ || deterministic_ticks_) {
                lt.tm_year = 90, lt.tm_mon = 9, lt.tm_mday = 15;  // 1990-10-15
                if (deterministic_ticks_) read_ticks();
                const double ms = deterministic_ticks_ ? ticks_ / kTicksPerMsDos : now_ms_ - start_ms_;
                lt.tm_hour = 12 + static_cast<int>(ms / 3600000) % 12;
                lt.tm_min = static_cast<int>(ms / 60000) % 60;
                lt.tm_sec = static_cast<int>(ms / 1000) % 60;
            } else {
                const std::time_t t = std::time(nullptr);
#if defined(_WIN32)
                localtime_s(&lt, &t);
#else
                localtime_r(&t, &lt);
#endif
            }
            if (ah == 0x2A) {
                cpu.r[CX] = static_cast<std::uint16_t>(lt.tm_year + 1900);
                cpu.set8(6, static_cast<std::uint8_t>(lt.tm_mon + 1));
                cpu.set8(2, static_cast<std::uint8_t>(lt.tm_mday));
                cpu.set8(0, static_cast<std::uint8_t>(lt.tm_wday));
            } else {
                const double ms = deterministic_ticks_ ? ticks_ / kTicksPerMsDos : now_ms_ - start_ms_;
                const int hundredths = static_cast<int>(ms / 10) % 100;
                cpu.set8(5, static_cast<std::uint8_t>(lt.tm_hour));
                cpu.set8(1, static_cast<std::uint8_t>(lt.tm_min));
                cpu.set8(6, static_cast<std::uint8_t>(lt.tm_sec));
                cpu.set8(2, static_cast<std::uint8_t>(hundredths));
            }
            break;
        }
        case 0x2F:
            cpu.s[ES] = rd16(0x40, 0xF2);
            cpu.r[BX] = rd16(0x40, 0xF0);
            break;
        case 0x30: cpu.r[AX] = 0x1E03; cpu.r[BX] = cpu.r[CX] = 0; break;  // DOS 3.30
        case 0x33: cpu.set8(2, 0); break;
        case 0x35:
            cpu.r[BX] = rd16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4));
            cpu.s[ES] = rd16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4 + 2));
            break;
        case 0x36:  // get free disk space: ~16 MB free
            cpu.r[AX] = 8;
            cpu.r[BX] = 4096;
            cpu.r[CX] = 512;
            cpu.r[DX] = 8192;
            break;
        case 0x39:
        case 0x3A:
        case 0x3B: cpu.cf = false; break;
        case 0x47:
            wr8(cpu.s[DS], cpu.r[SI], 0);
            cpu.cf = false;
            break;
        case 0x48:
        case 0x49:
        case 0x4A: int21_memory(ah); break;
        case 0x4B: int21_exec(); break;
        case 0x00:
        case 0x4C:
            if (in_title_intro_) {
                in_title_intro_ = false;
                just_chained_intro_ = true;
                reset();
                break;
            }
            exit_code_ = cpu.get8(0);
            state_ = State::Exited;
            budget = 0;
            break;
        case 0x4E: int21_find(true); break;
        case 0x4F: int21_find(false); break;
        case 0x56: {  // rename file DS:DX -> ES:DI
            const auto src = host_path(read_asciiz(cpu.s[DS], cpu.r[DX]), true);
            const auto dst = host_path(read_asciiz(cpu.s[ES], cpu.r[DI]), true);
            std::error_code ec;
            if (!src || !dst || !fs::exists(*src, ec)) {
                cpu.cf = true;
                cpu.r[AX] = kFileNotFound;
            } else {
                fs::rename(*src, *dst, ec);
                cpu.cf = static_cast<bool>(ec);
                if (ec) cpu.r[AX] = kAccessDenied;
            }
            break;
        }
        default:
            if (ah >= 0x3C && ah <= 0x44) {
                int21_files(ah);
                break;
            }
            if (warned_.insert(0x21000 + ah).second) SDL_Log("unhandled INT 21h AH=%02Xh", ah);
            cpu.cf = true;
            cpu.r[AX] = 1;
            break;
    }
    return true;
}

}  // namespace chq::dos
