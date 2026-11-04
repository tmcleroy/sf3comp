// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's choice of a stage, written again as C++ (rewrites: runtime/lift.h). The stage of the next fight is a
// byte (sfiii3.h's kStage, 0x020154F5), numbered as the characters are (each character's stage: Gill 0, Alex 1 ...).
//
// Between two players (the versus game, 0x0201556C 3) the stage is a character's: the one of the side the byte at
// 0x020113DA names (0 player 1, 1 player 2), or of the other side (0x020113DB) if that one is Q, who has no stage of
// its own; and when both chose Q, one at random of the eighteen others (a table at 0x0619542A: 1-14, 16, 17, 19, 20,
// not Gill's, the second Akuma's or Q's). (Against the computer the stage is the opponent's, chosen elsewhere.)
//
// Checked by games/sfiii3/checkstages.sh (versus made-up play): checklifts.sh's runs have no versus game.
//
// Registers: its one caller (0x060A0868) reads r0 (the stage) and r14 after it (tools/liveout.py), so the rest are
// free. It calls the game's dice (0x0611E0EE) through Guest::Call.
#include "lift.h"

#include <iterator>

namespace cps3recomp {

namespace {
    constexpr uint32_t kChosen = 0x02011387;    // bytes: each side's character (sfiii3.h's kChosen)
    constexpr uint32_t kWhoseStage = 0x020113DA; // byte: the side whose character's stage it is
    constexpr uint32_t kOtherSide = 0x020113DB;  // byte: the other side
    constexpr uint32_t kRandomStages = 0x0619542A; // bytes: the stages a Q against Q may be on
    constexpr uint32_t kDice = 0x0611E0EE;      // the game's dice (random_16_com: lifts.cpp's NextRandom), in r0
    constexpr uint8_t kQ = 18;

    int32_t Dice(Guest &g) {
        g.Call(kDice);
        return static_cast<int32_t>(g.R(0));
    }

    // 060A0C52: the versus game's stage
    uint8_t VersusStage(Guest &g) {
        if (g.Read8(kChosen) == kQ && g.Read8(kChosen + 1) == kQ) {
            int32_t at = Dice(g) & 1;
            at += Dice(g) & 1;
            at = static_cast<int16_t>(at) + static_cast<int16_t>(Dice(g));
            return g.Read8(kRandomStages + static_cast<uint32_t>(at));
        }
        const uint32_t whose = kChosen + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kWhoseStage)));
        if (g.Read8(whose) == kQ) {
            return g.Read8(kChosen + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kOtherSide))));
        }
        return g.Read8(whose);
    }

    bool VersusStageLift(Guest &g) {
        g.R(0) = VersusStage(g);
        g.Return(40);
        return true;
    }

    const Lift kLifts[] = {
        {"StageForVersus", 0x060A0C52, 0x5E, 0x0A11240B587B474Eull, VersusStageLift, true, 0x100FE},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterStageSelect() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
