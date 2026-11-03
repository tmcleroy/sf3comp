// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// Spare machines (spare.h). The states are whole (not the in-memory kind that shares character RAM's pages with
// the machine's recent past: a spare may be kept for long, while the port's machine goes on).
#include "spare.h"

#include "recomp.h"
#include "state.h"

#include <algorithm>
#include <cstring>

namespace cps3recomp {

namespace {
    std::vector<uint8_t> g_mine; // the port's machine, while a spare runs in it
    bool g_spareRan = false;
} // namespace

bool SpareRan() {
    return g_spareRan;
}
void ClearSpareRan() {
    g_spareRan = false;
}

void KeepMachine(Machine &machine, std::vector<uint8_t> &state) {
    state.clear();
    Archive ar(state, true);
    machine.Serialize(ar);
}

namespace {
    bool g_inSpare = false;
}
bool InSpare() {
    return g_inSpare;
}

void RunSpare(Machine &machine, Spare &spare, const std::function<void(Machine &)> &before) {
    g_spareRan = true;
    // What a save state does not hold is kept by hand: the picture, the controls, the sound made so far, drawing
    uint16_t held[Machine::kMaxPlayers];
    memcpy(held, machine.players, sizeof(held));
    const size_t sound = machine.audio.size();
    const bool drawing = machine.drawing;
    KeepMachine(machine, g_mine);
    if (spare.state.empty()) {
        machine.Reset();
        spare.ready = false;
        spare.frames = 0;
    } else {
        Archive ar(spare.state, false);
        machine.Serialize(ar);
    }
    StateLoaded();
    memset(machine.players, 0, sizeof(machine.players));
    machine.drawing = false;
    before(machine);
    g_inSpare = true;
    machine.RunFrame();
    g_inSpare = false;
    spare.frames++;
    machine.audio.resize(sound);
    KeepMachine(machine, spare.state);
    {
        Archive ar(g_mine, false);
        machine.Serialize(ar);
    }
    StateLoaded();
    machine.drawing = drawing;
    memcpy(machine.players, held, sizeof(held));
}

void Promote(Machine &machine, Spare &spare) {
    if (!spare.state.empty()) {
        Archive ar(spare.state, false);
        machine.Serialize(ar);
        StateLoaded();
    }
    spare.Clear();
}

} // namespace cps3recomp
