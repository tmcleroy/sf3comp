// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Attract mode (the Alternative modes page, as the CPS-2 ports have it): the game's own show on a loop, as the board
// runs it with nobody playing, from a reset. "Shows" picks what of it is seen; the rest runs unseen, at speed.
//
// The show's parts, told by the song the sound driver plays (music.cpp; seen over a long run of the board's attract
// mode, which loops every 7,600 frames or so): the opening (song 48: Capcom's logo and the opening), the drawings and
// the title (1), character select (2), the versus screen (3), a demonstration fight on a stage (its stage's song,
// 10-41), "here comes a new challenger" (7), a second fight, and then the opening again. The intro is the opening and
// the title: once character select comes the board is reset, to the opening again. The fights are the stage songs.
#include "game.h"
#include "sfiii3.h"
#include "system.h"

#include <cstdint>

namespace cps3recomp {

int SongNow();     // music.cpp
void EndBattle();  // battle.cpp

namespace {
    enum Show { kEverything, kIntro, kFights };
    constexpr int kOpening = 48, kTitle = 1, kFirstStageSong = 10, kLastStageSong = 41;

    struct {
        bool on = false;
        bool fast = false;
        bool seenIntro = false; // (the intro only) the opening reached since the last reset
        uint16_t was[2] = {};   // the controls at the last frame (Start or a coin ends it)
    } g;

    void Fast(bool on) {
        if (on != g.fast) {
            g.fast = on;
            SetFastForward(on);
        }
    }
    void StartAttract(Machine &machine) {
        EndBattle();
        SetFeature("training_mode", false);
        g = {};
        g.on = true;
        machine.Reset();
    }
} // namespace

bool AttractOn() {
    return g.on;
}

void AttractBeforeFrame(Machine &m) {
    if (!g.on) {
        Fast(false);
        return;
    }
    // (Start or a coin, newly pressed: attract mode is over, and the press the game's, to begin a game with)
    for (int p = 0; p < 2; p++) {
        const uint16_t now = m.players[p] & (kStart | kCoin);
        if (now & ~g.was[p]) {
            g = {};
            Fast(false);
            return;
        }
        g.was[p] = now;
    }
    const int song = SongNow();
    switch (FeatureValue("attract_show")) {
    case kIntro:
        if (song == kOpening) {
            g.seenIntro = true;
        }
        if (g.seenIntro && song != kOpening && song != kTitle && song != -1) {
            g.seenIntro = false;
            m.Reset(); // (past the title: the opening again)
        }
        Fast(!g.seenIntro);
        break;
    case kFights:
        Fast(!(song >= kFirstStageSong && song <= kLastStageSong));
        break;
    default:
        Fast(false);
        break;
    }
}

void RegisterAttract(Game &game) {
    game.commands.push_back({"attract_start", "Attract mode", "Start",
                             "Starts attract mode: the game's own show, over and over. Start (or a coin) ends it.", StartAttract});
    game.features.push_back({"attract_show", "Attract mode", "Shows",
                             "What attract mode plays: everything the board does, the introduction over and over, or one "
                             "demonstration fight after another.",
                             0, {"Everything", "The intro only", "The fights only"}});
}

} // namespace cps3recomp
