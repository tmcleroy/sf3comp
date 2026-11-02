// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// The camera's zoom: the picture shown larger about one player's fighter, following it, at the sizes the model
// viewer draws a character at from its own to twice it (twice: the top of a tall one goes off the screen). A
// press for a player steps its size up, from 2x back to none; a press for the other player looks at that one
// instead, at the same size. The fighter is always in the middle (past the picture's edges is black); the size,
// and the place looked at when it changes fighter, go smoothly from one to the next, and what was
// chosen is said on the picture for a few seconds. Only where the game's sources say where a fighter is
// (runtime/game.h, Game::focus), and only while there is one: elsewhere the picture goes back to its own size.
// It is the window's alone: the game, its save states and its screenshots know nothing of it.
//
// Or both fighters at once: the camera is as close as keeps both in the picture (2x at most, the whole picture at
// least), between them, and inside the picture's edges: tight when they are close, wide when they are apart.
#pragma once

#include "present.h"

namespace port {

class Zoom {
public:
    static constexpr float kSizes[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
    static constexpr int kSizeCount = static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0]));
    // Whether the game can be zoomed at all (not in online play: Forbid)
    static bool Possible();
    // Online play (issue #196): no zoom while it lasts, for either player; the whole picture shown again at once
    void Forbid(bool forbid = true);

    static constexpr int kBoth = 2; // (the "player" the camera follows when it keeps both in the picture)
    // A press for player `player` (0 or 1)
    void Press(int player);
    // A press for both fighters at once: on, or off when it is on
    void Both();
    // The in-game menu's Zoom row (the session feature zoom, main.cpp): 0 off, 1 or 2 that player's fighter (at the size
    // it was at, 1.5x if none), 3 both; and what is chosen now, by the same numbers
    void Choose(int choice);
    int Choice() const {
        return m_size == 0 ? 0 : m_player == kBoth ? 3 : m_player + 1;
    }
    // The camera moved off its fighter by so many steps across and down (the d-pad or the arrows, while zooming), to
    // frame it as the player wants; kept until the zoom is off or looks at the other fighter
    void Pan(int across, int down);
    // Straight to a size (0: none) about a player (kBoth: both, whatever the size), with no easing (--zoom)
    void Set(int size, int player);
    // Each picture shown, `width` x `height`: the size and the place looked at a step nearer what is wanted
    void Follow(int width, int height);
    // The part of the picture shown, in its pixels; false when it is all of it
    bool View(int width, int height, SDL_FRect &view) const;
    // What was last chosen ("ZOOM 1.5x  PLAYER 1"), for a few seconds after it was ("": nothing to say)
    const char *Label() const {
        return m_labelTicks > 0 ? m_label : "";
    }

private:
    void Said();
    int m_size = 0, m_player = 0;
    float m_shown = 1.0f, m_x = 0, m_y = 0; // as shown now
    float m_offX = 0, m_offY = 0;           // (the camera's way off its fighter, after it changed fighter)
    float m_panX = 0, m_panY = 0;           // (... and where the player has moved it from its fighter)
    float m_bothSize = 1.0f;                // (keeping both in the picture: the size it is going to)
    int m_followed = 0;
    bool m_wasSeen = false;
    bool m_settled = false;                 // (the place is known: eased to from here on)
    bool m_jump = false;                    // (the next Follow goes straight there)
    int m_labelTicks = 0;
    char m_label[40] = {};
};

} // namespace port
