// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's choice of a stage, written again as C++ (rewrites: runtime/lift.h). The stage of the next fight is a
// byte (sfiii3.h's kStage, 0x020154F5), numbered as the characters are (each character's stage: Gill 0, Alex 1 ...).
//
// Between two players (the versus game, 0x0201556C 3) the stage is a character's: the one of the side the byte at
// 0x020113DA names (0 player 1, 1 player 2), or of the other side (0x020113DB) if that one is Q, who has no stage of
// its own; and when both chose Q, one at random of the eighteen others (a table at 0x0619542A: 1-14, 16, 17, 19, 20,
// not Gill's, the second Akuma's or Q's). Against the computer (an arcade game's next opponent, 0x060A27DC) the stage
// is the opponent's, Q's the one he is met on.
//
// Checked by games/sfiii3/checkstages.sh (versus made-up play: checklifts.sh's runs have no versus game) and
// checklifts.sh (its one-player games, for the opponent's).
//
// Registers: the versus stage's one caller (0x060A0868) reads r0 (the stage) and r14 after it (tools/liveout.py), so the
// rest are free; the opponent's callers read none of r0-r7 or T. They call the game's routines (the dice, 0x0611E0EE;
// the opponent's colour and the fight's settings) through Guest::Call.
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

    // --- against the computer ---
    constexpr uint32_t kTheirSide = 0x02015503; // byte: the computer's side (0 player 1, 1 player 2)
    constexpr uint32_t kNext = 0x02015504;      // byte: the next opponent's character
    constexpr uint32_t kQsStage = 0x02016D81;   // byte: the stage Q is met on (his own: the last fight's)
    constexpr uint32_t kStageWord = 0x020154F4; // word: the next fight's stage (its low byte sfiii3.h's kStage)
    constexpr uint32_t kStageHead = 0x02026BAC; // the stage's head (bg_w: stage_camera.cpp); +4 its number, +5 its part
    constexpr uint32_t kFightNumber = 0x02015500; // byte: which of the arcade's fights it is
    constexpr uint32_t kSideFlags[2] = {0x0201581F, 0x02015821}; // bytes by side, cleared for a new opponent
    constexpr uint32_t kColourChosen = 0x0201138B; // bytes by side: the colour (version) of each side's character
    constexpr uint32_t kColourByFight = 0x020156C0; // bytes by fight: the opponent's colour kept for it
    constexpr uint32_t kColourFor = 0x060A2846, kAfter = 0x060A28C4, kSetSide = 0x060A28FE; // (the game's: the
                                                // opponent's colour; then two more of the next fight's settings)
    constexpr uint32_t kSetSideWord = 0x0201575A;  // word: passed on to kSetSide (made by kAfter)

    // 060A27DC: the next opponent made the computer's side's character, and the next fight's stage its stage (Q, who has
    // none, on the one he is met on); then its colour, and the next fight's other settings (the game's)
    void OpponentsStage(Guest &g) {
        const auto side = [&]() { return static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kTheirSide))); };
        for (const uint32_t at : kSideFlags) {
            g.Write8(at + side(), 0);
        }
        g.Write8(kChosen + side(), g.Read8(kNext));
        int8_t stage;
        if (g.Read8(kNext) == kQ) {
            g.Write16(kStageWord, g.Read8(kQsStage));
            stage = static_cast<int8_t>(g.Read8(kQsStage));
        } else {
            g.Write16(kStageWord, static_cast<uint16_t>(static_cast<int8_t>(g.Read8(kNext))));
            stage = static_cast<int8_t>(g.Read8(kStageWord + 1));
        }
        g.Write8(kStageHead + 4, static_cast<uint8_t>(stage));
        g.Write8(kStageHead + 5, 0);
        const uint32_t colourAt = kColourChosen + side();
        const uint32_t keptAt = kColourByFight + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kFightNumber)));
        g.Call(kColourFor);
        g.Write8(keptAt, static_cast<uint8_t>(g.R(0)));
        g.Write8(colourAt, static_cast<uint8_t>(g.R(0)));
        g.Call(kAfter);
        g.R(4) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kTheirSide)));
        g.R(5) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kSetSideWord)));
        g.Call(kSetSide);
    }
    bool OpponentsStageLift(Guest &g) {
        OpponentsStage(g);
        g.Return(80);
        return true;
    }

    const Lift kLifts[] = {
        {"StageForVersus", 0x060A0C52, 0x5E, 0x0A11240B587B474Eull, VersusStageLift, true, 0x100FE},
        {"StageForOpponent", 0x060A27DC, 0x6A, 0x183F21927A11D7FCull, OpponentsStageLift, true, 0x100FF},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterStageSelect() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
