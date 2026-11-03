// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player (the code that plays a fighter whose record's byte 3 is 0): what is known of it, for
// the rewrites of its routines (computer_*.cpp, one file a part). Names in their comments are the game's own source's,
// as the PS2 version's decompilation has them (crowded-street/3s-decomp: names only, it is AGPL-3.0, none of its code
// is here). The fields each part uses are named in its own file.
//
// Found by comparing the routines called in a training session with player 2 played by the game against one with it
// standing (CPS3RECOMP_CALL_GRAPH): 0x06006040, called once a frame for the fighter from 0x06119610, is the computer;
// about 110 routines under it, most of them between 0x06006000 and 0x06014000. Its parts:
//   computer_entry.cpp    0x06006040 itself, 0x060060B4 and what it calls (0x060061D0, 0x0600643C, 0x0600653C,
//                         0x06006610, 0x06006C72, 0x06006CE0, 0x060083A0, 0x060084A4, 0x060088D4, 0x0600FC20,
//                         0x0600FC6A), and 0x0600899A, 0x06008A08 after it
//   computer_tactics.cpp  the modes 2 and 7-13 (0x06006C22, 0x060066E8 ... 0x06008126) and their steps, the plan's
//                         choice (0x060104B4), the answer's (0x06010DEA) and the reaction's (0x060109AC), the levels,
//                         and steps of the plans the moves left (0x06009100-0x0600B6F6: EM_Term, SA_Term, Jump ...)
//   computer_answers.cpp  0x06012588 (Ck_Passive_Term) and what it calls: the answers to the other (the three chains,
//                         eight dispatchers, twelve tests, per-character lists)
//   computer_moves.cpp    0x06006C22's 0x06066874 and what it calls
#pragma once

#include "lift.h"
#include "sfiii3.h"

#include <cstdint>
