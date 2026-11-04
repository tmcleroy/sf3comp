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
// is the opponent's, Q's the one he is met on; and with the opponent its super art and its colour, by the routines
// character select chooses a colour by too (the button it was chosen with).
//
// Checked by games/sfiii3/checkstages.sh (versus made-up play: checklifts.sh's runs have no versus game) and
// checklifts.sh (its one-player games, for the opponent's).
//
// Registers: the versus stage's one caller (0x060A0868) reads r0 (the stage) and r14 after it (tools/liveout.py), so the
// rest are free; the opponent's callers read none of r0-r7 or T, the art's r0 (the art), the colour button's none;
// a side's colour's are not known (character select jumps on from it), so it leaves every register as the game's
// code does. They call the game's routines (the dice, 0x0611E0EE) through Guest::Call, and one another so too.
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp: Next_CPU.c), where they are known: this is the arcade's build of the same code, which
// differs from it here and there (the arcade's is what is written here). (Names only: that project is AGPL-3.0, and
// none of its code is here.)
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

    // --- against the computer (Setup_Next_Fighter) ---
    constexpr uint32_t kPlayer = 0x02015500;    // byte: the player's side (0 player 1, 1 player 2; Player_id)
    constexpr uint32_t kTheirSide = 0x02015503; // byte: the computer's side (COM_id)
    constexpr uint32_t kNext = 0x02015504;      // byte: the next opponent's character (EM_id)
    constexpr uint32_t kQsStage = 0x02016D81;   // byte: the stage Q is met on (his own: the last fight's)
    constexpr uint32_t kStageWord = 0x020154F4; // word: the next fight's stage (its low byte sfiii3.h's kStage)
    constexpr uint32_t kStageHead = 0x02026BAC; // the stage's head (bg_w: stage_camera.cpp); +4 its number, +5 its part
    constexpr uint32_t kSideFlags[2] = {0x0201581F, 0x02015821}; // bytes by side, cleared for a new opponent
                                                                 // (paring_counter, paring_bonus_r)
    constexpr uint32_t kArts = 0x0201138B;      // bytes by side: each side's super art, 0-2 (Super_Arts)
    constexpr uint32_t kKeptArts = 0x020156C0;  // bytes by player: the computer's art for the player's game, -1 none
                                                // yet (Stock_Com_Arts: kept until the player beats it)
    constexpr uint32_t kArtsByDice = 0x06195568; // bytes: the arts the dice choose from (Arts_Rnd_Data: 0 0 0 1 1 1 2 2)
    constexpr uint32_t kKeptColours = 0x020156BC; // words by player (Stock_Com_Color)
    constexpr uint32_t kBeatenBy = 0x020157D4;  // bytes [player][24]: each character the player has beaten (Break_Com)
    constexpr uint32_t kColourButton = 0x0201575A; // word: the button the computer's colour is chosen as by
                                                   // (Com_Color_Shot)
    constexpr uint8_t kGill = 0;

    int32_t SignedByte(Guest &g, uint32_t at) {
        return static_cast<int8_t>(g.Read8(at));
    }

    // 060A2846 (Setup_Com_Arts): the computer's super art for its next fight: Gill's second (1); else the one kept for
    // the player's game, or with none kept one by the dice (three in eight its first, three its second, two its third)
    int32_t ComputersArt(Guest &g) {
        if (g.Read8(kNext) == kGill) {
            return 1;
        }
        const uint32_t kept = kKeptArts + static_cast<uint32_t>(SignedByte(g, kPlayer));
        if (SignedByte(g, kept) == -1) {
            return g.Read8(kArtsByDice + (Dice(g) & 7));
        }
        return SignedByte(g, kept);
    }
    bool ComputersArtLift(Guest &g) {
        g.R(0) = static_cast<uint32_t>(ComputersArt(g));
        g.Return(30);
        return true;
    }

    // 060A28C4 (Setup_Com_Color): the button the computer's colour is chosen as by: heavy kick (0x200) for a character
    // the player has beaten before, else light punch (0x10). (The game's code writes the kept colour there first,
    // Stock_Com_Color, and then this over it: the kept one is never used.)
    void ComputersColour(Guest &g) {
        const int32_t player = SignedByte(g, kPlayer);
        const uint32_t beaten = kBeatenBy + static_cast<uint32_t>(static_cast<int8_t>(player * 24)) + static_cast<uint32_t>(SignedByte(g, kNext));
        g.Write16(kColourButton, g.Read8(beaten) != 0 ? 0x200 : 0x10);
    }
    bool ComputersColourLift(Guest &g) {
        ComputersColour(g);
        g.Return(20);
        return true;
    }

    // 060A28FE (Setup_PL_Color): a side's colour (r4 the side) by the button it was chosen with (r5): light punch
    // (0x10) its first, medium punch (0x20) its second, heavy punch (0x40) its third, light kick (0x80) its fourth,
    // medium kick (0x100) its fifth, heavy kick (or any other) its sixth, and light punch, medium kick and heavy punch
    // together (0x150) its seventh (its first, with the byte at 0x020156B2 3). But if the other side is a person who
    // has chosen the same character, in that colour, the colour paired with it: the first and the fourth, the second
    // and the fifth, the third and the sixth, the seventh and the first. Called by character select and for the
    // computer's next opponent (above).
    constexpr uint32_t kRecords = 0x02068C6C, kRecordSize = 0x498; // the players' records (plw); +3 a person's, not 0
                                                                   // (operator)
    constexpr uint32_t kHasChosen = 0x020154C2;   // words by side: not 0, it has chosen (Sel_PL_Complete)
    constexpr uint32_t kColours = 0x02015683;   // bytes by side: each side's colour (Player_Color; sfiii3.h's
                                                // kSelectColour)
    constexpr uint32_t kNoSeventh = 0x020156B2; // byte: 3, the seventh colour's buttons give the first
    struct Pair {
        int32_t colour, paired;
    };
    Pair ColourFor(Guest &g, uint32_t button) {
        switch (button) {
        case 0x10: return {0, 3};
        case 0x20: return {1, 4};
        case 0x40: return {2, 5};
        case 0x80: return {3, 0};
        case 0x100: return {4, 1};
        case 0x150: return g.Read8(kNoSeventh) == 3 ? Pair{0, 3} : Pair{6, 0};
        default: return {5, 2};
        }
    }
    bool SideColour(Guest &g) {
        const int32_t side = static_cast<int16_t>(g.R(4)), other = side ^ 1;
        const uint32_t button = g.R(5) & 0xFFFF;
        const int32_t person = SignedByte(g, kRecords + 3 + static_cast<uint32_t>(static_cast<int16_t>(other * kRecordSize)));
        // (the two sides' characters, as compared: none alike with the other side the computer's, or not chosen yet)
        int32_t mine = -1, theirs = 1;
        if (person != 0) {
            mine = SignedByte(g, kChosen + static_cast<uint32_t>(side));
            theirs = SignedByte(g, kChosen + static_cast<uint32_t>(other));
        }
        if (g.Read16(kHasChosen + static_cast<uint32_t>(other << 1)) == 0) {
            mine = 127;
        }
        const int32_t otherColour = SignedByte(g, kColours + static_cast<uint32_t>(other));
        const Pair pair = ColourFor(g, button);
        const bool paired = otherColour == pair.colour && mine == theirs;
        g.Write8(kColours + static_cast<uint32_t>(side), static_cast<uint8_t>(paired ? pair.paired : pair.colour));

        // What the game's code leaves in r0-r3, r6, r7 and T (character select jumps on from it to a routine of its
        // own: all are kept as the game's code has them)
        uint32_t r0 = static_cast<uint32_t>(otherColour), r1 = static_cast<uint32_t>(person), r2 = kColours,
                 r3 = static_cast<uint32_t>(other) << 1;
        if (button != 0x10 && button != 0x20 && button != 0x40) {
            r1 = button == 0x80 ? 0x80 : button == 0x100 ? 0x100 : 0x150; // (the buttons compared)
        }
        switch (button) {
        case 0x10:
            r0 = button;
            r1 = static_cast<uint32_t>(otherColour);
            break;
        case 0x20:
            if (paired) {
                r3 = 4;
            }
            break;
        case 0x40:
            if (paired) {
                r3 = 5;
            } else {
                r1 = 2;
            }
            break;
        case 0x100:
            if (!paired) {
                r2 = 4;
            }
            break;
        case 0x150:
            r3 = kNoSeventh;
            if (g.Read8(kNoSeventh) == 3) {
                r0 = 3;
                r1 = static_cast<uint32_t>(otherColour);
            } else if (!paired) {
                r2 = 6;
            }
            break;
        case 0x80:
            break;
        default:
            if (paired) {
                r3 = 2;
            } else {
                r1 = 5;
            }
            break;
        }
        g.R(0) = r0;
        g.R(1) = r1;
        g.R(2) = r2;
        g.R(3) = r3;
        g.R(6) = kColours + static_cast<uint32_t>(side);
        g.R(7) = static_cast<uint32_t>(mine);
        g.SetT(paired);
        g.Return(60);
        return true;
    }

    // 060A27DC: the next opponent made the computer's side's character, and the next fight's stage its stage (Q, who has
    // none, on the one he is met on); then its super art (kept for the player's game, as the game's code has it, as
    // well as the side's), the button its colour is chosen as by and its colour by that
    void OpponentsStage(Guest &g) {
        const auto side = [&]() { return static_cast<uint32_t>(SignedByte(g, kTheirSide)); };
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
        const uint32_t artAt = kArts + side();
        const uint32_t keptAt = kKeptArts + static_cast<uint32_t>(SignedByte(g, kPlayer));
        g.Call(0x060A2846); // (ComputersArt)
        g.Write8(keptAt, static_cast<uint8_t>(g.R(0)));
        g.Write8(artAt, static_cast<uint8_t>(g.R(0)));
        g.Call(0x060A28C4); // (ComputersColour)
        g.R(4) = static_cast<uint32_t>(SignedByte(g, kTheirSide));
        g.R(5) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kColourButton)));
        g.Call(0x060A28FE); // (SideColour)
    }
    bool OpponentsStageLift(Guest &g) {
        OpponentsStage(g);
        g.Return(80);
        return true;
    }

    const Lift kLifts[] = {
        {"StageForVersus", 0x060A0C52, 0x5E, 0x0A11240B587B474Eull, VersusStageLift, true, 0x100FE},
        {"StageForOpponent", 0x060A27DC, 0x6A, 0x183F21927A11D7FCull, OpponentsStageLift, true, 0x100FF},
        {"ComputersArt", 0x060A2846, 0x38, 0x480E43D0F890824Aull, ComputersArtLift, true, 0x100FE},
        {"ComputersColour", 0x060A28C4, 0x3A, 0xFBC113FEC6124131ull, ComputersColourLift, true, 0x103FF},
        {"SideColour", 0x060A28FE, 0x178, 0x99CB56C588730F21ull, SideColour, true, 0},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterStageSelect() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
