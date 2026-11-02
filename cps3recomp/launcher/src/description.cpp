// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// What cps2recomp tells the launcher the recomp ports share (recomp-common/launcher/src/launcher.h): the
// words for a ROM set, the check of the player's, what the game is started with, the buttons that can be
// chosen. A game's own launcher is told about that game through LAUNCHER_* definitions (from
// games/<id>/game.cfg); the collection's (LAUNCHER_COLLECTION) about every game, from the table games.h
// that tools/gamecfg.py --collection writes from them all.

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

// A game, as its game.cfg tells it
struct GameInfo {
    const char *id, *name, *rom_set, *rom_parent, *rom_name, *program, *graphics, *sound_program, *samples, *key;
    // Features whose switch is on the first page, under "Full screen" (game.cfg's `display`): name|Title|What it
    // says, with || between two of them (the game leaves these out of the list it gives for the Features page:
    // runtime/game.h, kFeatureFirstPage)
    const char* display;
    // How many players (game.cfg's `players`; "" is two), and the game's names for its buttons ("Attack|Jump",
    // port/bindings.h; "": a fighting game's six)
    const char *players, *buttons;
    // The board (game.cfg's `board` and `cps_b`: "" for a CPS-2; machine/romset.h)
    const char *board, *cps_b;
    // Another set the game reads too (game.cfg's extra_set; empty: none). In a collection, the set chosen for the
    // game that is that set's is given to it (--rom2); else the game looks for it beside its own
    const char* extra_set;
    // The Multi game page's modes it takes part in (game.cfg's multi: "endless")
    const char* multi;
    // Other sets of the game it plays as well (game.cfg's other_set: other regions of the same program;
    // machine/romset.h)
    const char* other_set;
    // The series the launcher puts it under, with the other games of it (game.cfg's series; "": none)
    const char* series;
};
#ifdef LAUNCHER_COLLECTION
const GameInfo* game_of_set(const char* set);
#endif

#ifdef LAUNCHER_COLLECTION
// (the table every game shares in the collection: below)
const port::BoundTable& shared_table();
#endif

// (the words made for each game, kept for as long as the launcher runs: the descriptions point into them)
const char* keep(std::string text) {
    static std::deque<std::string> kept;
    kept.push_back(std::move(text));
    return kept.back().c_str();
}

// The sets the game plays, by name: its own ("mvsc (Marvel Vs. Capcom ... (Europe 980123))"), then its others' ("or
// another region's: mvscu, mvscj or mvsca")
std::string sets_named(const GameInfo& game) {
    std::string text = std::string(game.rom_set) + " (" + game.rom_name + ")";
    const cps3recomp::RomList list = cps3recomp::ParseRomList("", "", "", "", "", "", "", "", "", game.other_set);
    for (size_t n = 0; n < list.others.size(); n++) {
        text += (n == 0 ? " or another region's: " : n + 1 == list.others.size() ? " or " : ", ") + list.others[n].set;
    }
    return text;
}

// A ROM set: the set's zip (with its parent set's zip beside it, if the collection keeps shared files
// there) or a folder with the set's files. Every file is checked against the checksums the port was made
// from, and the program is deciphered with the set's key, as the game itself does when it starts. The
// launcher only ever reads it: never copies, moves or changes it.
launcher::FileCheck check_rom_set(const GameInfo& game, const std::string& path) {
    launcher::FileCheck check;
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) {
        check.message = "It isn't there any more.";
        return check;
    }
    // (the same reading the game does: its files by checksum, from the zip, its parent's or the folder)
    const cps3recomp::RomList list =
        cps3recomp::ParseRomList(game.program, game.graphics, game.sound_program, game.samples, game.key, game.rom_set, game.rom_parent,
                                 game.board, game.cps_b, game.other_set);
    cps3recomp::RomSet set;
    std::string message;
    if (cps3recomp::LoadRomSet(path, list, set, message)) {
        check.result = launcher::FileCheck::Result::Good;
        check.message = set.name.empty() ? game.rom_name : set.name;
    } else {
        check.message = "Not usable: " + message + ". This port needs the set " + sets_named(game) + (*game.key ? ", with its key file" : "") +
                        ". Other versions of the game will not work.";
    }
    return check;
}

// A set in a "rom" folder beside the launcher or above it (a build inside the source tree finds the tree's
// own, from the build folder or from the app bundle): rom/<id>/, or rom/ itself
std::string rom_set_nearby(const GameInfo& game, const std::filesystem::path& program_folder) {
    std::filesystem::path folder = program_folder;
    std::error_code error;
    for (int up = 0; up < 8 && !folder.empty(); up++, folder = folder.parent_path()) {
        for (const std::filesystem::path& rom : {folder / "rom" / game.id, folder / "rom"}) {
            if (std::filesystem::is_directory(rom, error) && check_rom_set(game, rom.string()).usable()) {
                return rom.string();
            }
        }
        if (folder == folder.parent_path()) {
            break;
        }
    }
    return {};
}

// Where in a folder of ROM sets this game's may be: its zip or a folder of its files, by the set's name (and
// then the parent set's, which a merged collection names it by)
std::vector<std::filesystem::path> rom_set_candidates(const GameInfo& game, const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> list;
    std::vector<std::string> names = {game.rom_set, game.rom_parent};
    for (const cps3recomp::RomList::Other& other : cps3recomp::ParseRomList("", "", "", "", "", "", "", "", "", game.other_set).others) {
        names.push_back(other.set);
    }
    for (const std::string& name : names) {
        if (!name.empty()) {
            list.push_back(folder / (name + ".zip"));
            list.push_back(folder / name);
        }
    }
    return list;
}

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

// What a launcher is told about one game
launcher::Description describe_game(const GameInfo& game) {
    launcher::Description d;
    const std::string set = sets_named(game);
    d.original = "in the arcade";
    d.file_heading = "ROM SET";
    // (a game that names its own buttons is not a fighting game, and has no training mode; the collection's controls,
    // which its first game's description gives, are for every game, the fighting games among them)
    const bool fighting = !*game.buttons;
#ifdef LAUNCHER_COLLECTION
    const bool forAll = true;
#else
    const bool forAll = fighting;
#endif
#ifdef __ANDROID__
    // (Android: the set is copied into the app's folder; the screen's controls have a Menu button, and Back opens it)
    d.file_intro = keep("Choose your own copy of the game: the arcade ROM set " + set + (*game.key ? ", with its key file" : "") +
                        (*game.other_set ? ". Only these versions work" : ". Only this version works") +
                        ". It is copied into the app's folder.\n\nWhile playing, Back or the screen's Menu button, or the left "
                        "stick's click (or Start and Select together) on a gamepad, opens the in-game menu: " +
#else
    d.file_intro = keep("Choose your own copy of the game: the arcade ROM set " + set + (*game.key ? ", with its key file" : "") +
                        (*game.other_set ? ". Only these versions work" : ". Only this version works") +
                        ". It stays where it is: the launcher only reads it.\n\nWhile playing, Esc or F1 on "
                        "the keyboard, or the left stick's click (or Start and Select together) on a gamepad, opens the in-game menu: " +
#endif
                        (fighting ? "training mode and the other modes" : "the game's options") + ", save states, speed, and every feature.");
#ifdef LAUNCHER_COLLECTION
    if (const GameInfo* other = game.extra_set && *game.extra_set ? game_of_set(game.extra_set) : nullptr) {
        d.file_intro = keep(std::string(d.file_intro) + "\n\nIt takes things from " + other->name + " too: choose that game's ROM set (" +
                            other->rom_set + ") on its own page as well.");
    }
#endif
#ifdef __ANDROID__
    d.file_empty = keep("None chosen. Choose " + std::string(game.rom_set) + ".zip.");
#else
    d.file_empty = keep("None chosen. Drop " + std::string(game.rom_set) + ".zip or its folder here, or choose the zip.");
#endif
    d.file_not_set_up = "The game isn't set up yet. Open the launcher (without --play) once to choose your ROM set.";
    d.file_filter = {"ROM set (zip)", "zip"};
    d.file_key = "rom";
    d.check = [game](const std::string& path) { return check_rom_set(game, path); };
    d.find_nearby = [game](const std::filesystem::path& folder) { return rom_set_nearby(game, folder); };
    d.candidates = [game](const std::filesystem::path& folder) { return rom_set_candidates(game, folder); };
    const std::vector<FeatureSwitch> switches = feature_switches(game.display);
    // What the game is started with: the player's ROM set (read only) and their saves folder
    const std::string extra = game.extra_set ? game.extra_set : "";
    d.arguments = [switches, extra](const launcher::Launch& launch) {
        std::vector<std::string> args = {"--rom", launch.file, "--eeprom", (launch.folder / "saves" / "eeprom.bin").string(),
                                         "--settings", (launch.folder / "settings.ini").string()};
#ifdef LAUNCHER_COLLECTION
        if (const GameInfo* other = extra.empty() || !launch.file_of ? nullptr : game_of_set(extra.c_str())) {
            const std::string file = launch.file_of(other->id);
            if (!file.empty()) {
                args.push_back("--rom2");
                args.push_back(file);
            }
        }
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
    // (online play: port/netplay.h)
    d.online = {"--host", "--join", 7845, true,
                "Two players, each on their own machine with the game, anywhere: one hosts and gives the other the code their game "
                "shows; the other joins with it. Each presses Start to come in. The match has the host's board settings and the "
                "features that change play."};
    // (the lobby: online/, the matches hosts have listed; Netplay::kService, kProtocol)
    d.online.lobby = keep(std::string(port::Netplay::kService) + "/v1/lobby/" + game.id + "?protocol=" + std::to_string(port::Netplay::kProtocol));
    d.online.list_argument = "--net-list";
    d.online.name_argument = "--net-name";
    d.online.arcade_argument = "--net-arcade"; // (the host plays alone until the other player comes: port/main.cpp, ArcadeFrame)
    d.online.invite_argument = "--net-invite"; // (a player in the waiting room invited: online/, Collection::room)
    d.online.invite_name_argument = "--net-invite-name";
    d.display = {{"fullscreen", "Full screen", "F11 switches while playing.", false, nullptr}};  // (a window unless asked)
    for (const FeatureSwitch& one : switches) {
        d.display.push_back({keep(one.name), keep(one.title), keep(one.detail), false, nullptr});
    }
    // (its own table: a collection's game has the one every game shares, Collection::bindings, unless it is given a
    // layout of its own on the Controllers page)
    d.bindings = port::MakeBoundTable(*game.players ? atoi(game.players) : 2, game.buttons, false).table;
    d.controls_heading = "WHILE PLAYING";
    d.controls = {{"Keys", keep(std::string("Esc, F1, the left stick's click, or Start and Select together: the in-game menu (") + (forAll ? "training mode and the other modes" : "the game's options") + ", save states, coin, cheats, quit)   P / O: stop, one frame on   - / =: speed   " + (forAll ? "Backspace: start over (training)   " : "") + "F5 / F9: save and load a state   F6 / F7: slot   F2: operator's test switch   F11: full screen. On a gamepad: right stick's click stops the game and starts it again, left trigger goes on a frame while it is stopped, left shoulder starts over. (A key or button given to the game above does only that.)")}};
    return d;
}

}  // namespace

#ifdef LAUNCHER_COLLECTION

#include "games.h"

namespace {
// As many players as the game with the most, and each button called as a fighting game calls it and as the other
// games call it (port/bindings.h)
const port::BoundTable& shared_table() {
    static const port::BoundTable& table = [&]() -> const port::BoundTable& {
        int players = 2;
        std::vector<std::string> names(port::kButtons);
        for (const GameInfo& game : kGames) {
            players = std::max(players, *game.players ? atoi(game.players) : 2);
            std::string list = game.buttons;
            for (int button = 0; button < port::kButtons && !list.empty(); button++) {
                const size_t bar = list.find('|');
                std::string name = list.substr(0, bar);
                name.erase(0, name.find_first_not_of(' '));
                name.erase(name.find_last_not_of(' ') + 1);
                list = bar == std::string::npos ? "" : list.substr(bar + 1);
                if (!name.empty() && ("/" + names[button] + "/").find("/" + name + "/") == std::string::npos) {
                    names[button] += (names[button].empty() ? "" : "/") + name;
                }
            }
        }
        std::string buttons;
        for (int button = 0; button < port::kButtons; button++) {
            buttons += (button ? "|" : "") + names[button];
        }
        return port::MakeBoundTable(players, buttons, true);
    }();
    return table;
}
// The collection's game whose ROM set this is
const GameInfo* game_of_set(const char* set) {
    for (const GameInfo& game : kGames) {
        if (std::string(game.rom_set) == set) {
            return &game;
        }
    }
    return nullptr;
}
}  // namespace

const launcher::Collection& launcher::describe_collection() {
    static const Collection about = [] {
        Collection c;
        c.subtitle = "Capcom CPS-1 and CPS-2 arcade games, native";
#ifdef __ANDROID__
        // (Android: the sets are chosen with the system's picker and copied into the app's folder: recomp-common's
        // launcher, App::choose_folder)
        c.intro = "Each game needs your own copy of it: its arcade ROM set (a CPS-2 game's with its key file). Add the sets from your phone or a "
                  "drive: they are copied into the app's folder.\n\n"
                  "While playing, Back or the screen's Menu button, or the left stick's click (or Start and Select together) on a gamepad, "
                  "opens the in-game menu.";
        c.find_title = "Add your sets";
        c.find_detail = "Choose the ROM sets, as many as you like at once, by the set's name (sfa2u.zip, say). Each goes to its game.";
#else
        c.intro = "Each game needs your own copy of it: its arcade ROM set (a CPS-2 game's with its key file). Show the launcher the folder you keep them in, "
                  "or drop a set (or the folder) on this window. The sets stay where they are: the launcher only reads them.\n\n"
                  "While playing, Esc or F1 on the keyboard, or the left stick's click (or Start and Select together) on a gamepad, "
                  "opens the in-game menu.";
        c.find_title = "Look in a folder";
        c.find_detail = "Finds each game's ROM set in a folder of them, by the set's name (sfa2u.zip, say).";
#endif
        for (const GameInfo& game : kGames) {
            c.games.push_back({game.id, game.name, game.id, "", describe_game(game), *game.series ? game.series : nullptr});
        }
        c.bindings = shared_table().table;
        // The Multi game page: the fighting games' endless battle (fight/battle.h), each game's every stage once and
        // then the next game's (--multi endless:shuffle:SECONDS or endless:scroll:SPEED)
        c.multi_intro = "Several games played one after another, as long as you like. Choose the games and how they are played, then Start.";
        // (online play's lobby, every game's: online/, Netplay::kService, kProtocol)
        static const std::string lobby = std::string(port::Netplay::kService) + "/v1/lobby?protocol=" + std::to_string(port::Netplay::kProtocol);
        c.lobby = lobby.c_str();
        static const std::string lookup = std::string(port::Netplay::kService) + "/v1/code/";
        c.code_lookup = lookup.c_str();
        // (the waiting room: online/, the players whose Online page is open, to invite to a match)
        static const std::string room = std::string(port::Netplay::kService) + "/v1/room";
        c.room = room.c_str();
        c.room_protocol = port::Netplay::kProtocol;
        c.file_name = "ROM set";
        c.online_features = "Online"; // (the ping and the input delay on the screen: port/main.cpp)
        c.online_intro = "Play someone on another machine: join a match a player is hosting, or host one in any of your games for others to "
                         "join, or invite someone from the waiting room. A game's own page has its Online card too, for a match with a code only the people "
                         "you give it to know.";
        launcher::MultiMode endless;
        endless.key = "endless";
        endless.title = "Endless battle";
        endless.detail = "Two fighters the game plays, on every stage of a game, and then on to the next game's. Touch the controls to take a "
                         "fighter over; ten idle seconds give it back. The fighters are as each game's own Endless battle page has them.";
        for (const GameInfo& game : kGames) {
            if (std::string(game.multi).find("endless") != std::string::npos) {
                endless.games.push_back(game.id);
            }
        }
        endless.choices = {
            {"stages", "Stages", "Shuffle: a game's stages in turn at random, each fading into the next. Scroll: they go by in order, and the fighters are carried along.",
             {"Shuffle", "Scroll"}},
            {"time", "Each stage for", "Shuffle: how long they fight on a stage before it fades into the next.",
             {"30 seconds", "1 minute", "2 minutes", "3 minutes", "5 minutes"}, 1, "stages", 0},
            {"speed", "Scroll speed", "Scroll: how fast the stages go by.", {"Slow", "Medium", "Fast"}, 1, "stages", 1},
            {"screen", "Screen", "Every game full screen, or every game in a window, or each as its own Full screen switch has it. For this run only.",
             {"Full screen", "Window", "As each game is set"}},
            {"picture", "Picture", "Every game in widescreen, or every game 4:3, or each as its own Widescreen switch has it. For this run only.",
             {"Widescreen", "4:3", "As each game is set"}},
            // (the Display page's own, port/main.cpp: every game has them, and every endless battle invisible walls)
            {"walls", "Invisible walls", "With widescreen: every game played exactly as on the board, behind walls that are not seen, or every game "
             "to the wide picture's edges, or each as its own Display page has it. For this run only.",
             {"On", "Off", "As each game is set"}, 2},
            {"scaling", "Scaling", "Sharp: each pixel a crisp block. Whole pixels: every pixel the same whole number of the screen's pixels tall. "
             "Smooth: softened all over. Or each as its own Display page has it. For this run only.",
             {"Sharp", "Whole pixels", "Smooth", "As each game is set"}, 3},
            {"shape", "Picture shape", "4:3 as the arcade's monitor, square pixels, or filling the window or screen, or each as its own Display "
             "page has it. For this run only.",
             {"4:3", "Square pixels", "Fill", "As each game is set"}, 3},
            {"size", "Window size", "How large the first game's window opens, in times the board's 224 lines, or as its own Display page has it. "
             "Each game after it opens where the one before was, as large.",
             {"2x", "3x", "4x", "5x", "6x", "As each game is set"}, 5},
            // (the Display page's Shader, port/main.cpp display_shader: the same choices, in the same order)
            {"shader", "Shader", "Every game through the same shader, or each as its own Display page has it. For this run only.",
             {"Off", "CRT", "CRT Lottes", "CRT Royale", "CRT Guest", "CRT Light", "Scanlines: light", "Scanlines: strong", "Scanlines: OSSC",
              "Scanlines: dark", "xBRZ", "ScaleFX", "Your own", "As each game is set"},
             13},
            // (port/zoom.h: --zoom 1:3, the camera on both fighters from the start)
            {"zoom", "Zoom", "Both fighters: in every game the camera follows the two fighters, as close as they allow (the zoom's L3 and R3 "
             "together). Off: as each game starts, all of the picture.",
             {"Off", "Both fighters"}},
        };
        // (port/frontend_sdl.cpp: in a Multi game run; each unless the player has given it to the game)
        endless.buttons = {
            {"Next game", "Fades out of this game and on to the next.", {"N", "Left shoulder"}},
            {"Fast forward", "Held: the game runs four times as fast, without sound.", {"F", "Left trigger"}},
        };
        endless.arguments = [](const std::vector<int>& values) {
            static const int kSeconds[] = {30, 60, 120, 180, 300};
            std::vector<std::string> args = {"--multi", values[0] == 1 ? "endless:scroll:" + std::to_string(values[2])
                                                                       : "endless:shuffle:" + std::to_string(kSeconds[std::clamp(values[1], 0, 4)])};
            // (after the game's own arguments, so these win; nothing is kept in its settings)
            if (values[3] == 0) {
                args.push_back("--fullscreen");
            } else if (values[3] == 1) {
                args.push_back("--window");
            }
            if (values[4] != 2) {
                args.push_back("--features");
                args.push_back(values[4] == 0 ? "widescreen" : "-widescreen");
            }
            // (the features' own values: display_scaling and display_shape as listed, window_size 3x first)
            static const int kSizes[] = {1, 0, 2, 3, 4};
            std::string display;
            auto give = [&](const std::string& one) { display += (display.empty() ? "" : ",") + one; };
            if (values[5] != 2) {
                give(values[5] == 0 ? "widescreen_walls" : "-widescreen_walls");
            }
            if (values[6] != 3) {
                give("display_scaling=" + std::to_string(values[6]));
            }
            if (values[7] != 3) {
                give("display_shape=" + std::to_string(values[7]));
            }
            if (values[8] != 5) {
                give("window_size=" + std::to_string(kSizes[std::clamp(values[8], 0, 4)]));
            }
            if (values[9] != 13) {
                give("display_shader=" + std::to_string(values[9]));
            }
            if (!display.empty()) {
                args.push_back("--features");
                args.push_back(display);
            }
            if (values[10] == 1) {
                args.push_back("--zoom");
                args.push_back("1:3");
            }
            return args;
        };
        if (!endless.games.empty()) {
            c.multi.push_back(endless);
        }
        return c;
    }();
    return about;
}

#else

#ifndef LAUNCHER_DISPLAY_SWITCHES
#define LAUNCHER_DISPLAY_SWITCHES ""
#endif
#ifndef LAUNCHER_PLAYERS
#define LAUNCHER_PLAYERS ""
#endif
#ifndef LAUNCHER_BUTTONS
#define LAUNCHER_BUTTONS ""
#endif
#ifndef LAUNCHER_BOARD
#define LAUNCHER_BOARD ""
#endif
#ifndef LAUNCHER_CPS_B
#define LAUNCHER_CPS_B ""
#endif
#ifndef LAUNCHER_OTHER_SET
#define LAUNCHER_OTHER_SET ""
#endif

const launcher::Description& launcher::describe() {
    static const Description about = describe_game({LAUNCHER_GAME_EXE, LAUNCHER_GAME_NAME, LAUNCHER_ROM_SET, LAUNCHER_ROM_PARENT, LAUNCHER_ROM_NAME,
                                                    LAUNCHER_ROM_PROGRAM, LAUNCHER_ROM_GRAPHICS, LAUNCHER_ROM_SOUND_PROGRAM, LAUNCHER_ROM_SAMPLES,
                                                    LAUNCHER_ROM_KEY, LAUNCHER_DISPLAY_SWITCHES, LAUNCHER_PLAYERS, LAUNCHER_BUTTONS,
                                                    LAUNCHER_BOARD, LAUNCHER_CPS_B, "", "", LAUNCHER_OTHER_SET, ""});
    return about;
}

#endif
