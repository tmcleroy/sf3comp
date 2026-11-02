// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// The port's entry point: reads the ROM set, runs the game, shows and plays what it makes. (cps2recomp's, with the
// CPS-2's own parts taken out: its sound CPU and driver, the board timing, the custom soundtrack, extra sets.)
#include "game_info.h"

#include "../runtime/game.h"
#include "../runtime/state.h"
#include "hud.h"
#include "png_write.h"
#include "present.h"
#include "port.h"
#include "../runtime/recomp.h"
#include "../recomp-common/folders.h"
#include "../recomp-common/proxy.h"
#include "../recomp-common/settings.h"
#include "../recomp-common/text.h"

#include <SDL3/SDL_main.h> // (on Windows the game is a windowed program: its main is called from WinMain)

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace cps3recomp;

namespace port {

bool Movie::Load(const std::string &path) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        return false;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long frame;
        unsigned p[4] = {};
        if (line[0] != '#' && sscanf(line, "%llu %x %x %x %x", &frame, &p[0], &p[1], &p[2], &p[3]) >= 3) {
            changes[frame] = {static_cast<uint16_t>(p[0]), static_cast<uint16_t>(p[1]), static_cast<uint16_t>(p[2]), static_cast<uint16_t>(p[3])};
        }
    }
    fclose(f);
    return true;
}

bool Movie::Save(const std::string &path) const {
    FILE *f = fopen(path.c_str(), "w");
    if (!f) {
        return false;
    }
    // (as many players as any line has a press for, and two at least: a two-player movie is as it always was)
    int players = 2;
    for (const auto &[frame, pads] : changes) {
        for (int n = 2; n < static_cast<int>(pads.size()); n++) {
            players = pads[n] ? std::max(players, n + 1) : players;
        }
    }
    fprintf(f, "# cps3recomp input movie: frame player1 player2%s%s (pressed bits, hex)\n", players > 2 ? " player3" : "", players > 3 ? " player4" : "");
    for (const auto &[frame, pads] : changes) {
        fprintf(f, "%llu", static_cast<unsigned long long>(frame));
        for (int n = 0; n < players; n++) {
            fprintf(f, " %04x", pads[n]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

namespace {
    constexpr uint32_t kStateVersion = 2; // (2: the colours made again from colour RAM, not kept)

    bool ReadFile(const std::string &path, std::vector<uint8_t> &data) {
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        fseek(f, 0, SEEK_END);
        data.resize(static_cast<size_t>(ftell(f)));
        fseek(f, 0, SEEK_SET);
        const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        return ok;
    }
    bool WriteFile(const std::string &path, const void *data, size_t size) {
        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        const bool ok = fwrite(data, 1, size, f) == size;
        fclose(f);
        return ok;
    }
    // "FRAME:path"
    bool AtFrame(const std::string &spec, uint64_t frame, std::string &path) {
        const size_t colon = spec.find(':');
        if (spec.empty() || colon == std::string::npos || strtoull(spec.c_str(), nullptr, 10) != frame) {
            return false;
        }
        path = spec.substr(colon + 1);
        return true;
    }
    uint64_t Random(uint64_t &state) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
} // namespace

// Every game's features that come from the port itself (after the game's own: RegisterGame), for the game and
// for the list the launcher asks for (--list-features)
void RegisterPortFeatures() {
    // (run_ahead: Port::RunAhead)
    TheGame().features.push_back({"run_ahead", "Game", "Run-ahead",
                                  "Shows the game a frame or two further on than it is, so that what you press is seen sooner than "
                                  "on the board (input lag taken away). The game runs as it does without it; it takes two or three "
                                  "times the processor. Off by itself in the game's own modes beyond fights and training.",
                                  0, {"Off", "1 frame", "2 frames"}});
    // (rapid_fire: Port::RapidFire)
    TheGame().features.push_back({"rapid_fire", "Game", "Rapid fire",
                                  "A button held down is pressed again and again (fifteen times a second), for the first button or "
                                  "for all of them. The first press is seen at once, as without it.",
                                  0, {"Off", "First button", "Every button"}});
    // (display_*: how the picture is fitted to the window, present.h; the first of each is how it always was)
    TheGame().features.push_back({"display_scaling", "Display", "Scaling",
                                  "Sharp: as large as fits, each pixel a crisp block. Whole pixels: every pixel the same whole "
                                  "number of the screen's pixels tall (a border round it). Smooth: softened all over.",
                                  0, {"Sharp", "Whole pixels", "Smooth"}});
    TheGame().features.push_back({"display_shape", "Display", "Picture shape",
                                  "4:3: as the arcade's monitor showed it. Square pixels: each pixel as wide as it is tall, "
                                  "a little wider than 4:3. Fill: the whole window or screen, stretched to it.",
                                  0, {"4:3", "Square pixels", "Fill"}});
    // (display_shader: Port::ShaderPreset, shading.h; the presets are in shaders/ beside the program, kShaderPresets.
    // librashader is in each release, tools/librashader.sh; on Android the presets are unpacked by GameActivity.java)
    TheGame().features.push_back({"display_shader", "Display", "Shader",
                                  "The picture drawn through one of RetroArch's shaders. CRT: a curved arcade monitor. CRT "
                                  "Lottes and CRT Guest: flat monitors, sharp and bright. CRT Royale: the closest to a real "
                                  "monitor's glass and the slowest. CRT Light: lines and a faint mask, for a slow computer. "
                                  "Scanlines: the lines alone, light, strong, as an OSSC draws them, or dark (a black line "
                                  "between each two, best with Whole pixels). xBRZ and ScaleFX: the pixels' edges made smooth "
                                  "lines and curves. Your own: the first .slangp preset in the shaders folder beside the "
                                  "game's settings. The launcher chooses which of these the menu goes through. The picture's "
                                  "shape and size stay as set; the shader draws its pixels in place of Scaling.",
                                  kFeatureMenuChoices,
                                  {"Off", "CRT", "CRT Lottes", "CRT Royale", "CRT Guest", "CRT Light", "Scanlines: light",
                                   "Scanlines: strong", "Scanlines: OSSC", "Scanlines: dark", "xBRZ", "ScaleFX", "Your own"}});
    // (zoom: the camera, port/zoom.h, from the menu as well as the zoom's buttons; for this run only, as they are)
    if (Zoom::Possible()) {
        TheGame().features.push_back({"zoom", "Display", "Zoom",
                                      "The picture shown larger about a fighter, following it: player 1's or player 2's (1.5x; the "
                                      "zoom's buttons change the size), or both fighters', as close as keeps them both in the "
                                      "picture. Also the left stick's click held with a shoulder (with the right stick's: both), "
                                      "Left Shift with Q, E or W, or a touch screen's Zoom button (both).",
                                      kFeatureSession, {"Off", "Player 1", "Player 2", "Both fighters"}});
    }
    // (online play's readout, each player's own: Port::Shown)
    TheGame().features.push_back({"online_ping", "Online", "Ping",
                                  "During an online match, the time a packet takes to the other player and back, at the bottom of "
                                  "the picture (\"PING 42 ms\").",
                                  kFeatureEnhanced});
    TheGame().commands.push_back({"online_host", "Online", "Host a match",
                                  "Others can join you online while you play on as you like (modes, the menu, save states): "
                                  "the match's code is at the bottom of the screen to give them, and with your name from the "
                                  "launcher's Online page it's in the lobby too. When someone joins, the game starts over "
                                  "for the match. This row again stops hosting.",
                                  nullptr});
    // (names: shown unless hidden, whatever the preset, so a switch that hides them)
    TheGame().features.push_back({"online_hide_names", "Online", "Hide names",
                                  "During an online match, each player's name is under their life bar (the name each gave on "
                                  "the launcher's Online page): this hides them.",
                                  0});
    TheGame().features.push_back({"online_delay", "Online", "Input delay",
                                  "During an online match, how many frames each player's own controls are held back, at the bottom "
                                  "of the picture (\"DELAY 2\"): the host chooses it.",
                                  0});
    // (display_vsync: Presenter::SetVsync, issue #185)
    TheGame().features.push_back({"display_vsync", "Display", "VSync",
                                  "Each picture shown when the screen refreshes, so the picture never tears. The game keeps the "
                                  "board's pace (59.59 frames a second), so now and then a frame stays up for one more of the "
                                  "screen's refreshes (on a 60 Hz screen, about every three seconds), and the controls can be a "
                                  "frame later; on a screen slower than 60 Hz the game slows to it. Off: each picture shown as soon "
                                  "as it's made.",
                                  0});
    TheGame().features.push_back({"window_size", "Display", "Window size",
                                  "How large the window opens, in times the board's 224 lines (it can be made any size by "
                                  "dragging its edges; full screen is on the launcher's first page and F11).",
                                  0, {"3x", "2x", "4x", "5x", "6x"}});
}

Scaling DisplayScaling() {
    return static_cast<Scaling>(std::clamp(FeatureValue("display_scaling"), 0, 2));
}
Shape DisplayShape() {
    return static_cast<Shape>(std::clamp(FeatureValue("display_shape"), 0, 2));
}
int WindowSize() {
    static const int kTimes[] = {3, 2, 4, 5, 6};
    return kTimes[std::clamp(FeatureValue("window_size"), 0, 4)];
}

bool Port::Init(std::string &error) {
    RomList list;
    list.set = GAME_ROM_SET;
    list.parent = GAME_ROM_PARENT;
    list.bios = ParseRomFiles(GAME_BIOS);
    const char *simms[7] = {"", GAME_SIMM1, GAME_SIMM2, GAME_SIMM3, GAME_SIMM4, GAME_SIMM5, GAME_SIMM6};
    for (int s = 1; s <= 6; s++) {
        list.simm[s] = ParseRomFiles(simms[s]);
    }
    list.programEnciphered = std::string(GAME_PROGRAM_ENCIPHERED) != "0";
    if (!LoadRomSet(opt.romPath, list, roms, error)) {
        return false;
    }
    machine = std::make_unique<Machine>(roms);
    SetRam(machine->ram());
    RegisterGame();
    RegisterPortFeatures();
    LoadFeatures(opt.settingsPath, opt.features);
    // The game's SH-2 code: translated (native), the interpreter run the same way block by block (free: the
    // translation's reference), or the interpreter an instruction at a time (interp)
    if (opt.cpu.empty()) {
        opt.cpu = "native";
    }
    if (opt.cpu != "native" && opt.cpu != "free" && opt.cpu != "interp") {
        error = "--cpu: native, free or interp";
        return false;
    }
    if (!opt.eepromPath.empty()) {
        std::vector<uint8_t> data;
        if (ReadFile(opt.eepromPath, data) && data.size() == Eeprom::kBytes) {
            memcpy(machine->eeprom.words, data.data(), data.size());
        }
    }
    machine->Reset();
    if (opt.cpu != "interp") {
        cps3recomp::Options recomp;
        recomp.mode = opt.cpu == "free" ? cps3recomp::Mode::Free : cps3recomp::Mode::Native;
        recomp.recordPath = opt.recordEntries;
        recomp.stats = opt.stats;
        cps3recomp::Attach(machine->board, recomp);
    }
    if (!opt.moviePlay.empty() && !playMovie.Load(opt.moviePlay)) {
        error = "cannot read the movie " + opt.moviePlay;
        return false;
    }
    if (!opt.hashLog.empty()) {
        hashFile = fopen(opt.hashLog.c_str(), "w");
    }
    autoState = opt.autoSeed * 0x9E3779B97F4A7C15ull + 0x1234567ull;
    rollbackTest.random = opt.rollbackTest * 0x9E3779B97F4A7C15ull + 0x89ABCDEull;
    return true;
}

// The Display page's Shader's presets, in the order of its choices after Off (the folders and files of RetroArch's
// slang-shaders, copied to shaders/ beside the program by tools/shaders.py), and before them what Your own is
namespace {
    const char *const kShaderPresets[] = {
        "crt/crt-geom-mini.slangp", "crt/crt-lottes.slangp", "crt/crt-royale.slangp", "crt/crt-guest-advanced-fast.slangp",
        "crt/zfast-crt.slangp", "scanlines/scanline-fract.slangp", "pixel-art-scaling/sharp-bilinear-scanlines.slangp",
        "scanlines/ossc.slangp", "scanlines/integer-scaling-scanlines.slangp",
        "edge-smoothing/xbrz/xbrz-freescale-multipass.slangp", "edge-smoothing/scalefx/scalefx.slangp",
    };
    constexpr int kYourOwnShader = 1 + static_cast<int>(std::size(kShaderPresets));
} // namespace

std::string Port::ShaderPreset() const {
    if (!opt.shader.empty()) {
        return opt.shader;
    }
    const int value = FeatureValue("display_shader");
    std::error_code error;
    if (value == kYourOwnShader) {
        // (the first .slangp, by name, in shaders/ beside settings.ini)
        if (opt.settingsPath.empty()) {
            return "";
        }
        std::vector<std::filesystem::path> found;
        for (const auto &entry : std::filesystem::directory_iterator(TextPath(opt.settingsPath).parent_path() / "shaders", error)) {
            if (entry.path().extension() == ".slangp") {
                found.push_back(entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        return found.empty() ? "" : PathText(found.front());
    }
    if (value < 1 || value >= kYourOwnShader) {
        return "";
    }
    // (beside the program, or in a macOS app its Resources; CPS3RECOMP_SHADERS for another folder)
    if (const char *folder = getenv("CPS3RECOMP_SHADERS")) {
        return PathText(TextPath(folder) / kShaderPresets[value - 1]);
    }
    const char *base = SDL_GetBasePath();
    const std::filesystem::path program = base ? TextPath(base) : std::filesystem::path(".");
    for (const std::filesystem::path &at : {program / "shaders", program / ".." / "Resources" / "shaders"}) {
        if (std::filesystem::exists(at / kShaderPresets[value - 1], error)) {
            return PathText(at / kShaderPresets[value - 1]);
        }
    }
    return PathText(program / "shaders" / kShaderPresets[value - 1]);
}

// What the game's state comes to, for comparing runs: the CPU, its memory, the video and object memory, the
// registers and the picture
uint32_t Port::StateHash() const {
    Machine &m = *machine;
    uLong h = crc32(0, nullptr, 0);
    auto add = [&](const void *p, size_t size) { h = crc32(h, static_cast<const Bytef *>(p), static_cast<uInt>(size)); };
    if (opt.hashPicture) {
        add(m.Picture(), sizeof(uint32_t) * m.PictureWidth() * Video::kHeight);
        return static_cast<uint32_t>(h);
    }
    const machine::SH2 &cpu = m.board.Cpu();
    add(cpu.R.data(), sizeof(uint32_t) * 16);
    const uint32_t regs[] = {cpu.PC, cpu.PR, cpu.GBR, cpu.VBR, cpu.SR.u32, cpu.MAC.L, cpu.MAC.H};
    add(regs, sizeof(regs));
    add(m.ram(), 0x80000);
    if (!opt.hashNoPicture) {
        add(m.Picture(), sizeof(uint32_t) * m.PictureWidth() * Video::kHeight);
    }
    return static_cast<uint32_t>(h);
}

void Port::MenuAct(Menu::Action action) {
    std::string error;
    const std::string path = StatePath(menu.Slot());
    switch (action) {
    case Menu::Action::None: return;
    case Menu::Action::Coin:
        coinFrames = 6;
        menu.Close();
        return;
    case Menu::Action::Start2:
        start2Frames = 6;
        menu.Close();
        return;
    case Menu::Action::Test:
        testFrames = 6;
        menu.Close();
        return;
    case Menu::Action::ResetOptions:
        ResetFeatures();
        menu.SetFullSpeed();
        menu.Note("Every option is back as it first was.");
        return;
    case Menu::Action::Reset:
        EndSessionFeatures();
        machine->Reset();
        menu.Close();
        return;
    case Menu::Action::Quit:
        quit = true;
        menu.Close();
        return;
    case Menu::Action::Command:
        RunCommand(menu.Command());
        menu.Close();
        return;
    case Menu::Action::Save:
        if (SaveState(path, error)) {
            menu.Close();
            return;
        }
        break;
    case Menu::Action::Load:
        if (LoadState(path, error)) {
            menu.Close();
            return;
        }
        error = "Nothing is kept in slot " + std::to_string(menu.Slot()) + ".";
        break;
    }
    menu.Note(error);
}

void Port::RunCommand(size_t index) {
    const auto &commands = TheGame().commands;
    if (index < commands.size() && !strcmp(commands[index].name, "online_host")) {
        HostFromMenu();
        return;
    }
    if (index < commands.size() && commands[index].run) {
        commands[index].run(*machine);
    }
}

void Port::Key(char key, unsigned n) {
    if (net && key != 'z' && key != 'm' && key != 'l' && key != 'y') {
        return; // (online: only the leave question, and the zoom's, which does nothing: Zoom::Forbid)
    }
    switch (key) {
    case 'p':
        paused = !paused;
        steps = 0;
        break;
    case 'o': // (stops the game if it is running)
        if (paused) {
            steps++;
        }
        paused = true;
        break;
    case '-': menu.Faster(-1); break;
    case '=': menu.Faster(1); break;
    case 'h': hurry = !hurry; break; // (tests: the frontend sets it while F is held)
    case 'l': leaving = net && !leaving; break; // (tests: online, Esc's "Leave the match?")
    case 'y': // (tests: ... answered yes)
        if (leaving) {
            LeaveOnline();
        }
        break;
    case 'z': // (tests: the zoom pressed for player n + 1; 2: for both)
        if (n == Zoom::kBoth) {
            zoom.Both();
        } else {
            zoom.Press(static_cast<int>(n));
        }
        break;
    case 'm': // (tests: the zoom's camera moved a step: 1 left, 2 right, 3 up, 4 down)
        zoom.Pan(n == 1 ? -1 : n == 2 ? 1 : 0, n == 3 ? -1 : n == 4 ? 1 : 0);
        break;
    case 'n':
        if (!MultiGame().empty() && NextGameAsked() < 0) {
            AskNextGame(static_cast<int64_t>(tick));
        }
        break;
    case 'k':
        for (size_t i = 0; i < TheGame().commands.size(); i++) {
            if (TheGame().commands[i].key == n && n != kKeyNone) {
                RunCommand(i);
            }
        }
        break;
    default: break;
    }
}

namespace {
constexpr uint64_t kNextGameWait = 150; // ticks (so stopped too)

// (hosting a player invited from the launcher's waiting room: whether they have been asked, or said no)
std::string InvitationLine(const Netplay &net) {
    return net.Invitation() == "declined" ? net.Invited() + " declined"
           : net.Invitation() == "away"   ? net.Invited() + " couldn't be invited"
                                          : "Invited " + net.Invited();
}
} // namespace

bool Port::Tick() {
    if (ProgramEnded() >= 0) {
        quit = true; // (the game's sources are done: runtime/game.h, EndProgram)
        return false;
    }
    // (tests: --keys TICK:KEY,...: keys pressed at those ticks; a digit after 'k' is the command key's number)
    for (size_t at = 0; at < opt.keys.size();) {
        const size_t colon = opt.keys.find(':', at);
        if (colon == std::string::npos) {
            break;
        }
        if (strtoull(opt.keys.c_str() + at, nullptr, 10) == tick && colon + 1 < opt.keys.size()) {
            const char key = opt.keys[colon + 1];
            const size_t end = std::min(opt.keys.find(',', colon), opt.keys.size());
            if (key == 'c') { // (c and a command's name)
                const std::string name = opt.keys.substr(colon + 2, end - colon - 2);
                for (size_t i = 0; i < TheGame().commands.size(); i++) {
                    if (name == TheGame().commands[i].name) {
                        RunCommand(i);
                    }
                }
            } else if (key == 'f') { // (f and NAME=N: a feature set)
                const std::string what = opt.keys.substr(colon + 2, end - colon - 2);
                const size_t eq = what.find('=');
                if (eq != std::string::npos) {
                    SetFeatureValue(what.substr(0, eq).c_str(), atoi(what.c_str() + eq + 1));
                }
            } else {
                Key(key, key == 'k' || key == 'z' || key == 'm' ? static_cast<unsigned>(atoi(opt.keys.c_str() + colon + 2)) : 0);
            }
        }
        const size_t comma = opt.keys.find(',', colon);
        at = comma == std::string::npos ? opt.keys.size() : comma + 1;
    }
    tick++;
    if (opt.online && !net && !rejoin.empty()) {
        // (joining again: a moment between tries)
        SetHud({{0.5f, 0.36f, 0, rejoin}, {0.5f, 0.48f, 0, "Joining again in a moment"}, {0.5f, 0.6f, 0, "Esc: quit"}});
        if (tick >= againAt) {
            Again(rejoin);
        }
        return false;
    }
    if (net && connecting) {
        // (the other player not here yet: said on the screen, as the readout, over the board's black picture)
        std::string error;
        const int state = net->Waiting(*this, error);
        if (state < 0) {
            if (!rejoin.empty()) {
                // (joining again: the host may not be back yet; tried every few seconds until Esc)
                fprintf(stderr, "online: %s\n", error.c_str());
                net.reset();
                againAt = tick + 180;
                return false;
            }
            if (hostedFromMenu) {
                SetHud({});
                BackToSolo(false);
                hostNote = "Stopped hosting: " + error;
                hostNoteUntil = tick + 300;
                return false;
            }
            net->ended = error;
            net->failed = true;
            quit = true;
            SetHud({});
            return false;
        }
        if (state == 0 && hostedFromMenu && !net->Found()) {
            // (the one who came went quiet before the match began: the player's own game again, still hosted)
            SetHud({});
            BackToSolo(true);
            return false;
        }
        if (state == 0 && net->Arcade()) {
            // (--net-arcade: the game played meanwhile, as offline but with the match's features, the wait said at
            // the bottom (Shown); until someone has come, who is then sent the game as it is, and it waits for them)
            SetHud({});
            if (net->Found() || leaving) {
                return false;
            }
            ArcadeFrame();
            return true;
        }
        if (state == 0) {
            // (short lines, each its own panel: one wider than the picture is not drawn; the dots padded so that the
            // line keeps its width; the code in green)
            static const char *const kDots[] = {"   ", ".  ", ".. ", "..."};
            std::vector<HudLine> lines;
            if (!rejoin.empty()) {
                lines.push_back({0.5f, 0, 0, rejoin});
            }
            lines.push_back({0.5f, 0, 0, net->waiting + kDots[(tick / 30) % 4]});
            if (!net->code.empty()) {
                lines.push_back({0.5f, 0, 0, "Match code " + net->code, 96, 230, 120});
                if (net->Listed()) {
                    lines.push_back({0.5f, 0, 0, "Listed in the lobby"});
                }
                if (!net->Invited().empty()) {
                    lines.push_back({0.5f, 0, 0, InvitationLine(*net)});
                }
            }
            lines.push_back({0.5f, 0, 0, rejoin.empty() ? "Esc: give up" : "Esc: quit"});
            for (size_t n = 0; n < lines.size(); n++) {
                lines[n].y = 0.5f + (static_cast<float>(n) - (lines.size() - 1) / 2.0f) * 0.1f;
            }
            SetHud(lines);
            return false;
        }
        SetHud({});
        Matched();
    }
    if (net) {
        // Online: a frame a tick when the other side's controls allow (and as fast as it goes while the game's
        // sources take it somewhere); no stopping, slowing or speeding up
        const bool ready = net->Ready(frame);
        HearChat();
        if (!opt.netTyping.empty()) {
            typing = true;
            typed = opt.netTyping;
        }
        if (!opt.netSay.empty() && frame >= opt.netSayAt) {
            typed = opt.netSay;
            opt.netSay.clear();
            Say();
        }
        if (!ready) {
            if (!net->ended.empty()) {
                // (the other side gone: a window waits for them again; the host's leaving ends the joining side's)
                const bool again = !opt.headless && (net->Hosting() ? net->left || net->lost : net->lost);
                if (hostedFromMenu) {
                    BackToSolo(again); // (the other side gone: still hosted, the player's own game again)
                } else if (again) {
                    Again(net->ended);
                } else {
                    quit = true;
                }
            }
            return false;
        }
        RunFrame();
        for (int n = 0; n < 120 && FastForward() && net->Ready(frame); n++) {
            machine->audio.clear();
            RunFrame();
        }
        return true;
    }
    // (the next game asked for: the game's sources have had their time to end it their own way)
    if (NextGameAsked() >= 0 && tick >= static_cast<uint64_t>(NextGameAsked()) + kNextGameWait) {
        EndProgram(kNextGame);
    }
    if (FastForward()) {
        // The game's sources are taking the game somewhere: as fast as it goes, a slice at a time
        // (no further than --frames asks)
        for (int n = 0; n < 120 && FastForward() && (opt.frames == 0 || frame < opt.frames); n++) {
            RunFrame();
            machine->audio.clear();
        }
        return true;
    }
    int frames = 1;
    if (paused) {
        if (steps == 0) {
            return false;
        }
        steps--;
    } else {
        const Menu::Rate rate = menu.GameRate();
        owed += rate.frames;
        frames = owed / rate.ticks;
        owed %= rate.ticks;
        if (frames == 0) {
            return false;
        }
    }
    if (hurry && !paused && !MultiGame().empty()) {
        // A Multi game run's fast forward: kHurry frames, unheard
        for (int n = 0; n < kHurry; n++) {
            RunFrame();
            machine->audio.clear();
        }
        return true;
    }
    // (faster than the board: those frames and all their sound, which the frontend plays faster by as much)
    for (int n = 0; n < frames; n++) {
        RunFrame();
    }
    return true;
}

void Port::Shown(std::vector<uint32_t> &pixels, std::vector<SDL_Rect> *panels) const {
    const int across = ShownWidth();
    pixels.assign(machine->Picture(), machine->Picture() + across * Video::kHeight);
    DrawHud(pixels.data(), across, Video::kHeight, panels);
    // A line at the bottom, in the middle (`up` lines higher)
    auto status = [&](const char *text, int up) {
        const recomp::text::Canvas canvas{pixels.data(), across, Video::kHeight};
        const float width = recomp::text::TextWidth(text, 1);
        const float x = (across - width) / 2, y = static_cast<float>(Video::kHeight - 11 - up);
        canvas.Fill(x - 3, y - 2, width + 6, 12, {20, 22, 30, 220});
        canvas.Text(x, y, 1, 0, {255, 255, 255, 255}, text);
        if (panels) {
            panels->push_back({static_cast<int>(x) - 3, static_cast<int>(y) - 2, static_cast<int>(width) + 7, 12});
        }
    };
    // Stopped or fast-forwarded: said at the bottom all the while (stopped, with the frame's number, for counting
    // frames by stepping); slowed or sped up, for a few seconds after the speed changes (speedTicks)
    const bool hurried = hurry && !paused && !MultiGame().empty();
    const bool told = paused || hurried || (!menu.FullSpeed() && speedTicks > 0);
    if (told) {
        char text[64];
        if (paused) {
            snprintf(text, sizeof(text), "STOPPED  frame %llu", static_cast<unsigned long long>(frame));
        } else if (hurried) {
            snprintf(text, sizeof(text), "FAST FORWARD x%d", kHurry);
        } else {
            const Menu::Rate rate = menu.GameRate();
            if (rate.frames < rate.ticks) {
                snprintf(text, sizeof(text), "SPEED %d/%d", rate.frames, rate.ticks);
            } else {
                snprintf(text, sizeof(text), "SPEED x%g", static_cast<double>(rate.frames) / rate.ticks);
            }
        }
        status(text, 0);
    }
    // (online: the ping and the input delay, as the Display page has them, or that the other side has gone quiet; and
    // above, until this side's player has pressed Start, that it lets them in)
    int said = told ? 1 : 0;
    // (hosting from the in-game menu, the game the player's own meanwhile: the match's code; or why it stopped)
    if (waitingHost) {
        const std::string code = waitingHost->code;
        status(code.empty() ? "HOSTING" : ("HOSTING  CODE " + code + (waitingHost->Listed() ? "  IN THE LOBBY" : "")).c_str(), said++ * 14);
    } else if (tick < hostNoteUntil) {
        status(hostNote.c_str(), said++ * 14);
    }
    if (net && !connecting) {
        char text[64] = "";
        if (net->Silent() > 1000) {
            snprintf(text, sizeof(text), "WAITING FOR THE OTHER PLAYER");
        } else if (FeatureOn("online_ping") && FeatureOn("online_delay")) {
            snprintf(text, sizeof(text), "PING %d ms   DELAY %d", net->Ping(), net->Delay());
        } else if (FeatureOn("online_ping")) {
            snprintf(text, sizeof(text), "PING %d ms", net->Ping());
        } else if (FeatureOn("online_delay")) {
            snprintf(text, sizeof(text), "DELAY %d", net->Delay());
        }
        if (*text) {
            status(text, said++ * 14);
        }
        if (!cameIn) {
            status("PRESS START TO JOIN", said++ * 14);
        }
    }
    // (--net-arcade: the host playing while it waits; the wait, and the match's code to give, at the bottom)
    if (net && connecting && net->Arcade()) {
        if (net->Found()) {
            status("A CHALLENGER IS COMING", said++ * 14);
        } else {
            static const char *const kDots[] = {"   ", ".  ", ".. ", "..."};
            const std::string waiting = std::string("WAITING FOR A CHALLENGER") + kDots[(tick / 30) % 4];
            status(waiting.c_str(), said++ * 14);
            if (!net->code.empty()) {
                const std::string code = "MATCH CODE " + net->code + (net->Listed() ? "   IN THE LOBBY" : "");
                status(code.c_str(), said++ * 14);
                if (!net->Invited().empty()) {
                    std::string invited = InvitationLine(*net);
                    for (char &c : invited) {
                        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                    }
                    status(invited.c_str(), said++ * 14);
                }
            }
        }
    }
    // (what the camera's zoom was set to, for a few seconds: above that if both are said)
    if (*zoom.Label()) {
        status(zoom.Label(), said++ * 14);
    }
    if (shotTicks > 0) {
        status(shotSaid.c_str(), said * 14);
    }
    // (online: each player's name under their side's life bar, and under it a win streak of two or more)
    if (net && !connecting) {
        const recomp::text::Canvas canvas{pixels.data(), across, Video::kHeight};
        auto under = [&](int side, float y, const std::string &text, recomp::text::Color colour) {
            const float width = recomp::text::TextWidth(text.c_str(), 1);
            const float x = side == 0 ? 10.0f : across - 10.0f - width;
            canvas.Fill(x - 3, y - 2, width + 6, 12, {20, 22, 30, 220});
            canvas.Text(x, y, 1, 0, colour, text.c_str());
            if (panels) {
                panels->push_back({static_cast<int>(x) - 3, static_cast<int>(y) - 2, static_cast<int>(width) + 7, 12});
            }
        };
        for (int side = 0; side < 2; side++) {
            float y = 38.0f;
            const std::string name = net->Name(side);
            if (!FeatureOn("online_hide_names") && !name.empty()) {
                under(side, y, name, {255, 255, 255, 255});
                y += 12;
            }
            if (streak[side] >= 2) {
                under(side, y, "STREAK " + std::to_string(streak[side]), {255, 210, 80, 255});
            }
        }
        // The chat: its lines at the left, above the super gauges, the newest lowest, and the line being typed under them
        float y = Video::kHeight - 44.0f;
        auto line = [&](const std::string &text, recomp::text::Color colour) {
            const float width = recomp::text::TextWidth(text.c_str(), 1);
            canvas.Fill(7, y - 2, width + 6, 12, {20, 22, 30, 200});
            canvas.Text(10, y, 1, 0, colour, text.c_str());
            if (panels) {
                panels->push_back({7, static_cast<int>(y) - 2, static_cast<int>(width) + 7, 12});
            }
            y -= 12;
        };
        // (and under them, what the keys and buttons are: while a line is typed, and for a while as a match begins)
#ifdef __ANDROID__
        const char *open = "Chat: tap CHAT (or a gamepad's right stick click)", *keys = "Enter: send   CHAT or Back: cancel";
#else
        const char *open = "Chat: T, or a gamepad's right stick click", *keys = "Enter: send   Esc or right stick click: cancel";
#endif
        if (typing) {
            line(keys, {170, 170, 170, 255});
            line("Say: " + typed + ((tick / 20) % 2 ? "_" : " "), {255, 210, 80, 255});
        } else if (tick < chatHintUntil) {
            line(open, {170, 170, 170, 255});
        }
        for (auto it = chat.rbegin(); it != chat.rend(); ++it) {
            line(it->text, it->own ? recomp::text::Color{170, 200, 255, 255} : recomp::text::Color{255, 255, 255, 255});
        }
    }
    // (online: Esc's question, in the middle, over the game, which goes on; the port's own, not the readout, which the
    // game's sources set every frame. --net-arcade's wait: whether to stop hosting, the game stopped meanwhile)
    if (net && leaving) {
        const recomp::text::Canvas canvas{pixels.data(), across, Video::kHeight};
        const bool hosting = connecting;
        const char *question = hostedFromMenu ? "Leave the match?" : hosting ? "Stop hosting and quit?" : "Leave the match?";
        const char *confirm = hostedFromMenu ? "Confirm (A, Enter): play alone" : hosting ? "Confirm (A, Enter): quit" : "Confirm (A, Enter): leave";
        const struct {
            const char *text;
            recomp::text::Color colour;
        } lines[] = {{question, {255, 255, 255, 255}},
                     {confirm, {255, 130, 120, 255}},
                     {"Back (B, Backspace, Esc): stay", {255, 255, 255, 255}}};
        float y = Video::kHeight / 2.0f - 30;
        for (const auto &line : lines) {
            const float width = recomp::text::TextWidth(line.text, 1);
            const float x = (across - width) / 2;
            canvas.Fill(x - 6, y - 4, width + 12, 16, {20, 22, 30, 235});
            canvas.Text(x, y, 1, 0, line.colour, line.text);
            if (panels) {
                panels->push_back({static_cast<int>(x) - 6, static_cast<int>(y) - 4, static_cast<int>(width) + 13, 16});
            }
            y += 22;
        }
    }
}

bool Port::Windowed(std::vector<uint32_t> &pixels, Zoomed &zoomed) {
    const int across = ShownWidth();
    zoom.Follow(across, Video::kHeight);
    const Menu::Rate rate = menu.GameRate();
    if (rate.frames != saidRate.frames || rate.ticks != saidRate.ticks) {
        saidRate = rate;
        speedTicks = kSpeedTicks;
    } else if (speedTicks > 0) {
        speedTicks--;
    }
    if (shotTicks > 0) {
        shotTicks--;
    }
    zoomed.keep.clear();
    if (!zoom.View(across, Video::kHeight, zoomed.view)) {
        Shown(pixels);
        return false;
    }
    // (the game's own picture above, the one with the readout below)
    Shown(pixels, &zoomed.keep);
    pixels.insert(pixels.begin(), machine->Picture(), machine->Picture() + across * Video::kHeight);
    return true;
}

void Port::MenuPicture(std::vector<uint32_t> &pixels, uint32_t &width, uint32_t &height) const {
    std::vector<uint32_t> shown;
    Shown(shown);
    const uint32_t across = static_cast<uint32_t>(ShownWidth());
    height = Video::kHeight * 3;
    width = height * 4 / 3 * across / Video::kWidth;
    pixels.resize(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *from = &shown[static_cast<size_t>(y / 3) * across];
        uint32_t *to = &pixels[static_cast<size_t>(y) * width];
        for (uint32_t x = 0; x < width; x++) {
            to[x] = from[x * across / width];
        }
    }
}

std::string Port::PlayerFolder() const {
    const size_t slash = opt.eepromPath.find_last_of('/');
    return slash == std::string::npos ? "" : opt.eepromPath.substr(0, slash + 1);
}

std::string Port::StatePath(int slot) const {
    return PlayerFolder() + "state" + std::to_string(slot) + ".bin";
}

bool Port::Screenshot() {
    const std::string folder = PlayerFolder() + "screenshots/";
    SDL_CreateDirectory(folder.c_str());
    char stamp[32] = "";
    const std::time_t now = std::time(nullptr);
    if (const std::tm *local = std::localtime(&now)) {
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H.%M.%S", local);
    }
    // (another in the same second: a number after it)
    std::string name = std::string(GAME_ID " ") + stamp;
    for (int n = 2; std::filesystem::exists(std::filesystem::u8path(folder + name + ".png")); n++) {
        name = std::string(GAME_ID " ") + stamp + " " + std::to_string(n);
    }
    const int ticks = shotTicks;
    shotTicks = 0; // (not the last one's line in this one)
    std::vector<uint32_t> shown;
    Shown(shown);
    shotTicks = ticks;
    const std::string path = folder + name + ".png";
    if (!WritePng(path.c_str(), shown.data(), static_cast<uint32_t>(ShownWidth()), Video::kHeight)) {
        fprintf(stderr, "screenshot: could not write %s\n", path.c_str());
        shotSaid = "SCREENSHOT NOT SAVED";
        shotTicks = kSpeedTicks;
        return false;
    }
    fprintf(stderr, "screenshot: %s\n", path.c_str());
    shotSaid = "SCREENSHOT SAVED  screenshots/" + name + ".png";
    shotTicks = kSpeedTicks;
    return true;
}

bool Port::SaveState(const std::string &path, std::string &error) {
    std::vector<uint8_t> data;
    Archive ar(data, true);
    ar.Mark(0x43505332);
    uint32_t version = kStateVersion;
    ar.Pod(version);
    machine->Serialize(ar);
    if (TheGame().state) {
        TheGame().state(ar);
    }
    if (!WriteFile(path, data.data(), data.size())) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

bool Port::LoadState(const std::string &path, std::string &error) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) {
        error = "cannot read " + path;
        return false;
    }
    Archive ar(data, false);
    ar.Mark(0x43505332);
    uint32_t version = 0;
    ar.Pod(version);
    const bool known = version == kStateVersion;
    ar.version = version;
    if (ar.Ok() && known) {
        machine->Serialize(ar);
        if (TheGame().state) {
            TheGame().state(ar);
        }
        cps3recomp::StateLoaded();
    }
    if (!ar.Ok() || !known) {
        error = path + " is not a state this build wrote";
        machine->Reset();
        return false;
    }
    return true;
}

void Port::KeepState(std::vector<uint8_t> &data, bool inMemory) {
    data.clear();
    Archive ar(data, true);
    ar.inMemory = inMemory;
    machine->Serialize(ar);
    if (TheGame().state) {
        TheGame().state(ar);
    }
}

void Port::PutBackState(const std::vector<uint8_t> &data, bool inMemory) {
    std::vector<uint8_t> &from = const_cast<std::vector<uint8_t> &>(data); // (an Archive that reads does not change it)
    Archive ar(from, false);
    ar.inMemory = inMemory;
    machine->Serialize(ar);
    if (TheGame().state) {
        TheGame().state(ar);
    }
    cps3recomp::StateLoaded();
}

void Port::Step(const uint16_t *players) {
    memcpy(machine->players, players, sizeof(machine->players));
    if (TheGame().beforeFrame) {
        TheGame().beforeFrame(*machine);
    }
    machine->RunFrame();
    if (TheGame().frame) {
        TheGame().frame(*machine);
    }
}

// --rollback-test (port.h, RollbackTest): the frame run with the others' controls guessed where they have not
// come, then, as the others' controls come, the frames guessed wrong run again
void Port::RollbackFrame(const uint16_t *real) {
    RollbackTest &t = rollbackTest;
    using Clock = std::chrono::steady_clock;
    const auto seconds = [](Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count(); };
    const Clock::time_point began = Clock::now();
    const auto keep = [&](RollbackTest::Kept &k) {
        const Clock::time_point at = Clock::now();
        KeepState(k.state, true);
        k.sound = machine->audio.size();
        t.keepTime += seconds(at);
        t.keeps++;
        t.stateSize = k.state.size();
    };
    const auto step = [&](const Movie::Pads &used) {
        const Clock::time_point at = Clock::now();
        Step(used.data());
        t.stepTime += seconds(at);
        t.steps++;
    };
    const auto guessed = [&](const uint16_t *pads, uint64_t at) {
        Movie::Pads used;
        std::copy(pads, pads + Machine::kMaxPlayers, used.begin());
        if (at >= t.confirmed) {
            const Movie::Pads last = t.confirmed ? t.kept[(t.confirmed - 1) % RollbackTest::kKept].real : Movie::Pads{};
            std::copy(last.begin() + 1, last.end(), used.begin() + 1);
        }
        return used;
    };
    RollbackTest::Kept &now = t.kept[frame % RollbackTest::kKept];
    keep(now);
    std::copy(real, real + Machine::kMaxPlayers, now.real.begin());
    now.used = guessed(real, frame);
    step(now.used);
    now.hash = hashFile ? StateHash() : 0;
    // The others' controls come up to here (the lag changes now and then, and never takes back what has come)
    if (Random(t.random) % 20 == 0) {
        t.lag = static_cast<int>(Random(t.random) % (RollbackTest::kMostLag + 1));
    }
    if (opt.frames && frame + 1 >= opt.frames) {
        t.lag = 0; // (the run's last frame: everything has come, so that what it ends on can be compared)
    }
    const uint64_t upTo = std::max<uint64_t>(t.confirmed, frame + 1 >= static_cast<uint64_t>(t.lag) ? frame + 1 - t.lag : 0);
    uint64_t wrong = upTo;
    for (uint64_t at = t.confirmed; at < upTo; at++) {
        const RollbackTest::Kept &k = t.kept[at % RollbackTest::kKept];
        if (k.used != k.real) {
            wrong = at;
            break;
        }
    }
    // (the hash log has a frame once its controls are the real ones and it has been run with them)
    const auto logConfirmed = [&](uint64_t from) {
        for (uint64_t at = from; hashFile && at < upTo; at++) {
            fprintf(hashFile, "%llu %08x\n", static_cast<unsigned long long>(at + 1), t.kept[at % RollbackTest::kKept].hash);
        }
    };
    const uint64_t from = t.confirmed;
    t.confirmed = upTo;
    if (wrong == upTo) {
        logConfirmed(from);
        t.frameTimes.push_back(static_cast<float>(seconds(began)));
        return;
    }
    t.rollbacks++;
    t.rerun += frame + 1 - wrong;
    t.most = std::max<uint64_t>(t.most, frame + 1 - wrong);
    const Clock::time_point putting = Clock::now();
    PutBackState(t.kept[wrong % RollbackTest::kKept].state, true);
    t.putBackTime += seconds(putting);
    t.putBacks++;
    machine->audio.resize(t.kept[wrong % RollbackTest::kKept].sound);
    for (uint64_t at = wrong; at <= frame; at++) {
        RollbackTest::Kept &k = t.kept[at % RollbackTest::kKept];
        if (at != wrong) {
            keep(k);
        }
        k.used = guessed(k.real.data(), at);
        // (only the last frame's picture is seen; a hash log with the picture in it has them all drawn)
        machine->drawing = at == frame || (hashFile && !opt.hashNoPicture);
        step(k.used);
        k.hash = hashFile ? StateHash() : 0;
    }
    logConfirmed(from);
    t.frameTimes.push_back(static_cast<float>(seconds(began)));
}

// Run-ahead: after the frame, the frames after it are run with the controls as they are and the picture (and
// the readout) of the last is the one shown; then the machine and the game's sources are put back as they were
// after the frame, and its sound is the frame's own. So the game goes on exactly as without it, and the picture
// shows what a press does a frame or two sooner. Not done for a frame a spare machine ran in (runtime/spare.h).
void Port::RunAhead() {
    const int ahead = FeatureValue("run_ahead");
    // (nor while stopped, stepped a frame at a time or slowed: there the frame shown is the frame)
    if (ahead <= 0 || FastForward() || hurry || paused || !menu.FullSpeed() ||
        (TheGame().noRunAhead && TheGame().noRunAhead())) {
        return;
    }
    static std::vector<uint8_t> kept;
    KeepState(kept, true);
    const size_t sound = machine->audio.size();
    uint16_t held[Machine::kMaxPlayers];
    memcpy(held, machine->players, sizeof(held));
    SetRunningAhead(true);
    for (int n = 0; n < ahead; n++) {
        memcpy(machine->players, held, sizeof(held));
        if (TheGame().beforeFrame) {
            TheGame().beforeFrame(*machine);
        }
        machine->RunFrame();
        if (TheGame().frame) {
            TheGame().frame(*machine);
        }
    }
    PutBackState(kept, true);
    SetRunningAhead(false);
    machine->audio.resize(sound);
    memcpy(machine->players, held, sizeof(held));
}

// Online: a round's result, the frame it comes (once: a second's quiet after it before the next, so that a frame run
// again in a rollback is not counted twice), and a match won when a player has won its rounds
void Port::Streaks() {
    const int result = -1; // (no round results yet: the fight module is not here yet)
    if (result < 0) {
        return; // (not a fighting game)
    }
    if (result == 0) {
        if (++roundQuiet >= 60) {
            roundCounted = false;
        }
        return;
    }
    roundQuiet = 0;
    if (roundCounted) {
        return;
    }
    roundCounted = true;
    if (result == 3 || !cameIn || !otherIn) {
        return; // (a draw; or a fight against the computer, before both have come in)
    }
    const int winner = result - 1;
    if (++roundsWon[winner] >= 2) {
        streak[winner]++;
        streak[1 - winner] = 0;
        roundsWon[0] = roundsWon[1] = 0;
        fprintf(stderr, "online: player %d won the match (%d in a row)\n", winner + 1, streak[winner]);
    }
}

// Online play (netplay.h): the other side found and agreed with before the first frame; with a window, a step a tick
// (Tick), the wait shown on the screen, and without, here
// The chat's lines, each said for eight seconds, five at most (the oldest go first)
namespace {
    constexpr uint64_t kChatTicks = 8 * 60;
    constexpr size_t kChatLines = 5;
}

void Port::Say() {
    std::string line = typed;
    typing = false;
    typed.clear();
    if (!net || connecting) {
        return;
    }
    net->Say(line);
    const std::string name = net->Name(net->Local());
    if (line.find_first_not_of(' ') != std::string::npos) {
        chat.push_back({(name.empty() ? "You" : name) + ": " + line.substr(0, Netplay::kChatMost), tick + kChatTicks, true});
    }
}

void Port::HearChat() {
    const std::string name = net->Name(1 - net->Local());
    for (const std::string &line : net->TakeHeard()) {
        fprintf(stderr, "online: chat: %s: %s\n", name.empty() ? "(no name)" : name.c_str(), line.c_str());
        chat.push_back({(name.empty() ? "Them" : name) + ": " + line, tick + kChatTicks, false});
    }
    while (!chat.empty() && (chat.size() > kChatLines || chat.front().until <= tick)) {
        chat.pop_front();
    }
}

#ifdef _WIN32
static const char *const kExeSuffix = ".exe";
#else
static const char *const kExeSuffix = "";
#endif

// The launcher's player name ([online] name in its launcher.ini: the collection's, or the game's own launcher's),
// for a match hosted from the in-game menu
std::filesystem::path LauncherFolder() {
    const char *base = SDL_GetBasePath();
    if (!base) {
        return {};
    }
    std::error_code missing;
    const bool collection = !std::filesystem::exists(std::string(base) + GAME_ID "-launcher" + kExeSuffix, missing);
    const std::string name = recomp::FolderName(GAME_NAME);  // (as the launcher names it)
    std::filesystem::path folder = recomp::PortableFolder(base);
    if (!folder.empty()) {
        folder /= std::filesystem::u8path(name);
    } else if (char *pref = SDL_GetPrefPath("", name.c_str())) {
        folder = pref;
        SDL_free(pref);
    }
    return folder;
}

namespace {
    std::string LauncherName() {
        const std::filesystem::path folder = LauncherFolder();
        if (folder.empty()) {
            return "";
        }
        recomp::Ini prefs(folder / "launcher.ini");
        prefs.load();
        return prefs.get("online", "name");
    }
}

void Port::HostFromMenu() {
    menu.Close();
    if (waitingHost) {
        StopHosting(); // (the row again: Stop hosting)
        return;
    }
    if (net || opt.headless) {
        return;
    }
    Netplay::Settings settings = opt.net;
    settings.hosting = true;
    settings.solo = true;
    settings.arcade = false;
    settings.port = Netplay::kDefaultPort;
    settings.name = LauncherName();
    settings.listed = !settings.name.empty();
    settings.renewCode.clear();
    settings.renewSecret.clear();
    waitingHost = std::make_unique<Netplay>(settings);
    std::string error;
    if (!waitingHost->Begin(*this, error)) {
        waitingHost.reset();
        fprintf(stderr, "online: %s\n", error.c_str());
        menu.Open(true);
        menu.Note(error);
        return;
    }
    opt.net = settings;
    hostedFromMenu = true;
    menu.hosting = true;
    fprintf(stderr, "online: hosting from the menu; playing alone until someone joins\n");
}

void Port::PumpHost() {
    menu.hosting = waitingHost != nullptr;
    if (!waitingHost) {
        return;
    }
    Netplay *host = waitingHost.get();
    std::string error;
    if (host->Waiting(*this, error) < 0 && waitingHost.get() == host) {
        fprintf(stderr, "online: stopped hosting: %s\n", error.c_str());
        hostNote = "Stopped hosting: " + error;
        hostNoteUntil = tick + 300;
        StopHosting();
    }
}

void Port::MatchComing() {
    fprintf(stderr, "online: someone joined: the match starts from the board's reset\n");
    ownFeatures.clear();
    for (const Feature &feature : TheGame().features) {
        ownFeatures[feature.name] = FeatureSetting(feature.name);
    }
    menu.Close();
    menu.SetFullSpeed();
    paused = false;
    steps = 0;
    typing = false;
    EndSessionFeatures();
    machine->Reset();
    machine->audio.clear();
    zoom.Forbid();
    net = std::move(waitingHost); // (Netplay::Waiting goes on, as the match's)
    menu.hosting = false;
    connecting = true;
    cameIn = false;
    frame = 0;
}

void Port::StopHosting() {
    waitingHost.reset();
    hostedFromMenu = false;
    menu.hosting = false;
    fprintf(stderr, "online: stopped hosting\n");
}

void Port::BackToSolo(bool stillHosting) {
    Netplay::Settings settings = opt.net;
    if (net) {
        if (!net->code.empty()) {
            settings.renewCode = net->code.substr(0, 3) + net->code.substr(4);
            settings.renewSecret = net->Secret();
        }
        if (!connecting) {
            net->Leave();
            net->Report(true);
        }
    }
    net.reset(); // (the socket closed: the port is free for hosting again)
    connecting = leaving = typing = false;
    rejoin.clear();
    chat.clear();
    for (const auto &[name, value] : ownFeatures) {
        SetFeatureForNow(name.c_str(), value);
    }
    ownFeatures.clear();
    zoom.Forbid(false);
    hostedFromMenu = false;
    if (!stillHosting) {
        fprintf(stderr, "online: playing alone again\n");
        return;
    }
    settings.invite.clear();
    settings.inviteName.clear();
    waitingHost = std::make_unique<Netplay>(settings);
    std::string error;
    if (!waitingHost->Begin(*this, error)) {
        waitingHost.reset();
        fprintf(stderr, "online: %s\n", error.c_str());
        return;
    }
    opt.net = settings;
    hostedFromMenu = true;
    fprintf(stderr, "online: playing alone again, still hosting\n");
}

void Port::LeaveOnline() {
    leaving = false;
    if (hostedFromMenu) {
        BackToSolo(false);
    } else {
        quit = true;
    }
}

bool Port::StartOnline(std::string &error) {
    if (opt.rollbackTest || !opt.moviePlay.empty() || !opt.stateLoad.empty()) {
        error = "online play: not with --rollback-test, --movie or --state-load";
        return false;
    }
    net = std::make_unique<Netplay>(opt.net);
    zoom.Forbid(); // (both players see the whole picture, as the board shows it)
    if (opt.headless && !net->Arcade()) {
        if (!net->Connect(*this, error)) {
            net.reset();
            return false;
        }
        Matched();
        return true;
    }
    if (!net->Begin(*this, error)) {
        net.reset();
        return false;
    }
    connecting = true;
    return true;
}

void Port::Again(const std::string &why) {
    Netplay::Settings settings = opt.net;
    if (net && net->Hosting()) {
        // (the same code for them to come back with; and the game from the board's reset, which the other side is sent)
        if (!net->code.empty()) {
            settings.renewCode = net->code.substr(0, 3) + net->code.substr(4);
            settings.renewSecret = net->Secret();
        }
        // (the invited player is not invited again: they had the match, and know its code)
        settings.invite.clear();
        settings.inviteName.clear();
        opt.net = settings;
        EndSessionFeatures();
        machine->Reset();
        machine->audio.clear();
    }
    fprintf(stderr, "online: %s Waiting for the match again.\n", why.c_str());
    rejoin = why;
    cameIn = false;
    frame = 0;
    net.reset(); // (the socket closed: the host's port is free for the new one)
    net = std::make_unique<Netplay>(settings);
    connecting = true;
    std::string error;
    if (!net->Begin(*this, error)) {
        fprintf(stderr, "online: %s\n", error.c_str());
        net.reset();
        againAt = tick + 180;
    }
}

// --net-arcade: a frame of the host's own play while it waits, player 1's controls only (the other players' are the
// one who comes's); counted as the match's are, from the first frame of the play
void Port::ArcadeFrame() {
    for (int player = 1; player < Machine::kMaxPlayers; player++) {
        pad[player] = 0;
    }
    RunFrame();
}

void Port::Matched() {
    connecting = false;
    chatHintUntil = tick + kChatTicks;
    // (the host's player, playing while it waited, is in already; the joining side is told so in the welcome)
    cameIn = net->Arcade() && cameIn;
    otherIn = !net->Hosting() && net->HostIn();
    fprintf(stderr, "online: player 1 %s, player 2 %s\n", net->Name(0).empty() ? "(no name)" : net->Name(0).c_str(),
            net->Name(1).empty() ? "(no name)" : net->Name(1).c_str());
    if (net->Arcade()) {
        fprintf(stderr, "online: the match begins after %llu frames of the host's play\n", static_cast<unsigned long long>(frame));
    }
    streak[0] = streak[1] = roundsWon[0] = roundsWon[1] = 0;
    roundCounted = false;
    rejoin.clear();
    frame = 0; // (the match's frames count from its start, on both sides)
    // (the hashes compared leave out the picture: frames run again are not drawn)
    opt.hashNoPicture = true;
    if (net->Local() != 0) {
        opt.eepromPath.clear(); // (the board settings are the host's, for this match: the player's own are left as they are)
    }
}

// The port's rapid fire: a button the player holds is the game's pressed for two frames and let go for two, from
// the frame it goes down (so the first press is not held back). Movies keep what the player held.
void Port::RapidFire() {
    const int choice = FeatureValue("rapid_fire");
    const uint16_t buttons = choice == 1 ? kPunch1 : choice == 2 ? (kPunch1 | kPunch2 | kPunch3 | kKick1 | kKick2 | kKick3) : 0;
    for (int player = 0; player < Machine::kMaxPlayers; player++) {
        for (int bit = 0; bit < 16; bit++) {
            const uint16_t mask = static_cast<uint16_t>(1u << bit);
            if (!(buttons & mask) || !(machine->players[player] & mask)) {
                rapidHeld[player][bit] = 0;
                continue;
            }
            if ((rapidHeld[player][bit]++ / 2) % 2) {
                machine->players[player] &= static_cast<uint16_t>(~mask);
            }
        }
    }
}

void Port::RunFrame() {
    if (!opt.moviePlay.empty()) {
        auto it = playMovie.changes.find(frame);
        if (it != playMovie.changes.end()) {
            std::copy(it->second.begin(), it->second.end(), pad);
        }
    } else if (opt.autoPlay) {
        // Made-up play, to reach as much of the game as it can: coins and start buttons now and then, sticks
        // and buttons at random, held for random lengths of time. The same seed gives the same run.
        if (frame >= opt.autoFrom) {
            for (int p = 0; p < opt.autoPlayers; p++) {
                if (Random(autoState) % 8 == 0) {
                    uint16_t held = 0;
                    const uint64_t r = Random(autoState);
                    static const uint16_t kSticks[] = {0, kLeft, kRight, kUp, kDown, kLeft | kDown, kRight | kDown,
                                                       kLeft | kUp, kRight | kUp, 0, kRight, kLeft};
                    held |= kSticks[r % 12];
                    held |= static_cast<uint16_t>((r >> 8) & (r >> 16) & 0x3F0); // punches and kicks
                    if ((r >> 32) % 24 == 0) {
                        held |= kStart;
                    }
                    if ((r >> 40) % 96 == 0) {
                        held |= kCoin;
                    }
                    autoPad[p] = held;
                }
            }
            std::copy(std::begin(autoPad), std::end(autoPad), pad);
        }
    }
    if (!opt.movieRecord.empty() && !net && memcmp(pad, lastRecorded, sizeof(pad)) != 0) { // (online: Netplay records both players)
        std::copy(std::begin(pad), std::end(pad), recMovie.changes[frame].begin());
        memcpy(lastRecorded, pad, sizeof(pad));
    }
    // (tests: --menu FRAME:KEYS[:shot.png]: the menu opened at that frame and those buttons pressed in it:
    // u d l r, a for a button)
    if (!opt.menuTest.empty() && strtoull(opt.menuTest.c_str(), nullptr, 10) == frame) {
        const size_t first = opt.menuTest.find(':'), second = opt.menuTest.find(':', first + 1);
        const std::string keys =
            opt.menuTest.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
        menu.Open(true);
        for (const char key : keys) {
            MenuAct(menu.Buttons(key == 'u' ? kUp : key == 'd' ? kDown : key == 'l' ? kLeft : key == 'r' ? kRight : key == 'a' ? kPunch1 : 0));
        }
        if (second != std::string::npos && menu.IsOpen()) {
            std::vector<uint32_t> pixels;
            uint32_t width = 0, height = 0;
            MenuPicture(pixels, width, height);
            menu.Draw(pixels.data(), static_cast<int>(width), static_cast<int>(height));
            WritePng(opt.menuTest.substr(second + 1).c_str(), pixels.data(), width, height);
        }
        menu.Close();
    }
    memcpy(machine->players, pad, sizeof(pad));
    RapidFire();
    if (coinFrames > 0) {
        coinFrames--;
        machine->players[0] |= kCoin;
    }
    if (start2Frames > 0) {
        start2Frames--;
        machine->players[1] |= kStart;
    }
    machine->serviceButton = service && !net;
    machine->testSwitch = !net && (test || testFrames > 0 || (frame >= opt.testFrom && frame < opt.testTo));
    if (testFrames > 0) {
        testFrames--;
    }
    uint16_t pressed[Machine::kMaxPlayers]; // (for run-ahead's frames)
    memcpy(pressed, machine->players, sizeof(pressed));

    std::string path, error;
    if (!net && AtFrame(opt.stateLoad, frame, path) && !LoadState(path, error)) {
        fprintf(stderr, "%s\n", error.c_str());
    }
    if (net && connecting) {
        Step(pressed); // (--net-arcade: the host playing while it waits)
        cameIn = cameIn || (pressed[0] & kStart);
    } else if (net) {
        net->Frame(*this, pressed[0]); // (this side's player plays with player 1's controls)
        cameIn = cameIn || (pressed[0] & kStart);
        otherIn = otherIn || (machine->players[1 - net->Local()] & kStart);
        Streaks();
    } else if (opt.rollbackTest) {
        RollbackFrame(pressed);
    } else {
        Step(pressed);
    }
    frame++;
    // (for finding things out: --poke ADDR=BYTE,... every frame; --dump-ram FRAME:file)
    if (!opt.poke.empty()) {
        const char *at = opt.poke.c_str();
        while (*at) {
            char *end;
            const uint32_t address = static_cast<uint32_t>(strtoul(at, &end, 16));
            if (*end != '=') {
                break;
            }
            SetRam8(address, static_cast<uint8_t>(strtoul(end + 1, &end, 16)));
            at = *end == ',' ? end + 1 : end;
        }
    }
    if (!opt.trace.empty()) {
        // LOW-HIGH:FILE (hex addresses): those bytes of work RAM after every frame, one frame after another
        static FILE *file = nullptr;
        static uint32_t low = 0, high = 0;
        if (!file) {
            const size_t dash = opt.trace.find('-'), colon = opt.trace.find(':');
            low = static_cast<uint32_t>(strtoul(opt.trace.c_str(), nullptr, 16)) & 0x7FFFF;
            high = static_cast<uint32_t>(strtoul(opt.trace.c_str() + dash + 1, nullptr, 16)) & 0x7FFFF;
            file = fopen(opt.trace.c_str() + colon + 1, "wb");
        }
        if (file && high >= low) {
            fwrite(machine->ram() + low, 1, high - low + 1, file);
            fflush(file);
        }
    }
    for (const std::string &dump : opt.dumpRam) {
        if (!AtFrame(dump, frame, path)) {
            continue;
        }
        WriteFile(path, machine->ram(), 0x80000);
    }
    if (AtFrame(opt.stateSave, frame, path) && !SaveState(path, error)) {
        fprintf(stderr, "%s\n", error.c_str());
    }
    if (hashFile && !opt.rollbackTest && !net) { // (RollbackFrame and online play log a frame when it is confirmed)
        fprintf(hashFile, "%llu %08x\n", static_cast<unsigned long long>(frame), StateHash());
    }
    if (!net) {
        memcpy(machine->players, pressed, sizeof(pressed));
        RunAhead();
    }
    if (opt.shotEvery && frame % opt.shotEvery == 0) {
        char shot[512];
        snprintf(shot, sizeof(shot), "%s%06llu.png", opt.shotPrefix.c_str(), static_cast<unsigned long long>(frame));
        std::vector<uint32_t> shown;
        Shown(shown);
        WritePng(shot, shown.data(), ShownWidth(), Video::kHeight);
    }
}

void Port::Finish() {
    if (net && !connecting) {
        net->Leave();
        net->Report(true);
    }
    if (hashFile) {
        fclose(hashFile);
    }
    if (opt.rollbackTest) {
        const RollbackTest &t = rollbackTest;
        const auto ms = [](double total, uint64_t count) { return count ? 1000 * total / count : 0.0; };
        fprintf(stderr, "rollback test: %llu rollbacks, %llu frames run again, at most %llu at once\n",
                static_cast<unsigned long long>(t.rollbacks), static_cast<unsigned long long>(t.rerun),
                static_cast<unsigned long long>(t.most));
        std::vector<float> times = t.frameTimes;
        std::sort(times.begin(), times.end());
        const auto at = [&](double part) { return times.empty() ? 0.0 : 1000.0 * times[static_cast<size_t>(part * (times.size() - 1))]; };
        const size_t over = static_cast<size_t>(times.end() - std::upper_bound(times.begin(), times.end(), 1.0f / Machine::kFramesPerSecond));
        fprintf(stderr, "rollback test: a frame %.3f ms, keeping %.3f ms, putting back %.3f ms, state %zu bytes\n",
                ms(t.stepTime, t.steps), ms(t.keepTime, t.keeps), ms(t.putBackTime, t.putBacks), t.stateSize);
        fprintf(stderr, "rollback test: a frame with its rollback: half under %.2f ms, 99%% %.2f ms, 99.9%% %.2f ms, the worst %.2f ms; "
                        "%zu of %zu over a frame's time\n", at(0.5), at(0.99), at(0.999), at(1.0), over, times.size());
    }
    if (!opt.movieRecord.empty()) {
        recMovie.Save(opt.movieRecord);
    }
    if (!opt.eepromPath.empty()) {
        WriteFile(opt.eepromPath, machine->eeprom.words, Eeprom::kBytes);
    }
}

// The list --list-features writes (on Android the launcher asks the game's library for it: port/android.cpp)
std::string LauncherFeatureList() {
    RegisterGame();
    RegisterPortFeatures();
    std::vector<recomp::FeatureInfo> list = FeatureList();
    // (nor are the ones whose switch is on the launcher's first page: game.h, kFeatureFirstPage)
    list.erase(std::remove_if(list.begin(), list.end(), [](const recomp::FeatureInfo &f) { return f.flags & kFeatureFirstPage; }), list.end());
    return recomp::FeatureListText(list);
}

bool InCollection() {
#ifdef __ANDROID__
    return true; // (Android has only the collection's app)
#endif
    static const bool collection = [] {
        const char *base = SDL_GetBasePath();
        std::error_code missing;
        (void)base;
        (void)missing;
        return false; // (a game of its own: no collection yet)
    }();
    return collection;
}

} // namespace port

namespace {

void Usage() {
    fprintf(stderr,
            "%s\n"
            "  --rom PATH             the ROM set (%s): a folder with its files, or its zip\n"
            "  --rom2 PATH            (a game that reads another set too) that set: a folder or zip (else\n"
            "                         beside --rom, by the set's name)\n"
            "  --eeprom FILE          where the board's settings memory is kept\n"
            "  --cpu MODE             native (translated code), blocks (the interpreter run the same way),\n"
            "                         interp (the interpreter, an instruction at a time), check\n"
            "  --sound SIDE           driver (the sound program as ordinary code), z80 (the program itself),\n"
            "                         check (both, every write to the chip compared)\n"
            "  --sound-test SEED[:N]  every N frames (30) a command to the sound side: each song in turn, placed\n"
            "                         some made-up way, and the other commands (with --sound check)\n"
            "  --scale N              window size, times 224 lines (else the Display page's)\n"
            "  --zoom SIZE:PLAYER     the camera zoomed from the start: SIZE 1-4 (1.25x-2x), PLAYER 1 or 2 (3: both)\n"
            "  --fullscreen  --window (a window, after --fullscreen)  --mute  --unthrottled\n"
            "  --headless             no window, no sound, as fast as it goes\n"
            "  --frames N             stop after N frames\n"
            "  --shot-every N:PFX     write PFX<frame>.png every N frames\n"
            "  --hash-log FILE        a line per frame with a hash of the game's state\n"
            "  --hash-picture         ... of the picture only (to compare runs whose timing is not the same)\n"
            "  --hash-no-picture      ... of all but the picture (--rollback-test then draws only the frames shown)\n"
            "  --hash-no-sound        ... and without the sound side, as online play's (to compare a match's --record)\n"
            "  --hash-game            ... without the Z80 and its own memory (to compare --sound driver and z80)\n"
"  --renderer sdl|gpu     how the picture gets to the window: SDL's GPU API unless said (or not there)\n"
            "  --present-test F.png   (headless) the run's last picture drawn by the GPU path, no window, as the\n"
            "                         Display page has it (896x672 at 4:3)\n"
            "  --present-size WxH     ... in a window this big\n"
            "  --present-every N[:P]  (headless) every N frames the picture as the window draws it, zoom and all, to P000123.png\n"
                        "  --cpu-speed N          the 68000 N times as fast as the board's: the game is never short of time\n"
            "  --movie FILE           replay an input movie\n"
            "  --record FILE          record an input movie\n"
            "  --auto SEED[:FROM[:N]] made-up input (from frame FROM; for N players, 2 unless said)\n"
            "  --rollback-test SEED   each frame run as online play's rollback would: the other players' controls late\n"
            "                         and guessed, and the frames guessed wrong run again (--hash-log must not change)\n"
            "  --host [PORT]          online play: wait for the other player (on UDP port PORT, 7845 unless said);\n"
            "                         the host is player 1, and its board settings and play features are the match's\n"
            "  --join ADDRESS[:PORT]  online play: go to the host at ADDRESS (player 2), or CODE: the match the host's\n"
            "                         code names (the online service says where the host is)\n"
            "  --net-service URL      the online service that gives hosts codes (online/), or off\n"
            "  --net-list             (hosting) the match in the lobby, the Online card's list of open matches\n"
            "  --net-name NAME        ... under this name; and either side's: shown to the other player\n"
            "  --net-say [FRAME:]TEXT (tests) TEXT said in the chat at FRAME of the match (120 unless said)\n"
            "  --net-typing TEXT      (pictures) TEXT shown as a chat line being typed, all through the match\n"
            "  --net-arcade           (hosting) the game played alone until the other player comes, who then challenges\n"
            "  --net-invite TO:FROM   (hosting) the player TO in the launcher's waiting room invited, from FROM (ids;\n"
            "                         --net-name the inviter's name)\n"
            "  --net-invite-name NAME ... TO's name, as the wait says it\n"
            "  --net-delay N          (hosting) frames each player's controls are held back, 2 unless said (0-8)\n"
            "  --net-fake MS[:JIT[:LOSS]]  (tests) each packet sent MS ms late, give or take JIT, LOSS in 100 lost\n"
            "  --test-switch FROM:TO  the operator's test switch held from frame FROM to TO\n"
            "  --music-test FRAMES    each piece of the game's music in turn on the board, FRAMES each (with the test\n"
            "                         switch held, say), and how long each made a sound for (to tell the ones that end)\n"
            "  --record-entries FILE  add the places reached by computed jumps to FILE (for tools/recomp.py)\n"
            "  --state-save F:FILE  --state-load F:FILE   at frame F\n"
            "  --features A,B         switch optional features on (--list-features lists them; -NAME: off; given\n"
            "                         more than once, all of them in order)\n"
            "  --settings FILE        settings.ini with the player's feature choices\n"
            "  --multi MODE:CHOICES   played as one of the launcher's Multi game page's games (endless:scroll:1,\n"
            "                         endless:shuffle:60); exit code 3 when it is the next game's turn\n"
            "  --shader FILE          draw through that RetroArch shader preset (.slangp), whatever the Display page says\n"
            "  --soundtrack DIR       the custom soundtrack's folder (its songs.txt names the files)\n"
            "  --soundtrack-dump FILE (with --headless) write what the custom soundtrack plays to a WAV file\n"
            "  --poke A=B,...         write byte B at RAM address A after every frame (hex)\n"
            "  --dump-ram F:FILE      write work RAM after frame F (and FILE.video, .objects, .regs); more than once\n"
            "  --check-rewrites       run every rewritten routine and its original, and report differences\n"
            "  --no-rewrites          run the game's own routines where the sources have rewrites\n"
            "  --trace LOW-HIGH:FILE  those bytes of work RAM after every frame, appended to FILE\n"
            "  --watch LOW[-HIGH]     report the 68000's writes to these addresses (hex), with who made them\n"
            "  --keys TICK:KEY,...    keys pressed at those ticks of the screen: p (stop/go), o (a frame on),\n"
            "                         - and = (speed), n and h (a Multi game run's next game, fast forward on/off),\n"
            "                         k1.. (the game's command keys), cNAME (a command),\n"
            "                         fNAME=N (a feature set)\n"
            "  --stats                speed, at the end\n"
            "  --audio-dump FILE      write the sound to a WAV file\n"
            "  --dump-program FILE    write the deciphered 68000 program and stop\n",
            GAME_NAME, GAME_ROM_SET);
}

bool ParseArgs(int argc, char **argv, port::Options &o) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rom") {
            o.romPath = next();
        } else if (a == "--rom2") {
            o.extraRomPath = next();
        } else if (a == "--eeprom") {
            o.eepromPath = next();
        } else if (a == "--cpu") {
            o.cpu = next();
        } else if (a == "--music-test") {
            o.musicTest = std::max(1u, static_cast<unsigned>(strtoul(next(), nullptr, 10)));
        } else if (a == "--sound-test") {
            const std::string v = next();
            o.soundTest = strtoull(v.c_str(), nullptr, 10);
            if (const size_t colon = v.find(':'); colon != std::string::npos) {
                o.soundTestEvery = std::max(1u, static_cast<unsigned>(strtoul(v.c_str() + colon + 1, nullptr, 10)));
            }
        } else if (a == "--sound") {
            o.sound = next();
        } else if (a == "--scale") {
            o.scale = atoi(next());
        } else if (a == "--zoom") {
            const char *spec = next();
            o.zoom = atoi(spec);
            const char *colon = strchr(spec, ':');
            o.zoomPlayer = colon ? atoi(colon + 1) - 1 : 0;
        } else if (a == "--fullscreen") {
            o.fullscreen = true;
        } else if (a == "--window") {
            o.fullscreen = false; // (the Multi game page's, after what the game's own Play says)
        } else if (a == "--mute") {
            o.mute = true;
        } else if (a == "--unthrottled") {
            o.unthrottled = true;
        } else if (a == "--headless") {
            o.headless = true;
        } else if (a == "--frames") {
            o.frames = strtoull(next(), nullptr, 10);
        } else if (a == "--present-every") {
            const std::string v = next();
            const size_t colon = v.find(':');
            o.presentEvery = strtoull(v.c_str(), nullptr, 10);
            o.presentPrefix = colon == std::string::npos ? "present_" : v.substr(colon + 1);
        } else if (a == "--shot-every") {
            const std::string v = next();
            const size_t colon = v.find(':');
            o.shotEvery = strtoull(v.c_str(), nullptr, 10);
            o.shotPrefix = colon == std::string::npos ? "shot_" : v.substr(colon + 1);
        } else if (a == "--hash-log") {
            o.hashLog = next();
        } else if (a == "--hash-no-sound") {
            o.hashNoSound = true;
        } else if (a == "--hash-no-picture") {
            o.hashNoPicture = true;
        } else if (a == "--hash-game") {
            o.hashGame = true;
        } else if (a == "--hash-picture") {
            o.hashPicture = true;
        } else if (a == "--renderer") {
            o.renderer = next();
        } else if (a == "--present-test") {
            o.presentTest = next();
        } else if (a == "--present-size") {
            const std::string v = next();
            o.presentWidth = atoi(v.c_str());
            o.presentHeight = v.find('x') == std::string::npos ? 0 : atoi(v.c_str() + v.find('x') + 1);
        } else if (a == "--cpu-speed") {
            o.cpuSpeed = atoi(next());
        } else if (a == "--movie") {
            o.moviePlay = next();
        } else if (a == "--record") {
            o.movieRecord = next();
        } else if (a == "--auto") {
            const std::string v = next();
            const size_t colon = v.find(':');
            o.autoPlay = true;
            o.autoSeed = strtoull(v.c_str(), nullptr, 10);
            o.autoFrom = colon == std::string::npos ? 0 : strtoull(v.c_str() + colon + 1, nullptr, 10);
            const size_t second = colon == std::string::npos ? colon : v.find(':', colon + 1);
            o.autoPlayers = second == std::string::npos ? 2 : atoi(v.c_str() + second + 1);
        } else if (a == "--rollback-test") {
            o.rollbackTest = strtoull(next(), nullptr, 10) | 1;
        } else if (a == "--host") {
            o.online = o.net.hosting = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                o.net.port = atoi(next());
            }
        } else if (a == "--join") {
            o.online = true;
            o.net.address = next();
        } else if (a == "--net-list") {
            o.net.listed = true;
        } else if (a == "--net-arcade") {
            o.net.arcade = true;
        } else if (a == "--net-name") {
            o.net.name = next();
        } else if (a == "--net-say") {
            o.netSay = next();
            const size_t colon = o.netSay.find(':');
            if (colon != std::string::npos && colon > 0 && o.netSay.find_first_not_of("0123456789") == colon) {
                o.netSayAt = strtoull(o.netSay.c_str(), nullptr, 10);
                o.netSay.erase(0, colon + 1);
            }
        } else if (a == "--net-typing") {
            o.netTyping = next();
        } else if (a == "--net-invite") {
            // (two waiting room ids: 12 hex digits each)
            const std::string v = next();
            const size_t colon = v.find(':');
            const auto id = [](const std::string &t) { return t.size() == 12 && t.find_first_not_of("0123456789abcdef") == std::string::npos; };
            if (colon != std::string::npos && id(v.substr(0, colon)) && id(v.substr(colon + 1))) {
                o.net.invite = v.substr(0, colon);
                o.net.from = v.substr(colon + 1);
            }
        } else if (a == "--net-invite-name") {
            o.net.inviteName = next();
        } else if (a == "--net-service") {
            o.net.service = next();
        } else if (a == "--net-delay") {
            o.net.delay = atoi(next());
        } else if (a == "--net-fake") {
            const std::string v = next();
            const size_t colon = v.find(':'), second = colon == std::string::npos ? colon : v.find(':', colon + 1);
            o.net.fakeLag = atoi(v.c_str());
            o.net.fakeJitter = colon == std::string::npos ? 0 : atoi(v.c_str() + colon + 1);
            o.net.fakeLoss = second == std::string::npos ? 0 : atoi(v.c_str() + second + 1);
        } else if (a == "--test-switch") {
            const std::string v = next();
            const size_t colon = v.find(':');
            o.testFrom = strtoull(v.c_str(), nullptr, 10);
            o.testTo = colon == std::string::npos ? ~0ull : strtoull(v.c_str() + colon + 1, nullptr, 10);
        } else if (a == "--record-entries") {
            o.recordEntries = next();
        } else if (a == "--state-save") {
            o.stateSave = next();
        } else if (a == "--state-load") {
            o.stateLoad = next();
        } else if (a == "--stats") {
            o.stats = true;
        } else if (a == "--list-features") {
            // For the launcher: one optional feature per line (recomp-common/settings.h; the ones that last a
            // session, switched in the game's menu only, are not in it)
            fputs(port::LauncherFeatureList().c_str(), stdout);
            exit(0);
        } else if (a == "--settings") {
            o.settingsPath = next();
        } else if (a == "--shader") {
            o.shader = next();
        } else if (a == "--soundtrack") {
            o.soundtrack = next();
        } else if (a == "--soundtrack-dump") {
            o.soundtrackDump = next();
        } else if (a == "--multi") {
            SetMultiGame(next());
        } else if (a == "--features") {
            // (given more than once, all of them, in order: what the launcher says, then what follows it)
            const std::string more = next();
            o.features = o.features.empty() ? more : o.features + "," + more;
        } else if (a == "--watch") {
            // LOW[-HIGH] (hex): who writes there; needs the interpreter, so the run is done block by block
            const std::string v = next();
            const size_t dash = v.find('-');
            o.watchLow = static_cast<uint32_t>(strtoul(v.c_str(), nullptr, 16));
            o.watchHigh = dash == std::string::npos ? o.watchLow : static_cast<uint32_t>(strtoul(v.c_str() + dash + 1, nullptr, 16));
            o.cpu = "blocks";
        } else if (a == "--check-rewrites") {
            o.checkRewrites = true;
        } else if (a == "--no-rewrites") {
            o.noRewrites = true;
        } else if (a == "--trace") {
            o.trace = next();
        } else if (a == "--dump-ram") {
            o.dumpRam.push_back(next());
        } else if (a == "--poke") {
            o.poke = next();
        } else if (a == "--keys") {
            o.keys = next();
        } else if (a == "--menu") {
            o.menuTest = next();
        } else if (a == "--audio-dump") {
            o.audioDump = next();
        } else if (a == "--dump-program") {
            o.dumpProgram = next();
        } else {
            Usage();
            return false;
        }
    }
    return true;
}

// The game's program opened by itself, with nothing to say what to play (a double click on it rather than on
// the launcher): the launcher beside it is started instead, straight into the game if the player has chosen
// their ROM set in it before (its --play), or at its first page to choose one. The launcher is the game's own, or
// the collection's (cps2recomp-launcher: then at this game's page, or --play ID). True: it was started.
#ifndef __ANDROID__
bool StartLauncher() {
    const char *base = SDL_GetBasePath();
    if (!base) {
        return false;
    }
    const char *kExe = port::kExeSuffix;
    std::string launcher = std::string(base) + GAME_ID "-launcher" + kExe;
    std::error_code missing;
    bool collection = false;
    if (!std::filesystem::exists(launcher, missing)) {
        return false;
    }
    bool setUp = false;
    // (the launcher's own folder and file; the collection's keeps each game's file in a section of its own. In
    // portable mode the folder is in user/ beside the launcher, as the launcher has it: recomp-common/folders.h)
    const std::string name = recomp::FolderName(collection ? "CPS-2 Recomp" : GAME_NAME);  // (as the launcher names it)
    std::filesystem::path folder = recomp::PortableFolder(base);
    if (!folder.empty()) {
        folder /= std::filesystem::u8path(name);
    } else if (char *pref = SDL_GetPrefPath("", name.c_str())) {
        folder = pref;
        SDL_free(pref);
    }
    if (!folder.empty()) {
        recomp::Ini prefs(folder / "launcher.ini");
        prefs.load();
        setUp = !prefs.get(collection ? "game." GAME_ID : "game", "rom").empty();
    }
    const char *own[] = {launcher.c_str(), setUp ? "--play" : nullptr, nullptr};
    const char *theirs[] = {launcher.c_str(), setUp ? "--play" : "--page", GAME_ID, nullptr};
    SDL_Process *process = SDL_CreateProcess(collection ? theirs : own, false);
    if (!process) {
        return false;
    }
    SDL_DestroyProcess(process); // (only SDL's record of it: the launcher goes on)
    return true;
}
#endif

// A start that went wrong: said on the standard error, and, with a window wanted, in a message box too (a
// windowed program started by a double click has nowhere else to say it)
void SayFailed(const port::Options &opt, const std::string &error) {
    fprintf(stderr, "%s: %s\n", GAME_NAME, error.c_str());
    if (!opt.headless) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_NAME, error.c_str(), nullptr);
    }
}

} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    // (a windowed program has no console: started from one, its output goes there, unless it was sent elsewhere)
    if (!GetStdHandle(STD_ERROR_HANDLE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    recomp::UseSystemProxy();  // (Windows' proxy, for the curl that asks the online service: recomp-common/proxy.h)
#endif
#ifdef __ANDROID__
    port::StartOnAndroid();
#else
    if (argc == 1 && StartLauncher()) {
        return 0;
    }
#endif
    port::Port p;
    if (!ParseArgs(argc, argv, p.opt)) {
        return 2;
    }
    std::string error;
    if (!p.Init(error)) {
        SayFailed(p.opt, argc == 1 ? error + "\n\nStart the game with its launcher (" GAME_ID "-launcher), which passes it your ROM set." : error);
        return 1;
    }
    if (!p.opt.dumpProgram.empty()) {
        // (the deciphered program, big-endian as the CPU reads it: the cartridge's ROM, then FILE.program the SIMMs')
        auto dump = [](const std::string &path, const std::vector<uint32_t> &words) {
            if (FILE *f = fopen(path.c_str(), "wb")) {
                for (uint32_t w : words) {
                    const uint8_t b[4] = {static_cast<uint8_t>(w >> 24), static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w)};
                    fwrite(b, 1, 4, f);
                }
                fclose(f);
            }
        };
        dump(p.opt.dumpProgram, p.roms.bios);
        dump(p.opt.dumpProgram + ".program", p.roms.program);
        return 0;
    }
    if (p.opt.online && !p.StartOnline(error)) {
        SayFailed(p.opt, error);
        return 1;
    }
    if (p.opt.zoom > 0) {
        p.zoom.Set(p.opt.zoom, std::clamp(p.opt.zoomPlayer, 0, +port::Zoom::kBoth));
    }
    int result = 0;
    const auto begin = std::chrono::steady_clock::now();
    if (p.opt.headless) {
        std::vector<int16_t> sound;
        // A picture as the window draws it (--present-test, --present-every): through the GPU path, read back, written
        // to `path`; in a window --present-size big, or one the picture's shape at 3 times its lines
        auto present = [&](const std::string &path, const std::vector<uint32_t> &shown, const port::Zoomed *zoomed) {
            std::vector<uint32_t> drawn;
            std::string error;
            const int across = p.ShownWidth();
            const int outHeight = p.opt.presentHeight > 0 ? p.opt.presentHeight : 672;
            const int outWidth = p.opt.presentWidth > 0 ? p.opt.presentWidth
                                                        : static_cast<int>(std::lround(outHeight * port::PictureShape(across, Video::kHeight, port::DisplayShape(), 4.0f / 3.0f)));
            const SDL_FRect where = port::FitPicture(outWidth, outHeight, across, Video::kHeight, port::DisplayShape(), port::DisplayScaling());
            if (SDL_Init(SDL_INIT_VIDEO) &&
                port::Presenter::Render(shown.data(), across, Video::kHeight, outWidth, outHeight, where, port::DisplayScaling(), drawn, error, zoomed)) {
                WritePng(path.c_str(), drawn.data(), outWidth, outHeight);
                return true;
            }
            fprintf(stderr, "--present-test: %s\n", error.empty() ? SDL_GetError() : error.c_str());
            return false;
        };
        // (online: at the board's pace, as a window would be, unless --unthrottled)
        auto paced = std::chrono::steady_clock::now();
        while (!p.quit && (p.opt.frames == 0 || p.frame < p.opt.frames)) {
            const bool ran = p.Tick();
            if (p.net && !p.opt.unthrottled) {
                paced += std::chrono::nanoseconds(static_cast<int64_t>(1e9 / Machine::kFramesPerSecond));
                std::this_thread::sleep_until(paced);
            }
            if (!ran) {
                continue;
            }
            if (p.opt.presentEvery) {
                // (the window's picture made every frame, as the window would, so that the zoom eases as it does there)
                std::vector<uint32_t> shown;
                port::Zoomed zoomed;
                const bool isZoomed = p.Windowed(shown, zoomed);
                if (p.frame % p.opt.presentEvery == 0) {
                    char path[512];
                    snprintf(path, sizeof(path), "%s%06llu.png", p.opt.presentPrefix.c_str(), static_cast<unsigned long long>(p.frame));
                    present(path, shown, isZoomed ? &zoomed : nullptr);
                }
            }
            if (!p.opt.audioDump.empty()) {
                sound.insert(sound.end(), p.machine->audio.begin(), p.machine->audio.end());
            }
            p.machine->audio.clear();
        }
        if (!p.opt.presentTest.empty()) {
            // (the picture the run ended on, through the GPU path and read back: what a window would show)
            std::vector<uint32_t> shown;
            port::Zoomed zoomed;
            const bool isZoomed = p.Windowed(shown, zoomed);
            if (!present(p.opt.presentTest, shown, isZoomed ? &zoomed : nullptr)) {
                result = 1;
            }
        }
        if (!p.opt.audioDump.empty()) {
            if (FILE *f = fopen(p.opt.audioDump.c_str(), "wb")) {
                const uint32_t bytes = static_cast<uint32_t>(sound.size() * 2), rate = static_cast<uint32_t>(Machine::kSampleRate);
                const uint32_t header[11] = {0x46464952, 36 + bytes, 0x45564157, 0x20746D66, 16,      0x00020001,
                                             rate,       rate * 4,   0x00100004, 0x61746164, bytes};
                fwrite(header, 4, 11, f);
                fwrite(sound.data(), 2, sound.size(), f);
                fclose(f);
            }
        }
    } else {
        result = port::RunWindowed(p);
    }
    if (p.opt.stats) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        fprintf(stderr, "%llu frames in %.2f s: %.0f a second (cpu: %s)\n", static_cast<unsigned long long>(p.frame), seconds,
                p.frame / seconds, p.opt.cpu.c_str());
        if (p.tick != p.frame) { // (slowed, sped up or stopped: the screen's pictures, which were not the game's frames)
            fprintf(stderr, "in %llu ticks of the screen\n", static_cast<unsigned long long>(p.tick));
        }
    }
    p.Finish();
    if (!cps3recomp::Finish()) {
        return 4;
    }
    if (p.net && !p.net->ended.empty()) {
        // (why the match ended, if it did before the player left it)
        if (p.net->failed) {
            SayFailed(p.opt, p.net->ended);
            return 5;
        }
        fprintf(stderr, "online: %s\n", p.net->ended.c_str());
        if (!p.opt.headless) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, GAME_NAME, p.net->ended.c_str(), nullptr);
        }
    }
    return ProgramEnded() >= 0 ? ProgramEnded() : result;
}
