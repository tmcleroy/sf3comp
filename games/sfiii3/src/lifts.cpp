// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's routines rewritten as readable C++, and its hooks (cps3recomp/runtime/lift.h). Each names the
// routine's address and the hash of its code (tools/lifthash.py on the deciphered program), so it runs only on the
// code it was written from; a rewrite is checked against the game's code with CPS3RECOMP_LIFT_CHECK=1.
#include "game.h"
#include "lift.h"

#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace cps3recomp {

namespace {
    // 0611E0EE: the next of the 64 numbers of a table, from a counter that goes round them: the game's dice. The
    // counter's place and the table's are in the routine's literal pool (0611E1B4, 0611E1B8: 020155E8 and 065EB434),
    // read as the routine reads them. Leaves r0 the number, r1 the table, r3 the counter plus one before it was
    // wrapped, r4 the counter's place, and T clear (the shift's).
    bool NextRandom(Guest &g) {
        const uint32_t counter = g.Read32(0x0611E1B4), table = g.Read32(0x0611E1B8);
        const uint32_t next = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(counter))) + 1;
        g.Write16(counter, static_cast<uint16_t>(next));
        g.Write16(counter, static_cast<uint16_t>(static_cast<int16_t>(g.Read16(counter)) & 63));
        const uint32_t offset = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(counter))) << 1;
        g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(table + offset)));
        g.R(1) = table;
        g.R(3) = next;
        g.R(4) = counter;
        g.SetT(false);
        g.Return(13); // (eleven instructions and rts, as the original counts)
        return true;
    }

    const Lift kLifts[] = {
        {"NextRandom", 0x0611E0EE, 24, 0xF56C31F46D9EC36Aull, NextRandom, true},
    };

    // 06000528: the vertical blank's interrupt, once a frame. (A demonstration of a hook, and a debugging aid:
    // SFIII3_VBLANKS=1 says every 600th, counted only for the message.)
    void VerticalBlank(Guest &) {
        static const bool say = getenv("SFIII3_VBLANKS") != nullptr;
        static unsigned long long count = 0;
        if (say && ++count % 600 == 0) {
            fprintf(stderr, "vertical blanks: %llu\n", count);
        }
    }
} // namespace

void RegisterLifts3rdStrike() {
    RegisterLifts(kLifts, std::size(kLifts));
    AddHook("VerticalBlank", 0x06000528, 8, 0xF41697FE6C5AEE4Dull, VerticalBlank);
}

} // namespace cps3recomp
