// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer player each frame (0x06006040, called from the fighter's own routine at 0x06119610) and its first steps: what it sees of the fight (0x060060B4 and what it calls), and the routines it calls last.
// The computer's design, its record's fields and the other parts: computer.h.
//
// How the computer plays a fighter, as far as this part goes. Once a frame the fighter's routine (0x06119610) asks the
// computer for the lever and buttons it holds (0x06006040, the answer a word in r0: bits 0-3 the lever, up 1, down 2,
// one way 4, the other 8; the buttons above); the fighter takes them as a player's. Before it decides, the computer
// looks at the fight (0x060060B4): the gap between the two (along the floor), which of four ranges it is in (the
// character's own three limits), and whether the other is attacking, counting the other's attacks and keeping the
// last four kinds. Then it does what its mode says: a mode for each side (the first of four words, kMode; its step in
// the second), sixteen of them, each a routine (the table at 0x0613E504):
//    0  starting: everything of the computer's cleared, the character's ranges and timers set; then 1
//    1  free: nothing under way; a plan (Select_Active, 0x060104B4) is chosen and it is 2
//    2  active: the plan played (by the character: computer_tactics.cpp, computer_moves.cpp)
//    3  before following up: waits a moment (the step's frames) then picks a follow-up by range; then 4
//    4  following up (the character's follow-up routine)
//    5  before answering: waits, ready to guard; then 6
//    6  answering the other (the character's answer)
//    7  guarding, 8 against a projectile, 9 guarding one, 10 hit, 11 in the air after a hit, 12 parrying (Flip:
//       3rd Strike's blocking, the lever forward as the attack hits), 13 caught in a throw: not here (computer_tactics.cpp)
//   14  lying down: waits to get up
//   15  throwing: mashes the throw, as a player would
// Every mode but 0 first looks at what has happened to the fighter (CheckHit, CheckCaught, CheckParry, CheckGuard):
// hit, thrown, knocked down or attacked, it changes mode at once, and the frame is the new mode's (the main step runs
// the mode once more when kRunAgain says so).
//
// After the mode, the lever the computer chose is kept: for how long it has been held down, forward, back (kHeld, for
// the commands that need a charge) and the last twenty frames' (kLastLevers).
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, Com_Pl.c, Com_Sub.c), where they are known: this is the arcade's build of the same code.
// (Names only: that project is AGPL-3.0, and none of its code is here; what the routines do was read from the arcade's
// own program and checked call by call against it.)
#include "computer.h"

#include <iterator>

namespace cps3recomp {

namespace {
    // --- the fighter's record (r4: the computer's; its kOpponent, the other's) ---
    constexpr uint32_t kSide = 0x08;          // word: 0 player 1, 1 player 2 (wu.id); the computer's tables are by it
    constexpr uint32_t kMovesFaceRight = 0x0B; // byte: the way its moves face (rl_waza): forward is 8 on the lever, else 4
    constexpr uint32_t kOpponent = 0x0C;      // long: the other fighter's record (target_adrs)
    constexpr uint32_t kDoing = 0x26;         // word: what the fighter is doing (routine_no[1]): 0 free, 1 hit, 2
                                              // throwing, 3 thrown
    constexpr uint32_t kDoingHow = 0x28;      // word: its detail (routine_no[2]): for 0, which action
    constexpr uint32_t kX = 0x64, kY = 0x68;  // words: where it is, along the floor and up from it
    constexpr uint32_t kPose = 0x20E;         // byte: its pose (pat_status): 0x26 lying on the floor
    constexpr uint32_t kAttackKind = 0x20F;   // byte: the kind of its attack (kind_of_waza)
    constexpr uint32_t kAttackReach = 0x210;  // byte: how far its attack reaches, a class (hit_range: kReachByClass)
    constexpr uint32_t kCharacter = 0x3C0;    // word: the character (player_number); byte 0x3C1 its low byte
    constexpr uint32_t kAttacking = 0x428;    // byte: not 0 while it attacks (caution_flag: the other's, to the computer)

    // --- the computer's tables: two of each, by kSide (a byte, a word, or more each) ---
    constexpr uint32_t kMode = 0x02015572;        // words, four a side (CP_No): the mode, its step, and two more words
                                                  // the mode uses (the third: a mash's way, ComRapid)
    constexpr uint32_t kPlanPlace = 0x02015582;   // words, eight a side (CP_Index): where the mode's plan has got to
    constexpr uint32_t kPattern = 0x020155A2;     // word: the plan chosen (Pattern_Index)
    constexpr uint32_t kTimer = 0x020155A6;       // word: frames to wait (Timer_00)
    constexpr uint32_t kTimerReload = 0x020155AA; // word: a second timer, the first's reload when mashing (Timer_01)
    constexpr uint32_t kGap = 0x020155B2;         // word: the gap between the two along the floor (PL_Distance)
    constexpr uint32_t kRange = 0x020155B6;       // word: which range the gap is in, 0 close - 3 far (Area_Number)
    constexpr uint32_t kLever = 0x020155BA;       // word: the lever and buttons chosen this frame (Lever_Buff)
    constexpr uint32_t kRanges = 0x02015602;      // words, three a side: the ranges' far limits (Separate_Area)
    constexpr uint32_t kOtherLying = 0x02015624;  // byte: the other is lying on the floor (Lie_Flag)
    constexpr uint32_t kOtherAttacking = 0x0201562A; // byte: the other is attacking (Attack_Flag)
    constexpr uint32_t kWidth = 0x02015632;       // word: the computer's character's width (Com_Width_Data)
    constexpr uint32_t kHeldDownSince = 0x0201563E; // word (Lever_Squat)
    constexpr uint32_t kShellRanges = 0x02015744; // words, three a side: the ranges against a projectile (Shell_Separate_Area)
    constexpr uint32_t kAttacksSeen = 0x02015750; // word: the other's attacks counted (Attack_Counter)
    constexpr uint32_t kLastAttackSeen = 0x02015754; // word (Last_Attack_Counter): -1 at the start
    constexpr uint32_t kStandFor = 0x0201575C;    // byte: frames to stand before the next plan (Standing_Master_Timer)
    constexpr uint32_t kCrouchFor = 0x02015766;   // byte: frames to crouch before the next (Squat_Master_Timer)
    constexpr uint32_t kLastLevers = 0x0201576E;  // words, twenty a side: the levers of the last twenty frames, the
                                                  // newest first (Resume_Lever)
    constexpr uint32_t kTurnTimer = 0x020157BE;   // byte (Turn_Over_Timer): 1 at the start
    constexpr uint32_t kHitSeen = 0x020157CA;     // byte (Receive_Flag): cleared as it is hit
    constexpr uint32_t kRunAgain = 0x020157CC;    // byte: the mode changed: run the new one this frame too (Disposal_Again)
    constexpr uint32_t kHeld = 0x02015806;        // words, three a side: frames the lever has been held down, forward,
                                                  // back (Lever_Store)
    constexpr uint32_t kOtherAttackKind = 0x02015829; // byte: the kind of the other's attack, the last (Type_of_Attack)
    constexpr uint32_t kLeverNow = 0x0201582C;    // word: the lever to hold while nothing else is chosen (Lever_LR)
    constexpr uint32_t kAttackKinds = 0x0201583E; // bytes, four a side: the kinds of the other's last four attacks
                                                  // (Attack_Count_Buff); 0-3 when forgotten
    constexpr uint32_t kAttackKindPlace = 0x02015846; // byte: where the next goes in them (Attack_Count_Index)
    constexpr uint32_t kGuardedAttack = 0x02015848; // word: kAttacksSeen when it last decided not to guard (Guard_Counter)
    constexpr uint32_t kProjectiles = 0x02016ADC; // byte (Bullet_No)
    constexpr uint32_t kProjectileCount = 0x02016ADE; // byte: 3 or 4 (Bullet_Counter)
    constexpr uint32_t kLastPattern = 0x02016AD8; // word: kPattern the frame before (Last_Pattern_Index)
    constexpr uint32_t kParries = 0x02016B85;     // byte: parries in a row (Flip_Counter); 0xFF: no more now
    constexpr uint32_t kForgetAttacks = 0x02016DA8; // byte: frames with no attack until kAttackKinds is forgotten (Reset_Timer)

    // Cleared whenever the computer changes what it does (ClearFlags): bytes, but kLastEffect, a word, and
    // kJumpTimers, four bytes a side
    constexpr uint32_t kAnswering = 0x0201561C;   // (Passive_Flag)
    constexpr uint32_t kParryFlag = 0x0201561E;   // (Flip_Flag): a parry under way
    constexpr uint32_t kLimited = 0x0201562D;     // (Limited_Flag)
    constexpr uint32_t kShellIgnore = 0x0201562F; // (Shell_Ignore_Timer)
    constexpr uint32_t kGuarding = 0x0201563B;    // (Guard_Flag)
    constexpr uint32_t kPierce = 0x02015646;      // (Pierce_Menu)
    constexpr uint32_t kCounter = 0x02015648;     // (Counter_Attack): bit 1: a counter attack under way, not to be broken off
    constexpr uint32_t kBeforeJump = 0x0201564D;  // (Before_Jump)
    constexpr uint32_t kContinue = 0x02015730;    // (Continue_Menu)
    constexpr uint32_t kStanding = 0x02015740;    // (Standing_Timer)
    constexpr uint32_t kBeforeLook = 0x02015742;  // (Before_Look): kStanding is kept from one plan to the next
    constexpr uint32_t kAttackCounted = 0x02015758; // (Attack_Count_No0): the other's attack now is counted
    constexpr uint32_t kTurnOver = 0x02015768;    // (Turn_Over)
    constexpr uint32_t kJumpTimers = 0x020157C0;  // (Jump_Pass_Timer)
    constexpr uint32_t kLastEffect = 0x02015830;  // (Last_Eftype)

    // --- the game's, for everyone ---
    constexpr uint32_t kFightOn = 0x02011389;     // byte: 0 while the fighters may not fight (Allow_a_battle_f)
    constexpr uint32_t kPaused = 0x02068C67;      // byte: 1: the computer holds nothing (pcon_dp_flag)
    constexpr uint32_t kPlayTime = 0x02011372;    // word: how far the game has got (Control_Time): the computer's
                                                  // levels are by it
    constexpr uint32_t kRank = 0x0201584C;        // byte: which set of level tables (by it, a row of each)
    constexpr uint32_t kLevelBonus = 0x0201584D;  // byte: added to the guard's level (CC_Value)
    constexpr uint32_t kDemo = 0x020154AF;        // byte: not 0 for attract mode's fights (Demo_Flag)
    constexpr uint32_t kWeakSide = 0x02016AD6;    // byte: the side that plays at level 2 at most, outside a demo (Weak_PL)
    constexpr uint32_t kBrokenInto = 0x02016B00;  // byte: 2: a challenger broke in: the computer at its best (Break_Into_CPU)
    constexpr uint32_t kBossRank = 0x02016B33;    // byte: not 0: the boss's tables (row 18), not the character's (EM_Rank)
    constexpr uint32_t kLevelNow = 0x0200DCF4;    // byte: the level just worked out (Lv)
    constexpr uint32_t kDiceNow = 0x0200DCF5;     // byte: the random number just taken (Rnd)
    constexpr uint32_t kFighter[2] = {0x02068C6C, 0x02069104}; // the fighters' records (plw)
    constexpr uint32_t kFighterSize = 0x498;

    // --- tables in the program ---
    constexpr uint32_t kModes = 0x0613E504;          // 16 longs: the modes' routines
    constexpr uint32_t kFollowUps = 0x0613E598;      // 21 longs: each character's follow-up routine (mode 4)
    constexpr uint32_t kAnswers = 0x0613E5EC;        // 21 longs: each character's answer (mode 6)
    constexpr uint32_t kRangeLimits = 0x0616E998;    // three words a character: kRanges (Area_Unit_Data)
    constexpr uint32_t kShellRangeLimits = 0x0616EA16; // three words a character: kShellRanges
    constexpr uint32_t kWidths = 0x06177422;         // a byte a character: kWidth (PL_Body_Width_Data)
    constexpr uint32_t kStandTimes = 0x06176EDC;     // bytes: [character, 18 the boss][range][8 at random] (Standing_Time_Data)
    constexpr uint32_t kCrouchTimes = 0x0617717C;    // bytes: [level of 8][8 at random] (Squat_Time_Data)
    constexpr uint32_t kCanParry = 0x0617735E;       // bytes, by kDoingHow: not 0 in a parry's action (PL_Damage_Data)
    constexpr uint32_t kReachByClass = 0x061773DE;   // words, by kAttackReach: an attack's reach (Hit_Range_Data)
    constexpr uint32_t kGuardChoices = 0x06175EBA;   // bytes: [character, 18 the boss: 0xB0][level: 16][16 at random]:
                                                     // the guard to do, 3 none (Guard_Data)
    constexpr uint32_t kMashSpeeds = 0x06176E7C;     // bytes: [level of 5][8 at random]: frames between a throw's
                                                     // mashes (Rapid_Hit_Data)
    constexpr uint32_t kMashLevers = 0x0613E6E0;     // two words: the mash's levers, one way then the other
    constexpr uint32_t kFollowUpMenus = 0x02000664;  // longs in RAM, a character each: its follow-ups by step, by side
                                                     // hit, 32 at random (Follow_Menu_1st_Unit_Data)
    constexpr uint32_t kFollowUpPlans = 0x02000698;  // longs, a character each: the plan for each follow-up, four
                                                     // bytes by range (Follow_Menu_2nd_Unit_Data)
    // The level tables (Level_04/08/10_Data): rows of a word each level's play time ends at, a set for each kRank
    constexpr uint32_t kLevels4 = 0x061753D4, kLevels8 = 0x061753E6, kLevels10 = 0x06175464;

    // --- the game's routines called ---
    constexpr uint32_t kDice128 = 0x0611E0D6;   // random_32_com: the next of a table of 128 (in r0)
    constexpr uint32_t kDice64 = 0x0611E0EE;    // random_16_com: the next of a table of 64 (lifts.cpp's NextRandom)
    constexpr uint32_t kDice32 = 0x0611E11E;    // random_16_ex_com: the next of a table of 32
    constexpr uint32_t kChoosePlan = 0x060104B4; // Select_Active (r4): the plan for mode 2
    constexpr uint32_t kPlanCheck = 0x06008BF0;  // Pattern_Insurance (r4, r5 which, r6 the plan): a plan past the
                                                 // character's last of that kind is put back to r6 (computer_tactics.cpp)
    constexpr uint32_t kParryPossible = 0x0601111A;    // Check_Flip_Term (r4, r5): it may parry now
    constexpr uint32_t kStartParry = 0x06011324;       // Next_Be_Flip (r4, r5 0)
    constexpr uint32_t kStartGuard = 0x06011248;       // Next_Be_Guard (r4, r5 the other, r6 the guard)
    constexpr uint32_t kKnockedDown = 0x060138E0;      // Check_Blow_Off (r4, r5 the other, r6 0): still lying down
    constexpr uint32_t kAfterHit = 0x0600775A;         // Exit_Damage_Sub (r4): what next, once up again
    constexpr uint32_t kBeFree = 0x06008706;           // Next_Be_Free (r4): back to mode 1

    // The modes
    enum Mode : int16_t {
        kStarting = 0, kFree = 1, kActive = 2, kBeforeFollowUp = 3, kFollowingUp = 4, kBeforeAnswer = 5, kAnswer = 6,
        kGuardMode = 7, kVsProjectile = 8, kGuardProjectile = 9, kHit = 10, kInAir = 11, kParry = 12, kCaught = 13,
        kLyingDown = 14, kThrowing = 15,
    };
    // kDoing
    constexpr int16_t kDoingFree = 0, kDoingHit = 1, kDoingThrow = 2, kDoingThrown = 3;

    // One side of the computer: its fighter's record and its entries in the tables
    class Side {
    public:
        Side(Guest &guest, uint32_t record)
            : g(guest), me(record), side(static_cast<int16_t>(guest.Read16(record + kSide))) {}

        Guest &g;
        const uint32_t me;
        const int32_t side;

        uint32_t Other() { return g.Read32(me + kOpponent); }
        int16_t Field(uint32_t field) { return static_cast<int16_t>(g.Read16(me + field)); }
        int16_t OtherField(uint32_t field) { return static_cast<int16_t>(g.Read16(Other() + field)); }
        int16_t Character() { return Field(kCharacter); }

        // A byte or word of a table of the computer's, the n-th of the side's
        uint8_t Byte(uint32_t table, int32_t perSide = 1, int32_t n = 0) { return g.Read8(table + static_cast<uint32_t>(side * perSide + n)); }
        void SetByte(uint32_t table, int32_t value, int32_t perSide = 1, int32_t n = 0) {
            g.Write8(table + static_cast<uint32_t>(side * perSide + n), static_cast<uint8_t>(value));
        }
        uint32_t WordAt(uint32_t table, int32_t perSide, int32_t n) { return table + static_cast<uint32_t>(side * perSide * 2 + n * 2); }
        int16_t Word(uint32_t table, int32_t perSide = 1, int32_t n = 0) { return static_cast<int16_t>(g.Read16(WordAt(table, perSide, n))); }
        void SetWord(uint32_t table, int32_t value, int32_t perSide = 1, int32_t n = 0) { g.Write16(WordAt(table, perSide, n), static_cast<uint16_t>(value)); }

        int16_t ModeWord(int n) { return Word(kMode, 4, n); }
        void SetModeWord(int n, int32_t value) { SetWord(kMode, value, 4, n); }
        // A new mode, at its first step
        void SetMode(int16_t mode) {
            SetModeWord(0, mode);
            for (int n = 1; n < 4; n++) {
                SetModeWord(n, 0);
            }
        }
        void ClearPlanPlaces(int count) {
            for (int n = 0; n < count; n++) {
                SetWord(kPlanPlace, 0, 8, n);
            }
        }
    };

    // A routine of the game's called with the fighter's record (and more) in the registers
    void CallWith(Guest &g, uint32_t routine, uint32_t r4, uint32_t r5 = 0, uint32_t r6 = 0) {
        g.R(4) = r4;
        g.R(5) = r5;
        g.R(6) = r6;
        g.Call(routine);
    }
    int16_t Dice(Guest &g, uint32_t which) {
        g.Call(which);
        return static_cast<int16_t>(g.R(0));
    }


    // --- levels ---

    // Setup_Lv04/08/10: the computer's level from how far the game has got: the first of `levels - 1` play times
    // (a row of words: the kRank's set of rows, `row` of them) that kPlayTime has not passed, or the last level. The
    // offsets are bytes, as the game's code keeps them (rank * set size and row * row size, each cut to a signed byte).
    int16_t LevelByPlayTime(Guest &g, uint32_t table, int levels, int32_t row) {
        const int32_t perRow = (levels - 1) * 2;
        const int32_t sets = static_cast<int8_t>(static_cast<int8_t>(g.Read8(kRank)) * (levels == 8 ? perRow * 3 : perRow));
        const uint32_t times = table + static_cast<uint32_t>(sets) + static_cast<uint32_t>(static_cast<int8_t>(row * perRow));
        const int16_t now = static_cast<int16_t>(g.Read16(kPlayTime));
        int16_t level = 0;
        while (level < levels - 1 && now > static_cast<int16_t>(g.Read16(times + static_cast<uint32_t>(level) * 2))) {
            level++;
        }
        return level;
    }

    // ... and the registers the game's code leaves (its callers' callers read r5-r7: an answer to crouching passes them
    // out of Ck_Passive_Term): r1 the table, r2 the last play time compared, r3 kPlayTime's word (or, past the last,
    // the level), r4 the level, r5 the levels less one, r6 the row's place, r7 kPlayTime, T the last comparison's
    void LevelRegistersAsTheGame(Guest &g, uint32_t table, int levels, int32_t row) {
        const int32_t perRow = (levels - 1) * 2;
        const int32_t sets = static_cast<int8_t>(static_cast<int8_t>(g.Read8(kRank)) * (levels == 8 ? perRow * 3 : perRow));
        const uint32_t times = table + static_cast<uint32_t>(sets) + static_cast<uint32_t>(static_cast<int8_t>(row * perRow));
        const int16_t now = static_cast<int16_t>(g.Read16(kPlayTime));
        int16_t level = 0, last = 0, third = 0;
        bool t = false;
        for (;;) {
            third = now;
            last = static_cast<int16_t>(g.Read16(times + static_cast<uint32_t>(level) * 2));
            if (!(now > last)) {
                t = false; // (cmp/gt: not passed)
                break;
            }
            level++;
            third = level;
            if (level >= levels - 1) {
                t = true; // (cmp/ge: the last level)
                break;
            }
        }
        g.R(0) = static_cast<uint32_t>(static_cast<int32_t>(level));
        g.R(1) = table;
        g.R(2) = static_cast<uint32_t>(static_cast<int32_t>(last));
        g.R(3) = static_cast<uint32_t>(static_cast<int32_t>(third));
        g.R(4) = static_cast<uint32_t>(static_cast<int32_t>(level));
        g.R(5) = static_cast<uint32_t>(levels - 1);
        g.R(6) = times;
        g.R(7) = kPlayTime;
        g.SetT(t);
    }

    // 0600FA14 (Setup_Lv08): the level of eight, r4 the row of the table (three a set)
    bool LevelOf8(Guest &g) {
        LevelRegistersAsTheGame(g, kLevels8, 8, static_cast<int16_t>(g.R(4)));
        g.Return(30);
        return true;
    }


    // --- what the computer sees ---

    // 0600FC20 (Ck_Distance): the gap between the two, along the floor
    void MeasureGap(Side &s) {
        const int16_t gap = static_cast<int16_t>(s.OtherField(kX) - s.Field(kX));
        s.SetWord(kGap, gap < 0 ? -gap : gap);
    }

    // 0600FC6A (Ck_Area): which of the side's ranges the gap is in: the first whose limit it is within, or 3 (far)
    int16_t RangeOfGap(Side &s) {
        const int16_t gap = s.Word(kGap);
        for (int16_t range = 0; range < 3; range++) {
            if (gap <= s.Word(kRanges, 3, range)) {
                return range;
            }
        }
        return 3;
    }

    // 060088D4 (Check_At_Count): the other's attacks, counted as each begins; the kinds of the last four are kept, and
    // forgotten after 120 frames of none
    void CountAttacks(Side &s) {
        const bool attacking = s.Byte(kOtherAttacking) != 0;
        if (s.Byte(kAttackCounted) == 0) {
            if (attacking) {
                s.SetWord(kAttacksSeen, s.Word(kAttacksSeen) + 1);
                s.SetByte(kAttackCounted, 1);
                const uint8_t kind = s.g.Read8(s.Other() + kAttackKind);
                s.SetByte(kOtherAttackKind, kind);
                s.SetByte(kAttackKinds, kind, 4, s.Byte(kAttackKindPlace));
                s.SetByte(kAttackKindPlace, (s.Byte(kAttackKindPlace) + 1) & 3);
            }
        } else if (!attacking) {
            s.SetByte(kAttackCounted, 0); // (the attack over: the next is counted)
        }
        if (attacking) {
            s.SetByte(kForgetAttacks, 120);
            return;
        }
        s.SetByte(kForgetAttacks, s.Byte(kForgetAttacks) - 1);
        if (s.Byte(kForgetAttacks) == 0) {
            for (int n = 0; n < 4; n++) {
                s.SetByte(kAttackKinds, n, 4, n);
            }
        }
    }


    // --- clearing ---

    // 060087E4 (Clear_Com_Flag): what the computer was in the middle of, cleared
    void ClearFlags(Side &s) {
        for (const uint32_t flag : {kAnswering, kParryFlag, kCounter, kLimited, kGuarding, kBeforeJump, kShellIgnore, kPierce,
                                    kContinue, kStanding, kBeforeLook, kAttackCounted, kTurnOver}) {
            s.SetByte(flag, 0);
        }
        for (int n = 0; n < 4; n++) {
            s.SetByte(kJumpTimers, 0, 4, n);
        }
        s.SetWord(kLastEffect, 0);
    }

    // 06008BB0 (Setup_Bullet_Counter): 3 or 4, at random
    void SetProjectileCount(Side &s) {
        s.SetByte(kProjectileCount, 3);
        const int16_t dice = Dice(s.g, kDice128);
        s.SetByte(kProjectileCount, s.Byte(kProjectileCount) + (dice & 1));
    }

    // 060134CC (Setup_Next_Stand_Timer): how long to stand before the next plan: one of eight at random, by the
    // character (the boss's own) and the range
    uint8_t StandTime(Side &s) {
        const int32_t row = s.g.Read8(kBossRank) != 0 ? 18 : s.Character();
        const uint32_t times = kStandTimes + static_cast<uint32_t>(row * 32 + s.Word(kRange) * 8);
        return s.g.Read8(times + static_cast<uint32_t>(Dice(s.g, kDice64) & 7));
    }

    // 060136A4 (Setup_Next_Squat_Timer): how long to crouch: one of eight at random, by the level of eight. (The
    // registers as the game's code leaves them: the level's (LevelRegistersAsTheGame), then the dice's, r2 its address.)
    uint8_t CrouchTime(Side &s) {
        LevelRegistersAsTheGame(s.g, kLevels8, 8, 0);
        const uint32_t times = kCrouchTimes + static_cast<uint32_t>(static_cast<int16_t>(s.g.R(0)) * 8);
        s.g.R(2) = kDice64;
        const uint8_t time = s.g.Read8(times + static_cast<uint32_t>(Dice(s.g, kDice64) & 7));
        return time;
    }


    // --- what has happened to the fighter: each true when the computer changed mode for it ---

    // 0600777C (Check_Damage): hit (but in a counter attack, guarding or already guarding or hit): mode 10, the lever
    // down
    bool CheckHit(Side &s) {
        if (s.Byte(kCounter) & 2) {
            return false;
        }
        const int16_t mode = s.ModeWord(0);
        if (s.Field(kDoing) == kDoingHit && mode != kGuardMode && mode != kGuardProjectile && s.Byte(kGuarding) == 0) {
            s.SetMode(kHit);
            s.SetByte(kHitSeen, 0);
            s.SetWord(kLever, 2);
            ClearFlags(s);
            return true;
        }
        return false;
    }

    // 0600831A (Check_Caught): thrown: mode 13
    bool CheckCaught(Side &s) {
        if (s.Field(kDoing) == kDoingThrown) {
            s.SetMode(kCaught);
            ClearFlags(s);
            return true;
        }
        return false;
    }

    // 06007F64 (Check_Flip): a parry begun (the fighter free, in a parry's action: its kDoingHow in kCanParry), when
    // one is not under way already (kParryFlag) and parries are not stopped (kParries): mode 12, waiting 15 frames; its step 1 in the air, else 3 when the
    // reload timer is 8, else 2
    bool CheckParry(Side &s) {
        if (s.Byte(kParryFlag) != 0 || s.Field(kDoing) != kDoingFree) {
            return false;
        }
        if (s.g.Read8(kCanParry + static_cast<uint32_t>(s.Field(kDoingHow))) == 0 || s.Byte(kParries) == 0xFF) {
            return false;
        }
        s.SetModeWord(0, kParry);
        s.SetModeWord(2, 0);
        s.SetModeWord(3, 0);
        s.SetWord(kTimer, 15);
        s.SetModeWord(1, s.Word(kTimerReload) == 8 ? 3 : 2);
        if (s.Field(kY) > 0) {
            s.SetModeWord(1, 1);
        }
        return true;
    }

    // 06010F4C (Check_Guard): the other's attack, new (not one already let through) and near enough to reach (its
    // reach and the computer's width): the computer's level of guarding (of ten, by the play time; 2 for the weak side
    // outside a demo; plus the bonus; 10 when a challenger broke in) and a dice say the guard to do, 3 none (the
    // attack is then let through). With a guard, it parries instead if it may.
    bool CheckGuard(Side &s) {
        Guest &g = s.g;
        const uint32_t other = s.Other();
        if (s.Byte(kOtherAttacking) == 0 || s.Word(kGuardedAttack) == s.Word(kAttacksSeen)) {
            return false;
        }
        const int16_t reach = static_cast<int16_t>(g.Read16(kReachByClass + static_cast<uint32_t>(g.Read8(other + kAttackReach)) * 2) + s.Word(kWidth));
        if (s.Word(kGap) > reach) {
            return false;
        }
        g.Write8(kLevelNow, static_cast<uint8_t>(LevelByPlayTime(g, kLevels10, 10, 0)));
        if (g.Read8(kDemo) == 0 && g.Read8(kWeakSide) == static_cast<uint32_t>(s.side)) {
            g.Write8(kLevelNow, 2);
        }
        g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice64)));
        g.Write8(kLevelNow, static_cast<uint8_t>(g.Read8(kLevelNow) + g.Read8(kLevelBonus)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 10);
        }
        const int32_t row = g.Read8(kBossRank) != 0 ? 18 : s.Character();
        const uint32_t choices = kGuardChoices + static_cast<uint32_t>(static_cast<int16_t>(row * 0xB0)) +
                                 static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kLevelNow)) * 16);
        if (static_cast<int8_t>(g.Read8(choices + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kDiceNow))))) == 3) {
            s.SetWord(kGuardedAttack, s.Word(kAttacksSeen)); // (let through: not looked at again)
            return false;
        }
        CallWith(g, kParryPossible, s.me, s.me);
        if (g.R(0) != 0) {
            CallWith(g, kStartParry, s.me, 0);
        } else {
            const int16_t dice = Dice(g, kDice32);
            const int8_t guard = static_cast<int8_t>(g.Read8(choices + static_cast<uint32_t>(dice)));
            CallWith(g, kStartGuard, s.me, other, static_cast<uint32_t>(guard));
        }
        return true;
    }


    // --- the modes ---

    // 060061D0 (Com_Initialize), mode 0: everything of the computer's cleared, the character's ranges and width taken
    // from its tables, the first stand and crouch times chosen; then mode 1
    void Start(Side &s) {
        Guest &g = s.g;
        s.SetMode(kFree);
        s.SetWord(kHeldDownSince, 0);
        for (int n = 0; n < 3; n++) {
            s.SetWord(kHeld, 0, 3, n);
        }
        s.SetWord(kAttacksSeen, 0);
        s.SetByte(kProjectiles, 0);
        s.SetWord(kLastAttackSeen, -1);
        s.SetWord(kGuardedAttack, -1);
        s.SetByte(kTurnTimer, 1);
        s.SetByte(kAttackKindPlace, 0);
        s.SetByte(kParries, 0);
        // (the character's three limits a table: its row an offset in bytes, cut to a signed byte, as the code has it)
        const int8_t character = static_cast<int8_t>(g.Read8(s.me + kCharacter + 1));
        const uint32_t row = static_cast<uint32_t>(static_cast<int8_t>(character * 6));
        for (int n = 0; n < 3; n++) {
            s.SetWord(kRanges, static_cast<int16_t>(g.Read16(kRangeLimits + row + static_cast<uint32_t>(n) * 2)), 3, n);
        }
        for (int n = 0; n < 3; n++) {
            s.SetWord(kShellRanges, static_cast<int16_t>(g.Read16(kShellRangeLimits + row + static_cast<uint32_t>(n) * 2)), 3, n);
        }
        s.SetWord(kWidth, static_cast<int8_t>(g.Read8(kWidths + static_cast<uint32_t>(s.Character()))));
        ClearFlags(s);
        s.SetByte(kStandFor, StandTime(s));
        s.SetByte(kCrouchFor, CrouchTime(s));
        SetProjectileCount(s);
        for (int n = 0; n < 20; n++) {
            s.SetWord(kLastLevers, 0, 20, n);
        }
        for (int n = 0; n < 3; n++) {
            s.SetByte(kAttackKinds, 0xFF, 4, n);
        }
    }

    // 0600643C (Com_Free), mode 1: holding kLeverNow; unless hit or thrown, a plan is chosen (Select_Active) and it is
    // mode 2. Everything is cleared for the plan but the standing time, when it is to be kept.
    void Free(Side &s) {
        s.SetWord(kLever, s.Word(kLeverNow));
        if (CheckHit(s) || CheckCaught(s)) {
            return;
        }
        s.SetMode(kActive);
        const uint8_t standing = s.Byte(kBeforeLook) != 0 ? s.Byte(kStanding) : 0;
        ClearFlags(s);
        s.SetByte(kStanding, standing);
        s.ClearPlanPlaces(8);
        CallWith(s.g, kChoosePlan, s.me);
    }

    // 06010918 (Decide_Follow_Menu): the follow-up, from the character's menus: one of 32 at random by the step and
    // the third word (the side hit), then the plan for it by the range
    void ChooseFollowUp(Side &s) {
        Guest &g = s.g;
        const uint32_t menus = g.Read32(kFollowUpMenus + static_cast<uint32_t>(s.Character()) * 4);
        g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice128)));
        const uint32_t menu = menus + static_cast<uint32_t>(s.ModeWord(1) * 64 + s.ModeWord(2) * 32);
        const int8_t followUp = static_cast<int8_t>(g.Read8(menu + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kDiceNow)))));
        const uint32_t plans = g.Read32(kFollowUpPlans + static_cast<uint32_t>(s.Character()) * 4);
        s.SetWord(kPattern, static_cast<int8_t>(g.Read8(plans + static_cast<uint32_t>(followUp * 4 + s.Word(kRange)))));
    }

    // 0600653C (Com_Before_Follow), mode 3: holding kLeverNow, it waits (kTimer) unless hit, thrown, attacked or
    // knocked down; then the follow-up is chosen and it is mode 4
    void BeforeFollowUp(Side &s) {
        s.SetWord(kLever, s.Word(kLeverNow));
        if (CheckHit(s) || CheckCaught(s) || CheckGuard(s) || CheckParry(s)) {
            return;
        }
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        if (s.Word(kTimer) != 0) {
            return;
        }
        ChooseFollowUp(s);
        s.SetMode(kFollowingUp);
        s.ClearPlanPlaces(4);
        ClearFlags(s);
    }

    // 06006610 (Com_Before_Passive), mode 5: as mode 3, but guarding only when not limited or countering; then mode 6
    void BeforeAnswer(Side &s) {
        s.SetWord(kLever, s.Word(kLeverNow));
        if (CheckHit(s) || CheckCaught(s) || CheckParry(s)) {
            return;
        }
        if (s.Byte(kLimited) == 0 && s.Byte(kCounter) == 0 && CheckGuard(s)) {
            return;
        }
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        if (s.Word(kTimer) != 0) {
            return;
        }
        s.SetMode(kAnswer);
        s.ClearPlanPlaces(4);
    }

    // 06006C72 (Com_Follow) and 06006CE0 (Com_Passive), modes 4 and 6: unless hit, thrown or knocked down, the plan is
    // checked (Pattern_Insurance, with which kind and the plan to fall back on) and the character's own routine plays
    void ByCharacter(Side &s, uint32_t routines, int which, int fallBack) {
        if (CheckHit(s) || CheckCaught(s) || CheckParry(s)) {
            return;
        }
        CallWith(s.g, kPlanCheck, s.me, static_cast<uint32_t>(which), static_cast<uint32_t>(fallBack));
        CallWith(s.g, s.g.Read32(routines + static_cast<uint32_t>(s.Character()) * 4), s.me);
    }

    // 060084A4 (Com_Wait_Lie), mode 14: lying down until the fighter is up; then what next (Exit_Damage_Sub)
    void LyingDown(Side &s) {
        CallWith(s.g, kKnockedDown, s.me, s.Other(), 0);
        if (s.g.R(0) == 0) {
            CallWith(s.g, kAfterHit, s.me);
        }
    }

    // 060082D6 (Com_Rapid_Sub): a mash: every kTimer frames (reloaded from kTimerReload) the lever one way then the
    // other (`way`, a word of the computer's: 0 or 1), with the `buttons`; nothing between
    uint16_t Mash(Side &s, uint16_t buttons, uint32_t way) {
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        if (s.Word(kTimer) != 0) {
            return 0;
        }
        s.SetWord(kTimer, s.Word(kTimerReload));
        Guest &g = s.g;
        const int16_t n = static_cast<int16_t>(g.Read16(way));
        const uint16_t lever = static_cast<uint16_t>(static_cast<int16_t>(g.Read16(kMashLevers + static_cast<uint32_t>(n * 2))) | buttons);
        g.Write16(way, static_cast<uint16_t>(n + 1));
        g.Write16(way, static_cast<uint16_t>(static_cast<int16_t>(g.Read16(way)) & 1));
        return lever;
    }

    // 060083A0 (Com_Catch), mode 15: the computer throwing. At its first step the mash's speed is chosen (by the level of
    // five, 4 when a challenger broke in, one of eight at random); then it mashes the buttons while the throw lasts,
    // and is free again after.
    void Throwing(Side &s) {
        Guest &g = s.g;
        switch (s.ModeWord(1)) {
        case 0: {
            s.SetModeWord(1, s.ModeWord(1) + 1);
            s.SetModeWord(2, 0);
            s.SetWord(kTimer, 1);
            int16_t level = LevelByPlayTime(g, kLevels4, 4, 0);
            if (g.Read8(kBrokenInto) == 2) {
                level = 4;
            }
            const uint32_t speeds = kMashSpeeds + static_cast<uint32_t>(level * 8);
            s.SetWord(kTimerReload, g.Read8(speeds + static_cast<uint32_t>(Dice(g, kDice64) & 7)));
            break;
        }
        case 1:
            if (s.Field(kDoing) != kDoingThrow || s.OtherField(kDoing) != kDoingThrown) {
                CallWith(g, kBeFree, s.me);
                break;
            }
            s.SetWord(kLever, Mash(s, 0x3F0, s.WordAt(kMode, 4, 2)));
            break;
        }
    }

    // A mode's routine: this part's in C++, the others the game's
    void RunMode(Side &s) {
        switch (s.ModeWord(0)) {
        case kStarting: Start(s); break;
        case kFree: Free(s); break;
        case kBeforeFollowUp: BeforeFollowUp(s); break;
        case kFollowingUp: ByCharacter(s, kFollowUps, 3, 2); break;
        case kBeforeAnswer: BeforeAnswer(s); break;
        case kAnswer: ByCharacter(s, kAnswers, 1, 1); break;
        case kLyingDown: LyingDown(s); break;
        case kThrowing: Throwing(s); break;
        default: CallWith(s.g, s.g.Read32(kModes + static_cast<uint32_t>(s.ModeWord(0)) * 4), s.me); break;
        }
    }


    // --- the frame ---

    // 060060B4 (Main_Program): the fight looked at (the gap, its range, whether the other attacks), then the mode run;
    // once more if it changed and asked to be
    void Think(Side &s) {
        MeasureGap(s);
        s.SetWord(kRange, RangeOfGap(s));
        const uint32_t other = kFighter[0] + static_cast<uint32_t>(static_cast<int16_t>((s.side ^ 1) * kFighterSize));
        s.SetByte(kOtherAttacking, s.g.Read8(other + kAttacking));
        CountAttacks(s);
        s.SetByte(kRunAgain, 0);
        RunMode(s);
        if (s.Byte(kRunAgain) != 0) {
            RunMode(s);
        }
    }

    // 06008ABC (Store_LR_Sub): the lever forward or back: kHeld's count of that way goes on, the other's starts again
    void CountForwardBack(Side &s) {
        const int forward = s.g.Read8(s.me + kMovesFaceRight) != 0 ? 8 : 4;
        const int back = forward ^ 12;
        if (s.Word(kLever) & forward) {
            s.SetWord(kHeld, s.Word(kHeld, 3, 1) + 1, 3, 1);
            s.SetWord(kHeld, 0, 3, 2);
        }
        if (s.Word(kLever) & back) {
            s.SetWord(kHeld, 0, 3, 1);
            s.SetWord(kHeld, s.Word(kHeld, 3, 2) + 1, 3, 2);
        }
    }

    // 06008A08 (Check_Store_Lv): how long the lever has been held down, forward and back: down (or down and a way)
    // counts on in kHeld's first; a way alone or with down, in the way's (CountForwardBack); anything else (up,
    // nothing) starts them all again
    void CountHeld(Side &s) {
        switch (s.Word(kLever) & 15) {
        case 2:
            s.SetWord(kHeld, s.Word(kHeld, 3, 0) + 1, 3, 0);
            break;
        case 6:
        case 10:
            CountForwardBack(s);
            s.SetWord(kHeld, s.Word(kHeld, 3, 0) + 1, 3, 0);
            break;
        case 4:
        case 8:
            CountForwardBack(s);
            break;
        default:
            for (int n = 0; n < 3; n++) {
                s.SetWord(kHeld, 0, 3, n);
            }
            break;
        }
    }

    // 0600899A (Shift_Resume_Lv): the lever kept, the newest of the last twenty
    void KeepLever(Side &s) {
        for (int n = 18; n >= 0; n--) {
            s.SetWord(kLastLevers, s.Word(kLastLevers, 20, n), 20, n + 1);
        }
        s.SetWord(kLastLevers, s.Word(kLever), 20, 0);
    }

    // 06006040 (CPU_Sub): the computer's lever and buttons for the frame: none while the fighters may not fight or the
    // computer is held. Else it starts from nothing, notes whether the other lies on the floor and the plan it had,
    // thinks, and keeps what it chose.
    uint16_t ComputerPlays(Side &s) {
        Guest &g = s.g;
        if (g.Read8(kFightOn) == 0 || g.Read8(kPaused) == 1) {
            return 0;
        }
        s.SetWord(kLever, 0);
        s.SetByte(kOtherLying, g.Read8(s.Other() + kPose) == 0x26 ? 1 : 0);
        s.SetWord(kLastPattern, s.Word(kPattern));
        Think(s);
        CountHeld(s);
        KeepLever(s);
        return static_cast<uint16_t>(s.Word(kLever));
    }


    // --- the lifts: each routine's own (r4 the fighter's record; a result in r0) ---
    template <void (*F)(Side &)>
    bool Lifted(Guest &g) {
        Side s(g, g.R(4));
        F(s);
        g.Return(60);
        return true;
    }
    template <typename R, R (*F)(Side &)>
    bool LiftedWithResult(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = static_cast<uint32_t>(F(s));
        g.Return(60);
        return true;
    }
    template <bool (*F)(Side &)>
    bool LiftedCheck(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = F(s) ? 1 : 0;
        g.Return(60);
        return true;
    }
    void FollowUp(Side &s) {
        ByCharacter(s, kFollowUps, 3, 2);
    }
    void Answer(Side &s) {
        ByCharacter(s, kAnswers, 1, 1);
    }
    // 060082D6: r5 the buttons, r6 the word of the way
    bool MashLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = Mash(s, static_cast<uint16_t>(g.R(5)), g.R(6));
        g.Return(30);
        return true;
    }

    // (free: r0-r7 and T for a routine with no result, r1-r7 and T for one with: the Hitachi compiler's callers keep
    // nothing there)
    constexpr uint32_t kNoResult = 0x100FF, kResult = 0x100FE;
    const Lift kLifts[] = {
        {"ComputerPlays", 0x06006040, 0x74, 0xB6CAAEC23DEF8988ull, LiftedWithResult<uint16_t, ComputerPlays>, true, kResult},
        {"ComputerThinks", 0x060060B4, 0xD2, 0x649669B273511C9Eull, Lifted<Think>, true, kNoResult},
        {"ComputerStarts", 0x060061D0, 0x26C, 0x6B877783E48CBCDCull, Lifted<Start>, true, kNoResult},
        {"ComputerFree", 0x0600643C, 0x100, 0x9C99AFBAD3CB4804ull, Lifted<Free>, true, kNoResult},
        {"ComputerBeforeFollowUp", 0x0600653C, 0xD4, 0x9DDE9B5B813C2991ull, Lifted<BeforeFollowUp>, true, kNoResult},
        {"ComputerBeforeAnswer", 0x06006610, 0xD8, 0x2A23C3BD1ED06E98ull, Lifted<BeforeAnswer>, true, kNoResult},
        {"ComputerFollowsUp", 0x06006C72, 0x6E, 0x2B1B10E89C46E475ull, Lifted<FollowUp>, true, kNoResult},
        {"ComputerAnswers", 0x06006CE0, 0x50, 0x823E6BE537B648C9ull, Lifted<Answer>, true, kNoResult},
        {"ComputerCheckHit", 0x0600777C, 0x9C, 0x3DD4B64815422E80ull, LiftedCheck<CheckHit>, true, kResult},
        {"ComputerCheckParry", 0x06007F64, 0xE4, 0x1D4F93BB379514B8ull, LiftedCheck<CheckParry>, true, kResult},
        {"ComputerMash", 0x060082D6, 0x44, 0xF23972205E033DE3ull, MashLift, true, kResult},
        {"ComputerCheckCaught", 0x0600831A, 0x52, 0xDDE9BCCC833DA774ull, LiftedCheck<CheckCaught>, true, kResult},
        {"ComputerThrowing", 0x060083A0, 0xCA, 0x64975B7D43812AC1ull, Lifted<Throwing>, true, kNoResult},
        {"ComputerLyingDown", 0x060084A4, 0x20, 0xB79BD0D8CC9C7D71ull, Lifted<LyingDown>, true, kNoResult},
        {"ComputerClearFlags", 0x060087E4, 0xA2, 0x46D56DBA2B10E567ull, Lifted<ClearFlags>, true, kNoResult},
        {"ComputerCountAttacks", 0x060088D4, 0xC6, 0x6FA093ED77B4AAD1ull, Lifted<CountAttacks>, true, kNoResult},
        {"ComputerKeepLever", 0x0600899A, 0x48, 0xA5BBAB00B2A19150ull, Lifted<KeepLever>, true, kNoResult},
        {"ComputerCountHeld", 0x06008A08, 0xB4, 0xE42747AB8F3813B0ull, Lifted<CountHeld>, true, kNoResult},
        {"ComputerCountForwardBack", 0x06008ABC, 0xF4, 0xE932A277FBC388CCull, Lifted<CountForwardBack>, true, kNoResult},
        {"ComputerProjectileCount", 0x06008BB0, 0x30, 0x7DC058BFCE765195ull, Lifted<SetProjectileCount>, true, kNoResult},
        {"ComputerLevelOf8", 0x0600FA14, 0x3C, 0x5A083F4C70B323FEull, LevelOf8, true, 0},
        {"ComputerMeasureGap", 0x0600FC20, 0x34, 0x7FE48C7BE973F67Aull, Lifted<MeasureGap>, true, kNoResult},
        {"ComputerRangeOfGap", 0x0600FC6A, 0x42, 0x8F7DE92EF7EC5DE8ull, LiftedWithResult<int16_t, RangeOfGap>, true, kResult},
        {"ComputerChooseFollowUp", 0x06010918, 0x70, 0x22262FF97EBFF72Aull, Lifted<ChooseFollowUp>, true, kNoResult},
        {"ComputerCheckGuard", 0x06010F4C, 0x14E, 0x2480BD8272B5C596ull, LiftedCheck<CheckGuard>, true, kResult},
        {"ComputerStandTime", 0x060134CC, 0x96, 0x2074FD8D03405A01ull, LiftedWithResult<uint8_t, StandTime>, true, kResult},
        {"ComputerCrouchTime", 0x060136A4, 0x22, 0xE60943E34CF13E61ull, LiftedWithResult<uint8_t, CrouchTime>, true, 0},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerEntry() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
