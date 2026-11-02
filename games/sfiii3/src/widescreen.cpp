// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike in widescreen: the picture gets 64 columns on each side (the renderer draws the stage's tilemaps on into
// them), and in a fight the camera is kept 64 columns further from each end of the stage than the game keeps it, so
// that the wider picture never shows past the stage's ends. So the corners come 64 columns sooner than on the board.
#include "game.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

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

void WidescreenBeforeFrame(Machine &m, bool fighting) {
    const bool on = FeatureOn("widescreen");
    m.SetWideMargin(on ? kMargin : 0);
    if (!on || !fighting) {
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

void WidescreenState(Archive &ar) {
    ar.Pod(g);
}

} // namespace cps3recomp
