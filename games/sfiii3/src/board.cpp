// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The board's game settings, as the test menu's "game settings" page sets them, from the menu and the launcher: the
// settings EEPROM's bytes written before the board starts (a reset takes a change). Found by writing the bytes and
// reading the test menu: 0x21 difficulty (0-7: 1-8), 0x22 damage (0-3: 1-4), 0x23 timer speed (0-3: 1-4), 0x24 the
// rounds (high half between two players, low half against the computer: 0 one round, 1 three, 2 five); a second copy
// of the block at 0x50. A blank EEPROM (the first start) gets the game's defaults at start-up, over these: they take
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
            const int shift = (3 - static_cast<int>((base + offset) % 4)) * 8;
            word = (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
        }
    }
    uint8_t Byte(const Machine &m, uint32_t offset) {
        return static_cast<uint8_t>(m.eeprom.words[offset / 4] >> ((3 - static_cast<int>(offset % 4)) * 8));
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
}

} // namespace cps3recomp
