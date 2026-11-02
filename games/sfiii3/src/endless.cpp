// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Endless battle: the game plays itself, match after match, a new pair of fighters each time. A two-player game is
// started as a training session's is (the board reset, the way to character select worked unseen), but as the game's
// own versus mode: each player's character is picked at random (the chosen character's byte, as Play as Gill writes
// it) and both are played by the computer (the player records' byte 3), the rounds counted and the matches ended as
// the game does, and both players back in for the next. The stage is the game's choice, or the Stages option's
// (the opponent's: New Generation's or 2nd Impact's).
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <cstdio>
#include <vector>

namespace cps3recomp {

namespace {
    struct State {
        uint32_t random = 0x2545F491u;
        uint32_t frames = 0;
        uint32_t matches = 0;
        bool picked[2] = {};
        uint8_t choice[2] = {};
        bool fighting = false;
        uint8_t lastState = 0;
    } e;

    uint32_t Next() {
        e.random ^= e.random << 13;
        e.random ^= e.random >> 17;
        e.random ^= e.random << 5;
        return e.random;
    }
    // A match on (its state 1-8: a round's start to its end; the characters' locked bytes stay set between matches)
    bool Locked() {
        const uint8_t state = Ram8(sfiii3::kMatchState);
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && state >= 1 && state <= 8;
    }

    void Start(Machine &machine) {
        session::StartSession(machine); // (the way to character select for two players)
        SetFeature("training_mode", false); // (but the game's own versus, not a session)
        SetFeature("endless", true);
        e = {};
        e.random ^= static_cast<uint32_t>(machine.frame * 2654435761u) | 1u;
    }
} // namespace

void EndlessBeforeFrame(Machine &m) {
    if (!FeatureOn("endless") || session::Booting()) {
        return;
    }
    e.frames++;
    SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 2));
    const bool press = e.frames % 20 < 4;
    uint16_t held[2] = {};
    const bool locked = Locked();
    for (int p = 0; p < 2; p++) {
        const uint8_t state = Ram8(sfiii3::kSelectState[p]);
        if (state == 0) {
            held[p] = press ? kStart : 0; // (back in, as a challenger: the next match is the two again)
            e.picked[p] = false;
        } else if (!locked) {
            // (a character at random, Gill too, written every frame from the choice until the match is on, as Play as
            // Gill does; a light punch takes the character, its super art and its colour)
            if (state >= 2) {
                if (!e.picked[p]) {
                    e.picked[p] = true;
                    e.choice[p] = static_cast<uint8_t>(Next() % 21);
                }
                SetRam8(sfiii3::kChosen[p], e.choice[p]);
                held[p] = state < 5 && press ? kPunch1 : 0;
            }
        } else {
            e.picked[p] = false;
            SetRam8(sfiii3::kPlayer[p] + 3, 0); // (the computer plays it)
        }
    }
    // (a match over: the match's state 9)
    const uint8_t matchState = Ram8(sfiii3::kMatchState);
    if (matchState == 9 && e.lastState != 9) {
        e.matches++;
    }
    e.lastState = matchState;
    e.fighting = locked;
    // (between matches, the game's screens go on with the buttons pressed now and then)
    if (!locked) {
        m.players[0] = held[0] ? held[0] : (press ? kPunch1 : 0);
        m.players[1] = held[1] ? held[1] : (press ? kPunch1 : 0);
    } else {
        m.players[0] = m.players[1] = 0;
    }
}

void EndlessHud(std::vector<HudLine> &lines) {
    if (!FeatureOn("endless")) {
        return;
    }
    char text[48];
    snprintf(text, sizeof(text), "ENDLESS BATTLE  MATCH %u", e.matches + 1);
    lines.push_back({0.5f, 0.235f, 0, text, 160, 220, 255});
}

void EndlessState(Archive &ar) {
    ar.Mark(0x454E444C); // ENDL
    ar.Pod(e);
}

void RegisterEndless(Game &game) {
    game.features.push_back({"endless", "Endless battle", "Endless battle",
                             "The game plays itself, match after match, a new pair of fighters each time (Gill too). \"Start\" above "
                             "begins it; resetting the game ends it.",
                             kFeatureSession});
    game.commands.push_back({"endless_start", "Endless battle", "Start",
                             "Starts an endless battle: the game starts over and the computer fights itself, a new pair each match, "
                             "on the game's stage or the Stages option's.",
                             Start});
}

} // namespace cps3recomp
