// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer's scripts: their interpreter (0x06012588, 0x060125B4) and its instructions.
// The computer's design, its record's fields and the other parts: computer.h.
#include "computer.h"

namespace cps3recomp {

namespace {
    const Lift kLifts[] = {
        {"(none yet)", 0, 0, 0, nullptr}, // (a place holder: a lift at address 0 never runs)
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerScripts() {
    RegisterGameLifts(kLifts, 0);
}

} // namespace cps3recomp
