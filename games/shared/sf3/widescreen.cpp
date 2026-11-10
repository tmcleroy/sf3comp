// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// New Generation and 2nd Impact in widescreen, as 3rd Strike is (games/sfiii3/src/widescreen.cpp, whose shape this
// keeps): the picture gets 64 columns on each side (the renderer draws the stage's tilemaps on into them), and in a
// fight the camera is kept 64 columns further from each end of the stage than the game keeps it, so that the wider
// picture never shows past the stage's ends. So the corners come 64 columns sooner than on the board. With invisible
// walls the camera's ends are left the game's, and the picture is kept on the stage by drawing the world off centre.
// The stages' things need no wider range here: these games' own range_x_check (0x060796CA, 0x0609FABC) keeps them
// within 272 columns of their layer's middle, past the wide picture's 256.
// On a few stages a far layer ends short of the wider picture at the stage's ends (its art is only as wide as the
// board's picture needs): there the renderer shows the layer's own end again, mirrored (Machine::SetWideFill).
#include "facts.h"
#include "widescreen.h"

#include "game.h"
#include "state.h"
#include "system.h"

#include <algorithm>

namespace cps3recomp {

namespace {
    using sf3::TheFacts;

    constexpr int kMargin = 64;
    struct State {
        int16_t left = 0, right = 0;       // the stage's own camera limits, as the game set them
        int16_t setLeft = 0, setRight = 0; // what they were made
    } g;

    int16_t Word(uint32_t address) {
        return static_cast<int16_t>(Ram16(address));
    }
    // (each end is kept twice, a word apart, as 3rd Strike's are: both are set)
    void SetEnd(uint32_t address, int16_t value) {
        SetRam16(address, static_cast<uint16_t>(value));
        SetRam16(address + 2, static_cast<uint16_t>(value));
    }
    bool Fighting() {
        const sf3::Facts &f = TheFacts();
        return Ram8(f.locked[0]) == 0xFF || Ram8(f.locked[1]) == 0xFF;
    }
} // namespace

int WorldShift() {
    if (!FeatureOn("widescreen") || !FeatureOn("widescreen_walls")) {
        return 0;
    }
    const sf3::Facts &f = TheFacts();
    const int camera = Word(f.cameraX), left = Word(f.cameraLeft), right = Word(f.cameraRight);
    if (!Fighting() || right - left <= 2 * kMargin) {
        return 0;
    }
    const int middle = std::clamp(camera, left + kMargin, right - kMargin);
    return middle - camera;
}

int WidestPicture() {
    return Video::kWidth + (FeatureOn("widescreen") ? 2 * kMargin : 0);
}

void WidescreenBeforeFrame(Machine &m) {
    const sf3::Facts &f = TheFacts();
    const bool on = FeatureOn("widescreen");
    const bool fighting = Fighting() && Ram8(f.matchState) == 2;
    m.SetWideMargin(on ? kMargin : 0);
    m.SetWideFill(on); // (the far layers that end short of the wider picture at a stage's ends)
    m.worldShiftNow = WorldShift;
    const bool walls = FeatureOn("widescreen_walls");
    if (walls && fighting && g.setLeft && Word(f.cameraLeft) == g.setLeft && Word(f.cameraRight) == g.setRight) {
        // (switched on in a fight: the game's own limits back)
        SetEnd(f.cameraLeft, g.left);
        SetEnd(f.cameraRight, g.right);
    }
    if (!on || !fighting || walls) {
        g = {};
        return;
    }
    // (the game sets its limits at a stage's start, and may again: what is not what was made of them is the game's)
    const int16_t left = Word(f.cameraLeft), right = Word(f.cameraRight);
    if (left != g.setLeft || right != g.setRight) {
        g.left = left;
        g.right = right;
    }
    if (g.right - g.left <= 2 * kMargin) {
        return; // (a stage too narrow to narrow)
    }
    g.setLeft = static_cast<int16_t>(g.left + kMargin);
    g.setRight = static_cast<int16_t>(g.right - kMargin);
    SetEnd(f.cameraLeft, g.setLeft);
    SetEnd(f.cameraRight, g.setRight);
}

void RegisterWidescreen(Game &game) {
    game.features.push_back({"widescreen", "Display", "Widescreen",
                             "16:9: 64 more columns of the stage on each side. In a fight the camera stays 64 columns further from "
                             "the stage's ends, so the corners come that much sooner than on the board.",
                             kFeatureFirstPage});
    game.features.push_back({"widescreen_walls", "Display", "Invisible walls",
                             "With widescreen: fights are still shown in 16:9, but played exactly as on the board. The camera goes "
                             "as far as the board's and the corners are the board's; near a stage's end the view stays on the stage, "
                             "so the fight is then to one side of the picture. Nothing without widescreen."});
    game.widestPicture = WidestPicture;
}

void WidescreenState(Archive &ar) {
    ar.Pod(g);
}

} // namespace cps3recomp
