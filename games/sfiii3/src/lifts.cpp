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
#include <string>
#include <vector>

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


    // --- the round's clock ---
    //
    // The clock is a word at 0x02011376 (seconds left), with a word of frames to its next tick at 0x02011378 that is
    // reloaded from the timer speed's frames (a word at 0x02028688, the board's timer speed setting's); the panel
    // keeps its own copy of the seconds at 0x02028678. Under 30 seconds (or 10 at the last) it flashes: 0x02028682
    // says the flashing is on, 0x02028680 counts the frames to the next flash, 0x02028683 is the flash's place in its
    // four colours.
    constexpr uint32_t kClock = 0x02011376, kClockFrames = 0x02011378, kClockReload = 0x02028688, kClockShown = 0x02028678;
    constexpr uint32_t kFlashing = 0x02028682, kFlashFrames = 0x02028680, kFlashPlace = 0x02028683;
    constexpr uint32_t kFlashEvery = 0x061B7884, kFlashColours = 0x061B7888; // (tables of words: by the speed of flashing, by place)
    constexpr uint32_t kSetPalette = 0x060D57CA; // (r4 the palette, r5-r7 and a long on the stack: the panel's colours)
    constexpr uint32_t kDrawClock = 0x060D6482;  // (r4, r5 the tens, r6 the units: draws the clock's digits)
    constexpr uint32_t kDivide = 0x0612D428;     // (r1 / r0, signed, the quotient in r0)

    // A call of the game's with a long on the stack beyond the registers (taken off again after)
    void CallWithLong(Guest &g, uint32_t address, uint32_t extra) {
        g.R(15) -= 4;
        g.Write32(g.R(15), extra);
        g.Call(address);
        g.R(15) += 4;
    }

    // 060D7CB8: the clock's flash. Each call takes a frame off its count; when that runs out the count starts again
    // (fast with r4 1, under ten seconds; slow with 0) and the panel's clock takes the next of its four colours.
    // Leaves r0 the flash's place after it moved on (1-4: 4 as it wraps to 0), or the count with nothing done; r7 5.
    bool ClockFlash(Guest &g) {
        const int16_t left = static_cast<int16_t>(g.Read16(kFlashFrames) - 1);
        g.Write16(kFlashFrames, static_cast<uint16_t>(left));
        if (left >= 0) {
            g.Return(10);
            return true; // (r0 as it came: the caller's)
        }
        g.Write16(kFlashFrames, g.Read16(kFlashEvery + static_cast<uint32_t>(static_cast<int8_t>(g.R(4))) * 2));
        const uint32_t colour = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kFlashColours + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kFlashPlace))) * 2)));
        g.R(4) = 22;
        g.R(5) = 0;
        g.R(6) = 4;
        g.R(7) = 5;
        CallWithLong(g, kSetPalette, colour);
        const int8_t place = static_cast<int8_t>(g.Read8(kFlashPlace) + 1);
        g.Write8(kFlashPlace, static_cast<uint8_t>(place));
        g.R(0) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kFlashPlace)));
        if (place == 4) {
            g.Write8(kFlashPlace, 0);
        }
        g.Return(30);
        return true;
    }


    // 060D7BAE: the clock, once a frame in a round. Nothing while the game is held (0x02011386), while 0x0611C87C says
    // the round is not timed, or while 0x0200EECC or 0x0201136E is set; nothing either with no time left. Under 30
    // seconds the clock starts flashing (slowly; fast from 10); each frame the frames to the next tick go down, and
    // at the tick a second goes, the panel's copy is set, and the digits drawn (at 0, the clock's colours set once
    // more and 00 drawn). Leaves r0 as the game's code would (all its callers read).
    bool ClockTick(Guest &g) {
        if (const int8_t held = static_cast<int8_t>(g.Read8(0x02011386))) {
            g.R(0) = static_cast<uint32_t>(held);
            g.Return(8);
            return true;
        }
        g.Call(0x0611C87C);
        if (g.R(0) != 0) {
            g.Return(8);
            return true;
        }
        for (const uint32_t at : {0x0200EECCu, 0x0201136Eu}) {
            g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(at)));
            if (g.R(0) != 0) {
                g.Return(8);
                return true;
            }
        }
        const int16_t clock = static_cast<int16_t>(g.Read16(kClock));
        if (clock == 0) {
            g.Return(12);
            return true;
        }
        const uint16_t frames = g.Read16(kClockFrames), reload = g.Read16(kClockReload);
        g.R(7) = kFlashing; // (as the game's code leaves them for what it calls: r5 the flash's count's place, r6 0)
        g.R(5) = kFlashFrames;
        g.R(6) = 0;
        const auto flash = [&](int fast) {
            g.R(4) = static_cast<uint32_t>(fast);
            g.Call(0x060D7CB8);
        };
        if (g.Read8(kFlashing) != 0) {
            g.R(0) = static_cast<uint32_t>(clock == 10 ? static_cast<int16_t>(reload) : clock); // (at 10, read to compare)
            if (clock == 10 && frames == reload) {
                g.Write16(kFlashFrames, 0); // (the fast flashing begun at once)
                flash(1);
            } else {
                flash(clock <= 10 ? 1 : 0);
            }
        } else {
            g.R(0) = static_cast<uint32_t>(clock);
            if (clock == 30 && frames == reload) {
                g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(reload));
                g.Write8(kFlashing, 1); // (the flashing begun)
                g.Write16(kFlashFrames, 0);
                flash(0);
            }
        }
        // (the frames to the next tick)
        if (g.Read16(kClockFrames) != 0) {
            g.Write16(kClockFrames, static_cast<uint16_t>(g.Read16(kClockFrames) - 1));
            g.Return(20);
            return true;
        }
        g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kClockReload)));
        g.Write16(kClockFrames, static_cast<uint16_t>(g.R(0)));
        g.Write16(kClock, static_cast<uint16_t>(g.Read16(kClock) - 1));
        if (g.Read16(kClock) == 0) {
            // (time up: the clock's colours once more)
            g.R(4) = 22;
            g.R(5) = 0;
            g.R(6) = 4;
            g.R(7) = 5;
            CallWithLong(g, kSetPalette, 8);
        }
        g.Write16(kClockShown, g.Read16(kClock));
        const uint32_t seconds = g.Read16(kClock);
        g.R(1) = seconds;
        g.R(0) = 10;
        g.Call(kDivide);
        const uint32_t tens = g.R(0);
        uint32_t units = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kClock))) - tens * 10;
        g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kClock)));
        g.R(1) = tens * 10; // (and r1-r3 as it leaves them for the digits' drawing)
        g.R(2) = tens;
        g.R(3) = kClockShown;
        g.R(4) = 0;
        g.R(5) = tens;
        if (g.R(0) == 0) {
            units = 0;
            g.R(5) = 0;
        }
        g.R(6) = units;
        g.Call(kDrawClock);
        g.Return(40);
        return true;
    }

    const Lift kLifts[] = {
        {"NextRandom", 0x0611E0EE, 24, 0xF56C31F46D9EC36Aull, NextRandom, true},
        {"ClockTick", 0x060D7BAE, 0x30, 0xD096A0977F288F4Bull, ClockTick, true, 0x100FE},
        {"ClockFlash", 0x060D7CB8, 0x48, 0x21663C64E30E0582ull, ClockFlash, true, 0x1007E},
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
    // (debugging aid, SFIII3_LIFTS_ONLY=NAME,NAME: only those registered, to check a rewrite that the others call)
    if (const char *only = getenv("SFIII3_LIFTS_ONLY")) {
        static std::vector<Lift> some;
        const std::string names = std::string(",") + only + ",";
        for (const Lift &lift : kLifts) {
            if (names.find(std::string(",") + lift.name + ",") != std::string::npos) {
                some.push_back(lift);
            }
        }
        RegisterLifts(some.data(), some.size());
    } else {
        RegisterLifts(kLifts, std::size(kLifts));
    }
    AddHook("VerticalBlank", 0x06000528, 8, 0xF41697FE6C5AEE4Dull, VerticalBlank);
}

} // namespace cps3recomp
