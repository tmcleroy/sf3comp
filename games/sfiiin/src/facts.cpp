// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// New Generation's facts for the sources the Street Fighter III games share (games/shared/sf3/facts.h). The set is
// sfiiin (Asia 970204, NO CD). Each address was carried over from 3rd Strike's (games/sfiii3/src/sfiii3.h) with
// cps3recomp/tools/crossmap.py and then checked in RAM dumps of a run of made-up play (--auto 7): at the title
// (frame 700), at character select (1500) and in a fight (2250, 3000, 3750).
#include "facts.h"

namespace sf3 {

const Facts &TheFacts() {
    static const Facts facts = {
        .opening = 780,        // (black at frame 800, the opening's first picture by 900)
        .credits = 0x02007188, // Ck_Coin, service_coin_check, credit_use; counts the run's coins
        .clock = 0x0200EB33,   // 94, 80, 66 at the three fight frames, as the pictures show
        // (3rd Strike's less 0x2852, as their neighbours: 00/00 at the title, FF/80 at select with one player in,
        // FF/FF in the fight; the state 0, 1, 2)
        .locked = {0x02012C74, 0x02012C76},
        .matchState = 0x02012C55,
        .player = {0x0200D18C, 0x0200D564}, // 38 routines of the same name load the first where 3rd Strike's load its
        .life = 0x9F,          // 160 at the round's start, going down with the bar in the pictures (both players)
        .fullLife = 160,
        .bonusGames = false,   // (the game has none: the settings' byte 0x27 is 0)
        .roundsBetweenTwo = false,
    };
    return facts;
}

} // namespace sf3
