// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Training mode for the Street Fighter III games built from the shared sources (training.cpp), for base.cpp to put
// round the frame.
#pragma once

namespace cps3recomp {

class Archive;
class Machine;
struct Game;

void RegisterTraining(Game &game);
void TrainingBeforeFrame(Machine &m); // (after the cheats: it reads the health they left)
void TrainingAfterFrame(Machine &m);
void TrainingState(Archive &ar);

} // namespace cps3recomp
