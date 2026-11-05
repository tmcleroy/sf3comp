// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's character RAM, the textures (rewrites: runtime/lift.h), the fourth piece of the native video
// (cps3recomp/docs/NATIVE.md, games/sfiii3/NATIVE.md). What the character DMA brings in is handed over by the DMA's
// start (video_palette.cpp, 0x06138168 with r4 1: the list's transfers, and the list itself as the game writes it); the
// only other bytes the game's code writes to character RAM are a cleared area (0x06136782, at a screen's set-up), handed
// over here. The game never reads character RAM. With CPS3RECOMP_NATIVE_CHECK=1 the board compares character RAM with
// the native textures every frame (games/sfiii3/checknative.sh).
#include "lift.h"
#include "machine.h"

#include <iterator>

namespace cps3recomp {

namespace {
    constexpr uint32_t kCharWindow = 0x24100000; // the CPU's window on character RAM
    constexpr uint32_t kCharBank = 0x02079C96;   // word: the window's bank now
    constexpr uint32_t kTakeArea = 0x061379EC;   // (r4 1, r5 0: an area of character RAM for the lists; r0 its page)
    constexpr uint32_t kAreaPlace = 0x06137980;  // r4 a page (from 1): its place in the window (4 KB pages)

    // 06136782: an area of a page taken for the character DMA's lists (kTakeArea with 1, 0) and its 4 KB cleared
    bool ClearListArea(Guest &g) {
        g.R(15) -= 12; // (pr and 8 bytes)
        g.R(5) = 0;
        g.R(4) = 1;
        g.Call(kTakeArea);
        g.R(4) = g.R(0);
        g.Call(kAreaPlace);
        const uint32_t at = g.R(0) + kCharWindow;
        const uint32_t bank = (static_cast<uint32_t>(g.Read16(kCharBank)) & 15 & 7) << 20;
        for (uint32_t k = 0; k < 0x800; k++) {
            g.Write16(at + k * 2, 0);
            g.Board().NativeCharacterWrite(bank | ((at + k * 2) & 0xFFFFE), 0, 2);
        }
        g.R(15) += 12;
        g.R(0) = at;
        g.R(1) = 0x800;
        g.R(2) = 0x800;
        g.R(3) = 0x800;
        g.SetT(true);
        g.Return(0x800 * 14 + 60);
        return true;
    }

    const Lift kLifts[] = {
        {"ClearListArea", 0x06136782, 0x6A, 0x64F2ABB38AEAC94Full, ClearListArea, true, 0x100FF},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterVideoTextures() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
