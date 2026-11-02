// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
// The machine the game's code runs on (machine/). Everything else in the runtime and all generated code names
// the CPU and the machine through this header.
#pragma once

#include "../machine/sh2.h"

namespace cps3recomp {
using SH2 = machine::SH2;
using Machine = machine::Machine;
} // namespace cps3recomp

// Counts everything that can change what a waiting CPU would see
#define CPS3RECOMP_ACTIVITY ::cps3recomp::machine::g_activity
// A write went to main RAM at `p`: translated code may lie there
#define CPS3RECOMP_NOTE_WRITE(p) ::cps3recomp::machine::NoteWrite(p)
#define CPS3RECOMP_USING_TYPES using namespace ::cps3recomp::machine::types;
#define CPS3RECOMP_CODE_BASE ::cps3recomp::machine::g_codeBase
#define CPS3RECOMP_CODE_MAP ::cps3recomp::machine::g_codeMap
#define CPS3RECOMP_CODE_SIZE ::cps3recomp::machine::g_codeSize
#define CPS3RECOMP_CODE_EPOCH ::cps3recomp::machine::g_codeEpoch
#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
