// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The bonus games (the Alternative modes page): 3rd Strike's two bonus stages of the one-player game, Crush the Car and
// Parry the Ball (Sean's basketballs), each played straight from the menu with the character chosen at character select.
//
// How: the board reset and a one-player game started (credits and Start, at speed and unseen, as the other modes
// start); character select shown, the player choosing. Then the game's own way to a bonus stage, from the step after
// select: once the player's character and super art are chosen, character select's third step out (Exit_3rd,
// 0x060A0912, below) would bring on the first opponent ("FIRST STAGE" and the versus screen, then the first match).
// With a bonus game on, it takes the one-player game instead to its step after a won match (G_No[1] 5, Game05 at
// 0x06095750), whose check for a bonus stage (Check_Bonus_Stage, 0x060A2E66, and its choice, Check_Bonus_Type,
// 0x060A2EE8: both below) chooses the bonus game's stage. From there it is all the game's: the "BONUS STAGE" screen
// and the versus screen (the car, or Sean), and the stage. When the game marks it done (its byte of Completion_Bonus),
// its result shows for a while and the menu opens again on the page.
//
// The one-player game's steps (G_No, words at 0x02015436: G_No[1] 1 character select, 2 a match, 3 its winner, 5 the
// next opponent (or bonus stage) shown, 9 the bonus stage); character select's step out (Exit_No, a word at
// 0x0201555E: 2 the opponent chosen next). The bonus check's places:
//   0x02015500  the one-player game's player (a byte: 0 player 1; Player_id)
//   0x02015506  opponents beaten, each player (words; VS_Index)
//   0x02016B89  the bonus stage chosen (a byte: 21 the car, 22 the ball, 0 none; Bonus_Type)
//   0x02016B8A  each bonus stage played, each player (two bytes a player: the car's, the ball's; 1 chosen, 0x80 done;
//               Completion_Bonus)
//   0x0206AC69  its bit 0: bonus stages in the one-player game (the check gives none without it)
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp: Game.c, Next_CPU.c, sel_pl.c), where they are known: this is the arcade's build of the
// same code, which differs from it here and there (the arcade's is what is written here). (Names only: that project is
// AGPL-3.0, and none of its code is here; what the routines do was read from the arcade's own program.)
//
// The three routines are rewrites (runtime/lift.h): with no bonus game on they do exactly what the game's code does
// (checked by checklifts.sh: CPS3RECOMP_LIFT_CHECK), with one on they make its choice, so a bonus game run with the
// check on reports them as different (checkstages.sh's bonus games compare only the stage's rewrites: these then
// registered as lifts, not compared). They leave every register as the game's code does.
#include "game.h"
#include "lift.h"
#include "recomp.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace cps3recomp {

void EndBattle(); // battle.cpp
bool AttractOn(); // attract.cpp
void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

namespace {
    constexpr const char *kPage = "Bonus games";
    constexpr uint32_t kPlayerId = 0x02015500, kBeaten = 0x02015506, kBonusType = 0x02016B89, kBonusDone = 0x02016B8A;
    constexpr uint8_t kCar = 21, kBall = 22;
    constexpr uint32_t kBootMost = 3000;   // frames the way to character select may take
    constexpr uint32_t kToStageMost = 600; // ... and from the choice to the bonus stage chosen (one frame, as a rule)
    constexpr uint32_t kPlayMost = 9000;   // ... and the bonus stage, with its screens (the car's clock run out: 4300)
    constexpr uint32_t kShownFor = 220;    // frames of its result after the bonus stage is done (to the screen's wipe)

    enum Phase : uint8_t { kOff, kBoot, kChoose, kToStage, kPlaying, kOver };

    struct State {
        Phase phase = kOff;
        uint8_t type = 0;      // kCar or kBall
        uint32_t frames = 0;   // in the phase
        bool fast = false;     // the game run unseen, at speed
        uint32_t session = 0;  // the sessions started before it (another started since: it is over)
    } g;

    void Fast(bool on) {
        if (g.fast != on) {
            g.fast = on;
            SetFastForward(on);
        }
    }
    void SetPhase(Phase phase) {
        if (getenv("SFIII3_BONUS_LOG")) { // (debugging aid)
            Machine *machine = TheMachine();
            fprintf(stderr, "bonus: phase %d -> %d after %u frames, at frame %llu (steps %u %u, beaten %u, bonus %u, done %02x %02x)\n", g.phase, phase,
                    g.frames, static_cast<unsigned long long>(machine ? machine->frame : 0), Ram16(0x02015438), Ram16(0x0201543A), Ram16(kBeaten), Ram8(kBonusType), Ram8(kBonusDone), Ram8(kBonusDone + 1));
        }
        g.phase = phase;
        g.frames = 0;
    }

    void Begin(Machine &machine, uint8_t type) {
        EndBattle();
        SetFeature("training_mode", false);
        machine.Reset();
        StateLoaded();
        g = {};
        g.type = type;
        g.session = session::Starts();
        SetPhase(kBoot);
        Fast(true);
    }
    void StartCar(Machine &machine) {
        Begin(machine, kCar);
    }
    void StartBall(Machine &machine) {
        Begin(machine, kBall);
    }
    void End() {
        Fast(false);
        SetPhase(kOff);
        AskMenu();
    }

    // --- the game's routines ---
    constexpr uint32_t kGameStep = 0x02015438;    // word: the game's step (G_No[1])
    constexpr uint32_t kGameSubStep = 0x0201543A; // word: ... and within it (G_No[2])
    constexpr uint8_t kAfterAWin = 5;             // (G_No[1]: Game05, the next opponent or a bonus stage)
    constexpr uint32_t kExitStep = 0x0201555E;    // word: character select's step out (Exit_No)
    constexpr uint32_t kSelectStep = 0x020154B8;  // word: character select's own step (S_No[1])
    constexpr uint32_t kBossRank = 0x02016B33;    // byte: the opponents to come are the last ones (EM_Rank)
    constexpr uint32_t kBonusesOn = 0x0206AC69;   // byte: bit 0, bonus stages in the one-player game
    constexpr uint32_t kFirstOpponent = 0x060A1A54; // the first opponent chosen and shown (Select_CPU_First): r0 not 0
                                                    // once done
    constexpr uint32_t kSetupId = 0x060A0712;     // which side is the player's, which the computer's (Setup_ID)
    constexpr uint32_t kBonusType_ = 0x060A2EE8;  // (Check_Bonus_Type, below)
    constexpr int kLastOpponents = 9;             // (opponents beaten from which the last ones come)
    // The stun gauges, each player's (the most stun it can take a word at +2: sfiii3.h's kStunMost its low byte)
    constexpr uint32_t kStunGauges = 0x020695F4; // (20 bytes a player)
    constexpr uint32_t kStunMostOf = 0x065EADBC, kStunRateOf = 0x065EADEC; // (a word, and a long, a character)
    constexpr uint32_t kChosenCharacter = 0x02011387; // (a byte a player)

    // 061186F2 (the stun gauge's set-up, player r4), as the one-player game's set-up of a match (0x06095758) calls it
    // for each player before every match: none taken, and the most its character can take
    void StunSetUp(Guest &guest, uint32_t player) {
        const uint32_t at = kStunGauges + player * 20, character = guest.Read8(kChosenCharacter + player);
        guest.Write8(at, 0);
        guest.Write16(at + 2, guest.Read16(kStunMostOf + character * 2));
        guest.Write16(at + 4, 0);
        guest.Write32(at + 8, 0);
        guest.Write32(at + 12, guest.Read32(kStunRateOf + character * 4));
        guest.Write16(at + 16, 0);
    }

    // The routine's own frame on the stack (what it pushes), so that what it calls finds the stack pointer where the
    // game's code has it
    struct Frame {
        Frame(Guest &guest, uint32_t bytes)
            : g(guest), size(bytes) {
            g.R(15) -= size;
        }
        ~Frame() { g.R(15) += size; }
        Guest &g;
        const uint32_t size;
    };
    uint32_t SignedByte(Guest &guest, uint32_t at) {
        return static_cast<uint32_t>(static_cast<int8_t>(guest.Read8(at)));
    }
    uint32_t SignedWord(Guest &guest, uint32_t at) {
        return static_cast<uint32_t>(static_cast<int16_t>(guest.Read16(at)));
    }

    // 060A0912 (Exit_3rd): character select's third step out, once both have chosen: the first opponent chosen and
    // shown ("FIRST STAGE", Select_CPU_First: r0 0 while it goes on, the rest as it leaves them); once that is done,
    // the next step out (Exit_No), select's own step back to 0, and whether the opponents to come are the last ones
    // (EM_Rank: after nine). Leaves r0 VS_Index's place, r1 1 (the last ones) or Player_id's place, r2 9, r3 the
    // opponents beaten, r4 0, r5 EM_Rank's place, T the last ones.
    //
    // With a bonus game on: no opponent; the one-player game goes on to its step after a won match (Game05, from its
    // start), whose bonus check chooses the bonus game's stage (below). The players' stun gauges are set up as the set-up
    // of a match before it would have (StunSetUp): with none, the most stun was 0, and the first ball that hit stunned at
    // once (the stage's own set-up does not do it: in the arcade the one-player game's matches before it have).
    bool ExitThird(Guest &guest) {
        if (g.phase == kChoose) {
            for (uint32_t player = 0; player < 2; player++) {
                StunSetUp(guest, player);
            }
            guest.Write16(kGameStep, kAfterAWin);
            guest.Write16(kGameSubStep, 0);
            SetPhase(kToStage);
            guest.Return(20);
            return true;
        }
        Frame frame(guest, 4); // (pr)
        guest.Call(kFirstOpponent);
        guest.SetT(guest.R(0) == 0);
        if (guest.R(0) == 0) {
            guest.Return(10);
            return true;
        }
        const uint32_t exitStep = SignedWord(guest, kExitStep) + 1;
        guest.Write16(kExitStep, static_cast<uint16_t>(exitStep));
        guest.Write16(kSelectStep, 0);
        const uint32_t beaten = SignedWord(guest, kBeaten + (SignedByte(guest, kPlayerId) << 1));
        const bool last = static_cast<int32_t>(beaten) >= kLastOpponents;
        guest.Write8(kBossRank, last ? 1 : 0);
        guest.R(0) = kBeaten;
        guest.R(1) = last ? 1 : kPlayerId;
        guest.R(2) = kLastOpponents;
        guest.R(3) = beaten;
        guest.R(4) = 0;
        guest.R(5) = kBossRank;
        guest.SetT(last);
        guest.Return(30);
        return true;
    }

    // 060A2E66 (Check_Bonus_Stage): in the step after a won match, whether a bonus stage comes next: none (r0 0) with
    // the board's bonus stages off (r3 the setting's place, the rest as Setup_ID leaves them); else the choice
    // (Check_Bonus_Type) kept as Bonus_Type, and if it is one, that stage's byte of Completion_Bonus set to 1 (chosen)
    // and r0 1 (r1 1, r2 Completion_Bonus' place, r3 Player_id's, r4 the stage less 21, T clear); r0 0 with none (r2
    // Bonus_Type's place, r4 0, T set).
    //
    // With a bonus game on: its stage, whatever the board's setting.
    bool CheckBonusStage(Guest &guest) {
        Frame frame(guest, 4); // (pr)
        guest.Call(kSetupId);
        guest.R(3) = kBonusesOn;
        guest.R(0) = SignedByte(guest, kBonusesOn);
        const bool bonuses = (guest.R(0) & 1) != 0 || g.phase == kToStage;
        guest.SetT(!bonuses);
        if (!bonuses) {
            guest.R(0) = 0;
            guest.Return(10);
            return true;
        }
        guest.Call(kBonusType_);
        const uint8_t type = static_cast<uint8_t>(guest.R(0));
        guest.R(2) = kBonusType;
        guest.R(4) = type;
        guest.SetT(type == 0);
        guest.Write8(kBonusType, type);
        if (type == 0) {
            guest.R(0) = 0;
            guest.Return(20);
            return true;
        }
        guest.R(3) = kPlayerId;
        guest.R(4) = type - 21u;
        guest.R(2) = kBonusDone;
        guest.R(1) = 1;
        guest.Write8(kBonusDone + (SignedByte(guest, kPlayerId) << 1) + guest.R(4), 1);
        guest.R(0) = 1;
        guest.Return(30);
        return true;
    }

    // 060A2EE8 (Check_Bonus_Type): the bonus stage due, in r0: the ball (22) once the player has beaten six opponents,
    // if not played yet; else the car (21) once three, if not played yet; else none (0). Leaves r1-r7 as the game's
    // code: r7 0x80, r3 Player_id's place, r4 the player's offset in the word tables, r5 its VS_Index's place, r6 its
    // Completion_Bonus, r2 and r1 what was compared last, T the last test's.
    //
    // With a bonus game on: its stage.
    bool CheckBonusType(Guest &guest) {
        if (g.phase == kToStage) {
            guest.R(0) = g.type;
            guest.Return(10);
            return true;
        }
        constexpr uint32_t kDone = 0x80;
        guest.R(7) = kDone;
        guest.R(3) = kPlayerId;
        guest.R(4) = SignedByte(guest, kPlayerId) << 1;
        guest.R(5) = kBeaten + guest.R(4);
        guest.R(6) = kBonusDone + guest.R(4);
        const int32_t beaten = static_cast<int16_t>(guest.Read16(guest.R(5)));
        guest.R(1) = 6;
        guest.R(2) = static_cast<uint32_t>(beaten);
        if (beaten >= 6) {
            const uint32_t played = SignedByte(guest, guest.R(6) + 1);
            guest.SetT((played & kDone) == 0);
            guest.R(0) = (played & kDone) ? 0 : kBall;
            guest.Return(10);
            return true;
        }
        guest.R(2) = 3;
        guest.R(3) = static_cast<uint32_t>(beaten);
        if (beaten < 3) {
            guest.SetT(false);
            guest.R(0) = 0;
            guest.Return(10);
            return true;
        }
        guest.R(1) = SignedByte(guest, guest.R(6));
        guest.SetT((guest.R(1) & kDone) == 0);
        guest.R(0) = (guest.R(1) & kDone) ? 0 : kCar;
        guest.Return(12);
        return true;
    }

    const Lift kLifts[] = {
        {"SelectExitThird", 0x060A0912, 0x3A, 0xC43BE36AB973FC63ull, ExitThird, true},
        {"CheckBonusStage", 0x060A2E66, 0x44, 0x293772A0F1200530ull, CheckBonusStage, true},
        {"CheckBonusType", 0x060A2EE8, 0x42, 0xEE39622C5ABA4AE7ull, CheckBonusType, true},
    };
} // namespace

bool BonusOn() {
    return g.phase != kOff;
}

void BonusBeforeFrame(Machine &m) {
    if (g.phase == kOff) {
        return;
    }
    if (session::Starts() != g.session || AttractOn()) {
        Fast(false);
        SetPhase(kOff); // (another mode started, training or a battle: the bonus game is over)
        return;
    }
    g.frames++;
    m.players[1] = 0; // (nobody joins in: a one-player game)
    const bool press = g.frames % 16 < 4;
    const int player = Ram8(kPlayerId) & 1;
    switch (g.phase) {
    case kBoot:
        // (a credit in, and player 1's Start until character select is on)
        SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 1));
        m.players[0] = press ? kStart : 0;
        if (Ram8(sfiii3::kSelectState[0]) == 2) {
            Fast(false);
            SetPhase(kChoose);
        } else if (g.frames > kBootMost) {
            End();
        }
        break;
    case kChoose:
        // (the player chooses; then select's step out takes the game to the bonus stage: ExitThird)
        break;
    case kToStage:
        if (Ram8(kBonusType) == g.type) {
            SetPhase(kPlaying);
        } else if (g.frames > kToStageMost) {
            End();
        }
        break;
    case kPlaying:
        if (Ram8(kBonusDone + 2 * player + (g.type == kCar ? 0 : 1)) & 0x80) {
            SetPhase(kOver);
        } else if (g.frames > kPlayMost) {
            End(); // (never marked done: the board reset under it, say)
        }
        break;
    case kOver:
        if (g.frames > kShownFor) {
            End();
        }
        break;
    default: break;
    }
}

void BonusState(Archive &ar) {
    ar.Mark(0x424F4E53); // BONS
    ar.Pod(g);
}

void RegisterBonusLifts() {
    // (with only some rewrites registered, SFIII3_LIFTS_ONLY, and these not among them: these still registered, as lifts
    // that are not compared, so that a bonus game can be played in a check of other rewrites: checkstages.sh's)
    if (const char *only = getenv("SFIII3_LIFTS_ONLY")) {
        static Lift asLifts[std::size(kLifts)];
        size_t count = 0;
        for (const Lift &lift : kLifts) {
            if ((std::string(",") + only + ",").find(std::string(",") + lift.name + ",") == std::string::npos) {
                asLifts[count] = lift;
                asLifts[count++].rewrite = false;
            }
        }
        RegisterLifts(asLifts, count);
    }
    RegisterGameLifts(kLifts, std::size(kLifts));
}

void RegisterBonus(Game &game) {
    game.commands.push_back({"bonus_car", kPage, "Crush the car",
                             "Choose a character, and go straight to the bonus stage of the car: break it up before the time runs out.",
                             StartCar});
    game.commands.push_back({"bonus_ball", kPage, "Parry the ball",
                             "Choose a character, and go straight to the bonus stage of Sean's basketballs: parry as many as you can.",
                             StartBall});
}

} // namespace cps3recomp
