// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); its shape is cps2recomp's runtime/spare.h.
//
// Machines kept as save states ("spares") and run beside the port's own, for a game's modes that prepare more than
// one game at once (the next fight made ready while the last one ends): a spare is loaded into the port's machine,
// run a frame and put away again, within the port's frame.
#pragma once

#include "system.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace cps3recomp {

// A machine kept as a save state
struct Spare {
    std::vector<uint8_t> state; // (empty: not yet switched on)
    bool ready = false;         // what it was being run for has come (its round is on, say)
    int32_t frames = 0;         // frames run on the way there
    void Clear() {
        state.clear();
        ready = false;
        frames = 0;
    }
};

// The port's machine as it is now, kept as a spare's state is (to begin spares from: a character choice, say)
void KeepMachine(Machine &machine, std::vector<uint8_t> &state);

// Runs the spare for a frame in the port's machine, which is left as it was (its picture, sound, controls and
// switches too). `before` is called with the spare loaded, before its frame. Not drawn.
void RunSpare(Machine &machine, Spare &spare, const std::function<void(Machine &)> &before);
// ... up to `frames` frames, the spare loaded and put away once for them all (a state is the machine's 8 MB of character
// RAM and more: kept and put back four times a frame, that was most of a spare's cost), stopping once it is ready
void RunSpareFrames(Machine &machine, Spare &spare, int frames, const std::function<void(Machine &)> &before);
// Frames run on spares since the program started (a count, for the checks)
uint64_t SpareFramesRun();
// Whether a spare's frame is running now (the game's sources' hooks run in it too)
bool InSpare();
// Whether a spare has run since this was last cleared (a frame with a spare in it is not run ahead)
bool SpareRan();
void ClearSpareRan();
// The spare's machine becomes the port's (and the spare is cleared)
void Promote(Machine &machine, Spare &spare);

} // namespace cps3recomp
