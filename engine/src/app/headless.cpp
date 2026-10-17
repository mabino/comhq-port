// Development runner: plays the recompiled game without a window.
//   chq_headless [--keys SCRIPT] [--frames N] [--shot FILE.bmp] [--shots-dir DIR --every N]
// Keys and mouse events are fed from SCRIPT; time is simulated (60 frames per
// second), so runs are repeatable.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/data_path.h"
#include "dos/keymap.h"
#include "dos/machine.h"
#include "dos/recomp.h"
#include "dos/rom_font.h"

namespace fs = std::filesystem;

static bool save_bmp(chq::dos::Machine& m, const std::string& path) {
    std::vector<std::uint32_t> argb(chq::dos::Ega::kWidth * chq::dos::Ega::kHeight);
    m.ega().render(argb, m.mem() + 0xB8000, chq::dos::kRomFont8x8, true);
    SDL_Surface* s = SDL_CreateSurfaceFrom(chq::dos::Ega::kWidth, chq::dos::Ega::kHeight, SDL_PIXELFORMAT_ARGB8888,
                                           argb.data(), chq::dos::Ega::kWidth * 4);
    const bool ok = s && SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    return ok;
}

static bool save_wav(const std::string& path, const std::vector<float>& samples, int sample_rate) {
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    std::uint32_t data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
    std::uint32_t riff_bytes = 36 + data_bytes;
    std::uint32_t rate = sample_rate;
    std::uint32_t byte_rate = rate * 2;
    std::uint16_t block_align = 2;
    std::uint16_t bits = 16;
    std::uint16_t format = 1;
    std::uint16_t channels = 1;
    std::uint32_t fmt_len = 16;
    std::fwrite("RIFF", 1, 4, fp);
    std::fwrite(&riff_bytes, 4, 1, fp);
    std::fwrite("WAVEfmt ", 1, 8, fp);
    std::fwrite(&fmt_len, 4, 1, fp);
    std::fwrite(&format, 2, 1, fp);
    std::fwrite(&channels, 2, 1, fp);
    std::fwrite(&rate, 4, 1, fp);
    std::fwrite(&byte_rate, 4, 1, fp);
    std::fwrite(&block_align, 2, 1, fp);
    std::fwrite(&bits, 2, 1, fp);
    std::fwrite("data", 1, 4, fp);
    std::fwrite(&data_bytes, 4, 1, fp);
    for (float s : samples) {
        std::int16_t pcm = static_cast<std::int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        std::fwrite(&pcm, 2, 1, fp);
    }
    std::fclose(fp);
    return true;
}

int main(int argc, char** argv) {
    std::string keys, shot, shots_dir, wav_path, save_dir = (fs::temp_directory_path() / "comhq-headless").string();
    long frames = 600, every = 0, budget = 65536;
    bool trace = false, verbose = false, title_intro = false;
    std::string dump, watch;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* name) { return std::strcmp(argv[i], name) == 0 && i + 1 < argc; };
        if (arg("--keys")) keys = argv[++i];
        else if (arg("--frames")) frames = std::atol(argv[++i]);
        else if (arg("--shot")) shot = argv[++i];
        else if (arg("--shots-dir")) shots_dir = argv[++i];
        else if (arg("--every")) every = std::atol(argv[++i]);
        else if (arg("--budget")) budget = std::atol(argv[++i]);
        else if (arg("--save-dir")) save_dir = argv[++i];
        else if (std::strcmp(argv[i], "--trace") == 0) trace = true;
        else if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
        else if (std::strcmp(argv[i], "--title-intro") == 0) title_intro = true;
        else if (arg("--dump-interpreted")) dump = argv[++i];
        else if (arg("--watch")) watch = argv[++i];
        else if (arg("--wav")) wav_path = argv[++i];
        else {
            std::fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    auto data = chq::DataPath::from_environment();
    fs::create_directories(save_dir);
    chq::dos::Machine m(chq::recomp::program(), {data ? data->root() : fs::path(), save_dir});
    if (title_intro) m.start_title_intro();
    m.set_tracing(trace);
    m.log_keys = verbose;
    if (!watch.empty()) {
        const auto dash = watch.find('-');
        m.set_write_watch(std::stoul(watch.substr(0, dash), nullptr, 16),
                          std::stoul(watch.substr(dash + 1), nullptr, 16));
    }
    const auto script = chq::dos::parse_script(keys);
    std::size_t next_key = 0;
    int idle = 0;
    double now = 0;
    std::vector<float> audio_samples;
    for (long f = 0; f < frames; ++f) {
        m.set_clock_override(now);
        const auto state = m.run(budget, now);
        now += 1000.0 / 60.0;
        if (state != chq::dos::Machine::State::Running) {
            std::printf("frame %ld: %s %s (exit code %d)\n", f,
                        state == chq::dos::Machine::State::Exited ? "exited" : "FAULT", m.fault().c_str(),
                        m.exit_code());
            break;
        }
        ++idle;
        if (next_key < script.size()) {
            const auto k = script[next_key];
            if (k.mouse_x >= 0 && k.mouse_y >= 0) {
                if (idle >= 2) {
                    if (verbose) {
                        std::printf("frame %ld: mouse (%d,%d) buttons=%u\n", f, k.mouse_x, k.mouse_y, k.mouse_buttons);
                    }
                    m.set_mouse(k.mouse_x, k.mouse_y, k.mouse_buttons);
                    ++next_key;
                    idle = 0;
                }
            } else if (!k.scan && !k.ascii) {
                ++next_key;
            } else if (idle >= 4 && m.waiting_for_key()) {
                if (verbose) std::printf("frame %ld: key %02X/%02X\n", f, k.scan, k.ascii);
                m.key_down(k.scan, k.ascii);
                ++next_key;
                idle = 0;
            }
        }
        if (every > 0 && !shots_dir.empty() && f % every == 0)
            save_bmp(m, (fs::path(shots_dir) / ("frame" + std::to_string(f) + ".bmp")).string());
        m.speaker().render(audio_samples, 44100, now);
    }
    if (!shot.empty()) save_bmp(m, shot);
    if (!wav_path.empty()) save_wav(wav_path, audio_samples, 44100);
    std::printf("blocks run: %llu, keys used: %zu/%zu, video mode %02Xh, pc %05X\n",
                static_cast<unsigned long long>(m.blocks_run()), next_key, script.size(), m.ega().mode(), m.cpu.pc);
    if (trace) {
        std::printf("last blocks (* = interpreted):");
        for (auto pc : m.trace_tail())
            std::printf(" %05X%s", pc & 0xFFFFF, pc & 0x80000000u ? "*" : "");
        std::printf("\n");
    }
    if (!dump.empty()) {
        // seg:off relative to the load segment, as chqre's --extra-seeds expects.
        if (std::FILE* f = std::fopen(dump.c_str(), "a")) {
            const auto& prog = chq::recomp::program();
            const std::uint32_t base = prog.load_segment * 16u;
            for (const auto& [pc, n] : m.interpreted()) {
                if (pc < base || pc - base >= prog.code_image.size()) continue;
                if (m.image_written(pc) && m.mem()[pc] != prog.code_image[pc - base]) continue;
                const std::uint16_t cs = m.interpreted_cs(pc);
                if (cs < prog.load_segment) continue;
                std::fprintf(f, "%04X:%04X\n", cs - prog.load_segment, static_cast<unsigned>(pc - cs * 16u));
            }
            std::fclose(f);
        }
    }
    std::size_t shown = 0;
    for (const auto& [pc, n] : m.interpreted()) {
        if (shown++ < 40)
            std::printf("interpreted entry %05X x%llu%s\n", pc, static_cast<unsigned long long>(n),
                        m.image_written(pc) ? " (written at run time)" : "");
    }
    return m.state() == chq::dos::Machine::State::Faulted ? 1 : 0;
}
