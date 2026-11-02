// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Endless battle: the game plays itself, match after match, a new pair of fighters each time. A two-player game is
// started as a training session's is (the board reset, the way to character select worked unseen), but as the game's
// own versus mode: each player's character is picked at random (the chosen character's byte, as Play as Gill writes
// it) and both are played by the computer (the player records' byte 3), the rounds counted and the matches ended as
// the game does, and both players back in for the next. The stage is the game's choice, or the Stages option's
// (the opponent's: New Generation's or 2nd Impact's).
#include "fight.h"
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <cstdio>
#include <string>
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
        uint32_t touched[2] = {}; // (when a person last worked each side's controls: it is theirs for ten seconds after)
        uint8_t stage = 0;        // (shuffled: this match's)
        bool stagePicked = false;
    } e;
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                       "Ken", "Sean", "Urien", "Akuma", "Akuma (red)", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
    constexpr uint32_t kTakenFor = 600;
    bool Person(int p) {
        return e.touched[p] && e.frames - e.touched[p] < kTakenFor;
    }

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
    if (FeatureOn("endless") && session::InSession()) {
        SetFeature("endless", false); // (a training session started: the endless battle is over)
    }
    if (!FeatureOn("endless") || session::Booting()) {
        m.hidePanel = false;
        return;
    }
    e.frames++;
    m.hidePanel = true; // (no bars, no clock: the text layer, where the game draws its panel, left out)
    if (Ram8(sfiii3::kClock) > 0 && Ram8(sfiii3::kClock) < 99) {
        SetRam8(sfiii3::kClock, 99); // (no clock: a round ends only with a knock-out)
    }
    SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 2));
    const bool press = e.frames % 20 < 4;
    uint16_t held[2] = {};
    const bool locked = Locked();
    // (a person's controls: that side is theirs, while they play and ten seconds after)
    for (int p = 0; p < 2; p++) {
        if (locked && (m.players[p] & (fight::kSticks | fight::kButtons))) {
            e.touched[p] = e.frames;
        }
    }
    // (the stage: the game's, one chosen (its owner's number), or shuffled: a new one each match)
    const int stageChoice = FeatureValue("endless_stage");
    if (!locked) {
        if (stageChoice == 1 && !e.stagePicked) {
            e.stagePicked = true;
            e.stage = static_cast<uint8_t>(Next() % 21);
        }
        if (stageChoice >= 1) {
            SetRam8(sfiii3::kStage, stageChoice == 1 ? e.stage : static_cast<uint8_t>(stageChoice - 2));
        }
    } else {
        e.stagePicked = false;
    }
    for (int p = 0; p < 2; p++) {
        const uint8_t state = Ram8(sfiii3::kSelectState[p]);
        if (state == 0) {
            held[p] = press ? kStart : 0; // (back in, as a challenger: the next match is the two again)
            e.picked[p] = false;
        } else if (!locked) {
            // (a character at random, Gill too, or the one chosen, written every frame from the choice until the match is
            // on, as Play as Gill does; a light punch takes the character, its super art and its colour)
            if (state >= 2) {
                if (!e.picked[p]) {
                    e.picked[p] = true;
                    const int fixed = FeatureValue(p == 0 ? "endless_one" : "endless_two");
                    e.choice[p] = static_cast<uint8_t>(fixed > 0 ? fixed - 1 : Next() % 21);
                }
                SetRam8(sfiii3::kChosen[p], e.choice[p]);
                held[p] = state < 5 && press ? kPunch1 : 0;
            }
        } else {
            e.picked[p] = false;
            SetRam8(sfiii3::kPlayer[p] + 3, Person(p) ? 1 : 0); // (the computer plays it, unless a person has taken it)
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
        for (int p = 0; p < 2; p++) {
            if (!Person(p)) {
                m.players[p] = 0;
            }
        }
    }
}

void EndlessHud(std::vector<HudLine> &) {
    // (no readout: an endless battle is to watch, with nothing over it; as cps2recomp's)
}

void EndlessState(Archive &ar) {
    ar.Mark(0x454E444C); // ENDL
    ar.Pod(e);
}

void RegisterEndless(Game &game) {
    static const char *const kPage = "Endless battle";
    game.features.push_back({"endless", kPage, "Endless battle",
                             "The game plays itself, match after match. \"Start\" above begins it; resetting the game ends it.",
                             kFeatureSession});
    game.commands.push_back({"endless_start", kPage, "Start",
                             "Starts an endless battle: the game starts over and the computer fights itself, match after match. Touch "
                             "a side's controls to take it over (the computer has it back ten seconds after you let go).",
                             Start});
    std::vector<const char *> fighters = {"Random"};
    for (const char *name : kCharacters) {
        fighters.push_back(name);
    }
    game.features.push_back({"endless_one", kPage, "Fighter 1", "Who is on player 1's side: someone new each match, or always the same.",
                             kFeatureMenuChoices, fighters});
    game.features.push_back({"endless_two", kPage, "Fighter 2", "Who is on player 2's side: someone new each match, or always the same.",
                             kFeatureMenuChoices, fighters});
    std::vector<const char *> stages = {"The game's choice", "Shuffle"};
    static std::vector<std::string> names;
    if (names.empty()) {
        for (const char *name : kCharacters) {
            names.push_back(std::string(name) + "'s");
        }
    }
    for (const std::string &name : names) {
        stages.push_back(name.c_str());
    }
    game.features.push_back({"endless_stage", kPage, "Stage",
                             "Where they fight: the game's choice, a new stage each match (shuffle), or always one. With the Stages "
                             "option's \"the opponent's\", that game's stage of the one chosen.",
                             kFeatureMenuChoices, stages});
}

} // namespace cps3recomp
