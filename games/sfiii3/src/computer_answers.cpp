// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer's answers to what the other does (0x06012588 and what it calls).
// The computer's design, its record's fields and the other parts: computer.h.
//
// How the computer answers. When it is to answer the other (Check_Passive, 0x06010DEA, computer_tactics.cpp), it asks
// 0x06012588 (Ck_Passive_Term) what to do: an answer, a number (VS_Tech, kAnswer, the plan of the computer's own
// character's answer routine to play: mode 6, computer_entry.cpp), and whether one was chosen (kAnswered, the
// result). The answer depends on the other: by the other's character, one of three chains of tests (Passive_jmp_tbl):
// one against Gill, one against Hugo, one against everyone else. A chain looks, in order, until a test answers:
//
//   - on the first look of an answer (kPassiveMode 4), by the range the gap is in (kRange, 0 close - 3 farthest) and
//     whether the other's attack is out (kOtherAttacking): the other's character's own list for that range, attack
//     or not (eight tables of 21 lists, a list a character: VS_RYU_AS, VS_RYU_A ... VS_RYU_D for Ryu). Then the tests
//     every character meets: an attack caught early (a given pose, a given kind of attack, no further into its
//     animation than a limit), move 33 (of kind 0) as a counter attack, the other after its attack, crouching or
//     standing for a while (the timers kStandFor and kCrouchFor, chosen at random), jumping, or taunting.
//   - on a later look (kPassiveMode 0): only the other dashing.
//
// A list is the character's moves worth an answer, each a test of the other's attack (Check_Special_Technique, Move
// here): its kind (the high five bits of kind_of_waza), the move (sp_tech_id), and when to answer it: at any time or
// as a counter attack (kCounter then set: the answer is not to be broken off), any strength or not the light one.
// The answer is chosen once an attack (kAttacksSeen): an attack not worth answering (the wrong strength) is marked
// seen (kLastAttackSeen) and not looked at again. The lists are the game's own code here, a function each, with
// the numbers the game's code has: the answers, the kinds and the moves; the two pairs of characters who share
// moves (Yun and Yang, Akuma and Shin Akuma) share lists.
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, Ck_Pass.c), where they are known: this is the arcade's build of the same code. (Names
// only: that project is AGPL-3.0, and none of its code is here; what the routines do was read from the arcade's own
// program and checked call by call against it.)
//
// Registers: the tests, the lists and the chains are compiled C, but what the last test leaves in r5-r7 reaches
// 0x06012588's callers (they pass them on: Check_Guard, 0x06010F4C, gives up early without touching them), so each
// test leaves r0 and r5-r7 as the game's code does (Pass sets the registers the call has; each test changes them
// where its code does). One slip of the game's own is kept: the jump test (Check_VS_Jump, 0x06013034) is given a
// height (r6) it never reads, and compares the other's height with r7, whatever the test before it left there.
//
// The chains call the lists through the game's tables (so each list is the lift at its own address), the tests
// directly; the routines not here (the dice, the stand and crouch times, the divide) through the game's calls.
// Checked in layers (checklifts.sh): a list is compared only when the game's own dispatcher calls it, and only
// against the characters the runs' made-up players chose, so the lists were met with more seeds than the check's.
#include "computer.h"

#include <cstdint>
#include <iterator>

namespace cps3recomp {

namespace {
    // --- the fighter's record (r4: the computer's; its kOpponent, r5, the other's) ---
    constexpr uint32_t kSide = 0x08;          // word: 0 player 1, 1 player 2 (wu.id); the computer's tables are by it
    constexpr uint32_t kOpponent = 0x0C;      // long: the other fighter's record (target_adrs)
    constexpr uint32_t kDoing = 0x26;         // word: what the fighter is doing (routine_no[1]): 0 free, 1 hit, 4 in an
                                              // attack
    constexpr uint32_t kDoingHow = 0x28;      // word: its detail (routine_no[2]): with 0, 5 a dash; with 4, 30 a taunt
                                              // (the personal action)
    constexpr uint32_t kDoingStep = 0x2A;     // word: its step (routine_no[3])
    constexpr uint32_t kX = 0x64, kY = 0x68;  // words: where it is, along the floor and up from it (xyz)
    constexpr uint32_t kWasX = 0x70;          // word: its x the frame before (old_pos[0])
    constexpr uint32_t kRising = 0x80;        // word: its speed upward (mvxy.a[1]): below 0 coming down
    constexpr uint32_t kAnimPlace = 0x204;    // word: where its animation is, in its data (cg_ix)
    constexpr uint32_t kAnimStepSize = 0x20C; // word: the size of a step of that data (cgd_type): kAnimPlace by it is
                                              // the step the animation is at
    constexpr uint32_t kPose = 0x20E;         // byte: its pose (pat_status): 34 rolling
    constexpr uint32_t kAttackKind = 0x20F;   // byte: the kind of its attack (kind_of_waza): the high five bits the
                                              // kind (0 a normal attack, the rest special moves and super arts), bits
                                              // 1-2 its strength
    constexpr uint32_t kMoveId = 0x213;       // byte: the move (sp_tech_id), the character's own numbers
    constexpr uint32_t kCharacter = 0x3C0;    // word: the character (player_number)
    constexpr uint32_t kStuck = 0x3C9;        // byte (micchaku_flag): pressed against the other

    // --- the computer's tables: two of each, by kSide ---
    constexpr uint32_t kMode = 0x02015572;        // words, four a side (CP_No): the mode, 7 guarding
    constexpr uint32_t kRange = 0x020155B6;       // word: which range the gap is in, 0 close - 3 far (Area_Number)
    constexpr uint32_t kAnswer = 0x020155F8;      // word: the answer chosen (VS_Tech)
    constexpr uint32_t kOtherAttacking = 0x0201562A; // byte: the other's attack is out (Attack_Flag)
    constexpr uint32_t kLimited = 0x0201562D;     // byte (Limited_Flag): the answer is to an attack caught early
    constexpr uint32_t kCounter = 0x02015648;     // byte (Counter_Attack): the answer a counter attack, not to be broken off
    constexpr uint32_t kStanding = 0x02015740;    // byte: frames the other has stood (Standing_Timer)
    constexpr uint32_t kAttacksSeen = 0x02015750; // word: the other's attacks counted (Attack_Counter)
    constexpr uint32_t kLastAttackSeen = 0x02015754; // word: kAttacksSeen when last answered or let go (Last_Attack_Counter)
    constexpr uint32_t kStandFor = 0x0201575C;    // byte: frames of standing worth an answer (Standing_Master_Timer)
    constexpr uint32_t kCrouching = 0x02015764;   // byte: frames the other has crouched (Squat_Timer)
    constexpr uint32_t kCrouchFor = 0x02015766;   // byte: frames of crouching worth an answer (Squat_Master_Timer)
    constexpr uint32_t kJumpTimers = 0x020157C0;  // bytes, four a side, by range (Jump_Pass_Timer): jumps let go

    // --- the game's, for everyone ---
    constexpr uint32_t kAnswered = 0x0200DCF8;    // byte: an answer was chosen this time (PASSIVE_X)
    constexpr uint32_t kPassiveMode = 0x0201562C; // byte: 4 the first look of an answer, 0 a later one (Passive_Mode)

    // --- tables in the program ---
    constexpr uint32_t kChains = 0x0613E8B0;      // 21 longs: the chain of tests against each character (Passive_jmp_tbl)
    // 21 longs each: the lists, a character each, by range and whether the other's attack is out
    constexpr uint32_t kCloseLists = 0x0613E904, kCloseAttackingLists = 0x0613E958;       // Passive_AS_tbl, Passive_A_tbl
    constexpr uint32_t kMiddleLists = 0x0613E9AC, kMiddleAttackingLists = 0x0613EA00;     // Passive_BS_tbl, Passive_B_tbl
    constexpr uint32_t kFarLists = 0x0613EA54, kFarAttackingLists = 0x0613EAA8;           // Passive_CS_tbl, Passive_C_tbl
    constexpr uint32_t kFarthestLists = 0x0613EAFC, kFarthestAttackingLists = 0x0613EB50; // Passive_DS_tbl, Passive_D_tbl

    // --- the routines called ---
    constexpr uint32_t kCloseList = 0x06013A86;   // Check_PL_Unit_AS ... (r4): the other's character's list, run
    constexpr uint32_t kCloseAttackingList = 0x0601412C, kMiddleList = 0x06014994, kMiddleAttackingList = 0x0601518A;
    constexpr uint32_t kFarList = 0x06015B74, kFarAttackingList = 0x060162C6, kFarthestList = 0x06016BFC;
    constexpr uint32_t kFarthestAttackingList = 0x06016CEA;
    constexpr uint32_t kStandTime = 0x060134CC;   // Setup_Next_Stand_Timer (r4): one at random (computer_entry.cpp)
    constexpr uint32_t kCrouchTime = 0x060136A4;  // Setup_Next_Squat_Timer (r4): one at random (computer_entry.cpp)
    constexpr uint32_t kDivide = 0x0612D584;      // the compiler's division of words: r1 by r0, in r0 (it leaves the
                                                  // CPU's division flags as it does: called, not written again)

    // The characters, kCharacter
    constexpr int kYun = 3, kYang = 10, kQ = 18;

    // A test's arguments: when to answer (Option)
    constexpr int16_t kAny = -1;
    constexpr int16_t kAsCounter = 1;      // as a counter attack: kCounter set
    constexpr int16_t kNotAttacking = 8;  // only while the other's attack is not out, and as a counter attack
    // and which strength (Option2: bits 1-2 of the kind): kAny, one strength not (the attack let go when it is that one:
    // kNotLight, not 0), or with kOnly the bits of which one must be there
    constexpr int16_t kNotLight = 0, kOnly = 8;
    // (the answer given even to a move that goes away from the computer)
    constexpr int16_t kAnswerAnyway = 23;

    // One side of the computer: its fighter's record, the other's, and its entries in the tables
    class Side {
    public:
        Side(Guest &guest, uint32_t record)
            : Side(guest, record, guest.Read32(record + kOpponent)) {}
        Side(Guest &guest, uint32_t record, uint32_t opponent)
            : g(guest), me(record), other(opponent), side(static_cast<int16_t>(guest.Read16(record + kSide))) {}

        Guest &g;
        const uint32_t me, other;
        const int32_t side;

        uint32_t At(uint32_t table, int32_t bytesPerSide) { return table + static_cast<uint32_t>(side * bytesPerSide); }
        uint8_t Byte(uint32_t table) { return g.Read8(At(table, 1)); }
        void SetByte(uint32_t table, int32_t value) { g.Write8(At(table, 1), static_cast<uint8_t>(value)); }
        int16_t Word(uint32_t table) { return static_cast<int16_t>(g.Read16(At(table, 2))); }
        void SetWord(uint32_t table, int32_t value) { g.Write16(At(table, 2), static_cast<uint16_t>(value)); }

        // The other's fields
        uint8_t Its(uint32_t field) { return g.Read8(other + field); }
        int16_t ItsWord(uint32_t field) { return static_cast<int16_t>(g.Read16(other + field)); }

        bool OtherAttacking() { return Byte(kOtherAttacking) != 0; }
        // The other's attack now answered already (or let go)
        bool AttackSeen() { return Word(kLastAttackSeen) == Word(kAttacksSeen); }
        void MarkAttackSeen() { SetWord(kLastAttackSeen, Word(kAttacksSeen)); }

        // The answer chosen
        bool Choose(int32_t answer) {
            SetWord(kAnswer, answer);
            g.Write8(kAnswered, 1);
            g.R(0) = 1;
            return true;
        }
        bool No() {
            g.R(0) = 0;
            return false;
        }

        // The registers as the game's call of a test has them: r4 the computer, r5 the other, r6 and r7 its first
        // arguments (where the call sets them)
        void Pass() {
            g.R(4) = me;
            g.R(5) = other;
        }
        void Pass(int32_t r6) {
            Pass();
            g.R(6) = static_cast<uint32_t>(r6);
        }
        void Pass(int32_t r6, int32_t r7) {
            Pass(r6);
            g.R(7) = static_cast<uint32_t>(r7);
        }
        uint32_t SideTimes2() { return static_cast<uint32_t>(side * 2); }
    };


    // --- the tests (r4 the computer, r5 the other, r6 the answer; true and the answer chosen when the other does
    // what the test looks for) ---

    // 06012FB0 (Check_Attack_Direction): the other moving away from the computer (its x against the frame before)
    bool MovingAway(Guest &g, uint32_t me, uint32_t other) {
        const int16_t mine = static_cast<int16_t>(g.Read16(me + kX)), theirs = static_cast<int16_t>(g.Read16(other + kX));
        const int16_t before = static_cast<int16_t>(g.Read16(other + kWasX));
        return mine < theirs ? theirs > before : theirs < before;
    }

    // 06012EA0 (Check_Special_Technique, r7 the kind, stack longs: the move, when, which strength): the other's attack
    // is the move of the kind given: answered when the move goes toward the computer (but kAnswerAnyway), when it is
    // not answered already and its strength is one asked for. Leaves r5-r7 as the routine does.
    bool Move(Side &s, int32_t answer, int32_t kind, int32_t move, int16_t when = kAny, int16_t strength = kAny) {
        Guest &g = s.g;
        s.Pass(answer, kind);
        if (when == kNotAttacking && s.OtherAttacking()) {
            return s.No();
        }
        if (static_cast<int16_t>(answer) != kAnswerAnyway && MovingAway(g, s.me, s.other)) {
            return s.No();
        }
        g.R(5) = kLastAttackSeen;
        if (s.AttackSeen()) {
            g.R(6) = s.SideTimes2();
            return s.No();
        }
        const uint8_t itsKind = s.Its(kAttackKind);
        g.R(6) = itsKind & 0xF8;
        if ((itsKind & 0xF8) != static_cast<uint8_t>(kind) || s.Its(kMoveId) != static_cast<uint8_t>(move)) {
            return s.No();
        }
        g.R(6) = 6;
        if (strength == kAny || !(strength & kOnly)) {
            if ((itsKind & 6) == strength) { // (not this strength: let go)
                s.MarkAttackSeen();
                g.R(5) = s.At(kLastAttackSeen, 2);
                g.R(6) = s.SideTimes2();
                return s.No();
            }
        } else if ((itsKind & strength & 6) == 0) {
            return s.No();
        }
        if (when == kNotAttacking || when == kAsCounter) {
            s.SetByte(kCounter, 1);
        }
        g.R(5) = kCounter;
        return s.Choose(answer);
    }

    // 06013212 (Check_Rolling): the other rolling: answer 6 when it rolls away, 5 when toward
    bool Roll(Side &s) {
        s.Pass();
        if (s.Its(kPose) != 34) {
            return s.No();
        }
        return s.Choose(MovingAway(s.g, s.me, s.other) ? 6 : 5);
    }

    // 06013252 (Check_Personal_Action): the other taunting: answer 0x1009
    bool Taunt(Side &s) {
        s.Pass();
        if (s.ItsWord(kDoing) != 4 || s.ItsWord(kDoingHow) != 30) {
            return s.No();
        }
        return s.Choose(0x1009);
    }

    // 06013280 (Check_Specific_Term, r7 a pose, stack longs: two more): the answer set in any case; chosen when the
    // other is in one of the three poses. Leaves r2, r4, r6, r7 as the routine does.
    bool InPose(Side &s, int32_t answer, int32_t pose, int32_t pose2, int32_t pose3) {
        Guest &g = s.g;
        s.Pass(answer, pose);
        g.R(7) = static_cast<uint8_t>(pose);
        s.SetWord(kAnswer, answer);
        g.R(6) = kAnswered;
        g.R(4) = 1;
        const uint8_t itsPose = s.Its(kPose);
        g.R(2) = itsPose;
        bool yes = itsPose == static_cast<uint8_t>(pose);
        if (!yes) {
            g.R(2) = static_cast<uint32_t>(static_cast<int8_t>(itsPose));
            yes = itsPose == static_cast<uint8_t>(pose2) || itsPose == static_cast<uint8_t>(pose3);
        }
        if (!yes) {
            return s.No();
        }
        g.Write8(kAnswered, 1);
        g.R(0) = 1;
        return true;
    }

    // 060132D8 (Check_Dash): the other dashing. Leaves r7 the other's record + 0x24, as the routine does (it reads the
    // three words from there).
    bool Dash(Side &s, int32_t answer) {
        s.Pass(answer);
        s.g.R(7) = s.other + 0x24;
        if (s.ItsWord(kDoing) != 0 || s.ItsWord(kDoingHow) != 5 || s.ItsWord(kDoingStep) == 0) {
            return s.No();
        }
        return s.Choose(answer);
    }

    // 06013306 (Check_Limited_Attack, r7 a pose, stack longs: a kind of attack, a limit): the other's attack, its
    // kind and pose those given, caught early: no further into its animation than the limit (a step more for Q's and
    // Yang's, two for Yun's, against answer 7). kLimited and kCounter set. Leaves r5 and r7 as the routine does.
    bool EarlyInMove(Side &s, int32_t answer, int32_t pose, int32_t kind, int32_t limit) {
        Guest &g = s.g;
        s.Pass(answer, pose);
        if (!s.OtherAttacking() || s.AttackSeen()) {
            return s.No();
        }
        g.R(7) = static_cast<uint8_t>(pose);
        if (s.Its(kPose) != static_cast<uint8_t>(pose) || s.Its(kAttackKind) != static_cast<int8_t>(kind)) {
            return s.No();
        }
        g.R(1) = static_cast<uint32_t>(static_cast<int32_t>(s.ItsWord(kAnimPlace)));
        g.R(0) = static_cast<uint32_t>(static_cast<int32_t>(s.ItsWord(kAnimStepSize)));
        g.Call(kDivide);
        const int16_t step = static_cast<int16_t>(g.R(0));
        g.R(7) = static_cast<uint32_t>(static_cast<int32_t>(step));
        int16_t most = static_cast<int16_t>(limit);
        if (static_cast<int16_t>(answer) == 7) {
            const int16_t character = s.ItsWord(kCharacter);
            most = static_cast<int16_t>(most + (character == kQ || character == kYang ? 1 : character == kYun ? 2 : 0));
        }
        if (step > most) {
            return s.No();
        }
        s.SetByte(kLimited, 1);
        s.SetByte(kCounter, 1);
        g.R(5) = 1;
        return s.Choose(answer);
    }

    // 06013460 (Check_Stand): the other standing, its attack not out, for kStandFor frames: then a new kStandFor
    bool Standing(Side &s, int32_t answer) {
        Guest &g = s.g;
        s.Pass(answer);
        if (s.OtherAttacking() || s.ItsWord(kDoing) != 0) {
            return s.No();
        }
        s.SetByte(kStanding, s.Byte(kStanding) + 1);
        if (static_cast<int8_t>(s.Byte(kStanding)) < static_cast<int8_t>(s.Byte(kStandFor))) {
            return s.No();
        }
        g.R(4) = s.me;
        g.Call(kStandTime);
        s.SetByte(kStandFor, static_cast<int32_t>(g.R(0)));
        return s.Choose(answer);
    }

    // 060135DE (Check_VS_Squat, r7 a pose, a stack long: another): the other on the floor, free, its attack not out, in
    // one of the two poses (crouching) for kCrouchFor frames: then a new kCrouchFor. Anything else starts the count
    // again. Leaves r6 0 and r7 as the routine does.
    bool Crouching(Side &s, int32_t answer, int32_t pose, int32_t pose2) {
        Guest &g = s.g;
        s.Pass(answer, pose);
        g.R(6) = 0;
        if (s.OtherAttacking() || s.ItsWord(kDoing) != 0 || s.ItsWord(kY) != 0) {
            s.SetByte(kCrouching, 0);
            return s.No();
        }
        g.R(7) = static_cast<uint8_t>(pose);
        const uint8_t itsPose = s.Its(kPose);
        if (itsPose != static_cast<uint8_t>(pose) && itsPose != static_cast<uint8_t>(pose2)) {
            s.SetByte(kCrouching, 0);
            return s.No();
        }
        s.SetByte(kCrouching, s.Byte(kCrouching) + 1);
        if (static_cast<int8_t>(s.Byte(kCrouching)) < static_cast<int8_t>(s.Byte(kCrouchFor))) {
            return s.No();
        }
        g.R(4) = s.me;
        g.Call(kCrouchTime);
        s.SetByte(kCrouchFor, static_cast<int32_t>(g.R(0)));
        return s.Choose(answer);
    }

    // 06013930 (Check_After_Attack): the other's attack, new, on the floor (unless the computer is guarding): marked
    // seen, and answered when it is a special move (or more) or a normal attack of strength 4 or 6. Leaves r5 and r7
    // as the routine does.
    bool AfterAttack(Side &s, int32_t answer) {
        Guest &g = s.g;
        s.Pass(answer);
        if (static_cast<int16_t>(g.Read16(s.At(kMode, 8))) == 7) {
            return s.No();
        }
        g.R(7) = kLastAttackSeen;
        if (s.AttackSeen() || s.ItsWord(kY) != 0 || s.ItsWord(kDoing) != 4) {
            return s.No();
        }
        s.MarkAttackSeen();
        const uint8_t itsKind = s.Its(kAttackKind);
        if ((itsKind & 0x38) == 0) {
            const uint32_t strength = itsKind & 6;
            g.R(5) = strength;
            g.R(7) = strength;
            if (strength == 0 || strength == 2) {
                return s.No();
            }
        }
        return s.Choose(answer);
    }

    // 06013404 (r5 the other, r6 a pose, r7 an attack's kind): the other in that pose (kPose) with an attack of that
    // kind (kAttackKind). Leaves r2, r3, r6, r7 and T as the routine does.
    bool PoseAndKindLift(Guest &g) {
        const uint32_t other = g.R(5);
        g.R(0) = 0x20E;
        g.R(6) &= 0xFF;
        g.R(3) = g.Read8(other + 0x20E);
        bool yes = g.R(3) == g.R(6);
        if (yes) {
            g.R(0) = 0x20F;
            g.R(7) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(g.R(7))));
            g.R(2) = g.Read8(other + 0x20F);
            yes = g.R(2) == g.R(7);
        }
        g.SetT(yes);
        g.R(0) = yes ? 1 : 0;
        g.Return(12);
        return true;
    }

    // 06013A18 (Check_F_Cross_Chop): the other's attack, new, a jumping normal attack of kind 4 (poses 20, 22, 26,
    // 28): kCounter set. Leaves r5 and r7 as the routine does.
    bool CrossChop(Side &s, int32_t answer) {
        Guest &g = s.g;
        s.Pass(answer);
        g.R(7) = s.SideTimes2();
        if (s.AttackSeen() || s.Its(kAttackKind) != 4) {
            return s.No();
        }
        const uint8_t itsPose = s.Its(kPose);
        if (itsPose != 22 && itsPose != 20 && itsPose != 26 && itsPose != 28) {
            return s.No();
        }
        s.SetByte(kCounter, 1);
        g.R(5) = 1;
        return s.Choose(answer);
    }

    // 06013034 (Check_VS_Jump, r6 a height it does not read): the other in the air (not hit, not in move 33). A jump
    // is let go while the range's kJumpTimers counts down. Coming down below r7 (the height the call meant, r6, was
    // never read: what the test before left in r7 is used, the game's slip) it is let go; rising into a jump (poses
    // 14, 20, 26), answer 0x1003 as a counter attack; else in the air, not just off the floor and rising, answer 18
    // pressed against the computer or in poses 22, 28, 16. Leaves r4-r7 as the routine does.
    bool Jump(Side &s, int32_t height) {
        Guest &g = s.g;
        s.Pass(height);
        if (s.ItsWord(kDoing) == 1 || s.Its(kMoveId) == 33) {
            return s.No();
        }
        g.R(4) = kJumpTimers;
        g.R(5) = kRange;
        g.R(6) = static_cast<uint32_t>(s.side);
        const uint32_t timer = s.At(kJumpTimers, 4) + static_cast<uint32_t>(static_cast<int32_t>(s.Word(kRange)));
        if (g.Read8(timer) != 0) {
            g.Write8(timer, static_cast<uint8_t>(g.Read8(timer) - 1));
            return s.No();
        }
        if (s.ItsWord(kRising) < 0) {
            g.R(7) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(g.R(7))));
            if (s.ItsWord(kY) <= static_cast<int16_t>(g.R(7))) {
                return s.No();
            }
        }
        if (InPose(s, 0x1003, 14, 20, 26)) {
            s.SetByte(kCounter, 1);
            return true;
        }
        const int16_t up = s.ItsWord(kY);
        if (up == 0 || (up < 32 && s.ItsWord(kRising) > 0)) {
            return s.No();
        }
        if (s.Its(kStuck) != 0) {
            return s.Choose(18);
        }
        return InPose(s, 18, 22, 28, 16);
    }


    // --- the lists: a function each, against the other's character, by range and whether its attack is out (the
    // tests in the game's order: the first to answer ends it) ---

    // Against Gill
    // 06013A9C (VS_GILL_AS)
    bool VsGillClose(Side &s) { return Move(s, 24, 8, 61) || Move(s, 24, 8, 64); }
    // 06014142 (VS_GILL_A)
    bool VsGillCloseAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 64, kAsCounter) || Move(s, 15, 8, 63, kAsCounter) ||
               Move(s, 24, 8, 61);
    }
    // 060149AA (VS_GILL_BS)
    bool VsGillMiddle(Side &s) { return Move(s, 24, 8, 61) || Move(s, 24, 8, 64); }
    // 060151AC (VS_GILL_B)
    bool VsGillMiddleAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 64, kAsCounter) || Move(s, 15, 8, 63, kAsCounter) ||
               Move(s, 24, 8, 61);
    }
    // 06015B8A (VS_GILL_CS)
    bool VsGillFar(Side &s) { return Move(s, 24, 8, 65) || Move(s, 24, 8, 64); }
    // 060162DC (VS_GILL_C)
    bool VsGillFarAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 64, kAsCounter) || Move(s, 15, 8, 63, kAsCounter) ||
               Move(s, 24, 8, 61);
    }
    // (no answer: farthest (VS_GILL_DS); farthest, attacking (VS_GILL_D))

    // Against Alex
    // 06013AEE (VS_ALEX_AS)
    bool VsAlexClose(Side &s) { return Move(s, 23, 24, 23) || Move(s, 14, 24, 22) || Move(s, 14, 56, 37) || Roll(s); }
    // 060141DA (VS_ALEX_A)
    bool VsAlexCloseAttacking(Side &s) {
        return Move(s, 25, 24, 22, kAsCounter) || Move(s, 24, 24, 115, kAsCounter, kNotLight) || CrossChop(s, 15) ||
               Move(s, 24, 8, 73, kAsCounter) || Move(s, 15, 8, 30, kAsCounter) || Move(s, 15, 40, 72, kAsCounter);
    }
    // 06014A00 (VS_ALEX_BS)
    bool VsAlexMiddle(Side &s) { return Move(s, 23, 24, 23) || Move(s, 14, 24, 22) || Move(s, 14, 56, 37) || Roll(s); }
    // 06015244 (VS_ALEX_B)
    bool VsAlexMiddleAttacking(Side &s) {
        return Move(s, 25, 24, 22, kAsCounter) || Move(s, 24, 24, 115, kAsCounter, kNotLight) || CrossChop(s, 15) ||
               Move(s, 24, 8, 73, kAsCounter) || Move(s, 15, 8, 30, kAsCounter) || Move(s, 15, 40, 72, kAsCounter);
    }
    // 06015BE0 (VS_ALEX_CS)
    bool VsAlexFar(Side &s) { return Move(s, 23, 24, 23) || Move(s, 14, 24, 22) || Move(s, 14, 56, 37); }
    // 06016380 (VS_ALEX_C)
    bool VsAlexFarAttacking(Side &s) {
        return Move(s, 24, 24, 115, kAsCounter, kNotLight) || Move(s, 15, 8, 30, kAsCounter) ||
               Move(s, 24, 8, 73, kAsCounter) || Move(s, 15, 40, 72, kAsCounter) || CrossChop(s, 15);
    }
    // (no answer: farthest (VS_ALEX_DS); farthest, attacking (VS_ALEX_D))

    // Against Ryu
    // 06013B8A (VS_RYU_AS)
    bool VsRyuClose(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter) || Move(s, 11, 32, 4) ||
               Move(s, 11, 32, 5, kAsCounter);
    }
    // 060142AC (VS_RYU_A)
    bool VsRyuCloseAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 23, 48, 3) || Move(s, 16, 32, 4) || Move(s, 11, 32, 5, kAsCounter);
    }
    // 06014A84 (VS_RYU_BS)
    bool VsRyuMiddle(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2) ||
               Move(s, 23, 48, 3) || Move(s, 16, 32, 4) || Move(s, 11, 32, 5, kAsCounter);
    }
    // 0601530E (VS_RYU_B)
    bool VsRyuMiddleAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2, kAsCounter) || Move(s, 16, 32, 4, kAsCounter) ||
               Move(s, 11, 32, 5, kAsCounter);
    }
    // 06015C54 (VS_RYU_CS)
    bool VsRyuFar(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter) || Move(s, 23, 48, 3) ||
               Move(s, 16, 32, 4) || Move(s, 11, 32, 5, kAsCounter);
    }
    // 0601642C (VS_RYU_C)
    bool VsRyuFarAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2, kAsCounter) || Move(s, 16, 32, 4, kAsCounter) ||
               Move(s, 11, 32, 5, kAsCounter);
    }
    // 06016C1A (VS_RYU_DS)
    bool VsRyuFarthest(Side &s) { return Move(s, 11, 32, 5, kAsCounter); }
    // 06016D08 (VS_RYU_D)
    bool VsRyuFarthestAttacking(Side &s) { return Move(s, 11, 32, 5, kAsCounter); }

    // Against Yun (and Yang: the same lists)
    // 06013C1A (VS_YUN_AS)
    bool VsYunClose(Side &s) { return Move(s, 23, 8, 29) || Move(s, 23, 32, 36) || Move(s, 14, 24, 74); }
    // 06014342 (VS_YUN_A)
    bool VsYunCloseAttacking(Side &s) { return Move(s, 20, 8, 52, kAsCounter) || Move(s, 25, 24, 74, kAsCounter); }
    // 06014B68 (VS_YUN_BS)
    bool VsYunMiddle(Side &s) { return Move(s, 23, 8, 29) || Move(s, 23, 32, 36) || Move(s, 14, 24, 74); }
    // 060153A6 (VS_YUN_B)
    bool VsYunMiddleAttacking(Side &s) {
        return Move(s, 8, 8, 31) || Move(s, 20, 8, 52, kAsCounter) || Move(s, 25, 24, 74, kAsCounter);
    }
    // 06015D14 (VS_YUN_CS)
    bool VsYunFar(Side &s) { return Move(s, 23, 8, 29) || Move(s, 23, 32, 36) || Move(s, 14, 24, 74); }
    // 060164C4 (VS_YUN_C)
    bool VsYunFarAttacking(Side &s) { return Move(s, 8, 8, 31) || Move(s, 20, 8, 52, kAsCounter); }
    // (no answer: farthest (VS_YUN_DS); farthest, attacking (VS_YUN_D))

    // Against Dudley
    // 06013C88 (VS_DUDLEY_AS)
    bool VsDudleyClose(Side &s) {
        return Move(s, 23, 8, 1) || Move(s, 24, 8, 2) || Move(s, 14, 32, 13) || Move(s, 23, 32, 11) ||
               Move(s, 14, 32, 12, kAny, kNotLight);
    }
    // 0601439A (VS_DUDLEY_A)
    bool VsDudleyCloseAttacking(Side &s) {
        return Move(s, 24, 8, 2) || Move(s, 18, 8, 75, kAsCounter, kNotLight) || Move(s, 14, 32, 12);
    }
    // 06014BDC (VS_DUDLEY_BS)
    bool VsDudleyMiddle(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 14, 32, 13) || Move(s, 23, 32, 11) || Move(s, 14, 32, 12);
    }
    // 06015428 (VS_DUDLEY_B)
    bool VsDudleyMiddleAttacking(Side &s) {
        return Move(s, 24, 8, 2, kAsCounter) || Move(s, 18, 8, 75, kAsCounter, kNotLight) || Move(s, 14, 32, 12);
    }
    // 06015D88 (VS_DUDLEY_CS)
    bool VsDudleyFar(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 24, 8, 2, kAny, kNotLight) || Move(s, 14, 32, 13) ||
               Move(s, 23, 32, 11) || Move(s, 14, 32, 12);
    }
    // 0601651C (VS_DUDLEY_C)
    bool VsDudleyFarAttacking(Side &s) {
        return Move(s, 24, 8, 2, kAsCounter, kNotLight) || Move(s, 18, 8, 75, kAsCounter, kNotLight) ||
               Move(s, 14, 32, 12);
    }
    // (no answer: farthest (VS_DUDLEY_DS); farthest, attacking (VS_DUDLEY_D))

    // Against Necro
    // 06014412 (VS_NECRO_A)
    bool VsNecroCloseAttacking(Side &s) {
        return Move(s, 24, 8, 38) || Move(s, 15, 8, 40) || Move(s, 17, 24, 24, kAsCounter) || Move(s, 11, 32, 41);
    }
    // 060154A2 (VS_NECRO_B)
    bool VsNecroMiddleAttacking(Side &s) {
        return Move(s, 24, 8, 38) || Move(s, 15, 8, 40) || Move(s, 17, 24, 24, kAsCounter) || Move(s, 11, 32, 41);
    }
    // 06016594 (VS_NECRO_C)
    bool VsNecroFarAttacking(Side &s) {
        return Move(s, 24, 8, 38) || Move(s, 15, 8, 40, kAsCounter) || Move(s, 17, 24, 24) || Move(s, 11, 32, 41);
    }
    // (no answer: close (VS_NECRO_AS); in the middle range (VS_NECRO_BS); far (VS_NECRO_CS); farthest (VS_NECRO_DS); farthest, attacking (VS_NECRO_D))

    // Against Hugo
    // 06013D34 (VS_HUGO_AS)
    bool VsHugoClose(Side &s) {
        return Move(s, 14, 8, 61) || EarlyInMove(s, 24, 32, 5, 32767) || EarlyInMove(s, 24, 32, 4, 32767) ||
               Move(s, 19, 24, 58) || Move(s, 24, 24, 61, kAsCounter);
    }
    // 060144AC (VS_HUGO_A)
    bool VsHugoCloseAttacking(Side &s) {
        return Move(s, 19, 24, 58, kAsCounter) || EarlyInMove(s, 24, 32, 5, 32767) || EarlyInMove(s, 24, 32, 4, 32767) ||
               Move(s, 19, 24, 62, kAsCounter) || Move(s, 19, 56, 55, kAsCounter) || Move(s, 24, 24, 61, kAsCounter);
    }
    // 06014C74 (VS_HUGO_BS)
    bool VsHugoMiddle(Side &s) {
        return Move(s, 24, 8, 61) || Move(s, 24, 8, 61) || EarlyInMove(s, 24, 32, 5, 32767) ||
               EarlyInMove(s, 24, 32, 4, 32767) || Move(s, 19, 24, 58) || Move(s, 24, 24, 61, kAsCounter);
    }
    // 06015536 (VS_HUGO_B)
    bool VsHugoMiddleAttacking(Side &s) {
        return Move(s, 24, 8, 2, kAsCounter) || Move(s, 24, 8, 61) || Move(s, 19, 24, 58, kAsCounter) ||
               EarlyInMove(s, 24, 32, 5, 32767) || EarlyInMove(s, 24, 32, 4, 32767) || Move(s, 19, 24, 62, kAsCounter) ||
               Move(s, 19, 56, 55, kAsCounter) || Move(s, 24, 24, 61, kAsCounter);
    }
    // 06015E40 (VS_HUGO_CS)
    bool VsHugoFar(Side &s) {
        return Move(s, 24, 8, 61) || Move(s, 24, 8, 61) || EarlyInMove(s, 24, 32, 5, 32767) ||
               EarlyInMove(s, 24, 32, 4, 32767) || Move(s, 19, 24, 58) || Move(s, 24, 24, 61, kAsCounter);
    }
    // 06016630 (VS_HUGO_C)
    bool VsHugoFarAttacking(Side &s) {
        return Move(s, 24, 8, 2, kAsCounter) || Move(s, 24, 8, 61) || Move(s, 19, 24, 58, kAsCounter) ||
               EarlyInMove(s, 24, 32, 5, 32767) || EarlyInMove(s, 24, 32, 4, 32767) || Move(s, 19, 24, 62, kAsCounter) ||
               Move(s, 19, 56, 55, kAsCounter) || Move(s, 24, 24, 61, kAsCounter);
    }
    // (no answer: farthest (VS_HUGO_DS); farthest, attacking (VS_HUGO_D, which returns with r0 left as it was: the
    // dispatcher's table, not 0, so the chain stops there as if answered, kAnswered still clear))

    // Against Ibuki
    // 06013DDC (VS_IBUKI_AS)
    bool VsIbukiClose(Side &s) { return Move(s, 23, 8, 28, kAny, kNotLight) || Move(s, 23, 32, 25); }
    // 06014580 (VS_IBUKI_A)
    bool VsIbukiCloseAttacking(Side &s) { return Move(s, 24, 24, 26, kAsCounter); }
    // 06014D4A (VS_IBUKI_BS)
    bool VsIbukiMiddle(Side &s) { return Move(s, 23, 8, 28, kAny, kNotLight) || Move(s, 23, 32, 25); }
    // 0601564C (VS_IBUKI_B)
    bool VsIbukiMiddleAttacking(Side &s) { return Move(s, 24, 24, 26, kAsCounter); }
    // 06015F16 (VS_IBUKI_CS)
    bool VsIbukiFar(Side &s) { return Move(s, 23, 32, 25); }
    // 0601674A (VS_IBUKI_C)
    bool VsIbukiFarAttacking(Side &s) { return Move(s, 24, 24, 26, kAsCounter); }
    // (no answer: farthest (VS_IBUKI_DS); farthest, attacking (VS_IBUKI_D))

    // Against Elena
    // 06013E30 (VS_ELENA_AS)
    bool VsElenaClose(Side &s) {
        return Move(s, 23, 8, 17) || Move(s, 23, 32, 14) || Move(s, 14, 32, 15) || Move(s, 2, 48, 16);
    }
    // 060145C0 (VS_ELENA_A)
    bool VsElenaCloseAttacking(Side &s) { return Move(s, 8, 8, 18) || Move(s, 2, 48, 16) || Move(s, 15, 8, 19); }
    // 06014DA2 (VS_ELENA_BS)
    bool VsElenaMiddle(Side &s) {
        return Move(s, 23, 8, 17) || Move(s, 23, 32, 14) || Move(s, 14, 32, 15) || Move(s, 2, 48, 16);
    }
    // 06015682 (VS_ELENA_B)
    bool VsElenaMiddleAttacking(Side &s) {
        return Move(s, 8, 8, 18) || Move(s, 15, 8, 19, kAsCounter) || Move(s, 2, 48, 16);
    }
    // 06015F4A (VS_ELENA_CS)
    bool VsElenaFar(Side &s) {
        return Move(s, 23, 8, 17, kAny, kNotLight) || Move(s, 23, 32, 14) || Move(s, 14, 32, 15) || Move(s, 2, 48, 16);
    }
    // 06016780 (VS_ELENA_C)
    bool VsElenaFarAttacking(Side &s) {
        return Move(s, 8, 8, 18, kAsCounter) || Move(s, 15, 8, 19, kAsCounter) || Move(s, 2, 48, 16);
    }
    // 06016C64 (VS_ELENA_DS)
    bool VsElenaFarthest(Side &s) { return Move(s, 2, 48, 16); }
    // 06016D68 (VS_ELENA_D)
    bool VsElenaFarthestAttacking(Side &s) { return Move(s, 2, 48, 16); }

    // Against Oro
    // 06013EBE (VS_ORO_AS)
    bool VsOroClose(Side &s) { return Move(s, 23, 8, 39, kAny, kNotLight); }
    // 06014634 (VS_ORO_A)
    bool VsOroCloseAttacking(Side &s) { return Move(s, 15, 8, 44, kAsCounter); }
    // 06014E40 (VS_ORO_BS)
    bool VsOroMiddle(Side &s) { return Move(s, 23, 8, 39, kAny, kNotLight); }
    // 06015702 (VS_ORO_B)
    bool VsOroMiddleAttacking(Side &s) { return Move(s, 15, 8, 44, kAsCounter); }
    // 06015FEA (VS_ORO_CS)
    bool VsOroFar(Side &s) { return Move(s, 23, 8, 39, kAny, kNotLight); }
    // 060167F8 (VS_ORO_C)
    bool VsOroFarAttacking(Side &s) { return Move(s, 15, 8, 44, kAsCounter); }
    // (no answer: farthest (VS_ORO_DS); farthest, attacking (VS_ORO_D))

    // Against Ken
    // 06013EF4 (VS_KEN_AS)
    bool VsKenClose(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 23, 32, 8) ||
               Move(s, 23, 32, 6);
    }
    // 0601466A (VS_KEN_A)
    bool VsKenCloseAttacking(Side &s) { return Move(s, 11, 8, 0, kAsCounter); }
    // 06014E76 (VS_KEN_BS)
    bool VsKenMiddle(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 24, 8, 2) ||
               Move(s, 23, 32, 8) || Move(s, 23, 32, 6);
    }
    // 06015738 (VS_KEN_B)
    bool VsKenMiddleAttacking(Side &s) { return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2, kAsCounter); }
    // 06016020 (VS_KEN_CS)
    bool VsKenFar(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 23, 32, 8) ||
               Move(s, 23, 32, 6);
    }
    // 0601682E (VS_KEN_C)
    bool VsKenFarAttacking(Side &s) { return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2, kAsCounter); }
    // (no answer: farthest (VS_KEN_DS); farthest, attacking (VS_KEN_D))

    // Against Sean
    // 06013F8A (VS_SEAN_AS)
    bool VsSeanClose(Side &s) { return Move(s, 23, 32, 8); }
    // 060146A0 (VS_SEAN_A)
    bool VsSeanCloseAttacking(Side &s) {
        return Move(s, 24, 8, 21, kAsCounter) || Move(s, 15, 8, 32, kAsCounter) || EarlyInMove(s, 24, 0, 5, 32767) ||
               Roll(s);
    }
    // 06014F2A (VS_SEAN_BS)
    bool VsSeanMiddle(Side &s) { return Move(s, 23, 32, 8); }
    // 06015790 (VS_SEAN_B)
    bool VsSeanMiddleAttacking(Side &s) {
        return Move(s, 15, 8, 32, kAsCounter) || Move(s, 24, 8, 21, kAsCounter) || Move(s, 24, 8, 20, kAsCounter) ||
               EarlyInMove(s, 24, 0, 5, 32767) || Roll(s);
    }
    // 060160B6 (VS_SEAN_CS)
    bool VsSeanFar(Side &s) { return Move(s, 23, 32, 8); }
    // 06016886 (VS_SEAN_C)
    bool VsSeanFarAttacking(Side &s) {
        return Move(s, 15, 8, 32, kAsCounter) || Move(s, 24, 8, 21, kAsCounter) || Move(s, 24, 8, 20, kAsCounter) ||
               EarlyInMove(s, 24, 0, 5, 32767);
    }
    // (no answer: farthest (VS_SEAN_DS); farthest, attacking (VS_SEAN_D))

    // Against Urien
    // 06013FBE (VS_URIEN_AS)
    bool VsUrienClose(Side &s) { return Move(s, 24, 8, 65) || Move(s, 24, 8, 64); }
    // 0601472C (VS_URIEN_A)
    bool VsUrienCloseAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 8, 8, 65, kAsCounter) || Move(s, 24, 8, 64, kAsCounter) ||
               Move(s, 15, 8, 63, kAsCounter);
    }
    // 06014F5E (VS_URIEN_BS)
    bool VsUrienMiddle(Side &s) { return Move(s, 24, 8, 65) || Move(s, 24, 8, 64); }
    // 0601583C (VS_URIEN_B)
    bool VsUrienMiddleAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 8, 8, 65, kAsCounter) || Move(s, 24, 8, 64, kAsCounter) ||
               Move(s, 15, 8, 63, kAsCounter);
    }
    // 060160EA (VS_URIEN_CS)
    bool VsUrienFar(Side &s) { return Move(s, 24, 8, 65) || Move(s, 24, 8, 64); }
    // 06016924 (VS_URIEN_C)
    bool VsUrienFarAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 8, 8, 65, kAsCounter) || Move(s, 24, 8, 64, kAsCounter) ||
               Move(s, 15, 8, 63, kAsCounter);
    }
    // 06016DA8 (VS_URIEN_D)
    bool VsUrienFarthestAttacking(Side &s) { return Move(s, 12, 8, 63, kAsCounter, kOnly | 4); }
    // (no answer: farthest (VS_URIEN_DS))

    // Against Akuma (and Shin Akuma: the same lists)
    // 06014014 (VS_GOUKI_AS)
    bool VsAkumaClose(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter) || Move(s, 23, 32, 69);
    }
    // 060147C4 (VS_GOUKI_A)
    bool VsAkumaCloseAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 16, 32, 4, kAsCounter) || Move(s, 13, 64, 47, kAsCounter);
    }
    // 06014FB4 (VS_GOUKI_BS)
    bool VsAkumaMiddle(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2) ||
               Move(s, 16, 32, 4) || Move(s, 23, 32, 69);
    }
    // 060158D4 (VS_GOUKI_B)
    bool VsAkumaMiddleAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2, kAsCounter) || Move(s, 16, 32, 4, kAsCounter) ||
               Move(s, 16, 32, 5, kAsCounter) || Move(s, 13, 64, 47, kAsCounter);
    }
    // 06016140 (VS_GOUKI_CS)
    bool VsAkumaFar(Side &s) {
        return Move(s, 23, 8, 1, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter) || Move(s, 16, 32, 4) ||
               Move(s, 23, 32, 69);
    }
    // 060169BC (VS_GOUKI_C)
    bool VsAkumaFarAttacking(Side &s) {
        return Move(s, 11, 8, 0, kAsCounter) || Move(s, 24, 8, 2, kAsCounter) || Move(s, 16, 32, 4, kAsCounter);
    }
    // (no answer: farthest (VS_GOUKI_DS); farthest, attacking (VS_GOUKI_D))

    // Against Chun-Li
    // 0601484C (VS_CHUN_LI_A)
    bool VsChunLiCloseAttacking(Side &s) {
        return Move(s, 11, 8, 78, kAsCounter) || Move(s, 15, 8, 114, kAsCounter, kNotLight) ||
               Move(s, 21, 8, 77, kAsCounter);
    }
    // 0601506C (VS_CHUN_LI_BS)
    bool VsChunLiMiddle(Side &s) { return Move(s, 11, 8, 78, kAsCounter) || Move(s, 24, 8, 76, kAny, kNotLight); }
    // 06015998 (VS_CHUN_LI_B)
    bool VsChunLiMiddleAttacking(Side &s) {
        return Move(s, 11, 8, 78, kAsCounter) || Move(s, 24, 8, 76, kAsCounter, kNotLight) ||
               Move(s, 15, 8, 114, kAsCounter, kNotLight) || Move(s, 21, 8, 77, kAsCounter);
    }
    // 060161D6 (VS_CHUN_LI_CS)
    bool VsChunLiFar(Side &s) { return Move(s, 11, 8, 78, kAsCounter) || Move(s, 24, 8, 76); }
    // 06016A40 (VS_CHUN_LI_C)
    bool VsChunLiFarAttacking(Side &s) {
        return Move(s, 24, 8, 76, kAsCounter) || Move(s, 15, 8, 114, kAsCounter, kNotLight) ||
               Move(s, 21, 8, 77, kAsCounter) || Move(s, 11, 8, 78, kAsCounter);
    }
    // (no answer: close (VS_CHUN_LI_AS); farthest (VS_CHUN_LI_DS); farthest, attacking (VS_CHUN_LI_D))

    // Against Makoto
    // 060150C4 (VS_MAKOTO_BS)
    bool VsMakotoMiddle(Side &s) { return Move(s, 8, 8, 92); }
    // 06015A32 (VS_MAKOTO_B)
    bool VsMakotoMiddleAttacking(Side &s) { return Move(s, 8, 8, 92, kAsCounter); }
    // 0601622E (VS_MAKOTO_CS)
    bool VsMakotoFar(Side &s) { return Move(s, 8, 8, 92); }
    // 06016ADA (VS_MAKOTO_C)
    bool VsMakotoFarAttacking(Side &s) { return Move(s, 8, 8, 92, kAsCounter); }
    // (no answer: close (VS_MAKOTO_AS); close, attacking (VS_MAKOTO_A); farthest (VS_MAKOTO_DS); farthest, attacking (VS_MAKOTO_D))

    // Against Q
    // 06014094 (VS_Q_AS)
    bool VsQClose(Side &s) { return Move(s, 25, 24, 88, kAsCounter); }
    // 060148CA (VS_Q_A)
    bool VsQCloseAttacking(Side &s) { return Move(s, 25, 24, 88, kAsCounter); }
    // 060150F8 (VS_Q_BS)
    bool VsQMiddle(Side &s) { return Move(s, 25, 24, 88, kAsCounter); }
    // 06015A68 (VS_Q_B)
    bool VsQMiddleAttacking(Side &s) {
        return Move(s, 24, 8, 84, kAsCounter) || Move(s, 21, 8, 87, kAsCounter) || Move(s, 25, 24, 88, kAsCounter);
    }
    // 06016B10 (VS_Q_C)
    bool VsQFarAttacking(Side &s) { return Move(s, 24, 8, 84, kAsCounter) || Move(s, 21, 8, 87, kAsCounter); }
    // (no answer: far (VS_Q_CS); farthest (VS_Q_DS); farthest, attacking (VS_Q_D))

    // Against Twelve
    // 06014900 (VS_NO12_A)
    bool VsTwelveCloseAttacking(Side &s) { return Move(s, 17, 8, 105, kAsCounter) || Move(s, 22, 8, 107, kAsCounter); }
    // 06015AE2 (VS_NO12_B)
    bool VsTwelveMiddleAttacking(Side &s) { return Move(s, 22, 8, 107, kAsCounter) || Move(s, 17, 8, 105, kAsCounter); }
    // 06016B68 (VS_NO12_C)
    bool VsTwelveFarAttacking(Side &s) { return Move(s, 22, 8, 107, kAsCounter) || Move(s, 17, 8, 105, kAsCounter); }
    // 06016CB8 (VS_NO12_DS): the other jumping, answered 15 whatever the jump test chose
    bool VsTwelveFarthest(Side &s) {
        if (!Jump(s, 32)) {
            return false;
        }
        s.SetWord(kAnswer, 15);
        return true;
    }
    // 06016DEE (VS_NO12_D)
    bool VsTwelveFarthestAttacking(Side &s) { return Move(s, 8, 8, 105, kAsCounter) || Move(s, 15, 8, 107, kAsCounter); }
    // (no answer: close (VS_NO12_AS); in the middle range (VS_NO12_BS); far (VS_NO12_CS))

    // Against Remy
    // 060140CE (VS_REMY_AS)
    bool VsRemyClose(Side &s) { return Move(s, 23, 8, 101, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter); }
    // 06014958 (VS_REMY_A)
    bool VsRemyCloseAttacking(Side &s) { return Move(s, 11, 8, 0, kAsCounter); }
    // 06015132 (VS_REMY_BS)
    bool VsRemyMiddle(Side &s) { return Move(s, 23, 8, 101, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter); }
    // 06015B3A (VS_REMY_B)
    bool VsRemyMiddleAttacking(Side &s) { return Move(s, 11, 8, 0, kAsCounter); }
    // 0601626E (VS_REMY_CS)
    bool VsRemyFar(Side &s) { return Move(s, 23, 8, 101, kAny, kNotLight) || Move(s, 11, 8, 0, kAsCounter); }
    // 06016BC0 (VS_REMY_C)
    bool VsRemyFarAttacking(Side &s) { return Move(s, 11, 8, 0, kAsCounter); }
    // (no answer: farthest (VS_REMY_DS); farthest, attacking (VS_REMY_D))


    // --- the chains (Passive_jmp_tbl: r4 the computer) ---

    // The case a chain is in: the range on the first look of an answer (4 - 7), 0 - 3 on a later one
    int32_t LookAt(Side &s) {
        return s.Word(kRange) + static_cast<int8_t>(s.g.Read8(kPassiveMode));
    }

    // The other's character's list for the range (Check_PL_Unit_AS ...), through the game's table
    bool List(Side &s, uint32_t lists) {
        s.g.R(4) = s.me;
        s.g.Call(lists);
        return s.g.R(0) != 0;
    }

    // The first look, the range not the farthest (`far` the third): with the other's attack out, its list, then the
    // attack caught early (pose 32: of kind 3 answered 12, of kind 5 answered 7; against Hugo, of kind 3 answered 7),
    // then, but far away, move 33 of the kind 0 answered 15 as a counter attack; with it not out, its list, the other
    // after an attack, crouching (but far away) and standing
    bool FirstLook(Side &s, uint32_t attackingList, uint32_t list, bool againstHugo, bool far) {
        if (s.OtherAttacking()) {
            if (List(s, attackingList)) {
                return true;
            }
            if (againstHugo ? EarlyInMove(s, 7, 32, 3, 0) : EarlyInMove(s, 12, 32, 3, 0) || EarlyInMove(s, 7, 32, 5, 0)) {
                return true;
            }
            return !far && Move(s, 15, 0, 33, kAsCounter);
        }
        return List(s, list) || AfterAttack(s, 28) || (!far && Crouching(s, 29, 33, 32)) || Standing(s, 0x1009);
    }

    // The first look, farthest away: the list, and with the other's attack not out, standing
    bool FirstLookFarthest(Side &s) {
        if (s.OtherAttacking()) {
            return List(s, kFarthestAttackingList);
        }
        return List(s, kFarthestList) || Standing(s, 0x1009);
    }

    // 060125B4 (KEN_vs): against everyone but Gill and Hugo. A later look answers only a dash; the first, by range,
    // FirstLook, a jump (but farthest away) and a taunt.
    bool AgainstMost(Side &s) {
        switch (LookAt(s)) {
        case 0: case 1: case 2: case 3: return Dash(s, 1);
        case 4: return FirstLook(s, kCloseAttackingList, kCloseList, false, false) || Jump(s, 16) || Taunt(s);
        case 5: return FirstLook(s, kMiddleAttackingList, kMiddleList, false, false) || Jump(s, 32) || Taunt(s);
        case 6: return FirstLook(s, kFarAttackingList, kFarList, false, true) || Jump(s, 64) || Taunt(s);
        default: return FirstLookFarthest(s) || Taunt(s);
        }
    }

    // 060128FA (HUGO_vs): against Hugo. As against the others, but one attack caught early, and no taunt.
    bool AgainstHugo(Side &s) {
        switch (LookAt(s)) {
        case 0: case 1: case 2: case 3: return Dash(s, 1);
        case 4: return FirstLook(s, kCloseAttackingList, kCloseList, true, false) || Jump(s, 16);
        case 5: return FirstLook(s, kMiddleAttackingList, kMiddleList, true, false) || Jump(s, 32);
        case 6: return FirstLook(s, kFarAttackingList, kFarList, true, true) || Jump(s, 64);
        default: return FirstLookFarthest(s);
        }
    }

    // 06012BA4 (GILL_vs): against Gill. As against the others, with no taunt, and farthest away the other crouching
    // answered too (answer 7).
    bool AgainstGill(Side &s) {
        switch (LookAt(s)) {
        case 0: case 1: case 2: case 3: return Dash(s, 1);
        case 4: return FirstLook(s, kCloseAttackingList, kCloseList, false, false) || Jump(s, 16);
        case 5: return FirstLook(s, kMiddleAttackingList, kMiddleList, false, false) || Jump(s, 32);
        case 6: return FirstLook(s, kFarAttackingList, kFarList, false, true) || Jump(s, 64);
        default: return FirstLookFarthest(s) || (!s.OtherAttacking() && Crouching(s, 7, 33, 32));
        }
    }

    // --- the lifts ---

    // 06012588 (Ck_Passive_Term, r4 the computer): kAnswered cleared, the chain for the other's character run (from
    // the game's table), and kAnswered the result
    bool ChooseAnswer(Guest &g) {
        g.Write8(kAnswered, 0);
        const uint32_t other = g.Read32(g.R(4) + kOpponent);
        const int32_t character = static_cast<int16_t>(g.Read16(other + kCharacter));
        g.Call(g.Read32(kChains + static_cast<uint32_t>(character * 4)));
        g.R(0) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(g.Read8(kAnswered))));
        g.Return(20);
        return true;
    }

    // 06013A86 (Check_PL_Unit_AS) and the seven like it, a table each: the other's character's list from the table,
    // run (the routine jumps to it: what the list leaves is the routine's)
    template <uint32_t kTable>
    bool RunList(Guest &g) {
        const uint32_t other = g.Read32(g.R(4) + kOpponent);
        const int32_t character = static_cast<int16_t>(g.Read16(other + kCharacter));
        // (the registers as the jump leaves them: a list with no test returns at once, r0 still the table)
        g.R(0) = kTable;
        g.R(1) = static_cast<uint32_t>(character * 4);
        g.R(2) = other;
        g.R(3) = g.Read32(kTable + static_cast<uint32_t>(character * 4));
        g.Call(g.R(3));
        g.Return(10);
        return true;
    }

    // A chain or a list (r4 the computer; the other its kOpponent): not 0 in r0 when it answered (what the last test
    // or list left there)
    template <bool (*F)(Side &)>
    bool Listed(Guest &g) {
        Side s(g, g.R(4));
        F(s);
        g.Return(60);
        return true;
    }

    // The tests called by the game's code: r4 the computer, r5 the other, r6 and r7 the first arguments, the rest
    // longs on the stack (their low byte or word)
    int32_t StackByte(Guest &g, int n) {
        return static_cast<int8_t>(g.Read8(g.R(15) + static_cast<uint32_t>(n * 4 + 3)));
    }
    int16_t StackWord(Guest &g, int n) {
        return static_cast<int16_t>(g.Read16(g.R(15) + static_cast<uint32_t>(n * 4 + 2)));
    }
    int32_t Arg(Guest &g, unsigned n) {
        return static_cast<int32_t>(g.R(n));
    }
    template <bool (*F)(Side &)>
    bool TestLift(Guest &g) {
        Side s(g, g.R(4), g.R(5));
        F(s);
        g.Return(20);
        return true;
    }
    template <bool (*F)(Side &, int32_t)>
    bool TestLift1(Guest &g) {
        Side s(g, g.R(4), g.R(5));
        F(s, Arg(g, 6));
        g.Return(30);
        return true;
    }
    bool MovingAwayLift(Guest &g) {
        g.R(0) = MovingAway(g, g.R(4), g.R(5)) ? 1 : 0;
        g.Return(12);
        return true;
    }
    bool MoveLift(Guest &g) {
        Side s(g, g.R(4), g.R(5));
        Move(s, Arg(g, 6), Arg(g, 7), StackByte(g, 0), StackWord(g, 1), StackWord(g, 2));
        g.Return(40);
        return true;
    }
    bool InPoseLift(Guest &g) {
        Side s(g, g.R(4), g.R(5));
        InPose(s, Arg(g, 6), Arg(g, 7), StackByte(g, 0), StackByte(g, 1));
        g.Return(16);
        return true;
    }
    bool EarlyInMoveLift(Guest &g) {
        Side s(g, g.R(4), g.R(5));
        EarlyInMove(s, Arg(g, 6), Arg(g, 7), StackByte(g, 0), StackWord(g, 1));
        g.Return(40);
        return true;
    }
    bool CrouchingLift(Guest &g) {
        Side s(g, g.R(4), g.R(5));
        Crouching(s, Arg(g, 6), Arg(g, 7), StackByte(g, 0));
        g.Return(40);
        return true;
    }

    // (free: r1-r4 and T; r0 and r5-r7 are left as the game's code leaves them, for 0x06012588's callers)
    constexpr uint32_t kFree = 0x1001E;
    // (The chain against Gill and Gill's lists are met in checklifts.sh's training runs, Gill chosen. The 41 lists with
    // no test, which return at once, are left the game's: nothing to write.)
    const Lift kLifts[] = {
        {"ComputerChooseAnswer", 0x06012588, 0x2C, 0x0CD11B638A690A29ull, ChooseAnswer, true, kFree},
        {"ComputerAnswerAgainstMost", 0x060125B4, 0x346, 0x3FE1C2387D578A74ull, Listed<AgainstMost>, true, kFree},
        {"ComputerAnswerAgainstHugo", 0x060128FA, 0x284, 0x988BB936ED299AE0ull, Listed<AgainstHugo>, true, kFree},
        {"ComputerAnswerAgainstGill", 0x06012BA4, 0x2E0, 0x7D573D6ED690610Full, Listed<AgainstGill>, true, kFree},
        {"ComputerCloseList", 0x06013A86, 0x16, 0xF6644E14F2A8C105ull, RunList<kCloseLists>, true, kFree},
        {"ComputerCloseAttackingList", 0x0601412C, 0x16, 0xA9F6D97D952C5355ull, RunList<kCloseAttackingLists>, true, kFree},
        {"ComputerMiddleList", 0x06014994, 0x16, 0x2A17665803B96930ull, RunList<kMiddleLists>, true, kFree},
        {"ComputerMiddleAttackingList", 0x0601518A, 0x16, 0x4C87C7B376D38318ull, RunList<kMiddleAttackingLists>, true, kFree},
        {"ComputerFarList", 0x06015B74, 0x16, 0x575816060D7713D2ull, RunList<kFarLists>, true, kFree},
        {"ComputerFarAttackingList", 0x060162C6, 0x16, 0xB4E900E0903074EEull, RunList<kFarAttackingLists>, true, kFree},
        {"ComputerFarthestList", 0x06016BFC, 0x16, 0x04D21CC88E5E5728ull, RunList<kFarthestLists>, true, kFree},
        {"ComputerFarthestAttackingList", 0x06016CEA, 0x16, 0x183A788155F7B37Cull, RunList<kFarthestAttackingLists>, true, kFree},
        {"ComputerAnswerMove", 0x06012EA0, 0xF4, 0x8EDD3FB357B58D39ull, MoveLift, true, kFree},
        {"ComputerMovingAway", 0x06012FB0, 0x34, 0xAF4D15248AA604C2ull, MovingAwayLift, true, kFree},
        {"ComputerAnswerJump", 0x06013034, 0x118, 0x69B79A37DC243B65ull, TestLift1<Jump>, true, kFree},
        {"ComputerAnswerRoll", 0x06013212, 0x40, 0xEB067D5D8C13FBA5ull, TestLift<Roll>, true, kFree},
        {"ComputerAnswerTaunt", 0x06013252, 0x2E, 0xB67E7C55FC68CDE7ull, TestLift<Taunt>, true, kFree},
        {"ComputerAnswerInPose", 0x06013280, 0x46, 0xABB54ADF9B9D9421ull, InPoseLift, true, 0x10018},
        {"ComputerAnswerDash", 0x060132D8, 0x2E, 0x0295F9F03C27ABDDull, TestLift1<Dash>, true, kFree},
        {"ComputerAnswerEarly", 0x06013306, 0xD6, 0x40AC66CE3D8CAB30ull, EarlyInMoveLift, true, kFree},
        {"ComputerAnswerStanding", 0x06013460, 0x6C, 0xF1C4D60EE0D1335Bull, TestLift1<Standing>, true, kFree},
        {"ComputerAnswerCrouching", 0x060135DE, 0xC6, 0x7BBF8CA940A06872ull, CrouchingLift, true, kFree},
        {"ComputerAnswerAfterAttack", 0x06013930, 0xCE, 0x386FAB4EAB21FF60ull, TestLift1<AfterAttack>, true, kFree},
        {"ComputerPoseAndKind", 0x06013404, 0x22, 0x747598DABD9BAAFCull, PoseAndKindLift, true},
        {"ComputerAnswerCrossChop", 0x06013A18, 0x6E, 0x4FDDFF81F2D7DE4Eull, TestLift1<CrossChop>, true, kFree},
        // the lists, against each character
        {"ComputerVsGillClose", 0x06013A9C, 0x52, 0xC05138F6A44A5425ull, Listed<VsGillClose>, true, kFree},
        {"ComputerVsGillCloseAttacking", 0x06014142, 0x98, 0xFEA30A70C6B1285Dull, Listed<VsGillCloseAttacking>, true, kFree},
        {"ComputerVsGillMiddle", 0x060149AA, 0x56, 0x49E7C254868FA9EBull, Listed<VsGillMiddle>, true, kFree},
        {"ComputerVsGillMiddleAttacking", 0x060151AC, 0x98, 0x1C8F466ADA04AE64ull, Listed<VsGillMiddleAttacking>, true, kFree},
        {"ComputerVsGillFar", 0x06015B8A, 0x56, 0x213881D9E012A6C1ull, Listed<VsGillFar>, true, kFree},
        {"ComputerVsGillFarAttacking", 0x060162DC, 0x98, 0x8776A6BE11383D84ull, Listed<VsGillFarAttacking>, true, kFree},
        {"ComputerVsAlexClose", 0x06013AEE, 0x9C, 0xB5FF81D15DD24499ull, Listed<VsAlexClose>, true, kFree},
        {"ComputerVsAlexCloseAttacking", 0x060141DA, 0xD2, 0xFC2F34021EB58DEAull, Listed<VsAlexCloseAttacking>, true, kFree},
        {"ComputerVsAlexMiddle", 0x06014A00, 0x84, 0x38200ED2168500ACull, Listed<VsAlexMiddle>, true, kFree},
        {"ComputerVsAlexMiddleAttacking", 0x06015244, 0xCA, 0x72F7DA3BAE7B970Cull, Listed<VsAlexMiddleAttacking>, true, kFree},
        {"ComputerVsAlexFar", 0x06015BE0, 0x74, 0x1F1B34112B455324ull, Listed<VsAlexFar>, true, kFree},
        {"ComputerVsAlexFarAttacking", 0x06016380, 0xAC, 0x1138D2137A1F398Aull, Listed<VsAlexFarAttacking>, true, kFree},
        {"ComputerVsRyuClose", 0x06013B8A, 0x90, 0xFA88A665C94E8715ull, Listed<VsRyuClose>, true, kFree},
        {"ComputerVsRyuCloseAttacking", 0x060142AC, 0x96, 0x58341CCB54BD1A76ull, Listed<VsRyuCloseAttacking>, true, kFree},
        {"ComputerVsRyuMiddle", 0x06014A84, 0xE4, 0x601649D280885399ull, Listed<VsRyuMiddle>, true, kFree},
        {"ComputerVsRyuMiddleAttacking", 0x0601530E, 0x98, 0xBFE667830FA1C98Eull, Listed<VsRyuMiddleAttacking>, true, kFree},
        {"ComputerVsRyuFar", 0x06015C54, 0xC0, 0x2C86106B0716D38Dull, Listed<VsRyuFar>, true, kFree},
        {"ComputerVsRyuFarAttacking", 0x0601642C, 0x98, 0x36BBD4D33417D5F7ull, Listed<VsRyuFarAttacking>, true, kFree},
        {"ComputerVsRyuFarthest", 0x06016C1A, 0x36, 0x45A733049DB5F7E4ull, Listed<VsRyuFarthest>, true, kFree},
        {"ComputerVsRyuFarthestAttacking", 0x06016D08, 0x36, 0x323C6AFAEFB1B974ull, Listed<VsRyuFarthestAttacking>, true, kFree},
        {"ComputerVsYunClose", 0x06013C1A, 0x6E, 0x082EB45BF05FD577ull, Listed<VsYunClose>, true, kFree},
        {"ComputerVsYunCloseAttacking", 0x06014342, 0x58, 0xEE04626F8F0A469Aull, Listed<VsYunCloseAttacking>, true, kFree},
        {"ComputerVsYunMiddle", 0x06014B68, 0x74, 0x6EF51EF75313CEBAull, Listed<VsYunMiddle>, true, kFree},
        {"ComputerVsYunMiddleAttacking", 0x060153A6, 0x78, 0x0E3C3D34931F428Bull, Listed<VsYunMiddleAttacking>, true, kFree},
        {"ComputerVsYunFar", 0x06015D14, 0x74, 0x8B23BE58F2713B32ull, Listed<VsYunFar>, true, kFree},
        {"ComputerVsYunFarAttacking", 0x060164C4, 0x58, 0x3E017EEDC46D4496ull, Listed<VsYunFarAttacking>, true, kFree},
        {"ComputerVsDudleyClose", 0x06013C88, 0xA8, 0x3C5A1DBFF84575B8ull, Listed<VsDudleyClose>, true, kFree},
        {"ComputerVsDudleyCloseAttacking", 0x0601439A, 0x78, 0x2798BC68010A0D13ull, Listed<VsDudleyCloseAttacking>, true, kFree},
        {"ComputerVsDudleyMiddle", 0x06014BDC, 0x94, 0x33806D92F1376823ull, Listed<VsDudleyMiddle>, true, kFree},
        {"ComputerVsDudleyMiddleAttacking", 0x06015428, 0x7A, 0x2FD1346E205144BFull, Listed<VsDudleyMiddleAttacking>, true, kFree},
        {"ComputerVsDudleyFar", 0x06015D88, 0xB4, 0x1100C0D4A6122A2Dull, Listed<VsDudleyFar>, true, kFree},
        {"ComputerVsDudleyFarAttacking", 0x0601651C, 0x78, 0x37F1AD1833CD4F46ull, Listed<VsDudleyFarAttacking>, true, kFree},
        {"ComputerVsNecroCloseAttacking", 0x06014412, 0x94, 0xEF5E0ACEA06213F1ull, Listed<VsNecroCloseAttacking>, true, kFree},
        {"ComputerVsNecroMiddleAttacking", 0x060154A2, 0x94, 0xB759051FD731B93Bull, Listed<VsNecroMiddleAttacking>, true, kFree},
        {"ComputerVsNecroFarAttacking", 0x06016594, 0x9C, 0x41700D25A326F229ull, Listed<VsNecroFarAttacking>, true, kFree},
        {"ComputerVsHugoClose", 0x06013D34, 0xA8, 0xBEF703124C3761C3ull, Listed<VsHugoClose>, true, kFree},
        {"ComputerVsHugoCloseAttacking", 0x060144AC, 0xD4, 0x189B87F06C8E2150ull, Listed<VsHugoCloseAttacking>, true, kFree},
        {"ComputerVsHugoMiddle", 0x06014C74, 0xD6, 0x61FD12B1147F2B87ull, Listed<VsHugoMiddle>, true, kFree},
        {"ComputerVsHugoMiddleAttacking", 0x06015536, 0x116, 0x644ED6F3096B48BEull, Listed<VsHugoMiddleAttacking>, true, kFree},
        {"ComputerVsHugoFar", 0x06015E40, 0xD6, 0x630ECC6FE9B3554Bull, Listed<VsHugoFar>, true, kFree},
        {"ComputerVsHugoFarAttacking", 0x06016630, 0x11A, 0x457CF2D7F487AC17ull, Listed<VsHugoFarAttacking>, true, kFree},
        {"ComputerVsIbukiClose", 0x06013DDC, 0x54, 0xC7DE2A53CB4E0DBBull, Listed<VsIbukiClose>, true, kFree},
        {"ComputerVsIbukiCloseAttacking", 0x06014580, 0x36, 0x28ED18D08EAA8D0Bull, Listed<VsIbukiCloseAttacking>, true, kFree},
        {"ComputerVsIbukiMiddle", 0x06014D4A, 0x58, 0x4B7535AD62A60048ull, Listed<VsIbukiMiddle>, true, kFree},
        {"ComputerVsIbukiMiddleAttacking", 0x0601564C, 0x36, 0x6163718AA474F4B7ull, Listed<VsIbukiMiddleAttacking>, true, kFree},
        {"ComputerVsIbukiFar", 0x06015F16, 0x34, 0xFECC1A76F3AE106Cull, Listed<VsIbukiFar>, true, kFree},
        {"ComputerVsIbukiFarAttacking", 0x0601674A, 0x36, 0x0DDA93D820DDA612ull, Listed<VsIbukiFarAttacking>, true, kFree},
        {"ComputerVsElenaClose", 0x06013E30, 0x8E, 0x9C3E4234D9907094ull, Listed<VsElenaClose>, true, kFree},
        {"ComputerVsElenaCloseAttacking", 0x060145C0, 0x74, 0x06717822C20D6E72ull, Listed<VsElenaCloseAttacking>, true, kFree},
        {"ComputerVsElenaMiddle", 0x06014DA2, 0x9E, 0x54CA3D08E8CC47CBull, Listed<VsElenaMiddle>, true, kFree},
        {"ComputerVsElenaMiddleAttacking", 0x06015682, 0x80, 0x8AAD78EDE586916Full, Listed<VsElenaMiddleAttacking>, true, kFree},
        {"ComputerVsElenaFar", 0x06015F4A, 0xA0, 0x268582930C2CC5D5ull, Listed<VsElenaFar>, true, kFree},
        {"ComputerVsElenaFarAttacking", 0x06016780, 0x78, 0xDB790D930EB9A4BDull, Listed<VsElenaFarAttacking>, true, kFree},
        {"ComputerVsElenaFarthest", 0x06016C64, 0x34, 0x77FC3340450ED534ull, Listed<VsElenaFarthest>, true, kFree},
        {"ComputerVsElenaFarthestAttacking", 0x06016D68, 0x34, 0x8503E78CEC1A2F98ull, Listed<VsElenaFarthestAttacking>, true, kFree},
        {"ComputerVsOroClose", 0x06013EBE, 0x36, 0x114B6913C2CA4E3Dull, Listed<VsOroClose>, true, kFree},
        {"ComputerVsOroCloseAttacking", 0x06014634, 0x36, 0xA576B50D58B71FE8ull, Listed<VsOroCloseAttacking>, true, kFree},
        {"ComputerVsOroMiddle", 0x06014E40, 0x36, 0x44771F445F8AD383ull, Listed<VsOroMiddle>, true, kFree},
        {"ComputerVsOroMiddleAttacking", 0x06015702, 0x36, 0x9D024799FE781214ull, Listed<VsOroMiddleAttacking>, true, kFree},
        {"ComputerVsOroFar", 0x06015FEA, 0x36, 0x79A1B47376801E79ull, Listed<VsOroFar>, true, kFree},
        {"ComputerVsOroFarAttacking", 0x060167F8, 0x36, 0xF45476B2D7FD9AB8ull, Listed<VsOroFarAttacking>, true, kFree},
        {"ComputerVsKenClose", 0x06013EF4, 0x96, 0x68A17A9E27AA97C4ull, Listed<VsKenClose>, true, kFree},
        {"ComputerVsKenCloseAttacking", 0x0601466A, 0x36, 0x50C5F60AF4D7F2B1ull, Listed<VsKenCloseAttacking>, true, kFree},
        {"ComputerVsKenMiddle", 0x06014E76, 0xB4, 0x1B7AA1E6A8B7F694ull, Listed<VsKenMiddle>, true, kFree},
        {"ComputerVsKenMiddleAttacking", 0x06015738, 0x58, 0xF64B51A70EFEC456ull, Listed<VsKenMiddleAttacking>, true, kFree},
        {"ComputerVsKenFar", 0x06016020, 0x96, 0xB9B05FA3D95EE094ull, Listed<VsKenFar>, true, kFree},
        {"ComputerVsKenFarAttacking", 0x0601682E, 0x58, 0x9F3AEE08C5D645A0ull, Listed<VsKenFarAttacking>, true, kFree},
        {"ComputerVsSeanClose", 0x06013F8A, 0x34, 0xC5071640ECABA514ull, Listed<VsSeanClose>, true, kFree},
        {"ComputerVsSeanCloseAttacking", 0x060146A0, 0x8C, 0xE1159E2CCEA5CDE1ull, Listed<VsSeanCloseAttacking>, true, kFree},
        {"ComputerVsSeanMiddle", 0x06014F2A, 0x34, 0xD9B2CB2B9FDEDAB5ull, Listed<VsSeanMiddle>, true, kFree},
        {"ComputerVsSeanMiddleAttacking", 0x06015790, 0xAC, 0xF2F204EB32DD7074ull, Listed<VsSeanMiddleAttacking>, true, kFree},
        {"ComputerVsSeanFar", 0x060160B6, 0x34, 0xC72FC90227F9A540ull, Listed<VsSeanFar>, true, kFree},
        {"ComputerVsSeanFarAttacking", 0x06016886, 0x9E, 0x0428817AAD25DE33ull, Listed<VsSeanFarAttacking>, true, kFree},
        {"ComputerVsUrienClose", 0x06013FBE, 0x56, 0xD61CA91E34B2CA21ull, Listed<VsUrienClose>, true, kFree},
        {"ComputerVsUrienCloseAttacking", 0x0601472C, 0x98, 0xE9C0A5000F042BD5ull, Listed<VsUrienCloseAttacking>, true, kFree},
        {"ComputerVsUrienMiddle", 0x06014F5E, 0x56, 0xEAFE5060E2B9D7D7ull, Listed<VsUrienMiddle>, true, kFree},
        {"ComputerVsUrienMiddleAttacking", 0x0601583C, 0x98, 0xE9C0A5000F042BD5ull, Listed<VsUrienMiddleAttacking>, true, kFree},
        {"ComputerVsUrienFar", 0x060160EA, 0x56, 0xD570588E20BEC141ull, Listed<VsUrienFar>, true, kFree},
        {"ComputerVsUrienFarAttacking", 0x06016924, 0x98, 0x64E22167B67A2881ull, Listed<VsUrienFarAttacking>, true, kFree},
        {"ComputerVsUrienFarthestAttacking", 0x06016DA8, 0x36, 0x102B61C45DE1B002ull, Listed<VsUrienFarthestAttacking>, true, kFree},
        {"ComputerVsAkumaClose", 0x06014014, 0x78, 0x2F41D61AA7D9DAABull, Listed<VsAkumaClose>, true, kFree},
        {"ComputerVsAkumaCloseAttacking", 0x060147C4, 0x88, 0x46B97491757B38AAull, Listed<VsAkumaCloseAttacking>, true, kFree},
        {"ComputerVsAkumaMiddle", 0x06014FB4, 0xB4, 0x8BB1C81B86E0331Aull, Listed<VsAkumaMiddle>, true, kFree},
        {"ComputerVsAkumaMiddleAttacking", 0x060158D4, 0xC4, 0xB819DC59151F7606ull, Listed<VsAkumaMiddleAttacking>, true, kFree},
        {"ComputerVsAkumaFar", 0x06016140, 0x96, 0xA2DF30512B4F479Cull, Listed<VsAkumaFar>, true, kFree},
        {"ComputerVsAkumaFarAttacking", 0x060169BC, 0x84, 0xDF404B47FD11D012ull, Listed<VsAkumaFarAttacking>, true, kFree},
        {"ComputerVsChunLiCloseAttacking", 0x0601484C, 0x7A, 0xD8460096B0CE394Cull, Listed<VsChunLiCloseAttacking>, true, kFree},
        {"ComputerVsChunLiMiddle", 0x0601506C, 0x58, 0x4D1C1C5CD487A615ull, Listed<VsChunLiMiddle>, true, kFree},
        {"ComputerVsChunLiMiddleAttacking", 0x06015998, 0x9A, 0xD7CDA60C3C62275Full, Listed<VsChunLiMiddleAttacking>, true, kFree},
        {"ComputerVsChunLiFar", 0x060161D6, 0x58, 0x80078760C0D4F43Bull, Listed<VsChunLiFar>, true, kFree},
        {"ComputerVsChunLiFarAttacking", 0x06016A40, 0x9A, 0xFE0048303A026623ull, Listed<VsChunLiFarAttacking>, true, kFree},
        {"ComputerVsMakotoMiddle", 0x060150C4, 0x34, 0x8B11423DFE853BD8ull, Listed<VsMakotoMiddle>, true, kFree},
        {"ComputerVsMakotoMiddleAttacking", 0x06015A32, 0x36, 0x77540227F8BA7783ull, Listed<VsMakotoMiddleAttacking>, true, kFree},
        {"ComputerVsMakotoFar", 0x0601622E, 0x38, 0x4DCBC039C85ECB3Bull, Listed<VsMakotoFar>, true, kFree},
        {"ComputerVsMakotoFarAttacking", 0x06016ADA, 0x36, 0x95893ED688EC47CBull, Listed<VsMakotoFarAttacking>, true, kFree},
        {"ComputerVsQClose", 0x06014094, 0x36, 0x2331756A6A84C65Full, Listed<VsQClose>, true, kFree},
        {"ComputerVsQCloseAttacking", 0x060148CA, 0x36, 0xFC07A8EDB15CC3DBull, Listed<VsQCloseAttacking>, true, kFree},
        {"ComputerVsQMiddle", 0x060150F8, 0x36, 0xFC6DE2B4DD7BE58Aull, Listed<VsQMiddle>, true, kFree},
        {"ComputerVsQMiddleAttacking", 0x06015A68, 0x7A, 0x733E73DC4E35A206ull, Listed<VsQMiddleAttacking>, true, kFree},
        {"ComputerVsQFarAttacking", 0x06016B10, 0x58, 0x85604E545D990043ull, Listed<VsQFarAttacking>, true, kFree},
        {"ComputerVsTwelveCloseAttacking", 0x06014900, 0x58, 0xEAAECCB6A313FAD2ull, Listed<VsTwelveCloseAttacking>, true, kFree},
        {"ComputerVsTwelveMiddleAttacking", 0x06015AE2, 0x58, 0x8C5995930AEFFF80ull, Listed<VsTwelveMiddleAttacking>, true, kFree},
        {"ComputerVsTwelveFarAttacking", 0x06016B68, 0x58, 0x319BA3FC33944E66ull, Listed<VsTwelveFarAttacking>, true, kFree},
        {"ComputerVsTwelveFarthest", 0x06016CB8, 0x2E, 0x8102CB67BCD8A81Bull, Listed<VsTwelveFarthest>, true, kFree},
        {"ComputerVsTwelveFarthestAttacking", 0x06016DEE, 0x58, 0xEAC2CF57F9853C25ull, Listed<VsTwelveFarthestAttacking>, true, kFree},
        {"ComputerVsRemyClose", 0x060140CE, 0x5E, 0x4FD6EBCB45C2A1F3ull, Listed<VsRemyClose>, true, kFree},
        {"ComputerVsRemyCloseAttacking", 0x06014958, 0x36, 0xFEFCC40D60EC1B49ull, Listed<VsRemyCloseAttacking>, true, kFree},
        {"ComputerVsRemyMiddle", 0x06015132, 0x58, 0xFAE6FD0EB4C79F27ull, Listed<VsRemyMiddle>, true, kFree},
        {"ComputerVsRemyMiddleAttacking", 0x06015B3A, 0x36, 0x3EDFD2876B38B3E5ull, Listed<VsRemyMiddleAttacking>, true, kFree},
        {"ComputerVsRemyFar", 0x0601626E, 0x58, 0xE685773BF8EBCD6Full, Listed<VsRemyFar>, true, kFree},
        {"ComputerVsRemyFarAttacking", 0x06016BC0, 0x36, 0xFEFCC40D60EC1B49ull, Listed<VsRemyFarAttacking>, true, kFree},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerAnswers() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
