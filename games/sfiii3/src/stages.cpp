// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The definitive version's stages, an experiment (games/sfiii3/DEFINITIVE.md): New Generation's or 2nd Impact's board
// run beside 3rd Strike's, taken unseen to a fight and held there, its camera following 3rd Strike's; in a fight the
// picture is its stage with 3rd Strike's fighters, effects and HUD over it. The stage board is scenery: outside the
// save state, the same for both players online (each draws its own).
#include "game.h"
#include "game_info.h"
#include "lift.h"
#include "sfiii3.h"
#include "system.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <filesystem>
#include <memory>

namespace cps3recomp {

namespace {
    // Where a stage game keeps what is held (found in RAM traces: DEFINITIVE.md)
    struct StageGame {
        const char *set;
        uint32_t camera[3], edge[2]; // the camera's middle (and copies), its left edge (none: 0)
        uint32_t rightAt, leftAt;    // the stage's ends, if the game keeps them (else right, left)
        int right, left;
        uint32_t clock[2], life[6], stage;
        uint32_t players[2]; // the players' records (the place across the stage at +0x64, as in 3rd Strike)
    };
    const StageGame kNewGeneration = {"sfiiin", {0x0201BE7C, 0x0201BE84, 0x0201BE94}, {0x0201BE6E, 0x0201BEF0}, 0x0201BEC8, 0x0201BECC, 680, 320,
                                      {0x0200EB33, 0x0201DB15}, {0x0200D22B, 0x0200D22D, 0x0201DAF1, 0x0200D603, 0x0200D605, 0x0201DB09},
                                      0x02012CA3, {0x0200D18C, 0x0200D564}};
    const StageGame kSecondImpact = {"sfiii2n", {0x020221CC, 0x020221D4, 0x020221E4}, {0, 0}, 0x02022218, 0x0202221C, 0, 0,
                                     {0x02010167, 0x02023EF5}, {0x0200E5A3, 0x0200E5A5, 0x0200E9AF, 0x0200E9B1, 0, 0}, 0x020142E7,
                                     {0x0200E504, 0x0200E910}};
    // (a choice's stage: the one fought on in 3rd Strike, as that game has it: both number their stages as 3rd Strike
    // numbers its characters (Gill 0, Alex 1...: seen in their own one-player games, the opponent's stage number and
    // name), New Generation's 0-12 and 2nd Impact's 0-14; the characters they have not, 3rd Strike's own stage)
    constexpr int kOpponents = -2;
    // The choices: off, and stages by their games' own numbers (the ones seen so far)
    struct Choice {
        const StageGame *game;
        int stage;
    };
    // The choices: off, then every stage of each game by its own number (named for what it shows)
    const Choice kChoices[] = {
        {nullptr, -1}, {&kNewGeneration, 0}, {&kNewGeneration, 1}, {&kNewGeneration, 2},
        {&kNewGeneration, 3}, {&kNewGeneration, 4}, {&kNewGeneration, 5}, {&kNewGeneration, 6},
        {&kNewGeneration, 7}, {&kNewGeneration, 8}, {&kNewGeneration, 9}, {&kNewGeneration, 10},
        {&kNewGeneration, 11}, {&kNewGeneration, 12}, {&kSecondImpact, 0}, {&kSecondImpact, 1},
        {&kSecondImpact, 2}, {&kSecondImpact, 3}, {&kSecondImpact, 4}, {&kSecondImpact, 5},
        {&kSecondImpact, 6}, {&kSecondImpact, 7}, {&kSecondImpact, 8}, {&kSecondImpact, 9},
        {&kSecondImpact, 10}, {&kSecondImpact, 11}, {&kSecondImpact, 12}, {&kSecondImpact, 13},
        {&kSecondImpact, 14}, {&kNewGeneration, kOpponents}, {&kSecondImpact, kOpponents},
    };
    // (by whose stage it is: both games number their stages as 3rd Strike numbers its characters; New Generation has no
    // Hugo, and its stage 6 is a beer festival)
    const char *const kNames[] = {
        "3rd Strike's", "New Generation: Gill's", "New Generation: Alex's", "New Generation: Ryu's",
        "New Generation: Yun's", "New Generation: Dudley's", "New Generation: Necro's", "New Generation: a beer festival (6)",
        "New Generation: Ibuki's", "New Generation: Elena's", "New Generation: Oro's", "New Generation: Yang's",
        "New Generation: Ken's", "New Generation: Sean's", "2nd Impact: Gill's", "2nd Impact: Alex's",
        "2nd Impact: Ryu's", "2nd Impact: Yun's", "2nd Impact: Dudley's", "2nd Impact: Necro's",
        "2nd Impact: Hugo's", "2nd Impact: Ibuki's", "2nd Impact: Elena's", "2nd Impact: Oro's",
        "2nd Impact: Yang's", "2nd Impact: Ken's", "2nd Impact: Sean's", "2nd Impact: Urien's",
        "2nd Impact: Akuma's", "New Generation: the opponent's", "2nd Impact: the opponent's",
    };

    struct State {
        int loaded = 0;      // the choice whose board is there
        bool fought = false; // it has come to its fight
        uint8_t life = 0;
        uint32_t frames = 0; // its frames, for working its controls on the way
        int lastCamera = 0;  // (its camera as its last frame left it, for the debugging aid)
        int ownRight = 0, ownLeft = 0, madeRight = 0, madeLeft = 0; // its stage's ends as its game set them, and as made
        int stage = -1;      // (the opponent's: the stage its board is on its way to, or in; -1 none)
        bool offsetKnown = false;
        int offset = 0;      // its camera's middle less its fighters' as its fight began (New Generation's rainy London
                             // stage places its fighters about 512 left of its camera; the others about 0)
    } g;

    uint16_t Word(const uint8_t *ram, uint32_t at) {
        return static_cast<uint16_t>((ram[at & 0x7FFFF] << 8) | ram[(at + 1) & 0x7FFFF]);
    }
    void SetWord(uint8_t *ram, uint32_t at, int value) {
        ram[at & 0x7FFFF] = static_cast<uint8_t>(value >> 8);
        ram[(at + 1) & 0x7FFFF] = static_cast<uint8_t>(value);
    }
    bool InMatch() {
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2;
    }
    // On the stage: a round being fought, or its start or its end (the match's state 1 to 8: 1 the round's start, 2
    // fought, 3 and 6 the knock-out, 7 and 8 its end; 9 the match's end, 0 between)
    bool OnStage() {
        const uint8_t state = Ram8(sfiii3::kMatchState);
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && state >= 1 && state <= 8;
    }

    // 3rd Strike's own stage: a tilemap's lines, or an object with a stage's colours (palettes 0x80 and up)
    bool OwnStageObject(const machine::Machine &m, uint32_t object) {
        const uint32_t *list = m.SpriteList();
        return (list[object * 4 + 2] & 3) == 0 || (list[object * 4] & 0x1FF) >= 0x80;
    }
    // The stage board's stage: all but its fighters (6 bits a pixel and a palette below 0x40: New Generation's 0x08 and
    // 0x10, 2nd Impact's 0x00 and 0x18) and their shadows (palette 0, blending). Its stage's people and things have
    // palettes from 0x40 (or 8 bits a pixel); its HUD is in the text layer, not drawn.
    bool StageObject(const machine::Machine &m, uint32_t object) {
        const uint32_t v1 = m.SpriteList()[object * 4];
        const uint32_t palette = v1 & 0x1FF;
        const bool sixBits = (v1 >> 9) & 1, blend = (v1 >> 10) & 1;
        if ((m.SpriteList()[object * 4 + 2] & 3) == 0) {
            return true;
        }
        return !((sixBits && palette < 0x40) || (blend && palette == 0));
    }

    std::unique_ptr<RomSet> LoadSet(const StageGame &game) {
        RomList list;
        list.set = game.set;
        const bool ng = game.set == std::string("sfiiin");
        list.bios = ParseRomFiles(ng ? GAME_STAGE_NG_BIOS : GAME_STAGE_2I_BIOS);
        const char *ngSimm[7] = {"", GAME_STAGE_NG_SIMM1, GAME_STAGE_NG_SIMM2, GAME_STAGE_NG_SIMM3, GAME_STAGE_NG_SIMM4, GAME_STAGE_NG_SIMM5,
                                 GAME_STAGE_NG_SIMM6};
        const char *iiSimm[7] = {"", GAME_STAGE_2I_SIMM1, GAME_STAGE_2I_SIMM2, GAME_STAGE_2I_SIMM3, GAME_STAGE_2I_SIMM4, GAME_STAGE_2I_SIMM5,
                                 GAME_STAGE_2I_SIMM6};
        for (int n = 1; n <= 6; n++) {
            list.simm[n] = ParseRomFiles(ng ? ngSimm[n] : iiSimm[n]);
        }
        list.programEnciphered = ng; // (2nd Impact's data is not)
        // (beside the player's 3rd Strike set: its zip or its folder, by the set's name)
        std::filesystem::path folder = std::filesystem::u8path(RomPath());
        folder = std::filesystem::is_directory(folder) ? folder.parent_path() : folder.parent_path();
        auto set = std::make_unique<RomSet>();
        std::string error;
        for (const std::filesystem::path &at : {folder / (std::string(game.set) + ".zip"), folder / game.set}) {
            if (std::filesystem::exists(at) && LoadRomSet(at.string(), list, *set, error)) {
                return set;
            }
        }
        fprintf(stderr, "stages: the set %s is not beside %s%s%s\n", game.set, RomPath().c_str(), error.empty() ? "" : ": ", error.c_str());
        return nullptr;
    }

    // Before each of the stage board's frames: on the way to its fight, coins, Start and a character; in it, no clock,
    // nobody hurt, no controls, and its camera where 3rd Strike's is (from one stage's ends to the other's)
    void BeforeStage(Machine &machine, machine::Machine &stage) {
        const Choice &choice = kChoices[g.loaded];
        const StageGame &game = *choice.game;
        uint8_t *ram = stage.mainRam;
        const uint8_t clock = ram[game.clock[0] & 0x7FFFF];
        const bool fighting = clock > 0 && clock <= 99 && ram[game.life[0] & 0x7FFFF] > 0;
        g.frames++;
        if (fighting && !g.fought && g.frames > 600) {
            g.fought = true;
            g.life = ram[game.life[0] & 0x7FFFF];

        }
        if (!g.fought) {
            const uint32_t phase = g.frames % 40;
            uint32_t inputs = 0, extra = 0;
            if (g.frames < 900) {
                inputs = phase < 4 ? 0x01000000u : 0; // (coins)
            } else if (phase < 4) {
                inputs = (g.frames / 40) % 2 ? 0x10000000u : 0x10u; // (Start, then light punch: a character, its super art)
            }
            stage.inputs = ~inputs;
            stage.extra = ~extra;
            const int stageNumber = choice.stage == kOpponents ? g.stage : choice.stage;
            if (game.stage && stageNumber >= 0) {
                ram[game.stage & 0x7FFFF] = static_cast<uint8_t>(stageNumber);
            }
            return;
        }
        stage.inputs = stage.extra = 0xFFFFFFFFu;
        if (!g.offsetKnown) {
            // (its fighters' places as its fight begins, once its game has put them: its own coordinates against its camera's)
            const int one = static_cast<int16_t>(Word(ram, game.players[0] + 0x64)), two = static_cast<int16_t>(Word(ram, game.players[1] + 0x64));
            if (one == 0 && two == 0) {
                return;
            }
            g.offsetKnown = true;
            // (only a whole difference of coordinates, as the rainy London stage's 512, is taken: a camera held at its
            // limit as the fight begins, or one this word does not hold (0), is not one)
            const int camera = static_cast<int16_t>(Word(ram, game.camera[1]));
            g.offset = camera - (one + two) / 2;
            if (camera == 0 || std::abs(g.offset) < 256) {
                g.offset = 0;
            }
            if (getenv("SFIII3_STAGE_CAMERA")) {
                fprintf(stderr, "stage fight: camera %d, fighters %d %d: offset %d\n", static_cast<int16_t>(Word(ram, game.camera[1])), one, two, g.offset);
            }
        }
        // (debugging aid, SFIII3_STAGE_CAMERA=1: the stage game's camera block, every 300 frames)
        static const bool cameraLog = getenv("SFIII3_STAGE_CAMERA") != nullptr;
        if (cameraLog && g.frames % 300 == 0) {
            fprintf(stderr, "stage camera:");
            for (uint32_t at = game.camera[1] - (getenv("SFIII3_STAGE_CAMERA_WIDE") ? 0x200 : 0x10); at < game.camera[1] + (getenv("SFIII3_STAGE_CAMERA_WIDE") ? 0x200 : 0x50); at += 2) {
                fprintf(stderr, " %d", static_cast<int16_t>(Word(ram, at)));
            }
            fprintf(stderr, "\n");
        }
        g.lastCamera = static_cast<int16_t>(Word(ram, game.camera[1]));
        for (const uint32_t at : game.clock) {
            ram[at & 0x7FFFF] = 99;
        }
        for (const uint32_t at : game.life) {
            if (at) {
                ram[at & 0x7FFFF] = g.life;
            }
        }
        const int camera = static_cast<int16_t>(Ram16(sfiii3::kCameraX));
        const int right = static_cast<int16_t>(Ram16(sfiii3::kCameraRight[0])), left = static_cast<int16_t>(Ram16(sfiii3::kCameraLeft[0]));
        // (in widescreen its camera stays as far from its stage's ends as 3rd Strike's is kept from its own: where its game
        // keeps them, they are narrowed, the game's own remembered)
        const int margin = machine.WideMargin();
        // (its ends, where its game keeps them and they make sense: New Generation's look odd on a stage or two, which
        // then take the ends assumed)
        bool kept = false;
        if (game.rightAt) {
            const int right = static_cast<int16_t>(Word(ram, game.rightAt)), left = static_cast<int16_t>(Word(ram, game.leftAt));
            if (right != g.madeRight || left != g.madeLeft) {
                g.ownRight = right;
                g.ownLeft = left;
            }
            // (kept in its fighters' coordinates: the camera's, less the offset; see `offset`)
            kept = g.ownLeft + g.offset > 0 && g.ownRight - g.ownLeft > 2 * margin + 64;
        }
        if (kept) {
            g.madeRight = g.ownRight - margin;
            g.madeLeft = g.ownLeft + margin;
            for (const uint32_t at : {game.rightAt, game.rightAt + 2}) {
                SetWord(ram, at, g.madeRight);
            }
            for (const uint32_t at : {game.leftAt, game.leftAt + 2}) {
                SetWord(ram, at, g.madeLeft);
            }
        }
        const int theirRight = kept ? g.madeRight + g.offset : game.right - margin;
        const int theirLeft = kept ? g.madeLeft + g.offset : game.left + margin;
        if (right > left && theirRight > theirLeft) {
            // (its game makes its camera from its fighters each frame, whatever its camera words say: so its fighters,
            // unseen, are put where 3rd Strike's are, from one stage to the other, and its camera follows them)
            for (int p = 0; p < 2; p++) {
                const int x = static_cast<int16_t>(Ram16(sfiii3::kPlayer[p] + sfiii3::kX));
                SetWord(ram, game.players[p] + 0x64, theirLeft + (x - left) * (theirRight - theirLeft) / (right - left) - g.offset);
            }
            const int theirs = theirLeft + (std::clamp(camera, left, right) - left) * (theirRight - theirLeft) / (right - left);
            for (const uint32_t at : game.camera) {
                SetWord(ram, at, theirs);
            }
            // (debugging aid, SFIII3_STAGE_CAMERA=1: the camera given, and what its frame before left)
            static const bool log = getenv("SFIII3_STAGE_CAMERA") != nullptr;
            if (log && g.frames % 60 == 0) {
                fprintf(stderr, "stage camera: 3rd Strike %d (%d-%d), given %d (%d-%d), it had %d\n", camera, left, right, theirs, theirLeft,
                        theirRight, g.lastCamera);
            }
            for (const uint32_t at : game.edge) {
                if (at) {
                    SetWord(ram, at, theirs - 192);
                }
            }
        }
    }
} // namespace

bool MusicVoice(int voice); // (music.cpp)

// Each frame: the board for the choice made (loaded when the choice changes: a moment's pause)
void StagesBeforeFrame(Machine &machine) {
    // (in a fight on the stage board's stage: its music, 3rd Strike's left out; the effects and voices are 3rd Strike's)
    // (the custom soundtrack's file, when one plays, in place of both)
    const bool music = g.loaded && g.fought && OnStage() && machine.StageBoard() && !machine.soundtrack.Playing() &&
                       (kChoices[g.loaded].stage != kOpponents || g.stage >= 0);
    machine.stageSound = music;
    int silenced = 0;
    for (int v = 0; v < 16; v++) {
        machine.board.silenced[v] = (music || machine.soundtrack.Playing()) && MusicVoice(v);
        silenced += machine.board.silenced[v];
    }
    // (debugging aid, SFIII3_STAGE_SOUND=1: every 300 frames, the voices left out and the stage board's sound)
    static const bool log = getenv("SFIII3_STAGE_SOUND") != nullptr;
    if (log && machine.frame % 300 == 0) {
        fprintf(stderr, "stage sound: frame %llu, %s, %d of 3rd Strike's voices left out as music\n", static_cast<unsigned long long>(machine.frame),
                music ? "the stage's music" : "3rd Strike's own", silenced);
    }
    const int choice = std::clamp(FeatureValue("stages"), 0, static_cast<int>(std::size(kChoices)) - 1);
    if (choice == g.loaded && choice && kChoices[choice].stage == kOpponents && machine.StageBoard() && !OnStage()) {
        // (the opponent's: between fights, 3rd Strike's next stage chosen, the board started again on its way to that one)
        const int most = kChoices[choice].game == &kNewGeneration ? 12 : 14;
        // (New Generation's 6, Hugo's number though it has no Hugo, is a beer festival: Hugo's, as Germany's)
        const int next = Ram8(sfiii3::kStage) <= most ? Ram8(sfiii3::kStage) : -1;
        if (next != g.stage) {
            const int loaded = g.loaded;
            g = {};
            g.loaded = loaded;
            g.stage = next;
            machine.StageBoard()->Reset();
        }
        return;
    }
    if (choice == g.loaded) {
        return;
    }
    g = {};
    g.loaded = choice;
    machine.SetStageBoard(choice ? LoadSet(*kChoices[choice].game) : nullptr);
    if (choice && !machine.StageBoard()) {
        g.loaded = 0; // (not there: as without it)
        SetFeatureValue("stages", 0);
    }
}

void RegisterStages(Game &game) {
    game.features.push_back({"stages", "Display", "Stages (experiment)",
                             "The definitive version's stages, an early form: a fight drawn on one of New Generation's or 2nd "
                             "Impact's stages, by running that game beside this one (its set beside this one's, sfiiin.zip or sfiii2n.zip). "
                             "In a fight its music plays in place of 3rd Strike's. It takes a few seconds to reach its stage after it is chosen. "
                             "The opponent's: each fight on that game's stage of the character whose stage 3rd Strike chose (its own "
                             "for the characters that game has not).",
                             kFeatureMenuChoices, {std::begin(kNames), std::end(kNames)}});
    if (Machine *machine = TheMachine()) {
        machine->stageObject = StageObject;
        machine->ownStageObject = OwnStageObject;
        machine->beforeStage = BeforeStage;
        // (composed while 3rd Strike's round is on and the board is in its fight: for the opponent's, on a stage it has)
        machine->composeNow = [] { return OnStage() && g.fought && (kChoices[g.loaded].stage != kOpponents || g.stage >= 0); };
        machine->stageFramesNow = [] { return g.fought ? 1 : 4; }; // (on its way to its fight: four frames a frame)
    }
}

} // namespace cps3recomp
