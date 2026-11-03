// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer's choices (0x06008BF0 and what it calls, but for the scripts: computer_scripts.cpp).
// The computer's design, its record's fields and the other parts: computer.h.
#include "computer.h"

namespace cps3recomp {

namespace {
    const Lift kLifts[] = {
        {"(none yet)", 0, 0, 0, nullptr}, // (a place holder: a lift at address 0 never runs)
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerTactics() {
    RegisterGameLifts(kLifts, 0);
}

} // namespace cps3recomp
