// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Ring out: no health is lost; a fighter hit while at an end of the stage (against its wall) is knocked out of the
// ring and loses the round. The ring outs player 1 makes in a row (the opponent's rounds lost so, until player 1 is
// rung out) are the score, the best kept (settings.ini's ringout_best). In any game, one player or two.
//
// The walls: a fighter is kept within 164 of the camera's limits (seen in made-up play: 108 and 916 with the limits
// 272 and 752); "at the wall" is within 24 of that. A hit is the record's count of hits taken going up; the knock-out
// is the health set to none, as the One hit cheat does.
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace cps3recomp {

namespace {
    struct State {
        bool fighting = false;
        bool out[2] = {};       // rung out this round
        uint16_t hits[2] = {};  // the records' hits taken at the last frame
        uint16_t streak = 0;    // player 1's ring outs in a row
        uint32_t shownUntil = 0, frame = 0;
        int8_t lastOut = -1;    // who was rung out last (for the line said)
    } r;

    bool InMatch() {
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2;
    }
    int16_t Signed16(uint32_t address) {
        return static_cast<int16_t>(Ram16(address));
    }
} // namespace

void RingOutBeforeFrame(Machine &) {
    r.frame++;
    if (!FeatureOn("ringout") || session::InSession()) {
        r.fighting = false;
        return;
    }
    const bool fighting = InMatch();
    if (fighting && !r.fighting) {
        r.out[0] = r.out[1] = false; // (a round begins)
        for (int p = 0; p < 2; p++) {
            r.hits[p] = Ram16(sfiii3::kPlayer[p] + sfiii3::kHitCount);
        }
    }
    r.fighting = fighting;
    if (!fighting) {
        return;
    }
    const int left = Signed16(sfiii3::kCameraLeft[0]) - 164, right = Signed16(sfiii3::kCameraRight[0]) + 164;
    for (int p = 0; p < 2; p++) {
        const uint32_t base = sfiii3::kPlayer[p];
        const uint16_t hits = Ram16(base + sfiii3::kHitCount);
        const int x = Signed16(base + sfiii3::kX);
        const bool hit = hits != r.hits[p];
        r.hits[p] = hits;
        if (hit && !r.out[0] && !r.out[1] && (x <= left + 24 || x >= right - 24)) {
            r.out[p] = true;
            r.lastOut = static_cast<int8_t>(p);
            r.shownUntil = r.frame + 150;
            if (p == 1) {
                r.streak++;
                if (r.streak > FeatureValue("ringout_best") && !RunningAhead()) {
                    SetFeatureValue("ringout_best", std::min<int>(r.streak, 99));
                }
            } else {
                r.streak = 0;
            }
        }
        // (no health lost: only a ring out knocks out)
        SetRam8(base + sfiii3::kLife, r.out[p] ? 0 : sfiii3::kFullLife);
    }
}

void RingOutHud(std::vector<HudLine> &lines) {
    if (!FeatureOn("ringout") || session::InSession()) {
        return;
    }
    char text[64];
    snprintf(text, sizeof(text), "RING OUT  %u IN A ROW  BEST %d", r.streak, std::max<int>(r.streak, FeatureValue("ringout_best")));
    lines.push_back({0.5f, 0.235f, 0, text, 255, 160, 80});
    if (r.lastOut >= 0 && r.frame < r.shownUntil) {
        lines.push_back({0.5f, 0.40f, 0, r.lastOut == 1 ? "RING OUT!" : "RUNG OUT!", 255, 230, 60});
    }
}

void RingOutState(Archive &ar) {
    ar.Mark(0x52494E47); // RING
    ar.Pod(r);
}

void RegisterRingOut(Game &game) {
    game.features.push_back({"ringout", "Ring out", "Ring out",
                             "No health is lost: a fighter hit while against an end of the stage is knocked out of the ring and loses "
                             "the round. How many times in a row you ring your opponent out is the score (the best is kept)."});
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
    game.features.push_back({"ringout_best", "Ring out", "Best", "The most ring outs in a row. (Left puts it back.)", kFeatureOwn, choices});
}

} // namespace cps3recomp
