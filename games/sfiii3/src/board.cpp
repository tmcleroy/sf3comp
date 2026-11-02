// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The board's game settings, as the test menu's "game settings" page sets them, from the menu and the launcher: the
// settings EEPROM's bytes written before the board starts (a reset takes a change). Found by writing the bytes and
// reading the test menu: 0x21 difficulty (0-7: 1-8), 0x22 damage (0-3: 1-4), 0x23 timer speed (0-3: 1-4), 0x24 the
// rounds (high half between two players, low half against the computer: 0 one round, 1 three, 2 five); a second copy
// of each 0x30 bytes on (0x51 on for these). The system settings page and the rest of the game settings page, found the same way (each
// changed in the test menu and saved, the EEPROM compared): 0x02 continue (1 yes, 0 no), 0x05 demo sound (1 on, 0
// off), 0x25 event (0 normal, 1 one match; the game then sets continue to no), 0x27 bonus games (1 on, 0 off). A blank EEPROM (the first start) gets the game's defaults at start-up, over these: they take
// from the next start, once the EEPROM is kept.
#include "game.h"
#include "system.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace cps3recomp {

namespace {
    void SetByte(Machine &m, uint32_t offset, uint8_t value) {
        for (const uint32_t base : {0x00u, 0x30u}) { // (the block and its copy)
            uint32_t &word = m.eeprom.words[(base + offset) / 4];
            const int shift = static_cast<int>((base + offset) % 4) * 8; // (the game's bytes, low first in each word kept)
            word = (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
        }
    }
    uint8_t Byte(const Machine &m, uint32_t offset) {
        return static_cast<uint8_t>(m.eeprom.words[offset / 4] >> (static_cast<int>(offset % 4) * 8));
    }
} // namespace

// Before the board's first frame (after switching on or a reset): the settings chosen, written in
void BoardBeforeFrame(Machine &m) {
    if (m.frame != 0) {
        return;
    }
    if (getenv("SFIII3_BOARD_LOG")) {
        fprintf(stderr, "board settings: difficulty %d rounds %d %d\n", FeatureValue("board_difficulty"), FeatureValue("board_rounds_cpu"),
                FeatureValue("board_rounds_vs"));
    }
    if (const int v = FeatureValue("board_difficulty")) {
        SetByte(m, 0x21, static_cast<uint8_t>(v - 1));
    }
    if (const int v = FeatureValue("board_damage")) {
        SetByte(m, 0x22, static_cast<uint8_t>(v - 1));
    }
    if (const int v = FeatureValue("board_timer")) {
        SetByte(m, 0x23, static_cast<uint8_t>(v - 1));
    }
    // (the switches: 1 the board's first choice, 2 its second)
    if (const int v = FeatureValue("board_event")) {
        SetByte(m, 0x25, static_cast<uint8_t>(v - 1));
    }
    if (const int v = FeatureValue("board_bonus")) {
        SetByte(m, 0x27, v == 1 ? 1 : 0);
    }
    if (const int v = FeatureValue("board_continue")) {
        SetByte(m, 0x02, v == 1 ? 1 : 0);
    }
    if (const int v = FeatureValue("board_demo_sound")) {
        SetByte(m, 0x05, v == 1 ? 1 : 0);
    }
    const int cpu = FeatureValue("board_rounds_cpu"), vs = FeatureValue("board_rounds_vs");
    if (cpu || vs) {
        const uint8_t rounds = Byte(m, 0x24);
        SetByte(m, 0x24, static_cast<uint8_t>(((vs ? vs - 1 : rounds >> 4) << 4) | (cpu ? cpu - 1 : rounds & 15)));
    }
}

void RegisterBoard(Game &game) {
    game.features.push_back({"board_difficulty", "Board", "Difficulty", "How hard the computer plays (the board's 1 to 8). Takes effect when the game is reset.",
                             0, {"As the board has it", "1 (easiest)", "2", "3", "4", "5", "6", "7", "8 (hardest)"}});
    game.features.push_back({"board_damage", "Board", "Damage", "How much a hit takes (the board's 1 to 4). Takes effect when the game is reset.",
                             0, {"As the board has it", "1 (least)", "2", "3", "4 (most)"}});
    game.features.push_back({"board_timer", "Board", "Timer speed", "How fast the round's clock runs (the board's 1 to 4). Takes effect when the game is reset.",
                             0, {"As the board has it", "1 (slowest)", "2", "3", "4 (fastest)"}});
    game.features.push_back({"board_rounds_cpu", "Board", "Rounds against the computer", "Rounds to win a match against the computer. Takes effect when the game is reset.",
                             0, {"As the board has it", "1", "2 (best of 3)", "3 (best of 5)"}});
    game.features.push_back({"board_rounds_vs", "Board", "Rounds between two players", "Rounds to win a match between two players. Takes effect when the game is reset.",
                             0, {"As the board has it", "1", "2 (best of 3)", "3 (best of 5)"}});
    game.features.push_back({"board_event", "Board", "Event", "Off, or one match only (and no continue). Takes effect when the game is reset.",
                             0, {"As the board has it", "Off", "One match"}});
    game.features.push_back({"board_bonus", "Board", "Bonus games", "Whether the one-player game has its bonus stages. Takes effect when the game is reset.",
                             0, {"As the board has it", "On", "Off"}});
    game.features.push_back({"board_continue", "Board", "Continue", "Whether a game over can be continued. Takes effect when the game is reset.",
                             0, {"As the board has it", "Yes", "No"}});
    game.features.push_back({"board_demo_sound", "Board", "Demo sound", "Whether the attract mode plays sound. Takes effect when the game is reset.",
                             0, {"As the board has it", "On", "Off"}});
}

} // namespace cps3recomp
