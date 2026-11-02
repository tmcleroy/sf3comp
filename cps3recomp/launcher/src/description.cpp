// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); its shape is cps2recomp's.
//
// What cps3recomp tells the launcher the recomp ports share (recomp-common/launcher/src/launcher.h): the words for a
// ROM set, the check of the player's (read, checked by checksum and its cipher's keys found, as the game does), what
// the game is started with, the buttons that can be chosen. The game comes through LAUNCHER_* definitions (from
// games/<id>/game.cfg).

#include "launcher.h"

#include "../../machine/romset.h"
#include "../../port/bindings.h"
#include "../../port/netplay.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// (a game whose sources use other sets beside its own says so: the definitive version's stages, games/sfiii3)
#ifndef LAUNCHER_EXTRA_SETS_WORDS
#define LAUNCHER_EXTRA_SETS_WORDS ""
#endif

namespace {

cps3recomp::RomList rom_list() {
    cps3recomp::RomList list;
    list.set = LAUNCHER_ROM_SET;
    list.parent = LAUNCHER_ROM_PARENT;
    list.bios = cps3recomp::ParseRomFiles(LAUNCHER_ROM_BIOS);
    const char* simms[7] = {"", LAUNCHER_ROM_SIMM1, LAUNCHER_ROM_SIMM2, LAUNCHER_ROM_SIMM3, LAUNCHER_ROM_SIMM4, LAUNCHER_ROM_SIMM5, LAUNCHER_ROM_SIMM6};
    for (int s = 1; s <= 6; s++) {
        list.simm[s] = cps3recomp::ParseRomFiles(simms[s]);
    }
    list.programEnciphered = std::string(LAUNCHER_PROGRAM_ENCIPHERED) != "0";
    return list;
}

// A ROM set: its zip (with its parent's zip beside it, if the files are kept split) or a folder of its files.
// Every file is checked against the checksums the port was made from, and the program's cipher is worked out, as
// the game itself does when it starts. The launcher only ever reads it: never copies, moves or changes it.
launcher::FileCheck check_rom_set(const std::string& path) {
    launcher::FileCheck check;
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) {
        check.message = "It isn't there any more.";
        return check;
    }
    cps3recomp::RomSet set;
    std::string message;
    if (cps3recomp::LoadRomSet(path, rom_list(), set, message)) {
        check.result = launcher::FileCheck::Result::Good;
        check.message = LAUNCHER_ROM_NAME;
    } else {
        check.message = "Not usable: " + message + ". This port needs the set " LAUNCHER_ROM_SET " (" LAUNCHER_ROM_NAME
                        "). Other versions of the game will not work.";
    }
    return check;
}

// Where in a folder of ROM sets this game's may be: its zip or a folder of its files, by the set's name (and its
// parent's)
std::vector<std::filesystem::path> rom_set_candidates(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> list;
    for (const std::string name : {LAUNCHER_ROM_SET, LAUNCHER_ROM_PARENT}) {
        if (!name.empty()) {
            list.push_back(folder / (name + ".zip"));
            list.push_back(folder / name);
        }
    }
    return list;
}

}  // namespace

const launcher::Description& launcher::describe() {
    static const Description about = [] {
        Description d;
        d.original = "in the arcade";
        d.file_heading = "ROM SET";
        d.file_intro = "Choose your own copy of the game: the arcade ROM set " LAUNCHER_ROM_SET " (" LAUNCHER_ROM_NAME
                       "). Only this version works. It stays where it is: the launcher only reads it." LAUNCHER_EXTRA_SETS_WORDS "\n\nWhile playing, Esc or F1 on "
                       "the keyboard, or the left stick's click (or Start and Select together) on a gamepad, opens the in-game menu: "
                       "save states, speed, and every feature.";
        d.file_empty = "None chosen. Drop " LAUNCHER_ROM_SET ".zip or its folder here, or choose the zip.";
        d.file_not_set_up = "The game isn't set up yet. Open the launcher (without --play) once to choose your ROM set.";
        d.file_filter = {"ROM set (zip)", "zip"};
        d.file_key = "rom";
        d.check = check_rom_set;
        d.check_on_thread = true; // (reading 80 MB and finding the cipher's keys takes a moment)
        d.candidates = rom_set_candidates;
        d.arguments = [](const launcher::Launch& launch) {
            std::vector<std::string> args = {"--rom", launch.file, "--eeprom", (launch.folder / "saves" / "eeprom.bin").string(),
                                             "--settings", (launch.folder / "settings.ini").string()};
            if (launch.on("fullscreen")) {
                args.push_back("--fullscreen");
            }
            // (the first page's switches of the game's features: game.cfg's `display`, said at every start)
            std::string features;
            for (const launcher::Toggle &one : launcher::describe().display) {
                if (std::string(one.key) != "fullscreen") {
                    features += (features.empty() ? "" : ",") + std::string(launch.on(one.key) ? "" : "-") + one.key;
                }
            }
            if (!features.empty()) {
                args.push_back("--features");
                args.push_back(features);
            }
            return args;
        };
        d.saves_folder = true;
        // (the custom soundtrack's files: port/soundtrack.h)
        d.folders = {{"soundtrack", "Custom soundtrack",
                      "Your own music, played in place of the board's music while the Custom soundtrack feature is on: a folder of music "
                      "plays as a playlist, or files named as the songs.txt the game writes there replace those songs. None chosen: the "
                      "soundtrack folder in the game's own folder.",
                      "--soundtrack"}};
        d.online = {"--host", "--join", 7845, true,
                    "Two players, each on their own machine with the game, anywhere: one hosts and gives the other the code their game "
                    "shows; the other joins with it. Each presses Start to come in. The match has the host's settings and the "
                    "features that change play."};
        static const std::string lobby =
            std::string(port::Netplay::kService) + "/v1/lobby/" LAUNCHER_GAME_EXE "?protocol=" + std::to_string(port::Netplay::kProtocol);
        d.online.lobby = lobby.c_str();
        d.online.list_argument = "--net-list";
        d.online.name_argument = "--net-name";
        d.online.arcade_argument = "--net-arcade";
        d.online.invite_argument = "--net-invite";
        d.online.invite_name_argument = "--net-invite-name";
        d.display = {{"fullscreen", "Full screen", "F11 switches while playing.", false, nullptr},  // (a window unless asked)
                     {"widescreen", "Widescreen", "16:9: more of the stage at each side (the corners come a little sooner).", false, nullptr}};
        d.bindings = port::MakeBoundTable(2, "", false).table;
        d.controls_heading = "WHILE PLAYING";
        d.controls = {{"Keys", "Esc, F1, the left stick's click, or Start and Select together: the in-game menu (save states, coin, quit)   "
                               "P / O: stop, one frame on   - / =: speed   F5 / F9: save and load a state   F6 / F7: slot   F2: operator's "
                               "test switch   F11: full screen. On a gamepad: right stick's click stops the game and starts it again, left "
                               "trigger goes on a frame while it is stopped. (A key or button given to the game above does only that.)"}};
        return d;
    }();
    return about;
}
