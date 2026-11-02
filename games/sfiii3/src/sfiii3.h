// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's names: the places in main RAM (SH-2 addresses) the port's features use, with what each was found from.
#pragma once

#include <cstdint>

namespace sfiii3 {

// The credits (a byte): 0 at start-up, 1 and 2 after one and two coins (RAM compared before and after coins in
// attract mode); with it held at 1, Start goes to character select with no coin
constexpr uint32_t kCredits = 0x02007CE0;

// The round's clock (a byte, in seconds): 78, 72 and 61 where the screen showed those (RAM at three frames of a
// fight); held at 99 the clock shows 99 (98 for the moment before it is put back)
constexpr uint32_t kClock = 0x02011377;

} // namespace sfiii3
