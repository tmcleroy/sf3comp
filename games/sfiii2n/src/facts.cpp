// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 2nd Impact's facts for the sources the Street Fighter III games share (games/shared/sf3/facts.h). The set is
// sfiii2n (Asia 970930, NO CD). Each address was carried over from 3rd Strike's (games/sfiii3/src/sfiii3.h) with
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
    // are the same characters', each seen in a picture)
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang", "Ken", "Sean", "Urien", "Akuma"};
    // (each stage number written to the stage byte and the fight pictured: 0 to 14 are these, 15 is Akuma's again)
    const char *const kStageNames[] = {"Gill's", "Alex's", "Ryu's", "Yun's", "Dudley's", "Necro's", "Hugo's", "Ibuki's", "Elena's", "Oro's", "Yang's", "Ken's",
                                       "Sean's", "Urien's", "Akuma's"};
    const uint8_t kStageNumbers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
} // namespace

const Facts &TheFacts() {
    static const Facts facts = {
        .opening = 780,        // (black at frame 800, the opening's first picture by 900)
        .credits = 0x02008528, // credit_display_render, Ck_Coin, service_coin_check, credit_use; counts the run's coins
        .clock = 0x02010167,   // 92, 78, 68 at the three fight frames, as the pictures show
        // (counter_control's literal, the word after the clock's (Counter_low): kept above 0 in a session the picture's
        // clock stays at 99)
        .clockFrames = 0x02010168,
        // (3rd Strike's less 0x120E, as their neighbours: 00/00 at the title, 80/80 at select, FF/FF in the fight;
        // the state 0, 1, 2, and 6 once the round is over)
        .locked = {0x020142B8, 0x020142BA},
        .matchState = 0x02014299,
        .player = {0x0200E504, 0x0200E910}, // 51 routines of the same name load the first where 3rd Strike's load its
        .life = 0x9F,          // 160 at the round's start, going down with the bar in the pictures (player 2's seen)
        .fullLife = 160,
        .bonusGames = true,
        .roundsBetweenTwo = false,
        // (as New Generation's, found the same way: the third of the three bytes of RAM that differ with the settings'
        // byte 0x20 set, 0x62 after the first as there)
        .boardWide = 0x02065106,
        // (Sel_PL_1st's literal 0x0201432E is SP_No: 5 and 5 with both characters chosen, in dumps; a session's start
        // gets to the character choice by it, and player 1's controls then choose for player 2)
        .selectState = {0x0201432F, 0x02014337},
        // (bg_w is 0x02022160, where 46 routines load it for 3rd Strike's 0x02026BAC; the camera's words are at the same
        // places in it as 3rd Strike's. Checked: x 699 with the players at 846 and 547; the hit boxes drawn through it
        // sit on the fighters; the ends read 796 and 228 on one stage, 792 and 232 on Ken's.)
        .cameraX = 0x02022264, .cameraY = 0x02022268, .cameraRight = 0x020222A8, .cameraLeft = 0x020222AC,
        // (3rd Strike's number: put there by "Start over" with the camera at its end, the players stand at the picture's
        // edge at the stage's end, and player 1 walking on towards it stays at that x)
        .wall = 164,
        // (3rd Strike's stage byte less 0x120E: 11 in a session of Ryu against Ken, on Ken's stage; written while no
        // round is on, the next fight is on that stage: all 15 pictured)
        .stage = 0x020142E7, .stageNames = kStageNames, .stageNumbers = kStageNumbers, .stages = 15,
        .characters = kCharacters, .characterCount = 15,
        .record = {
            .computer = 3,     // 1 for both players of a two-player fight; at 0 for player 2, it fights by itself (checked)
            .facing = 0x0A,    // 1 player 1 on the left, 0 player 2: the boxes drawn turned by it sit on both fighters
            .hitBy = 0x14,     // player 2's reads player 1's record's address from the first hit on
            .doing = 0x26, .which = 0x28, // 4 and 0 through a standing light punch, 0 again after; 1 and 25 while stunned
            .freeze = 0x45,    // counts down over the frames after a hit, the animation still meanwhile
            .x = 0x64, .y = 0x68, // traced: x changes walking, y rises and falls to 0 in a jump
            .animation = 0x1C0, .animationFrame = 0x1C4, // one address through a move; the frame steps as cg_ctr (0x1D4) runs out
            .cancelHit = 0x2BF, // 0 as a move starts, 1 from the frame it hits
            .hurtStop = 0x2EC, // 0xFFF1 (-15) as it parries
            .hitCount = 0x2FC, // one more with every hit taken (the combo readout's hits are counted by it)
            .character = 0x358, // 2 for Ryu, 11 for Ken
            .thrown = 0x367,   // not 0 through each of 22 throws
            .busy = 0x36A, .blocking = 0x36B, // blocking: 0 until the dummy guards, 33 guards seen by it in 33 moves guarded
            .attacking = 0x3C0, .inputTaken = 0x3C1, // attacking: 1 from a punch's first frame until its attack's frame is over
            .commands = 0x350, // 0x020218CC and 0x02021CD2 in dumps: wcp[2], where 14 routines load it for 3rd Strike's
            .gauge = 0x388,    // 0x0200ED1C and 0x0200ED48: super_arts[2], 44 bytes each
            .stun = 0x390,     // 0x0200ED74 and 0x0200ED88
            .attackBoxes = 0x288, .attackBoxCount = 2, // (two boxes a set: the pointer moves 16 bytes for each of the frame's index)
            .throwBox = 0x278, // (one box, 8 bytes for each of the frame's index)
            .hurtBoxes = 0x260,
        },
        // (the push box, the body's four, the hands' two, the attack's two, the throw's, the one it is thrown by: drawn,
        // they sit on the fighters, the attack's on the arm or leg out; a fireball's record has a body and an attack box)
        .boxes = {{0x294, 1, 2}, {0x260, 4, 0}, {0x268, 2, 0}, {0x288, 2, 1}, {0x278, 1, 3}, {0x280, 1, 4}},
        // (read in dumps: the fill 0x2A of a gauge 0x80 long, the stocks 0 of 1; set to the most, with the redraw byte, the
        // picture's gauge says MAX and the super art comes out, 10 times in a run. The kind is from the game's own routine
        // alone (gauge_type): no art with a timer was tried.)
        .gauge = {.fill = 0x18, .stocks = 0x22, .stocksMost = 0x20, .kind = 0x06},
        // (sa_gauge_flash, spgauge_cont_init's and spgauge_control's literals: without it player 2's gauge stayed drawn
        // empty with its stocks full)
        .gaugeRedraw = {0x020145E0, 0x020145E1},
        // (the readout's numbers: 0 of 64 at the start, up with each hit and draining after; 36 of 72 in a dump)
        .stun = {.most = 2, .timer = 4, .taken = 8},
        // (waza_flag[3], [4], [5] and [12] of the commands' record, as the hit judgement reads them (defense_ground,
        // defense_sky; [12] against a player in the air): the dummy tapping towards or down as an attack comes parries it,
        // 52 times in a run, and jump-ins by [12], 13 times. In the air its taps parried nothing (the window opened: two
        // read too early).)
        .parryWindow = {0x1D, 0x1F, 0x21, 0x2F},
        .parryStop = -15,      // (set_paring_status; seen as each parry lands)
        // (effect_work_list_init's and push_effect_work's literals; a fireball in flight is the third record of list 3,
        // 0x800 bytes a record, its byte 2 the player's number)
        .effectList = 0x02064316, .effects = 0x02024210, .effectSize = 0x800,
        .throwEscape = true,   // (the stick to a side and a heavy punch as the throw takes hold: 22 of 22 throws escaped, none without)
        .quickStand = true,    // (down tapped as it lands: up 16 frames sooner after a sweep, 50 for 66)
        .stunMash = true,      // (mashing, stuns of 90 to 98 frames last 42 to 70)
    };
    return facts;
}

} // namespace sf3
