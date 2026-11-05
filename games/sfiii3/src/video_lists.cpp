// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's object lists, written again as C++ (rewrites: runtime/lift.h), the third piece of the native video
// (cps3recomp/docs/NATIVE.md, games/sfiii3/NATIVE.md): the lists' heads (sprite RAM's first 8 KB, what the object DMA
// takes at the vertical blank) handed to the native interface (video/native.h, ListHead) as they are written. With
// CPS3RECOMP_NATIVE_CHECK=1 the board compares, at every object DMA the game starts, the heads it takes with those
// handed over (games/sfiii3/checknative.sh).
//
// How the lists are made. Through the frame the game's drawing code puts each thing's objects in sprite RAM (from
// 04002000: four longs an object, read back and changed in place, so they stay the game's memory) and links a node for
// them into one of 128 priorities (0x06136F28: nodes of six words, the head's three longs, linked at +12; byte +18 says
// the node is in use), in one of two sets of priorities by the frame (0x020700A8). In the vertical blank, after the
// object DMA has taken the last frame's, 0x06136F90 writes the heads in priority order from the highest, the first
// head always an empty one at the screen's place, then pads with empty heads (as many as 350 less the objects listed,
// over 16, and one) and ends them. 0x061367EE writes an empty frame (the first head and 22 empty ones) when a screen
// is cleared, then starts the DMA and waits for it.
#include "lift.h"
#include "machine.h"

#include <iterator>
#include <vector>

namespace cps3recomp {

namespace {
    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }

    constexpr uint32_t kHeads = 0x24000000;      // sprite RAM's first 8 KB: the heads, 16 bytes each (the last long unused)
    constexpr uint32_t kHeadsAsked = 0x020700A9; // byte: the heads to be written this vertical blank
    constexpr uint32_t kSet = 0x020700A8;        // byte: which set of priorities the frame filled (1: kPriorities, 0: kPriorities2)
    constexpr uint32_t kPriorities = 0x0206FC94, kPriorities2 = 0x0206FE94; // longs, 128 each: a priority's first node
    constexpr uint32_t kScreenPlace = 0x02007ED0; // words: the empty heads' block word, the first head's position (ED4, ED6),
                                                  // the pads' (ED8, EDA)
    constexpr uint32_t kMostHeads = 0x1FF, kObjectsAll = 0x15E;
    constexpr uint32_t kDivide = 0x0612D428; // r1 / r0, signed

    // A head written (its six words) and kept for the interface
    void PutHead(Guest &g, uint32_t &at, std::vector<native::ListHead> &heads, const uint16_t (&w)[6]) {
        for (int k = 0; k < 6; k++) {
            g.Write16(at + 2 * k, w[k]);
        }
        at += 16;
        heads.push_back({(static_cast<uint32_t>(w[0]) << 16) | w[1], (static_cast<uint32_t>(w[2]) << 16) | w[3],
                         (static_cast<uint32_t>(w[4]) << 16) | w[5]});
    }
    void EmptyHead(Guest &g, uint32_t &at, std::vector<native::ListHead> &heads, bool first) {
        const uint16_t w[6] = {16, g.Read16(kScreenPlace), g.Read16(kScreenPlace + (first ? 4 : 8)), g.Read16(kScreenPlace + (first ? 6 : 10)), 0, 0};
        PutHead(g, at, heads, w);
    }

    // 06136F90: the frame's list heads written (when asked), in priority order, padded and ended
    bool ListHeads(Guest &g) {
        if (g.Read8(kHeadsAsked) == 0) {
            g.R(0) = 0;
            g.R(3) = kHeadsAsked;
            g.SetT(true);
            g.Return(10);
            return true;
        }
        g.R(15) -= 52; // (pr and 48 bytes: what the division finds)
        std::vector<native::ListHead> heads;
        uint32_t count = 1;
        int32_t objects = 16;
        g.Write8(kHeadsAsked, 0);
        uint32_t at = kHeads;
        EmptyHead(g, at, heads, true);
        const uint32_t priorities = g.Read8(kSet) ? kPriorities : kPriorities2;
        uint64_t cycles = 60;
        bool full = false;
        for (int32_t priority = 127; priority >= 0 && !full; priority--) {
            uint32_t node = g.Read32(priorities + Signed(priority * 4));
            cycles += 12;
            if (node == 0) {
                continue;
            }
            // (the next node's byte +18 is read even past the last: the program ROM's byte 0x12 then ends the walk)
            while (g.Read8(node + 18) != 0) {
                objects += static_cast<int16_t>(g.Read16(node)) & 0x1FF;
                const uint16_t w[6] = {g.Read16(node), g.Read16(node + 2), g.Read16(node + 4), g.Read16(node + 6), g.Read16(node + 8),
                                       g.Read16(node + 10)};
                PutHead(g, at, heads, w);
                node = g.Read32(node + 12);
                cycles += 50;
                if (++count >= kMostHeads) {
                    full = true;
                    break;
                }
            }
        }
        if (!full && objects < static_cast<int32_t>(kObjectsAll)) {
            for (int32_t pad = 0;; pad++) {
                g.R(3) = Signed(objects);
                g.R(1) = Signed(static_cast<int32_t>(kObjectsAll) - objects);
                g.R(0) = 16;
                g.R(2) = kDivide;
                g.Call(kDivide);
                const int32_t pads = static_cast<int32_t>(g.R(0)) + 1;
                g.R(0) = Signed(pads);
                g.R(1) = Signed(pad);
                if (pad >= pads) {
                    break;
                }
                EmptyHead(g, at, heads, false);
                cycles += 90;
            }
        }
        g.Write16(at, 0x8000); // (the end)
        g.Board().NativeLists(std::move(heads));
        g.R(15) += 52;
        g.Return(cycles);
        return true;
    }

    const Lift kLifts[] = {
        {"ListHeads", 0x06136F90, 0x284, 0x06D76DCD2C3326A5ull, ListHeads, true, 0x1007F},
    };

    // 061367EE (hooked: it ends by waiting for the object DMA it starts, which a rewrite cannot): the empty frame's
    // heads, the first and 22 empty ones, handed over as the game's code then writes them
    void EmptyFrame(Guest &g) {
        std::vector<native::ListHead> heads;
        const auto head = [&](bool first) {
            heads.push_back({(16u << 16) | g.Read16(kScreenPlace), (static_cast<uint32_t>(g.Read16(kScreenPlace + (first ? 4 : 8))) << 16) |
                                                                       g.Read16(kScreenPlace + (first ? 6 : 10)),
                             0});
        };
        head(true);
        for (int n = 0; n < 22; n++) {
            head(false);
        }
        g.Board().NativeLists(std::move(heads));
    }
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterVideoLists() {
    RegisterGameLifts(kLifts, std::size(kLifts));
    AddHook("EmptyFrame", 0x061367EE, 0xC0, 0x2A10159006482AFEull, EmptyFrame);
}

} // namespace cps3recomp
