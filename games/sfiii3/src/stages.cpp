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
    const StageGame kNewGeneration = {"sfiiin", {0x0201BE7C, 0x0201BE84, 0x0201BE94}, {0x0201BE6E, 0x0201BEF0}, 0, 0, 680, 320,
                                      {0x0200EB33, 0x0201DB15}, {0x0200D22B, 0x0200D22D, 0x0201DAF1, 0x0200D603, 0x0200D605, 0x0201DB09},
                                      0x02012CA3, {0x0200D18C, 0x0200D564}};
    const StageGame kSecondImpact = {"sfiii2n", {0x020221CC, 0x020221D4, 0x020221E4}, {0, 0}, 0x02022218, 0x0202221C, 0, 0,
                                     {0x02010167, 0x02023EF5}, {0x0200E5A3, 0x0200E5A5, 0x0200E9AF, 0x0200E9B1, 0, 0}, 0x020142E7,
                                     {0x0200E504, 0x0200E910}};
    // The choices: off, and stages by their games' own numbers (the ones seen so far)
    struct Choice {
        const StageGame *game;
        int stage;
    };
    const Choice kChoices[] = {{nullptr, -1},        {&kNewGeneration, 4}, {&kNewGeneration, 8}, {&kNewGeneration, 12},
                               {&kSecondImpact, 4}, {&kSecondImpact, 5}, {&kSecondImpact, 6}};

    struct State {
        int loaded = 0;      // the choice whose board is there
        bool fought = false; // it has come to its fight
        uint8_t life = 0;
        uint32_t frames = 0; // its frames, for working its controls on the way
        int lastCamera = 0;  // (its camera as its last frame left it, for the debugging aid)
        int ownRight = 0, ownLeft = 0, madeRight = 0, madeLeft = 0; // its stage's ends as its game set them, and as made
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
            if (game.stage && choice.stage >= 0) {
                ram[game.stage & 0x7FFFF] = static_cast<uint8_t>(choice.stage);
            }
            return;
        }
        stage.inputs = stage.extra = 0xFFFFFFFFu;
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
        if (game.rightAt) {
            const int right = static_cast<int16_t>(Word(ram, game.rightAt)), left = static_cast<int16_t>(Word(ram, game.leftAt));
            if (right != g.madeRight || left != g.madeLeft) {
                g.ownRight = right;
                g.ownLeft = left;
            }
            g.madeRight = g.ownRight - margin;
            g.madeLeft = g.ownLeft + margin;
            for (const uint32_t at : {game.rightAt, game.rightAt + 2}) {
                SetWord(ram, at, g.madeRight);
            }
            for (const uint32_t at : {game.leftAt, game.leftAt + 2}) {
                SetWord(ram, at, g.madeLeft);
            }
        }
        const int theirRight = game.rightAt ? g.madeRight : game.right - margin;
        const int theirLeft = game.leftAt ? g.madeLeft : game.left + margin;
        if (right > left && theirRight > theirLeft) {
            // (its game makes its camera from its fighters each frame, whatever its camera words say: so its fighters,
            // unseen, are put where 3rd Strike's are, from one stage to the other, and its camera follows them)
            for (int p = 0; p < 2; p++) {
                const int x = static_cast<int16_t>(Ram16(sfiii3::kPlayer[p] + sfiii3::kX));
                SetWord(ram, game.players[p] + 0x64, theirLeft + (x - left) * (theirRight - theirLeft) / (right - left));
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

namespace {
    // Which of the sound chip's voices 3rd Strike's music has now: its sound driver keeps a record for each of its
    // music's 16 voices (0x02078D0C on, 0x74 each) and for each of 8 sound effects' (0x020797EC on), which take voices 8-15
    // from the music while they play. Its routine at 0x0613B1D8 (a note's pitch) is called for a voice as it is set,
    // with the record in r14 and the voice's registers in r13: a hook there notes who has the voice.
    std::array<bool, 16> g_music{};
    void PitchHook(Guest &g) {
        const uint32_t voice = ((g.R(13) & 0x1FFFFFFFu) - 0x040E0000u) / 0x20;
        if (voice < 16) {
            const uint32_t record = g.R(14);
            g_music[voice] = record >= 0x02078D0Cu && record < 0x02078D0Cu + 16 * 0x74;
        }
    }
} // namespace

// Each frame: the board for the choice made (loaded when the choice changes: a moment's pause)
void StagesBeforeFrame(Machine &machine) {
    // (in a fight on the stage board's stage: its music, 3rd Strike's left out; the effects and voices are 3rd Strike's)
    const bool music = g.loaded && g.fought && InMatch() && machine.StageBoard();
    machine.stageSound = music;
    int silenced = 0;
    for (int v = 0; v < 16; v++) {
        machine.board.silenced[v] = music && g_music[v];
        silenced += machine.board.silenced[v];
    }
    // (debugging aid, SFIII3_STAGE_SOUND=1: every 300 frames, the voices left out and the stage board's sound)
    static const bool log = getenv("SFIII3_STAGE_SOUND") != nullptr;
    if (log && machine.frame % 300 == 0) {
        fprintf(stderr, "stage sound: frame %llu, %s, %d of 3rd Strike's voices left out as music\n", static_cast<unsigned long long>(machine.frame),
                music ? "the stage's music" : "3rd Strike's own", silenced);
    }
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
                             "The definitive version's stages, an early form: a fight drawn on one of New Generation's or 2nd "
                             "Impact's stages, by running that game beside this one (its set beside this one's, sfiiin.zip or sfiii2n.zip). "
                             "In a fight its music plays in place of 3rd Strike's. It takes a few seconds to reach its stage after it is chosen.",
                             0, {"3rd Strike's", "New Generation: London", "New Generation: cliffs", "New Generation: snowy street", "2nd Impact: street",
                                 "2nd Impact: factory", "2nd Impact: market"}});
    if (Machine *machine = TheMachine()) {
        machine->stageObject = StageObject;
        machine->ownStageObject = OwnStageObject;
        machine->beforeStage = BeforeStage;
        machine->composeNow = InMatch;
    }
    AddHook("Voice set (music or effect)", 0x0613B1D8, 8, 0xBA5502CCC81A1B0Eull, PitchHook);
}

} // namespace cps3recomp
