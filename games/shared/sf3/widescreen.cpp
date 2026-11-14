// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Widescreen in the games built from the shared sources.
//
// New Generation and 2nd Impact have a wide picture of their own: the board shows 495 columns, the game lays its panel
// out to the picture's corners and keeps the camera's ends where the wider picture needs them, as it did on a 16:9
// monitor. The game takes it from a setting (the settings' byte 0x20) that only the Japanese sets' test menu shows: it
// is written here before the board's first frame, as the Board page's settings are, and the game does the rest. The
// game reads it as it starts, so switched in the in-game menu the board starts again (as the test menu's way out does).
//
// 3rd Strike 990608 (whose own wide picture leaves its panel laid out for 384 columns, on the left) has the sources'
// own instead, as the definitive version's (games/sfiii3/src/widescreen.cpp, whose shape it keeps): the picture gets
// 64 columns on each side (the renderer draws the stage's tilemaps on into them), and in a fight the camera is kept 64
// columns further from each end of the stage than the game keeps it, so that the wider picture never shows past the
// stage's ends. So the corners come 64 columns sooner than on the board. With invisible walls the camera's ends are
// left the game's, and the picture is kept on the stage by drawing the world off centre. The stages' things need no
// wider range here: the game's own range_x_check keeps them within 272 columns of their layer's middle, past the wide
// picture's 256.
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
    constexpr int kBoardWidth = 495;      // (the board's wide picture: its timing registers' 0x76 to 0x265)
    constexpr uint32_t kWideSetting = 0x20; // (the settings' byte: 1 the board's wide picture, Facts::boardWide)
    constexpr uint64_t kSettingsRead = 60;  // (the game's RAM copy of its settings made by then: at frame 42, traced)
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
    if (TheFacts().boardWide || !FeatureOn("widescreen") || !FeatureOn("widescreen_walls")) {
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
    if (TheFacts().boardWide) {
        return FeatureOn("widescreen") ? kBoardWidth : Video::kWidth;
    }
    return Video::kWidth + (FeatureOn("widescreen") ? 2 * kMargin : 0);
}

void WidescreenBeforeFrame(Machine &m) {
    const sf3::Facts &f = TheFacts();
    const bool on = FeatureOn("widescreen");
    if (f.boardWide) {
        // The setting written as the board starts, only when it is not what is asked (with widescreen never on, nothing
        // is changed). Once the game has read its settings (frame 42 in both games, after the memory checks that go
        // through all of RAM), what it runs with is its RAM copy: not what is asked (switched in the menu, or a first
        // start, whose settings the game writes its defaults over as it reads them), the setting is written and the
        // board started again, as a training session's start does; one started then is still choosing its way through
        // the start-up, and goes on. (A choice made from the state and the features alone: rollback and run-ahead make
        // the same.)
        const uint8_t wide = on ? 1 : 0;
        if (m.frame == 0 && sf3::SettingsByte(m, kWideSetting) != wide) {
            sf3::SetSettingsByte(m, kWideSetting, wide);
        } else if (m.frame >= kSettingsRead && Ram8(f.boardWide) != wide) {
            sf3::SetSettingsByte(m, kWideSetting, wide);
            m.Reset();
        }
        return;
    }
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
    game.widestPicture = WidestPicture;
    if (TheFacts().boardWide) {
        game.features.push_back({"widescreen", "Display", "Widescreen",
                                 "16:9, the game's own: the board's wide picture, as it was shown on a 16:9 monitor, with the "
                                 "panel out at the corners and the game's own corners for it. Switched here, the game starts again.",
                                 kFeatureFirstPage});
        return;
    }
    game.features.push_back({"widescreen", "Display", "Widescreen",
                             "16:9: 64 more columns of the stage on each side. In a fight the camera stays 64 columns further from "
                             "the stage's ends, so the corners come that much sooner than on the board.",
                             kFeatureFirstPage});
    game.features.push_back({"widescreen_walls", "Display", "Invisible walls",
                             "With widescreen: fights are still shown in 16:9, but played exactly as on the board. The camera goes "
                             "as far as the board's and the corners are the board's; near a stage's end the view stays on the stage, "
                             "so the fight is then to one side of the picture. Nothing without widescreen.",
                             kFeatureEnhanced});
}

void WidescreenState(Archive &ar) {
    ar.Pod(g);
}

} // namespace cps3recomp
