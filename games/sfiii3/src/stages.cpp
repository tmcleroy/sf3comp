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
    // 3rd Strike's own, as a stage board behind its fighters (the modes' shuffled and scrolling stages: battle.cpp)
    const StageGame kThirdStrike = {GAME_ROM_SET, {0x02026CA8, 0x02026CB0, 0x02026CC0}, {0, 0}, 0x02026CF4, 0x02026CF8, 748, 272,
                                    {0x02011377, 0x02028679}, {0x02068D0B, 0x020691A3, 0, 0, 0, 0}, 0x020154F5, {0x02068C6C, 0x02069104}};
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
        // (not on the menu: 3rd Strike's own stages behind its fighters, for the modes: kOwnFirst on)
        {&kThirdStrike, 0}, {&kThirdStrike, 1}, {&kThirdStrike, 2}, {&kThirdStrike, 3}, {&kThirdStrike, 4}, {&kThirdStrike, 5},
        {&kThirdStrike, 6}, {&kThirdStrike, 7}, {&kThirdStrike, 8}, {&kThirdStrike, 9}, {&kThirdStrike, 10}, {&kThirdStrike, 11},
        {&kThirdStrike, 12}, {&kThirdStrike, 13}, {&kThirdStrike, 14}, {&kThirdStrike, 15}, {&kThirdStrike, 16}, {&kThirdStrike, 17},
        {&kThirdStrike, 18}, {&kThirdStrike, 19}, {&kThirdStrike, 20},
    };
    constexpr int kOwnFirst = 31;
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
        int lastCamera = 0;  // its camera as its last frame left it
        int given = 0;       // ... and the camera it was given (its fighters placed for)
        // (the ends its camera really keeps, where they are nearer than its ends words say: learned when it stays put
        // short of where it was sent; 0 not known)
        int realLeft = 0, realRight = 0, stuck = 0, stuckAt = 0;
        int ownRight = 0, ownLeft = 0, madeRight = 0, madeLeft = 0; // its stage's ends as its game set them, and as made
        int stage = -1;      // (the opponent's: the stage its board is on its way to, or in; -1 none)
        bool offsetKnown = false;
        uint32_t inFight = 0; // its frames in its fight (its round's banners over by kBannersFor)
        int offset = 0;      // its camera's middle less its fighters' as its fight began (New Generation's rainy London
                             // stage places its fighters about 512 left of its camera; the others about 0)
    } g;

    // Stage boards kept as they were once in their fights (a stage reached once is there at once after), by game and
    // stage; the most recently used kept, up to 16
    struct Kept {
        const void *game = nullptr;
        int stage = -1;
        State state;
        std::vector<uint8_t> data;
        uint64_t used = 0;
    };
    std::vector<Kept> g_kept;
    bool g_keepNow = false;
    // Preloading (stages_preload, with the opponent's): between fights the board is lent to taking another stage of
    // its game to its fight, to keep; the board's own stage put back when it is wanted again
    struct Preload {
        bool on = false;
        State live;
    } g_pre;
    uint64_t g_uses = 0;
    // The modes' stage behind (battle.cpp): a choice in place of the menu's (0: the menu's), the camera wanted for it
    // (kFollow: 3rd Strike's mapped across), and the ends its camera was last kept between
    constexpr int kFollow = -100000;
    constexpr uint32_t kMadeFor = 480;    // (a stage made beside the one shown: its frames in its fight before it is ready, past "FIGHT!")
    constexpr uint32_t kBannersFor = 200; // (3rd Strike's own board: its "ROUND 1" and "FIGHT!" drawn as its stage is, so it is
                                          // shown only after them)
    int g_choiceWanted = 0;
    bool g_onlyMusic = false; // (the stage viewer: nothing of 3rd Strike's own heard but music: StageBehindOnlyMusic)
    bool g_boardMusic = true; // (the modes' stage behind: its own music, or 3rd Strike's fight's: StageBehindMusic)
    int g_cameraWanted = kFollow;
    int g_theirLeft = 0, g_theirRight = 0;

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
        if (kChoices[g.loaded].game == &kThirdStrike) {
            return OwnStageObject(m, object);
        }
        const uint32_t v1 = m.SpriteList()[object * 4];
        const uint32_t palette = v1 & 0x1FF;
        const bool sixBits = (v1 >> 9) & 1, blend = (v1 >> 10) & 1;
        if ((m.SpriteList()[object * 4 + 2] & 3) == 0) {
            return true;
        }
        return !((sixBits && palette < 0x40) || (blend && palette == 0));
    }

    std::unique_ptr<RomSet> LoadSet(const StageGame &game) {
        if (&game == &kThirdStrike) {
            // (the player's own set, read again)
            RomList own;
            own.set = GAME_ROM_SET;
            own.parent = GAME_ROM_PARENT;
            own.bios = ParseRomFiles(GAME_BIOS);
            const char *simms[7] = {"", GAME_SIMM1, GAME_SIMM2, GAME_SIMM3, GAME_SIMM4, GAME_SIMM5, GAME_SIMM6};
            for (int n = 1; n <= 6; n++) {
                own.simm[n] = ParseRomFiles(simms[n]);
            }
            own.programEnciphered = true;
            auto set = std::make_unique<RomSet>();
            std::string error;
            if (LoadRomSet(RomPath(), own, *set, error)) {
                return set;
            }
            fprintf(stderr, "stages: %s could not be read again: %s\n", RomPath().c_str(), error.c_str());
            return nullptr;
        }
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

    int StageNow() {
        return kChoices[g.loaded].stage == kOpponents ? g.stage : kChoices[g.loaded].stage;
    }
    void Keep(Machine &machine) {
        if (StageNow() < 0) {
            return; // (no stage of its own: 3rd Strike's)
        }
        const void *game = kChoices[g.loaded].game;
        Kept *slot = nullptr;
        for (Kept &one : g_kept) {
            if (one.game == game && one.stage == StageNow()) {
                slot = &one;
            }
        }
        if (!slot) {
            if (g_kept.size() >= 16) {
                slot = &*std::min_element(g_kept.begin(), g_kept.end(), [](const Kept &a, const Kept &b) { return a.used < b.used; });
            } else {
                slot = &g_kept.emplace_back();
            }
        }
        slot->game = game;
        slot->stage = StageNow();
        slot->state = g;
        slot->used = ++g_uses;
        machine.KeepStage(slot->data);
        if (getenv("SFIII3_STAGE_KEEP")) { // (debugging aid)
            size_t all = 0;
            for (const auto &one : g_kept) {
                all += one.data.size();
            }
            fprintf(stderr, "stages: kept stage %d at frame %llu (%zu kept, %zu MB)\n", StageNow(), static_cast<unsigned long long>(machine.frame),
                    g_kept.size(), all >> 20);
        }
    }
    // The board put back as it was in that stage's fight, if it has been there (true)
    bool PutBack(Machine &machine, const void *game, int stage) {
        for (Kept &one : g_kept) {
            if (one.game == game && one.stage == stage && !one.data.empty()) {
                const int loaded = g.loaded;
                machine.RestoreStage(one.data);
                g = one.state;
                g.loaded = loaded;
                one.used = ++g_uses;
                return true;
            }
        }
        return false;
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
        g.inFight++;
        // (both its fighters a player's, not its computer's, so that with no controls they stand still: the record's byte 3,
        // as in 3rd Strike, 0 the computer; a computer opponent would fight on, supers and all, behind 3rd Strike's)
        for (const uint32_t at : game.players) {
            ram[(at + 3) & 0x7FFFF] = 1;
        }
        if (!g.offsetKnown) {
            // (its fighters' places as its fight begins, once its game has put them: its own coordinates against its camera's)
            const int one = static_cast<int16_t>(Word(ram, game.players[0] + 0x64)), two = static_cast<int16_t>(Word(ram, game.players[1] + 0x64));
            if (one == 0 && two == 0) {
                return;
            }
            g.offsetKnown = true;
            g_keepNow = true; // (kept after this frame: StagesBeforeFrame)
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
            if (at) {
                ram[at & 0x7FFFF] = 99; // (3rd Strike's two: the clock shown and the one whose end is the time over)
            }
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
        bool kept = false, ownCoordinates = false;
        if (game.rightAt) {
            const int right = static_cast<int16_t>(Word(ram, game.rightAt)), left = static_cast<int16_t>(Word(ram, game.leftAt));
            if (right != g.madeRight || left != g.madeLeft) {
                g.ownRight = right;
                g.ownLeft = left;
            }
            // (in its camera's coordinates, or on a stage whose fighters' are another's, in theirs: the rainy London
            // stage's -240 and 240; see `offset`)
            ownCoordinates = g.ownLeft <= 100 && g.offset != 0;
            kept = g.ownLeft + (ownCoordinates ? g.offset : 0) > 100 && g.ownRight - g.ownLeft > 2 * margin + 64;
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
        const int shift = ownCoordinates ? g.offset : 0;
        int theirRight = kept ? g.madeRight + shift : game.right - margin;
        int theirLeft = kept ? g.madeLeft + shift : game.left + margin;
        if (g.realLeft && g.realLeft + margin > theirLeft && g.realLeft + margin < theirRight) {
            theirLeft = g.realLeft + margin;
        }
        if (g.realRight && g.realRight - margin < theirRight && g.realRight - margin > theirLeft) {
            theirRight = g.realRight - margin;
        }
        if (right > left && theirRight > theirLeft) {
            // (its game makes its camera from its fighters each frame, whatever its camera words say: so its fighters,
            // unseen, are put where 3rd Strike's are, from one stage to the other, and its camera follows them)
            // (their middle, which its camera follows, kept between the ends it was given: near a stage's end 3rd Strike's
            // fighters' middle is past its camera's limit, and a game that keeps its own limits would follow them there,
            // past the narrowed ones, and show past its stage's end in widescreen)
            int placed[2];
            for (int p = 0; p < 2; p++) {
                const int x = static_cast<int16_t>(Ram16(sfiii3::kPlayer[p] + sfiii3::kX));
                placed[p] = theirLeft + (x - left) * (theirRight - theirLeft) / (right - left);
            }
            const int middle = (placed[0] + placed[1]) / 2;
            const int push = middle < theirLeft ? theirLeft - middle : middle > theirRight ? theirRight - middle : 0;
            for (int p = 0; p < 2; p++) {
                SetWord(ram, game.players[p] + 0x64, placed[p] + push - g.offset);
            }
            int theirs = theirLeft + (std::clamp(camera, left, right) - left) * (theirRight - theirLeft) / (right - left);
            if (g_cameraWanted != kFollow) {
                theirs = std::clamp(g_cameraWanted, theirLeft, theirRight); // (the modes' scrolling stage: its camera its own)
                for (int p = 0; p < 2; p++) {
                    placed[p] = theirs + (p ? 40 : -40);
                    SetWord(ram, game.players[p] + 0x64, placed[p] - g.offset);
                }
            }
            g_theirLeft = theirLeft;
            g_theirRight = theirRight;
            // (its camera held short of where it was sent, frame after frame: that is its end)
            const int had = g.lastCamera;
            if (std::abs(theirs - had) > 8 && had == g.stuckAt) {
                if (++g.stuck > 30) {
                    (theirs < had ? g.realLeft : g.realRight) = had;
                    g.stuck = 0;
                }
            } else {
                g.stuck = 0;
            }
            g.stuckAt = had;
            g.given = theirs;
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
namespace {
    // Another stage of the board's game made ready beside the one shown (the modes' next stage): the board lent to it
    // for a few frames each frame, its own put back after
    struct Making {
        bool on = false, ready = false;
        int choice = 0;
        State state;
        std::vector<uint8_t> data;
        // (made on the second stage board, beside the first, to come in as the first scrolls out: its own camera wanted,
        // and the ends its camera is kept between)
        bool second = false;
        int cameraWanted = 0;
        int left = 0, right = 0;
    } g_make;
    std::vector<uint8_t> g_lent;

    // Before each of the second board's frames: its own state the live one for BeforeStage, and kept once in its fight
    void BeforeSecondStage(Machine &machine, machine::Machine &stage) {
        if (!g_make.on || !g_make.second) {
            return;
        }
        std::swap(g, g_make.state);
        std::swap(g_cameraWanted, g_make.cameraWanted);
        const int left = g_theirLeft, right = g_theirRight;
        const bool keepNow = g_keepNow;
        g_keepNow = false;
        BeforeStage(machine, stage);
        if (g_keepNow && StageNow() >= 0) {
            // (in its fight: kept, as the first board's stages are, to be there at once the next time)
            Kept *slot = nullptr;
            for (Kept &one : g_kept) {
                if (one.game == kChoices[g.loaded].game && one.stage == StageNow()) {
                    slot = &one;
                }
            }
            if (!slot && g_kept.size() < 16) {
                slot = &g_kept.emplace_back();
            }
            if (slot) {
                slot->game = kChoices[g.loaded].game;
                slot->stage = StageNow();
                slot->state = g;
                slot->used = ++g_uses;
                machine.KeepSecondStage(slot->data);
            }
        }
        g_keepNow = keepNow;
        g_make.left = g_theirLeft;
        g_make.right = g_theirRight;
        g_theirLeft = left;
        g_theirRight = right;
        g_make.ready = g.fought && g.offsetKnown && g.inFight > kMadeFor;
        std::swap(g_cameraWanted, g_make.cameraWanted);
        std::swap(g, g_make.state);
    }
    void MakeOn(Machine &machine) {
        if (!g_make.on || g_make.second || g_make.ready || !machine.StageBoard() || !g.loaded || kChoices[g_make.choice].game != kChoices[g.loaded].game) {
            return;
        }
        machine.KeepStage(g_lent);
        const State live = g;
        if (g_make.data.empty()) {
            machine.StageBoard()->Reset();
        } else {
            machine.RestoreStage(g_make.data);
        }
        g = g_make.state;
        machine.RunStageQuiet(8);
        g_keepNow = false; // (kept here, as it is made)
        g_make.state = g;
        machine.KeepStage(g_make.data);
        g_make.ready = g.fought && g.offsetKnown && g.inFight > kMadeFor; // (well past its banners: kept as it is, it goes on from there)
        machine.RestoreStage(g_lent);
        g = live;
    }
    bool IsKept(const void *game, int stage) {
        for (const auto &one : g_kept) {
            if (one.game == game && one.stage == stage) {
                return true;
            }
        }
        return false;
    }
    // The board back to its own stage (after preloading another): as kept, or (none) left as it is
    void EndPreload(Machine &machine) {
        if (!g_pre.on) {
            return;
        }
        g_pre.on = false;
        const State live = g_pre.live;
        if (live.stage >= 0 && PutBack(machine, kChoices[live.loaded].game, live.stage)) {
            return;
        }
        g = live;
        if (live.stage >= 0) {
            g.fought = false; // (not kept: taken to its fight again)
            g.offsetKnown = false;
            g.frames = 0;
            machine.StageBoard()->Reset();
        }
    }
} // namespace

void StagesBeforeFrame(Machine &machine) {
    if (g_keepNow) {
        g_keepNow = false;
        if (g.loaded && machine.StageBoard()) {
            Keep(machine);
            if (g_pre.on) {
                EndPreload(machine); // (that stage kept: the board's own back)
            }
        }
    }
    // (a round about to be on the stage while preloading: the board's own back at once)
    if (g_pre.on && ((OnStage() && g_pre.live.stage >= 0) || !FeatureOn("stages_preload"))) {
        EndPreload(machine);
    }
    // (in a fight on the stage board's stage: its music, 3rd Strike's left out; the effects and voices are 3rd Strike's)
    // (the custom soundtrack's file, when one plays, in place of both)
    const bool music = g_boardMusic && !g_pre.on && g.loaded && g.fought && OnStage() && machine.StageBoard() && !machine.soundtrack.Playing() &&
                       (kChoices[g.loaded].stage != kOpponents || g.stage >= 0);
    machine.stageSound = music;
    int silenced = 0;
    for (int v = 0; v < 16; v++) {
        const bool musicElsewhere = music || machine.soundtrack.Playing();
        // (the stage viewer: only music heard, the unseen fight's voices and effects left out)
        machine.board.silenced[v] = (musicElsewhere && MusicVoice(v)) || (g_onlyMusic && (musicElsewhere || !MusicVoice(v)));
        silenced += machine.board.silenced[v];
    }
    // (debugging aid, SFIII3_STAGE_SOUND=1: every 300 frames, the voices left out and the stage board's sound)
    static const bool log = getenv("SFIII3_STAGE_SOUND") != nullptr;
    if (log && machine.frame % 300 == 0) {
        fprintf(stderr, "stage sound: frame %llu, %s, %d of 3rd Strike's voices left out as music\n", static_cast<unsigned long long>(machine.frame),
                music ? "the stage's music" : "3rd Strike's own", silenced);
    }
    const int menu = std::clamp(FeatureValue("stages"), 0, static_cast<int>(std::size(kNames)) - 1);
    const int choice = g_choiceWanted ? g_choiceWanted : menu;
    MakeOn(machine);
    // (between fights; or in one on 3rd Strike's own stage, when the board is idle and may preload)
    const bool idle = !OnStage() || (g_pre.on ? g_pre.live.stage : g.stage) < 0;
    if (choice == g.loaded && choice && kChoices[choice].stage == kOpponents && machine.StageBoard() && idle) {
        // (the opponent's: between fights, 3rd Strike's next stage chosen, the board started again on its way to that one)
        const int most = kChoices[choice].game == &kNewGeneration ? 12 : 14;
        // (New Generation's 6, Hugo's number though it has no Hugo, is a beer festival: Hugo's, as Germany's)
        const int next = Ram8(sfiii3::kStage) <= most ? Ram8(sfiii3::kStage) : -1;
        if (g_pre.on && next != g_pre.live.stage) {
            EndPreload(machine); // (the next fight's stage is another: that one wanted now)
        }
        if (!g_pre.on && next == g.stage && FeatureOn("stages_preload") && (g.fought || g.stage < 0)) {
            // (idle: another of this game's stages not kept yet taken to its fight meanwhile)
            for (int stage = 0; stage <= most; stage++) {
                if (!IsKept(kChoices[choice].game, stage)) {
                    g_pre.on = true;
                    g_pre.live = g;
                    const int loaded = g.loaded;
                    g = {};
                    g.loaded = loaded;
                    g.stage = stage;
                    machine.StageBoard()->Reset();
                    break;
                }
            }
            return;
        }
        if (g_pre.on) {
            return; // (preloading goes on)
        }
        if (next != g.stage) {
            if (!PutBack(machine, kChoices[choice].game, next)) {
                const int loaded = g.loaded;
                g = {};
                g.loaded = loaded;
                g.stage = next;
                machine.StageBoard()->Reset();
            }
            g.stage = next;
        }
        return;
    }
    if (choice == g.loaded) {
        return;
    }
    // (another stage of the game whose board is there: put back if it has been there, else that board started again;
    // another game: its set loaded)
    if (choice && g.loaded && kChoices[choice].game == kChoices[g.loaded].game && machine.StageBoard()) {
        const int was = g.loaded;
        g.loaded = choice;
        const int stage = StageNow();
        if (!PutBack(machine, kChoices[choice].game, stage)) {
            g = {};
            g.loaded = choice;
            g.stage = kChoices[choice].stage == kOpponents ? Ram8(sfiii3::kStage) : -1;
            machine.StageBoard()->Reset();
        }
        (void)was;
        return;
    }
    g = {};
    g_pre = {};
    g.loaded = choice;
    g_kept.clear();
    machine.SetStageBoard(choice ? LoadSet(*kChoices[choice].game) : nullptr);
    if (choice && !machine.StageBoard()) {
        g.loaded = 0; // (not there: as without it)
        SetFeatureValue("stages", 0);
    }
}

// The modes' stage behind their fighters (battle.cpp): 3rd Strike's own stage `stage` drawn from the stage board (-1:
// none, the menu's choice again)
void StageBehind(int stage) {
    g_choiceWanted = stage >= 0 ? kOwnFirst + std::min(stage, 20) : 0;
    g_make = {};
    if (Machine *machine = TheMachine()) {
        machine->SetSecondStageBoard(false); // (a stage scrolling in no longer)
    }
    g_cameraWanted = kFollow;
    g_boardMusic = true;
    g_onlyMusic = false;
}
// ... nothing but music heard (the stage viewer)
void StageBehindOnlyMusic(bool on) {
    g_onlyMusic = on;
}
// ... its music heard (the default), or 3rd Strike's fight's (the stage viewer's other tunes)
void StageBehindMusic(bool on) {
    g_boardMusic = on;
}
// ... its camera's middle wanted (the scrolling stage), or 3rd Strike's followed (kFollow); and the ends it is kept
// between (false: none yet)
void StageBehindCamera(int x) {
    g_cameraWanted = x;
}
int StageBehindFollow() {
    return kFollow;
}
bool StageBehindEnds(int &left, int &right) {
    left = g_theirLeft;
    right = g_theirRight;
    return g.fought && g.offsetKnown && right > left;
}
bool StageBehindShown() {
    return g.loaded && g.fought && g.offsetKnown && g.inFight > kBannersFor;
}
// The next stage made ready beside it, and put in its place when ready
void StageBehindNext(int stage) {
    g_make = {};
    g_make.on = true;
    g_make.choice = kOwnFirst + std::min(stage, 20);
    g_make.state.loaded = g_make.choice;
}
bool StageBehindNextReady() {
    return g_make.on && g_make.ready;
}
// ... or made on the second stage board, beside the first, to scroll in as the first goes out (its camera at its left end
// while it comes in: StageBehindNextCamera); its ends once known
void StageBehindNextBeside(Machine &machine, int stage) {
    g_make = {};
    g_make.on = true;
    g_make.second = true;
    g_make.choice = kOwnFirst + std::min(stage, 20);
    g_make.state.loaded = g_make.choice;
    g_make.cameraWanted = -99999; // (its left end, until it is told: StageBehindNextCamera)
    machine.SetSecondStageBoard(true);
    if (!machine.SecondStageBoard()) {
        g_make = {};
        return;
    }
    // (a stage that has been there before: as it was then, at once)
    for (Kept &one : g_kept) {
        if (one.game == kChoices[g_make.choice].game && one.stage == std::min(stage, 20) && !one.data.empty()) {
            machine.RestoreSecondStage(one.data);
            g_make.state = one.state;
            g_make.state.loaded = g_make.choice;
            one.used = ++g_uses;
            return;
        }
    }
    machine.SecondStageBoard()->Reset();
}
void StageBehindNextCamera(int x) {
    g_make.cameraWanted = x;
}
bool StageBehindNextEnds(int &left, int &right) {
    left = g_make.left;
    right = g_make.right;
    return g_make.on && g_make.second && g_make.ready && right > left;
}
// The second board's picture coming in: `columns` of it at the right, its sound `gain` of 256
void StageBehindSlide(Machine &machine, int columns, int gain) {
    machine.stageSlide = columns;
    machine.secondStageGain = gain;
}
// The second board is the stage now
void StageBehindSlideSwitch(Machine &machine) {
    if (!g_make.on || !g_make.second) {
        return;
    }
    machine.SwapStageBoards();
    g = g_make.state;
    g_choiceWanted = g_make.choice;
    g_cameraWanted = g_make.cameraWanted;
    g_theirLeft = g_make.left;
    g_theirRight = g_make.right;
    g_make = {};
}
// The second board no longer wanted (the mode over)
void StageBehindNoSecond(Machine &machine) {
    if (g_make.second) {
        g_make = {};
    }
    machine.SetSecondStageBoard(false);
}
void StageBehindSwitch(Machine &machine) {
    if (!StageBehindNextReady()) {
        return;
    }
    machine.RestoreStage(g_make.data);
    g = g_make.state;
    g_choiceWanted = g_make.choice;
    g_make = {};
}

void RegisterStages(Game &game) {
    game.features.push_back({"stages", "Display", "Stages (experiment)",
                             "The definitive version's stages, an early form: a fight drawn on one of New Generation's or 2nd "
                             "Impact's stages, by running that game beside this one (its set beside this one's, sfiiin.zip or sfiii2n.zip). "
                             "In a fight its music plays in place of 3rd Strike's. It takes a few seconds to reach its stage after it is chosen. "
                             "The opponent's: each fight on that game's stage of the character whose stage 3rd Strike chose (its own "
                             "for the characters that game has not).",
                             kFeatureMenuChoices, {std::begin(kNames), std::end(kNames)}});
    game.features.push_back({"stages_preload", "Display", "Stages: load ahead",
                             "With the opponent's stages: between fights, the game's other stages are taken to their fights one by "
                             "one and kept, so that each opponent's is there at once. It takes the processor's time while no fight "
                             "is on (the stage game run fast), and about 150 MB of memory.",
                             kFeatureEnhanced});
    if (Machine *machine = TheMachine()) {
        machine->stageObject = StageObject;
        machine->ownStageObject = OwnStageObject;
        machine->beforeStage = BeforeStage;
        machine->beforeSecondStage = BeforeSecondStage;
        // (the second board, until it is ready (past its fight's banners): eight frames a frame, so that it is there before
        // the stage shown has been crossed even at the fastest scroll)
        machine->secondStageFramesNow = [] { return g_make.on && g_make.second && !g_make.ready ? 8 : 1; };
        // (composed while 3rd Strike's round is on and the board is in its fight: for the opponent's, on a stage it has)
        machine->composeNow = [] { return !g_pre.on && OnStage() && g.fought && (kChoices[g.loaded].game != &kThirdStrike || g.inFight > kBannersFor) && (kChoices[g.loaded].stage != kOpponents || g.stage >= 0); };
        // (on its way to its fight: eight frames a frame; in it one, its own pace (its animations and music))
        machine->stageFramesNow = [] { return g.fought ? 1 : 8; };
    }
}

} // namespace cps3recomp
