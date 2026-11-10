// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// What every Street Fighter III game's port has, from the game's facts (facts.h): free play, straight to the opening,
// the cheats that are a byte kept (the clock, health), the Board page, and training mode (training.cpp). New Generation
// and 2nd Impact are built from this; 3rd Strike has the same in its own sources (games/sfiii3/src/sfiii3.cpp,
// board.cpp, training.cpp), from before the others were games of their own.
#include "facts.h"
#include "training.h"

#include "game.h"
#include "session.h"
#include "state.h"
#include "system.h"

#include <cstdint>

namespace sf3 {
bool InRound() {
    const Facts &f = TheFacts();
    return (cps3recomp::Ram8(f.locked[0]) == 0xFF || cps3recomp::Ram8(f.locked[1]) == 0xFF) && cps3recomp::Ram8(f.matchState) == 2;
}
} // namespace sf3

namespace cps3recomp {

namespace {
    using sf3::InRound;
    using sf3::TheFacts;

    bool g_skipping = false; // (follows the frame number alone: nothing a save state need hold)

    // The settings EEPROM's bytes (the game's bytes are kept high first in each word; a block and its copy 0x30 on)
    void SetByte(Machine &m, uint32_t offset, uint8_t value) {
        for (const uint32_t base : {0x00u, 0x30u}) {
            uint32_t &word = m.eeprom.words[(base + offset) / 4];
            const int shift = (3 - static_cast<int>((base + offset) % 4)) * 8;
            word = (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
        }
    }
    uint8_t Byte(const Machine &m, uint32_t offset) {
        return static_cast<uint8_t>(m.eeprom.words[offset / 4] >> ((3 - static_cast<int>(offset % 4)) * 8));
    }

    // Before the board's first frame (after switching on or a reset): the settings chosen, written in
    void Board(Machine &m) {
        if (m.frame != 0) {
            return;
        }
        const sf3::Facts &f = TheFacts();
        if (const int v = FeatureValue("board_difficulty")) {
            SetByte(m, 0x21, static_cast<uint8_t>(v - 1));
        }
        if (const int v = FeatureValue("board_damage")) {
            SetByte(m, 0x22, static_cast<uint8_t>(v - 1));
        }
        if (const int v = FeatureValue("board_timer")) {
            SetByte(m, 0x23, static_cast<uint8_t>(v - 1));
        }
        const uint8_t rounds = Byte(m, 0x24);
        if (f.roundsBetweenTwo) {
            const int cpu = FeatureValue("board_rounds_cpu"), vs = FeatureValue("board_rounds_vs");
            if (cpu || vs) {
                SetByte(m, 0x24, static_cast<uint8_t>(((vs ? vs - 1 : rounds >> 4) << 4) | (cpu ? cpu - 1 : rounds & 15)));
            }
        } else if (const int v = FeatureValue("board_rounds")) {
            SetByte(m, 0x24, static_cast<uint8_t>((rounds & 0xF0) | (v - 1)));
        }
        if (const int v = FeatureValue("board_event")) {
            SetByte(m, 0x25, static_cast<uint8_t>(v - 1));
        }
        if (f.bonusGames) {
            if (const int v = FeatureValue("board_bonus")) {
                SetByte(m, 0x27, v == 1 ? 1 : 0);
            }
        }
        if (const int v = FeatureValue("board_continue")) {
            SetByte(m, 0x02, v == 1 ? 1 : 0);
        }
        if (const int v = FeatureValue("board_demo_sound")) {
            SetByte(m, 0x05, v == 1 ? 1 : 0);
        }
    }

    void BeforeFrame(Machine &m) {
        const sf3::Facts &f = TheFacts();
        Board(m);
        // (a training session's start runs unseen too, to the character choice: the fast forward is then its own)
        if (FeatureOn("skip_intro") && m.frame < f.opening && !session::Booting()) {
            if (!g_skipping) {
                g_skipping = true;
                SetFastForward(true);
            }
        } else if (g_skipping) {
            g_skipping = false;
            if (!session::Booting()) {
                SetFastForward(false);
            }
        }
        if (FeatureOn("free_play") && Ram8(f.credits) < 2) {
            SetRam8(f.credits, 2); // (two: a second player can always join)
        }
        if (InRound()) {
            const uint8_t clock = Ram8(f.clock);
            if (FeatureOn("infinite_time") && clock > 0 && clock < 99) {
                SetRam8(f.clock, 99);
            }
            for (int p = 0; p < 2; p++) {
                const uint32_t life = f.player[p] + f.life;
                // (one hit: health below full in a round is none)
                if (FeatureOn("one_hit") && Ram8(life) < f.fullLife && Ram8(life) > 0) {
                    SetRam8(life, 0);
                }
                if (FeatureOn(p == 0 ? "no_damage" : "no_damage_2")) {
                    SetRam8(life, static_cast<uint8_t>(f.fullLife));
                }
            }
        }
        TrainingBeforeFrame(m);
    }
} // namespace

void RegisterGame() {
    const sf3::Facts &f = TheFacts();
    Game &game = TheGame();
    game.features.push_back({"free_play", "Game", "Free play", "Start works without a coin.", kFeatureEnhanced});
    game.features.push_back({"skip_intro", "Game", "Straight to the opening",
                             "The board's start-up (its memory checks, the region's warning and Capcom's logo) runs unseen, so the game "
                             "shows from its opening in about two seconds.",
                             kFeatureEnhanced});
    game.features.push_back({"no_damage", "Cheats", "No damage", "Player 1's health does not go down."});
    game.features.push_back({"no_damage_2", "Cheats", "No damage, player 2", "Player 2's health does not go down (the opponent's, in a one-player game)."});
    game.features.push_back({"one_hit", "Cheats", "One-hit knockouts", "Any hit that does damage takes all the health that is left (both players)."});
    game.features.push_back({"infinite_time", "Cheats", "Infinite time", "The round's clock stands still."});

    game.features.push_back({"board_difficulty", "Board", "Difficulty", "How hard the computer plays (the board's 1 to 8). Takes effect when the game is reset.",
                             0, {"As the board has it", "1 (easiest)", "2", "3", "4", "5", "6", "7", "8 (hardest)"}});
    game.features.push_back({"board_damage", "Board", "Damage level", "How much a hit takes (the board's 1 to 4). Takes effect when the game is reset.",
                             0, {"As the board has it", "1 (least)", "2", "3", "4 (most)"}});
    game.features.push_back({"board_timer", "Board", "Timer speed", "How fast the round's clock runs (the board's 1 to 4). Takes effect when the game is reset.",
                             0, {"As the board has it", "1 (slowest)", "2", "3", "4 (fastest)"}});
    if (f.roundsBetweenTwo) {
        game.features.push_back({"board_rounds_cpu", "Board", "Rounds against the computer", "Rounds to win a match against the computer. Takes effect when the game is reset.",
                                 0, {"As the board has it", "1", "2 (best of 3)", "3 (best of 5)"}});
        game.features.push_back({"board_rounds_vs", "Board", "Rounds between two players", "Rounds to win a match between two players. Takes effect when the game is reset.",
                                 0, {"As the board has it", "1", "2 (best of 3)", "3 (best of 5)"}});
    } else {
        game.features.push_back({"board_rounds", "Board", "Rounds", "Rounds to win a match. Takes effect when the game is reset.",
                                 0, {"As the board has it", "1", "2 (best of 3)", "3 (best of 5)"}});
    }
    game.features.push_back({"board_event", "Board", "Event", "Off, or one match only. Takes effect when the game is reset.",
                             0, {"As the board has it", "Off", "One match"}});
    if (f.bonusGames) {
        game.features.push_back({"board_bonus", "Board", "Bonus games", "Whether the one-player game has its bonus stages. Takes effect when the game is reset.",
                                 0, {"As the board has it", "On", "Off"}});
    }
    game.features.push_back({"board_continue", "Board", "Continue", "Whether a game over can be continued. Takes effect when the game is reset.",
                             0, {"As the board has it", "Yes", "No"}});
    game.features.push_back({"board_demo_sound", "Board", "Demo sound", "Whether the attract mode plays sound. Takes effect when the game is reset.",
                             0, {"As the board has it", "On", "Off"}});
    game.beforeFrame = BeforeFrame;
    game.frame = TrainingAfterFrame;
    game.state = TrainingState;
    RegisterTraining(game);
}

} // namespace cps3recomp
