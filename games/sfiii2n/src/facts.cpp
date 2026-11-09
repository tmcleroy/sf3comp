// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 2nd Impact's facts for the sources the Street Fighter III games share (games/shared/sf3/facts.h). The set is
// sfiii2n (Asia 970930, NO CD). Each address was carried over from 3rd Strike's (games/sfiii3/src/sfiii3.h) with
// cps3recomp/tools/crossmap.py and then checked in RAM dumps of a run of made-up play (--auto 7): at the title
// (frame 700), at character select (1500) and in a fight (2250, 3000, 3750).
#include "facts.h"

namespace sf3 {

const Facts &TheFacts() {
    static const Facts facts = {
        .opening = 780,        // (black at frame 800, the opening's first picture by 900)
        .credits = 0x02008528, // credit_display_render, Ck_Coin, service_coin_check, credit_use; counts the run's coins
        .clock = 0x02010167,   // 92, 78, 68 at the three fight frames, as the pictures show
        // (3rd Strike's less 0x120E, as their neighbours: 00/00 at the title, 80/80 at select, FF/FF in the fight;
        // the state 0, 1, 2, and 6 once the round is over)
        .locked = {0x020142B8, 0x020142BA},
        .matchState = 0x02014299,
        .player = {0x0200E504, 0x0200E910}, // 51 routines of the same name load the first where 3rd Strike's load its
        .life = 0x9F,          // 160 at the round's start, going down with the bar in the pictures (player 2's seen)
        .fullLife = 160,
        .bonusGames = true,
        .roundsBetweenTwo = false,
    };
    return facts;
}

} // namespace sf3
