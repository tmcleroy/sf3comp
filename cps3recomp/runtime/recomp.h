// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
// The runtime that runs the game's translated SH-2 code in place of the interpreter (recomp.cpp).
#pragma once

#include <string>

namespace cps3recomp {
namespace machine {
    class Machine;
}

enum class Mode {
    // Free running: blocks run to their end whatever the time slice, time is nominal cycles per instruction
    Native, // the translated code (block_native.h); the interpreter for code the build lacks
    Free,   // the interpreter runs every block the same way: the exact reference for Native
};

struct Options {
    Mode mode = Mode::Native;
    std::string recordPath; // block database to add newly reached blocks to (tools/recomp.py reads it)
    bool stats = false;
    // Check (Native): every `checkEvery`th dispatch runs the interpreter (Free) and the translation from the same
    // state and compares the registers, the cycles, main RAM and the cache RAM they leave. Dispatches that touch
    // the board's hardware cannot be run twice and are not compared (counted).
    unsigned checkEvery = 0;
};

// (the board itself: runtime/backend.h calls it Machine; the port's runtime/system.h has a Machine of its own)
void Attach(machine::Machine &machine, const Options &options);
// Prints what was asked for (stats, the check's results), writes the block database; false if the check found
// differences
bool Finish();

// A save state was loaded: what the dispatcher keeps about where the CPU was is forgotten
void StateLoaded();
// Run-ahead and rollback: the same, kept and put back in place of forgetting it
void KeepDispatcher();
void PutDispatcherBack();

} // namespace cps3recomp
