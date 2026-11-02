// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "soundtrack.h"

#include "state.h"

#include <cstdio>
#include <cstdlib>

namespace cps3recomp {

void Soundtrack::Follow(int32_t song, bool started, uint64_t frame) {
    static const bool log = getenv("CPS3RECOMP_SOUNDTRACK_LOG") != nullptr;
    state.boardFrames++;
    if (song != state.board || (started && song >= 0 && state.boardFrames > 8)) {
        if (log) {
            fprintf(stderr, "soundtrack: frame %llu %s %d%s\n", static_cast<unsigned long long>(frame), song < 0 ? "music stops" : "music",
                    song < 0 ? state.board : song, song >= 0 && Replaced(static_cast<uint16_t>(song)) ? " (file)" : "");
        }
        state.board = song;
        state.boardFrames = 0;
        if (song >= 0 && Replaced(static_cast<uint16_t>(song))) {
            state.song = song;
            state.serial++;
            state.frames = 0;
            state.level = state.target = kFull;
            state.step = 0;
        } else {
            state.song = -1;
        }
        return;
    }
    if (state.song >= 0) {
        state.frames++;
    }
}

void Soundtrack::Serialize(Archive &ar) {
    ar.Mark(0x534F4E47);
    ar.Pod(state);
}

} // namespace cps3recomp
