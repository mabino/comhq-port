#include "dos/machine.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "core/mz_image.h"
#include "dos/dos_file.h"
#include "dos/keymap.h"
#include "dos/rom_font.h"

namespace chq::dos {

namespace {
constexpr std::uint16_t kEnvSeg = 0x0F00;
constexpr std::uint16_t kFontOff = 0xFA6E;       // IBM BIOS location of the 8x8 font
constexpr std::uint16_t kFontHighOff = 0xF000;   // characters 128-255 (blank)
constexpr double kTicksPerMs = 1193182.0 / 65536.0 / 1000.0;

bool is_shifted_ascii(std::uint8_t ascii) {
    static constexpr const char kShifted[] = "!@#$%^&*()_+{}|:\"<>?~";
    for (char c : kShifted)
        if (c && static_cast<std::uint8_t>(c) == ascii) return true;
    return false;
}
}  // namespace

Machine::Machine(const Program& program, FileSystem fs)
    : program_(program), fs_(std::move(fs)), mem_(kMemSize + 16, 0) {
    reset();
    image_lo_ = program_.load_segment * 16u;
    image_size_ = static_cast<std::uint32_t>(program_.image.size());
    written_.assign(image_size_, false);
}

void Machine::watched_write(std::uint32_t a, std::uint8_t v) {
    const std::uint32_t who = lin(cpu.s[CS], cpu.ip());
    if (warned_.insert(static_cast<int>(0x20000000u | ((who ^ (a & 0xFFFFF000u)) & 0x0FFFFFFF))).second)
        SDL_Log("write %02X to %05X by block/insn near %04X:%04X (pc %05X), DS=%04X ES=%04X SI=%04X DI=%04X CX=%04X",
                v, a, cpu.s[CS], cpu.ip(), cpu.pc, cpu.s[CS], cpu.s[ES], cpu.r[SI], cpu.r[DI], cpu.r[CX]);
}

void Machine::self_modified(std::uint32_t a) {
    if (warned_.insert(static_cast<int>(0x40000000u | a)).second)
        SDL_Log("self-modifying write to recompiled code at %05X (%04X:%04X)", a, cpu.s[CS], cpu.ip());
}

Machine::~Machine() = default;

bool Machine::start_title_intro() {
    const auto path = host_path("THQ.EXE", false);
    if (!path) return false;
    try {
        const auto thq = MzImage::load(*path);
        in_title_intro_ = false;
        reset();
        const auto img = thq.image();
        std::memcpy(&mem_[lin(program_.load_segment, 0)], img.data(), img.size());
        cpu.s[DS] = cpu.s[ES] = psp_seg_;
        cpu.s[SS] = static_cast<std::uint16_t>(program_.load_segment + thq.stack_ss());
        cpu.r[SP] = thq.stack_sp();
        far_jump(static_cast<std::uint16_t>(program_.load_segment + thq.entry_cs()), thq.entry_ip());
        in_title_intro_ = true;
        return true;
    } catch (...) {
        in_title_intro_ = false;
        return false;
    }
}

void Machine::reset() {
    std::fill(mem_.begin(), mem_.end(), 0);
    for (int n = 0; n < 256; ++n) {
        const std::uint16_t off = static_cast<std::uint16_t>(kStubBase + 4 * n);
        wr16(0, static_cast<std::uint16_t>(n * 4), off);
        wr16(0, static_cast<std::uint16_t>(n * 4 + 2), kBiosSeg);
        mem_[lin(kBiosSeg, off)] = 0xCF;  // IRET
    }
    std::memcpy(&mem_[lin(kBiosSeg, kFontOff)], kRomFont8x8, sizeof kRomFont8x8);
    wr16(0, 0x1F * 4, kFontHighOff);
    wr16(0, 0x1F * 4 + 2, kBiosSeg);
    wr16(0, 0x43 * 4, kFontOff);
    wr16(0, 0x43 * 4 + 2, kBiosSeg);

    // BIOS data area.
    wr16(0x40, 0x10, 0x0021);  // equipment: floppy, 80x25 colour (EGA reports via INT 10h/12h)
    wr16(0x40, 0x13, 640);     // KB of conventional memory
    mem_[0x449] = 3;           // video mode
    wr16(0x40, 0x4A, 80);
    wr16(0x40, 0x63, 0x3D4);
    mem_[0x484] = 24;
    wr16(0x40, 0x85, 14);
    mem_[0x487] = 0x60;
    mem_[0x488] = 0x09;
    mem_[0xFFFFE] = 0xFF;      // machine ID: PC

    // Environment and PSP, then the program image above them.
    const char env[] = "COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0\x01\0C:\\CHQ.EXE";
    std::memcpy(&mem_[lin(kEnvSeg, 0)], env, sizeof env);
    psp_seg_ = static_cast<std::uint16_t>(program_.load_segment - 0x10);
    psp_paras_ = 0x4000;
    dos_blocks_.clear();
    wr16(psp_seg_, 0x00, 0x20CD);  // INT 20h
    wr16(psp_seg_, 0x02, 0xA000);  // first segment beyond conventional memory
    mem_[lin(psp_seg_, 0x05)] = 0x9A;
    wr16(psp_seg_, 0x2C, kEnvSeg);
    mem_[lin(psp_seg_, 0x80)] = 0;
    mem_[lin(psp_seg_, 0x81)] = 0x0D;
    wr16(0x40, 0xF0, 0x0080);      // default DTA offset
    wr16(0x40, 0xF2, psp_seg_);    // default DTA segment
    if (!program_.image.empty()) {
        std::memcpy(&mem_[lin(program_.load_segment, 0)], program_.image.data(), program_.image.size());
        // Patch Main Options menu to 7 items (mode 0), removing (Exit to DOS).
        if (mem_.size() >= 0x2F99C) {
            mem_[0x2F993] = 0x07;
            mem_[0x2F99B] = 0x00;
        }
    }

    cpu = Cpu{};
    cpu.s[DS] = cpu.s[ES] = psp_seg_;
    cpu.s[SS] = program_.ss;
    cpu.r[SP] = program_.sp;
    far_jump(program_.cs, program_.ip);
    ega_.set_mode(3, true);
    state_ = State::Running;
    pending_in_game_save_ = false;
    keys_.clear();
    scancodes_.clear();
    port60_ = 0;
    pic_in_service_ = false;
    files_.clear();
    find_matches_.clear();
    find_index_ = 0;
    pit0_lo_written_ = false;
    pit2_lo_written_ = false;
    pit_read_hi_ = false;
    sub_slices_ = 0;
    prev_now_ms_ = 0;
    sub_now_ms_ = 0;
    next_pit0_ms_ = -1.0;
    pit0_irq_pending_ = false;
    mouse_x_ = 160;
    mouse_y_ = 100;
    mouse_buttons_ = 0;
    mouse_cb_mask_ = 0;
    mouse_cb_seg_ = 0;
    mouse_cb_off_ = 0;
}

void Machine::fail(std::string why) {
    if (state_ != State::Running) return;
    state_ = State::Faulted;
    fault_ = std::move(why);
    budget = -1;
    SDL_Log("machine fault: %s", fault_.c_str());
}

bool Machine::is_block(std::uint32_t pc) const {
    return std::binary_search(program_.blocks.begin(), program_.blocks.end(), pc);
}

const Program::Segment* Machine::segment_for(std::uint32_t pc) const {
    for (const auto& s : program_.segments)
        if (pc >= s.begin && pc < s.end) return &s;
    return nullptr;
}

std::vector<std::uint32_t> Machine::trace_tail() const {
    std::vector<std::uint32_t> out;
    const std::size_t n = std::min(trace_pos_, kTraceSize);
    for (std::size_t i = trace_pos_ - n; i < trace_pos_; ++i) out.push_back(trace_[i % kTraceSize]);
    return out;
}

void Machine::prefill_manual_answer() {
    // 1aa3:115f just wrote the first character of DS:0xE73A into DS:0xCA86 and
    // NUL into DS:0xCA87. Copy the remainder of the unpacked 8-char manual word
    // from DS:0xE73A into DS:0xCA86 so 13f0:0012 displays the full answer and
    // a single Enter keystroke accepts it.
    for (int i = 1; i < 9; ++i) {
        const std::uint8_t ch = mem_[lin(kDgroupSeg, static_cast<std::uint16_t>(0xE73A + i))];
        mem_[lin(kDgroupSeg, static_cast<std::uint16_t>(0xCA86 + i))] = ch;
        if (!ch) break;
    }
}

void Machine::on_empty_key_poll() {
    if (startup_settings_.auto_submit_manual_word && !interpret_only_ &&
        mem_[lin(kDgroupSeg, 0xE73A)] != 0 &&
        mem_[lin(kDgroupSeg, 0xCA86)] == mem_[lin(kDgroupSeg, 0xE73A)] &&
        cpu.s[CS] == static_cast<std::uint16_t>(program_.load_segment + 0x1786)) {
        key_down(0x1C, 0x0D);
        return;
    }
    waiting_key_ = true;
}

Machine::State Machine::run(std::int64_t slice, double now_ms) {
    if (state_ != State::Running) return state_;
    const double target_ms = clock_override_ ? *clock_override_ : now_ms;
    if (start_ms_ < 0) {
        start_ms_ = target_ms;
        prev_now_ms_ = target_ms;
    }
    now_ms_ = target_ms;
    sync_ticks();
    waiting_key_ = false;
    std::int64_t remaining = slice;
    const std::uint32_t stub_lo = lin(kBiosSeg, kStubBase), stub_hi = stub_lo + 4 * 256;

    while (remaining > 0 && state_ == State::Running) {
        const double frac = slice > 0 ? static_cast<double>(slice - remaining) / static_cast<double>(slice) : 1.0;
        sub_now_ms_ = prev_now_ms_ + (now_ms_ - prev_now_ms_) * frac;
        budget = std::min<std::int64_t>(remaining, 256);
        const std::int64_t sub_start = budget;
        deliver_hardware();
        if (pending_in_game_save_ && is_in_game() && cpu.ifl && !pic_in_service_ &&
            !(cpu.pc >= stub_lo && cpu.pc < stub_hi) && is_block(cpu.pc)) {
            pending_in_game_save_ = false;
            push(cpu.s[CS]);
            push(cpu.ip());
            cpu.s[CS] = 0x1000;
            cpu.pc = 0x10492;
        }
        while (budget >= 0 && state_ == State::Running) {
            const std::uint32_t pc = cpu.pc;
            if (pc >= stub_lo && pc < stub_hi) {
                --budget;
                if (!run_stub(static_cast<int>((pc - stub_lo) / 4))) {
                    remaining = 0;
                    break;
                }
                if (budget < 0) break;
                continue;
            }
            if (!in_title_intro_) {
                if (const auto* seg = segment_for(pc); seg && !interpret_only_ && is_block(pc)) {
                    const std::int64_t before = budget;
                    seg->run(*this);
                    blocks_run_ += static_cast<std::uint64_t>(before - std::max<std::int64_t>(0, budget));
                    continue;
                }
            }
            // Interpreter fallback for THQ.EXE title intro, runtime thunks, drivers, or un-seeded blocks.
            if (pc < 0x00500 || pc >= 0xA0000) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "execution left conventional memory at %04X:%04X", cpu.s[CS], cpu.ip());
                fail(buf);
                break;
            }
            if (!in_title_intro_ && !interpret_only_ &&
                ((pc >= image_lo_ && pc < lin(kDgroupSeg, 0)) ||
                 (pc >= 0x50000u && pc < image_lo_ + image_size_))) {
                ++interpreted_[pc];
                interpreted_cs_[pc] = cpu.s[CS];
            }
            int non_block_steps = 0;
            do {
                if (!in_title_intro_ && is_block(cpu.pc)) {
                    if (--budget < 0) {
                        break;
                    }
                    if (tracing && !trace(cpu.pc)) {
                        remaining = 0;
                        break;
                    }
                    ++blocks_run_;
                    non_block_steps = 0;
                } else if (in_title_intro_ || !segment_for(cpu.pc)) {
                    if (++non_block_steps >= 16) {
                        --budget;
                        non_block_steps = 0;
                    }
                }
                if (!step()) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "cannot decode instruction at %04X:%04X", cpu.s[CS], cpu.ip());
                    fail(buf);
                    break;
                }
            } while (budget >= 0 && state_ == State::Running &&
                     (in_title_intro_ || interpret_only_ || !is_block(cpu.pc)) &&
                     !(cpu.pc >= stub_lo && cpu.pc < stub_hi) && cpu.pc >= 0x00500 && cpu.pc < 0xA0000);
        }
        const std::int64_t used = std::max<std::int64_t>(1, sub_start - std::max<std::int64_t>(0, budget));
        remaining -= used;
        ++sub_slices_;
    }
    prev_now_ms_ = now_ms_;
    return state_;
}

void Machine::interrupt(std::uint8_t n, std::uint32_t ret_pc) {
    push(cpu.pack());
    cpu.ifl = cpu.tf = false;
    push(cpu.s[CS]);
    push(static_cast<std::uint16_t>(ret_pc - cpu.s[CS] * 16u));
    far_jump(rd16(0, static_cast<std::uint16_t>(n * 4 + 2)), rd16(0, static_cast<std::uint16_t>(n * 4)));
}

bool Machine::int33() {
    switch (cpu.r[AX]) {
        case 0x0000:  // Reset driver and read status
            cpu.r[AX] = 0xFFFF;
            cpu.r[BX] = 2;
            break;
        case 0x0001:  // Show cursor
        case 0x0002:  // Hide cursor
            break;
        case 0x0003:  // Return position and button status
            cpu.r[BX] = mouse_buttons_;
            cpu.r[CX] = static_cast<std::uint16_t>(mouse_x_ * 2);
            cpu.r[DX] = static_cast<std::uint16_t>(mouse_y_);
            break;
        case 0x0004:  // Position mouse cursor
            mouse_x_ = std::clamp<int>(cpu.r[CX] / 2, 0, Ega::kNativeWidth - 1);
            mouse_y_ = std::clamp<int>(cpu.r[DX], 0, Ega::kNativeHeight - 1);
            break;
        case 0x0007:  // Define horizontal cursor range
        case 0x0008:  // Define vertical cursor range
            break;
        case 0x000C:  // Define interrupt subroutine parameters
            mouse_cb_mask_ = cpu.r[CX];
            mouse_cb_seg_ = cpu.s[ES];
            mouse_cb_off_ = cpu.r[DX];
            break;
        case 0x0014:  // Exchange interrupt subroutines
            std::swap(cpu.r[CX], mouse_cb_mask_);
            std::swap(cpu.s[ES], mouse_cb_seg_);
            std::swap(cpu.r[DX], mouse_cb_off_);
            break;
        default: break;
    }
    return true;
}

bool Machine::run_stub(int n) {
    bool done = true;
    switch (n) {
        case 0x00: fail("divide error (INT 0) with no handler installed"); return true;
        case 0x10: done = int10(); break;
        case 0x11: cpu.r[AX] = rd16(0x40, 0x10); break;
        case 0x12: cpu.r[AX] = rd16(0x40, 0x13); break;
        case 0x14: cpu.r[AX] = 0x8000; break;  // serial port: timeout / not present
        case 0x15: cpu.cf = true; cpu.set8(4, 0x86); break;  // no joystick / AT services
        case 0x16: done = int16(); break;
        case 0x1A: done = int1a(); break;
        case 0x20:
            if (in_title_intro_) {
                in_title_intro_ = false;
                reset();
                return true;
            }
            state_ = State::Exited;
            budget = 0;
            return true;
        case 0x21:
            just_chained_intro_ = false;
            done = int21();
            if (just_chained_intro_) return true;
            break;
        case 0x33: done = int33(); break;
        case 0x08:
        case 0x09:
            pic_in_service_ = false;
            break;
        case 0x05: case 0x1B: case 0x1C: case 0x23: case 0x24: case 0x28: case 0x2F:
            break;
        default:
            if (warned_.insert(0x1000 + n).second) SDL_Log("unhandled INT %02Xh AX=%04X", n, cpu.r[AX]);
            break;
    }
    if (!done) return false;
    if (state_ != State::Running) return true;
    const bool cf = cpu.cf, zf = cpu.zf;
    iret();
    cpu.cf = cf, cpu.zf = zf;
    return true;
}

void Machine::read_ticks() {
    if (!deterministic_ticks_) return sync_ticks();
    ticks_ = static_cast<std::uint32_t>(tick_reads_++ / 64);
    std::memcpy(&mem_[0x46C], &ticks_, 4);
}

void Machine::sync_ticks() {
    if (deterministic_ticks_ || start_ms_ < 0) return;
    static const double midnight_offset = [] {
        const std::time_t t = std::time(nullptr);
        std::tm lt{};
#if defined(_WIN32)
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        return (lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec) * 1000.0;
    }();
    const double base = clock_override_ ? 0.0 : midnight_offset;
    ticks_ = static_cast<std::uint32_t>((base + now_ms_ - start_ms_) * kTicksPerMs);
    std::memcpy(&mem_[0x46C], &ticks_, 4);
}

void Machine::deliver_hardware() {
    const std::uint32_t stub_lo = lin(kBiosSeg, kStubBase), stub_hi = stub_lo + 4 * 256;
    const bool in_stub = cpu.pc >= stub_lo && cpu.pc < stub_hi;

    // 1. Deliver pending XT scancode to INT 09h when hooked (1786:00AD).
    if (!scancodes_.empty() && rd16(0, 0x09 * 4 + 2) != kBiosSeg && cpu.ifl && !pic_in_service_ && !in_stub) {
        port60_ = scancodes_.front();
        scancodes_.pop_front();
        pic_in_service_ = true;
        interrupt(0x09, cpu.pc);
        return;
    }

    // 2. Deliver PIT timer tick (INT 08h or INT 1Ch).
    const auto hooked = [this](int n) {
        return rd16(0, static_cast<std::uint16_t>(n * 4 + 2)) != kBiosSeg;
    };

    if (deterministic_ticks_) {
        if ((sub_slices_ & 63) == 1) {
            ++ticks_;
            std::memcpy(&mem_[0x46C], &ticks_, 4);
            if (cpu.ifl && !pic_in_service_ && !in_stub &&
                (in_title_intro_ || interpret_only_ || program_.segments.empty() || is_block(cpu.pc) || cpu.pc >= 0x60000)) {
                if (hooked(0x08)) {
                    pic_in_service_ = true;
                    interrupt(0x08, cpu.pc);
                } else if (hooked(0x1C)) {
                    pic_in_service_ = true;
                    interrupt(0x1C, cpu.pc);
                }
            }
        }
    } else {
        sync_ticks();
        const std::uint32_t div = pit0_divisor_ ? pit0_divisor_ : 65536;
        const double period_ms = 1000.0 / (Speaker::kPitHz / div);
        if (next_pit0_ms_ < 0.0) {
            next_pit0_ms_ = sub_now_ms_ + period_ms;
        }
        if (sub_now_ms_ >= next_pit0_ms_) {
            pit0_irq_pending_ = true;
            next_pit0_ms_ += period_ms;
            if (next_pit0_ms_ < sub_now_ms_ - period_ms * 4.0) {
                next_pit0_ms_ = sub_now_ms_;
            }
        }
        if (pit0_irq_pending_ && cpu.ifl && !pic_in_service_ && !in_stub &&
            (in_title_intro_ || interpret_only_ || program_.segments.empty() || is_block(cpu.pc) || cpu.pc >= 0x60000)) {
            pit0_irq_pending_ = false;
            if (hooked(0x08)) {
                pic_in_service_ = true;
                interrupt(0x08, cpu.pc);
            } else if (hooked(0x1C)) {
                pic_in_service_ = true;
                interrupt(0x1C, cpu.pc);
            }
        }
    }
}

void Machine::key_down(std::uint8_t scan, std::uint8_t ascii) {
    if (rd16(0, 0x09 * 4 + 2) == kBiosSeg && keys_.size() < 16) {
        keys_.push_back(static_cast<std::uint16_t>(scan << 8 | ascii));
    }
    if (scancodes_.size() >= 64 || scan == 0) return;
    const bool shifted_fn = scan >= 0x54 && scan <= 0x5D;
    const std::uint8_t phys_scan = shifted_fn ? static_cast<std::uint8_t>(0x3B + (scan - 0x54)) : scan;
    const bool need_shift = shifted_fn || is_shifted_ascii(ascii) || (shift_flags_ & 0x03) != 0;
    const bool need_ctrl = (shift_flags_ & 0x04) != 0;
    const bool need_alt = (shift_flags_ & 0x08) != 0;

    if (need_ctrl) scancodes_.push_back(0x1D);   // Left Ctrl make
    if (need_alt) scancodes_.push_back(0x38);    // Left Alt make
    if (need_shift) scancodes_.push_back(0x2A);  // Left Shift make
    scancodes_.push_back(phys_scan);
    scancodes_.push_back(static_cast<std::uint8_t>(phys_scan | 0x80));
    if (need_shift) scancodes_.push_back(0xAA);  // Left Shift break
    if (need_alt) scancodes_.push_back(0xB8);    // Left Alt break
    if (need_ctrl) scancodes_.push_back(0x9D);   // Left Ctrl break
    waiting_key_ = false;
}

void Machine::set_mouse(int x, int y, std::uint16_t buttons) {
    mouse_x_ = std::clamp(x, 0, Ega::kNativeWidth - 1);
    mouse_y_ = std::clamp(y, 0, Ega::kNativeHeight - 1);
    mouse_buttons_ = buttons;
    // Apply the exact effect of the game's INT 33h callback (24B0:0427) in DGROUP
    // so both recompiled and lockstep runs see identical state without mid-block jumps;
    // for custom callbacks in unit tests, step the callback in-place and restore CPU state.
    if (mouse_cb_seg_ != 0 && mouse_cb_mask_ != 0) {
        if (mouse_cb_seg_ == static_cast<std::uint16_t>(program_.load_segment + 0x24B0) && mouse_cb_off_ == 0x0427) {
            wr16(kDgroupSeg, 0xEAFE, static_cast<std::uint16_t>(mouse_x_));
            wr16(kDgroupSeg, 0xEB52, static_cast<std::uint16_t>(mouse_y_));
            wr8(kDgroupSeg, 0x40EB, (mouse_buttons_ & 1) ? 1 : 0);
            wr8(kDgroupSeg, 0x40EC, (mouse_buttons_ & 2) ? 1 : 0);
            wr8(kDgroupSeg, 0x40C7, 1);
        } else {
            const Cpu saved = cpu;
            cpu.r[AX] = mouse_cb_mask_;
            cpu.r[BX] = mouse_buttons_;
            cpu.r[CX] = static_cast<std::uint16_t>(mouse_x_);
            cpu.r[DX] = static_cast<std::uint16_t>(mouse_y_);
            const std::uint16_t ret_off = kStubBase;
            push(kBiosSeg);
            push(ret_off);
            far_jump(mouse_cb_seg_, mouse_cb_off_);
            const std::uint32_t sentinel = lin(kBiosSeg, ret_off);
            for (int steps = 0; steps < 256 && cpu.pc != sentinel && state_ == State::Running; ++steps) {
                if (!step()) break;
            }
            cpu = saved;
        }
    }
    if (buttons != 0) {
        unsaved_progress_ = true;
        waiting_key_ = false;
    }
}

void Machine::type_string(std::string_view s) {
    for (char ch : s) {
        if (ch == '\r' || ch == '\n') {
            key_down(0x1C, 0x0D);
        } else if (ch == '\x1B') {
            key_down(0x01, 0x1B);
        } else if (auto k = key_for_char(ch)) {
            key_down(k->scan, k->ascii);
        }
    }
}

std::uint8_t Machine::video_read(std::uint32_t a) {
    if (a < 0xB0000) return ega_.graphics() ? ega_.read(static_cast<std::uint16_t>(a - 0xA0000)) : 0xFF;
    return mem_[a];
}

void Machine::video_write(std::uint32_t a, std::uint8_t v) {
    if (a < 0xB0000) {
        if (ega_.graphics()) ega_.write(static_cast<std::uint16_t>(a - 0xA0000), v);
        return;
    }
    mem_[a] = v;
}

std::uint8_t Machine::in8(std::uint16_t port) {
    if (port >= 0x3C0 && port <= 0x3DF) return ega_.port_in(port);
    if (port == 0x3BA) return ega_.port_in(port);
    switch (port) {
        case 0x20:
        case 0x21: return 0x00;
        case 0x40:
        case 0x42: {
            pit_counter_ = static_cast<std::uint16_t>(pit_counter_ - 37);
            const bool hi = pit_read_hi_;
            pit_read_hi_ = !pit_read_hi_;
            return hi ? static_cast<std::uint8_t>(pit_counter_ >> 8) : static_cast<std::uint8_t>(pit_counter_);
        }
        case 0x60: return port60_;
        case 0x61: {
            refresh_ = !refresh_;
            return static_cast<std::uint8_t>((port61_ & 0x0F) | (refresh_ ? 0x10 : 0) |
                                             (speaker_.on() ? 0x20 : 0));
        }
        case 0x201: return 0xF0;  // no joystick
        default: return 0xFF;
    }
}

void Machine::out8(std::uint16_t port, std::uint8_t v) {
    if (port >= 0x3C0 && port <= 0x3DF) {
        ega_.port_out(port, v);
        return;
    }
    switch (port) {
        case 0x20:
            if (v == 0x20) pic_in_service_ = false;
            break;
        case 0x43:
            pit_mode_ = v;
            if ((v >> 6) == 0) pit0_lo_written_ = false;
            else if ((v >> 6) == 2) pit2_lo_written_ = false;
            break;
        case 0x40:
            if (!pit0_lo_written_) {
                pit0_latch_lo_ = v;
                pit0_lo_written_ = true;
            } else {
                pit0_divisor_ = static_cast<std::uint16_t>(pit0_latch_lo_ | v << 8);
                pit0_lo_written_ = false;
                const std::uint32_t div = pit0_divisor_ ? pit0_divisor_ : 65536;
                const double period_ms = 1000.0 / (Speaker::kPitHz / div);
                next_pit0_ms_ = sub_now_ms_ + period_ms;
            }
            break;
        case 0x42:
            if (!pit2_lo_written_) {
                pit2_latch_lo_ = v;
                pit2_lo_written_ = true;
            } else {
                pit2_divisor_ = static_cast<std::uint16_t>(pit2_latch_lo_ | v << 8);
                pit2_lo_written_ = false;
                speaker_.set((port61_ & 3) == 3, pit2_divisor_, sub_now_ms_);
            }
            break;
        case 0x61:
            port61_ = v;
            speaker_.set((v & 3) == 3, pit2_divisor_, sub_now_ms_);
            break;
        default: break;
    }
}

}  // namespace chq::dos
