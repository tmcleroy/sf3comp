// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's frame registers, written again as C++ (rewrites: runtime/lift.h), the second piece of the native video
// (cps3recomp/docs/NATIVE.md, games/sfiii3/NATIVE.md): the routines that set the video chip's once-a-frame registers
// hand the same values to the native interface (video/native.h, FrameRegisters) as they write the board's. With
// CPS3RECOMP_NATIVE_CHECK=1 the board compares what it latched with what these handed over, every frame
// (games/sfiii3/checknative.sh).
//
// All run in the vertical blank's interrupt (0x06000528), in the frame's first lines: the objects' global scrolls
// (0x0612DDCC), the screen's timing and the text layer's set-up when the game changes its mode (0x06005C00), the
// tilemaps' registers and the text layer's scrolls (0x0612E610), the whole picture's zoom when the game asks
// (0x06005FB8). The text layer's palette base is set with its colours' place (0x061388DA). Each takes the values the
// game's code left in main RAM for the frame (the layers' places: 0x0206A084 on, eight of 16 bytes for the objects,
// 0x0206A104 on, four of 16 for the tilemaps, their working words at 0x0206A154, ten bytes each; the stage's shake and
// the camera's offsets at 0x0200DCD2-0x0200DCE4) and moves the next frame's values up.
#include "lift.h"
#include "machine.h"

#include <iterator>

namespace cps3recomp {

namespace {
    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }
    int32_t Word(Guest &g, uint32_t at) {
        return static_cast<int16_t>(g.Read16(at));
    }

    // A register written: the board's, and the same value in the interface's terms
    void Register(Guest &g, uint32_t address, uint32_t value) {
        g.Write16(address, static_cast<uint16_t>(value));
        native::FrameRegisters *r = g.Board().NativeRegisters();
        if (!r) {
            return;
        }
        const uint32_t a = address & 0x1FFFFFFF;
        const uint16_t v = static_cast<uint16_t>(value);
        if (a >= 0x040C0000 && a < 0x040C0020) {
            r->scroll[(a - 0x040C0000) / 4][(a & 2) / 2] = v;
        } else if (a >= 0x040C0020 && a < 0x040C0060) {
            r->tilemap[(a - 0x040C0020) / 16][((a - 0x040C0020) % 16) / 2] = v;
        } else if (a >= 0x040C0060 && a < 0x040C0080) {
            r->screen[(a - 0x040C0060) / 2] = v;
        } else if (a >= 0x05050000 && a < 0x05050030) {
            r->text[(a - 0x05050000) / 4 * 2 + ((a & 2) ? 1 : 0)] = static_cast<uint8_t>(v);
        }
    }

    // --- the board's ---
    constexpr uint32_t kObjectScrolls = 0x240C0000; // eight pairs of words: x, y
    constexpr uint32_t kTilemaps = 0x240C0020;      // four of 16 bytes
    constexpr uint32_t kScreen = 0x240C0060;        // the screen's timing (60-66, 70-76) and zoom (68-6E, 78-7E)
    constexpr uint32_t kText = 0x25050000;          // the text layer's registers (bytes, in the low halves' low bytes)

    // --- the game's ---
    constexpr uint32_t kShakeX = 0x0200DCD2, kShakeY = 0x0200DCD4;          // words: the whole picture's shake
    constexpr uint32_t kObjectOffsetX = 0x0200DCD6, kObjectOffsetY = 0x0200DCD8;
    constexpr uint32_t kObjectLayers = 0x0206A084;  // 16 bytes each: the next x (long), x (long), the next y, y
    constexpr uint32_t kTilemapLayers = 0x0206A104; // 16 bytes each: the same
    constexpr uint32_t kTilemapWork = 0x0206A154;   // 10 bytes each: x, y, the settings' two words, the bases
    constexpr uint32_t kTilemapOffsetX = 0x0200DCDE, kTilemapOffsetY = 0x0200DCE0;
    constexpr uint32_t kUprightScreen = 0x02000005; // byte: the screen turned (the board's setting): 0 the usual
    constexpr uint32_t kTilemapSettings = 0x0206A1BC; // words, four bytes a tilemap: the next settings' low bits, the settings'
    constexpr uint32_t kTilemapFlips = 0x0206A1CC;    // ... the next flips, the flips
    constexpr uint32_t kTilemapBases = 0x0206A17C;    // longs, 16 bytes a tilemap: the next map base, the next line base,
                                                      // the map base (+8), the line base (+12)
    constexpr uint32_t kTileBanksAsked = 0x0206A1E0;  // byte: the tilemaps' tile banks to be set again (0x0612E14C)
    constexpr uint32_t kSetTileBanks = 0x0612E14C;
    constexpr uint32_t kTextLayer = 0x0206A144;     // the text layer's x (next, now) and y (next, now), longs
    constexpr uint32_t kTextOffsetX = 0x0200DCC6, kTextShakeX = 0x0200DCDA, kTextOffsetY = 0x0200DCC8, kTextShakeY = 0x0200DCDC;

    // 0612DDCC: the objects' eight global scrolls: each layer's x less the shake plus the objects' offset, its y plus the
    // shake and the offset less 2, ten bits each; the next frame's places made the current ones
    bool ObjectScrolls(Guest &g) {
        for (uint32_t i = 0; i < 8; i++) {
            const uint32_t layer = kObjectLayers + i * 16;
            const int32_t x = Word(g, layer + 4) - Word(g, kShakeX) + Word(g, kObjectOffsetX);
            Register(g, kObjectScrolls + i * 4, Signed(x) & 0x3FF);
            const int32_t y = Word(g, layer + 12) + Word(g, kShakeY) + Word(g, kObjectOffsetY) + 0xFFFE;
            Register(g, kObjectScrolls + i * 4 + 2, Signed(y) & 0x3FF);
            g.Write32(layer + 4, g.Read32(layer));
            g.Write32(layer + 12, g.Read32(layer + 8));
        }
        g.R(0) = kObjectLayers + 12;
        g.R(1) = 8;
        g.R(2) = 8;
        g.R(3) = 8;
        g.SetT(true);
        g.Return(8 * 40);
        return true;
    }

    // 0612E610: the four tilemaps' registers (scroll x and y, settings, bases) from their working words, made for the
    // frame from the layers' places, the shake and the offsets; then the text layer's scrolls. r8 kept; r0-r4 and T as
    // they come (no caller reads them)
    bool TilemapRegisters(Guest &g) {
        g.R(15) -= 32; // (r8, pr and 24 bytes: what 0x0612E14C finds)
        for (uint32_t n = 0; n < 4; n++) {
            const uint32_t work = kTilemapWork + Signed(static_cast<int8_t>(n * 10));
            const uint32_t layer = kTilemapLayers + n * 16;
            const uint32_t out = kTilemaps + n * 16;
            g.Write16(work, static_cast<uint16_t>(Word(g, layer + 4) + Word(g, kShakeX) + Word(g, kTilemapOffsetX)));
            g.Write16(work, static_cast<uint16_t>(Word(g, work) & 0x3FF));
            Register(g, out, g.Read16(work));
            int32_t y;
            if (g.Read8(kUprightScreen) == 0) {
                y = -Word(g, layer + 12) + Word(g, kShakeY) + Word(g, kTilemapOffsetY) + 0xFEFC;
            } else {
                y = -Word(g, layer + 12) + Word(g, kShakeY) + 4;
            }
            g.Write16(work + 2, static_cast<uint16_t>(y));
            g.Write16(work + 2, static_cast<uint16_t>(Word(g, work + 2) & 0x3FF));
            Register(g, out + 2, g.Read16(work + 2));
            Register(g, out + 4, g.Read16(work + 4));
            if (g.Read8(kTileBanksAsked) != 0) {
                g.Write8(kTileBanksAsked, 0);
                g.Call(kSetTileBanks);
            }
            // (the settings' low word: the bits kept, the next settings' ten, the flips)
            const uint32_t settings = kTilemapSettings + n * 4, flips = kTilemapFlips + n * 4;
            g.Write16(work + 6, static_cast<uint16_t>((Word(g, work + 6) & 0xFC00) | (Word(g, settings + 2) & 0x3FF)));
            g.Write16(work + 6, static_cast<uint16_t>(Word(g, work + 6) & 0xF3FF));
            g.Write16(work + 6, static_cast<uint16_t>(Word(g, work + 6) | (Word(g, flips + 2) & 0x0C00)));
            Register(g, out + 6, g.Read16(work + 6));
            // (the bases: the map's from its address, its seven bits in the low byte; the line scroll table's in the high)
            const uint32_t bases = kTilemapBases + n * 16;
            if (const uint32_t map = g.Read32(bases + 8)) {
                g.Write16(work + 8, static_cast<uint16_t>(static_cast<int32_t>(map + 0xDC000000u) >> 12));
                g.Write16(work + 8, static_cast<uint16_t>(Word(g, work + 8) & 127));
            }
            if (const uint32_t lines = g.Read32(bases + 12)) {
                const int32_t place = static_cast<int16_t>(static_cast<int32_t>(lines + 0xDC000000u) >> 4) & 0x7F00;
                g.Write16(work + 8, static_cast<uint16_t>(Word(g, work + 8) | place));
            }
            Register(g, out + 8, g.Read16(work + 8));
            // (the next frame's made the current)
            g.Write32(layer + 4, g.Read32(layer));
            g.Write32(layer + 12, g.Read32(layer + 8));
            g.Write32(bases + 8, g.Read32(bases));
            g.Write32(bases + 12, g.Read32(bases + 4));
            g.Write16(settings + 2, g.Read16(settings));
            g.Write16(flips + 2, g.Read16(flips));
        }
        // The text layer: x (its low byte, then its high), y (the same)
        const int32_t x = Word(g, kTextLayer + 4) + Word(g, kTextOffsetX) + Word(g, kTextShakeX);
        Register(g, kText + 0x0E, Signed(x));
        Register(g, kText + 0x10, Signed(static_cast<int16_t>(x) >> 8));
        const int32_t y = -Word(g, kTextLayer + 12) + Word(g, kTextOffsetY) + Word(g, kTextShakeY);
        Register(g, kText + 0x20, Signed(y));
        const int32_t high = static_cast<int16_t>(y) >> 8;
        Register(g, kText + 0x22, Signed(high));
        g.Write32(kTextLayer + 4, g.Read32(kTextLayer));
        g.Write32(kTextLayer + 12, g.Read32(kTextLayer + 8));
        g.R(15) += 32;
        g.R(0) = g.Read32(kTextLayer + 8);
        g.R(1) = kTextOffsetY;
        g.R(2) = kTextLayer;
        g.R(3) = kTextLayer + 12;
        g.SetT((static_cast<int16_t>(y) >> 7) & 1);
        g.Return(4 * 220 + 60);
        return true;
    }

    // 06005C00: the screen's timing and the text layer's set-up, when the game's mode has changed (the screen's turn,
    // 0x02000005, against the one last set, 0x02000660: 0x06005AFC sets the mode up again first) or it asks
    // (0x0200DCD1): from the mode's words at 0x0200DCA4 (the picture's edges, eight words) and the text layer's settings
    // by its kind (0x0200DCCC, eight bytes a kind at 0x0613E442)
    bool ScreenRegisters(Guest &g) {
        constexpr uint32_t kTurnSet = 0x02000660, kAsked = 0x0200DCD1, kMode = 0x0200DCA4, kSprites = 0x0200DCC4,
                           kTextKind = 0x0200DCCC, kTextKinds = 0x0613E442, kModeChanged = 0x06005AFC;
        const int32_t turn = static_cast<int8_t>(g.Read8(kUprightScreen));
        const int32_t was = static_cast<int8_t>(g.Read8(kTurnSet));
        if (turn != was) {
            g.Write8(kTurnSet, static_cast<uint8_t>(turn));
            g.R(2) = Signed(was);
            g.R(4) = Signed(turn);
            g.R(5) = kTurnSet;
            const uint32_t r14 = g.R(14); // (the routine keeps the turn's address in r14 while it calls, and puts r14 back)
            g.R(14) = kUprightScreen;
            g.R(15) -= 8;
            g.Call(kModeChanged);
            g.R(15) += 8;
            g.R(14) = r14;
        } else {
            if (g.Read8(kAsked) == 0) {
                g.R(2) = 0;
                g.R(4) = kAsked;
                g.R(5) = kTurnSet;
                g.SetT(true);
                g.Return(20);
                return true;
            }
            g.Write8(kAsked, 0);
        }
        Register(g, kScreen + 0x00, g.Read16(kMode + 0));
        Register(g, kScreen + 0x02, g.Read16(kMode + 2));
        Register(g, kScreen + 0x04, g.Read16(kMode + 4));
        Register(g, kScreen + 0x06, g.Read16(kMode + 6));
        Register(g, kScreen + 0x10, g.Read16(kMode + 8));
        Register(g, kScreen + 0x12, g.Read16(kMode + 10));
        Register(g, kScreen + 0x14, g.Read16(kMode + 12));
        Register(g, kScreen + 0x16, g.Read16(kMode + 14));
        const bool turned = g.Read8(kUprightScreen) != 0;
        g.Write16(0x240C0080, static_cast<uint16_t>(g.Read16(kSprites) | (turned ? 24 : 0))); // (the object DMA's word: kept as it is)
        Register(g, kText + 0x00, g.Read16(kMode));
        const uint32_t kind = kTextKinds + g.Read32(kTextKind) * 8;
        Register(g, kText + 0x02, g.Read16(kind + 0));
        Register(g, kText + 0x04, g.Read16(kind + 2));
        Register(g, kText + 0x06, g.Read16(kind + 4));
        Register(g, kText + 0x08, g.Read16(kind + 6));
        const uint16_t edge = g.Read16(kMode + 6);
        Register(g, kText + 0x0A, edge);
        Register(g, kText + 0x0C, edge >> 8);
        Register(g, kText + 0x12, g.Read16(kMode + 8));
        const uint16_t right = g.Read16(kMode + 10);
        Register(g, kText + 0x14, right);
        Register(g, kText + 0x16, right >> 8);
        const uint16_t top = static_cast<uint16_t>(g.Read16(kMode + 12) + 2);
        Register(g, kText + 0x18, top);
        Register(g, kText + 0x1A, top >> 8);
        const uint16_t bottom = g.Read16(kMode + 14);
        Register(g, kText + 0x1C, bottom);
        Register(g, kText + 0x1E, bottom >> 8);
        Register(g, kText + 0x26, g.Read16(kSprites) & 7);
        const int32_t turnNow = static_cast<int8_t>(g.Read8(kUprightScreen));
        Register(g, kText + 0x28, turnNow ? 3 : 0);
        g.R(0) = Signed(turnNow);
        g.R(1) = kSprites;
        g.R(2) = turnNow ? 3 : 0;
        g.R(3) = kText + 0x26;
        g.R(4) = kText + 0x28;
        g.R(5) = kMode + 8;
        g.R(6) = kMode;
        g.R(7) = kMode;
        g.SetT(turnNow == 0);
        g.Return(200);
        return true;
    }

    // 06005FB8: the whole picture's zoom and its centre, when asked (0x0200DCD0): eight words from 0x0200DCB4, the
    // first of each four with the centre's offset (0x0200DCE2, 0x0200DCE4), ten bits
    bool PictureZoom(Guest &g) {
        constexpr uint32_t kAsked = 0x0200DCD0, kZoom = 0x0200DCB4, kCentreX = 0x0200DCE2, kCentreY = 0x0200DCE4;
        if (g.Read8(kAsked) == 0) {
            g.R(0) = 0;
            g.R(4) = kAsked;
            g.SetT(true);
            g.Return(8);
            return true;
        }
        g.Write8(kAsked, 0);
        Register(g, kScreen + 0x08, Signed(Word(g, kZoom) + Word(g, kCentreX)) & 0x3FF);
        Register(g, kScreen + 0x0A, g.Read16(kZoom + 2));
        Register(g, kScreen + 0x0C, g.Read16(kZoom + 4));
        Register(g, kScreen + 0x0E, g.Read16(kZoom + 6));
        Register(g, kScreen + 0x18, Signed(Word(g, kZoom + 8) + Word(g, kCentreY)) & 0x3FF);
        Register(g, kScreen + 0x1A, g.Read16(kZoom + 10));
        Register(g, kScreen + 0x1C, g.Read16(kZoom + 12));
        Register(g, kScreen + 0x1E, g.Read16(kZoom + 14));
        g.R(0) = Signed(Word(g, kZoom + 14));
        g.R(1) = kScreen + 0x1C;
        g.R(2) = kScreen + 0x1E;
        g.R(3) = kScreen + 0x1A;
        g.R(4) = kZoom + 8;
        g.R(5) = kZoom;
        g.R(6) = 0x3FF;
        g.SetT(false);
        g.Return(60);
        return true;
    }

    // 061388DA: the text layer's colours' place (r4, a byte offset in colour RAM) set, and its palette base with it
    bool TextPalette(Guest &g) {
        constexpr uint32_t kTextColourBase = 0x02078CE4;
        const uint32_t at = g.R(4) + 0x24080000;
        g.Write32(kTextColourBase, at);
        const uint32_t base = (at >> 10) & 0xFF;
        Register(g, kText + 0x24, base);
        g.R(0) = base;
        g.R(2) = kText + 0x24;
        g.R(3) = 0x24080000;
        g.R(4) = at;
        g.R(5) = kTextColourBase;
        g.Return(12);
        return true;
    }

    const Lift kLifts[] = {
        {"ObjectScrolls", 0x0612DDCC, 0x9C, 0xBDC47072323497EDull, ObjectScrolls, true, 0x1003F, nullptr, kHandsOver},
        {"TilemapRegisters", 0x0612E610, 0x404, 0x78CCA79ED47596D7ull, TilemapRegisters, true, 0x1001F, nullptr, kHandsOver},
        {"ScreenRegisters", 0x06005C00, 0x11E, 0x90CC17B7165E52F1ull, ScreenRegisters, true, 0x1001F, nullptr, kHandsOver},
        {"PictureZoom", 0x06005FB8, 0x58, 0xFC8223620916E67Full, PictureZoom, true, 0x1001F, nullptr, kHandsOver},
        {"TextPalette", 0x061388DA, 0x16, 0xFDC3A59BA456D6DEull, TextPalette, true, 0x100F9, nullptr, kHandsOver},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterVideoRegisters() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
