// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The native forms of the routines 3rd Strike's video hand-off rewrites call (video_palette.cpp, video_registers.cpp,
// video_lists.cpp, video_textures.cpp), for their dry runs (runtime/lift.h, AddNativeForm). With the arcade timing the
// game's own code runs in place of those rewrites, for its exact cycles; each is then run dry before the game's code,
// on a copy of the registers and an overlay of memory, to hand the native interface what the game's code is about to
// give the chips (cps3recomp/docs/NATIVE.md, "Arcade timing"). What a dry run calls cannot be the game's code (that
// would change the game), so each routine below is its native form: what the routine does to main RAM and r0 (the
// result the rewrites read), on the dry run's memory. Its writes to the chips are dropped with the rest of the dry
// run's. The other registers are left as they come: no rewrite reads them after the call.
//
// They are checked by what they lead to: the hand-overs of the dry runs against the board's (games/sfiii3/checknative.sh
// with the arcade timing on).
#include "lift.h"

namespace cps3recomp {

namespace {
    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }
    int32_t Word(Guest &g, uint32_t at) {
        return static_cast<int16_t>(g.Read16(at));
    }

    // 0612D428: r1 / r0, signed, the quotient (towards 0) in r0. By 0: r0 0, and the error 0x44E noted at 0x02079C90.
    void Divide(Guest &g) {
        const int32_t divisor = static_cast<int32_t>(g.R(0)), dividend = static_cast<int32_t>(g.R(1));
        if (divisor == 0) {
            g.Write32(0x02079C90, 0x44E);
            g.R(0) = 0;
            return;
        }
        const int32_t quotient = (dividend == INT32_MIN && divisor == -1) ? INT32_MIN : dividend / divisor;
        g.R(0) = g.R(1) = Signed(quotient);
    }

    // 0612E14C: the four tilemaps' tile banks set again. For each of three banks, a mask of the tile bank settings
    // (0x0206A1DE) from a table of masks and bits (0x06605548: a word of mask, a word of the settings' bit), shifted on
    // for each tilemap: where the settings have all of the mask's bits, the bank's bit is set in the tilemap's settings
    // word (0x0206A15A, ten bytes a tilemap: kTilemapWork + 6), else cleared. Then the settings made the last ones
    // (0x0206A1DC to 0x0206A1DE).
    void SetTileBanks(Guest &g) {
        constexpr uint32_t kMasks = 0x06605548, kAsked = 0x0206A1DE, kNext = 0x0206A1DC, kSettings = 0x0206A15A;
        for (uint32_t bank = 0; bank < 3; bank++) {
            uint16_t mask = g.Read16(kMasks + bank * 4);
            for (uint32_t n = 0; n < 4; n++) {
                const uint16_t have = static_cast<uint16_t>(g.Read16(kAsked) & mask);
                const uint32_t at = kSettings + Signed(static_cast<int8_t>(n * 10));
                const uint16_t bit = g.Read16(kMasks + 2 + bank * 4);
                g.Write16(at, static_cast<uint16_t>(have == mask ? g.Read16(at) | bit : g.Read16(at) & ~bit));
                mask = static_cast<uint16_t>(mask << 1);
            }
        }
        g.Write16(kAsked, g.Read16(kNext));
    }

    // 06005AFC: the screen's mode set up again (the screen turned or back): the whole picture's zoom asked for
    // (0x0200DCD0), and the layers' offsets (eight words from 0x0200DCD6) from the text layer's kind's row of the
    // turned screen's table (0x0613E482, 16 bytes a kind), or 0 when not turned
    void ModeChanged(Guest &g) {
        constexpr uint32_t kZoomAsked = 0x0200DCD0, kTurned = 0x02000005, kTextKind = 0x0200DCCC, kOffsets = 0x0613E482;
        static constexpr uint32_t kTo[8] = {0x0200DCD6, 0x0200DCD8, 0x0200DCDA, 0x0200DCDC, 0x0200DCDE, 0x0200DCE0, 0x0200DCE2, 0x0200DCE4};
        g.Write8(kZoomAsked, 1);
        const bool turned = g.Read8(kTurned) != 0;
        const uint32_t row = kOffsets + (turned ? g.Read32(kTextKind) * 16 : 0);
        for (uint32_t k = 0; k < 8; k++) {
            g.Write16(kTo[k], turned ? g.Read16(row + k * 2) : 0);
        }
    }

    // The CPU's window on character RAM: its bank now (0x02079C96) and the one before (0x02079C94); the board's register
    // 0x040C0086 takes its low four bits
    constexpr uint32_t kBank = 0x02079C96, kBankBefore = 0x02079C94, kBankRegister = 0x240C0086;

    // 0613AA5E: the bank set to r4 (if it is not already: the one now kept as the one before); r0 the bank before
    void SetCharBank(Guest &g) {
        const uint16_t want = static_cast<uint16_t>(g.R(4));
        if (g.Read16(kBank) == want) {
            g.R(0) = Signed(Word(g, kBank));
            return;
        }
        g.Write16(kBankBefore, g.Read16(kBank));
        g.Write16(kBank, want);
        g.Write16(kBankRegister, static_cast<uint16_t>(want & 15));
        g.R(0) = Signed(Word(g, kBankBefore));
    }

    // 0613AAB0: the bank before put back (the two swapped); r0 the bank it was
    void RestoreCharBank(Guest &g) {
        const uint16_t now = g.Read16(kBank), before = g.Read16(kBankBefore);
        if (now == before) {
            g.R(0) = Signed(Word(g, kBank));
            return;
        }
        g.Write16(kBank, before);
        g.Write16(kBankRegister, static_cast<uint16_t>(before & 15));
        g.Write16(kBankBefore, now);
        g.R(0) = Signed(static_cast<int16_t>(now));
    }

    // 061379EC: r4 pages of character RAM's areas for the lists taken (r5 the owner's number): the free pages are a
    // list of words (0x02074FB8: each the offset of the next free one, 0x1002 the end, 0x1000 past the last); the first
    // run of r4 pages one after another is taken out of it, its owner and length noted (0x02072FB4, two words a page)
    // and the owner's count of pages added to (0x02075FBC, a word an owner). r0 the first page's number (from 1), or 0
    // when there is no such run.
    void TakeArea(Guest &g) {
        constexpr uint32_t kFree = 0x02074FB8, kTaken = 0x02072FB4, kOwners = 0x02075FBC;
        constexpr uint32_t kEnd = 0x1002, kPast = 0x1000;
        const uint32_t pages = g.R(4), owner = g.R(5);
        uint32_t link = 0, at = 0, first = 0;
        for (;;) {
            first = Signed(Word(g, kFree + at)); // (a run starts at the free page `link` points to)
            if (first == kEnd) {
                g.R(0) = 0;
                return;
            }
            at = first;
            uint32_t left = pages;
            bool found = false;
            for (;;) {
                if (--left == 0) {
                    found = true;
                    break;
                }
                if (at == kPast) {
                    g.R(0) = 0;
                    return;
                }
                if (Signed(Word(g, kFree + at) - 2) != at) {
                    break; // (not the next page: the run starts again from here)
                }
                at += 2;
            }
            if (found) {
                break;
            }
            link = at;
        }
        g.Write16(kFree + link, g.Read16(kFree + at));
        g.Write16(kTaken + first * 2, static_cast<uint16_t>(owner));
        g.Write16(kTaken + 2 + first * 2, static_cast<uint16_t>(pages));
        const uint32_t count = kOwners + owner * 2;
        g.Write16(count, static_cast<uint16_t>(pages + Signed(Word(g, count))));
        g.R(0) = (first * 2) >> 2;
    }

    // 06137980: page r4's place in the CPU's window (4 KB a page, from 1)
    void AreaPlace(Guest &g) {
        g.R(0) = Signed((static_cast<int16_t>(g.R(4)) - 1) << 12);
    }
} // namespace

void RegisterVideoForms() {
    AddNativeForm(0x0612D428, Divide);
    AddNativeForm(0x0612E14C, SetTileBanks);
    AddNativeForm(0x06005AFC, ModeChanged);
    AddNativeForm(0x0613AA5E, SetCharBank);
    AddNativeForm(0x0613AAB0, RestoreCharBank);
    AddNativeForm(0x061379EC, TakeArea);
    AddNativeForm(0x06137980, AreaPlace);
}

} // namespace cps3recomp
