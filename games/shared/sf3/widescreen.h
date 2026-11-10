// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Widescreen in the games built from the shared sources (widescreen.cpp).
#pragma once

namespace cps3recomp {

class Archive;
class Machine;
struct Game;

void RegisterWidescreen(Game &game);
void WidescreenBeforeFrame(Machine &m);
void WidescreenState(Archive &ar);
int WorldShift(); // how far the world is drawn off centre (invisible walls), in columns

} // namespace cps3recomp
