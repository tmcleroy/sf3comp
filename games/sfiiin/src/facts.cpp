// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// New Generation's facts for the sources the Street Fighter III games share (games/shared/sf3/facts.h). The set is
// sfiiin (Asia 970204, NO CD). Each address was carried over from 3rd Strike's (games/sfiii3/src/sfiii3.h) with
// cps3recomp/tools/crossmap.py and then checked in RAM dumps of a run of made-up play (--auto 7): at the title
// (frame 700), at character select (1500) and in a fight (2250, 3000, 3750).
//
// Training mode's facts were found from the game's own routines (their literal pools, by the routines' names:
// tools/names.py, tools/crossmap.py), the records' layouts as the routines use them, and RAM traced a frame at a time
// through scripted training sessions (games/shared/sf3/checktraining.sh's play: --trace of the players' records,
// --dump-ram, pictures with the boxes drawn). "Checked" below names the run that showed it.
#include "facts.h"

namespace sf3 {

namespace {
    // (by the record's character number: Ryu 2 and Ken 11 read in a session of Ryu against Ken; the stage byte's numbers
    // are the same characters', each seen in a picture; 6 is not a character of this game)
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "?", "Ibuki", "Elena", "Oro", "Yang", "Ken", "Sean"};
    // (each stage number written to the stage byte and the fight pictured: 0 to 12 are stages, 13 on a black screen. 6
    // is a stage no character of this game has: the one 2nd Impact gives Hugo.)
    const char *const kStageNames[] = {"Gill's", "Alex's", "Ryu's", "Yun's", "Dudley's", "Necro's", "Ibuki's", "Elena's", "Oro's", "Yang's", "Ken's", "Sean's",
                                       "The spare one (number 6: 2nd Impact's Hugo's)"};
    const uint8_t kStageNumbers[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 6};
} // namespace

const Facts &TheFacts() {
    static const Facts facts = {
        .opening = 780,        // (black at frame 800, the opening's first picture by 900)
        .credits = 0x02007188, // Ck_Coin, service_coin_check, credit_use; counts the run's coins
        .clock = 0x0200EB33,   // 94, 80, 66 at the three fight frames, as the pictures show
        // (counter_control's second literal, the word after the clock's: traced counting down from 60 a frame, the clock
        // going down as it runs out; kept above 0 in a session the picture's clock stays at 99)
        .clockFrames = 0x0200EB34,
        // (3rd Strike's less 0x2852, as their neighbours: 00/00 at the title, FF/80 at select with one player in,
        // FF/FF in the fight; the state 0, 1, 2)
        .locked = {0x02012C74, 0x02012C76},
        .matchState = 0x02012C55,
        .player = {0x0200D18C, 0x0200D564}, // 38 routines of the same name load the first where 3rd Strike's load its
        .life = 0x9F,          // 160 at the round's start, going down with the bar in the pictures (both players)
        .fullLife = 160,
        .bonusGames = false,   // (the game has none: the settings' byte 0x27 is 0)
        .roundsBetweenTwo = false,
        // (the settings' byte 0x20 set to 1 at start-up: the board's picture 495 columns (its timing registers 0x76 to 0x265,
        // set_screen_mode's 7 where it is otherwise 3), the panel out at the corners, the fighters' start and the camera's
        // ends the game's own for it. Of the three bytes of RAM that then differ from a start without it and stay so, this
        // is the one test_mode_exit (0x06005692) reads to choose the mode, and the game's routines read throughout. Seen
        // in fights on every stage: games/shared/sf3/checkwide.sh)
        .boardWide = 0x0205ED46,
        // (Sel_PL_3rd's literal 0x02012CEA is SP_No, a word for each of four steps for each player: traced through a
        // made-up game, player 1's low byte 1, 2, 3, 4, 5 at frames 780-889; a session's start gets to the character
        // choice by it, and player 1's controls then choose for player 2)
        .selectState = {0x02012CEB, 0x02012CF3},
        // (bg_w is 0x0201BE10, where 26 routines load it for 3rd Strike's 0x02026BAC; the camera's words are at the same
        // places in it as 3rd Strike's (bgw[1].wxy at +0x104 and +0x108, r_limit +0x148, l_limit +0x14C). Checked: x is
        // about the middle of the two players' x in dumps; the hit boxes drawn through it sit on the fighters; the ends
        // read 196 and -196 on one stage, 345 and -326 on Ken's.)
        .cameraX = 0x0201BF14, .cameraY = 0x0201BF18, .cameraRight = 0x0201BF58, .cameraLeft = 0x0201BF5C,
        // (3rd Strike's number: put there by "Start over" with the camera at its end, the players stand at the picture's
        // edge at the stage's end, and player 1 walking on towards it stays at that x)
        .wall = 164,
        // (3rd Strike's stage byte less 0x2852: 11 in a session of Ryu against Ken, on Ken's stage; written while no
        // round is on, the next fight is on that stage: all 13 pictured)
        .stage = 0x02012CA3, .stageNames = kStageNames, .stageNumbers = kStageNumbers, .stages = 13,
        .characters = kCharacters, .characterCount = 13,
        .record = {
            .computer = 3,     // 1 for both players of a two-player fight; at 0 for player 2, it fights by itself (checked)
            .facing = 0x0A,    // 1 player 1 on the left, 0 player 2: the boxes drawn turned by it sit on both fighters
            .hitBy = 0x14,     // player 2's reads player 1's record's address from the first hit on
            .doing = 0x26, .which = 0x28, // 4 and 0 through a standing light punch, 0 again after; 1 and 25 while stunned
            .freeze = 0x45,    // 7 down to 0 over the frames after a light punch hits, its animation still meanwhile
            .x = 0x64, .y = 0x68, // traced: x changes walking, y rises and falls to 0 in a jump
            .animation = 0x1C0, .animationFrame = 0x1C4, // one address through a move; the frame steps by 4 as cg_ctr (0x1D4) runs out
            .cancelHit = 0x299, // 0 as a light punch starts, 1 from the frame it hits
            .hurtStop = 0x2C6, // 0xFFF9 the frame a light punch hits it; 0xFFF4 (-12) as it parries
            .hitCount = 0x2D6, // one more with every hit taken (the combo readout's hits are counted by it)
            .character = 0x334, // 2 for Ryu, 11 for Ken
            .thrown = 0x343,   // not 0 through each of 22 throws
            .busy = 0x346, .blocking = 0x347, // blocking: 0 until the dummy guards, 33 guards seen by it in 33 moves guarded
            .attacking = 0x39E, .inputTaken = 0x39F, // attacking: 1 from a punch's first frame until its attack's frame is over
            .commands = 0x32C, // 0x0201B6C4 and 0x0201BA3A in dumps: wcp[2], where 11 routines load it for 3rd Strike's
            .gauge = 0x364,    // 0x0200D93C and 0x0200D95C: super_arts[2], 32 bytes each
            .stun = 0x36C,     // 0x0200D97C and 0x0200D990
            .attackBoxes = 0x260, .attackBoxCount = 3, // (three boxes a set: set_jugde_area steps the attack index by 24)
            .throwBox = 0,     // (this game's throws are attack boxes)
            .hurtBoxes = 0x258,
        },
        // (the push box, the body's five, the attack's three, the one it is thrown by: drawn, they sit on the fighters, the
        // attack's on the arm or leg out; a fireball's record has a body and an attack box at its front)
        .boxes = {{0x270, 1, 2}, {0x258, 5, 0}, {0x260, 3, 1}, {0x268, 1, 4}},
        // (read in dumps: the fill 0x39 of a gauge 0x58 long, the stocks 0 of 2; set to the most, the picture's gauge
        // says MAX and the super art comes out, 10 times in a run. The kind is from the game's own routine alone
        // (sag_jmp_tbl by gauge_type): no art with a timer was tried.)
        .gauge = {.fill = 0x0C, .stocks = 0x16, .stocksMost = 0x14, .kind = 0x04},
        .gaugeRedraw = {0, 0}, // (spgauge_control compares the stocks with those it shows: no flag needed)
        // (the readout's numbers: 0 of 64 at the start, up with each hit and draining after; the timer 150 down while
        // stunned)
        .stun = {.most = 2, .timer = 4, .taken = 8},
        // (waza_flag[3], [4] and [5] of the commands' record, as the hit judgement reads them (defense_ground,
        // defense_sky): the dummy tapping towards or down as an attack comes parries it, 52 times in a run; a jump-in it parries
        // forward, 8 times, and jumping itself it parried 7 attacks in the air. This game's judgement has no window of
        // its own against jump-ins.)
        .parryWindow = {0x1D, 0x1F, 0x21, 0},
        .parryStop = -12,      // (set_paring_status; seen as each parry lands)
        // (effect_work_list_init's and push_effect_work's literals; a fireball in flight is the third record of list 3,
        // 0x800 bytes a record, its byte 2 the player's number)
        .effectList = 0x0205DF0A, .effects = 0x0201DE04, .effectSize = 0x800,
        .throwEscape = false,  // (the game has none)
        .quickStand = true,    // (down tapped as it lands: up 16 frames sooner after a sweep, 52 for 68)
        .stunMash = true,      // (mashing, stuns of 90 to 130 frames last 42 to 70)
    };
    return facts;
}

} // namespace sf3
