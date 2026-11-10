// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's 990608 revision's facts for the sources the Street Fighter III games share (games/shared/sf3/facts.h).
// The set is sfiii3n (Japan 990608, NO CD).
//
// The program is the 990512 one (sfiii3nr1, games/sfiii3) built again: every routine is at another address, and the
// memory is laid out the same. Of the 1,533 places in memory that routines of 990512 and the same routines here
// both name, 1,527 are the same address in both (the two programs compared routine by routine, addresses left out).
// So each fact here is 3rd Strike's own (games/sfiii3/src/sfiii3.h, and training.cpp's), which were found and
// checked there; the three pointers in a player's record that sfiii3.h does not name (the commands', the gauge's and
// the stun's records) were read in a dump of a fight: each holds the address 3rd Strike's sources use for that record.
#include "facts.h"

namespace sf3 {

namespace {
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                       "Ken", "Sean", "Urien", "Akuma", "Akuma (red)", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
    const char *const kStageNames[] = {"Gill's", "Alex's", "Ryu's", "Yun's", "Dudley's", "Necro's", "Hugo's", "Ibuki's", "Elena's", "Oro's", "Yang's",
                                       "Ken's", "Sean's", "Urien's", "Akuma's", "Akuma's (red)", "Chun-Li's", "Makoto's", "Q's", "Twelve's", "Remy's"};
    const uint8_t kStageNumbers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
} // namespace

const Facts &TheFacts() {
    static const Facts facts = {
        .opening = 380,        // (Capcom's logo is up by frame 400)
        .credits = 0x02007CE0,
        .clock = 0x02011377,
        .clockFrames = 0x02011378, // (the word after the clock's, as the two older games')
        .locked = {0x020154C6, 0x020154C8},
        .matchState = 0x020154A7,
        .player = {0x02068C6C, 0x02069104},
        .life = 0x9F,
        .fullLife = 160,
        .bonusGames = true,
        .roundsBetweenTwo = true,
        .selectState = {0x0201553D, 0x02015545},
        .cameraX = 0x02026CB0, .cameraY = 0x02026CB4, .cameraRight = 0x02026CF4, .cameraLeft = 0x02026CF8,
        .wall = 164,
        .stage = 0x020154F5, .stageNames = kStageNames, .stageNumbers = kStageNumbers, .stages = 21,
        .characters = kCharacters, .characterCount = 21,
        .record = {
            .computer = 3,
            .facing = 0x0A,
            .hitBy = 0x14,
            .doing = 0x26, .which = 0x28,
            .freeze = 0x45,
            .x = 0x64, .y = 0x68,
            .animation = 0x200, .animationFrame = 0x204,
            .cancelHit = 0x2FF,
            .hurtStop = 0x32E,
            .hitCount = 0x33E,
            .character = 0x3C0,
            .thrown = 0x3CF,
            .busy = 0x3D1, .blocking = 0x3D3,
            .attacking = 0x428, .inputTaken = 0x46D, // (the low byte of 3rd Strike's word at 0x46C)
            // (read in a fight's dump: 0x02026318 and 0x0202671E, 0x0206959C and 0x020695C8, 0x020695F4 and 0x02069608)
            .commands = 0x3B8,
            .gauge = 0x3F0,
            .stun = 0x3F8,
            .attackBoxes = 0x2C8, .attackBoxCount = 4,
            .throwBox = 0x2B8,
            .hurtBoxes = 0x2A0,
        },
        .boxes = {{0x2D4, 1, 2}, {0x2A0, 4, 0}, {0x2A8, 4, 0}, {0x2C8, 4, 1}, {0x2B8, 1, 3}, {0x2C0, 1, 4}},
        // (the super art's record as 2nd Impact's: 3rd Strike's gauge, stocks and most are those bytes of it)
        .gauge = {.fill = 0x18, .stocks = 0x22, .stocksMost = 0x20, .kind = 0x06},
        .gaugeRedraw = {0x020157C8, 0x020157C9},
        .stun = {.most = 2, .timer = 4, .taken = 8},
        .parryWindow = {0x1D, 0x1F, 0x21, 0x2F},
        .parryStop = -15,
        .effectList = 0x02068A96, .effects = 0x02028990, .effectSize = 0x800,
        .throwEscape = true,
        .quickStand = true,
        .stunMash = true,
    };
    return facts;
}

} // namespace sf3
