// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp's fight/session.h.
//
// A fighting game's training session, for the games on the CPS-3: the menu's "Start" (the board switched off and on,
// and the way to the character choice with two players worked unseen), player 1's controls choosing for both, the
// round kept as its fighting begins for "Start over" (sides swapped or not), and the aids of fight.h put together:
// the combo readout, the input display, frame data and its meter, health that comes back, and the dummy.
//
// cps2recomp's, without its alternative modes and battles (not on this toolkit yet), and with the game's places as a
// game's sources describe them (Description). The steps of the fight are told by the game's sources (Step, Contact,
// Hit): read once a frame from the game's memory until the translated code has hooks for them.
#pragma once

#include "fight.h"
#include "game.h"
#include "system.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cps3recomp {
class Archive;
}

namespace cps3recomp::session {

struct Description {
    // --- the game's memory ---
    uint32_t player[2] = {}; // the players' records
    uint32_t x = 0;          // in them: the place across the stage (a signed word)
    uint32_t health = 0;     // ... the health (a byte)
    uint8_t healthFull = 0;
    uint32_t credits = 0; // (a byte: the credits, for the way to the character choice)
    // A round is being fought (the aids work only then)
    bool (*fighting)() = nullptr;
    // A player has joined (for the way to the character choice) and has chosen its character
    bool (*joined)(int player) = nullptr;
    bool (*chosen)(int player) = nullptr;
    // The players are choosing their characters
    bool (*choosing)() = nullptr;
    // What a player holds as the game takes it (Button bits; none: what the port gave the board), and whether it faces left
    uint16_t (*held)(int player) = nullptr;
    bool (*facingLeft)(int player) = nullptr;
    // For the dummy's guard: whether something of player 1's is near enough to guard, and must be guarded standing
    void (*guardView)(Machine &machine, bool &inRange, bool &standing) = nullptr;
    // The game's own readouts, beside the toolkit's (lines and boxes of the HUD)
    void (*hud)(Machine &machine, std::vector<HudLine> &lines, std::vector<HudBox> &boxes) = nullptr;
    float inputY = 0.30f, inputSpacing = 0.062f, framesY = 0.72f;
    int framesTop = 193;
    fight::DummyOptions dummy;
    // The game's own training features, and which of them are aids (working only in a session)
    std::vector<Feature> features;
    std::vector<const char *> ownAids;
};

// The menu's page for training
constexpr const char *kPage = "Training mode";

void Register(const Description &description);
void StartSession(Machine &machine);
bool InSession();
// No clock and nobody knocked out (a session)
bool NoClock();
bool NoKnockOut();

// From the game's sources: a player's step of a fight; a hit landing or guarded; a hit's damage taken
void Step(int player, const fight::StepView &view);
void Contact(int victim, bool guarded);
void Hit(int player, unsigned damage, bool chip = false);
fight::Doing Now(int player);

// Around the port's machine's frame (the game's beforeFrame and frame), and its save states
void BeforeFrame(Machine &machine);
void Frame(Machine &machine);
void State(Archive &ar);

} // namespace cps3recomp::session
