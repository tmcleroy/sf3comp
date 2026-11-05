// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The native forms of the routines 3rd Strike's video hand-off rewrites call (video_palette.cpp, video_registers.cpp,
// video_lists.cpp, video_textures.cpp), for their dry runs (runtime/lift.h, AddNativeForm). With the arcade timing the
// game's own code runs in place of those rewrites, for its exact cycles; each is then run dry before the game's code,
// on a copy of the registers and an overlay of memory, to hand the native interface what the game's code is about to
// give the chips (cps3recomp/docs/NATIVE.md, "Arcade timing"). On the stage board the stage game's are run only dry
// (games/sfiii3/NATIVE.md, "Guest stages"). What a dry run calls cannot be the game's code (that would change the
// game), so each routine below is its native form: what the routine does to main RAM and r0 (the result the rewrites
// read), on the dry run's memory, for a game's places (video.h: the three games' routines are the same code). Its
// writes to the chips are dropped with the rest of the dry run's. The other registers are left as they come: no
// rewrite reads them after the call.
//
// They are checked by what they lead to: the hand-overs of the dry runs against the board's (games/sfiii3/checknative.sh
// with the arcade timing on, and with a stage on).
#include "video.h"

namespace cps3recomp {

namespace {
    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }
    int32_t Word(Guest &g, uint32_t at) {
        return static_cast<int16_t>(g.Read16(at));
    }

    // (divide): r1 / r0, signed, the quotient (towards 0) in r0. By 0: r0 0, and the error 0x44E noted.
    template <const VideoPlaces &P> void Divide(Guest &g) {
        const int32_t divisor = static_cast<int32_t>(g.R(0)), dividend = static_cast<int32_t>(g.R(1));
        if (divisor == 0) {
            g.Write32(P.divideError, 0x44E);
            g.R(0) = 0;
            return;
        }
        const int32_t quotient = (dividend == INT32_MIN && divisor == -1) ? INT32_MIN : dividend / divisor;
        g.R(0) = g.R(1) = Signed(quotient);
    }

    // (setTileBanks): the four tilemaps' tile banks set again. For each of three banks, a mask of the tile bank
    // settings from a table of masks and bits (a word of mask, a word of the settings' bit), shifted on for each
    // tilemap: where the settings have all of the mask's bits, the bank's bit is set in the tilemap's settings word
    // (tilemapWork + 6, ten bytes a tilemap), else cleared. Then the next settings made the settings.
    template <const VideoPlaces &P> void SetTileBanks(Guest &g) {
        const uint32_t settings = P.tilemapWork + 6;
        for (uint32_t bank = 0; bank < 3; bank++) {
            uint16_t mask = g.Read16(P.tileMasks + bank * 4);
            for (uint32_t n = 0; n < 4; n++) {
                const uint16_t have = static_cast<uint16_t>(g.Read16(P.tileBankSettings) & mask);
                const uint32_t at = settings + Signed(static_cast<int8_t>(n * 10));
                const uint16_t bit = g.Read16(P.tileMasks + 2 + bank * 4);
                g.Write16(at, static_cast<uint16_t>(have == mask ? g.Read16(at) | bit : g.Read16(at) & ~bit));
                mask = static_cast<uint16_t>(mask << 1);
            }
        }
        g.Write16(P.tileBankSettings, g.Read16(P.tileBankNext));
    }

    // (modeChanged): the screen's mode set up again (the screen turned or back): the whole picture's zoom asked for,
    // and the layers' offsets (eight words from objectOffsetX: the objects', the text layer's shake, the tilemaps', the
    // zoom's centre) from the text layer's kind's row of the turned screen's table, or 0 when not turned
    template <const VideoPlaces &P> void ModeChanged(Guest &g) {
        constexpr uint32_t kTurned = 0x02000005;
        const uint32_t to[8] = {P.objectOffsetX, P.objectOffsetY, P.textShakeX, P.textShakeY, P.tilemapOffsetX, P.tilemapOffsetY, P.centreX, P.centreY};
        g.Write8(P.zoomAsked, 1);
        const bool turned = g.Read8(kTurned) != 0;
        const uint32_t row = P.turnedOffsets + (turned ? g.Read32(P.textKind) * 16 : 0);
        for (uint32_t k = 0; k < 8; k++) {
            g.Write16(to[k], turned ? g.Read16(row + k * 2) : 0);
        }
    }

    // The CPU's window on character RAM: its bank now and the one before; the board's register 0x040C0086 takes its
    // low four bits
    constexpr uint32_t kBankRegister = 0x240C0086;

    // (setCharBank): the bank set to r4 (if it is not already: the one now kept as the one before); r0 the bank before
    template <const VideoPlaces &P> void SetCharBank(Guest &g) {
        const uint16_t want = static_cast<uint16_t>(g.R(4));
        if (g.Read16(P.charBank) == want) {
            g.R(0) = Signed(Word(g, P.charBank));
            return;
        }
        g.Write16(P.charBankBefore, g.Read16(P.charBank));
        g.Write16(P.charBank, want);
        g.Write16(kBankRegister, static_cast<uint16_t>(want & 15));
        g.R(0) = Signed(Word(g, P.charBankBefore));
    }

    // (restoreCharBank): the bank before put back (the two swapped); r0 the bank it was
    template <const VideoPlaces &P> void RestoreCharBank(Guest &g) {
        const uint16_t now = g.Read16(P.charBank), before = g.Read16(P.charBankBefore);
        if (now == before) {
            g.R(0) = Signed(Word(g, P.charBank));
            return;
        }
        g.Write16(P.charBank, before);
        g.Write16(kBankRegister, static_cast<uint16_t>(before & 15));
        g.Write16(P.charBankBefore, now);
        g.R(0) = Signed(static_cast<int16_t>(now));
    }

    // (takeArea): r4 pages of character RAM's areas for the lists taken (r5 the owner's number): the free pages are a
    // list of words (each the offset of the next free one, 0x1002 the end, 0x1000 past the last); the first run of r4
    // pages one after another is taken out of it, its owner and length noted (two words a page) and the owner's count
    // of pages added to (a word an owner). r0 the first page's number (from 1), or 0 when there is no such run.
    template <const VideoPlaces &P> void TakeArea(Guest &g) {
        constexpr uint32_t kEnd = 0x1002, kPast = 0x1000;
        const uint32_t pages = g.R(4), owner = g.R(5);
        uint32_t link = 0, at = 0, first = 0;
        for (;;) {
            first = Signed(Word(g, P.freePages + at)); // (a run starts at the free page `link` points to)
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
                if (Signed(Word(g, P.freePages + at) - 2) != at) {
                    break; // (not the next page: the run starts again from here)
                }
                at += 2;
            }
            if (found) {
                break;
            }
            link = at;
        }
        g.Write16(P.freePages + link, g.Read16(P.freePages + at));
        g.Write16(P.takenPages + first * 2, static_cast<uint16_t>(owner));
        g.Write16(P.takenPages + 2 + first * 2, static_cast<uint16_t>(pages));
        const uint32_t count = P.ownerPages + owner * 2;
        g.Write16(count, static_cast<uint16_t>(pages + Signed(Word(g, count))));
        g.R(0) = (first * 2) >> 2;
    }

    // (areaPlace): page r4's place in the CPU's window (4 KB a page, from 1)
    void AreaPlace(Guest &g) {
        g.R(0) = Signed((static_cast<int16_t>(g.R(4)) - 1) << 12);
    }
} // namespace

template <const VideoPlaces &P> void RegisterVideoForms() {
    AddNativeForm(P.divide, Divide<P>);
    AddNativeForm(P.setTileBanks, SetTileBanks<P>);
    AddNativeForm(P.modeChanged, ModeChanged<P>);
    AddNativeForm(P.setCharBank, SetCharBank<P>);
    AddNativeForm(P.restoreCharBank, RestoreCharBank<P>);
    AddNativeForm(P.takeArea, TakeArea<P>);
    AddNativeForm(P.areaPlace, AreaPlace);
}

template void RegisterVideoForms<kThirdStrikeVideo>();
template void RegisterVideoForms<kNewGenerationVideo>();
template void RegisterVideoForms<kSecondImpactVideo>();

} // namespace cps3recomp
