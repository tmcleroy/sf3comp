// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Survival: a one-player game against the computer in which player 1's health is not given back, from round to round
// and fight to fight; how many rounds are won before it runs out is the score, the best kept (settings.ini's
// survival_best). Each round starts with what the last one left, and a tenth back for each round won (written into
// the record as the round begins: the game has put it back to full by then); the computer's opponents start each
// round full, as always.
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace cps3recomp {

namespace {
    struct State {
        bool playing = false;   // a survival run is on (a one-player game, the feature on, since its character was chosen)
        bool fighting = false;  // a round was being fought at the last frame
        uint8_t health = 0;     // player 1's health to carry into the next round
        uint8_t opponent = 0;   // the computer's at the last frame
        uint8_t own = 0;        // ... and player 1's
        uint16_t won = 0;       // rounds won in this run
    } s;

    bool InMatch() {
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2;
    }
    uint32_t Life(int p) {
        return sfiii3::kPlayer[p] + sfiii3::kLife;
    }
} // namespace

void SurvivalBeforeFrame(Machine &) {
    if (!FeatureOn("survival") || session::InSession()) {
        s = {};
        return;
    }
    // (a new one-player game: player 1 choosing a character, nobody on player 2's side)
    const uint8_t one = Ram8(sfiii3::kSelectState[0]), two = Ram8(sfiii3::kSelectState[1]);
    if (one == 2 && two == 0) {
        if (!s.playing || s.won) {
            s = {};
        }
        s.playing = true;
        s.health = sfiii3::kFullLife;
        return;
    }
    if (!s.playing || two != 0) {
        s.playing = s.playing && two == 0; // (a second player came in: not survival any more)
        return;
    }
    const bool fighting = InMatch();
    if (fighting && !s.fighting && s.health > 0 && s.health < sfiii3::kFullLife) {
        SetRam8(Life(0), s.health); // (the round begins: with what the last one left)
    }
    // (the knock-out comes as the round's fighting ends: watched while a match is on)
    const bool match = Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    if (match) {
        // (health 0, or 255 once the knock-out has gone through: out)
        const uint8_t mine = Ram8(Life(0)), theirs = Ram8(Life(1));
        const bool mineOut = mine == 0 || mine > sfiii3::kFullLife, theirsOut = theirs == 0 || theirs > sfiii3::kFullLife;
        if (fighting && !mineOut && !theirsOut) {
            s.health = mine; // (until the round is won: then a tenth more, above)
        }
        if (theirsOut && s.opponent > 0 && s.opponent <= sfiii3::kFullLife && !mineOut) {
            s.won++;
            if (getenv("SFIII3_SURVIVAL_LOG")) {
                fprintf(stderr, "survival: round won (%u), health %u\n", s.won, s.health);
            }
            s.health = static_cast<uint8_t>(std::min<int>(sfiii3::kFullLife, s.health + sfiii3::kFullLife / 10)); // (a little back)
            if (s.won > FeatureValue("survival_best") && !RunningAhead()) {
                SetFeatureValue("survival_best", std::min<int>(s.won, 99));
            }
        }
        if (mineOut && s.own > 0 && s.own <= sfiii3::kFullLife) {
            s.playing = false; // (knocked out: the run is over; a continue is a new run)
        }
        s.opponent = theirs;
        s.own = mine;
    }
    s.fighting = fighting;
}

// Its line under the round's clock while a run is on
void SurvivalHud(std::vector<HudLine> &lines) {
    if (!s.playing || !FeatureOn("survival")) {
        return;
    }
    char text[64];
    snprintf(text, sizeof(text), "SURVIVAL  ROUNDS WON %u  BEST %d", s.won, std::max<int>(s.won, FeatureValue("survival_best")));
    lines.push_back({0.5f, 0.235f, 0, text, 255, 210, 80});
}

void SurvivalState(Archive &ar) {
    ar.Mark(0x53555256); // SURV
    ar.Pod(s);
}

void RegisterSurvival(Game &game) {
    game.features.push_back({"survival", "Survival", "Survival",
                             "A one-player game in which your health is not given back between rounds and fights: how many rounds "
                             "you win before it runs out is the score (the best is kept). The computer's fighters start each round "
                             "full, as always. A tenth of your health comes back for each round won."});
    // (kept with the features so that it is remembered; the numbers are its choices)
    static std::vector<std::string> numbers;
    if (numbers.empty()) {
        numbers.reserve(100);
        for (int n = 0; n < 100; n++) {
            numbers.push_back(std::to_string(n));
        }
    }
    std::vector<const char *> choices;
    for (const std::string &number : numbers) {
        choices.push_back(number.c_str());
    }
    game.features.push_back({"survival_best", "Survival", "Best", "The most rounds won in one survival run. (Left puts it back.)",
                             kFeatureOwn, choices});
}

} // namespace cps3recomp
