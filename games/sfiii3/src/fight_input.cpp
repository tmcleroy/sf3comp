// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's players' input, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h):
// where a fighter's lever and buttons come from each frame (0x06092294, 0x060B6242).
//
// How a fighter gets its input. Once a frame the fighter's own routine (0x061195E0, r4 its record, r5 the person's
// lever and buttons) puts a word in the place its record names (+0x3B8): a person's controls as they are (bits 0-3
// the lever: up 1, down 2, one way 4, the other 8; then the three punches, the three kicks); the computer's choice
// (CPU_Sub, computer_entry.cpp's ComputerPlays) put in the same form (below); or, while attract mode's demonstration
// fights play (a byte at 0x02016B6C 0xFF), the next word of the fight's recording (below), put in that form too.
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    constexpr uint32_t kRecordings = 0x02017020; // longs, a side each: where the demonstration's recording has got to

    // 06092294: the next word of the demonstration fight's recording for a side (r4), and the recording moved on.
    // Leaves r4 the word, r5 the side's place in kRecordings, r2 where its recording now is.
    bool NextRecorded(Guest &g) {
        const uint32_t place = kRecordings + Signed(static_cast<int16_t>(g.R(4))) * 4;
        const uint32_t at = g.Read32(place);
        const uint32_t word = Signed(Word(g, at));
        g.Write32(place, at + 2);
        g.R(0) = word & 0xFFFF;
        g.R(2) = at + 2;
        g.R(4) = word;
        g.R(5) = place;
        g.Return(10);
        return true;
    }

    // 060B6242: a lever and buttons (r4) as the computer and the recordings keep them (the kicks at bits 7-9) put in a
    // person's form (the kicks at bits 8-10, bit 7 unused); r0 the word. Leaves r4 the word, r5 its low seven bits,
    // r2 0x700.
    bool AsPlayersInput(Guest &g) {
        const uint32_t low = g.R(4) & 0x7F;
        const uint32_t kicks = ((g.R(4) & 0xFFFF) << 1) & 0x700;
        g.R(2) = 0x700;
        g.R(4) = kicks | low;
        g.R(5) = low;
        g.R(0) = kicks | low;
        g.Return(8);
        return true;
    }

    // (free: r1-r7 and T for the first, r1-r6 and T for the second, whose callers read r7: liveout.py)
    const Lift kLifts[] = {
        {"NextRecorded", 0x06092294, 0x16, 0x0E25CAEB4E08C3AFull, NextRecorded, true, 0x100FE},
        {"AsPlayersInput", 0x060B6242, 0x12, 0xAA619E8E79F01418ull, AsPlayersInput, true, 0x1007E},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightInput() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
