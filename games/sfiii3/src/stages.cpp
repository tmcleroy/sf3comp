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
#include "session.h"

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
        // word: its camera held (bg_stop: 3rd Strike's 0x02028028, at +0x18 of the stage's state the match's set-up
        // clears, `matchHands`' third; 1 as a round starts), and the byte after it, held for its stage's entrance
        // (bg_app_stop)
        uint32_t cameraHeld;
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
        0x0201D288,
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
        0x020235E0,
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
        // How much of its step to where it would be its camera takes in a frame, in hundredths: 3rd Strike's and 2nd
        // Impact's camera code cut it to 80 in 100 (3rd Strike's 0x060B8620, 2nd Impact's 0x060A29A6: the step times 80
        // by the divide, by 100); New Generation's takes it whole (its moves, 0x0607C178 and 0x0607C1E4, move the layer
        // by the columns, no more than the most a frame, and nothing cuts them)
        int cut = 80;
    };
    // (its fighters' layer: the camera's middle across at +0x20 of it, as in 3rd Strike; its place up at +0x1C shown and
    // +0x24, its top at +0x6E: stage_camera.cpp)
    constexpr uint32_t kSpeedUp = 0x14, kShownUp = 0x1C, kUp = 0x24, kHomeUp = 0x4A, kTopEnd = 0x6E;
    constexpr uint32_t kSteps = 0x04;   // (a layer's step, 0 not begun)
    constexpr uint32_t kMovedUp = 0x14; // (of the stage's head, 0x54 before its layers: how far up the camera's layer is from its start)
    const StageGame kNewGeneration = {"sfiiin", {0x0201BF0C, 0x0201BF14, 0x0201BF24}, {0x0201BEFE, 0x0201BF80}, 0x0201BF58, 0x0201BF5C, 680, 320,
                                      {0x0200EB33, 0x0201DB15}, {0x0200D22B, 0x0200D22D, 0x0201DAF1, 0x0200D603, 0x0200D605, 0x0201DB09},
                                      0x02012CA3, {0x0200D18C, 0x0200D564}, 0x1C000, 1, &kNewGenerationAlone, 100};
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
    constexpr int kOpponentsFirst = 29; // (the choices from 1 up to it: every stage of the two games, by number)
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
        int givenUp = 0;     // ... and how far up (Give)
        bool gaveCamera = false; // (given one for its last frame)
        bool givenBefore = false; // (given one since its stage was built)
        bool jumped = false; // (sent somewhere else at once, or first, the frame before: the stage check passes that frame by)
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
    bool ComparingPlay();
    struct Compare {
        bool on = false;
        State state;                 // (the second board's)
        std::vector<uint8_t> handed; // (what the match's stage took this frame)
        std::vector<std::pair<uint32_t, uint32_t>> extra;
        double refStageAt = 0;    // (where in its frame the match's board ran its stage's frame, 0-1 from the vertical blank)
        uint32_t refModes = 0;    // (the match's board's calls of the game's modes since the last comparison)
        uint64_t late = 0, lateDifferent = 0; // (frames in which it did not run them: its frame ran long; of them, different)
        bool refFollows = false;
        uint64_t frames = 0, layersDifferent = 0, composedDifferent = 0, objectsDifferent = 0, zoomDifferent = 0, upFrames = 0, idleFrames = 0;
        int cameraLeast = 1 << 30, cameraMost = -(1 << 30);
        int stage = -1;
        unsigned reports = 4;
        // (in play: the made-up play's dice, and the sticks and buttons held)
        uint64_t dice = 0;
        uint32_t pads[2] = {}, kicks[2] = {};
        ~Compare() {
            if (frames) {
                fprintf(stderr, "stages: stage alone against %s (stage %d): %llu frames compared (%llu with its camera up, %llu with its fighters idle), the stage's "
                                "layers %llu different, its objects %llu different, its zoom %llu different, the composed picture %llu different; its camera "
                                "given %d-%d; the match's frames that ran long %llu (%llu of them with the layers different)\n",
                        ComparingPlay() ? "its own match in play" : "the match's", stage, static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(upFrames), static_cast<unsigned long long>(idleFrames), static_cast<unsigned long long>(layersDifferent),
                        static_cast<unsigned long long>(objectsDifferent), static_cast<unsigned long long>(zoomDifferent),
                        static_cast<unsigned long long>(composedDifferent), cameraLeast, cameraMost, static_cast<unsigned long long>(late),
                        static_cast<unsigned long long>(lateDifferent));
            }
        }
    } g_cmp;
    bool Comparing() {
        static const bool on = getenv("SFIII3_STAGE_COMPARE") != nullptr;
        return on;
    }
    // The check against the stage game's own match in play (SFIII3_STAGE_COMPARE=play): the match is played (made-up
    // play on the stage board, its fighters walking, jumping and standing idle in turn, its own camera following them),
    // and the stage alone is given only what the stage alone is given in a fight (Give: its camera across and up, its
    // fighters' places), from the match's own fighters and camera, and the dice and frame count (noise, not the stage's
    // to make). The ground truth is the stage game's own picture of its stage; the check with SFIII3_STAGE_COMPARE=1
    // handed the stage alone everything the match hands its stage (the fighters' records whole among it), so the stage
    // alone never had to make its camera from what it is given, and could not differ there.
    bool ComparingPlay() {
        static const bool on = getenv("SFIII3_STAGE_COMPARE") && std::string(getenv("SFIII3_STAGE_COMPARE")) == "play";
        return on;
    }
    // The stage check in play (SFIII3_STAGE_CHECK=1: checkguests.sh), each frame a stage game's stage is shown in a fight:
    // its camera where it was sent (across and up, after its frame: Give), its picture's zoom whole when its own frame
    // is (the stage's head's +0x2E, +0x30 at 64), no music of 3rd Strike's heard with the stage's, no sound of the
    // stage board's playing when a stage is built there (a stage chosen after another), none heard with no stage
    // chosen. Reported at the end.
    bool Checking() {
        static const bool on = getenv("SFIII3_STAGE_CHECK") != nullptr;
        return on;
    }
    // (how far off its camera may be and pass: across, 2 columns (3rd Strike's camera's step mapped to the stage's
    // columns, its camera code's cut to 80 in 100); up, 6 lines or half the way, whichever is more (its camera code's
    // place up is made from heights, a step of 1.75 lines, and the stage games' own way up eases, a few lines behind
    // 3rd Strike's in a fast jump). Within the limits: on average a column off or less, and no more than one frame in
    // 50 off by more than those, either way. Before 2026-11-06 it was some columns off every frame (its camera made for
    // the stage's left end, put back each frame) and never went up)
    constexpr int kAcrossOff = 2, kUpOff = 6;
    struct PlayCheck {
        uint64_t frames = 0, across = 0, up = 0, upFrames = 0, zoom = 0, twoSongs = 0, songFrames = 0, leftOver = 0, built = 0, heardAlone = 0;
        uint64_t off = 0; // (columns off, all told)
        int most = 0;
        unsigned reports[6] = {3, 3, 3, 3, 3, 3};
        ~PlayCheck() {
            if (Checking()) {
                const bool within = frames > 0 && off <= frames && across * 50 <= frames && up * 50 <= frames;
                fprintf(stderr, "stages: the stage check: %llu frames on a stage game's stage (%llu with its camera up), its camera off where it was sent: "
                                "across %llu (%.2f columns on average, %d at most), up %llu (%s the limits); its picture's zoom not whole with its frame whole: %llu; 3rd Strike's music heard with the stage's: %llu "
                                "(of %llu frames of the stage's music); stages built %llu, with the stage board's sound playing %llu; its sound heard with no "
                                "stage: %llu\n",
                        static_cast<unsigned long long>(frames), static_cast<unsigned long long>(upFrames), static_cast<unsigned long long>(across),
                        frames ? static_cast<double>(off) / static_cast<double>(frames) : 0.0, most, static_cast<unsigned long long>(up),
                        within ? "within" : "outside", static_cast<unsigned long long>(zoom), static_cast<unsigned long long>(twoSongs),
                        static_cast<unsigned long long>(songFrames), static_cast<unsigned long long>(built), static_cast<unsigned long long>(leftOver),
                        static_cast<unsigned long long>(heardAlone));
            }
        }
        void Report(int kind, const char *what, uint64_t frame) {
            if (reports[kind]) {
                reports[kind]--;
                fprintf(stderr, "stages: the stage check, frame %llu: %s\n", static_cast<unsigned long long>(frame), what);
            }
        }
    } g_check;

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
    int g_sessionChoice = 0; // (a training session's Stage row's choice of another game's stage: StageForSession)
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
    // A fighter's object: 6 bits a pixel, the players' palettes 0x00-0x1F (0x08 and 0x10 most often, 0x00 and 0x18 by
    // the colour chosen, in all three games' fights: SFIII3_PRIORITY_STATS), not blending (not its shadow). (Not every
    // palette below 0x40: New Generation's effects in front, palette 0x3E at priority 10, were taken for a fighter, and its
    // stages' pieces in front at 20 and 22 put under its fighters again)
    bool FighterWords(uint32_t v1, uint32_t v3) {
        const uint32_t palette = v1 & 0x1FF;
        return (v3 & 3) != 0 && ((v1 >> 9) & 1) && palette < 0x20 && !((v1 >> 10) & 1);
    }
    // The frontmost priority of a board's fighters (their objects' heads' priorities, native::Object::priority: lower is
    // drawn later); 0 none. The stage's pieces in front of the fighters, as the stage's own game draws them, are those of a
    // lower priority (SFIII3_PRIORITY_STATS, in 3rd Strike's fights on Akuma's stage and 2nd Impact's own match on its Hugo
    // stage: the fighters at 26-54, now and then 67; the pieces in front, Akuma's stage's roots and the market's, at 10
    // and 19-20; the stages' layers behind at 69 and up; the fighters' shadows at 68; the round's banners at 10).
    int FrontOf(const machine::Machine &m) {
        int front = 0x100;
        for (const native::Object &o : m.native.objects) {
            if (o.priority != native::kNoPriority && FighterWords(o.words[0], o.words[2])) {
                front = std::min<int>(front, o.priority);
            }
        }
        return front < 0x100 ? front : 0;
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

    // How far the stage alone's camera may be from where it is sent and still be left to follow its fighters (Give)
    constexpr int kSteered = 24;
    // 3rd Strike's camera as its stage is shown: in a scripted move (a super art's zoom or a throw's close-up sending the
    // camera somewhere and back: its stage's head's chase flags, 0x02026BD1, bits 0-3 across, 4-7 up) its fighters'
    // layer's scripted place (chase_xy, +0x58 across, +0x5C up: stage_zoom.cpp), from which its stage's layers are drawn
    // then; else its place (wxy). (Until 2026-11-06 its place only: in a super art that sends the camera up, the guest
    // stage stayed down.)
    constexpr uint32_t kOwnChase = 0x02026BD1, kOwnFighters = 0x02026C90;
    int OwnCameraAcross() {
        return static_cast<int16_t>(Ram16((Ram8(kOwnChase) & 0x0F) ? kOwnFighters + 0x58 : sfiii3::kCameraX));
    }
    int OwnCameraUp() {
        return static_cast<int16_t>(Ram16((Ram8(kOwnChase) & 0xF0) ? kOwnFighters + 0x5C : sfiii3::kCameraY));
    }

    // Whether its stage is in its own entrance (its picture's frame drawn in from a close-up as it begins, its camera
    // panning down from the sky: New Generation's Gill stage, and some of 2nd Impact's): its frame not whole (its head's
    // +0x2E and +0x30 not 64), or its camera held for its entrance (the byte after its camera held, bg_app_stop, its
    // own: never given). Given nothing then (BeforeStage), its entrance its own, as in its match.
    bool InEntrance(const uint8_t *ram, const StageGame &game) {
        const uint32_t head = game.camera[1] - 0x20 - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
        return Word(ram, head + 0x2E) != 64 || Word(ram, head + 0x30) != 64 || (game.alone && ram[(game.alone->cameraHeld + 2) & 0x7FFFF] != 0);
    }

    // What the stage board's stage is given each frame for its camera (BeforeStage, from 3rd Strike's camera and
    // fighters mapped to its stage; in the check against its own match in play, SFIII3_STAGE_COMPARE=play, from that
    // match's): its camera's middle across, its fighters' places across (its coordinates) and heights, and how far up
    // its camera is
    struct Gift {
        int across = 0;
        int placed[2] = {};
        int height[2] = {};
        int up = 0;
        // (its stage alone: its fighters' places and heights where its camera code reads them, +0x60 and +0x62, when
        // known (the check against its own match in play: the match's own); none (-32768): made (Give))
        int seen[2] = {-32768, -32768}, seenUp[2] = {-32768, -32768};
        // (its stage alone: its camera held, as 3rd Strike's is (its round's start, a throw, a super art's); and the
        // stage's shakes, its head's frames of a shake left across and up, +0x2A and +0x2C)
        int held = 0, quake[2] = {};
    };
    // The gift written where its game's camera code reads it: its fighters' places (+0x64 across, +0x68 up, as in 3rd
    // Strike), so that its game's camera step, which makes its camera from them each frame, leaves it about there; its
    // camera's words; and its layers' places up, for the frames its game holds its camera (its own round's start)
    void Give(uint8_t *ram, const StageGame &game, const Gift &gift, bool alone) {
        for (int p = 0; p < 2; p++) {
            SetWord(ram, game.players[p] + 0x64, gift.placed[p]);
            SetWord(ram, game.players[p] + 0x68, gift.height[p]);
            // (its stage alone: no fighter of its runs, and a fighter's own frame is what copies its place to where the
            // camera code reads it (+0x60 across, +0x62 up: scr_mv_x, scr_mv_y); so those are given too (below).
            // Left as they were (0), its camera made for the stage's left end every frame (put back each frame, its
            // far layers and objects placed from where it went: parts of the stage black, others scrolling by
            // themselves) and never went up. Up: the heights, from which its camera code makes its place up.)
            if (alone) {
                SetWord(ram, game.players[p] + 0x62, gift.seenUp[p] != -32768 ? gift.seenUp[p] : gift.height[p]);
            }
        }
        // Across (its stage alone): its camera is left where its last frame left it, and its own camera code moves it
        // on from there (where it is before its frame, where it was and how far it moves all its own, as a match's are:
        // its stage's code that brings in the stage's columns as its camera moves goes by where it was and where it is,
        // and its far layers by how far it moved). Put where it was sent each frame (until 2026-11-06), its camera code
        // then took a step of its own after its fighters, and the next frame began somewhere else: columns its camera
        // passed were never brought in (black) and its far layers went by a camera that was not there. So its fighters
        // are put where its camera code takes it exactly to where it is sent: one in the picture's left quarter, one in
        // its right, their middle where it is to go (its camera code then goes to their middle: scr_11_22, stage_camera.cpp's
        // ToMiddle; the two of the same character, as on its board, where no fighter has been chosen), so far that the
        // step it takes, cut to 80 in 100 by 2nd Impact's camera code and whole by New Generation's (StageGame::cut; and
        // to the most a frame, made enough), is the step wanted. (Until 2026-11-06 the step was made for the cut on
        // New Generation's stages too: its camera took the whole of a step a quarter longer than wanted, went a column
        // past where it was sent and back the next frame, every frame, with the fighters standing still: the play-test's
        // stage jittering 2 pixels left and right.) Further than
        // kSteered from there (a round's start, a stage built), it is put there. In the check against its own match in
        // play the match's own places are given (Gift::seen), and its camera code makes its camera as the match's did.
        const uint32_t fighters0 = game.camera[1] - 0x20;
        const uint32_t head0 = fighters0 - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
        const int had = static_cast<int16_t>(Word(ram, game.camera[1]));
        const bool steered = alone && game.alone && g.givenBefore && std::abs(had - gift.across) <= kSteered;
        if (alone && game.alone) {
            if (gift.seen[0] != -32768) {
                for (int p = 0; p < 2; p++) {
                    SetWord(ram, game.players[p] + 0x60, gift.seen[p]);
                }
            } else {
                const int d = steered ? gift.across - had : 0, want = std::abs(d);
                const int step = want ? (want * 100 + game.cut - 1) / game.cut : 0;
                if (step > static_cast<int16_t>(Word(ram, head0 + 0x4E))) {
                    SetWord(ram, head0 + 0x4E, step);
                }
                const int middle = (steered ? had : gift.across) + (d < 0 ? -step : step);
                SetWord(ram, game.players[0] + 0x60, middle - 200);
                SetWord(ram, game.players[1] + 0x60, middle + 200);
            }
        }
        const uint32_t fighters = game.camera[1] - 0x20;
        for (const uint32_t at : {fighters + kShownUp, fighters + kUp}) {
            SetWord(ram, at, gift.up);
            SetWord(ram, at + 2, 0);
        }
        // (and how far up it is for its other layers, and their places up from that, as its game makes them each frame
        // (stage_camera.cpp's LayerUp), which it does not while its camera is held; the layers in use have begun their
        // steps)
        const uint32_t head = fighters - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
        const int moved = gift.up - static_cast<int16_t>(Word(ram, fighters + kHomeUp));
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
        if (alone && game.alone) {
            SetWord(ram, game.alone->cameraHeld, gift.held);
            SetWord(ram, head + 0x2A, gift.quake[0]);
            SetWord(ram, head + 0x2C, gift.quake[1]);
        }
        if (!steered) {
            for (const uint32_t at : game.camera) {
                SetWord(ram, at, gift.across);
            }
            // (its stage alone, its camera put somewhere at once: its other layers put where they are for it, as its
            // game makes them each frame (stage_camera.cpp's LayerAcross), which it does not while its camera is held
            // (a round's start): put there as a round started, its far layers stayed where they were for its camera
            // before, some columns off, until the round began)
            if (alone) {
                const int moved = gift.across - static_cast<int16_t>(Word(ram, fighters0 + 0x48));
                for (int n = 0; n < 7; n++) {
                    const uint32_t layer = head0 + 0x54 + static_cast<uint32_t>(n) * 0x90;
                    if (layer == fighters0 || Word(ram, layer + kSteps) == 0) {
                        continue;
                    }
                    const uint32_t speed = (static_cast<uint32_t>(Word(ram, layer + 0x10)) << 16) | Word(ram, layer + 0x12);
                    const uint32_t place = speed * static_cast<uint32_t>(moved);
                    const int across = static_cast<int16_t>(place >> 16) + static_cast<int16_t>(Word(ram, layer + 0x48));
                    for (const uint32_t at : {layer + 0x18u, layer + 0x20u, layer + 0x58u}) {
                        SetWord(ram, at, across);
                        SetWord(ram, at + 2, static_cast<int>(place & 0xFFFF));
                    }
                    SetWord(ram, layer + 0x30, across);
                }
            }
            for (const uint32_t at : game.edge) {
                if (at) {
                    SetWord(ram, at, gift.across - 192);
                }
            }
        }
        g.jumped = !g.givenBefore || std::abs(gift.across - g.given) > kSteered;
        g.given = gift.across;
        g.givenUp = gift.up;
        g.gaveCamera = true;
        g.givenBefore = true;
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
        // (the stage check: where its last frame left its camera, against where it was sent; within the ends its game
        // keeps it between)
        if (Checking() && g.gaveCamera && &stage == machine.StageBoard() && kChoices[g.loaded].game != &kThirdStrike) {
            const uint32_t fighters = game.camera[1] - 0x20;
            const int had = static_cast<int16_t>(Word(ram, game.camera[1])), hadUp = static_cast<int16_t>(Word(ram, fighters + kUp));
            const int lo = static_cast<int16_t>(Word(ram, fighters + 0x6A)), hi = static_cast<int16_t>(Word(ram, fighters + 0x66));
            const int top = static_cast<int16_t>(Word(ram, fighters + kTopEnd));
            const int wantedUp = std::clamp(g.givenUp, 0, std::max(top, 0));
            g_check.frames++;
            g_check.upFrames += wantedUp > 0;
            char what[160];
            if (g.given >= lo && g.given <= hi && !g.jumped) {
                g_check.off += static_cast<uint64_t>(std::abs(had - g.given));
                g_check.most = std::max(g_check.most, std::abs(had - g.given));
            }
            if (g.given >= lo && g.given <= hi && !g.jumped && std::abs(had - g.given) > kAcrossOff) {
                g_check.across++;
                snprintf(what, sizeof(what), "its camera across at %d, sent to %d (its ends %d-%d)", had, g.given, lo, hi);
                g_check.Report(0, what, machine.frame);
            }
            if (!g.jumped && std::abs(hadUp - wantedUp) > std::max(kUpOff, wantedUp / 2)) {
                g_check.up++;
                snprintf(what, sizeof(what), "its camera up at %d, sent to %d", hadUp, wantedUp);
                g_check.Report(1, what, machine.frame);
            }
        }
        g.gaveCamera = false;
        if (alone) {
            stage.inputs = stage.extra = 0xFFFFFFFFu;
            if (!g.fought || (Comparing() && !g_cmp.refFollows)) {
                return;
            }
            if (ComparingPlay() && &stage == machine.SecondStageBoard() && machine.StageBoard() && InEntrance(ram, game)) {
                g.givenBefore = false;
                return; // (its own entrance: nothing given, as in a fight)
            }
            if (ComparingPlay() && &stage == machine.SecondStageBoard() && machine.StageBoard()) {
                // (the check against its own match in play: given what the stage alone is given in a fight, from the
                // match's own camera and fighters)
                const uint8_t *ref = machine.StageBoard()->mainRam;
                Gift gift;
                // (its camera as the match's stage is shown: in a scripted move (its round's start, a super art's) its
                // scripted place, as 3rd Strike's is given in a fight: OwnCameraAcross)
                const uint32_t refFighters = game.camera[1] - 0x20;
                const uint8_t chase = ref[(refFighters - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90 + 0x25) & 0x7FFFF];
                gift.across = static_cast<int16_t>(Word(ref, (chase & 0x0F) ? refFighters + 0x58 : game.camera[1]));
                gift.up = std::max(0, static_cast<int>(static_cast<int16_t>(Word(ref, (chase & 0xF0) ? refFighters + 0x5C : refFighters + kShownUp))));
                // (and its fighters' places as its camera code read them, as its stage's frame began: taken there,
                // MatchStageFrame, with the fighters' records)
                size_t records = 0;
                for (const auto &[place, size] : g_cmp.extra) {
                    records += size;
                }
                for (int p = 0; p < 2; p++) {
                    gift.placed[p] = static_cast<int16_t>(Word(ref, game.players[p] + 0x64));
                    gift.height[p] = static_cast<int16_t>(Word(ref, game.players[p] + 0x68));
                    const size_t at = records + (game.players[p] - game.alone->matchHands[0].first);
                    if (at + 0x63 < g_cmp.handed.size()) {
                        gift.seen[p] = static_cast<int16_t>((g_cmp.handed[at + 0x60] << 8) | g_cmp.handed[at + 0x61]);
                        gift.seenUp[p] = static_cast<int16_t>((g_cmp.handed[at + 0x62] << 8) | g_cmp.handed[at + 0x63]);
                    }
                }
                const auto handed = [&](uint32_t place) -> int {
                    size_t from = records;
                    for (const auto &[at, size] : game.alone->matchHands) {
                        if (place >= at && place + 1 < at + size) {
                            const size_t i = from + (place - at);
                            return i + 1 < g_cmp.handed.size() ? static_cast<int16_t>((g_cmp.handed[i] << 8) | g_cmp.handed[i + 1]) : 0;
                        }
                        from += size;
                    }
                    return 0;
                };
                const uint32_t head = game.camera[1] - 0x20 - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
                gift.held = handed(game.alone->cameraHeld);
                gift.quake[0] = handed(head + 0x2A);
                gift.quake[1] = handed(head + 0x2C);
                Give(ram, game, gift, true);
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
        if (ComparingPlay() && &stage == machine.StageBoard() && !alone) {
            // (the check against its own match in play: its fighters played, made up: 600 frames idle, then 600 of
            // walking, jumping and attacking, by turns; the same for a stage each run)
            uint64_t &r = g_cmp.dice;
            if (r == 0) {
                r = 0x9E3779B97F4A7C15ull * static_cast<uint64_t>(StageNow() + 1);
            }
            if ((g.inFight / 600) % 2 == 1) {
                for (int p = 0; p < 2; p++) {
                    r = r * 6364136223846793005ull + 1442695040888963407ull;
                    if (((r >> 33) & 7) == 0) {
                        // (a stick and buttons held for a while: up the most often, for the camera's way up)
                        static const uint32_t kSticks[] = {0, 1, 1, 9, 5, 4, 8, 4, 8, 2, 0, 1};
                        const uint64_t q = r >> 20;
                        uint32_t held = kSticks[q % 12] | (static_cast<uint32_t>((q >> 8) & (q >> 16)) & 0x70);
                        const uint32_t kicks = static_cast<uint32_t>((q >> 24) & (q >> 28)) & 7;
                        g_cmp.pads[p] = held << (8 * p);
                        g_cmp.kicks[p] = kicks;
                    }
                }
            } else {
                g_cmp.pads[0] = g_cmp.pads[1] = g_cmp.kicks[0] = g_cmp.kicks[1] = 0;
            }
            uint32_t inputs = g_cmp.pads[0] | g_cmp.pads[1], extra = 0;
            extra |= (g_cmp.kicks[0] & 1 ? 0x80000u : 0) | (g_cmp.kicks[0] & 2 ? 0x40000u : 0) | (g_cmp.kicks[0] & 4 ? 0x20000u : 0);
            extra |= (g_cmp.kicks[1] & 1 ? 0x100000u : 0) | (g_cmp.kicks[1] & 2 ? 0x200000u : 0);
            inputs |= g_cmp.kicks[1] & 4 ? 0x4000000u : 0;
            stage.inputs = ~inputs;
            stage.extra = ~extra;
        }
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
        if (ComparingPlay() && &stage == machine.StageBoard()) {
            return; // (the check against its own match in play: its camera its own)
        }
        const int camera = alone ? OwnCameraAcross() : static_cast<int16_t>(Ram16(sfiii3::kCameraX));
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
        // (its stage alone, in its own entrance (its picture's frame drawn in from a close-up as it begins, its camera
        // panning down from the sky: New Generation's Gill stage, and some of 2nd Impact's): given nothing until its
        // frame is whole (its head's +0x2E and +0x30 at 64), its entrance its own, as in its match (and as a stage built
        // between fights had it: 3rd Strike's camera's ends then not a stage's, nothing was given). Built in a fight
        // (chosen on the menu, or in turn), it was given 3rd Strike's camera through its entrance, its layers put
        // down from the sky at once; its entrance then put its picture's zoom whole as it began and went on drawing
        // it out a step a frame, past whole: the stage drawn small, black to its right and below, the rest of the
        // fight)
        if (alone && InEntrance(ram, game)) {
            g.givenBefore = false;
            return;
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
            Gift gift;
            for (int p = 0; p < 2; p++) {
                gift.placed[p] = placed[p] + push - g.offset;
            }
            // (3rd Strike between rounds, its camera's ends not its stage's yet (1 and 95 seen, before a round's start):
            // its middle, where 3rd Strike's camera begins a round, its fighters either side of it. Followed from
            // those ends, it went to its right end, and as the round began panned back across for some 40 frames
            // over the round's start; and held at its end it learned that place as its end, the stage cut short.)
            const bool ownEnds = OnStage() && right - left >= 128;
            if (!ownEnds && g_cameraWanted == kFollow) {
                theirs = (theirLeft + theirRight) / 2;
                for (int p = 0; p < 2; p++) {
                    gift.placed[p] = theirs + (p ? 40 : -40) - g.offset;
                }
            }
            if (g_cameraWanted != kFollow) {
                theirs = std::clamp(g_cameraWanted, theirLeft, theirRight); // (the modes' scrolling stage: its camera its own)
                for (int p = 0; p < 2; p++) {
                    placed[p] = theirs + (p ? 40 : -40);
                    gift.placed[p] = placed[p] - g.offset;
                }
            }
            // Up as 3rd Strike's camera is (its fighters' layer's place up, a line for a line: Oro's entrance on his stage,
            // which starts in the sky, a jump): its game makes that from its fighters too (the higher one's height past 88,
            // times its speed up, as far as its camera's layer's top), so its fighters, unseen, are put as high as that
            // needs; and its layers' places up are given too, for the frames its game holds its camera (its own round's
            // start, as 3rd Strike's intros play)
            const int up = g_cameraWanted == kFollow ? std::max(0, alone ? OwnCameraUp() : static_cast<int>(static_cast<int16_t>(Ram16(sfiii3::kCameraY)))) : 0;
            const int top = static_cast<int16_t>(Word(ram, game.camera[1] - 0x20 + kTopEnd));
            const int wantedUp = std::clamp(up, 0, std::max(top, 0));
            const int height = wantedUp > 0 ? 88 + static_cast<int>((static_cast<int64_t>(wantedUp) * 65536 + game.upSpeed - 1) / game.upSpeed) : 0;
            for (int p = 0; p < 2; p++) {
                gift.height[p] = height;
            }
            gift.up = wantedUp;
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
            gift.across = theirs;
            // (its stage alone: held as 3rd Strike's camera is (its round's start, a throw, a super art's), and shaken as
            // 3rd Strike's stage is)
            gift.held = Ram16(0x02028028);
            gift.quake[0] = Ram16(0x02026BAC + 0x2A);
            gift.quake[1] = Ram16(0x02026BAC + 0x2C);
            Give(ram, game, gift, alone);
            // (debugging aid, SFIII3_STAGE_CAMERA=1: the camera given, and what its frame before left)
            static const bool log = getenv("SFIII3_STAGE_CAMERA") != nullptr;
            static const bool everyFrame = getenv("SFIII3_STAGE_CAMERA_ALL") != nullptr;
            if (log && (g.frames % 60 == 0 || everyFrame)) {
                fprintf(stderr, "stage camera: 3rd Strike %d (%d-%d), given %d (%d-%d), it had %d (in its fight %u, ends learned %d %d)\n", camera, left,
                        right, theirs, theirLeft, theirRight, g.lastCamera, g.inFight, g.realLeft, g.realRight);
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
            for (size_t i = 0; i < a.matchHands.size(); i++) {
                const auto &[place, size] = a.matchHands[i];
                // (in play: only the dice's two counters and the hit stop's and frame count's words, the last three: the
                // rest the stage alone makes from what it is given (Give), as in a fight)
                const bool noise = i + 3 >= a.matchHands.size();
                for (uint32_t n = 0; n < size && at < g_cmp.handed.size(); n++, at++) {
                    // (and the fighters' records (the first), all but their places, +0x60-0x6B, which it is given: what
                    // else of theirs the stage's code reads (against a wall, ...) the stage alone has not, from 3rd
                    // Strike's, in a fight)
                    const uint32_t inRecord = (place + n - kChoices[g.loaded].game->players[0]) % (kChoices[g.loaded].game->players[1] - kChoices[g.loaded].game->players[0]);
                    const bool record = i == 0 && !(inRecord >= 0x60 && inRecord < 0x6C);
                    if (!ComparingPlay() || noise || record) {
                        guest.Write8(place + n, g_cmp.handed[at]);
                    }
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
            // (the stage check: the stage board's sound as its stage is built: the last stage's music stopped)
            if (Checking() && !second) {
                g_check.built++;
                if (guest.Board().keyOn != 0) {
                    g_check.leftOver++;
                    char what[96];
                    snprintf(what, sizeof(what), "stage %d built with the stage board's voices %04X playing", stage, guest.Board().keyOn);
                    g_check.Report(2, what, machine->frame);
                }
            }
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
        const int given = ComparingPlay() ? g_cmp.state.given : g.given;
        g_cmp.cameraLeast = std::min(g_cmp.cameraLeast, given);
        g_cmp.cameraMost = std::max(g_cmp.cameraMost, given);
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
        // (the whole picture's zoom, each board's own)
        const bool sameZoom = (one->native.registers.screen[7] & 0xFF) == (two->native.registers.screen[7] & 0xFF) &&
                              (one->native.registers.screen[15] & 0xFF) == (two->native.registers.screen[15] & 0xFF);
        g_cmp.zoomDifferent += !sameZoom;
        g_cmp.upFrames += static_cast<int16_t>(Word(one->mainRam, kChoices[g.loaded].game->camera[1] + 4)) > 0;
        g_cmp.idleFrames += ComparingPlay() && ((g.inFight / 600) % 2 == 0);
        if (!sameZoom && g_cmp.reports) {
            fprintf(stderr, "stages: the check, frame %llu: the zoom %02X %02X (the match's) / %02X %02X (alone)\n", static_cast<unsigned long long>(machine.frame),
                    one->native.registers.screen[7] & 0xFF, one->native.registers.screen[15] & 0xFF, two->native.registers.screen[7] & 0xFF,
                    two->native.registers.screen[15] & 0xFF);
        }
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
        if (((!layers || !objects || !composed || !sameZoom) && g_cmp.reports) || (everyPicture && (!layers || !composed))) {
            g_cmp.reports -= g_cmp.reports > 0;
            fprintf(stderr, "stages: the check, frame %llu (in its fight %u): the stage's layers %s, its objects %s (%zu / %zu), the composed picture %s; "
                            "the match's stage ran at %.3f of its frame\n",
                    static_cast<unsigned long long>(machine.frame), g.inFight, layers ? "the same" : "different", objects ? "the same" : "different",
                    oa.size(), ob.size(), composed ? "the same" : "different", g_cmp.refStageAt);
            if (everyPicture) {
                // (and how their palettes and textures differ)
                const uint16_t *ca = one->native.palette.Colours(), *cb = two->native.palette.Colours();
                uint32_t colours = 0, first = 0;
                for (uint32_t i = 0; i < native::Palette::kColours; i++) {
                    if (ca[i] != cb[i] && !colours++) {
                        first = i;
                    }
                }
                uint32_t pages = 0;
                for (int page = 0; page < native::Textures::kPages; page++) {
                    pages += memcmp(one->native.textures.Pixels() + page * 0x10000, two->native.textures.Pixels() + page * 0x10000, 0x10000) != 0;
                }
                fprintf(stderr, "stages: the check: %u colours different (the first %05X: %04X / %04X), %u texture pages different\n", colours, first, ca[first],
                        cb[first], pages);
                {
                    const uint8_t *sa = one->SpriteRamBytes(), *sb = two->SpriteRamBytes();
                    uint32_t bytes = 0, firstAt = 0, lastAt = 0;
                    for (uint32_t i = 0x8000; i < 0x80000; i++) { // (past the object words: the tilemaps' cells and line tables)
                        if (sa[i] != sb[i]) {
                            firstAt = bytes++ ? firstAt : i;
                            lastAt = i;
                        }
                    }
                    fprintf(stderr, "stages: the check: sprite RAM from 8000: %u bytes different (%05X-%05X)\n", bytes, firstAt, lastAt);
                }
                const uint32_t l1 = kChoices[g.loaded].game->camera[1] - 0x20, hd = l1 - 0x54 - 0x90;
                for (const machine::Machine *m : {one, two}) {
                    const uint8_t *r = m->mainRam;
                    fprintf(stderr, "stages: the check's camera (%s): col %d shown %04X.%04X at %04X.%04X was %d abs %d | moved %d now %d | L0 %d %d L2 %d %d\n", m == one ? "match" : "alone",
                            static_cast<int16_t>(Word(r, l1 + 0x0A)), Word(r, l1 + 0x18), Word(r, l1 + 0x1A), Word(r, l1 + 0x20), Word(r, l1 + 0x22),
                            static_cast<int16_t>(Word(r, l1 + 0x30)), static_cast<int16_t>(Word(r, l1 + 0x8C)), static_cast<int16_t>(Word(r, hd + 0x12)),
                            static_cast<int16_t>(Word(r, hd + 0x40)), static_cast<int16_t>(Word(r, l1 - 0x90 + 0x0A)), static_cast<int16_t>(Word(r, l1 - 0x90 + 0x8C)),
                            static_cast<int16_t>(Word(r, l1 + 0x90 + 0x0A)), static_cast<int16_t>(Word(r, l1 + 0x90 + 0x8C)));
                }
                for (size_t i = 0; i < std::min(oa.size(), ob.size()); i++) {
                    if (!(oa[i] == ob[i])) {
                        fprintf(stderr, "stages: the check, object %zu: %08X %08X %08X / %08X %08X %08X\n", i, oa[i].words[0], oa[i].words[1], oa[i].words[2],
                                ob[i].words[0], ob[i].words[1], ob[i].words[2]);
                        break;
                    }
                }
            }
        }
    }
} // namespace

namespace {
    // The jitter check (SFIII3_JITTER_CHECK=1: checkjitter.sh), in a fight with its fighters standing still (a training
    // session, player 1 and the dummy idle; in the modes, both made players' with no controls): every kJitterHold frames
    // both fighters are put somewhere else on the stage (kJitterPlaces places across it, from one end to the other, close
    // together, apart and far apart by turns), and once the camera has had kJitterSettle frames to get there, each frame
    // the stage as shown (its layers drawn alone, as the picture has them: the stage board's stage, or 3rd Strike's own)
    // is lined up with the frame before's, a few columns and lines either way: with nothing moving it must stay put. The
    // play-test's jitter (2026-11-06: "some stages jitter left and right by like 2 pixels rapidly when chars standing in
    // certain spots") is a picture moved one way and back. Judged by the picture's pixels, not the camera's words: what a
    // camera word says and what is drawn from it went apart before (DEFINITIVE.md). Reported at the end.
    bool JitterChecking() {
        static const bool on = getenv("SFIII3_JITTER_CHECK") != nullptr;
        return on;
    }
    constexpr uint32_t kJitterHold = 130, kJitterSettle = 80;
    constexpr int kJitterPlaces = 7;
    struct Jitter {
        uint32_t heldFor = 0;
        int place = 0;
        std::vector<uint32_t> last;
        int width = 0, height = 0;
        uint64_t places = 0, still = 0, stageBoard = 0, moved = 0, backAndForth = 0;
        int most = 0, lastSign = 0;
        uint64_t lastAt = 0;
        unsigned reports = 8;
        ~Jitter() {
            if (JitterChecking()) {
                fprintf(stderr, "stages: the jitter check: %llu places held, %llu frames of the fighters standing still compared (%llu of them a stage "
                                "board's stage), the stage's picture moved in %llu (back and forth in %llu, %d columns or lines at most)\n",
                        static_cast<unsigned long long>(places), static_cast<unsigned long long>(still), static_cast<unsigned long long>(stageBoard),
                        static_cast<unsigned long long>(moved), static_cast<unsigned long long>(backAndForth), most);
            }
        }
    } g_jitter;

    // How far the picture `now` moved from `was` (the same size): the shift, columns across (dx) and lines down (dy), that
    // lines up the most pixels, (0, 0) unless another lines up clearly more (the stage's own moving things, its people
    // and water, are a small part of it)
    void PictureShift(const std::vector<uint32_t> &was, const std::vector<uint32_t> &now, int width, int height, int &dx, int &dy) {
        constexpr int kAcross = 8, kDown = 3;
        int64_t best = -1, still = 0;
        dx = dy = 0;
        for (int sy = -kDown; sy <= kDown; sy++) {
            for (int sx = -kAcross; sx <= kAcross; sx++) {
                int64_t same = 0;
                for (int y = std::max(0, sy) + kDown; y < height - kDown && y - sy < height; y += 2) {
                    const uint32_t *a = &now[static_cast<size_t>(y) * width], *b = &was[static_cast<size_t>(y - sy) * width];
                    for (int x = kAcross; x < width - kAcross; x++) {
                        same += a[x] == b[x - sx];
                    }
                }
                if (sx == 0 && sy == 0) {
                    still = same;
                }
                if (same > best) {
                    best = same;
                    dx = sx;
                    dy = sy;
                }
            }
        }
        const int64_t compared = static_cast<int64_t>((height - 2 * kDown) / 2) * (width - 2 * kAcross);
        if (best <= still + compared / 200) {
            dx = dy = 0;
        }
    }

    void JitterFrame(Machine &machine) {
        Jitter &j = g_jitter;
        // (a stage chosen anew, or none: its places begun again)
        const bool composedNow = machine.StageBoard() && machine.composeNow && machine.composeNow();
        const int stageNow = composedNow ? g.loaded * 100 + StageNow() : -1 - Ram8(sfiii3::kStage);
        static int s_stage = -1000;
        if (!InMatch() || stageNow != s_stage) {
            s_stage = stageNow;
            j.heldFor = 0;
            j.last.clear();
            if (!InMatch()) {
                return;
            }
        }
        // (the fighters idle: the game's computer plays neither side, and nothing is pressed)
        for (int p = 0; p < 2; p++) {
            SetRam8(sfiii3::kPlayer[p] + 3, 1);
            machine.players[p] = 0;
        }
        if (j.heldFor == 0) {
            // (the next place: across the camera's span from end to end, the fighters close (60), apart (140) or far apart
            // (220), within the walls, 164 past the camera's ends)
            const int left = static_cast<int16_t>(Ram16(sfiii3::kCameraLeft[0])), right = static_cast<int16_t>(Ram16(sfiii3::kCameraRight[0]));
            if (right - left < 64) {
                return; // (not a fight's camera yet)
            }
            const int k = j.place % kJitterPlaces;
            const int gap = (j.place % 3 == 0) ? 60 : (j.place % 3 == 1) ? 140 : 220;
            const int middle = left - 120 + (right - left + 240) * k / (kJitterPlaces - 1);
            const int lo = left - 150, hi = right + 150;
            const int one = std::clamp(middle - gap / 2, lo, hi - gap), two = one + gap;
            SetRam16(sfiii3::kPlayer[0] + sfiii3::kX, static_cast<uint16_t>(one));
            SetRam16(sfiii3::kPlayer[1] + sfiii3::kX, static_cast<uint16_t>(two));
            j.place++;
            j.places++;
        }
        j.heldFor = (j.heldFor + 1) % kJitterHold;
        // The stage as it is shown: the stage board's stage when it is composed, else 3rd Strike's own
        static native::Rasteriser r;
        static std::vector<uint32_t> now;
        int w = 0, h = 0;
        r.margin = machine.WideMargin();
        r.text = false;
        const bool composed = machine.StageBoard() && machine.composeNow && machine.composeNow();
        if (composed) {
            r.keep = [](const void *, const native::Object &o, uint32_t) { return StageWords(o.words[0], o.words[2]); };
            r.Draw(machine.StageBoard()->native, machine.StageBoard()->SpriteRamBytes(), machine.StageBoard()->SsRam(), now, w, h);
        } else {
            r.keep = [](const void *, const native::Object &o, uint32_t) { return OwnStageWords(o.words[0], o.words[2]); };
            r.Draw(machine.board.native, machine.board.SpriteRamBytes(), machine.board.SsRam(), now, w, h);
        }
        const bool measured = j.heldFor > kJitterSettle && j.width == w && j.height == h && j.last.size() == now.size();
        if (measured) {
            int dx = 0, dy = 0;
            PictureShift(j.last, now, w, h, dx, dy);
            j.still++;
            j.stageBoard += composed;
            if (dx || dy) {
                j.moved++;
                const int sign = dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 2 : -2);
                if (j.lastSign == -sign && machine.frame - j.lastAt <= 3) {
                    j.backAndForth++;
                }
                j.lastSign = sign;
                j.lastAt = machine.frame;
                j.most = std::max({j.most, std::abs(dx), std::abs(dy)});
                if (j.reports) {
                    j.reports--;
                    const uint8_t *ram = composed ? machine.StageBoard()->mainRam : nullptr;
                    fprintf(stderr, "stages: the jitter check, frame %llu (place %d, the fighters at %d and %d): the stage's picture moved %d across, %d down; "
                                    "3rd Strike's camera %d,%d%s",
                            static_cast<unsigned long long>(machine.frame), j.place - 1, static_cast<int16_t>(Ram16(sfiii3::kPlayer[0] + sfiii3::kX)),
                            static_cast<int16_t>(Ram16(sfiii3::kPlayer[1] + sfiii3::kX)), dx, dy, static_cast<int16_t>(Ram16(sfiii3::kCameraX)),
                            static_cast<int16_t>(Ram16(sfiii3::kCameraY)), composed ? "" : "\n");
                    if (ram) {
                        fprintf(stderr, ", the stage board's %d (given %d)\n", static_cast<int16_t>(Word(ram, kChoices[g.loaded].game->camera[1])), g.given);
                    }
                }
            }
        }
        std::swap(j.last, now);
        j.width = w;
        j.height = h;
    }
} // namespace

namespace {
    // The training sessions' stages check (SFIII3_SESSION_STAGES=1: checkguests.sh): each frame of a session's fight,
    // which stage is shown, another game's (its set and number, its stage board in its fight and composed) or 3rd
    // Strike's own; reported at the end, the frames on each. A stage chosen on the session's Stage row must be the one
    // shown (the play-test of 2026-11-06: "can't select stages from other games in training mode menu").
    bool SessionStagesChecking() {
        static const bool on = getenv("SFIII3_SESSION_STAGES") != nullptr;
        return on;
    }
    struct SessionStages {
        std::vector<std::pair<std::string, uint64_t>> shown;
        ~SessionStages() {
            if (!SessionStagesChecking()) {
                return;
            }
            fprintf(stderr, "stages: training sessions' stages shown:");
            for (const auto &[what, frames] : shown) {
                fprintf(stderr, " %s %llu frames;", what.c_str(), static_cast<unsigned long long>(frames));
            }
            fprintf(stderr, "\n");
        }
    } g_sessionStages;
    void SessionStagesFrame(Machine &machine) {
        if (!session::InSession() || !InMatch()) {
            return;
        }
        const bool composed = machine.StageBoard() && machine.composeNow && machine.composeNow() && g.loaded && kChoices[g.loaded].game != &kThirdStrike;
        const std::string what = composed ? std::string(kChoices[g.loaded].game->set) + ":" + std::to_string(StageNow())
                                          : std::string("own:") + std::to_string(Ram8(sfiii3::kStage));
        auto &shown = g_sessionStages.shown;
        if (shown.empty() || shown.back().first != what) {
            if (getenv("SFIII3_SESSION_STAGES_DEBUG")) {
                fprintf(stderr, "session stage: frame %llu: %s\n", static_cast<unsigned long long>(machine.frame), what.c_str());
            }
            shown.push_back({what, 0});
        }
        shown.back().second++;
    }
} // namespace

namespace {
    // The layer check (SFIII3_LAYER_CHECK=1: checkguests.sh, checkstages.sh), against ground truth: a board drawing its own
    // fight, its stage and its fighters in its own order (stage pieces in front of the fighters drawn over them). The
    // stage's layers and the rest (the fighters, their shadows and effects) of that same board are composed as the
    // picture composes a stage from one board with the fighters of another, both ways the port does it: one frame of two
    // producers (Rasteriser::DrawWithGuest: a guest stage, and the modes' 3rd Strike stages from the stage board) and two
    // pictures, the stage's then the rest over it (the modes' stages fading or scrolling in, the board model's
    // composition). At each pixel a fighter is drawn at (its opaque objects: not shadows, which a composition of two
    // boards' colours makes otherwise), the composed picture must be the board's own. The ground truth: on a stage game's
    // stage, that game's own match on it (SFIII3_STAGE_COMPARE=play: the stage board plays it); on 3rd Strike's own
    // stages, 3rd Strike's fight. Reported at the end, with the frames in which the stage covered some of a fighter (the
    // stage has pieces in front, and they were met).
    bool LayerChecking() {
        static const bool on = getenv("SFIII3_LAYER_CHECK") != nullptr;
        return on;
    }
    struct LayerCheck {
        uint64_t frames = 0, covered = 0, oneFrame = 0, twoPictures = 0, coveredPixels = 0;
        unsigned reports = 6;
        ~LayerCheck() {
            if (LayerChecking()) {
                fprintf(stderr, "stages: the layer check: %llu frames, the stage in front of a fighter in %llu (%llu pixels); at the fighters' pixels the one "
                                "frame of two producers different from the board's own picture in %llu, the two pictures in %llu\n",
                        static_cast<unsigned long long>(frames), static_cast<unsigned long long>(covered), static_cast<unsigned long long>(coveredPixels),
                        static_cast<unsigned long long>(oneFrame), static_cast<unsigned long long>(twoPictures));
            }
        }
    } g_layers;
    using StageRule = bool (*)(uint32_t v1, uint32_t v3);
    bool GuestStageWords(uint32_t v1, uint32_t v3) {
        const uint32_t palette = v1 & 0x1FF;
        const bool sixBits = (v1 >> 9) & 1, blend = (v1 >> 10) & 1;
        return (v3 & 3) == 0 || !((sixBits && palette < 0x40) || (blend && palette == 0));
    }
    void LayerFrame(Machine &machine) {
        // (which board, and its stage's rule)
        machine::Machine *board = nullptr;
        StageRule rule = nullptr;
        if (ComparingPlay() && g_cmp.on && machine.StageBoard() && g.fought && g.offsetKnown) {
            board = machine.StageBoard(); // (the stage game's own match on the stage)
            rule = GuestStageWords;
        } else if (!g.loaded && InMatch() && !machine.onlyStage) {
            board = &machine.board; // (3rd Strike's own fight on its own stage)
            rule = OwnStageWords;
        }
        if (!board || !board->NativeForPicture()) {
            return;
        }
        static StageRule s_rule;
        s_rule = rule;
        const native::KeepObject stage = [](const void *, const native::Object &o, uint32_t) { return s_rule(o.words[0], o.words[2]); };
        const native::KeepObject rest = [](const void *, const native::Object &o, uint32_t) { return !s_rule(o.words[0], o.words[2]); };
        const native::KeepObject fighters = [](const void *, const native::Object &o, uint32_t) {
            return !s_rule(o.words[0], o.words[2]) && !((o.words[0] >> 10) & 1);
        };
        static native::Rasteriser r;
        static std::vector<uint32_t> truth, one, two, a, b;
        int w = 0, h = 0;
        r.margin = machine.WideMargin();
        r.text = false;
        r.overlay = false;
        // (the board's own picture)
        r.keep = nullptr;
        r.backdrop = 0;
        r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), truth, w, h);
        // (its fighters' pixels: drawn on two backdrops, the same on both)
        r.keep = fighters;
        r.backdrop = 0x01FF00FF;
        r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), a, w, h);
        r.backdrop = 0x0100FF00;
        r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), b, w, h);
        r.backdrop = 0;
        // (one frame of two producers, as the picture is drawn with a stage: the stage's pieces in front of the fighters,
        // as the frontmost of them has it, over them, by the picture's rule, Machine::frontPriority)
        static int s_front;
        s_front = machine.frontPriority ? machine.frontPriority(*board) : 0;
        r.front = s_front;
        r.keep = rest;
        r.DrawWithGuest(&board->native, board->SpriteRamBytes(), stage, nullptr, board->native, board->SpriteRamBytes(), board->SsRam(), one, w, h);
        r.front = 0;
        // (two pictures, as Machine::Compose draws them: the stage's, then the rest over it; with pieces in front of the
        // fighters, those over that, then the rest's in front of them)
        static bool s_bands;
        static int s_piece;
        s_bands = false;
        s_piece = 0x100;
        for (const native::Object &o : board->native.objects) {
            if (s_rule(o.words[0], o.words[2]) && native::InFront(o.priority, s_front)) {
                s_bands = true;
                s_piece = std::min<int>(s_piece, o.priority);
            }
        }
        r.keep = [](const void *, const native::Object &o, uint32_t) { return s_rule(o.words[0], o.words[2]) && !(s_bands && native::InFront(o.priority, s_front)); };
        r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), two, w, h);
        r.overlay = true;
        r.keep = [](const void *, const native::Object &o, uint32_t) { return !s_rule(o.words[0], o.words[2]) && !(s_bands && native::InFront(o.priority, s_piece)); };
        r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), two, w, h);
        if (s_bands) {
            r.keep = [](const void *, const native::Object &o, uint32_t) { return s_rule(o.words[0], o.words[2]) && native::InFront(o.priority, s_front); };
            r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), two, w, h);
            r.keep = [](const void *, const native::Object &o, uint32_t) { return !s_rule(o.words[0], o.words[2]) && native::InFront(o.priority, s_piece); };
            r.Draw(board->native, board->SpriteRamBytes(), board->SsRam(), two, w, h);
        }
        r.overlay = false;
        r.keep = nullptr;
        const size_t n = static_cast<size_t>(w) * h;
        if (truth.size() < n || a.size() < n || b.size() < n || one.size() < n || two.size() < n) {
            return;
        }
        uint64_t covered = 0, oneOff = 0, twoOff = 0;
        int firstX = -1, firstY = -1;
        for (size_t i = 0; i < n; i++) {
            if (a[i] != b[i]) {
                continue; // (no fighter here)
            }
            covered += truth[i] != a[i];
            if (one[i] != truth[i] && firstX < 0) {
                firstX = static_cast<int>(i % w);
                firstY = static_cast<int>(i / w);
            }
            oneOff += one[i] != truth[i];
            twoOff += two[i] != truth[i];
        }
        g_layers.frames++;
        g_layers.covered += covered > 0;
        g_layers.coveredPixels += covered;
        g_layers.oneFrame += oneOff > 0;
        g_layers.twoPictures += twoOff > 0;
        if ((oneOff || twoOff) && g_layers.reports) {
            g_layers.reports--;
            // (debugging aid, SFIII3_LAYER_SHOTS=PREFIX: the board's own picture and the one frame composed, as reported)
            if (const char *shots = getenv("SFIII3_LAYER_SHOTS")) {
                const std::string at = std::string(shots) + std::to_string(machine.frame);
                WritePng((at + "-own.png").c_str(), truth.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                WritePng((at + "-composed.png").c_str(), one.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
            fprintf(stderr, "stages: the layer check, frame %llu: a fighter's pixels covered by the stage %llu; composed otherwise than the board's own picture: "
                            "%llu (one frame; the first at %d,%d), %llu (two pictures)\n",
                    static_cast<unsigned long long>(machine.frame), static_cast<unsigned long long>(covered), static_cast<unsigned long long>(oneOff),
                    firstX, firstY, static_cast<unsigned long long>(twoOff));
        }
    }
} // namespace

// After the frame's other sources (the modes' too): the checks that drive the fight or watch it (SFIII3_JITTER_CHECK,
// SFIII3_SESSION_STAGES, SFIII3_LAYER_CHECK)
namespace {
    // The pop-in check (SFIII3_POPIN_CHECK=1: checkpopin.sh), in widescreen, each frame a stage is shown (3rd Strike's own
    // in a fight, a stage board's composed, the stage viewer's): the stage's layers drawn as the picture has them, its
    // objects alone and each of its tilemaps alone, against the frame before. A stage piece that appears or goes in one
    // frame, whole, inside the picture (its pixels drawn where nothing of the stage's objects was within a few columns
    // the frame before, a patch of kPopArea or more, not at the picture's outer edge where things slide in) is pop-in;
    // counted in the margins (the columns widescreen adds) and in the board's 384 between (what the 4:3 game would show
    // appearing too: animation, things the stage brings on). A tilemap's cell (16 columns of 8 lines) that goes
    // from nothing to something or back, seen so, once its band of lines is lined up with the frame before (a column
    // brought in late), likewise. And columns at the picture's ends with nothing
    // of the stage at all (black: what was never meant to be seen). Reported by stage at the end; the first few frames
    // with pop-in in the margins written as strips (the frame before | the frame | red where it appeared) to
    // SFIII3_POPIN_STRIPS's folder. The play-test of 2026-11-07: "in widescreen (at least stage viewer mode) stage elements
    // that would be offscreen on 4:3 pop in at the edges of 16:9".
    bool PopInChecking() {
        static const bool on = getenv("SFIII3_POPIN_CHECK") != nullptr;
        return on;
    }
    constexpr int kPopArea = 160, kPopNear = 6, kPopEdge = 4;
    struct PopStats {
        uint64_t frames = 0, objectsMargin = 0, objectsMiddle = 0, cellsMargin = 0, cellsMiddle = 0, blackFrames = 0;
    };
    struct PopIn {
        std::vector<uint32_t> objects, tiles[4], whole;
        int width = 0, height = 0;
        std::string stage;
        std::vector<std::pair<std::string, PopStats>> stats;
        unsigned strips = 6;
        ~PopIn() {
            if (!PopInChecking()) {
                return;
            }
            for (const auto &[name, s] : stats) {
                fprintf(stderr, "popin: stage %s: %llu frames compared; objects appearing or going in the margins %llu (between them %llu); tilemap cells "
                                "changing in the margins %llu (between them %llu); frames with an end column with nothing of the stage %llu\n",
                        name.c_str(), static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.objectsMargin),
                        static_cast<unsigned long long>(s.objectsMiddle), static_cast<unsigned long long>(s.cellsMargin),
                        static_cast<unsigned long long>(s.cellsMiddle), static_cast<unsigned long long>(s.blackFrames));
            }
        }
    } g_pop;

    // Patches of pixels drawn in `now` with nothing drawn within kPopNear of them in `was`: (count of patches of kPopArea or
    // more inside the picture, not touching its outer kPopEdge columns) in the margins and between; `mark` set where they are
    // (`was` moved by the frame's move, ox across and oy down, first; a patch also kPopSize across and down at least: not
    // the thin lines a layer's line objects leave as its lines scroll)
    constexpr int kPopSize = 8;
    void Appeared(const std::vector<uint32_t> &was, const std::vector<uint32_t> &now, int w, int h, int margin, int ox, int oy, uint64_t &inMargin,
                  uint64_t &inMiddle, std::vector<uint8_t> &mark) {
        // (was's coverage spread kPopNear each way: a box sum)
        std::vector<int> sum(static_cast<size_t>(w + 1) * (h + 1), 0);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                sum[static_cast<size_t>(y + 1) * (w + 1) + x + 1] = (was[static_cast<size_t>(y) * w + x] != 0) + sum[static_cast<size_t>(y) * (w + 1) + x + 1] +
                                                                    sum[static_cast<size_t>(y + 1) * (w + 1) + x] - sum[static_cast<size_t>(y) * (w + 1) + x];
            }
        }
        const auto near = [&](int x, int y) {
            x -= ox;
            y -= oy;
            const int x0 = std::max(0, x - kPopNear), x1 = std::min(w, x + kPopNear + 1), y0 = std::max(0, y - kPopNear), y1 = std::min(h, y + kPopNear + 1);
            return sum[static_cast<size_t>(y1) * (w + 1) + x1] - sum[static_cast<size_t>(y0) * (w + 1) + x1] - sum[static_cast<size_t>(y1) * (w + 1) + x0] +
                       sum[static_cast<size_t>(y0) * (w + 1) + x0] > 0;
        };
        std::vector<uint8_t> fresh(static_cast<size_t>(w) * h, 0);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                fresh[static_cast<size_t>(y) * w + x] = now[static_cast<size_t>(y) * w + x] != 0 && !near(x, y);
            }
        }
        std::vector<int> stack;
        for (int start = 0; start < w * h; start++) {
            if (fresh[start] != 1) {
                continue;
            }
            int area = 0, left = w, right = -1, top = h, bottom = -1;
            int64_t across = 0;
            std::vector<int> patch;
            stack.assign(1, start);
            fresh[start] = 2;
            while (!stack.empty()) {
                const int at = stack.back();
                stack.pop_back();
                patch.push_back(at);
                const int x = at % w, y = at / w;
                area++;
                across += x;
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                const int next[4] = {x > 0 ? at - 1 : -1, x + 1 < w ? at + 1 : -1, y > 0 ? at - w : -1, y + 1 < h ? at + w : -1};
                for (const int n : next) {
                    if (n >= 0 && fresh[n] == 1) {
                        fresh[n] = 2;
                        stack.push_back(n);
                    }
                }
            }
            if (area < kPopArea || right - left + 1 < kPopSize || bottom - top + 1 < kPopSize || left < kPopEdge || right >= w - kPopEdge) {
                continue;
            }
            // (and nothing of the stage's objects anywhere in its box the frame before: a thing that was there and moved
            // its arm, a spectator's animation, is not one that came on)
            {
                const int x0 = std::max(0, left - ox), x1 = std::min(w, right + 1 - ox), y0 = std::max(0, top - oy), y1 = std::min(h, bottom + 1 - oy);
                if (x1 > x0 && y1 > y0 &&
                    sum[static_cast<size_t>(y1) * (w + 1) + x1] - sum[static_cast<size_t>(y0) * (w + 1) + x1] - sum[static_cast<size_t>(y1) * (w + 1) + x0] +
                            sum[static_cast<size_t>(y0) * (w + 1) + x0] > 0) {
                    continue;
                }
            }
            const int middle = static_cast<int>(across / area);
            const bool inTheMargin = middle < margin || middle >= w - margin;
            (inTheMargin ? inMargin : inMiddle)++;
            if (inTheMargin) {
                for (const int at : patch) {
                    mark[at] = 1;
                }
            }
        }
    }

    void PopInFrame(Machine &machine) {
        PopIn &p = g_pop;
        const int margin = machine.WideMargin();
        const bool composed = machine.StageBoard() && machine.composeNow && machine.composeNow();
        if (!InMatch() || margin <= 0 || (machine.onlyStage && !composed)) {
            p.width = 0;
            return;
        }
        const machine::Machine &m = composed ? *machine.StageBoard() : machine.board;
        const std::string stage = composed ? std::string(kChoices[g.loaded].game->set) + ":" + std::to_string(StageNow())
                                           : std::string("sfiii3:") + std::to_string(Ram8(sfiii3::kStage));
        static native::Rasteriser r;
        r.margin = margin;
        r.text = false;
        struct Keep {
            bool guest;
            int tilemap; // (-1: the objects, not the tilemaps' lines; -2 all of the stage)
        } keep{composed, -1};
        r.keepContext = &keep;
        r.keep = [](const void *context, const native::Object &o, uint32_t) {
            const Keep &k = *static_cast<const Keep *>(context);
            if (!(k.guest ? StageWords(o.words[0], o.words[2]) : OwnStageWords(o.words[0], o.words[2]))) {
                return false;
            }
            const bool line = (o.words[2] & 3) == 0;
            return k.tilemap == -2 || (k.tilemap == -1 ? !line : line && static_cast<int>((o.words[2] >> 4) & 3) == k.tilemap);
        };
        std::vector<uint32_t> objects, tiles[4], whole;
        int w = 0, h = 0;
        r.Draw(m.native, m.SpriteRamBytes(), m.SsRam(), objects, w, h);
        for (int t = 0; t < 4; t++) {
            keep.tilemap = t;
            r.Draw(m.native, m.SpriteRamBytes(), m.SsRam(), tiles[t], w, h);
        }
        keep.tilemap = -2;
        r.Draw(m.native, m.SpriteRamBytes(), m.SsRam(), whole, w, h);
        bool compared = p.width == w && p.height == h && p.stage == stage && p.objects.size() == objects.size();
        if (compared) {
            // (not a frame the picture jumped in, further than the checks reach: a round's start, a camera put somewhere at
            // once; the whole stage lined up with the frame before at least half the same)
            int wx = 0, wy = 0;
            PictureShift(p.whole, whole, w, h, wx, wy);
            int64_t same = 0, all = 0;
            for (int y = std::max(0, wy); y < h + std::min(0, wy); y += 2) {
                for (int x = std::max(0, wx); x < w + std::min(0, wx); x += 2) {
                    same += whole[static_cast<size_t>(y) * w + x] == p.whole[static_cast<size_t>(y - wy) * w + x - wx];
                    all++;
                }
            }
            compared = same * 2 >= all;
        }
        if (compared) {
            auto it = std::find_if(p.stats.begin(), p.stats.end(), [&](const auto &one) { return one.first == stage; });
            if (it == p.stats.end()) {
                p.stats.push_back({stage, {}});
                it = p.stats.end() - 1;
            }
            PopStats &s = it->second;
            s.frames++;
            std::vector<uint8_t> mark(static_cast<size_t>(w) * h, 0);
            const uint64_t before = s.objectsMargin;
            int ox = 0, oy = 0;
            PictureShift(p.objects, objects, w, h, ox, oy);
            Appeared(p.objects, objects, w, h, margin, ox, oy, s.objectsMargin, s.objectsMiddle, mark); // (appearing)
            Appeared(objects, p.objects, w, h, margin, -ox, -oy, s.objectsMargin, s.objectsMiddle, mark); // (going)
            // (each tilemap lined up with the frame before; then its cells that changed, but the columns brought in at the
            // picture's ends by the move)
            // (lined up a band of kBand lines at a time, across only (a layer's lines scroll each their own way: the
            // floor's), the frame's move up taken from the whole layer; a band that does not line up nearly whole, as
            // when the camera jumps further than kReach, is passed by)
            const uint64_t cellsBefore = s.cellsMargin;
            constexpr int kBand = 8, kReach = 16, kCell = 16;
            for (int t = 0; t < 4; t++) {
                int gx = 0, dy = 0;
                PictureShift(p.tiles[t], tiles[t], w, h, gx, dy);
                for (int by = std::max(0, dy); by + kBand <= h + std::min(0, dy); by += kBand) {
                    int best = -1, dx = 0, drawn = 0;
                    for (int sx = -kReach; sx <= kReach; sx++) {
                        int same = 0;
                        for (int y = by; y < by + kBand; y += 2) {
                            const uint32_t *a = &tiles[t][static_cast<size_t>(y) * w], *b = &p.tiles[t][static_cast<size_t>(y - dy) * w];
                            for (int x = kReach; x < w - kReach; x += 2) {
                                same += a[x] == b[x - sx] && a[x] != 0;
                            }
                        }
                        if (same > best) {
                            best = same;
                            dx = sx;
                        }
                    }
                    for (int y = by; y < by + kBand; y += 2) {
                        for (int x = kReach; x < w - kReach; x += 2) {
                            drawn += tiles[t][static_cast<size_t>(y) * w + x] != 0;
                        }
                    }
                    if (drawn < 32 || best * 10 < drawn * 8) {
                        continue;
                    }
                    for (int cx = kPopEdge + kReach; cx + kCell <= w - kPopEdge - kReach; cx += kCell / 2) {
                        int changed = 0;
                        for (int y = by; y < by + kBand; y++) {
                            for (int x = cx; x < cx + kCell; x++) {
                                const uint32_t a = tiles[t][static_cast<size_t>(y) * w + x], b = p.tiles[t][static_cast<size_t>(y - dy) * w + x - dx];
                                // (nothing of it there and now something, or the other way, and seen so in the picture: not a colour changing, as
                                // a neon sign flashes, nor a layer behind another changing where it is covered)
                                const size_t at = static_cast<size_t>(y) * w + x, was = static_cast<size_t>(y - dy) * w + x - dx;
                                changed += (a == 0) != (b == 0) && whole[at] != p.whole[was];
                            }
                        }
                        if (changed > kCell * kBand / 2) {
                            const bool inTheMargin = cx + kCell / 2 < margin || cx + kCell / 2 >= w - margin;
                            (inTheMargin ? s.cellsMargin : s.cellsMiddle)++;
                            if (inTheMargin) {
                                for (int y = by; y < by + kBand; y++) {
                                    for (int x = cx; x < cx + kCell; x++) {
                                        mark[static_cast<size_t>(y) * w + x] = 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // (a column at either end with nothing of the stage in it)
            bool black = false;
            for (const int x : {0, w - 1}) {
                bool any = false;
                for (int y = 0; y < h && !any; y++) {
                    any = whole[static_cast<size_t>(y) * w + x] != 0;
                }
                black = black || !any;
            }
            s.blackFrames += black;
            if ((s.objectsMargin != before || s.cellsMargin != cellsBefore) && getenv("SFIII3_POPIN_LOG")) { // (debugging aid)
                fprintf(stderr, "popin: frame %llu (the board's %llu), stage %s: objects %llu, cells %llu in the margins\n",
                        static_cast<unsigned long long>(machine.frame), static_cast<unsigned long long>(m.Frame()), stage.c_str(),
                        static_cast<unsigned long long>(s.objectsMargin - before), static_cast<unsigned long long>(s.cellsMargin - cellsBefore));
            }
            const char *folder = getenv("SFIII3_POPIN_STRIPS");
            if ((s.objectsMargin != before || s.cellsMargin != cellsBefore || black) && folder && p.strips) {
                p.strips--;
                std::vector<uint32_t> strip(static_cast<size_t>(w) * 3 * h);
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        const size_t at = static_cast<size_t>(y) * w + x, to = static_cast<size_t>(y) * w * 3 + x;
                        strip[to] = p.whole[at];
                        strip[to + w] = whole[at];
                        strip[to + 2 * w] = mark[at] ? 0x0000FFu : (whole[at] >> 2) & 0x3F3F3Fu;
                    }
                }
                char path[600];
                snprintf(path, sizeof(path), "%s/popin-%s-%06llu.png", folder, stage.c_str(), static_cast<unsigned long long>(machine.frame));
                for (char *c = path + strlen(folder); *c; c++) {
                    *c = *c == ':' ? '_' : *c;
                }
                WritePng(path, strip.data(), static_cast<uint32_t>(w * 3), static_cast<uint32_t>(h));
            }
        }
        p.objects = std::move(objects);
        for (int t = 0; t < 4; t++) {
            p.tiles[t] = std::move(tiles[t]);
        }
        p.whole = std::move(whole);
        p.width = w;
        p.height = h;
        p.stage = stage;
    }
} // namespace

void StageChecksBeforeFrame(Machine &machine) {
    if (PopInChecking()) {
        PopInFrame(machine);
    }
    // (debugging aid, SFIII3_PRIORITY_STATS=1: over the run, the priorities of each kind of object, on each board: the
    // fighters (6 bits a pixel, palettes below 0x40, not blending), their shadows and the rest of what is not the stage,
    // and the stage's; and the frames with a stage object in front of the frontmost fighter)
    static const bool stats = getenv("SFIII3_PRIORITY_STATS") != nullptr;
    if (stats) {
        struct Stats {
            uint64_t kinds[2][3][256] = {};
            uint64_t frontFrames[2] = {}, frames[2] = {};
            ~Stats() {
                static const char *const kKinds[3] = {"fighters", "the rest", "the stage"};
                for (int b = 0; b < 2; b++) {
                    fprintf(stderr, "priority stats, %s (%llu frames, a stage object in front of the frontmost fighter in %llu):\n", b ? "the stage board" : "3rd Strike's board",
                            static_cast<unsigned long long>(frames[b]), static_cast<unsigned long long>(frontFrames[b]));
                    for (int k = 0; k < 3; k++) {
                        fprintf(stderr, "  %s:", kKinds[k]);
                        for (int p = 0; p < 256; p++) {
                            if (kinds[b][k][p]) {
                                fprintf(stderr, " %d(%llu)", p, static_cast<unsigned long long>(kinds[b][k][p]));
                            }
                        }
                        fprintf(stderr, "\n");
                    }
                }
            }
        };
        static Stats st;
        for (machine::Machine *m : {&machine.board, machine.StageBoard()}) {
            if (!m || !OnStage()) {
                continue;
            }
            const int b = m == &machine.board ? 0 : 1;
            st.frames[b]++;
            int front = 256, stageFront = 256;
            for (const native::Object &o : m->native.objects) {
                const bool isStage = b == 0 ? OwnStageWords(o.words[0], o.words[2]) : GuestStageWords(o.words[0], o.words[2]);
                const uint32_t palette = o.words[0] & 0x1FF;
                const bool fighter = !isStage && ((o.words[0] >> 9) & 1) && palette < 0x40 && !((o.words[0] >> 10) & 1);
                st.kinds[b][isStage ? 2 : fighter ? 0 : 1][o.priority]++;
                if (fighter) {
                    front = std::min<int>(front, o.priority);
                }
                if (isStage && o.priority != native::kNoPriority) {
                    stageFront = std::min<int>(stageFront, o.priority);
                }
            }
            st.frontFrames[b] += stageFront < front;
        }
    }
    // (debugging aid, SFIII3_PRIORITIES=EVERY: every so many frames, each board's objects by their heads' priorities: how
    // many, how many are its stage's, their palettes)
    static const long long every = getenv("SFIII3_PRIORITIES") ? atoll(getenv("SFIII3_PRIORITIES")) : 0;
    if (every > 0 && machine.frame % static_cast<uint64_t>(every) == 0) {
        for (machine::Machine *m : {&machine.board, machine.StageBoard()}) {
            if (!m) {
                continue;
            }
            const bool own = m == &machine.board;
            fprintf(stderr, "priorities, frame %llu, %s:", static_cast<unsigned long long>(machine.frame), own ? "3rd Strike's board" : "the stage board");
            int last = -1, count = 0, stage = 0, lo = 0x200, hi = -1;
            const auto flush = [&] {
                if (count) {
                    fprintf(stderr, " [%d: %d objects, %d stage, palettes %03X-%03X]", last, count, stage, lo, hi);
                }
            };
            for (const native::Object &o : m->native.objects) {
                if (o.priority != last) {
                    flush();
                    last = o.priority;
                    count = stage = 0;
                    lo = 0x200;
                    hi = -1;
                }
                count++;
                stage += own ? OwnStageWords(o.words[0], o.words[2]) : GuestStageWords(o.words[0], o.words[2]);
                lo = std::min<int>(lo, static_cast<int>(o.words[0] & 0x1FF));
                hi = std::max<int>(hi, static_cast<int>(o.words[0] & 0x1FF));
            }
            flush();
            fprintf(stderr, "\n");
        }
    }
    // (the checks, SFIII3_STAGE_PICTURES=PREFIX:EVERY: every so many frames of a fight, the stage as it is shown, its
    // layers drawn alone (the stage board's stage when it is composed, else this board's own: no fighters, no panel),
    // to PREFIX<frame>.png; stagecheck.py names the stage shown from them: checkviewer.sh)
    static const char *pictures = getenv("SFIII3_STAGE_PICTURES");
    if (pictures && InMatch()) {
        const std::string spec = pictures;
        const size_t colon = spec.rfind(':');
        const uint64_t each = colon == std::string::npos ? 60 : std::max(1ull, strtoull(spec.c_str() + colon + 1, nullptr, 10));
        const bool composed = machine.StageBoard() && machine.composeNow && machine.composeNow();
        // (the stage viewer: only once its stage is shown, not the unseen fight's own before)
        if (machine.frame % each == 0 && (composed || !machine.onlyStage)) {
            static native::Rasteriser r;
            static std::vector<uint32_t> shown;
            int w = 0, h = 0;
            r.margin = machine.WideMargin();
            r.text = false;
            if (composed) {
                r.keep = [](const void *, const native::Object &o, uint32_t) { return StageWords(o.words[0], o.words[2]); };
                r.Draw(machine.StageBoard()->native, machine.StageBoard()->SpriteRamBytes(), machine.StageBoard()->SsRam(), shown, w, h);
            } else {
                r.keep = [](const void *, const native::Object &o, uint32_t) { return OwnStageWords(o.words[0], o.words[2]); };
                r.Draw(machine.board.native, machine.board.SpriteRamBytes(), machine.board.SsRam(), shown, w, h);
            }
            char path[600];
            snprintf(path, sizeof(path), "%s%06llu.png", spec.substr(0, colon).c_str(), static_cast<unsigned long long>(machine.frame));
            WritePng(path, shown.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
    }
    if (JitterChecking()) {
        JitterFrame(machine);
    }
    if (LayerChecking()) {
        LayerFrame(machine);
    }
    if (SessionStagesChecking()) {
        SessionStagesFrame(machine);
    }
}

void StagesBeforeFrame(Machine &machine) {
    if (Comparing()) {
        CompareStages(machine);
    }
    // (debugging aid, SFIII3_STAGE_TRACE=FILE: each shown frame, the stage board's camera and layers as its last frame
    // left them, its fighters' camera words, its scrolls and zoom)
    static FILE *trace = getenv("SFIII3_STAGE_TRACE") ? fopen(getenv("SFIII3_STAGE_TRACE"), "w") : nullptr;
    if (trace && g.loaded && machine.StageBoard() && kChoices[g.loaded].game != &kThirdStrike) {
        const StageGame &game = *kChoices[g.loaded].game;
        const uint8_t *ram = machine.StageBoard()->mainRam;
        const uint32_t fighters = game.camera[1] - 0x20, head = fighters - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
        fprintf(trace, "%llu fought %d given %d 3s %d,%d | head moved %d,%d now %d held? | f", static_cast<unsigned long long>(machine.frame), g.fought, g.given,
                static_cast<int16_t>(Ram16(sfiii3::kCameraX)), static_cast<int16_t>(Ram16(sfiii3::kCameraY)), static_cast<int16_t>(Word(ram, head + 0x12)),
                static_cast<int16_t>(Word(ram, head + 0x14)), static_cast<int16_t>(Word(ram, head + 0x40)));
        for (const uint32_t p : game.players) {
            fprintf(trace, " %d,%d,%d,%d", static_cast<int16_t>(Word(ram, p + 0x60)), static_cast<int16_t>(Word(ram, p + 0x62)), static_cast<int16_t>(Word(ram, p + 0x64)),
                    static_cast<int16_t>(Word(ram, p + 0x68)));
        }
        fprintf(trace, " | L");
        for (int n = 0; n < 7; n++) {
            const uint32_t l = head + 0x54 + static_cast<uint32_t>(n) * 0x90;
            fprintf(trace, " %d:%d,%d,%d", static_cast<int16_t>(Word(ram, l + 4)), static_cast<int16_t>(Word(ram, l + 0x20)), static_cast<int16_t>(Word(ram, l + 0x1C)),
                    static_cast<int16_t>(Word(ram, l + 0x0A)));
        }
        const native::FrameRegisters &r = machine.StageBoard()->native.registers;
        fprintf(trace, " | T");
        for (int t = 0; t < 4; t++) {
            fprintf(trace, " %04X,%04X", r.tilemap[t][0], r.tilemap[t][1]);
        }
        fprintf(trace, " | S");
        for (int s = 0; s < 8; s++) {
            fprintf(trace, " %d,%d", r.scroll[s][0] & 0x3FF, r.scroll[s][1] & 0x3FF);
        }
        fprintf(trace, " | zoom %02X %02X | screen", r.screen[7] & 0xFF, r.screen[15] & 0xFF);
        for (int s = 0; s < 16; s++) {
            fprintf(trace, " %04X", r.screen[s]);
        }
        fprintf(trace, " | head");
        for (uint32_t at = 0; at < 0x54; at += 2) {
            fprintf(trace, " %04X", Word(ram, head + at));
        }
        // (and SFIII3_STAGE_TRACE_RAM=ADDRESS:BYTES, hex: those of its RAM too)
        static const char *more = getenv("SFIII3_STAGE_TRACE_RAM");
        if (more) {
            char *end;
            const uint32_t at = static_cast<uint32_t>(strtoul(more, &end, 16));
            const uint32_t bytes = *end == ':' ? static_cast<uint32_t>(strtoul(end + 1, nullptr, 16)) : 0x10;
            fprintf(trace, " | ram");
            for (uint32_t n = 0; n < bytes; n += 2) {
                fprintf(trace, " %04X", Word(ram, at + n));
            }
        }
        fprintf(trace, "\n");
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
    if (Checking()) {
        // (the stage check, each frame: no music of 3rd Strike's heard with the stage's (a voice keyed on whose music
        // channel plays and whose effect channel does not, as its sound driver gives the voices out: sound_driver.cpp),
        // none of the stage board's heard with no stage of its chosen; and its picture's zoom whole when its frame is)
        if (music) {
            g_check.songFrames++;
            int heard = 0;
            for (uint32_t v = 0; v < 16; v++) {
                const bool musics = (Ram8(0x02078D0C + v * 116 + 94) & 0x80) == 0, effects = (Ram8(0x0207944C + v * 116 + 94) & 0x80) == 0;
                heard += ((machine.board.keyOn >> v) & 1) && musics && !effects && !machine.board.silenced[v];
            }
            if (heard) {
                g_check.twoSongs++;
                char what[96];
                snprintf(what, sizeof(what), "%d voices of 3rd Strike's music heard with the stage's", heard);
                g_check.Report(3, what, machine.frame);
            }
        }
        if (machine.stageSound && !g.loaded) {
            g_check.heardAlone++;
        }
        if (g.loaded && g.fought && OnStage() && machine.StageBoard() && kChoices[g.loaded].game != &kThirdStrike) {
            const StageGame &game = *kChoices[g.loaded].game;
            const uint8_t *ram = machine.StageBoard()->mainRam;
            const uint32_t head = game.camera[1] - 0x20 - 0x54 - static_cast<uint32_t>(game.cameraLayer) * 0x90;
            const native::FrameRegisters &r = machine.StageBoard()->native.registers;
            const int zoomX = r.screen[7] & 0xFF, zoomY = r.screen[15] & 0xFF;
            // (its frame whole since the frame before: the zoom handed over at the vertical blank, a frame after)
            static bool wasWhole = false;
            const bool whole = Word(ram, head + 0x2E) == 64 && Word(ram, head + 0x30) == 64;
            const bool wholeBefore = wasWhole;
            wasWhole = whole;
            if (whole && wholeBefore && (zoomX != 0x40 || zoomY != 0x40)) {
                g_check.zoom++;
                char what[96];
                snprintf(what, sizeof(what), "its picture's zoom %02X %02X, its frame whole", zoomX, zoomY);
                g_check.Report(4, what, machine.frame);
            }
        }
    }
    // (debugging aid, SFIII3_STAGE_SOUND=1: every 300 frames, the voices left out and the stage board's sound)
    static const bool log = getenv("SFIII3_STAGE_SOUND") != nullptr;
    if (log && machine.frame % 300 == 0) {
        fprintf(stderr, "stage sound: frame %llu, %s, %d of 3rd Strike's voices left out as music\n", static_cast<unsigned long long>(machine.frame),
                music ? "the stage's music" : "3rd Strike's own", silenced);
    }
    const int menu = std::clamp(FeatureValue("stages"), 0, static_cast<int>(std::size(kNames)) - 1);
    const int choice = g_choiceWanted ? g_choiceWanted : g_sessionChoice ? g_sessionChoice : menu;
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
    // (debugging aid, SFIII3_DUMP_STAGE_PROGRAM=PREFIX: the stage game's deciphered program, its first 2 MB (its code) as its CPU
    // reads them, to PREFIX<set>.program, for finding its routines)
    if (const char *dump = getenv("SFIII3_DUMP_STAGE_PROGRAM"); dump && machine.StageBoard()) {
        if (FILE *f = fopen((std::string(dump) + kChoices[choice].game->set + ".program").c_str(), "wb")) {
            fwrite(machine.StageBoard()->ProgramBytes(), 1, 0x200000, f);
            fclose(f);
        }
        if (FILE *f = fopen((std::string(dump) + kChoices[choice].game->set + ".data").c_str(), "wb")) {
            fwrite(machine.StageBoard()->ProgramDataBytes(), 1, 0x200000, f); // (as its data reads see it: 2nd Impact's literals)
            fclose(f);
        }
    }
    if (choice && !machine.StageBoard()) {
        g.loaded = 0; // (not there: as without it)
        SetFeatureValue("stages", 0);
    } else if (choice && Alone(*kChoices[choice].game)) {
        g.stage = kChoices[choice].stage == kOpponents ? -1 : g.stage;
        TripFromStart(machine);
    }
}

// A training session's stage of another game (training.cpp, its Stage row): the choice's New Generation's stages from 1
// (its 0-12), then 2nd Impact's (0-14), in place of the Display page's choice while the session is on (0: none, that
// choice again). (Until 2026-11-06 the row had 3rd Strike's stages only: another game's stage in a session came only from
// the Display page's choice.)
void StageForSession(int choice) {
    g_sessionChoice = std::clamp(choice, 0, kOpponentsFirst - 1);
}
// (the names of those choices, in order)
std::vector<const char *> GuestStageNames() {
    std::vector<const char *> names;
    for (int c = 1; c < kOpponentsFirst; c++) {
        names.push_back(kNames[c]);
    }
    return names;
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
// ... or another game's stage (the stage viewer's New Generation's and 2nd Impact's: GuestStageNames' choice, from 1),
// made alone as in a fight on it, its own music
void StageBehindGuest(int choice) {
    StageBehind(-1);
    g_choiceWanted = std::clamp(choice, 1, kOpponentsFirst - 1);
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
        machine->frontPriority = FrontOf;
        machine->composeNow = [] { return !g_pre.on && OnStage() && g.fought && (kChoices[g.loaded].game != &kThirdStrike || g.inFight > kBannersFor) && (kChoices[g.loaded].stage != kOpponents || g.stage >= 0); };
        // (on its way to its fight: eight frames a frame; in it one, its own pace (its animations and music))
        machine->stageFramesNow = [] { return g_booting ? kBootFrames : g.fought || Comparing() ? 1 : 8; }; // (the check: one, the two boards in step)
    }
}

} // namespace cps3recomp
