// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer player each frame (0x06006040, called from the fighter's own routine at 0x06119610) and its first steps: what it sees of the fight (0x060060B4 and what it calls), and the routines it calls last.
// The computer's design, its record's fields and the other parts: computer.h.
#include "computer.h"

namespace cps3recomp {

namespace {
    const Lift kLifts[] = {
        {"(none yet)", 0, 0, 0, nullptr}, // (a place holder: a lift at address 0 never runs)
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerEntry() {
    RegisterGameLifts(kLifts, 0);
}

} // namespace cps3recomp
