// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Training scenarios: a session set up to practise one thing, over and over.
//
// "Evo Moment 37": Ken, with health that any guarded hit takes, against Chun-Li's second super art (Houyoku-sen), which
// she does a moment into each try. Every hit of it has to be parried; the readout counts them, and says where a try
// ended. A few seconds after the super the round starts over, so the tries come one after another (after the last
// parry there is time for the answer: the jump, the kick and the rest).
//
// How: a training session is started (the session's own command), the character choice is steered as the alternative
// modes steer it (battle.cpp: each side's character written through the choice, the super art's cursor moved to the
// one wanted, a press now and then), and in the round player 2's controls are the scenario's (the dummy's choices are
// left alone, and do nothing meanwhile). Player 1's health is kept at one point until the try is over, player 2's
// gauge full. The parries and the hits are training.cpp's (ReadParries), told here.
//
// Nothing of this is in a save state: a state loaded in the middle of a try loses the try's count, no more.
#include "sfiii3.h"

#include "game.h"
#include "session.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cps3recomp {

namespace {
    constexpr uint8_t kKen = 11, kChunLi = 16; // (the characters' numbers: battle.cpp's kCharacters)
    constexpr uint8_t kSecondArt = 1;
    constexpr int kWait = 70;        // steps of the round before the super's motion
    constexpr int kAfter = 420;      // ... and after it before the round starts over
    constexpr int kAfterOver = 150;  // ... or after a round that ended (the knock-out)
    constexpr int kLeast = 30;       // steps after the motion before the super can be over

    struct Scenario {
        int kind = 0;        // 1: Moment 37
        uint32_t starts = 0; // the session it is (session::Starts)
        int tick = 0;        // steps of the try's round fought
        int over = 0;        // steps since the round ended
        int parries = 0;
        int result = 0;      // 0 going on, 1 every hit parried, 2 hit or guarded
        int best = 0, tries = 0, made = 0;
        uint32_t frames = 0;
        uint64_t last = 0;   // the machine's frame at the step before (the round put back by the player: a try again)
    } s;

    bool Locked() {
        return Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    }
    void Run(const char *name, Machine &m) {
        for (const Command &command : TheGame().commands) {
            if (!strcmp(command.name, name) && command.run) {
                command.run(m);
                return;
            }
        }
    }
    // Player 2's controls at a step of the try: two quarter circles towards player 1 and a kick
    uint16_t Held(int tick, bool onRight) {
        const uint16_t towards = onRight ? kLeft : kRight;
        const int at = tick - kWait;
        if (at < 0 || at >= 21) {
            return 0;
        }
        static const int kMotion[7] = {1, 2, 3, 1, 2, 3, 4}; // down, down-towards, towards, ..., towards with the kick
        switch (kMotion[at / 3]) {
        case 1: return kDown;
        case 2: return static_cast<uint16_t>(kDown | towards);
        case 3: return towards;
        default: return static_cast<uint16_t>(towards | kKick3);
        }
    }
    void StartMoment37(Machine &m) {
        session::StartSession(m);
        s = {};
        s.kind = 1;
        s.starts = session::Starts();
    }
    // The round as it began, in the middle whatever the player holds or has chosen for "Start over" (which reads both)
    void NextTry(Machine &m) {
        const int place = FeatureSetting("restart_at");
        const uint16_t held = m.players[0];
        SetFeatureValue("restart_at", 0);
        m.players[0] = 0;
        Run("training_restart", m);
        m.players[0] = held;
        SetFeatureValue("restart_at", place);
        s.tick = s.over = s.parries = s.result = 0;
    }
} // namespace

bool ScenarioOn() {
    return s.kind != 0;
}

void ScenarioBeforeFrame(Machine &m) {
    if (!s.kind) {
        return;
    }
    if (!session::InSession() || session::Starts() != s.starts) {
        s = {}; // (the session over, or another one started)
        return;
    }
    if (session::Booting()) {
        return;
    }
    s.frames++;
    const bool press = s.frames % 16 < 3;
    const bool fighting = TheGame().fighting && TheGame().fighting();
    if (!Locked()) {
        // The character choice: Ken, and Chun-Li with her second super art
        static const uint8_t kWant[2] = {kKen, kChunLi};
        for (int p = 0; p < 2; p++) {
            const uint8_t state = Ram8(sfiii3::kSelectState[p]);
            m.players[p] = 0;
            if (state == 0) {
                m.players[p] = press ? kStart : 0;
            } else if (state >= 2) {
                SetRam8(sfiii3::kChosen[p], kWant[p]);
                m.players[p] = state < 5 && press ? kPunch1 : 0;
                if (p == 1 && state == 4 && Ram8(sfiii3::kSelectArt[1]) != kSecondArt) {
                    m.players[p] = press ? kDown : 0;
                }
            }
        }
        return;
    }
    if (!fighting) {
        // (the versus screen and the round's start: a press now and then; a round over: the next try after a moment)
        if (s.tick == 0) {
            m.players[1] = press ? kPunch1 : 0;
        } else if (++s.over >= kAfterOver) {
            NextTry(m);
        }
        return;
    }
    s.over = 0;
    if (s.tick > 0 && m.frame != s.last + 1) {
        s.tick = s.parries = s.result = 0; // (the player's own "Start over", or a state loaded)
    }
    s.last = m.frame;
    if (s.tick == 0) {
        s.tries++;
    }
    s.tick++;
    const uint32_t one = sfiii3::kPlayer[0], two = sfiii3::kPlayer[1];
    const bool onRight = static_cast<int16_t>(Ram16(two + sfiii3::kX)) >= static_cast<int16_t>(Ram16(one + sfiii3::kX));
    m.players[1] = Held(s.tick, onRight);
    // (player 2's gauge full for the super; player 1's health one point until the try is over)
    const uint8_t most = Ram8(sfiii3::kMaxMeterCount[1]);
    if (s.tick < kWait && most && Ram8(sfiii3::kMeterCount[1]) != most && Ram8(sfiii3::kMeterCountShown[1]) != most) {
        SetRam8(sfiii3::kGauge[1], 0);
        SetRam8(sfiii3::kMeterCount[1], most);
        SetRam8(sfiii3::kMeterUpdate[1], 1);
    }
    if (s.result == 0 && Ram8(one + sfiii3::kLife) > 1) {
        SetRam8(one + sfiii3::kLife, 1);
    }
    // (the super over, Chun-Li free again, and nothing of it hit or guarded: done)
    if (s.result == 0 && s.parries > 0 && s.tick > kWait + 21 + kLeast && session::Now(1) == fight::Doing::Free) {
        s.result = 1;
        s.made++;
    }
    if (s.tick >= kWait + kAfter) {
        NextTry(m);
    }
}

// From training.cpp's reading of the parries: player `p` parried a hit, or was hit or guarded one
void ScenarioParried(int p) {
    if (s.kind && p == 0 && s.result == 0 && s.tick >= kWait) {
        s.parries++;
        s.best = std::max(s.best, s.parries);
    }
}
void ScenarioStruck(int p) {
    if (s.kind && p == 0 && s.result == 0 && s.tick >= kWait) {
        s.result = 2;
    }
}

void ScenarioHud(std::vector<HudLine> &lines) {
    if (!s.kind || session::Booting() || !Locked()) {
        return;
    }
    char text[64];
    snprintf(text, sizeof(text), "MOMENT 37   PARRIED %d", s.parries);
    lines.push_back({0.5f, 0.22f, 0, text});
    if (s.result == 1) {
        lines.push_back({0.5f, 0.285f, 0, "ALL OF IT PARRIED: NOW THE ANSWER", 96, 230, 120});
    } else if (s.result == 2) {
        snprintf(text, sizeof(text), s.parries ? "HIT AFTER %d" : "HIT BY THE FIRST", s.parries);
        lines.push_back({0.5f, 0.285f, 0, text, 255, 110, 96});
    }
    snprintf(text, sizeof(text), "TRY %d   DONE %d   BEST %d", s.tries, s.made, s.best);
    lines.push_back({0.5f, 0.35f, 0, text});
}

void RegisterScenarios(Game &game) {
    game.commands.push_back({"scenario_moment37", session::kPage, "Scenario: Evo Moment 37",
                             "A training session as Ken with a sliver of health against Chun-Li, who does her super art a moment "
                             "into every try: parry every hit of it (forward for each; the first before the screen stops), "
                             "then answer. The round starts over by itself after each try. Start a training session as usual "
                             "to leave it.",
                             StartMoment37});
}

} // namespace cps3recomp
