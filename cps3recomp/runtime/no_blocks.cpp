// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
// Used when no code has been generated for the game: everything runs in the interpreter (or in another game's
// registered code, if its program is this one's: a stage game's own build, CMakeLists.txt).
#include "blocks.h"

namespace cps3recomp {
extern const BlockTable g_blockTables[] = {{nullptr, 0, nullptr, 0}};
extern const size_t g_blockTableCount = 0;
} // namespace cps3recomp
