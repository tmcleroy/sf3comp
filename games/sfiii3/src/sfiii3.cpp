// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's own sources: its features (runtime/game.h).
#include "sfiii3.h"

#include "game.h"

namespace cps3recomp {

void RegisterTraining(Game &game);
void TrainingBeforeFrame(Machine &m);
void TrainingAfterFrame(Machine &m);

namespace {
    void BeforeFrame(Machine &m) {
        if (FeatureOn("free_play") && Ram8(sfiii3::kCredits) < 2) {
            SetRam8(sfiii3::kCredits, 2); // (two: a second player can always join)
        }
        // (only while the byte holds a running clock: what it holds outside a fight is not known yet)
        const uint8_t clock = Ram8(sfiii3::kClock);
        if (FeatureOn("infinite_time") && clock > 0 && clock < 99) {
            SetRam8(sfiii3::kClock, 99);
        }
        TrainingBeforeFrame(m);
    }
} // namespace

void RegisterGame() {
    Game &game = TheGame();
    game.features.push_back({"free_play", "Game", "Free play", "Start works without a coin.", kFeatureEnhanced});
    game.features.push_back({"infinite_time", "Cheats", "Infinite time", "The round's clock stands still."});
    game.beforeFrame = BeforeFrame;
    game.frame = TrainingAfterFrame;
    RegisterTraining(game);
}

} // namespace cps3recomp
