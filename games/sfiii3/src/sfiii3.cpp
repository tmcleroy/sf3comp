// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's own sources: its features (runtime/game.h).
#include "sfiii3.h"

#include "game.h"
#include "session.h"
#include "state.h"
#include "system.h"

namespace cps3recomp {

void RegisterTraining(Game &game);
void RegisterLifts3rdStrike();
void RegisterStages(Game &game);
void RegisterSoundProbe();
void RegisterBoard(Game &game);
void BoardBeforeFrame(Machine &m);
void StagesBeforeFrame(Machine &machine);
void WidescreenBeforeFrame(Machine &m, bool fighting);
void TrainingBeforeFrame(Machine &m);
void TrainingAfterFrame(Machine &m);

namespace {
    // The board's start-up (its checks, the region's warning, Capcom's logo) runs unseen when skip_intro is on: the
    // game shows from its opening, which starts at frame 800 (seen in pictures)
    constexpr uint64_t kOpening = 760;
    bool g_skipping = false;

    void BeforeFrame(Machine &m) {
        BoardBeforeFrame(m);
        if (FeatureOn("skip_intro") && m.frame < kOpening && !session::Booting()) {
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
        if (FeatureOn("free_play") && Ram8(sfiii3::kCredits) < 2) {
            SetRam8(sfiii3::kCredits, 2); // (two: a second player can always join)
        }
        // (only while the byte holds a running clock: what it holds outside a fight is not known yet)
        const uint8_t clock = Ram8(sfiii3::kClock);
        if (FeatureOn("infinite_time") && clock > 0 && clock < 99) {
            SetRam8(sfiii3::kClock, 99);
        }
        WidescreenBeforeFrame(m, (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2);
        StagesBeforeFrame(m);
        TrainingBeforeFrame(m);
    }
} // namespace

void RegisterGame() {
    Game &game = TheGame();
    game.features.push_back({"widescreen", "Game", "Widescreen",
                             "16:9: 64 more columns of the stage on each side. In a fight the camera stays 64 columns further from "
                             "the stage's ends, so the corners come that much sooner than on the board.",
                             0});
    game.features.push_back({"skip_intro", "Game", "Straight to the opening",
                             "The board's start-up (its memory checks, the region's warning and Capcom's logo) runs unseen, so the game "
                             "shows from its opening in about two seconds.",
                             kFeatureEnhanced});
    game.features.push_back({"free_play", "Game", "Free play", "Start works without a coin.", kFeatureEnhanced});
    game.features.push_back({"infinite_time", "Cheats", "Infinite time", "The round's clock stands still."});
    game.features.push_back({"no_damage", "Cheats", "No damage", "Player 1's health does not go down."});
    game.features.push_back({"no_damage_2", "Cheats", "No damage, player 2", "Player 2's health does not go down (the opponent's, in a one-player game)."});
    game.beforeFrame = BeforeFrame;
    game.frame = TrainingAfterFrame;
    RegisterTraining(game);
    RegisterLifts3rdStrike();
    RegisterStages(game);
    RegisterSoundProbe();
    RegisterBoard(game);
}

} // namespace cps3recomp
