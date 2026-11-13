// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); its shape is cps2recomp's.
//
// What cps3recomp tells the launcher the recomp ports share (recomp-common/launcher/src/launcher.h): the words for a
// ROM set, the check of the player's (read, checked by checksum and its cipher's keys found, as the game does), what
// the game is started with, the buttons that can be chosen. A game's own launcher is told about that game through
// LAUNCHER_* definitions (from games/<id>/game.cfg); the collection's (LAUNCHER_COLLECTION) about every game, from
// the table games.h that tools/gamecfg.py --collection writes from them all.

#include "launcher.h"

#include "../../machine/romset.h"
#include "../../port/bindings.h"
#include "../../port/netplay.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// A game, as its game.cfg tells it (the collection's table, games.h, has them in this order: tools/gamecfg.py,
// COLLECTION_KEYS)
struct GameInfo {
    const char *id, *name, *rom_set, *rom_parent, *rom_name;
    // The set's files (machine/romset.h): the cartridge's program ROM, the SIMMs, and whether the SIMMs' program is
    // read through the cipher ("0": not)
    const char *bios, *simm1, *simm2, *simm3, *simm4, *simm5, *simm6, *program_enciphered;
    // What a launcher of the game's own says of the other sets it uses beside its own (game.cfg's
    // launcher_extra_sets: the definitive version's stages, games/sfiii3; empty: none)
    const char *extra_sets_words;
    // Other games whose sets it can use beside its own (game.cfg's guests: their ids; empty: none). In a collection,
    // the set chosen for each is given to it (--guest ID=PATH); else the game looks for it beside its own. None is
    // needed to play.
    const char *guests;
    // Features whose switch is on the first page, under "Full screen" (game.cfg's `display`): name|Title|What it
    // says, with || between two of them (the game leaves these out of the list it gives for the Features page:
    // runtime/game.h, kFeatureFirstPage)
    const char *display;
    // The game's name on the collection launcher's pages, when it is not `name` (game.cfg's launcher_name; empty:
    // `name`). Its folder stays `name`'s: a player's settings and saves from before are still its own.
    const char *launcher_name;
};

// (the words made for each game, kept for as long as the launcher runs: the descriptions point into them)
const char* keep(std::string text) {
    static std::deque<std::string> kept;
    kept.push_back(std::move(text));
    return kept.back().c_str();
}

#ifdef LAUNCHER_COLLECTION
const GameInfo* game_of_id(const std::string& id);
// The ids in a list of them (game.cfg's guests)
std::vector<std::string> ids_in(const char* list) {
    std::vector<std::string> ids;
    std::string id;
    for (const char* p = list ? list : "";; p++) {
        if (*p == ' ' || *p == '\0') {
            if (!id.empty()) {
                ids.push_back(id);
            }
            id.clear();
            if (!*p) {
                break;
            }
        } else {
            id += *p;
        }
    }
    return ids;
}
#endif

struct FeatureSwitch {
    std::string name, title, detail;
};
std::vector<FeatureSwitch> feature_switches(const std::string& all) {
    std::vector<FeatureSwitch> list;
    for (size_t at = 0; at < all.size();) {
        const size_t end = std::min(all.find("||", at), all.size());
        const std::string one = all.substr(at, end - at);
        const size_t first = one.find('|'), second = first == std::string::npos ? first : one.find('|', first + 1);
        auto trim = [](std::string text) {
            text.erase(0, text.find_first_not_of(' '));
            text.erase(text.find_last_not_of(' ') + 1);
            return text;
        };
        if (second != std::string::npos && !trim(one.substr(0, first)).empty()) {
            list.push_back({trim(one.substr(0, first)), trim(one.substr(first + 1, second - first - 1)), trim(one.substr(second + 1))});
        }
        at = end + 2;
    }
    return list;
}

cps3recomp::RomList rom_list(const GameInfo& game) {
    cps3recomp::RomList list;
    list.set = game.rom_set;
    list.parent = game.rom_parent;
    list.bios = cps3recomp::ParseRomFiles(game.bios);
    const char* simms[7] = {"", game.simm1, game.simm2, game.simm3, game.simm4, game.simm5, game.simm6};
    for (int s = 1; s <= 6; s++) {
        list.simm[s] = cps3recomp::ParseRomFiles(simms[s]);
    }
    list.programEnciphered = std::string(game.program_enciphered) != "0";
    return list;
}

// A ROM set: its zip (with its parent's zip beside it, if the files are kept split) or a folder of its files.
// Every file is checked against the checksums the port was made from, and the program's cipher is worked out, as
// the game itself does when it starts. The launcher only ever reads it: never copies, moves or changes it.
launcher::FileCheck check_rom_set(const GameInfo& game, const std::string& path) {
    launcher::FileCheck check;
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) {
        check.message = "It isn't there any more.";
        return check;
    }
    cps3recomp::RomSet set;
    std::string message;
    if (cps3recomp::LoadRomSet(path, rom_list(game), set, message)) {
        check.result = launcher::FileCheck::Result::Good;
        check.message = game.rom_name;
    } else {
        check.message = "Not usable: " + message + ". This port needs the set " + game.rom_set + " (" + game.rom_name +
                        "). Other versions of the game will not work.";
    }
    return check;
}

// Where in a folder of ROM sets this game's may be: its zip or a folder of its files, by the set's name (and its
// parent's)
std::vector<std::filesystem::path> rom_set_candidates(const GameInfo& game, const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> list;
    for (const std::string name : {game.rom_set, game.rom_parent}) {
        if (!name.empty()) {
            list.push_back(folder / (name + ".zip"));
            list.push_back(folder / name);
        }
    }
    return list;
}

// A set in a "rom" folder beside the launcher or above it (an unpacked release with the sets put in rom/, or a build
// inside the source tree): the set's zip or a folder of its files there, or rom/ itself
std::string rom_set_nearby(const GameInfo& game, const std::filesystem::path& program_folder) {
    std::filesystem::path folder = program_folder;
    std::error_code error;
    for (int up = 0; up < 8 && !folder.empty(); up++, folder = folder.parent_path()) {
        const std::filesystem::path rom = folder / "rom";
        if (std::filesystem::is_directory(rom, error)) {
            std::vector<std::filesystem::path> tries = rom_set_candidates(game, rom);
            tries.push_back(rom);
            for (const std::filesystem::path& one : tries) {
                if (std::filesystem::exists(one, error) && check_rom_set(game, one.u8string()).usable()) {
                    return one.u8string();
                }
            }
        }
        if (folder == folder.parent_path()) {
            break;
        }
    }
    return {};
}

// What a launcher is told about one game
launcher::Description describe_game(const GameInfo& game) {
    launcher::Description d;
    d.original = "in the arcade";
    d.file_heading = "ROM SET";
#ifdef __ANDROID__
    // (Android: the set is copied into the app's folder; the screen's controls have a Menu button, and Back opens it)
    std::string intro = std::string("Choose your own copy of the game: the arcade ROM set ") + game.rom_set + " (" + game.rom_name +
                        "). Only this version works. It is copied into the app's folder.";
#else
    std::string intro = std::string("Choose your own copy of the game: the arcade ROM set ") + game.rom_set + " (" + game.rom_name +
                        "). Only this version works. It stays where it is: the launcher only reads it.";
#endif
#ifdef LAUNCHER_COLLECTION
    // (a game that uses other games' sets: the ones chosen on those games' pages are given to it)
    {
        std::string others;
        const std::vector<std::string> ids = ids_in(game.guests);
        for (size_t n = 0; n < ids.size(); n++) {
            if (const GameInfo* other = game_of_id(ids[n])) {
                others += std::string(others.empty() ? "" : n + 1 == ids.size() ? " or " : ", ") + other->name + "'s (" + other->rom_set + ")";
            }
        }
        if (!others.empty()) {
            intro += "\n\nWith " + others + " ROM set chosen on its own page too (or beside this one), it can show that game's stages: the "
                     "menu's Stage.";
        }
    }
#else
    intro += game.extra_sets_words;
#endif
#ifdef __ANDROID__
    intro += "\n\nWhile playing, Back or the screen's Menu button, or the left stick's click (or Start and Select together) on a gamepad, "
             "opens the in-game menu: save states, speed, and every feature.";
    d.file_intro = keep(intro);
    d.file_empty = keep(std::string("None chosen. Choose ") + game.rom_set + ".zip.");
#else
    intro += "\n\nWhile playing, Esc or F1 on the keyboard, or the left stick's click (or Start and Select together) on a gamepad, opens the "
             "in-game menu: save states, speed, and every feature.";
    d.file_intro = keep(intro);
    d.file_empty = keep(std::string("None chosen. Drop ") + game.rom_set + ".zip or its folder here, or choose the zip.");
#endif
    d.file_not_set_up = "The game isn't set up yet. Open the launcher (without --play) once to choose your ROM set.";
    d.file_filter = {"ROM set (zip)", "zip"};
    d.file_key = "rom";
    d.check = [game](const std::string& path) { return check_rom_set(game, path); };
    d.check_on_thread = true; // (reading 80 MB and finding the cipher's keys takes a moment)
    d.find_nearby = [game](const std::filesystem::path& folder) { return rom_set_nearby(game, folder); };
    d.candidates = [game](const std::filesystem::path& folder) { return rom_set_candidates(game, folder); };
    const std::vector<FeatureSwitch> switches = feature_switches(game.display);
    const std::string guests = game.guests ? game.guests : "";
    d.arguments = [switches, guests](const launcher::Launch& launch) {
        std::vector<std::string> args = {"--rom", launch.file, "--eeprom", (launch.folder / "saves" / "eeprom.bin").string(),
                                         "--settings", (launch.folder / "settings.ini").string()};
#ifdef LAUNCHER_COLLECTION
        for (const std::string& id : ids_in(guests.c_str())) {
            const std::string file = launch.file_of && game_of_id(id) ? launch.file_of(id.c_str()) : "";
            if (!file.empty()) {
                args.push_back("--guest");
                args.push_back(id + "=" + file);
            }
        }
#else
        (void)guests;
#endif
        if (launch.on("fullscreen")) {
            args.push_back("--fullscreen");
        }
        // (each first-page feature on or off as its switch is: said every time, so that the switch decides)
        std::string features;
        for (const FeatureSwitch& one : switches) {
            features += (features.empty() ? "" : ",") + std::string(launch.on(one.name.c_str()) ? "" : "-") + one.name;
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
    d.online.lobby = keep(std::string(port::Netplay::kService) + "/v1/lobby/" + game.id + "?protocol=" + std::to_string(port::Netplay::kLobbyProtocol));
    d.online.list_argument = "--net-list";
    d.online.name_argument = "--net-name";
    d.online.arcade_argument = "--net-arcade";
    d.online.invite_argument = "--net-invite";
    d.online.invite_name_argument = "--net-invite-name";
#ifndef LAUNCHER_COLLECTION
    // (the waiting room: the players whose Online page is open, to invite to a match; a collection's is its own)
    static const std::string room = std::string(port::Netplay::kService) + "/v1/room";
    d.online.room = room.c_str();
    d.online.room_protocol = port::Netplay::kLobbyProtocol;
    d.online.game = game.id;
    d.online_page = true; // (hosting, joining, the lobby and the waiting room on a page of their own, after Game)
#endif
    d.display = {{"fullscreen", "Full screen", "F11 switches while playing.", false, nullptr}};  // (a window unless asked)
    for (const FeatureSwitch& one : switches) {
        d.display.push_back({keep(one.name), keep(one.title), keep(one.detail), false, nullptr});
    }
    d.bindings = port::MakeBoundTable(2, "", false).table;
    d.controls_heading = "WHILE PLAYING";
    d.controls = {{"Keys", "Esc, F1, the left stick's click, or Start and Select together: the in-game menu (save states, coin, quit)   "
                           "P / O: stop, one frame on   - / =: speed   F5 / F9: save and load a state   F6 / F7: slot   F2: operator's "
                           "test switch   F11: full screen. On a gamepad: right stick's click stops the game and starts it again, left "
                           "trigger goes on a frame while it is stopped. (A key or button given to the game above does only that.)"}};
    return d;
}

}  // namespace

#ifdef LAUNCHER_COLLECTION

#include "games.h"

namespace {
// The collection's game of this id
const GameInfo* game_of_id(const std::string& id) {
    for (const GameInfo& game : kGames) {
        if (id == game.id) {
            return &game;
        }
    }
    return nullptr;
}
}  // namespace

const launcher::Collection& launcher::describe_collection() {
    static const Collection about = [] {
        Collection c;
        c.subtitle = "Street Fighter III, the three arcade games, native";
#ifdef __ANDROID__
        // (Android: the sets are chosen with the system's picker and copied into the app's folder: recomp-common's
        // launcher, App::choose_folder)
        c.intro = "Each game needs your own copy of it: its arcade ROM set. Add the sets from your phone or a drive: they are copied "
                  "into the app's folder.\n\n"
                  "While playing, Back or the screen's Menu button, or the left stick's click (or Start and Select together) on a gamepad, "
                  "opens the in-game menu.";
        c.find_title = "Add your sets";
        c.find_detail = "Choose the ROM sets, as many as you like at once, by the set's name (sfiii3nr1.zip, say). Each goes to its game.";
#else
        c.intro = "Each game needs your own copy of it: its arcade ROM set. Show the launcher the folder you keep them in, or drop a set "
                  "(or the folder) on this window. The sets stay where they are: the launcher only reads them.\n\n"
                  "While playing, Esc or F1 on the keyboard, or the left stick's click (or Start and Select together) on a gamepad, "
                  "opens the in-game menu.";
        c.find_title = "Look in a folder";
        c.find_detail = "Finds each game's ROM set in a folder of them, by the set's name (sfiii3nr1.zip, say).";
#endif
        for (const GameInfo& game : kGames) {
            // (each game's folder is named by its name, as its own launcher's was: what a player had before the
            // collection, its settings, saves and the ROM set chosen, is still its own)
            c.games.push_back({game.id, *game.launcher_name ? game.launcher_name : game.name, game.id, "", describe_game(game), nullptr, game.name});
        }
        // (the keys and buttons every game shares: the three have the same six buttons)
        c.bindings = port::MakeBoundTable(2, "", true).table;
        // (online play's lobby, every game's: Netplay::kService, kLobbyProtocol, so this collection's own)
        static const std::string lobby = std::string(port::Netplay::kService) + "/v1/lobby?protocol=" + std::to_string(port::Netplay::kLobbyProtocol);
        c.lobby = lobby.c_str();
        static const std::string lookup = std::string(port::Netplay::kService) + "/v1/code/";
        c.code_lookup = lookup.c_str();
        // (the waiting room: the players whose Online page is open, to invite to a match)
        static const std::string room = std::string(port::Netplay::kService) + "/v1/room";
        c.room = room.c_str();
        c.room_protocol = port::Netplay::kLobbyProtocol;
        c.file_name = "ROM set";
        c.online_features = "Online"; // (the ping and the input delay on the screen: port/main.cpp)
        c.online_intro = "Play someone on another machine: join a match a player is hosting, or host one in any of your games for others to "
                         "join, or invite someone from the waiting room. A game's own page has its Online card too, for a match with a code "
                         "only the people you give it to know.";
        return c;
    }();
    return about;
}

#else

// (a game whose sources use other sets beside its own says so: the definitive version's stages, games/sfiii3)
#ifndef LAUNCHER_EXTRA_SETS_WORDS
#define LAUNCHER_EXTRA_SETS_WORDS ""
#endif
#ifndef LAUNCHER_DISPLAY_SWITCHES
#define LAUNCHER_DISPLAY_SWITCHES ""
#endif

const launcher::Description& launcher::describe() {
    static const Description about = describe_game({LAUNCHER_GAME_EXE, LAUNCHER_GAME_NAME, LAUNCHER_ROM_SET, LAUNCHER_ROM_PARENT, LAUNCHER_ROM_NAME,
                                                    LAUNCHER_ROM_BIOS, LAUNCHER_ROM_SIMM1, LAUNCHER_ROM_SIMM2, LAUNCHER_ROM_SIMM3, LAUNCHER_ROM_SIMM4,
                                                    LAUNCHER_ROM_SIMM5, LAUNCHER_ROM_SIMM6, LAUNCHER_PROGRAM_ENCIPHERED, LAUNCHER_EXTRA_SETS_WORDS, "",
                                                    LAUNCHER_DISPLAY_SWITCHES, ""});
    return about;
}

#endif
