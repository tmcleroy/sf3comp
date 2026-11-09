// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike in widescreen: the picture gets 64 columns on each side (the renderer draws the stage's tilemaps on into
// them), and in a fight the camera is kept 64 columns further from each end of the stage than the game keeps it, so
// that the wider picture never shows past the stage's ends. So the corners come 64 columns sooner than on the board.
#include "game.h"
#include "lift.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <iterator>

namespace cps3recomp {

namespace {
    constexpr int kMargin = 64;
    struct State {
        int16_t left = 0, right = 0;     // the stage's own camera limits, as the game set them
        int16_t setLeft = 0, setRight = 0; // what they were made
    } g;

    int16_t Word(uint32_t address) {
        return static_cast<int16_t>(Ram16(address));
    }
    void SetWord(uint32_t address, int16_t value) {
        SetRam16(address, static_cast<uint16_t>(value));
    }
} // namespace

// Invisible walls (widescreen_walls): the camera's limits left as the game sets them, so play is the board's; the picture
// is kept on the stage near its ends by drawing the world off centre (Machine::worldShiftNow): its middle is the
// camera's, held between the limits brought in by the margin
int WorldShift() {
    if (!FeatureOn("widescreen") || !FeatureOn("widescreen_walls")) {
        return 0;
    }
    const bool fighting = (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF);
    const int camera = Word(sfiii3::kCameraX), left = Word(sfiii3::kCameraLeft[0]), right = Word(sfiii3::kCameraRight[0]);
    Machine *m = TheMachine();
    if (!fighting || right - left <= 2 * kMargin || (m && m->StageBoard())) {
        return 0;
    }
    const int middle = std::clamp(camera, left + kMargin, right - kMargin);
    return middle - camera;
}

// The picture's columns, as the window is shaped for it (Game::widestPicture, as the CPS-2 ports have it): at start-up,
// and again whenever widescreen is switched (in the menu). Until 2026-11-08 the game gave none, so the window was made
// 4:3 at start-up and kept so: a widescreen picture shown in it letterboxed (the play-test: "when switching to
// widescreen in the in game menu it shrinks the display and doesnt fill the full wide area")
int WidestPicture() {
    return Video::kWidth + (FeatureOn("widescreen") ? 2 * kMargin : 0);
}

void WidescreenBeforeFrame(Machine &m, bool fighting) {
    const bool on = FeatureOn("widescreen");
    m.SetWideMargin(on ? kMargin : 0);
    m.worldShiftNow = WorldShift;
    // (invisible walls: the limits as the game sets them; with another game's stage, which follows the narrowed limits,
    // not offered)
    const bool walls = FeatureOn("widescreen_walls") && !m.StageBoard();
    if (walls && fighting && g.setLeft && Word(sfiii3::kCameraLeft[0]) == g.setLeft && Word(sfiii3::kCameraRight[0]) == g.setRight) {
        // (switched on in a fight: the game's own limits back)
        for (const uint32_t at : sfiii3::kCameraLeft) {
            SetWord(at, g.left);
        }
        for (const uint32_t at : sfiii3::kCameraRight) {
            SetWord(at, g.right);
        }
    }
    if (!on || !fighting || walls) {
        g = {};
        return;
    }
    // (the game sets its limits at a stage's start, and may again: what is not what was made of them is the game's)
    const int16_t left = Word(sfiii3::kCameraLeft[0]), right = Word(sfiii3::kCameraRight[0]);
    if (left != g.setLeft || right != g.setRight) {
        g.left = left;
        g.right = right;
    }
    if (g.right - g.left <= 2 * kMargin) {
        return; // (a stage too narrow to narrow)
    }
    g.setLeft = static_cast<int16_t>(g.left + kMargin);
    g.setRight = static_cast<int16_t>(g.right - kMargin);
    for (const uint32_t at : sfiii3::kCameraLeft) {
        SetWord(at, g.setLeft);
    }
    for (const uint32_t at : sfiii3::kCameraRight) {
        SetWord(at, g.setRight);
    }
}

// The stage's objects in widescreen (the play-test of 2026-11-07: "stage elements that would be offscreen on 4:3 pop in at
// the edges of 16:9"). The stages' things (the effects' records: crates, signs, sawhorses, people) are drawn through the
// game's display entries (disp_pos_trans_entry_r and its kin, 0x060B65E0-0x060B6B00, as the PlayStation 2 version's
// decompilation names them), which push an object for drawing only while range_x_check (0x060B6404) says its place is
// within 193 columns of its layer's middle (the layer's place, or its scripted place in a scripted move): the board's 384
// and a column. So in the wider picture each came on whole, 64 columns inside its edge. With widescreen on, for those
// entries only, the range is wider by the margin and a half-margin more (a thing's place is its middle: one up to that far
// past the picture's edge slides in from outside it). What else asks range_x_check, and range_x_check3 (0x060B64FC:
// projectiles, the characters' things), is left as it is: those decide play, not only what is drawn. Off, the game's own
// routine runs. New Generation's and 2nd Impact's own range_x_check (0x060796CA, 0x0609FABC) keep things within 272
// columns already: wide enough. (games/sfiii3/checkpopin.sh)
namespace {
    constexpr uint32_t kStageHead = 0x02026BAC; // (bg_w: stage_camera.cpp)
    constexpr uint32_t kEntriesFrom = 0x060B65E0, kEntriesTo = 0x060B6B00;
    bool RangeAcross(Guest &g) {
        const uint32_t from = g.PR();
        static const bool log = getenv("SFIII3_WIDE_LOG") != nullptr; // (debugging aid: every ask, its caller and places)
        if (log) {
            const uint32_t w = g.R(4);
            const int fam = static_cast<int16_t>(g.Read16(w + 0x26E));
            fprintf(stderr, "range: from %08X work %08X family %d place %d middle %d\n", from, w, fam, static_cast<int16_t>(g.Read16(w + 0x64)),
                    static_cast<int16_t>(g.Read16(kStageHead + 0x54 + static_cast<uint32_t>((fam - 1) * 0x90) + 0x20)));
        }
        if (!FeatureOn("widescreen") || from < kEntriesFrom || from >= kEntriesTo) {
            return false; // (the game's own)
        }
        const uint32_t work = g.R(4);
        const bool chase = (g.Read8(kStageHead + 0x25) & 0x0F) != 0;
        const int family = static_cast<int16_t>(g.Read16(work + 0x26E));
        const uint32_t layer = kStageHead + 0x54 + static_cast<uint32_t>((family - 1) * 0x90);
        const int middle = static_cast<int16_t>(g.Read16(layer + (chase ? 0x58 : 0x20)));
        const int place = static_cast<int16_t>(g.Read16(work + 0x64));
        const int reach = 193 + kMargin + kMargin / 2;
        const int left = static_cast<int16_t>(middle - reach), right = static_cast<int16_t>(middle + reach);
        g.R(0) = place >= left && place <= right ? 1 : 0;
        g.Return(30);
        return true;
    }
    const Lift kWideLifts[] = {
        {"WideRangeAcross", 0x060B6404, 0x6C, 0xF1AF3CD8C070CEC7ull, RangeAcross, false, 0, nullptr, false, Lift::kFirstBoard | Lift::kStageBoards},
    };
} // namespace

void RegisterWidescreen() {
    RegisterLifts(kWideLifts, std::size(kWideLifts));
}

void WidescreenState(Archive &ar) {
    ar.Pod(g);
}

} // namespace cps3recomp
