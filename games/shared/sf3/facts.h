// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// What the sources the Street Fighter III games share (games/shared/sf3) need to know of each game: the three are
// one engine, with the same variables at other addresses. A game's own src/facts.cpp gives them, each with how it
// was found (cps3recomp/tools/crossmap.py carries an address over from 3rd Strike by the routines' names; every one
// is then checked in the game).
#pragma once

#include <cstdint>

namespace sf3 {

struct Facts {
    // The board's start-up (its memory checks, the region's warning, Capcom's logo) is over and the opening about
    // to begin at this frame (seen in pictures taken every 100 frames)
    uint64_t opening;
    uint32_t credits;      // byte: the credits (Ck_Coin, credit_use)
    uint32_t clock;        // byte: the round's clock, 99 down
    // A round is on: a character locked in by either side (0xFF) and the match's state 2 (players can move)
    uint32_t locked[2];
    uint32_t matchState;
    uint32_t player[2];    // the players' records (plw[0], plw[1])
    uint32_t life;         // in a record, byte: the health (the low byte of vital_new)
    int fullLife;
    // The board's game settings in the settings EEPROM (byte offsets, as 3rd Strike's board.cpp has them; 0: the game
    // has no such setting): each has a second copy 0x30 bytes on
    bool bonusGames;       // the game has bonus stages, and the setting for them (0x27)
    bool roundsBetweenTwo; // the rounds byte's high half is the rounds between two players (else one number for both)
};
const Facts &TheFacts(); // (the game's src/facts.cpp)

} // namespace sf3
