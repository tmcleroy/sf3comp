// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The definitive version's stages, an experiment (games/sfiii3/DEFINITIVE.md): New Generation's or 2nd Impact's board
// run beside 3rd Strike's, taken unseen to a fight and held there, its camera following 3rd Strike's; in a fight the
// picture is its stage with 3rd Strike's fighters, effects and HUD over it. The stage board is scenery: outside the
// save state, the same for both players online (each draws its own).
#include "game.h"
#include "game_info.h"
#include "sfiii3.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
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
        uint32_t clock[2], life[3], stage;
    };
    const StageGame kNewGeneration = {"sfiiin", {0x0201BE7C, 0x0201BE84, 0x0201BE94}, {0x0201BE6E, 0x0201BEF0}, 0, 0, 748, 272,
                                      {0x0200EB33, 0x0201DB15}, {0x0200D603, 0x0200D605, 0x0201DB09}, 0};
    const StageGame kSecondImpact = {"sfiii2n", {0x020221CC, 0x020221D4, 0x020221E4}, {0, 0}, 0x02022218, 0x0202221C, 0, 0,
                                     {0x02010167, 0x02023EF5}, {0x0200E5A3, 0x0200E5A5, 0}, 0x020142E7};
    // The choices: off, New Generation's (the stage its fight comes to), 2nd Impact's by its stage number
    struct Choice {
        const StageGame *game;
        int stage;
    };
    const Choice kChoices[] = {{nullptr, -1}, {&kNewGeneration, -1}, {&kSecondImpact, 4}, {&kSecondImpact, 5}, {&kSecondImpact, 6}};

    struct State {
        int loaded = 0;      // the choice whose board is there
        bool fought = false; // it has come to its fight
        uint8_t life = 0;
        uint32_t frames = 0; // its frames, for working its controls on the way
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

    // A stage's object: a tilemap's lines, or an object with a stage's colours (palettes 0x80 and up)
    bool StageObject(const machine::Machine &m, uint32_t object) {
        const uint32_t *list = m.SpriteList();
        return (list[object * 4 + 2] & 3) == 0 || (list[object * 4] & 0x1FF) >= 0x80;
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
            if (game.stage && choice.stage >= 0) {
                ram[game.stage & 0x7FFFF] = static_cast<uint8_t>(choice.stage);
            }
            return;
        }
        stage.inputs = stage.extra = 0xFFFFFFFFu;
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
        const int theirRight = game.rightAt ? static_cast<int16_t>(Word(ram, game.rightAt)) : game.right;
        const int theirLeft = game.leftAt ? static_cast<int16_t>(Word(ram, game.leftAt)) : game.left;
        if (right > left && theirRight > theirLeft) {
            const int theirs = theirLeft + (std::clamp(camera, left, right) - left) * (theirRight - theirLeft) / (right - left);
            for (const uint32_t at : game.camera) {
                SetWord(ram, at, theirs);
            }
            for (const uint32_t at : game.edge) {
                if (at) {
                    SetWord(ram, at, theirs - 192);
                }
            }
        }
    }
} // namespace

// Each frame: the board for the choice made (loaded when the choice changes: a moment's pause)
void StagesBeforeFrame(Machine &machine) {
    const int choice = std::clamp(FeatureValue("stages"), 0, static_cast<int>(std::size(kChoices)) - 1);
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
                             "The definitive version's stages, an early form: a fight drawn on New Generation's stage, or one of 2nd "
                             "Impact's, by running that game beside this one (its set beside this one's, sfiiin.zip or sfiii2n.zip). "
                             "3rd Strike's own music plays. It takes a few seconds to reach its stage after it is chosen.",
                             0, {"3rd Strike's", "New Generation", "2nd Impact: street", "2nd Impact: factory", "2nd Impact: market"}});
    if (Machine *machine = TheMachine()) {
        machine->stageObject = StageObject;
        machine->beforeStage = BeforeStage;
        machine->composeNow = InMatch;
    }
}

} // namespace cps3recomp
