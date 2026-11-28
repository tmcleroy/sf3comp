// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// The window: SDL3 picture, sound and controls around Port::RunFrame().
#include "game_info.h"

#include "bindings.h"
#include "port.h"
#include "png_write.h"
#include "present.h"
#include "socd.h"
#include "touch.h"
#include "i18n.h"

#include "../runtime/game.h"
#include "../recomp-common/arcade_screen.h"
#include "../recomp-common/gamepads.h"
#include "../recomp-common/menu_sdl.h"
#include "../recomp-common/own_shaders.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace cps3recomp;

namespace port {

#ifdef __APPLE__
void HandFrontToLauncher(); // (front_macos.mm)
#endif

namespace {

    // What each player's keys and gamepad buttons do: the player's choices (bindings.h; the launcher's
    // Controllers page), or the usual layout. A game started from the collection's launcher has the table every
    // game there shares; one with a launcher of its own, its own table.
    const BoundTable &Table() {
        static const BoundTable &table = MakeBoundTable(GamePlayers(), GameButtons(), InCollection());
        return table;
    }
    Bindings g_bindings = recomp::DefaultBindings(Table().table);
    int Players() {
        return Table().table.players;
    }

    static_assert(kBoundActions[0].bit == kUp && kBoundActions[1].bit == kDown && kBoundActions[2].bit == kLeft &&
                  kBoundActions[3].bit == kRight && kBoundActions[4].bit == kPunch1 && kBoundActions[9].bit == kKick3 &&
                  kBoundActions[10].bit == kStart && kBoundActions[11].bit == kCoin &&
                  kBoundActions[12].bit == (kPunch1 | kPunch2 | kPunch3) && kBoundActions[13].bit == (kKick1 | kKick2 | kKick3));

    SocdCleaner socdCleaners[Machine::kMaxPlayers]; // (each player's own: port/socd.h)

    constexpr int kDead = 16000; // how far a stick or a trigger must go to count

    // The Arcade monitor page (issue #15; recomp-common's arcade_sdl.h and arcade_screen.h, as cps2recomp's): full
    // screen in the board's 384 x 224 or a super resolution, at the refresh nearest the board's or made at exactly it
    // with Switchres (Windows), on the chosen screen; the frames paced by the screen's refreshes there unless Keep the
    // board's pace is on. The desktop's mode as it was at the end (Close)
    recomp::sdl::ArcadeScreen &Arcade() {
        static recomp::sdl::ArcadeScreen arcade(ArcadeBoardPicture());
        return arcade;
    }
#ifndef __ANDROID__ // (a phone: its one screen, full)
    // The Arcade monitor page's screen (DisplayScreen): the main display, or the others in the order SDL lists them;
    // the main one where the one chosen is not connected
    SDL_DisplayID ChosenDisplay() {
        return recomp::sdl::ChosenDisplay(DisplayScreen());
    }

    // Full screen as the Display page's "Full screen as" says (BorderlessFullScreen, issue #20): SDL's full screen, or
    // a window with no edges as large as the chosen screen (for what needs a window: NVIDIA RTX HDR, overlays). Where
    // and how large the window was before is kept, to put it back
    struct Borderless {
        bool on = false, maximized = false;
        int x = 0, y = 0, width = 0, height = 0;
    } g_borderless;
    bool IsFullScreen(SDL_Window *window) {
        return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) || g_borderless.on;
    }
    // (the window made edgeless and as large as the chosen screen, all of it)
    void CoverScreen(SDL_Window *window) {
        SDL_Rect bounds;
        if (!SDL_GetDisplayBounds(ChosenDisplay(), &bounds)) {
            fprintf(stderr, "borderless window: %s\n", SDL_GetError());
            return;
        }
        SDL_SetWindowBordered(window, false);
        SDL_SetWindowPosition(window, bounds.x, bounds.y);
        SDL_SetWindowSize(window, bounds.w, bounds.h);
        SDL_SyncWindow(window);
    }
    // Full screen (`full`) or a window again, either way as chosen now; a full screen of the other way becomes this one
    void GoFullScreen(SDL_Window *window, bool full) {
        if (full == IsFullScreen(window) && (!full || BorderlessFullScreen() == g_borderless.on)) {
            return;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
            SDL_SetWindowFullscreen(window, false);
            SDL_SyncWindow(window);
            if (recomp::sdl::SwitchresActive()) {
                Arcade().SetFullscreen(window, ArcadeChosen(), false); // (a window again: the desktop's own mode back)
            }
        }
        if (g_borderless.on) {
            g_borderless.on = false;
            SDL_SetWindowBordered(window, true);
            SDL_SetWindowSize(window, g_borderless.width, g_borderless.height);
            SDL_SetWindowPosition(window, g_borderless.x, g_borderless.y);
            if (g_borderless.maximized) {
                SDL_MaximizeWindow(window);
            }
            SDL_SyncWindow(window);
        }
        if (!full) {
            return;
        }
        if (BorderlessFullScreen()) {
            g_borderless.maximized = SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED;
            if (g_borderless.maximized) {
                SDL_RestoreWindow(window);
                SDL_SyncWindow(window);
            }
            SDL_GetWindowPosition(window, &g_borderless.x, &g_borderless.y);
            SDL_GetWindowSize(window, &g_borderless.width, &g_borderless.height);
            g_borderless.on = true;
            CoverScreen(window);
        } else {
            Arcade().SetFullscreen(window, ArcadeChosen(), true);
            SDL_SetWindowFullscreen(window, true);
        }
    }
#else
    bool IsFullScreen(SDL_Window *window) {
        return SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
    }
#endif

    bool KeyBound(SDL_Scancode key) {
        for (int player = 0; player < Players(); player++) {
            for (int action = 0; action < Table().table.count; action++) {
                if (g_bindings.key[player][action] == key) {
                    return true;
                }
            }
        }
        return false;
    }
    bool ButtonBound(SDL_GamepadButton button) {
        return PadInputBound(g_bindings, Table(), {button, -1});
    }

    // The camera's zoom (zoom.h) is the left stick's click held and a shoulder pressed: left for player 1, right for
    // player 2, the right stick's click for both fighters at once; the d-pad (or the stick) moves the camera. While the
    // click is held (and is not the game's), the shoulders and the directions are not the game's. On the keyboard,
    // Left Shift held, Q, E and W, and the arrows.
    bool ZoomHeld(SDL_Gamepad *gp) {
        return Zoom::Possible() && gp && !ButtonBound(SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    }
    bool Shoulder(int button) {
        return button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    }
    bool DPad(int button) {
        return button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN || button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
               button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    }
    bool KeyZoomHeld(const bool *keys) {
        return Zoom::Possible() && keys[SDL_SCANCODE_LSHIFT] && !KeyBound(SDL_SCANCODE_LSHIFT);
    }
    bool KeyZoom(SDL_Scancode key) {
        return key == SDL_SCANCODE_Q || key == SDL_SCANCODE_E || key == SDL_SCANCODE_W || key == SDL_SCANCODE_UP || key == SDL_SCANCODE_DOWN ||
               key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT;
    }

    uint16_t ReadKeyboard(const bool *keys, int player) {
        uint16_t pad = 0;
        const bool zoomHeld = KeyZoomHeld(keys);
        for (int action = 0; action < Table().table.count; action++) {
            const SDL_Scancode key = g_bindings.key[player][action];
            if (zoomHeld && KeyZoom(key)) {
                continue;
            }
            if (key != SDL_SCANCODE_UNKNOWN && keys[key]) {
                pad |= Table().bits[action];
            }
        }
        // (Return starts player 1's game too, unless it has been given something to do)
        if (player == 0 && keys[SDL_SCANCODE_RETURN] && !KeyBound(SDL_SCANCODE_RETURN)) {
            pad |= kStart;
        }
        return pad;
    }

    uint16_t ReadGamepad(SDL_Gamepad *gp, int player) {
        uint16_t pad = 0;
        const bool zoomHeld = ZoomHeld(gp);
        for (int action = 0; action < Table().table.count; action++) {
            const PadInput input = g_bindings.pad[player][action];
            if (zoomHeld && (Shoulder(input.button) || DPad(input.button))) {
                continue;
            }
            if ((input.button >= 0 && SDL_GetGamepadButton(gp, static_cast<SDL_GamepadButton>(input.button))) ||
                (input.axis >= 0 && SDL_GetGamepadAxis(gp, static_cast<SDL_GamepadAxis>(input.axis)) > kDead)) {
                pad |= Table().bits[action];
            }
        }
        const int x = zoomHeld ? 0 : SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = zoomHeld ? 0 : SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
        if (x < -kDead) pad |= kLeft;
        if (x > kDead) pad |= kRight;
        if (y < -kDead) pad |= kUp;
        if (y > kDead) pad |= kDown;
        return pad;
    }

    // A Multi game run's window: where the game before left it (each game is a program of its own, and its window
    // would open where the system puts a new one). The game ending for the next writes it down; the next game reads
    // it, if it was written moments ago (not a run before), and opens its window there, as tall.
    struct Placement {
        int x = 0, y = 0, height = 0;
        bool maximized = false;
    };
    std::filesystem::path PlacementFile() {
        std::error_code error;
        const std::filesystem::path folder = std::filesystem::temp_directory_path(error);
        return error ? std::filesystem::path() : folder / "sf3comp-multi-window.txt";
    }
    void KeepPlacement(SDL_Window *window) {
        const std::filesystem::path file = PlacementFile();
        if (file.empty() || IsFullScreen(window)) {
            return;
        }
        Placement at;
        int width = 0;
        SDL_GetWindowPosition(window, &at.x, &at.y);
        SDL_GetWindowSize(window, &width, &at.height);
        at.maximized = SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED;
        std::ofstream(file) << at.x << ' ' << at.y << ' ' << at.height << ' ' << at.maximized << '\n';
    }
    bool TakePlacement(Placement &at) {
        const std::filesystem::path file = PlacementFile();
        std::error_code error;
        if (file.empty() || !std::filesystem::exists(file, error)) {
            return false;
        }
        const auto age = std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(file, error);
        bool fresh = !error && age < std::chrono::minutes(1);
        {
            std::ifstream in(file);
            fresh = fresh && (in >> at.x >> at.y >> at.height >> at.maximized) && at.height > 0;
        }
        std::filesystem::remove(file, error);
        return fresh;
    }

} // namespace

int RunWindowed(Port &port) {
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    if (!port.opt.settingsPath.empty()) {
        g_bindings = LoadBindings(port.opt.settingsPath, Table());
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    // (controllers SDL doesn't know, issue #213: gamecontrollerdb.txt beside the program, in the launcher's folder or in
    // the game's own; recomp-common/gamepads.h)
    {
        const char *base = SDL_GetBasePath();
        const std::string mine = port.PlayerFolder();
        recomp::AddGamepadMappings({base ? std::filesystem::u8path(base) : std::filesystem::path(), LauncherFolder(),
                                    mine.empty() ? std::filesystem::path() : std::filesystem::u8path(mine)});
    }
    // (the window: so many times the board's 224 lines, --scale's or the Display page's)
    const int scale = port.opt.scale > 0 ? port.opt.scale : WindowSize();
    // (the window's name: game.cfg's "title" where a game has one, else its name)
#ifdef GAME_TITLE
    const char *title = GAME_TITLE;
#else
    const char *title = GAME_NAME;
#endif
    // (the picture's shape: the board's 384 x 224 shown 4:3 unless the Display page says otherwise, and as
    // much wider as the widest picture the game's sources may ask for; 4:3 for a picture that fills it)
    const int columns = TheGame().widestPicture ? std::clamp(TheGame().widestPicture(), +Video::kWidth, +Video::kMaxWidth) : Video::kWidth;
    // (with the Display page's bezels, 16:9 at least: the picture in the middle and the bezels beside it, issue #8)
    auto windowShape = [](int across) {
        const float picture = PictureShape(across, Video::kHeight, DisplayShape(), 4.0f / 3.0f * across / Video::kWidth);
        return DisplayBezel() != Bezel::Off && DisplayShape() != Shape::Fill ? std::max(picture, 16.0f / 9.0f) : picture;
    };
    // (in a Multi game run, a window where the game before's was: a full screen one is full screen as asked)
    Placement placed;
    const bool carried = !MultiGame().empty() && TakePlacement(placed) && !port.opt.fullscreen;
    const int windowHeight = carried ? placed.height : Video::kHeight * scale;
    // The window, as an OpenGL window for a shader (shading.h: nothing else draws to it), `at` a place for it (`width`
    // 0: as wide as the picture's shape makes it)
    auto makeWindow = [&](bool opengl, const Placement *at, int width, int height, bool fullscreen) {
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width > 0 ? width : std::lround(height * windowShape(columns)));
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        if (at) {
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, at->x);
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, at->y);
        } else {
#ifndef __ANDROID__
            // (on the Display page's screen)
            const SDL_DisplayID display = ChosenDisplay();
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
#endif
        }
#ifdef __ANDROID__
        fullscreen = true; // (a phone's whole screen, its bars hidden)
        const bool fullscreenNow = true;
#else
        // (an arcade resolution: made a window, given its mode (or the desktop switched to the one Switchres makes), then
        // made full screen; a borderless window: made a window, then made to cover the screen)
        const bool borderless = fullscreen && BorderlessFullScreen();
        const bool fullscreenNow = fullscreen && !ArcadeResolution() && !borderless;
#endif
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                              SDL_WINDOW_RESIZABLE | (fullscreenNow ? SDL_WINDOW_FULLSCREEN : 0) | (at && at->maximized ? SDL_WINDOW_MAXIMIZED : 0) |
                                  (opengl ? SDL_WINDOW_OPENGL : 0) | (getenv("CPS3RECOMP_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN : 0)); // (hidden: for tests)
        SDL_Window *made = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
#ifndef __ANDROID__
        if (made) {
            Arcade().SetFullscreen(made, ArcadeChosen(), fullscreen && !borderless);
            if (borderless) {
                // (made again, for or without a shader: as large as the screen again, where it was before kept)
                if (g_borderless.on) {
                    CoverScreen(made);
                } else {
                    GoFullScreen(made, true);
                }
            } else if (fullscreen && !fullscreenNow) {
                SDL_SetWindowFullscreen(made, true);
            }
        }
#endif
        // (in front: on macOS a program started by one that is not, as the launcher is behind a Multi game run's
        // last game, opens behind the other windows)
        if (made && !getenv("CPS3RECOMP_HIDDEN_WINDOW")) {
            SDL_RaiseWindow(made);
        }
        return made;
    };
    // The picture goes to the window through SDL's GPU API, or through SDL's renderer where there is no
    // device for that (and the dummy video driver of the tests has none) or when asked: --renderer sdl. With a
    // shader preset chosen (the Display page's Shader, or --shader) it goes through the preset with OpenGL instead,
    // where librashader is beside the game and makes the preset; else as without one.
    Presenter presenter;
    std::string presentError;
    const bool wantGpu = port.opt.renderer != "sdl" && strcmp(SDL_GetCurrentVideoDriver(), "dummy") != 0;
    // (the checks, CPS3RECOMP_WINDOW_OFFSCREEN=1 with SDL's offscreen video driver: no window at all (that driver makes none
    // without OpenGL); the GPU's picture drawn as for one of CPS3RECOMP_WINDOW_SIZE, into a texture that size, read back
    // by CPS3RECOMP_WINDOW_SHOT)
    const bool offscreen = wantGpu && getenv("CPS3RECOMP_WINDOW_OFFSCREEN") != nullptr;
    int checkWidth = 1280, checkHeight = 720;
    if (const char *size = getenv("CPS3RECOMP_WINDOW_SIZE")) {
        sscanf(size, "%dx%d", &checkWidth, &checkHeight);
    }
    const bool canShade = strcmp(SDL_GetCurrentVideoDriver(), "dummy") != 0;
    std::string shaderError;
    std::string preset = canShade ? port.ShaderPreset(&shaderError) : "";
    if (!shaderError.empty()) {
        // (Your own with no preset to draw with: why, in the log and to the player)
        fprintf(stderr, "shader: Your own: %s; the picture is drawn without it\n", shaderError.c_str());
        port.shaderProblem = recomp::i18n::TrFormat("Your own shader: {}", {shaderError});
    }
    int shaderValue = FeatureValue("display_shader");
    std::string shaderOwn = port.ownShader; // (the name Your own's preset was found by)
    if (!preset.empty() && !Shading::Available(shaderError)) {
        fprintf(stderr, "shader: %s; the picture is drawn without it\n", shaderError.c_str());
        preset.clear();
    }
    if (offscreen) {
        preset.clear();
    }
    SDL_Window *window = offscreen ? nullptr : makeWindow(!preset.empty(), carried ? &placed : nullptr, 0, windowHeight, port.opt.fullscreen);
    // (the checks, CPS3RECOMP_WINDOW_SIZE=WxH: the window that size, as a full screen of that shape would be)
    if (getenv("CPS3RECOMP_WINDOW_SIZE") && window && checkWidth > 0 && checkHeight > 0) {
        SDL_SetWindowSize(window, checkWidth, checkHeight);
        SDL_SyncWindow(window);
    }
    // (opened as the preset says, or as without one when that fails)
    auto openPresenter = [&]() {
        if (!preset.empty()) {
            if (presenter.OpenShaded(window, preset, shaderError)) {
                return true;
            }
            fprintf(stderr, "shader %s: %s; the picture is drawn without it\n", preset.c_str(), shaderError.c_str());
            port.shaderProblem = recomp::i18n::TrFormat("The shader {} didn't load: {}",
                                                        {recomp::shaders::Utf8(recomp::shaders::PathOf(preset).filename()), shaderError});
            preset.clear();
            // (an OpenGL window is drawn to by nothing else: one that is not, where this one is)
            Placement at;
            int width = 0;
            SDL_GetWindowPosition(window, &at.x, &at.y);
            SDL_GetWindowSize(window, &width, &at.height);
            at.maximized = SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED;
            const bool fullscreen = IsFullScreen(window);
            SDL_DestroyWindow(window);
            window = makeWindow(false, &at, width, at.height, fullscreen);
            if (!window) {
                presentError = SDL_GetError();
                return false;
            }
        }
        if (offscreen) {
            return presenter.OpenOffscreen(std::max(checkWidth, 1), std::max(checkHeight, 1), false, presentError);
        }
        return presenter.Open(window, wantGpu, presentError);
    };
    presenter.SetVsync(FeatureOn("display_vsync")); // (kept when it is opened again)
    if ((!window && !offscreen) || !openPresenter()) {
        fprintf(stderr, "SDL window: %s\n", window || offscreen ? presentError.c_str() : SDL_GetError());
        return 1;
    }
    if (wantGpu && !presenter.UsesGpu() && !presenter.Shaded()) {
        fprintf(stderr, "no GPU device (%s): the picture goes through SDL's renderer\n", presentError.c_str());
    }
    auto sayPresenter = [&]() {
        if (presenter.Shaded() && getenv("CPS3RECOMP_SHADER_SHOT")) {
            presenter.Shader()->keepLast = true;
        }
        if (port.opt.stats || ArcadeResolution()) {
            fprintf(stderr, "picture: %s\n", presenter.Shaded()  ? ("OpenGL, shader " + preset).c_str()
                                             : presenter.UsesGpu() ? SDL_GetGPUDeviceDriver(presenter.Device())
                                                                   : SDL_GetRendererName(presenter.Renderer()));
        }
    };
    sayPresenter();
    // The Display page's Shader changed in the menu: another preset in the same window, or the window made again
    // for or without OpenGL, where it is and as large
    auto followShader = [&]() {
        const int value = FeatureValue("display_shader");
        // (or Your own's preset chosen by another name, while Your own is chosen)
        if ((value == shaderValue && (value != port::kYourOwnShaderChoice || port.ownShader == shaderOwn)) || !port.opt.shader.empty() ||
            !canShade) {
            return;
        }
        shaderValue = value;
        shaderOwn = port.ownShader;
        std::string next = port.ShaderPreset(&shaderError);
        if (!shaderError.empty()) {
            fprintf(stderr, "shader: Your own: %s; the picture is drawn without it\n", shaderError.c_str());
            port.shaderProblem = recomp::i18n::TrFormat("Your own shader: {}", {shaderError});
        }
        if (!next.empty() && !Shading::Available(shaderError)) {
            fprintf(stderr, "shader: %s; the picture is drawn without it\n", shaderError.c_str());
            next.clear();
        }
        if (next == preset) {
            return;
        }
        if (!next.empty() && presenter.Shaded()) {
            if (presenter.SetPreset(next, shaderError)) {
                preset = next;
                sayPresenter();
            } else {
                fprintf(stderr, "shader %s: %s; the one before is kept\n", next.c_str(), shaderError.c_str());
                while (!shaderError.empty() && shaderError.back() == '.') {
                    shaderError.pop_back(); // (librashader's ends with one)
                }
                port.shaderProblem = recomp::i18n::TrFormat("The shader {} didn't load: {}. The one before is kept.",
                                                            {recomp::shaders::Utf8(recomp::shaders::PathOf(next).filename()), shaderError});
            }
            return;
        }
        Placement at;
        int width = 0;
        SDL_GetWindowPosition(window, &at.x, &at.y);
        SDL_GetWindowSize(window, &width, &at.height);
        at.maximized = SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED;
        const bool fullscreen = IsFullScreen(window);
        presenter.Close();
        SDL_DestroyWindow(window);
        preset = next;
        window = makeWindow(!preset.empty(), &at, width, at.height, fullscreen);
        if (!window || !openPresenter()) {
            fprintf(stderr, "SDL window: %s\n", window ? presentError.c_str() : SDL_GetError());
            port.quit = true;
            return;
        }
        sayPresenter();
    };

    // Sound: how much is kept waiting to be played (seconds), and how much is too much. The device is asked
    // for small buffers, so that little waits there either. (CPS3RECOMP_AUDIO_LOG: what is waiting, printed
    // once a second, and nothing heard.)
    constexpr double kAudioWaiting = 0.028, kAudioMost = 0.11;
    const bool audioLog = getenv("CPS3RECOMP_AUDIO_LOG") != nullptr;
    double audioLeast = 1;
    int audioDropped = 0;
    // (--mute starts with no sound; the menu's Sound row switches it on, so the device is opened all the same)
    SDL_AudioStream *audio = nullptr;
    port.menu.muted = port.opt.mute;
    bool silent = false;
    // (for the checks, CPS3RECOMP_AUDIO_SINK=FILE:RATE[:QUANTUM]: no sound device; the stream is taken by the clock as a
    // device at RATE would take it, QUANTUM frames at a time (as PipeWire's quantum), and what it plays written to FILE,
    // 16-bit stereo; a quantum the stream could not fill is counted, and silence written for what was missing.
    // games/sfiii3/checkko.sh hears the knock-out through it at the Steam Machine's 48 kHz and the Mac's 44.1 kHz)
    struct Sink {
        FILE *file = nullptr;
        int rate = 0, quantum = 0;
        uint64_t start = 0, taken = 0, short_ = 0;
        std::vector<int16_t> buffer;
    } sink;
    if (const char *spec = getenv("CPS3RECOMP_AUDIO_SINK")) {
        const std::string s = spec;
        const size_t colon = s.find(':');
        if (colon != std::string::npos) {
            sink.file = fopen(s.substr(0, colon).c_str(), "wb");
            sink.rate = atoi(s.c_str() + colon + 1);
            const size_t second = s.find(':', colon + 1);
            sink.quantum = second == std::string::npos ? 512 : std::max(1, atoi(s.c_str() + second + 1));
        }
        if (!sink.file || sink.rate <= 0) {
            fprintf(stderr, "CPS3RECOMP_AUDIO_SINK: FILE:RATE[:QUANTUM]\n");
            return 1;
        }
    }
    const auto drainSink = [&]() {
        if (!sink.file || !audio) {
            return;
        }
        const uint64_t now = SDL_GetTicksNS();
        if (!sink.start) {
            sink.start = now;
        }
        const uint64_t due = (now - sink.start) * static_cast<uint64_t>(sink.rate) / 1000000000ull / static_cast<uint64_t>(sink.quantum) * static_cast<uint64_t>(sink.quantum);
        sink.buffer.resize(static_cast<size_t>(sink.quantum) * 2);
        for (; sink.taken < due; sink.taken += static_cast<uint64_t>(sink.quantum)) {
            const int want = sink.quantum * 4;
            const int got = std::max(0, SDL_GetAudioStreamData(audio, sink.buffer.data(), want));
            if (got < want) {
                sink.short_++;
                memset(reinterpret_cast<uint8_t *>(sink.buffer.data()) + got, 0, static_cast<size_t>(want - got));
            }
            fwrite(sink.buffer.data(), 1, static_cast<size_t>(want), sink.file);
        }
    };
    {
        const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(Machine::kSampleRate + 0.5)};
        if (sink.file) {
            const SDL_AudioSpec to{SDL_AUDIO_S16, 2, sink.rate};
            audio = SDL_CreateAudioStream(&spec, &to);
        } else {
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        }
        if (audio && sink.file) {
            if (audioLog) {
                fprintf(stderr, "audio: a sink of %d Hz, %d frames a quantum\n", sink.rate, sink.quantum);
            }
        } else if (audio) {
            SDL_ResumeAudioStreamDevice(audio);
            if (audioLog) {
                SDL_AudioSpec device{};
                int frames = 0;
                SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio), &device, &frames);
                fprintf(stderr, "audio: device %d Hz, %d channels, %d frames a buffer (%s)\n", device.freq, device.channels, frames, SDL_GetCurrentAudioDriver());
            }
        } else {
            fprintf(stderr, "Audio: %s\n", SDL_GetError());
        }
    }

    // The custom soundtrack (soundtrack.h): a stream of its own, at its files' rate, opened when it first plays
    SDL_AudioStream *music = nullptr;
    SDL_AudioSpec musicSpec{SDL_AUDIO_F32, 2, 0};
    std::vector<float> musicMade;
    bool musicSilent = false;

    std::vector<SDL_Gamepad *> gamepads; // (those connected, in the order they came)
    // Each player's gamepad: those the launcher's Controllers page gave a player, then the rest in the order they came
    // (recomp-common bindings.h: [controllers]); given out again when one comes or goes, not every frame
    const recomp::PadChoices padChoices = port.opt.settingsPath.empty() ? recomp::PadChoices{} : recomp::LoadPadChoices(port.opt.settingsPath);
    SDL_Gamepad *playerPads[recomp::kBindMaxPlayers] = {};
    int padsGivenFor = -1; // (the players they were given out for; -1: to do again)
    // The in-game menu: opened and closed by Esc, F1, a gamepad stick's click or its Start and Select
    // together (the game is left from its "Quit game"). While it is open the game stands still and the
    // controls work the menu; buttons held as it closes do not reach the game until let go.
    std::vector<uint32_t> menuBase, menuPicture, shown, overlay;
    uint64_t menuVersion = 0, overlayVersion = 0, menuShotVersion = 0; // (NativeGpu::Overlay's)
    uint32_t menuWidth = 0, menuHeight = 0;
    // (the menu's picture's size where it is drawn on the screen's own pixels: Port::MenuPicture; 0: as usual)
    int menuWantWidth = 0, menuWantHeight = 0;
    bool menuDirty = false;
    // (where the menu's picture is in the window, and its size, as last drawn: for the mouse, recomp-common/menu_sdl.h)
    recomp::sdl::MenuPlace menuPlace;
    uint16_t previousPad = 0, heldAtClose = 0;
    recomp::MenuRepeat menuRepeat; // (the arrows held go on through the rows and the choices)
    int triggerHeld = 0;
    // (the practice point's left shoulder: when it went down, 0 if not waiting; tapped, back to the point; held
    // kPointHold, the point set. And the left trigger once it has made a chord with the shoulder: not rewind until let go)
    constexpr uint64_t kPointHold = 500; // milliseconds
    uint64_t shoulderDownAt = 0;
    bool triggerChorded = false;
    recomp::sdl::MenuChord menuChord; // a gamepad's Start and Select (Back) together: the menu
    // (with a zoom to work, the left stick's click opens the menu when it is let go, unless a shoulder was pressed
    // while it was held: then it was the zoom's. Only when the launcher's Menu button has the left stick's click:
    // recomp-common/menu_buttons.h)
    bool stickWaiting = false;
    auto toggleMenu = [&]() {
        if (port.net) {
            // (online: the game goes on for both players; Esc gives up a wait, and in a match opens the match's own menu
            // over it, or asks whether to leave when there is nothing to choose: Port::OpenOnlineMenu)
            if (port.connecting && !port.net->Arcade()) {
                port.LeaveOnline();
            } else if (port.menu.IsOpen() || port.leaving) {
                port.menu.Close();
                port.leaving = false;
            } else {
                port.OpenOnlineMenu();
            }
            return;
        }
        if (port.menu.IsOpen()) {
            port.menu.Close();
        } else {
            // (the game's picture under the menu: the GPU's, with the menu over it on the GPU, where the last frame was
            // made for it; else the CPU's)
            menuBase.clear();
            if (!port.machine->GpuFrame() || !presenter.CanShowNative()) {
                port.MenuPicture(menuBase, menuWidth, menuHeight, menuWantWidth, menuWantHeight);
            }
            port.menu.Open(true);
            menuDirty = true;
        }
    };
    // Online, the chat (Port::typing): T on the keyboard, the right stick's click or the touch screen's Chat button
    // opens a line to type, and the system's text input with it (on a Steam Deck or a phone, its own on-screen
    // keyboard); Enter sends it, Esc (or the click again) puts it away. Meanwhile the keyboard is the chat's, and after
    // it, not the game's until every key is let go (the Enter that sent it is not the game's Start).
    uint64_t openedByT = 0; // (when T opened it: its own letter, if the system sends it as text too, is not the line's)
    bool keysAfterChat = false;
    auto chatOpen = [&](bool open) {
        if (open == port.typing || (open && (!port.net || port.connecting || port.leaving || port.menu.IsOpen()))) {
            return;
        }
        port.typing = open;
        port.typed.clear();
        if (open) {
            int w = 0, h = 0;
            SDL_GetWindowSize(window, &w, &h);
            const SDL_Rect area{0, h * 3 / 4, w / 2, h / 12};
            SDL_SetTextInputArea(window, &area, 0);
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
            keysAfterChat = true;
        }
    };
    auto quickState = [&](bool save) {
        if (port.net) {
            return; // (online: no save states)
        }
        std::string error;
        const std::string path = port.StatePath(port.menu.Slot());
        if (!(save ? port.SaveState(path, error) : port.LoadState(path, error))) {
            fprintf(stderr, "state %d: %s\n", port.menu.Slot(), error.c_str());
        }
    };
    // A Multi game run (the launcher's Multi game page): N or a gamepad's left shoulder for the next game, F or
    // the left trigger held to fast-forward (each when the player has not given it to the game)
    const bool multi = !MultiGame().empty();
    // (a touch screen's controls: touch.h)
    TouchControls touch;
    std::vector<uint32_t> touchPicture;
    // The open menu answers a finger itself, the touch controls on or off (with them off and no gamepad, it is the only
    // way back to them): a finger put down on it and lifted where it went down does what a click does on the row under
    // it, and slid up or down moves through the rows a row at a time. A finger on one of the touch controls'
    // buttons is still theirs (Menu closes it, the first two choose and go back). What it chose is done in the loop,
    // after the events (menuTaps)
    std::vector<recomp::Menu::Chosen> menuTaps;
    struct MenuFinger {
        SDL_FingerID id = 0;
        bool down = false, slid = false;
        float x0 = 0, y0 = 0, stepY = 0;
    } menuFinger;
    touch.before = [&](const SDL_Event &e, float x, float y) {
        auto tell = [&](recomp::MenuMouse what) {
            if (menuPlace.w > 0 && menuPlace.h > 0) {
                float mx = 0, my = 0;
                menuPlace.ToMenu(x, y, mx, my);
                menuTaps.push_back(port.menu.Shared().Mouse(what, mx, my));
            }
        };
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            if (port.net || !port.menu.IsOpen() || touch.OnButton(x, y)) {
                return false;
            }
            // (not chosen yet: choosing a row can scroll the list, and the lift's click, by where the rows were last
            // drawn, would then land on another row)
            menuFinger = {e.tfinger.fingerID, true, false, x, y, y};
            return true;
        }
        if (!menuFinger.down || e.tfinger.fingerID != menuFinger.id) {
            return false;
        }
        if (e.type == SDL_EVENT_FINGER_MOTION) {
            // (a row's worth of sliding, about a twentieth of the menu's height, is a row)
            const float row = std::max(1.0f, menuPlace.h / 20);
            menuFinger.slid = menuFinger.slid || std::hypot(x - menuFinger.x0, y - menuFinger.y0) > row * 0.5f;
            for (; port.menu.IsOpen() && menuFinger.slid && y - menuFinger.stepY <= -row; menuFinger.stepY -= row) {
                tell(recomp::MenuMouse::WheelDown);
            }
            for (; port.menu.IsOpen() && menuFinger.slid && y - menuFinger.stepY >= row; menuFinger.stepY += row) {
                tell(recomp::MenuMouse::WheelUp);
            }
            return true;
        }
        menuFinger.down = false;
        if (e.type == SDL_EVENT_FINGER_UP && !menuFinger.slid && port.menu.IsOpen()) {
            tell(recomp::MenuMouse::Click);
        }
        return true;
    };
    auto takeMenuTaps = [&]() {
        for (const recomp::Menu::Chosen &chosen : menuTaps) {
            if (!port.menu.IsOpen()) {
                break;
            }
            port.MenuAct(port.menu.Take(chosen));
            menuDirty = true;
            if (!port.menu.IsOpen()) {
                heldAtClose = previousPad;
            }
        }
        menuTaps.clear();
    };
    // (checks: CPS3RECOMP_TOUCH_LOG, the touch controls' read logged each frame it presses something)
    const bool touchLog = getenv("CPS3RECOMP_TOUCH_LOG") != nullptr;
    // (the menu's Zoom row, as last seen: a change there is the camera's, and a zoom's press is the row's)
    int zoomRow = port.zoom.Choice();
    const double kFrameNs = 1e9 / Machine::kFramesPerSecond;
    uint64_t nextFrame = SDL_GetTicksNS();
    bool running = true;
    while (running && !port.quit && (port.opt.frames == 0 || port.frame < port.opt.frames)) {
        SDL_Event ev;
        if (cps3recomp::TakeMenuAsk() && !port.menu.IsOpen() && !port.net) {
            toggleMenu(); // (the game's sources asked for it: a mode over)
        }
        // (the checks, CPS3RECOMP_MENU_SCRIPT=FRAME:KEYS[;FRAME:KEYS...]: from that frame on, the in-game menu worked as a
        // player works it, a key a pass of this loop: m opens or closes it, u d l r up, down, left, right, a confirms, b
        // goes back, cXxY. clicks the mouse at that point of the window, wXxY. turns its wheel down there, and fXxY.,
        // gXxY., hXxY. put a finger down on the touch screen there, move it there and lift it there (the window's
        // pixels; with the menu closed too): games/sfiii3/checkwide.sh, widescreen switched in the menu)
        if (static const char *script = getenv("CPS3RECOMP_MENU_SCRIPT"); script && !port.net) {
            static std::string left = script;
            static size_t at = std::string::npos;
            const uint64_t from = strtoull(left.c_str(), nullptr, 10);
            const size_t colon = left.find(':'), end = std::min(left.find(';'), left.size());
            if (!left.empty() && colon != std::string::npos && port.frame >= from) {
                if (at == std::string::npos) {
                    at = colon + 1;
                }
                if (at < end) {
                    static const std::string kKeys = "udlrab";
                    const char key = left[at++];
                    if (key == 'f' || key == 'g' || key == 'h') {
                        const size_t stop = std::min(left.find('.', at), end);
                        float x = 0, y = 0;
                        sscanf(left.substr(at, stop - at).c_str(), "%fx%f", &x, &y);
                        at = std::min(stop + 1, end);
                        touch.Scripted(key == 'f' ? SDL_EVENT_FINGER_DOWN : key == 'g' ? SDL_EVENT_FINGER_MOTION : SDL_EVENT_FINGER_UP, x, y);
                    } else if (key == 'c' || key == 'w') {
                        // (cXxY. : the mouse clicked at that point of the window; wXxY. : its wheel turned down there, both
                        // as SDL's events, through the mouse's own way into the menu below)
                        const size_t stop = std::min(left.find('.', at), end);
                        int x = 0, y = 0;
                        sscanf(left.substr(at, stop - at).c_str(), "%dx%d", &x, &y);
                        at = std::min(stop + 1, end);
                        SDL_Event made = {};
                        made.motion.type = SDL_EVENT_MOUSE_MOTION;
                        made.motion.windowID = SDL_GetWindowID(window);
                        made.motion.x = static_cast<float>(x), made.motion.y = static_cast<float>(y);
                        SDL_PushEvent(&made);
                        made = {};
                        if (key == 'c') {
                            made.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                            made.button.windowID = SDL_GetWindowID(window);
                            made.button.button = SDL_BUTTON_LEFT;
                            made.button.down = true;
                            made.button.clicks = 1;
                            made.button.x = static_cast<float>(x), made.button.y = static_cast<float>(y);
                        } else {
                            made.wheel.type = SDL_EVENT_MOUSE_WHEEL;
                            made.wheel.windowID = SDL_GetWindowID(window);
                            made.wheel.y = -1;
                            made.wheel.mouse_x = static_cast<float>(x), made.wheel.mouse_y = static_cast<float>(y);
                        }
                        SDL_PushEvent(&made);
                    } else if (key == 'm') {
                        toggleMenu();
                    } else if (port.menu.IsOpen() && kKeys.find(key) != std::string::npos) {
                        port.MenuAct(port.menu.Press(1u << kKeys.find(key)));
                        menuDirty = true;
                    }
                }
                if (at >= end) {
                    left = end < left.size() ? left.substr(end + 1) : "";
                    at = std::string::npos;
                }
            }
        }
        if (port.typing && (!port.net || port.connecting)) {
            chatOpen(false); // (the match over, or waited for again)
        }
        while (SDL_PollEvent(&ev)) {
            if (touch.Event(ev)) {
                continue;
            }
            if (port.typing && ev.type == SDL_EVENT_TEXT_INPUT) {
                if (openedByT && SDL_GetTicks() - openedByT < 250 && (!strcmp(ev.text.text, "t") || !strcmp(ev.text.text, "T"))) {
                    openedByT = 0;
                    continue;
                }
                openedByT = 0;
                for (const char *c = ev.text.text; *c; c++) {
                    if (*c >= 32 && *c < 127 && port.typed.size() < Netplay::kChatMost) {
                        port.typed += *c;
                    }
                }
                continue;
            }
            if (port.typing && ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.scancode == SDL_SCANCODE_RETURN || ev.key.scancode == SDL_SCANCODE_KP_ENTER) {
                    port.Say();
                    chatOpen(false);
                } else if (ev.key.scancode == SDL_SCANCODE_ESCAPE || ev.key.scancode == SDL_SCANCODE_AC_BACK) {
                    chatOpen(false);
                } else if (ev.key.scancode == SDL_SCANCODE_BACKSPACE && !port.typed.empty()) {
                    port.typed.pop_back();
                }
                continue; // (every other key the line's, as text)
            }
            if ((ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) || ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                port.LagPressed(ev.common.timestamp); // (the input lag readout: Port::Lag)
            }
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                port.KeepPicture(); // (the launcher's tile of the game: its last picture)
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!port.menu.IsOpen() && !ev.key.repeat &&
                    (ev.key.scancode == SDL_SCANCODE_Q || ev.key.scancode == SDL_SCANCODE_E || ev.key.scancode == SDL_SCANCODE_W) &&
                    KeyZoomHeld(SDL_GetKeyboardState(nullptr))) {
                    if (ev.key.scancode == SDL_SCANCODE_W) {
                        port.zoom.Both();
                    } else {
                        port.zoom.Press(ev.key.scancode == SDL_SCANCODE_E ? 1 : 0);
                    }
                } else if (recomp::sdl::MenuKey(ev.key.scancode, !KeyBound(SDL_SCANCODE_TAB))) {
                    // (Esc, F1, a phone's Back, Tab unless the game has it)
                    toggleMenu();
                } else if (KeyBound(ev.key.scancode)) {
                    // (a key the player has given to the game does only that)
                } else if (port.net && ev.key.scancode == SDL_SCANCODE_T && !ev.key.repeat) {
                    chatOpen(true);
                    openedByT = port.typing ? SDL_GetTicks() : 0;
                } else if (multi && !port.menu.IsOpen() && ev.key.scancode == SDL_SCANCODE_N && !ev.key.repeat) {
                    port.Key('n');
                } else if (ev.key.scancode == SDL_SCANCODE_F5 || ev.key.scancode == SDL_SCANCODE_F9) {
                    // F5 keeps the game in the slot, F9 goes back to it; F6 / F7: the slot before, the next
                    if (!port.menu.IsOpen()) {
                        quickState(ev.key.scancode == SDL_SCANCODE_F5);
                    }
                } else if (ev.key.scancode == SDL_SCANCODE_F6 || ev.key.scancode == SDL_SCANCODE_F7) {
                    port.menu.SetSlot(port.menu.Slot() + (ev.key.scancode == SDL_SCANCODE_F7 ? 1 : -1));
                } else if (!port.menu.IsOpen() && ev.key.scancode == SDL_SCANCODE_P && !ev.key.repeat) {
                    port.Key('p'); // stops the game, and starts it again
                } else if (!port.menu.IsOpen() && ev.key.scancode == SDL_SCANCODE_O) {
                    port.Key('o'); // on by one frame (held: frame after frame)
                } else if (ev.key.scancode == SDL_SCANCODE_MINUS || ev.key.scancode == SDL_SCANCODE_EQUALS) {
                    port.Key(ev.key.scancode == SDL_SCANCODE_MINUS ? '-' : '='); // slower, faster
                } else if (!port.menu.IsOpen() && ev.key.scancode == SDL_SCANCODE_BACKSPACE && !ev.key.repeat) {
                    port.Key('k', kKeyRestart);
                } else if (!port.menu.IsOpen() && ev.key.scancode == SDL_SCANCODE_BACKSLASH && !ev.key.repeat) {
                    port.Key('k', kKeyExtra);
                } else if (!port.menu.IsOpen() && ev.key.scancode == SDL_SCANCODE_RIGHTBRACKET && !ev.key.repeat) {
                    port.Key(']'); // (the practice point set: port.h, Practice)
                } else if (ev.key.scancode == SDL_SCANCODE_F12 && !ev.key.repeat && (ev.key.mod & SDL_KMOD_SHIFT) && !(ev.key.mod & SDL_KMOD_ALT)) {
                    port.SaveScene(); // (the scene export: scene.h)
                } else if (ev.key.scancode == SDL_SCANCODE_F12 && !ev.key.repeat && (ev.key.mod & SDL_KMOD_CTRL)) {
                    port.SceneClip();
                } else if (ev.key.scancode == SDL_SCANCODE_F12 && !ev.key.repeat && (ev.key.mod & SDL_KMOD_ALT) && (ev.key.mod & SDL_KMOD_SHIFT)) {
                    port.SaveReplay(); // (the instant replay: the last seconds played)
                } else if (ev.key.scancode == SDL_SCANCODE_F12 && !ev.key.repeat && (ev.key.mod & SDL_KMOD_ALT)) {
                    port.RecordVideo(); // (the video recording: video_record.h)
                } else if (ev.key.scancode == SDL_SCANCODE_F12 && !ev.key.repeat) {
                    port.Screenshot();
                } else if (ev.key.scancode == SDL_SCANCODE_F2) {
                    port.test = !port.test; // the operator's test switch
                } else if (ev.key.scancode == SDL_SCANCODE_F11 ||
                           (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
#ifdef __ANDROID__
                    SDL_SetWindowFullscreen(window, !IsFullScreen(window));
#else
                    GoFullScreen(window, !IsFullScreen(window));
#endif
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (!port.menu.IsOpen() && Shoulder(ev.gbutton.button) && ZoomHeld(SDL_GetGamepadFromID(ev.gbutton.which))) {
                    port.zoom.Press(ev.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER ? 1 : 0);
                    stickWaiting = false;
                } else if (!port.menu.IsOpen() && ev.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK &&
                           !ButtonBound(SDL_GAMEPAD_BUTTON_RIGHT_STICK) && ZoomHeld(SDL_GetGamepadFromID(ev.gbutton.which))) {
                    port.zoom.Both(); // (not the right stick's own pause)
                    stickWaiting = false;
                } else if (ButtonBound(static_cast<SDL_GamepadButton>(ev.gbutton.button))) {
                    // (a button the player has given to the game does only that)
                } else if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK && Zoom::Possible() && !port.menu.IsOpen()) {
                    stickWaiting = true;
                } else if (recomp::sdl::MenuButton(static_cast<SDL_GamepadButton>(ev.gbutton.button))) {
                    toggleMenu(); // (Guide, or the left stick's click: as the launcher's Menu button has them)
                } else if (port.menu.IsOpen()) {
                    break;
                } else if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
                    if (port.net) {
                        chatOpen(!port.typing);
                    } else {
                        port.Key('p');
                    }
                } else if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                    // (the left trigger held: Backslash's kKeyExtra, the practice point's replay; with the practice point
                    // on, tapped: back to it (or the game's Start over), held: the point set; port.h, Practice)
                    SDL_Gamepad *gp = SDL_GetGamepadFromID(ev.gbutton.which);
                    const bool chord = !multi && gp && SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kDead;
                    if (chord) {
                        triggerChorded = true;
                        port.Key('k', kKeyExtra);
                    } else if (!multi && port.PracticeOn()) {
                        shoulderDownAt = std::max<uint64_t>(1, SDL_GetTicks());
                    } else {
                        port.Key(multi ? 'n' : 'k', kKeyRestart);
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && shoulderDownAt) {
                    shoulderDownAt = 0;
                    if (!port.menu.IsOpen()) {
                        port.Key('k', kKeyRestart);
                    }
                }
                if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK && stickWaiting) {
                    stickWaiting = false;
                    if (!port.menu.IsOpen() && recomp::sdl::MenuButton(SDL_GAMEPAD_BUTTON_LEFT_STICK)) {
                        toggleMenu();
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_WHEEL: {
                // (the mouse in the menu: over a row chooses it, a click confirms, the right button goes back, the wheel
                // goes up and down; a touch made into a mouse event is the touch controls', touch.cpp)
                recomp::Menu::Chosen chosen;
                if (port.menu.IsOpen() && !port.net && recomp::sdl::MouseEvent(ev, window, menuPlace, port.menu.Shared(), chosen)) {
                    port.MenuAct(port.menu.Take(chosen));
                    menuDirty = true;
                    if (!port.menu.IsOpen()) {
                        heldAtClose = previousPad;
                    }
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                if (SDL_Gamepad *gp = SDL_OpenGamepad(ev.gdevice.which)) {
                    gamepads.push_back(gp);
                    padsGivenFor = -1;
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                for (auto it = gamepads.begin(); it != gamepads.end(); ++it) {
                    if (SDL_GetGamepadID(*it) == ev.gdevice.which) {
                        SDL_CloseGamepad(*it);
                        gamepads.erase(it);
                        // (given out again before any player's gamepad is read: this one is no longer open)
                        std::fill(std::begin(playerPads), std::end(playerPads), nullptr);
                        padsGivenFor = -1;
                        break;
                    }
                }
                break;
            default: break;
            }
        }

        takeMenuTaps(); // (the menu's taps, taken with the fingers: menuFinger)
        // (the Touch controls and Touch movement features: main.cpp; not on a Mac, where neither is: on, floating)
        touch.Allow(FeatureValue("touch_controls") != 1);
        touch.Floating(FeatureValue("touch_movement") == 0);
        if (touch.TakeMenuPress()) {
            toggleMenu();
        }
        port.PumpHost(); // (a match hosted from the menu, listened for, the menu open or not)
        touch.Chat(port.net != nullptr); // (online, from the start or a match hosted from the menu)
        if (touch.TakeZoomPress()) {
            if (port.net) {
                chatOpen(!port.typing);
            } else {
                port.zoom.Both();
            }
        }
        if (Zoom::Possible()) {
            const int asked = FeatureValue("zoom");
            if (asked != zoomRow) {
                port.zoom.Choose(asked);
            }
            zoomRow = port.zoom.Choice();
            if (asked != zoomRow) {
                SetFeatureForNow("zoom", zoomRow);
                menuDirty = true;
            }
        }
        if (port.opt.moviePlay.empty() && !port.opt.autoPlay) {
            // The keyboard is every player's (each has the keys chosen for it); each gamepad is the player's the
            // launcher's Controllers page gave it, or, left at Automatic, the next player without one in the order
            // they came; one set to Not used plays nobody
            uint16_t pads[Machine::kMaxPlayers] = {};
            int keyCount = 0;
            const bool *keys = SDL_GetKeyboardState(&keyCount);
            if (keysAfterChat) {
                keysAfterChat = std::any_of(keys, keys + keyCount, [](bool down) { return down; });
            }
            if (!(SDL_GetModState() & SDL_KMOD_ALT) && !port.typing && !keysAfterChat) {
                for (int player = 0; player < Players(); player++) {
                    pads[player] |= ReadKeyboard(keys, player);
                }
                port.service = keys[SDL_SCANCODE_9] && !KeyBound(SDL_SCANCODE_9);
            }
            bool hurry = multi && keys[SDL_SCANCODE_F] && !KeyBound(SDL_SCANCODE_F);
            // (rewind: R held, or a gamepad's left trigger below; port.h, Rewind)
            const bool rewindOn = port.RewindOn() && !multi;
            bool rewinding = rewindOn && !(SDL_GetModState() & SDL_KMOD_ALT) && !port.typing && !keysAfterChat && keys[SDL_SCANCODE_R] &&
                             !KeyBound(SDL_SCANCODE_R);
            if (shoulderDownAt && SDL_GetTicks() - shoulderDownAt >= kPointHold) {
                shoulderDownAt = 0;
                if (!port.menu.IsOpen()) {
                    port.Key(']');
                }
            }
            // (the camera moved by the arrows with Left Shift held, and by each gamepad's d-pad or stick with its click held)
            if (!port.menu.IsOpen()) {
                int across = 0, down = 0;
                if (KeyZoomHeld(keys) && !(SDL_GetModState() & SDL_KMOD_ALT)) {
                    across += keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT];
                    down += keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP];
                }
                for (SDL_Gamepad *gp : gamepads) {
                    if (ZoomHeld(gp)) {
                        const int x = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
                        const int right = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kDead;
                        const int left = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kDead;
                        const int lower = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kDead;
                        const int higher = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kDead;
                        across += right - left;
                        down += lower - higher;
                        // (the click was the camera's, not the menu's, once it has been moved with it)
                        if (right || left || lower || higher) {
                            stickWaiting = false;
                        }
                    }
                }
                port.zoom.Pan(std::clamp(across, -1, 1), std::clamp(down, -1, 1));
            }
            const uint16_t touched = touch.Read(port.menu.IsOpen());
            if (touchLog && touched) {
                printf("touch %llu: %04x\n", static_cast<unsigned long long>(port.frame), touched);
            }
            pads[0] |= touched;
            bool trigger = false;
            const bool stepOnTrigger = !PadInputBound(g_bindings, Table(), {-1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER});
            if (padsGivenFor != Players()) {
                recomp::AssignPads(padChoices, gamepads.data(), static_cast<int>(gamepads.size()), Players(), playerPads);
                padsGivenFor = Players();
            }
            for (int i = 0; i < Players() && i < recomp::kBindMaxPlayers && i < Machine::kMaxPlayers; i++) {
                SDL_Gamepad *const gp = playerPads[i];
                if (!gp) continue;
                pads[i] |= ReadGamepad(gp, i);
                const bool left = stepOnTrigger && SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kDead;
                trigger = trigger || left;
                hurry = hurry || (multi && left);
                // (while the game runs, the left trigger is its player's Tag: kTag, or with rewind on, rewind; in a Multi
                // game run, fast forward)
                if (left && !port.paused && rewindOn) {
                    rewinding = rewinding || !triggerChorded;
                } else if (left && !port.paused && !multi) {
                    pads[i] |= kTag;
                }
            }
            if (!trigger) {
                triggerChorded = false;
            }
            port.rewindHeld = rewinding && !port.menu.IsOpen();
            // The left trigger goes on by one frame (held: frame after frame, after a moment), once the game
            // has been stopped: it does not stop it, so a touch of it while playing does nothing
            port.hurry = hurry && !port.menu.IsOpen();
            triggerHeld = trigger && port.paused ? triggerHeld + 1 : 0;
            if (!port.menu.IsOpen() && (triggerHeld == 1 || (triggerHeld > 24 && triggerHeld % 4 == 0))) {
                port.Key('o');
            }
            // (opposite directions held together, as the SOCD cleaner says: port/socd.h)
            const auto socd = static_cast<Socd>(std::clamp(FeatureValue("socd"), 0, 3));
            for (int player = 0; player < Machine::kMaxPlayers; player++) {
                pads[player] = socdCleaners[player].Clean(socd, pads[player]);
            }
            uint16_t all = 0;
            for (const uint16_t one : pads) {
                all |= one;
            }
            uint16_t pressed = all & ~previousPad;
            previousPad = all;
            // Start and Select held together open and close the menu, whatever they are bound to (the second
            // of them to go down is not also a press in the menu; neither reaches the game until let go)
            // (the players' gamepads: one that plays nobody does not open it)
            if (menuChord.Step(playerPads, std::min(Players(), recomp::kBindMaxPlayers))) {
                toggleMenu();
                pressed = 0;
                heldAtClose = previousPad;
            }
            const bool both = menuChord.held;
            // The menu's buttons are the same whatever the game's are set to: the arrows, the d-pad and the
            // left stick (recomp-common/menu_sdl.h, MenuHeld), and the game's own directions; Space, Enter and A
            // choose; Backspace and B go back
            unsigned menuNow = recomp::sdl::MenuHeld(keys, gamepads.data(), static_cast<int>(gamepads.size()));
            menuNow |= (previousPad & kUp) ? recomp::kMenuUp : 0;
            menuNow |= (previousPad & kDown) ? recomp::kMenuDown : 0;
            menuNow |= (previousPad & kLeft) ? recomp::kMenuLeft : 0;
            menuNow |= (previousPad & kRight) ? recomp::kMenuRight : 0;
            menuNow |= touch.Menu();
            unsigned menuPressed = menuRepeat.Step(menuNow);
            if (both) {
                menuPressed = 0;
            }
            if (port.menu.IsOpen()) {
                if (menuPressed) {
                    port.MenuAct(port.menu.Press(menuPressed));
                    menuDirty = true;
                } else if (pressed) {
                    port.menu.Note(""); // (a press of something else still takes the note away)
                    menuDirty = true;
                }
                if (!port.menu.IsOpen() || port.net) {
                    heldAtClose = previousPad; // (online, the game goes on: this side's player stands still meanwhile)
                }
            } else if (port.leaving) {
                // (online, "Leave the match?": Confirm leaves it, Back stays; this side's player stands still meanwhile)
                if (menuPressed) {
                    port.LeaveQuestion(menuPressed);
                }
                heldAtClose = previousPad;
            }
            heldAtClose &= previousPad;
            for (int player = 0; player < Machine::kMaxPlayers; player++) {
                port.pad[player] = pads[player] & ~heldAtClose;
            }
        }

        // The window takes the picture's shape again when that changes (widescreen, or the Display page's
        // shape), and its size when the Display page's size does: a window only, not full screen or maximized
        {
            static int shownColumns = columns, shownSize = scale;
            static Shape shownShape = DisplayShape();
            static bool shownBezels = DisplayBezel() != Bezel::Off;
            const int now = TheGame().widestPicture ? std::clamp(TheGame().widestPicture(), +Video::kWidth, +Video::kMaxWidth) : columns;
            const int size = port.opt.scale > 0 ? port.opt.scale : WindowSize();
            const bool bezels = DisplayBezel() != Bezel::Off;
            if (now != shownColumns || DisplayShape() != shownShape || size != shownSize || bezels != shownBezels) {
                if (!IsFullScreen(window) && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)) {
                    int windowWidth = 0, windowHeight = 0;
                    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                    if (size != shownSize) {
                        windowHeight = Video::kHeight * size;
                    }
                    SDL_SetWindowSize(window, static_cast<int>(std::lround(windowHeight * windowShape(now))), windowHeight);
                }
                shownColumns = now;
                shownShape = DisplayShape();
                shownSize = size;
                shownBezels = bezels;
            }
        }
        followShader();
        if (!port.shaderProblem.empty()) {
            // (a preset that couldn't be drawn: that it wasn't at the bottom for a while, and why in the menu, now if it is
            // open, else when it next opens; and in the log)
            port.menu.Note(port.shaderProblem);
            port.shotSaid = recomp::i18n::Tr("SHADER NOT LOADED: THE MENU SAYS WHY");
            port.shotTicks = 600;
            port.shaderProblem.clear();
        }
#ifndef __ANDROID__
        // The Arcade monitor page's screen or full screen mode, or the Display page's Full screen as, changed (the menu can
        // put them back to off): the window to that screen, full screen in that mode (ArcadeScreen::Follow's work, with
        // the borderless window too)
        if (window) {
            static recomp::sdl::ArcadeChoices shown = ArcadeChosen();
            static bool shownBorderless = BorderlessFullScreen();
            const recomp::sdl::ArcadeChoices now = ArcadeChosen();
            if (now.screen != shown.screen || now.mode != shown.mode || now.switchres != shown.switchres || now.monitor != shown.monitor ||
                BorderlessFullScreen() != shownBorderless) {
                const bool full = IsFullScreen(window);
                GoFullScreen(window, false);
                if (now.screen != shown.screen) {
                    recomp::sdl::CentreOnScreen(window, now.screen);
                }
                Arcade().SetFullscreen(window, now, false); // (the mode kept for full screen; a mode Switchres made taken back)
                GoFullScreen(window, full);
                SDL_SyncWindow(window);
                shown = now;
                shownBorderless = BorderlessFullScreen();
            }
        }
#endif
        // (synced to the screen, in full screen at an arcade mode (the one the window was given, or the desktop in the mode
        // Switchres made), Keep the board's pace off and not online: VSync on, and each frame paced by the screen's
        // refreshes (ArcadeScreen::Shown) instead of the frame timer below)
        const bool arcadeScreen = Arcade().OnArcade(window);
        const bool syncScreen = Arcade().Paced(window, ArcadeChosen(), port.net != nullptr);
        presenter.SetVsync(FeatureOn("display_vsync") || syncScreen); // (the Display page's VSync, as changed in the menu)
        port.gpuPresent = presenter.CanShowNative(); // (the frames made for the GPU from here: Machine::gpuPicture)
        if (port.quit) {
            break;
        }
        // The picture in the window as the Display page says (present.h)
        int width = 0, height = 0;
        presenter.Size(width, height);
        presenter.SetScaling(DisplayScaling());
        // (full screen in an arcade resolution: the picture fills the screen's mode, whose shape the monitor gives it; a
        // super resolution's columns stretched over its many, its lines one for one; one of 240 lines, the board's 224 in
        // its middle: ArcadeLines)
        SDL_FRect where = FitPicture(width, height, port.ShownWidth(), Video::kHeight, arcadeScreen ? Shape::Fill : DisplayShape(), DisplayScaling());
        if (arcadeScreen) {
            where = recomp::sdl::ArcadeLines(where, height, Video::kHeight);
        }
        // (the Display page's bezels under the picture: made again when they, the window's size or the picture's place
        // change; none on the arcade monitor, which the picture fills)
        {
            static Bezel madeBezel = Bezel::Off;
            static int madeWidth = 0, madeHeight = 0;
            static SDL_FRect madeWhere = {};
            const Bezel bezel = arcadeScreen ? Bezel::Off : DisplayBezel();
            if (bezel != madeBezel || width != madeWidth || height != madeHeight || where.x != madeWhere.x || where.y != madeWhere.y ||
                where.w != madeWhere.w || where.h != madeWhere.h) {
                std::vector<uint32_t> bezelPixels;
                int bezelWidth = 0, bezelHeight = 0;
                if (MakeBezel(bezel, width, height, where, port.BezelFolder(), bezelPixels, bezelWidth, bezelHeight)) {
                    presenter.SetBackdrop(bezelPixels.data(), bezelWidth, bezelHeight);
                } else {
                    presenter.SetBackdrop(nullptr, 0, 0);
                }
                madeBezel = bezel;
                madeWidth = width;
                madeHeight = height;
                madeWhere = where;
            }
        }
        // (the menu compact where the Arcade monitor page asks for it, or on the arcade monitor itself; drawn again when
        // that changes)
        if (const bool compact = CompactMenuChosen() || arcadeScreen; compact != port.menu.Compact()) {
            port.menu.SetCompact(compact);
            menuBase.clear();
            menuDirty = true;
        }
        // (the picture shown smaller than the menu's usual 672 lines (a small window, an arcade monitor): the menu drawn
        // on the screen's own pixels there, so that its letters are not shrunk; on an arcade monitor, on the board's
        // pixels, which a super resolution stretches as it does the game's; drawn again when that changes)
        {
            const int wantHeight = static_cast<int>(std::lround(where.h));
            int wantWidth = static_cast<int>(std::lround(where.w));
            if (arcadeScreen && wantHeight == Video::kHeight) {
                wantWidth = std::min(wantWidth, port.ShownWidth());
            }
            const bool own = wantHeight >= Video::kHeight && wantHeight < 3 * Video::kHeight && wantWidth < NativeGpu::kOverlayWidth - 1;
            if ((own ? wantWidth : 0) != menuWantWidth || (own ? wantHeight : 0) != menuWantHeight) {
                menuWantWidth = own ? wantWidth : 0;
                menuWantHeight = own ? wantHeight : 0;
                menuBase.clear(); // (made again at the new size where the CPU's picture is the menu's)
                menuDirty = true;
            }
        }
        touch.Layout(width, height, where);
        int touchWidth = 0, touchHeight = 0;
        if (touch.TakePicture(touchPicture, touchWidth, touchHeight)) {
            presenter.SetOverlay(touchPicture.empty() ? nullptr : touchPicture.data(), touchWidth, touchHeight);
        }

        if (port.menu.IsOpen() && !port.net) {
            // (online, the game goes on, the menu drawn on its picture: Port::Shown)
            // The game's picture as it was, with the menu over it: on the GPU (the menu, and what Shown puts over the
            // picture, an overlay of the menu's picture's size over the frame's picture made as large: NativeGpu::Overlay),
            // or the CPU's picture (not through a shader: the menu is drawn three times the board's size, and is not the
            // game's picture)
            const native::Frame *gpuFrame = port.machine->GpuFrame();
            if (gpuFrame && presenter.CanShowNative()) {
                if (menuDirty) {
                    menuDirty = false;
                    port.MenuOverlay(menuPicture, menuWidth, menuHeight, menuWantWidth, menuWantHeight);
                    menuVersion++;
                }
                const NativeGpu::Overlay over = {menuPicture.data(), static_cast<int>(menuWidth), static_cast<int>(menuHeight), false, menuVersion};
                presenter.ShowNative(*gpuFrame, port.machine->GpuVideo(), port.machine->GpuText(), where, &over);
                menuPlace = {where.x, where.y, where.w, where.h, static_cast<int>(menuWidth), static_cast<int>(menuHeight)};
            } else {
                if (menuBase.empty()) {
                    port.MenuPicture(menuBase, menuWidth, menuHeight, menuWantWidth, menuWantHeight); // (the GPU's frame gone meanwhile, or the size changed)
                    menuDirty = true;
                }
                if (menuDirty) {
                    menuDirty = false;
                    menuPicture = menuBase;
                    port.menu.Draw(menuPicture.data(), static_cast<int>(menuWidth), static_cast<int>(menuHeight));
                    menuVersion++;
                }
                presenter.Show(menuPicture.data(), static_cast<int>(menuWidth), static_cast<int>(menuHeight), where, nullptr, false);
                // (the menu's picture over the window's pixels where the game's is: present.h, the window has no high pixel
                // density, so its pixels are the mouse's points)
                menuPlace = {where.x, where.y, where.w, where.h, static_cast<int>(menuWidth), static_cast<int>(menuHeight)};
            }
            // (the checks, CPS3RECOMP_WINDOW_SHOT: the menu as the window shows it, to PREFIXmenu<frame>.png)
            if (static const char *menuShot = getenv("CPS3RECOMP_WINDOW_SHOT"); menuShot && menuVersion != menuShotVersion) {
                std::vector<uint32_t> pixels;
                int w = 0, h = 0;
                presenter.keepShown = true;
                if (presenter.ReadShown(pixels, w, h)) {
                    const std::string spec = menuShot;
                    char path[600];
                    snprintf(path, sizeof(path), "%smenu%06llu.png", spec.substr(0, spec.rfind(':')).c_str(), static_cast<unsigned long long>(port.frame));
                    WritePng(path, pixels.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                    menuShotVersion = menuVersion;
                }
            }
            SDL_Delay(8);
            nextFrame = SDL_GetTicksNS();
            Arcade().Menu(); // (the menu's time is not the game's: the rates measured, and the frame delay's last frame, start again)
            continue;
        }

        // (the game's time to the screen's: under 1 slowed, over 1 faster than the board)
        const auto rate = port.menu.GameRate();
        const double pace = port.paused ? 1.0 : static_cast<double>(rate.frames) / rate.ticks;
        const bool ran = port.Tick();

        drainSink(); // (the checks' sink: what a device would have taken by now)
        if (audio && silent != (port.menu.muted || audioLog)) {
            silent = port.menu.muted || audioLog;
            SDL_SetAudioStreamGain(audio, silent ? 0.0f : 1.0f);
        }
        std::vector<int16_t> &made = port.machine->audio;
        if (audio && ran && !made.empty()) {
            // The game makes sound at its own pace and the sound device takes it at its own, and the two
            // clocks never quite agree. So the sound is played a touch faster when it is piling up and a
            // touch slower when it is running out (a fraction of a percent, far too little to hear), which
            // keeps a frame or two of it waiting: no gaps, no growing delay.
            // (Slowed, the sound is played slower by as much; faster, faster.)
            double waiting = SDL_GetAudioStreamQueued(audio) / (4.0 * static_cast<int>(Machine::kSampleRate + 0.5)) / pace;
            // (When much more than that is waiting (the game caught up after being held up, say) what waits
            // is thrown away: a click once, rather than sound that stays late for the seconds it would take
            // to play it off.)
            if (waiting > kAudioMost) {
                SDL_ClearAudioStream(audio);
                waiting = 0;
                audioDropped++;
            }
            const double ratio = (1.0 + std::clamp((waiting - kAudioWaiting) * 0.5, -0.02, 0.02)) * pace;
            SDL_SetAudioStreamFrequencyRatio(audio, static_cast<float>(ratio));
            audioLeast = std::min(audioLeast, waiting);
            if (audioLog && port.frame % 60 == 0) {
                fprintf(stderr, "audio: frame %llu, %.0f ms waiting (least %.0f), ratio %.4f, thrown away %d times\n",
                        static_cast<unsigned long long>(port.frame), waiting * 1000, audioLeast * 1000, ratio, audioDropped);
                audioLeast = 1;
            }
            SDL_PutAudioStreamData(audio, made.data(), static_cast<int>(made.size() * sizeof(int16_t)));
        }
        // (the video being recorded: the game's sound as heard; the soundtrack's below)
        const bool recording = ran && port.recorder.Recording();
        std::vector<int16_t> recorded;
        if (recording) {
            recorded = made;
        }
        made.clear();
        bool musicFresh = false;
        if (ran && port.soundtrackOn && port.soundtrack.Any()) {
            // (fed as the game's sound is: the game's time's worth each frame, kept a frame or two ahead of the device)
            port.soundtrack.Make(port.machine->soundtrack.state, !FastForward() && !port.hurry, musicMade);
            musicFresh = true;
            if (!musicMade.empty()) {
                const int rate = port.soundtrack.Rate();
                if (!music) {
                    musicSpec.freq = rate;
                    music = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &musicSpec, nullptr, nullptr);
                    if (music) {
                        SDL_ResumeAudioStreamDevice(music);
                    } else {
                        fprintf(stderr, "Audio (soundtrack): %s\n", SDL_GetError());
                    }
                } else if (rate != musicSpec.freq) {
                    musicSpec.freq = rate;
                    SDL_SetAudioStreamFormat(music, &musicSpec, nullptr);
                }
            }
            if (music && !musicMade.empty()) {
                if (musicSilent != (port.menu.muted || audioLog)) {
                    musicSilent = port.menu.muted || audioLog;
                    SDL_SetAudioStreamGain(music, musicSilent ? 0.0f : 1.0f);
                }
                double waiting = SDL_GetAudioStreamQueued(music) / (8.0 * musicSpec.freq) / pace;
                if (waiting > kAudioMost) {
                    SDL_ClearAudioStream(music);
                    waiting = 0;
                }
                const double ratio = (1.0 + std::clamp((waiting - kAudioWaiting) * 0.5, -0.02, 0.02)) * pace;
                SDL_SetAudioStreamFrequencyRatio(music, static_cast<float>(ratio));
                SDL_PutAudioStreamData(music, musicMade.data(), static_cast<int>(musicMade.size() * sizeof(float)));
            }
        }
        if (recording) {
            port.VideoFrame(recorded.data(), recorded.size() / 2, musicFresh ? musicMade.data() : nullptr, musicFresh ? musicMade.size() / 2 : 0,
                            port.soundtrack.Rate());
        }

        Arcade().WorkDone(); // (the frame run: drawing it and showing it are the presenter's, below)
        // The frame drawn by the GPU from the native video interface (Presenter::ShowNative), where the machine made it
        // for the GPU, what Shown puts over it put over it there (an overlay: NativeGpu::Overlay); else the CPU's
        // picture as Shown makes it. Zoomed: the part of the picture the camera looks at, the readout's panels as they
        // are. Either way one command buffer, submitted here
        port.WindowedStep();
        const native::Frame *gpuFrame = port.machine->GpuFrame();
        Zoomed zoomed;
        bool shownNative = false;
        if (gpuFrame && presenter.CanShowNative()) {
            const bool isZoomed = port.WindowedOverlay(overlay, zoomed);
            const NativeGpu::Overlay over = {overlay.data(), port.ShownWidth(), Video::kHeight, isZoomed, ++overlayVersion};
            shownNative = presenter.ShowNative(*gpuFrame, port.machine->GpuVideo(), port.machine->GpuText(), where, overlay.empty() ? nullptr : &over,
                                               isZoomed ? &zoomed : nullptr);
        }
        if (!shownNative) {
            const bool isZoomed = port.WindowedPicture(shown, zoomed);
            presenter.Show(shown.data(), port.ShownWidth(), Video::kHeight, where, isZoomed ? &zoomed : nullptr);
        }
        port.LagShown(ran, SDL_GetTicksNS());
        port.FrameShown(SDL_GetTicksNS());
        // (the checks, CPS3RECOMP_WINDOW_SHOT=PREFIX:EVERY: every so many frames what the window was shown, read back, to
        // PREFIX<frame>.png: games/sfiii3/checkwide.sh)
        static const char *windowShot = getenv("CPS3RECOMP_WINDOW_SHOT");
        if (windowShot && ran) {
            const std::string spec = windowShot;
            const size_t colon = spec.rfind(':');
            const uint64_t every = colon == std::string::npos ? 60 : std::max<uint64_t>(1, strtoull(spec.c_str() + colon + 1, nullptr, 10));
            presenter.keepShown = true;
            std::vector<uint32_t> pixels;
            int w = 0, h = 0;
            if (port.frame % every == 0 && presenter.ReadShown(pixels, w, h)) {
                char path[600];
                snprintf(path, sizeof(path), "%s%06llu.png", spec.substr(0, colon).c_str(), static_cast<unsigned long long>(port.frame));
                WritePng(path, pixels.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
        }

        // (the instant replay being made, if it is: a few of its frames in the time this tick has to spare, a little
        // over half of it; unthrottled or fast forward, 4 ms)
        if (port.instant.making) {
            const uint64_t now = SDL_GetTicksNS(), due = nextFrame + static_cast<uint64_t>(kFrameNs);
            const bool paced = !port.opt.unthrottled && !FastForward();
            port.ReplayMake(paced ? (due > now ? (due - now) / 1e6 * 0.6 : 0.0) : 4.0);
        }
        if (syncScreen && !port.opt.unthrottled && !FastForward()) {
            nextFrame = Arcade().Shown(); // (paced by the screen: the picture just shown waited for its refresh; the frame delay)
        } else if (!port.opt.unthrottled && !FastForward()) {
            Arcade().BoardPace(arcadeScreen); // (in an arcade mode, the rate as measured in the log)
            nextFrame += static_cast<uint64_t>(kFrameNs);
            const uint64_t now = SDL_GetTicksNS();
            if (nextFrame > now) {
                SDL_DelayPrecise(nextFrame - now);
            } else if (now - nextFrame > 100000000ull) {
                nextFrame = now; // (fell far behind: start counting again)
            }
        }
    }
    // (for the checks: what a shaded window showed last, CPS3RECOMP_SHADER_SHOT=FILE.png)
    if (const char *shot = getenv("CPS3RECOMP_SHADER_SHOT"); shot && presenter.Shaded()) {
        std::vector<uint32_t> pixels;
        int width = 0, height = 0;
        if (presenter.Shader()->ReadBack(pixels, width, height)) {
            WritePng(shot, pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
    }
    for (SDL_Gamepad *gp : gamepads) {
        SDL_CloseGamepad(gp);
    }
    if (sink.file) {
        fclose(sink.file);
        fprintf(stderr, "cps3recomp: audio sink: %llu frames at %d Hz, %llu quanta short\n", static_cast<unsigned long long>(sink.taken), sink.rate,
                static_cast<unsigned long long>(sink.short_));
    }
    if (audio) {
        SDL_DestroyAudioStream(audio);
    }
    if (music) {
        SDL_DestroyAudioStream(music);
    }
    // (on to a Multi game run's next game: its window where this one is; on macOS the launcher starts it, and must
    // be in front for it to come forward)
    if (ProgramEnded() == kNextGame && !MultiGame().empty()) {
        KeepPlacement(window);
#ifdef __APPLE__
        HandFrontToLauncher();
#endif
    }
    presenter.Close();
    Arcade().Close(); // (the desktop's mode as it was, where Switchres made one)
    SDL_DestroyWindow(window);
    while (port.instant.making) { // (a replay being made, finished first)
        port.ReplayMake(1000); // (no game to keep at speed any more)
    }
    port.StopVideo(); // (a video being recorded finished: before SDL_Quit, which takes its sound converters with it)
    SDL_Quit();
    return 0;
}

} // namespace port
