// Command HQ port: application shell. Runs the recompiled CHQ.EXE (+ EGRAPHIC.EXE)
// on the emulated PC (dos::Machine), presents the 320x200 EGA screen at 4:3,
// maps host mouse and keyboard events to INT 33h / INT 09h, and plays the PC
// speaker through SDL audio.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include "app/host_keys.h"
#include "app/macos/macos_bridge.h"
#include "app/mouse_control.h"
#include "app/sound_state.h"
#include "app/window_geometry.h"
#include "core/data_path.h"
#include "core/save_file.h"
#include "dos/machine.h"
#include "dos/rom_font.h"
#include "net/multiplayer_manager.h"
#if CHQ_HAVE_GAME
#include "dos/recomp.h"
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kSampleRate = 44100;
constexpr std::int64_t kBlocksPerFrame = 65'536;

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;
    std::unique_ptr<chq::dos::Machine> machine;
    std::vector<std::uint32_t> argb = std::vector<std::uint32_t>(chq::dos::Ega::kWidth * chq::dos::Ega::kHeight);
    std::vector<float> samples;
    chq::SoundState sound;
    std::string data_dir;
    std::string default_save_dir;
    std::string initial_save_to_load;
    std::string active_save_path;
    bool force_save_as = false;
    bool smoke = false;          // --smoke: run a few frames and exit (used by tests)
    int frames = 0;
    std::uint64_t persisted_writes = 0;
    const char* shot = nullptr;  // --screenshot FILE.bmp: save a frame after --frames N and exit
    int shot_after = 1;
    double emulated_now_ms = 0.0;
    std::uint64_t last_tick_ns = 0;
    bool quitting = false;
};

SDL_AppResult step_app_frame(App& app);

#if defined(__EMSCRIPTEN__)
constexpr const char* kWebSaves = "/saves";
bool web_saves_ready() { return EM_ASM_INT({ return Module.chqSavesReady ? 1 : 0; }); }
void web_saves_mount() {
    EM_ASM({
        FS.mkdir('/saves');
        FS.mount(IDBFS, {}, '/saves');
        FS.syncfs(true, function(err) {
            if (err) console.warn('Command HQ: could not load saves', err);
            Module.chqSavesReady = true;
        });
    });
}
void web_saves_flush() {
    EM_ASM({ FS.syncfs(false, function(err) { if (err) console.warn('Command HQ: could not store saves', err); }); });
}
#endif

fs::path save_folder() {
#if defined(__EMSCRIPTEN__)
    return kWebSaves;
#endif
    if (char* pref = SDL_GetPrefPath("mabino", "Command HQ")) {
        fs::path p(pref);
        SDL_free(pref);
        return p;
    }
    return fs::temp_directory_path() / "Command HQ";
}

fs::path data_folder(App& app) {
    if (!app.data_dir.empty()) {
        std::error_code ec;
        if (fs::exists(app.data_dir, ec) && chq::DataPath::looks_like_install(app.data_dir)) {
            return app.data_dir;
        }
    }

    if (auto d = chq::DataPath::from_environment()) {
        app.data_dir = d->root().string();
        return d->root();
    }

#if defined(__EMSCRIPTEN__)
    if (chq::DataPath::looks_like_install("/data")) {
        app.data_dir = "/data";
        return "/data";
    }
#endif

    std::vector<fs::path> candidates;

    if (const char* base = SDL_GetBasePath()) {
        fs::path bp(base);
        candidates.push_back(bp / "data");
        candidates.push_back(bp / "Resources" / "data");
        candidates.push_back(bp / ".." / "Resources" / "data");
        candidates.push_back(bp / ".." / ".." / "Resources" / "data");
        candidates.push_back(bp);
        candidates.push_back(bp / ".." / ".." / "..");
        candidates.push_back(bp / ".." / ".." / ".." / "..");
        candidates.push_back(bp / ".." / ".." / ".." / ".." / "..");
    }

    const fs::path pref_file = save_folder() / "data_path.txt";
    std::error_code ec;
    if (fs::is_regular_file(pref_file, ec)) {
        std::ifstream ifs(pref_file);
        std::string line;
        if (std::getline(ifs, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) {
                line.pop_back();
            }
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.erase(line.begin());
            }
            if (!line.empty()) {
                candidates.push_back(line);
            }
        }
    }

    candidates.push_back(fs::current_path());
    candidates.push_back(fs::current_path() / "..");
    if (const char* home = std::getenv("HOME")) {
        fs::path h(home);
        candidates.push_back(h / "Downloads" / "comhq");
        candidates.push_back(h / "Downloads" / "Command HQ");
        candidates.push_back(h / "Games" / "comhq");
        candidates.push_back(h / "Games" / "Command HQ");
        candidates.push_back(h / "Applications" / "Command HQ");
    }

    if (auto hit = chq::DataPath::find_install(candidates)) {
        app.data_dir = hit->root().string();
        return hit->root();
    }

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    if (!app.smoke && !app.shot) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto chosen = chq::macos::prompt_for_data_folder();
            if (!chosen) {
                break;
            }
            fs::path p(*chosen);
            if (fs::exists(p, ec) && chq::DataPath::looks_like_install(p)) {
                std::ofstream ofs(pref_file);
                if (ofs) {
                    ofs << p.string() << "\n";
                }
                app.data_dir = p.string();
                return p;
            } else {
                chq::macos::show_alert("Command HQ Files Not Found",
                                       "The selected folder does not contain the required Command HQ files (CHQ.EXE was not found).\n\nPlease select the folder containing the original game files.");
            }
        }
    }
#endif

    return {};
}

void draw_no_game(App& app) {
    std::fill(app.argb.begin(), app.argb.end(), chq::dos::Ega::rgb(1));
    const char* lines[] = {"Command HQ", "", "This build contains no recompiled game.",
                           "Run scripts/recomp.zsh with CHQ.EXE, then rebuild."};
    int y = 140;
    for (const char* text : lines) {
        const int x0 = (chq::dos::Ega::kWidth - static_cast<int>(std::strlen(text)) * 8) / 2;
        for (int i = 0; text[i]; ++i)
            for (int gy = 0; gy < 8; ++gy) {
                const std::uint8_t bits = chq::dos::kRomFont8x8[(text[i] & 0x7F) * 8 + gy];
                for (int gx = 0; gx < 8; ++gx)
                    if (bits & (0x80 >> gx)) app.argb[(y + gy) * chq::dos::Ega::kWidth + x0 + i * 8 + gx] = 0xFFFFFFFF;
            }
        y += 16;
    }
}

bool save_bmp(App& app, const char* path) {
    SDL_Surface* surf = SDL_CreateSurfaceFrom(chq::dos::Ega::kWidth, chq::dos::Ega::kHeight,
                                              SDL_PIXELFORMAT_ARGB8888, app.argb.data(), chq::dos::Ega::kWidth * 4);
    const bool ok = surf && SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    return ok;
}

void sync_machine_settings(App& app) {
    if (!app.machine) return;
    chq::dos::Machine::StartupSettings settings;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    settings.prefill_manual_word = chq::macos::should_prefill_manual_word();
    settings.auto_submit_manual_word = chq::macos::should_auto_submit_manual_word();
#endif
    app.machine->set_startup_settings(settings);
}

void import_save_file(App& app, const fs::path& src) {
    if (app.default_save_dir.empty()) return;
    std::error_code ec;
    const fs::path dst_dir(app.default_save_dir);
    fs::create_directories(dst_dir, ec);
    fs::path dst = dst_dir / src.filename();
    // Ensure filename matches CHQ-xx.SG convention if not already named CHQ-xx.SG
    std::string upper = src.filename().string();
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper.rfind("CHQ-", 0) != 0 || upper.size() != 9 || upper.substr(6) != ".SG") {
        dst = dst_dir / "CHQ-00.SG";
    } else {
        dst = dst_dir / upper;
    }
    if (!fs::equivalent(src, dst, ec)) {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    }
}

void start_game([[maybe_unused]] App& app) {
#if CHQ_HAVE_GAME
    const fs::path data = data_folder(app);
    if (data.empty()) {
        SDL_Log("Command HQ: game data directory could not be located.");
        if (!app.smoke && !app.shot) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                     "Command HQ - Game Files Not Found",
                                     "Command HQ could not find the original game data files (e.g. CHQ.EXE, FONTS.CHQ).\n\n"
                                     "Please place the game files in ~/Downloads/comhq or set the CHQ_DATA environment variable.",
                                     app.window);
        }
        app.quitting = true;
        return;
    }
    const fs::path saves = save_folder();
    app.default_save_dir = saves.string();
    std::error_code ec;
    fs::create_directories(saves, ec);
    SDL_Log("saves: %s", saves.string().c_str());
    SDL_Log("data: %s", data.string().c_str());
    app.machine = std::make_unique<chq::dos::Machine>(chq::recomp::program(),
                                                      chq::dos::FileSystem{data, saves});
    app.machine->set_modem_bridge(chq::net::MultiplayerManager::instance().modem_bridge());
    sync_machine_settings(app);
    app.machine->set_save_hook([&app](chq::dos::Machine&, const fs::path& p) {
        app.active_save_path = p.string();
    });
    app.machine->in_game_save_hook = [&app](chq::dos::Machine& m) -> int {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        if (app.force_save_as || app.active_save_path.empty()) {
            app.force_save_as = false;
            char chosen[1024] = {0};
            const char* def_dir = app.default_save_dir.empty() ? nullptr : app.default_save_dir.c_str();
            const char* cur = app.active_save_path.empty() ? nullptr : app.active_save_path.c_str();
            std::string def_name = cur ? fs::path(cur).filename().string() : "CHQ-00.SG";
            if (!chq::macos::prompt_save_as_panel(chosen, sizeof(chosen), def_dir, def_name.c_str())) {
                return 2;  // Cancel
            }
            app.active_save_path = chosen;
        }
        m.set_active_save_path(app.active_save_path);
        const std::string fn = fs::path(app.active_save_path).filename().string();
        for (std::size_t i = 0; i < fn.size() && i < 64; ++i) {
            m.wr8(m.cpu.s[3], static_cast<std::uint16_t>(0xc5c2 + i), static_cast<std::uint8_t>(fn[i]));
        }
        m.wr8(m.cpu.s[3], static_cast<std::uint16_t>(0xc5c2 + std::min<std::size_t>(fn.size(), 64)), 0);
        return 1;  // Save
#else
        (void)app;
        (void)m;
        return 0;
#endif
    };
    app.machine->resume_saved_war_hook = [&app](chq::dos::Machine& m) -> int {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        char chosen[1024] = {0};
        const char* def_dir = app.default_save_dir.empty() ? nullptr : app.default_save_dir.c_str();
        if (!chq::macos::prompt_open_save_panel(chosen, sizeof(chosen), def_dir)) {
            return 2;  // Cancel
        }
        app.active_save_path = chosen;
        m.set_active_save_path(app.active_save_path);
        const std::string fn = fs::path(app.active_save_path).filename().string();
        for (std::size_t i = 0; i < fn.size() && i < 64; ++i) {
            m.wr8(m.cpu.s[3], static_cast<std::uint16_t>(0xc5c2 + i), static_cast<std::uint8_t>(fn[i]));
        }
        m.wr8(m.cpu.s[3], static_cast<std::uint16_t>(0xc5c2 + std::min<std::size_t>(fn.size(), 64)), 0);
        return 1;  // Load
#else
        (void)app;
        (void)m;
        return 0;
#endif
    };
    if (!app.smoke) app.machine->start_title_intro();
#endif
}

bool save_game_synchronously(App& app, const std::string& path) {
    if (!app.machine || !app.machine->is_in_game()) return false;
    app.active_save_path = path;
    app.machine->set_active_save_path(path);
    app.force_save_as = false;
    app.machine->trigger_save();
    for (int i = 0; i < 50; ++i) {
        app.machine->run(100'000, app.emulated_now_ms);
        if (!app.machine->has_unsaved_progress()) {
            return true;
        }
    }
    return !app.machine->has_unsaved_progress();
}

bool confirm_unsaved_changes(App& app, const char* action_message) {
    if (!app.machine || !app.machine->has_unsaved_progress()) {
        return true;
    }
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    auto res = chq::macos::prompt_unsaved_changes_alert(action_message);
    if (res == chq::macos::UnsavedPromptResult::DontSave) {
        return true;
    }
    if (res == chq::macos::UnsavedPromptResult::Cancel) {
        return false;
    }
    if (!app.active_save_path.empty()) {
        return save_game_synchronously(app, app.active_save_path);
    } else {
        char chosen[1024] = {0};
        const char* def_dir = app.default_save_dir.empty() ? nullptr : app.default_save_dir.c_str();
        if (chq::macos::prompt_save_as_panel(chosen, sizeof(chosen), def_dir, "CHQ-00.SG")) {
            return save_game_synchronously(app, chosen);
        }
        return false;
    }
#else
    (void)action_message;
    return true;
#endif
}

void update_mouse_from_window(App& app, float win_x, float win_y, std::uint32_t sdl_buttons) {
    if (!app.machine || !app.renderer) return;
    float lx = 0.0f, ly = 0.0f;
    SDL_RenderCoordinatesFromWindow(app.renderer, win_x, win_y, &lx, &ly);
    const auto clamped_lx = std::clamp(lx, 0.0f, static_cast<float>(chq::WindowGeometry::kGameWidth - 1));
    const auto clamped_ly = std::clamp(ly, 0.0f, static_cast<float>(chq::WindowGeometry::kGameHeight - 1));
    if (auto ega = chq::mouse::logical_to_ega(clamped_lx, clamped_ly)) {
        app.machine->set_mouse(ega->first, ega->second, chq::mouse::sdl_buttons_to_dos(sdl_buttons));
        if (app.machine->in_title_intro() && (sdl_buttons & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
            app.machine->key_down(0x39, 0x20);  // space key
        }
    }
}

}  // namespace

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
    auto* app = new App;
    *state = app;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) {
            app->smoke = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            app->shot = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            app->shot_after = std::atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            std::error_code ec;
            if (fs::is_regular_file(argv[i], ec) && chq::validate_save_file(argv[i])) {
                app->initial_save_to_load = argv[i];
            }
        }
    }
    SDL_SetAppMetadata("Command HQ", "0.2.0", "com.mabino.comhq-port");
    const bool headless = app->smoke || app->shot;
    if (headless) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1", SDL_HINT_OVERRIDE);
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO))) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    const int init_w = chq::WindowGeometry::kGameWidth;
    const int init_h = chq::WindowGeometry::kGameHeight;
#if defined(__EMSCRIPTEN__)
    const SDL_WindowFlags init_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
#else
    const SDL_WindowFlags init_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
#endif
    if (!SDL_CreateWindowAndRenderer("Command HQ", init_w, init_h, init_flags,
                                     &app->window, &app->renderer)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->renderer, 1);
    SDL_SetRenderLogicalPresentation(app->renderer, chq::WindowGeometry::kGameWidth,
                                     chq::WindowGeometry::kGameHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetWindowAspectRatio(app->window, chq::WindowGeometry::kAspectRatio, chq::WindowGeometry::kAspectRatio);
    SDL_SetWindowMinimumSize(app->window, 320, 240);

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    if (!headless && chq::macos::is_fullscreen_by_default()) {
        SDL_SetWindowFullscreen(app->window, true);
    }
#endif

    if (!headless) {
        SDL_HideCursor();
    }

    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     chq::dos::Ega::kWidth, chq::dos::Ega::kHeight);
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_PIXELART);

    if (!headless) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        app->sound.set_volume(chq::macos::get_saved_sound_volume());
        app->sound.set_muted(chq::macos::get_saved_sound_muted());
#endif
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
        app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (app->audio) {
            SDL_SetAudioStreamGain(app->audio, app->sound.effective_gain());
            SDL_ResumeAudioStreamDevice(app->audio);
        } else {
            SDL_Log("no audio: %s", SDL_GetError());
        }

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        chq::macos::SoundBridgeCallbacks callbacks;
        callbacks.context = app;
        callbacks.toggle_mute = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            a->sound.toggle_mute();
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            chq::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            chq::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.increase_volume = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            a->sound.increase_volume();
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            chq::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            chq::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.decrease_volume = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            a->sound.decrease_volume();
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            chq::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            chq::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.set_volume = [](void* ctx, float vol) {
            auto* a = static_cast<App*>(ctx);
            a->sound.set_volume(vol);
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            chq::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            chq::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.set_muted = [](void* ctx, bool mute) {
            auto* a = static_cast<App*>(ctx);
            a->sound.set_muted(mute);
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            chq::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            chq::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.get_volume = [](void* ctx) -> float {
            return static_cast<App*>(ctx)->sound.volume();
        };
        callbacks.is_muted = [](void* ctx) -> bool {
            return static_cast<App*>(ctx)->sound.muted();
        };
        callbacks.step_frame = [](void* ctx) {
            step_app_frame(*static_cast<App*>(ctx));
        };
        callbacks.can_save = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return a->machine && a->machine->is_in_game();
        };
        callbacks.has_active_save = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return !a->active_save_path.empty();
        };
        callbacks.get_active_save_path = [](void* ctx) -> const char* {
            auto* a = static_cast<App*>(ctx);
            return a->active_save_path.empty() ? nullptr : a->active_save_path.c_str();
        };
        callbacks.has_unsaved_progress = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return a->machine && a->machine->has_unsaved_progress();
        };
        callbacks.confirm_unsaved_changes = [](void* ctx, const char* action) -> bool {
            auto* a = static_cast<App*>(ctx);
            return confirm_unsaved_changes(*a, action);
        };
        callbacks.save_game = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            if (!a->machine || !a->machine->is_in_game()) return;
            if (a->active_save_path.empty()) {
                char chosen[1024] = {0};
                const char* def_dir = a->default_save_dir.empty() ? nullptr : a->default_save_dir.c_str();
                if (chq::macos::prompt_save_as_panel(chosen, sizeof(chosen), def_dir, "CHQ-00.SG")) {
                    save_game_synchronously(*a, chosen);
                }
            } else {
                save_game_synchronously(*a, a->active_save_path);
            }
        };
        callbacks.save_game_as = [](void* ctx, const char* path) {
            auto* a = static_cast<App*>(ctx);
            if (path && a->machine && a->machine->is_in_game()) {
                save_game_synchronously(*a, path);
            }
        };
        callbacks.new_game = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            if (!confirm_unsaved_changes(*a, "Are you sure you want to restart Command HQ?")) {
                return;
            }
            start_game(*a);
        };
        callbacks.open_game = [](void* ctx, const char* path) {
            auto* a = static_cast<App*>(ctx);
            if (path) {
                import_save_file(*a, path);
            }
        };
        callbacks.get_default_save_dir = [](void* ctx) -> const char* {
            auto* a = static_cast<App*>(ctx);
            return a->default_save_dir.empty() ? nullptr : a->default_save_dir.c_str();
        };
        callbacks.sync_startup_settings = [](void* ctx) {
            sync_machine_settings(*static_cast<App*>(ctx));
        };
        callbacks.get_doc_text = [](void* ctx) -> const char* {
            static std::string doc_text;
            if (doc_text.empty()) {
                auto* a = static_cast<App*>(ctx);
                std::optional<chq::DataPath> d;
                if (a && !a->data_dir.empty()) {
                    d = chq::DataPath(a->data_dir);
                } else {
                    d = chq::DataPath::from_environment();
                }
                if (d) {
                    auto p = d->resolve("READ.ME");
                    if (!p) p = d->resolve("README");
                    if (p) {
                        std::ifstream f(*p);
                        if (f.is_open()) {
                            doc_text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        }
                    }
                }
            }
            return doc_text.empty() ? nullptr : doc_text.c_str();
        };
        chq::macos::setup_macos_app(app->window, callbacks);
#endif
    }

#if defined(__EMSCRIPTEN__)
    web_saves_mount();
#endif
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
    auto* app = static_cast<App*>(state);
    if (event->type == SDL_EVENT_DROP_FILE) {
        const char* dropped = event->drop.data;
        if (dropped) {
            if (!chq::validate_save_file(dropped)) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Unable to Import Save File",
                                         "The dropped file is not a valid Command HQ save file (CHQ-xx.SG).",
                                         app->window);
                return SDL_APP_CONTINUE;
            }
            import_save_file(*app, dropped);
        }
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (!confirm_unsaved_changes(*app, "Are you sure you want to quit Command HQ?")) {
            return SDL_APP_CONTINUE;
        }
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        update_mouse_from_window(*app, event->motion.x, event->motion.y, event->motion.state);
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        float mx = event->button.x, my = event->button.y;
        const SDL_MouseButtonFlags mask = SDL_GetMouseState(nullptr, nullptr);
        update_mouse_from_window(*app, mx, my, mask);
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (app->machine) app->machine->set_shift_state(chq::host_shift_flags(event->key.mod));
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if ((event->key.mod & SDL_KMOD_GUI) && event->key.key == SDLK_Q) {
                if (!confirm_unsaved_changes(*app, "Are you sure you want to quit Command HQ?")) {
                    return SDL_APP_CONTINUE;
                }
                return SDL_APP_SUCCESS;
            }
            if ((event->key.mod & SDL_KMOD_GUI) && event->key.key == SDLK_N) {
                if (!confirm_unsaved_changes(*app, "Are you sure you want to restart Command HQ?")) {
                    return SDL_APP_CONTINUE;
                }
                start_game(*app);
                return SDL_APP_CONTINUE;
            }
            if ((event->key.mod & SDL_KMOD_GUI) && event->key.key == SDLK_S) {
                if (app->machine && app->machine->is_in_game()) {
                    if ((event->key.mod & SDL_KMOD_SHIFT) || app->active_save_path.empty()) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
                        char chosen[1024] = {0};
                        const char* def_dir = app->default_save_dir.empty() ? nullptr : app->default_save_dir.c_str();
                        const char* active = app->active_save_path.empty() ? nullptr : app->active_save_path.c_str();
                        std::string def_name = active ? fs::path(active).filename().string() : "CHQ-00.SG";
                        if (chq::macos::prompt_save_as_panel(chosen, sizeof(chosen), def_dir, def_name.c_str())) {
                            save_game_synchronously(*app, chosen);
                        }
#endif
                    } else {
                        save_game_synchronously(*app, app->active_save_path);
                    }
                }
                return SDL_APP_CONTINUE;
            }
            if (!app->machine) return event->key.key == SDLK_ESCAPE ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
            if (event->key.mod & SDL_KMOD_GUI) return SDL_APP_CONTINUE;
            if (auto k = chq::host_key(event->key.key, event->key.mod, event->key.scancode)) {
                app->machine->key_down(k->scan, k->ascii);
            }
        }
    }
    return SDL_APP_CONTINUE;
}

namespace {

SDL_AppResult step_app_frame(App& app) {
#if defined(__EMSCRIPTEN__)
    if (!web_saves_ready()) return SDL_APP_CONTINUE;
#endif
    if (app.quitting) return SDL_APP_SUCCESS;
    if (app.frames++ == 0) {
        start_game(app);
        if (!app.initial_save_to_load.empty()) {
            import_save_file(app, app.initial_save_to_load);
            app.initial_save_to_load.clear();
        }
    }
    if (app.machine) {
        const std::uint64_t current_ns = SDL_GetTicksNS();
        if (app.last_tick_ns == 0) {
            app.last_tick_ns = current_ns;
            app.emulated_now_ms = static_cast<double>(current_ns) / 1e6;
        }
        const double raw_delta_ms = static_cast<double>(current_ns - app.last_tick_ns) / 1e6;
        app.last_tick_ns = current_ns;

        const double delta_ms = chq::SoundState::clamp_frame_delta(raw_delta_ms);
        app.emulated_now_ms += delta_ms;
        const double now = app.emulated_now_ms;

        chq::net::MultiplayerManager::instance().update();
        const auto st = app.machine->run(kBlocksPerFrame, now);
#if defined(__EMSCRIPTEN__)
        if (app.machine->file_writes() != app.persisted_writes) {
            app.persisted_writes = app.machine->file_writes();
            web_saves_flush();
        }
#endif
        if (st == chq::dos::Machine::State::Exited) {
            app.quitting = true;
            return SDL_APP_SUCCESS;
        }
        if (st == chq::dos::Machine::State::Faulted) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Command HQ", app.machine->fault().c_str(), app.window);
            return SDL_APP_FAILURE;
        }
        const bool blink = (static_cast<long long>(now) / 267) % 2 == 0;
        app.machine->ega().render(app.argb, app.machine->mem() + 0xB8000, chq::dos::kRomFont8x8, blink);
        if (app.audio) {
            app.samples.clear();
            app.machine->speaker().render(app.samples, kSampleRate, now);
            if (!app.samples.empty()) {
                const int max_queued = chq::SoundState::max_queued_audio_bytes(kSampleRate, 1, sizeof(float), 0.10);
                if (SDL_GetAudioStreamQueued(app.audio) > max_queued) {
                    SDL_ClearAudioStream(app.audio);
                }
                SDL_PutAudioStreamData(app.audio, app.samples.data(),
                                       static_cast<int>(app.samples.size() * sizeof(float)));
            }
        }
    } else {
        draw_no_game(app);
    }
    if (app.shot && app.frames >= app.shot_after) return save_bmp(app, app.shot) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    SDL_UpdateTexture(app.texture, nullptr, app.argb.data(), chq::dos::Ega::kWidth * 4);
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    SDL_RenderTexture(app.renderer, app.texture, nullptr, nullptr);
    SDL_RenderPresent(app.renderer);
    return app.smoke && app.frames >= 3 ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

}  // namespace

SDL_AppResult SDL_AppIterate(void* state) {
    return step_app_frame(*static_cast<App*>(state));
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    auto* app = static_cast<App*>(state);
    if (!app) return;
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    if (app->texture) SDL_DestroyTexture(app->texture);
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    delete app;
}
