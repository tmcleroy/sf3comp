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
#include "png_write.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <string>
#include <filesystem>
#include <memory>

namespace cps3recomp {

namespace {
    // A stage game's stage from its own stage code alone (DEFINITIVE.md, "The stage alone"): its board started once and
    // kept where its game task runs the game's modes each frame (`modes`: the routines called there, by the task's
    // first step, the title's and attract mode's and the game's), which are replaced: no mode, no character select, no
    // fighters and no match ever run. The first time the board gets there (its start-up done: about 400 of its frames)
    // it is kept, and every stage starts from there. Once, the stage is built (its number put where the stage's choice
    // puts it and the match's set-up's clearing done, `beforeBuild`, then the routines `build`); then each frame its
    // stage routines (`frame`, each with its r4: the stage's frame, then the effects' queues and lists, where the
    // stage's people and things are) run, and the game task goes on as it does after the modes (the objects' lists
    // flipped, its wait for the frame). The board's vertical blank hands the frame over (the video hand-offs,
    // video_games.cpp, to the stage board's own interface) and runs its sound driver, as the game's own does; its DMA
    // interrupt chains the textures and colours the stage asked for. The stage's music is asked for by the game's own
    // routine for it (`music`: the stage's tune by its number), once the stage's frame runs (its head's step, `stepAt`,
    // at `frameStep`). Nothing called may wait for anything (Guest::Call: no time passes for the rest of the board).
    struct Call {
        uint32_t at;
        int32_t r4 = -1; // (-1: r4 left as it is)
    };
    struct Routine {
        uint32_t at, size; // (the bytes hashed)
        uint64_t hash;
    };
    struct StageAlone {
        Routine modes[2]; // (the game task's modes by its first step: the title and attract mode's, and the game's)
        uint32_t screenSetUp; // (jumped to first, a frame before the build: it waits for things, so it is not called)
        void (*beforeBuild)(Guest &g, int stage);
        std::vector<Call> build, frame;
        uint32_t music; // (called with r4 the stage's number)
        uint32_t stepAt; // byte: the stage's head's step (bg_routine): from `frameStep` on, its frame
        uint8_t frameStep;
        // For the check against a match played behind the stage (SFIII3_STAGE_COMPARE=1): the game's steps (three
        // words: the task's, the game's, the match's) as they are before the frame in which the match is set up (from
        // then the stage is built), the stage's frame routine (where what the match hands the stage is taken: hashed),
        // and what the match hands it: the fighters' records, and the stage's places the match sets (by address and
        // size), given to the stage alone as the match's stage took them
        uint32_t steps;
        uint16_t matchSetUp[3];
        Routine stageFrame;
        std::vector<std::pair<uint32_t, uint32_t>> matchHands;
    };
    // New Generation's (found from 3rd Strike's, whose routines its are: the game task 0x0602B3F4, its modes by its first
    // step 0x02012BEC from a table, 0x0602B454 the title's and attract mode's, 0x0602B9BE the game's; the match's set-up
    // 0x0602BFC0 and frame 0x0602C04E, of which these are the stage's parts). Built: the effects' lists emptied
    // (0x0609104C), the stage's state cleared (four words and a byte as the match's set-up clears them, then 0x0607C054,
    // 0x06079CB8) and its first step (0x06079FF0: the stage's frame, by its head's step: 0 its set-up, the layers, the
    // textures and colours asked for; then its frame). Each frame: the stage's frame (0x06079FF0), the effects' two
    // queues (0x0608EF88) and seven lists (0x06090F34: the stage's people and things are effects). Before all of it, the
    // match's screen set-up first (0x0602A69E: the DMA queue, character RAM's areas, the object lists and layers made
    // afresh; it waits for the object DMA and a frame, so it is jumped to, the frame before). The stage's tune:
    // 0x0602940C (by its head's stage number, from a table).
    const StageAlone kNewGenerationAlone = {
        {{0x0602B454, 0x40, 0x82E35830E6258E42ull}, {0x0602B9BE, 0x40, 0x45AC4B2E469B5E9Aull}},
        0x0602A69E,
        [](Guest &g, int stage) {
            // (the stage's number and its part, at its head's +4 and +5, as the stage's choice puts them, and the
            // game's stage byte)
            g.Write8(0x0201BE14, static_cast<uint8_t>(stage));
            g.Write8(0x0201BE15, 0);
            g.Write8(0x02012CA3, static_cast<uint8_t>(stage));
            for (uint32_t at = 0x0200D17C; at < 0x0200D184; at += 2) {
                g.Write16(at, 0);
            }
            g.Write8(0x0200EACC, 1);
        },
        {{0x0608BC5E, 1}, {0x0609104C}, {0x0607C054}, {0x06079CB8}, {0x06079FF0}},
        {{0x06079FF0}, {0x0608EF88, 0}, {0x0608EF88, 1}, {0x06090F34, 0}, {0x06090F34, 1}, {0x06090F34, 2}, {0x06090F34, 3},
         {0x06090F34, 4}, {0x06090F34, 5}, {0x06090F34, 6}},
        0x0602940C,
        0x0201BE10, 1, // (its stage's step: 0 its set-up, then its frame; 3rd Strike's sets up over three)
        // (the game task's step 1, the game's 2 (the match), the match's 0 (its set-up, 0x0602BFC0); the fighters' two
        // records (0x3D8 each); the bytes and words the stage's set-up clears that the match sets (0x0607C054: the
        // camera held and its reasons, the scripted moves' speeds, the zoom's asks); the stage head's +0x22-0x2D (its
        // moves stopped, scripted moves, shakes); the dice's counters (0x06009DA0 and its kin: the stage's people draw
        // from the same dice as the fighters); the hit stop's and the frame count's words (0x06028EBC))
        0x02012BEC, {1, 2, 0}, {0x06079FF0, 0x24, 0x0989F8913F36E7B8ull},
        {{0x0200D18C, 0x7B0}, {0x0200D18A, 1}, {0x0201D270, 0x1C}, {0x0201D5AE, 2}, {0x0201D5BC, 3}, {0x0201D5C0, 2}, {0x0201BE32, 12}, {0x02012DAC, 4}, {0x0201371A, 4}, {0x0200EB10, 0x20}},
    };

    // 2nd Impact's, the same (its game task 0x06084BFC, its modes by 0x02014230: 0x06084C5C the title's and attract
    // mode's, 0x06085298 the game's; the match's set-up 0x06085894 and frame 0x06085916). The screen set up afresh
    // (0x06083B8E, jumped to); built: the effects' lists emptied (0x060C072C), the stage's state cleared (four words and a
    // byte, then 0x060A2438, 0x060A0158) and its first step (0x060A0480, as New Generation's: 0 its set-up, then its
    // frame). Each frame: 0x060A0480, the effects' queues (0x060BE46C) and lists (0x060C0614). Its tune: 0x06082848, r4
    // the stage's number (the stage's tune from a table, as the round's management asks for it).
    const StageAlone kSecondImpactAlone = {
        {{0x06084C5C, 0x40, 0xE93B3772CD13B1AFull}, {0x06085298, 0x40, 0x9DA3750CF17B9497ull}},
        0x06083B8E,
        [](Guest &g, int stage) {
            g.Write8(0x02022164, static_cast<uint8_t>(stage));
            g.Write8(0x02022165, 0);
            g.Write8(0x020142E7, static_cast<uint8_t>(stage));
            for (uint32_t at = 0x0200E4F4; at < 0x0200E4FC; at += 2) {
                g.Write16(at, 0);
            }
            g.Write8(0x020100D8, 1);
        },
        {{0x060C072C}, {0x060A2438}, {0x060A0158}, {0x060A0480}},
        {{0x060A0480}, {0x060BE46C, 0}, {0x060BE46C, 1}, {0x060C0614, 0}, {0x060C0614, 1}, {0x060C0614, 2}, {0x060C0614, 3},
         {0x060C0614, 4}, {0x060C0614, 5}, {0x060C0614, 6}},
        0x06082848,
        0x02022160, 1,
        // (the check: as New Generation's, its places: the fighters' records 0x0200E504 and 0x0200E910, the camera held
        // and the zoom's asks (cleared by 0x060A2438), the head's +0x22-0x2D, the dice's counters, the hit stop's words)
        0x02014230, {1, 2, 0}, {0x060A0480, 0x24, 0x42CD88E3FF97B124ull},
        {{0x0200E504, 0x818}, {0x0200E502, 1}, {0x020235C8, 0x1C}, {0x02023906, 2}, {0x02023914, 3}, {0x02023918, 2}, {0x02022182, 12},
         {0x020143F0, 4}, {0x0201552E, 4}, {0x02010140, 0x24}},
    };

    // Where a stage game keeps what is held (found in RAM traces: DEFINITIVE.md)
    struct StageGame {
        const char *set;
        uint32_t camera[3], edge[2]; // the camera's middle (and copies: its fighters' layer's place across, the second of its
                                     // stage's seven layers), its left edge (none: 0)
        uint32_t rightAt, leftAt;    // the stage's ends, if the game keeps them (else right, left)
        int right, left;
        uint32_t clock[2], life[6], stage;
        uint32_t players[2]; // the players' records (the place across the stage at +0x64, up at +0x68, as in 3rd Strike)
        int32_t upSpeed;     // how far its camera goes up for a line of its higher fighter's height past 88 (16.16)
        int cameraLayer;     // which of its stage's seven layers is the camera's (the stage's head 0x54 before the first)
        const StageAlone *alone = nullptr; // (its stage from its stage code alone: none, a match played behind it)
    };
    // (its fighters' layer: the camera's middle across at +0x20 of it, as in 3rd Strike; its place up at +0x1C shown and
    // +0x24, its top at +0x6E: stage_camera.cpp)
    constexpr uint32_t kSpeedUp = 0x14, kShownUp = 0x1C, kUp = 0x24, kHomeUp = 0x4A, kTopEnd = 0x6E;
    constexpr uint32_t kSteps = 0x04;   // (a layer's step, 0 not begun)
    constexpr uint32_t kMovedUp = 0x14; // (of the stage's head, 0x54 before its layers: how far up the camera's layer is from its start)
    const StageGame kNewGeneration = {"sfiiin", {0x0201BF0C, 0x0201BF14, 0x0201BF24}, {0x0201BEFE, 0x0201BF80}, 0x0201BF58, 0x0201BF5C, 680, 320,
                                      {0x0200EB33, 0x0201DB15}, {0x0200D22B, 0x0200D22D, 0x0201DAF1, 0x0200D603, 0x0200D605, 0x0201DB09},
                                      0x02012CA3, {0x0200D18C, 0x0200D564}, 0x1C000, 1, &kNewGenerationAlone};
    const StageGame kSecondImpact = {"sfiii2n", {0x0202225C, 0x02022264, 0x02022274}, {0x0202224E, 0x020222D0}, 0x020222A8, 0x020222AC, 0, 0,
                                     {0x02010167, 0x02023EF5}, {0x0200E5A3, 0x0200E5A5, 0x0200E9AF, 0x0200E9B1, 0, 0}, 0x020142E7,
                                     {0x0200E504, 0x0200E910}, 0x1C000, 1, &kSecondImpactAlone};
    // 3rd Strike's own, as a stage board behind its fighters (the modes' shuffled and scrolling stages: battle.cpp)
    const StageGame kThirdStrike = {GAME_ROM_SET, {0x02026CA8, 0x02026CB0, 0x02026CC0}, {0, 0}, 0x02026CF4, 0x02026CF8, 748, 272,
                                    {0x02011377, 0x02028679}, {0x02068D0B, 0x020691A3, 0, 0, 0, 0}, 0x020154F5, {0x02068C6C, 0x02069104}, 0x1C000, 1};
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
        int heldAt = -1;     // (a place its camera stayed put at when sent either way: held there, not an end; -1 none)
        int ownRight = 0, ownLeft = 0, madeRight = 0, madeLeft = 0; // its stage's ends as its game set them, and as made
        int stage = -1;      // (the opponent's: the stage its board is on its way to, or in; -1 none)
        bool offsetKnown = false;
        uint32_t inFight = 0; // its frames in its fight (its round's banners over by kBannersFor)
        int offset = 0;      // its camera's middle less its fighters' as its fight began (New Generation's rainy London
                             // stage places its fighters about 512 left of its camera; the others about 0)
        bool screenSet = false;  // (a stage alone, StageAlone: its screen set up afresh)
        bool built = false;      // (... built on its board)
        bool musicAsked = false; // (... its tune asked for)
    } g;
    // (a stage alone: the board starting up, kept at its modes' routine the first time it is reached there: kBootFrames
    // of its frames a shown frame, about 400 in all for New Generation's)
    bool g_booting = false, g_reached = false;
    uint32_t g_bootFrames = 0;
    constexpr int kBootFrames = 16;
    constexpr uint32_t kMostBootFrames = 3000;

    // The check (SFIII3_STAGE_COMPARE=1, with a stage game's stage that is made alone): the stage board plays the match
    // behind its stage as before (the reference, shown), and from the frame its match is set up a second board, the same
    // board as it is then, makes the stage alone, given what the match hands the match's stage each frame (the
    // fighters' records, the camera held, scripted moves, shakes: StageAlone::matchHands) as it took them. Each frame
    // the two stages' layers are drawn (their stage objects and tilemaps) and compared, and 3rd Strike's picture
    // composed over each. Reported at the end.
    struct Compare {
        bool on = false;
        State state;                 // (the second board's)
        std::vector<uint8_t> handed; // (what the match's stage took this frame)
        std::vector<std::pair<uint32_t, uint32_t>> extra;
        double refStageAt = 0;    // (where in its frame the match's board ran its stage's frame, 0-1 from the vertical blank)
        uint32_t refModes = 0;    // (the match's board's calls of the game's modes since the last comparison)
        uint64_t late = 0, lateDifferent = 0; // (frames in which it did not run them: its frame ran long; of them, different)
        bool refFollows = false;
        uint64_t frames = 0, layersDifferent = 0, composedDifferent = 0, objectsDifferent = 0;
        int cameraLeast = 1 << 30, cameraMost = -(1 << 30);
        int stage = -1;
        unsigned reports = 4;
        ~Compare() {
            if (frames) {
                fprintf(stderr, "stages: stage alone against the match's (stage %d): %llu frames compared, the stage's layers %llu different, "
                                "its objects %llu different, the composed picture %llu different; its camera given %d-%d; the match's frames that ran long %llu "
                                "(%llu of them with the layers different)\n",
                        stage, static_cast<unsigned long long>(frames), static_cast<unsigned long long>(layersDifferent),
                        static_cast<unsigned long long>(objectsDifferent), static_cast<unsigned long long>(composedDifferent), cameraLeast, cameraMost,
                        static_cast<unsigned long long>(late), static_cast<unsigned long long>(lateDifferent));
            }
        }
    } g_cmp;
    bool Comparing() {
        static const bool on = getenv("SFIII3_STAGE_COMPARE") != nullptr;
        return on;
    }

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
    // The board on its way, its coins in and no game begun (kBaseAt of its frames from its reset): kept once for its
    // game, and its next trip to another stage's fight begun from there, its start-up (memory checks, warnings, its
    // logo) passed over. A trip from a reset took ~1600 of its frames, so the opponent's stage came some 200 frames
    // into 3rd Strike's round's start, 3rd Strike's own stage shown till then; from here it is in time.
    constexpr uint32_t kBaseAt = 880; // (its coins go in until 900, its Start from 920: its stage is not chosen yet)
    struct Base {
        const void *game = nullptr;
        State state;
        std::vector<uint8_t> data;
    } g_base;
    bool g_keepBaseNow = false;
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

    // 3rd Strike's own stage: a tilemap's lines, or an object with a stage's colours (palettes 0x80-0xFF; and 0x1FF, the
    // blank objects every stage has). Not the round's banners, "ROUND 1" (0x1ED) and "FIGHT!" (0x1E0), drawn with
    // palettes of their own above the stages': over another game's stage they are 3rd Strike's to show. (Every stage's
    // objects, in an endless battle on each of the 21 at two moments of a fight: palettes 0x80-0xFC, and 0x1FF.)
    // (both rules by an object's first and third words, as the board's object list and the native interface's objects
    // have them alike)
    bool OwnStageWords(uint32_t v1, uint32_t v3) {
        const uint32_t palette = v1 & 0x1FF;
        return (v3 & 3) == 0 || (palette >= 0x80 && palette < 0x100) || palette == 0x1FF;
    }
    bool OwnStageObject(const machine::Machine &m, uint32_t object) {
        const uint32_t *list = m.SpriteList();
        return OwnStageWords(list[object * 4], list[object * 4 + 2]);
    }
    // The stage board's stage: all but its fighters (6 bits a pixel and a palette below 0x40: New Generation's 0x08 and
    // 0x10, 2nd Impact's 0x00 and 0x18) and their shadows (palette 0, blending). Its stage's people and things have
    // palettes from 0x40 (or 8 bits a pixel); its HUD is in the text layer, not drawn.
    bool StageWords(uint32_t v1, uint32_t v3) {
        if (kChoices[g.loaded].game == &kThirdStrike) {
            return OwnStageWords(v1, v3);
        }
        const uint32_t palette = v1 & 0x1FF;
        const bool sixBits = (v1 >> 9) & 1, blend = (v1 >> 10) & 1;
        if ((v3 & 3) == 0) {
            return true;
        }
        return !((sixBits && palette < 0x40) || (blend && palette == 0));
    }
    bool StageObject(const machine::Machine &m, uint32_t object) {
        return StageWords(m.SpriteList()[object * 4], m.SpriteList()[object * 4 + 2]);
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

    // Whether a stage game's stage is made from its stage code alone (StageAlone; SFIII3_STAGE_MATCH=1: a match played
    // behind it, as before, for comparing the two)
    bool Alone(const StageGame &game) {
        static const bool match = getenv("SFIII3_STAGE_MATCH") != nullptr || getenv("SFIII3_STAGE_COMPARE") != nullptr;
        return game.alone && !match;
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
        // (the check: before the frame in which the match is set up, the second board made as this one is, to make the
        // stage alone from there)
        if (Comparing() && game.alone && &stage == machine.StageBoard() && !g_cmp.on) {
            const StageAlone &a = *game.alone;
            if (Word(ram, a.steps) == a.matchSetUp[0] && Word(ram, a.steps + 2) == a.matchSetUp[1] && Word(ram, a.steps + 4) == a.matchSetUp[2]) {
                std::vector<uint8_t> data;
                machine.KeepStage(data);
                machine.SetSecondStageBoard(true);
                machine.RestoreSecondStage(data);
                g_cmp.on = true;
                g_cmp.state = {};
                g_cmp.state.loaded = g.loaded;
                g_cmp.state.stage = StageNow();
                g_cmp.stage = StageNow();
                if (getenv("SFIII3_STAGE_CAMERA")) {
                    fprintf(stderr, "stages: the check's second board made, stage %d, at the stage board's frame %u\n", StageNow(), g.frames);
                }
            }
        }
        // (its stage alone: nothing played, no controls; its camera given once its stage's frame runs: AloneFrame)
        const bool alone = Alone(game) || (Comparing() && &stage == machine.SecondStageBoard());
        if (alone) {
            stage.inputs = stage.extra = 0xFFFFFFFFu;
            if (!g.fought || (Comparing() && !g_cmp.refFollows)) {
                return;
            }
        }
        if (fighting && !g.fought && g.frames > 600 && !alone) {
            g.fought = true;
            g.life = ram[game.life[0] & 0x7FFFF];

        }
        if (!g.fought) {
            if (g.frames == kBaseAt && &stage == machine.StageBoard() && &game != &kThirdStrike && g_base.game != choice.game) {
                g_keepBaseNow = true; // (kept before the next shown frame: StagesBeforeFrame)
            }
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
            if (!alone) {
                ram[(at + 3) & 0x7FFFF] = 1;
            }
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
            // (nor one with its fighters' middle between its stage's ends, where its game keeps them: their coordinates
            // are its camera's, and the camera was held off its stage as its fight began. 2nd Impact's Necro stage's
            // camera is at 250 then, its fighters either side of 512, its ends 262 and 764: taken as an offset of
            // -262, its fighters were put 262 right of where they were wanted, its camera after them every frame, 6
            // columns right of where it was sent)
            if (game.rightAt && g.offset != 0) {
                const int middle = (one + two) / 2;
                if (middle >= static_cast<int16_t>(Word(ram, game.leftAt)) && middle <= static_cast<int16_t>(Word(ram, game.rightAt))) {
                    g.offset = 0;
                }
            }
            if (getenv("SFIII3_STAGE_CAMERA")) {
                fprintf(stderr, "stage fight: camera %d, fighters %d %d: offset %d\n", static_cast<int16_t>(Word(ram, game.camera[1])), one, two, g.offset);
            }
        }
        // (debugging aid, SFIII3_STAGE_LAYERS=1: its stage's layers, every 300 frames)
        static const bool layerLog = getenv("SFIII3_STAGE_LAYERS") != nullptr;
        if (layerLog && g.frames % 300 == 0) {
            const uint32_t head = game.camera[1] - 0x20 - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
            fprintf(stderr, "stage head: number %d part %d routine %d\n", ram[(head + 4) & 0x7FFFF], ram[(head + 5) & 0x7FFFF], Word(ram, head + 0x4A));
            fprintf(stderr, "stage layers: frame %u, moved %d, up %d\n", g.frames, static_cast<int16_t>(Word(ram, head + 0x12)), static_cast<int16_t>(Word(ram, head + 0x14)));
            for (int n = 0; n < 7; n++) {
                const uint32_t l = head + 0x54 + static_cast<uint32_t>(n) * 0x90;
                fprintf(stderr, "  layer %d: family %d step %d across %d up %d home %d %d speed %08X %08X ends %d-%d top %d\n", n,
                        static_cast<int16_t>(Word(ram, l + 2)), static_cast<int16_t>(Word(ram, l + 4)), static_cast<int16_t>(Word(ram, l + 0x20)),
                        static_cast<int16_t>(Word(ram, l + 0x1C)), static_cast<int16_t>(Word(ram, l + 0x48)), static_cast<int16_t>(Word(ram, l + 0x4A)),
                        (Word(ram, l + 0x10) << 16) | Word(ram, l + 0x12), (Word(ram, l + 0x14) << 16) | Word(ram, l + 0x16),
                        static_cast<int16_t>(Word(ram, l + 0x6A)), static_cast<int16_t>(Word(ram, l + 0x66)), static_cast<int16_t>(Word(ram, l + 0x6E)));
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
            if (at && !alone) {
                ram[at & 0x7FFFF] = 99; // (3rd Strike's two: the clock shown and the one whose end is the time over)
            }
        }
        for (const uint32_t at : game.life) {
            if (at && !alone) {
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
            // (in its camera's coordinates, or on a stage whose fighters' are another's, in theirs; see `offset`. A stage
            // may have its middle at 0: 2nd Impact's Ryu stage's ends are -202 and 203)
            ownCoordinates = g.ownLeft <= 100 && g.offset != 0;
            kept = g.ownRight - g.ownLeft > 2 * margin + 64;
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
            // (mapped as the camera is, from one stage's ends to the other's; but on a stage wider than 3rd Strike's, each
            // only as far from its camera as 3rd Strike's is from 3rd Strike's: mapped, its fighters stood further
            // apart, past the quarters of the picture its camera keeps them in, and its camera went on after them, 6
            // columns a frame: 2nd Impact's Ken and Necro stages, 1.25 and 1.4 times as wide, 3-4 columns off on
            // average)
            int theirs = theirLeft + (std::clamp(camera, left, right) - left) * (theirRight - theirLeft) / (right - left);
            const bool wider = theirRight - theirLeft > right - left;
            int placed[2];
            for (int p = 0; p < 2; p++) {
                const int x = static_cast<int16_t>(Ram16(sfiii3::kPlayer[p] + sfiii3::kX));
                placed[p] = wider ? theirs + x - std::clamp(camera, left, right) : theirLeft + (x - left) * (theirRight - theirLeft) / (right - left);
            }
            const int middle = (placed[0] + placed[1]) / 2;
            const int push = middle < theirLeft ? theirLeft - middle : middle > theirRight ? theirRight - middle : 0;
            for (int p = 0; p < 2; p++) {
                SetWord(ram, game.players[p] + 0x64, placed[p] + push - g.offset);
            }
            // (3rd Strike between rounds, its camera's ends not its stage's yet (1 and 95 seen, before a round's start):
            // its middle, where 3rd Strike's camera begins a round, its fighters either side of it. Followed from
            // those ends, it went to its right end, and as the round began panned back across for some 40 frames
            // over the round's start; and held at its end it learned that place as its end, the stage cut short.)
            const bool ownEnds = OnStage() && right - left >= 128;
            if (!ownEnds && g_cameraWanted == kFollow) {
                theirs = (theirLeft + theirRight) / 2;
                for (int p = 0; p < 2; p++) {
                    SetWord(ram, game.players[p] + 0x64, theirs + (p ? 40 : -40) - g.offset);
                }
            }
            if (g_cameraWanted != kFollow) {
                theirs = std::clamp(g_cameraWanted, theirLeft, theirRight); // (the modes' scrolling stage: its camera its own)
                for (int p = 0; p < 2; p++) {
                    placed[p] = theirs + (p ? 40 : -40);
                    SetWord(ram, game.players[p] + 0x64, placed[p] - g.offset);
                }
            }
            // Up as 3rd Strike's camera is (its fighters' layer's place up, a line for a line: Oro's entrance on his stage,
            // which starts in the sky, a jump): its game makes that from its fighters too (the higher one's height past 88,
            // times its speed up, as far as its camera's layer's top), so its fighters, unseen, are put as high as that
            // needs; and its layers' places up are given too, for the frames its game holds its camera (its own round's
            // start, as 3rd Strike's intros play)
            const int up = g_cameraWanted == kFollow ? std::max(0, static_cast<int>(static_cast<int16_t>(Ram16(sfiii3::kCameraY)))) : 0;
            const int top = static_cast<int16_t>(Word(ram, game.camera[1] - 0x20 + kTopEnd));
            const int wantedUp = std::clamp(up, 0, std::max(top, 0));
            const int height = wantedUp > 0 ? 88 + static_cast<int>((static_cast<int64_t>(wantedUp) * 65536 + game.upSpeed - 1) / game.upSpeed) : 0;
            for (int p = 0; p < 2; p++) {
                SetWord(ram, game.players[p] + 0x68, height);
            }
            const uint32_t fighters = game.camera[1] - 0x20;
            for (const uint32_t at : {fighters + kShownUp, fighters + kUp}) {
                SetWord(ram, at, wantedUp);
                SetWord(ram, at + 2, 0);
            }
            // (and how far up it is for its other layers, and their places up from that, as its game makes them each
            // frame (stage_camera.cpp's LayerUp), which it does not while its camera is held; the layers in use have
            // begun their steps)
            const uint32_t head = fighters - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
            const int moved = wantedUp - static_cast<int16_t>(Word(ram, fighters + kHomeUp));
            SetWord(ram, head + kMovedUp, moved);
            for (int n = 0; n < 7; n++) {
                const uint32_t layer = head + 0x54 + static_cast<uint32_t>(n) * 0x90;
                if (layer == fighters || Word(ram, layer + kSteps) == 0) {
                    continue;
                }
                const uint32_t speed = (static_cast<uint32_t>(Word(ram, layer + kSpeedUp)) << 16) | Word(ram, layer + kSpeedUp + 2);
                uint32_t place = speed * static_cast<uint32_t>(moved);
                const int topEnd = static_cast<int16_t>(Word(ram, layer + kTopEnd));
                if (topEnd < static_cast<int16_t>(place >> 16)) {
                    place = static_cast<uint32_t>(topEnd) << 16;
                }
                place = (((place >> 16) + Word(ram, layer + kHomeUp)) << 16) | (place & 0xFFFF);
                for (const uint32_t at : {layer + kShownUp, layer + kUp}) {
                    SetWord(ram, at, static_cast<int>(place >> 16));
                    SetWord(ram, at + 2, static_cast<int>(place & 0xFFFF));
                }
            }
            // (debugging aid, SFIII3_STAGE_UP=1: 3rd Strike's camera up, and its)
            static const bool upLog = getenv("SFIII3_STAGE_UP") != nullptr;
            if (upLog && (up > 0 || g.frames % 60 == 0)) {
                fprintf(stderr, "stage up: frame %u, 3rd Strike %d, its %d (wanted %d, its top %d), its fighters' %d %d\n", g.frames, up,
                        static_cast<int16_t>(Word(ram, game.camera[1] + 4)), wantedUp, top, static_cast<int16_t>(Word(ram, game.players[0] + 0x62)),
                        static_cast<int16_t>(Word(ram, game.players[1] + 0x62)));
            }
            g_theirLeft = theirLeft;
            g_theirRight = theirRight;
            // (its camera held short of where it was sent, frame after frame: that is its end)
            const int had = g.lastCamera;
            // (but one it stays put at when sent the other way too is not an end: its game holds its camera there, or
            // moves it only as its fighters come near its picture's edges; an end learned there, the stage cut short to
            // nothing and its camera kept there the rest of the match, is forgotten, and none is learned there again
            // until its camera has moved away)
            if (g.heldAt >= 0 && std::abs(had - g.heldAt) > 16) {
                g.heldAt = -1;
            }
            if (std::abs(theirs - had) > 8 && had == g.stuckAt && (ownEnds || g_cameraWanted != kFollow)) {
                if (++g.stuck > 30) {
                    int &end = theirs < had ? g.realLeft : g.realRight, &other = theirs < had ? g.realRight : g.realLeft;
                    if (other && std::abs(other - had) <= 8) {
                        other = 0;
                        g.heldAt = had;
                    } else if (g.heldAt < 0) {
                        end = had;
                    }
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
            static const bool everyFrame = getenv("SFIII3_STAGE_CAMERA_ALL") != nullptr;
            if (log && (g.frames % 60 == 0 || everyFrame)) {
                fprintf(stderr, "stage camera: 3rd Strike %d (%d-%d), given %d (%d-%d), it had %d (in its fight %u, ends learned %d %d)\n", camera, left,
                        right, theirs, theirLeft, theirRight, g.lastCamera, g.inFight, g.realLeft, g.realRight);
            }
            for (const uint32_t at : game.edge) {
                if (at) {
                    SetWord(ram, at, theirs - 192);
                }
            }
        }
    }

    // The stage game's modes' routine (StageAlone::modes) on the stage board, its stage made alone: in its place the
    // stage's own routines (its build once, then its frame); while the board starts up, nothing (it is kept there, the
    // first time it gets there). Then back to the game task, which goes on as after its modes
    bool AloneFrame(Guest &guest) {
        Machine *machine = TheMachine();
        const bool second = Comparing() && g_cmp.on && machine && &guest.Board() == machine->SecondStageBoard();
        if (Comparing() && g_cmp.on && machine && &guest.Board() == machine->StageBoard() && guest.PC() == kChoices[g.loaded].game->alone->modes[1].at) {
            g_cmp.refModes++; // (the check: the match's frames that ran the game's modes)
        }
        if (!machine || (&guest.Board() != machine->StageBoard() && !second) || !g.loaded || (!Alone(*kChoices[g.loaded].game) && !second)) {
            return false; // (the game's own: a match played behind its stage)
        }
        // (the check's second board: its own state while its frame runs)
        struct Swapped {
            bool on;
            explicit Swapped(bool second) : on(second) {
                if (on) {
                    std::swap(g, g_cmp.state);
                }
            }
            ~Swapped() {
                if (on) {
                    std::swap(g, g_cmp.state);
                }
            }
        } swapped(second);
        const StageAlone &a = *kChoices[g.loaded].game->alone;
        if (second && g.built) {
            // (the check: what the match handed the match's stage this frame, given to this one)
            size_t at = 0;
            for (const auto &[place, size] : g_cmp.extra) {
                for (uint32_t n = 0; n < size && at < g_cmp.handed.size(); n++) {
                    guest.Write8(place + n, g_cmp.handed[at++]);
                }
            }
            for (const auto &[place, size] : a.matchHands) {
                for (uint32_t n = 0; n < size && at < g_cmp.handed.size(); n++) {
                    guest.Write8(place + n, g_cmp.handed[at++]);
                }
            }
        }
        const int stage = StageNow();
        if (g_booting || stage < 0) {
            g_reached = g_booting;
            guest.Return();
            return true;
        }
        const auto run = [&](const std::vector<Call> &calls) {
            for (const Call &c : calls) {
                if (c.r4 >= 0) {
                    guest.R(4) = static_cast<uint32_t>(c.r4);
                }
                guest.Call(c.at);
            }
        };
        bool backFromScreen = false;
        if (!g.screenSet && a.screenSetUp) {
            // (the screen set up afresh as the match's set-up does, its own code run (it waits for the object DMA and a
            // frame): it comes back here, to the modes' routine, where the stage is built, as the match's set-up builds
            // it after it; then back to the game task, whose place is kept on the stack meanwhile)
            g.screenSet = true;
            guest.R(15) -= 4;
            guest.Write32(guest.R(15), guest.PR());
            guest.SetPR(a.modes[1].at);
            guest.Jump(a.screenSetUp);
            return true;
        }
        if (!g.built) {
            g.built = true;
            backFromScreen = a.screenSetUp != 0;
            a.beforeBuild(guest, stage);
            run(a.build);
        } else {
            run(a.frame);
        }
        static const bool debug = getenv("SFIII3_ALONE_DEBUG") != nullptr; // (debugging aid: the stage's step, every 60 frames)
        if (debug && g.frames % 60 == 0) {
            fprintf(stderr, "stage alone: frame %u, stage %d, its step %d, fought %d, handed %zu\n", g.frames, stage, guest.Read8(a.stepAt), g.fought, g_cmp.handed.size());
        }
        // (its stage's frame running: its camera given from now on, its tune asked for)
        if (!g.fought && guest.Read8(a.stepAt) >= a.frameStep) {
            g.fought = true;
            g.offsetKnown = true;
            g.offset = 0;
        }
        if (g.fought && !g.musicAsked) {
            g.musicAsked = true;
            guest.R(4) = static_cast<uint32_t>(stage);
            guest.Call(a.music);
        }
        if (backFromScreen) {
            const uint32_t back = guest.Read32(guest.R(15));
            guest.R(15) += 4;
            guest.Jump(back);
            return true;
        }
        guest.Return();
        return true;
    }

    // (the check) The match's stage's frame routine on the stage board: what the match hands it, taken
    void MatchStageFrame(Guest &guest) {
        Machine *machine = TheMachine();
        if (!Comparing() || !g_cmp.on || !machine || &guest.Board() != machine->StageBoard() || !g.loaded || !kChoices[g.loaded].game->alone) {
            return;
        }
        g_cmp.handed.clear();
        g_cmp.refStageAt = guest.Board().FramePosition();
        static std::vector<std::pair<uint32_t, uint32_t>> extra = [] {
            // (debugging aid, SFIII3_STAGE_COMPARE_COPY=LO-HI,...: more of the match's memory handed, to find what else
            // its stage takes)
            std::vector<std::pair<uint32_t, uint32_t>> ranges;
            const char *v = getenv("SFIII3_STAGE_COMPARE_COPY");
            while (v && *v) {
                char *end;
                const uint32_t lo = static_cast<uint32_t>(strtoul(v, &end, 16));
                const uint32_t hi = *end == '-' ? static_cast<uint32_t>(strtoul(end + 1, &end, 16)) : lo + 1;
                ranges.push_back({lo, hi - lo});
                v = *end == ',' ? end + 1 : end;
            }
            return ranges;
        }();
        g_cmp.extra = extra;
        for (const auto &[place, size] : extra) {
            for (uint32_t n = 0; n < size; n++) {
                g_cmp.handed.push_back(guest.Read8(place + n));
            }
        }
        for (const auto &[place, size] : kChoices[g.loaded].game->alone->matchHands) {
            for (uint32_t n = 0; n < size; n++) {
                g_cmp.handed.push_back(guest.Read8(place + n));
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
        if (Comparing() && g_cmp.on) {
            // (the check's second board: its own state; its camera given as the match's stage's is)
            g_cmp.refFollows = g.fought && g.offsetKnown;
            std::swap(g, g_cmp.state);
            const bool keep = g_keepNow;
            BeforeStage(machine, stage);
            g_keepNow = keep;
            std::swap(g, g_cmp.state);
            return;
        }
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
    // The board started again on its way to a fight (g.loaded, g.stage say which): from its game's start-up passed
    // (Base), or from its reset
    void TripFromStart(Machine &machine) {
        if (Alone(*kChoices[g.loaded].game)) {
            // (its stage alone: its board as its start-up leaves it at its modes' routine, made once for its game and
            // kept (its start-up run kBootFrames of its frames a frame: EndBoot), its stage built there at its next frame)
            const int loaded = g.loaded, stage = g.stage;
            if (g_base.game != kChoices[g.loaded].game || g_base.data.empty()) {
                machine.StageBoard()->Reset();
                g_booting = true;
                g_reached = false;
                g_bootFrames = 0;
            } else {
                machine.RestoreStage(g_base.data);
            }
            g = {};
            g.loaded = loaded;
            g.stage = stage;
            return;
        }
        if (g_base.game && g_base.game == kChoices[g.loaded].game && !g_base.data.empty()) {
            const int loaded = g.loaded, stage = g.stage;
            machine.RestoreStage(g_base.data);
            g = g_base.state;
            g.loaded = loaded;
            g.stage = stage;
            return;
        }
        machine.StageBoard()->Reset();
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
            TripFromStart(machine);
        }
    }
} // namespace

namespace {
    // (the check, SFIII3_STAGE_COMPARE=1) The two stage boards' stages compared, as their last frames left them: their
    // stage layers (their stage objects and tilemaps, drawn by the native rasteriser with no text layer), their stage
    // objects, and 3rd Strike's picture composed over each (one frame of the two producers each)
    void CompareStages(Machine &machine) {
        machine::Machine *one = machine.StageBoard(), *two = machine.SecondStageBoard();
        if (!g_cmp.on || !one || !two || !g.fought || !g.offsetKnown || !g_cmp.state.fought) {
            return;
        }
        if (!one->NativeForPicture() || !two->NativeForPicture()) {
            return;
        }
        g_cmp.frames++;
        g_cmp.cameraLeast = std::min(g_cmp.cameraLeast, g.given);
        g_cmp.cameraMost = std::max(g_cmp.cameraMost, g.given);
        static native::Rasteriser r;
        static std::vector<uint32_t> a, b;
        int w = 0, h = 0;
        r.margin = machine.WideMargin();
        r.text = false;
        r.keep = [](const void *, const native::Object &o, uint32_t) { return StageWords(o.words[0], o.words[2]); };
        r.Draw(one->native, one->SpriteRamBytes(), one->SsRam(), a, w, h);
        r.Draw(two->native, two->SpriteRamBytes(), two->SsRam(), b, w, h);
        const bool layers = a == b;
        // (debugging aid, SFIII3_STAGE_COMPARE_SHOTS=EVERY:prefix: the two stages' layers, every so many frames)
        static const char *shots = getenv("SFIII3_STAGE_COMPARE_SHOTS");
        if (shots && machine.frame % strtoull(shots, nullptr, 10) == 0) {
            const std::string at = std::string(strchr(shots, ':') + 1) + std::to_string(machine.frame);
            WritePng((at + "-match.png").c_str(), a.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            WritePng((at + "-alone.png").c_str(), b.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
        // (the stage's objects that are pictures, in their order, by the three words that are drawn (the fourth is the
        // game's own, which the board does not read): not which list each came from, nor the
        // tilemaps' line objects, whose places in the frame's list the match's fighters change, nor the blank objects
        // (tile 0 in colours 0x1FF, off the picture) that fill the lists' places the match's fighters have)
        std::vector<native::Object> oa, ob;
        for (auto [from, to] : {std::pair{one, &oa}, std::pair{two, &ob}}) {
            for (native::Object o : from->native.objects) {
                if (StageWords(o.words[0], o.words[2]) && (o.words[2] & 3) != 0 && o.words[0] != 0x1FF) {
                    o.group = 0;
                    o.words[3] = 0;
                    to->push_back(o);
                }
            }
        }
        const bool sameObjects = oa == ob;
        bool sameTilemaps = true;
        for (int t = 0; t < 4; t++) {
            for (int n = 0; n < 8; n++) {
                sameTilemaps &= one->native.registers.tilemap[t][n] == two->native.registers.tilemap[t][n];
            }
        }
        const bool objects = sameObjects && sameTilemaps;
        if (!objects && g_cmp.reports) {
            if (!sameTilemaps) {
                for (int t = 0; t < 4; t++) {
                    fprintf(stderr, "stages: the check, tilemap %d: %04X %04X %04X %04X %04X %04X / %04X %04X %04X %04X %04X %04X\n", t,
                            one->native.registers.tilemap[t][0], one->native.registers.tilemap[t][1], one->native.registers.tilemap[t][2],
                            one->native.registers.tilemap[t][3], one->native.registers.tilemap[t][4], one->native.registers.tilemap[t][5],
                            two->native.registers.tilemap[t][0], two->native.registers.tilemap[t][1], two->native.registers.tilemap[t][2],
                            two->native.registers.tilemap[t][3], two->native.registers.tilemap[t][4], two->native.registers.tilemap[t][5]);
                }
            }
            for (size_t i = 0; i < std::min(oa.size(), ob.size()); i++) {
                if (!(oa[i] == ob[i])) {
                    fprintf(stderr, "stages: the check, object %zu: %08X %08X %08X %08X / %08X %08X %08X %08X\n", i, oa[i].words[0], oa[i].words[1],
                            oa[i].words[2], oa[i].words[3], ob[i].words[0], ob[i].words[1], ob[i].words[2], ob[i].words[3]);
                    break;
                }
            }
        }
        // (debugging aid, SFIII3_STAGE_COMPARE_OBJECTS=FRAME: the two stages' objects at that frame)
        static const long long objectsAt = getenv("SFIII3_STAGE_COMPARE_OBJECTS") ? atoll(getenv("SFIII3_STAGE_COMPARE_OBJECTS")) : -1;
        if (static_cast<long long>(machine.frame) == objectsAt) {
            for (const auto *list : {&oa, &ob}) {
                fprintf(stderr, "stages: the check's objects (%s):\n", list == &oa ? "the match's" : "alone");
                for (const native::Object &o : *list) {
                    fprintf(stderr, "  %08X %08X %08X %08X group %u\n", o.words[0], o.words[1], o.words[2], o.words[3], o.group);
                }
            }
        }
        // (3rd Strike's layers but its own stage over each)
        r.text = true;
        r.keep = [](const void *, const native::Object &o, uint32_t) { return !OwnStageWords(o.words[0], o.words[2]); };
        const auto stageKeep = [](const void *, const native::Object &o, uint32_t) { return StageWords(o.words[0], o.words[2]); };
        const machine::Machine &own = machine.board;
        r.DrawWithGuest(&one->native, one->SpriteRamBytes(), stageKeep, nullptr, own.native, own.SpriteRamBytes(), own.SsRam(), a, w, h);
        r.DrawWithGuest(&two->native, two->SpriteRamBytes(), stageKeep, nullptr, own.native, own.SpriteRamBytes(), own.SsRam(), b, w, h);
        const bool composed = a == b;
        const bool late = g_cmp.refModes == 0;
        g_cmp.refModes = 0;
        g_cmp.late += late;
        g_cmp.lateDifferent += late && !layers;
        g_cmp.layersDifferent += !layers;
        g_cmp.objectsDifferent += !objects;
        g_cmp.composedDifferent += !composed;
        // (debugging aid, SFIII3_STAGE_COMPARE_PIXELS=1: every frame whose pictures differ said)
        static const bool everyPicture = getenv("SFIII3_STAGE_COMPARE_PIXELS") != nullptr;
        if (((!layers || !objects || !composed) && g_cmp.reports) || (everyPicture && (!layers || !composed))) {
            g_cmp.reports -= g_cmp.reports > 0;
            fprintf(stderr, "stages: the check, frame %llu (in its fight %u): the stage's layers %s, its objects %s (%zu / %zu), the composed picture %s; "
                            "the match's stage ran at %.3f of its frame\n",
                    static_cast<unsigned long long>(machine.frame), g.inFight, layers ? "the same" : "different", objects ? "the same" : "different",
                    oa.size(), ob.size(), composed ? "the same" : "different", g_cmp.refStageAt);
        }
    }
} // namespace

void StagesBeforeFrame(Machine &machine) {
    if (Comparing()) {
        CompareStages(machine);
    }
    // (a stage alone's board started up: kept as it is, at its modes' routine, every stage of its game started from there)
    if (g_booting && machine.StageBoard() && g.loaded && (g_reached || (g_bootFrames += kBootFrames) > kMostBootFrames)) {
        g_booting = false;
        if (!g_reached) {
            fprintf(stderr, "stages: %s's board did not come to its modes' routine\n", kChoices[g.loaded].game->set);
        }
        const int loaded = g.loaded, stage = g.stage;
        g = {};
        g.loaded = loaded;
        g_base.game = kChoices[loaded].game;
        g_base.state = g;
        machine.KeepStage(g_base.data);
        g.stage = stage;
    }
    if (g_keepBaseNow) {
        g_keepBaseNow = false;
        if (g.loaded && machine.StageBoard() && !g.fought) {
            g_base.game = kChoices[g.loaded].game;
            g_base.state = g;
            machine.KeepStage(g_base.data);
        }
    }
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
        if (!g_pre.on && next == g.stage && FeatureOn("stages_preload") && (g.fought || g.stage < 0) && !Alone(*kChoices[choice].game)) {
            // (idle: another of this game's stages not kept yet taken to its fight meanwhile)
            for (int stage = 0; stage <= most; stage++) {
                if (!IsKept(kChoices[choice].game, stage)) {
                    g_pre.on = true;
                    g_pre.live = g;
                    const int loaded = g.loaded;
                    g = {};
                    g.loaded = loaded;
                    g.stage = stage;
                    TripFromStart(machine);
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
                TripFromStart(machine);
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
            TripFromStart(machine);
        }
        (void)was;
        return;
    }
    g = {};
    g_pre = {};
    g_base = {};
    g.loaded = choice;
    g_kept.clear();
    machine.SetStageBoard(choice ? LoadSet(*kChoices[choice].game) : nullptr);
    if (choice && !machine.StageBoard()) {
        g.loaded = 0; // (not there: as without it)
        SetFeatureValue("stages", 0);
    } else if (choice && Alone(*kChoices[choice].game)) {
        g.stage = kChoices[choice].stage == kOpponents ? -1 : g.stage;
        TripFromStart(machine);
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
bool StageBehindNextBesideReady() {
    return g_make.on && g_make.second && g_make.ready;
}
// The second board's picture coming in: `columns` of it at the right, its sound `gain` of 256
void StageBehindSlide(Machine &machine, int columns, int gain) {
    machine.stageSlide = columns;
    machine.secondStageGain = gain;
}
// ... or faded in over the first by `part` of 256, its sound as much
void StageBehindBlend(Machine &machine, int part) {
    machine.stageBlend = part;
    machine.secondStageGain = part;
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
    // (the stage games whose stages are made alone: their modes' routine)
    static std::vector<Lift> alone;
    for (const StageGame *game : {&kNewGeneration, &kSecondImpact}) {
        if (!game->alone) {
            continue;
        }
        for (const Routine &modes : game->alone->modes) {
            Lift l{"StageAlone", modes.at, modes.size, modes.hash, AloneFrame};
            l.boards = Lift::kStageBoards;
            alone.push_back(l);
        }
    }
    // (the check: the match's stage's frame, where what the match hands it is taken)
    for (const StageGame *game : {&kNewGeneration, &kSecondImpact}) {
        if (game->alone && game->alone->stageFrame.at) {
            Lift l{"MatchStageFrame", game->alone->stageFrame.at, game->alone->stageFrame.size, game->alone->stageFrame.hash, nullptr};
            l.hookFn = MatchStageFrame;
            l.boards = Lift::kStageBoards;
            alone.push_back(l);
        }
    }
    RegisterLifts(alone.data(), alone.size());
    if (Machine *machine = TheMachine()) {
        machine->stageObject = StageObject;
        machine->ownStageObject = OwnStageObject;
        machine->nativeStageObject = [](const native::Object &o) { return StageWords(o.words[0], o.words[2]); };
        machine->nativeOwnStageObject = [](const native::Object &o) { return OwnStageWords(o.words[0], o.words[2]); };
        machine->beforeStage = BeforeStage;
        machine->beforeSecondStage = BeforeSecondStage;
        // (the second board, until it is ready (past its fight's banners): eight frames a frame, so that it is there before
        // the stage shown has been crossed even at the fastest scroll)
        machine->secondStageFramesNow = [] { return g_make.on && g_make.second && !g_make.ready ? 8 : 1; };
        // (composed while 3rd Strike's round is on and the board is in its fight: for the opponent's, on a stage it has)
        machine->composeNow = [] { return !g_pre.on && OnStage() && g.fought && (kChoices[g.loaded].game != &kThirdStrike || g.inFight > kBannersFor) && (kChoices[g.loaded].stage != kOpponents || g.stage >= 0); };
        // (on its way to its fight: eight frames a frame; in it one, its own pace (its animations and music))
        machine->stageFramesNow = [] { return g_booting ? kBootFrames : g.fought || Comparing() ? 1 : 8; }; // (the check: one, the two boards in step)
    }
}

} // namespace cps3recomp
