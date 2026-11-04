// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's fight, written again as C++ (rewrites: runtime/lift.h), one file a part:
//   fight_score.cpp  the points a hit scores (0x0609E488) and a player's score drawn on the panel (0x0609E57A)
//   fight_round.cpp  the round's time each frame (0x06096CE8: the clock and the play time), its end (the time over
//                    0x06117C26, the knock-out 0x06117D28, and what they call), and the round's management (0x060992B0)
//                    with its step for the fight (0x0609989E)
//   fight_match.cpp  the match: the frame's hit tests (0x0608C7C0); the round counted (0x060999B0: the marks, the
//                    wins, the match's records, the computer's play time), the judges' round (0x06099D5E and its eight
//                    steps), the round's end (0x06099FE4), the next round's steps (0x0609A13C, 0x0609A182-0x0609A46E), the
//                    match's end (0x0609A486 and its steps, the bonuses' 0x0609A5F8-0x0609A80C), after it (0x0609AAD0,
//                    0x0609ABC8, 0x0609ADC8, 0x0609AF52), the game's steps around the fight (0x06095750, 0x06095A16,
//                    0x06096CB4)
//   fight_input.cpp  a fighter's frame (0x061195E0: its input, what it keeps of the last frame, its commands read, its
//                    step's routine) and where its input comes from: a demonstration's recording (0x06092294, never
//                    played on the arcade), the computer's put in a person's form (0x060B6242)
//   fight_judge.cpp  what the hit tests found made of: a hit (0x0608CE68), a throw (0x0608C994), a trade (0x0608C860),
//                    and what they call: the guards and parries (0x0608DC44, 0x0608DAE8), the damage and stun
//                    (0x0608D7FC), the reaction (0x0608D02C), the hit stops
//   fight_commands.cpp  the command reader (0x060B2BCC: each of a fighter's commands moved on by its input, by
//                    the step types' routines), the input picked up for it (0x060B59BE, 0x060B55F8)
//   fight_hits.cpp   two boxes' overlap (0x0608E908: the hit tests' own), the throws' and the attacks' tests of the
//                    frame's hit queue (0x0608E43C, 0x0608E5C4), what was hit given its results (0x0608C7F8), the two
//                    fighters' push boxes (0x0611DBA4)
// (the damage of a hit, its scaling and its taking, and the clock's tick and flash: lifts.cpp)
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp), where they are known: this is the arcade's build of the same code, which differs from it
// here and there (the arcade's is what is written here). (Names only: that project is AGPL-3.0, and none of its code is
// here; what the routines do was read from the arcade's own program and checked call by call against it.)
//
// Registers: a rewrite leaves r0-r7 and T as the game's code does where liveout.py could not show that every caller
// ignores them (through tail jumps, the leftovers reach callers further up), and the stack pointer what it calls sees
// is the game's (Frame).
#pragma once

#include "lift.h"

#include <cstdint>

namespace cps3recomp::fighting {

// The routine's own frame on the stack (what it pushes), so that what it calls finds the stack pointer where the
// game's code has it
struct Frame {
    Frame(Guest &guest, uint32_t bytes)
        : g(guest), size(bytes) {
        g.R(15) -= size;
    }
    ~Frame() { g.R(15) += size; }
    Guest &g;
    const uint32_t size;
};

inline uint32_t Signed(int32_t value) {
    return static_cast<uint32_t>(value);
}
inline int16_t Word(Guest &g, uint32_t at) {
    return static_cast<int16_t>(g.Read16(at));
}
inline int8_t Byte(Guest &g, uint32_t at) {
    return static_cast<int8_t>(g.Read8(at));
}

// The players' records (plw), 0x498 bytes each
constexpr uint32_t kPlayers = 0x02068C6C, kPlayerSize = 0x498;
constexpr uint32_t kOperator = 0x03; // byte: a person plays it (wu.operator); 0 the computer

// The compiler's divisions: r1 by r0, the quotient in r0 (they leave the CPU's division flags as they do: called,
// never written again)
constexpr uint32_t kDivide = 0x0612D428;         // signed
constexpr uint32_t kDivideUnsigned = 0x0612D4DC; // unsigned

// fight_score.cpp: both players' scores drawn again at the next frame of the round's time (fight_round.cpp), as they
// are now in RAM: for the port's features that change the scores, or the panel, from outside (a tag's machine)
void AskScoresDrawn();
// ... asked for: true once (and drawn by the caller, DrawScores)
bool ScoresDrawAsked();
void DrawScores(Guest &g);

} // namespace cps3recomp::fighting
