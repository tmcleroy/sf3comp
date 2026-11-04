// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's hit tests, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h): two
// boxes' overlap (0x0608E908). (A hit's damage, its scaling and its taking: lifts.cpp's BaseDamage, ScaleDamage,
// TakeDamage.)
//
// A box is four words (sfiii3.h's kBoxes): its left edge and width across, from the owner's place and turned with its
// facing (a record's byte +0x0A, rl_flag: the box mirrored about the owner), then its bottom and height up. Two boxes
// meet when each one's span across and up overlaps the other's; the tests take one unsigned comparison for each (the
// gap between the near edges, as an unsigned number, below the two sizes together).
#include "fight_core.h"
#include "sfiii3.h"

#include <iterator>

namespace cps3recomp {

bool ModelViewerOn(); // model.cpp

namespace fighting {

namespace {
    constexpr uint32_t kFacing = 0x0A;       // byte: not 0 facing right (rl_flag): boxes mirrored
    constexpr uint32_t kX = 0x64, kY = 0x68; // words: where it is (xyz[0], xyz[1])

    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }

    // Records the port has meet nothing. While the model viewer is on, player 2's fighter, kept unseen beside the
    // character, is hit by none of the character's attacks or projectiles and grabbed by none of its throws: every test
    // with its record says the boxes are apart (those called from 0x0611DBD8 and 0x0608E83E, the character's and its
    // projectiles' attacks, and from 0x0608E55E, a throw's catch: Hugo's 360 seen). Otherwise none: the game's own test.
    bool Unmet(uint32_t one, uint32_t two) {
        return (one == sfiii3::kPlayer[1] || two == sfiii3::kPlayer[1]) && ModelViewerOn();
    }

    // 0608E908 (hit_check_subroutine): whether the box at r6 of the record r4 meets the box at r7 of the record r5;
    // r0 0 if not, else how deep they overlap across (the smaller of the overlap from either side). Leaves the
    // registers as the game's code (its callers read r1, r2 and r5-r7): r6 and r7 past the boxes' first two words, or
    // past their third with the test up made; r1 the first box's left edge in the stage, or the second's owner's height;
    // r2 the sizes across together, or the second box's height; r5 the second record, or the sizes up together.
    bool HitTest(Guest &g) {
        const uint32_t one = g.R(4), two = g.R(5);
        Frame frame(g, 12); // (r11, r13, r14)
        // (across: each box's left edge in the stage)
        int32_t left1 = Word(g, g.R(6));
        const int32_t width1 = Word(g, g.R(6) + 2);
        g.R(6) += 4;
        if (g.Read8(one + kFacing) != 0) {
            left1 = -left1 - width1;
        }
        left1 += Word(g, one + kX);
        int32_t left2 = Word(g, g.R(7));
        const int32_t width2 = Word(g, g.R(7) + 2);
        g.R(7) += 4;
        if (g.Read8(two + kFacing) != 0) {
            left2 = -left2 - width2;
        }
        left2 += Word(g, two + kX);
        // (the gap from box 1's left edge to box 2's right one, against the two widths)
        const int32_t across = left2 + (width2 - left1);
        const int32_t widths = width2 + width1;
        g.R(1) = Signed(left1);
        g.R(3) = Extended(across);
        g.R(2) = Extended(widths);
        g.R(4) = one;
        g.R(5) = two;
        // (the model viewer's unseen player 2 is met by nothing: the boxes said to be apart across, as any two are)
        if (Extended(across) >= Extended(widths) || Unmet(one, two)) {
            g.SetT(true);
            g.R(0) = 0;
            g.Return(30);
            return true;
        }
        // (up: the same with the bottoms and heights)
        const int32_t bottom1 = Word(g, g.R(6)), height1 = Word(g, g.R(6) + 2);
        g.R(6) += 2;
        const int32_t bottom2 = Word(g, g.R(7)), height2 = Word(g, g.R(7) + 2);
        g.R(7) += 2;
        const int32_t up = Word(g, one + kY) + bottom1 - Word(g, two + kY) - bottom2 + height1;
        const int32_t heights = height1 + height2;
        g.R(1) = Extended(Word(g, two + kY));
        g.R(2) = Extended(height2);
        g.R(3) = Extended(bottom2);
        g.R(4) = Extended(up);
        g.R(5) = Extended(heights);
        if (Extended(up) >= Extended(heights)) {
            g.SetT(true);
            g.R(0) = 0;
            g.Return(40);
            return true;
        }
        // (how deep: the overlap from the nearer side)
        int32_t depth = static_cast<int16_t>(across);
        const int32_t other = static_cast<int16_t>(widths) - depth;
        g.R(2) = Signed(other);
        g.R(4) = Extended(across);
        g.SetT(depth > other);
        if (depth > other) {
            depth = widths - across;
            g.R(2) = Signed(depth);
        }
        g.R(0) = Extended(depth);
        g.Return(50);
        return true;
    }

    // (free: r3, r4 and T, which no caller reads: liveout.py)
    const Lift kLifts[] = {
        {"HitTest", 0x0608E908, 0x82, 0x4B446CC884A7C1DCull, HitTest, true, 0x10018},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightHits() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
