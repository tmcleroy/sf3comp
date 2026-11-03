// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer's tactics: the modes in which it defends and is hit (7-13), the plan's mode (2), and what they call.
// The computer's design, its record's fields and the other parts: computer.h.
//
// How the computer defends. The modes (computer_entry.cpp has the list) not about playing a plan are these:
//    2  active (Com_Active): the plan checked (Pattern_Insurance) and played by the character's routine
//    7  guarding (Com_Guard): the lever held back (and down, against a low attack) while the other's attack lasts;
//       a new attack of the other's chooses the guard again. Once it is over, the computer answers (an answer to
//       the other: Check_Passive, computer_answers.cpp) or counter attacks (the other repeating an attack: a
//       reaction, Select_Passive), or is free again
//    8  against a projectile (Com_VS_Shell): the character's own routine (the table at 0x0613E640)
//    9  guarding a projectile (Com_Guard_VS_Shell): as 7 against the projectile, until it is gone
//   10  hit (Com_Damage), in steps (the second of the mode's words: Damage_1st ... Damage_8th): first the guard
//       lever held (in case the combo goes on), and, knocked down, how to get up chosen (by the level, at random:
//       a quick rise, a special move or a super art as it rises, a guard); stunned, the buttons mashed
//   11  in the air after a hit (Com_Float): an air recovery's buttons, or the lever toward or away
//   12  parrying (Com_Flip; "Flip" in the game's source, the blocking of 3rd Strike): the lever forward (or down,
//       against a low attack) at the moment the other's attack would hit; another parry if the attack goes on,
//       a guard instead if the parry is late, an answer after it (by the level, at random)
//   13  caught in a throw (Com_Caught): the throw broken (a chance by the level) or, held, the buttons mashed
// Each first looks at what has happened to the fighter (computer_entry.cpp's CheckHit, CheckCaught, CheckRecovery,
// the last of which starts a parry: mode 12), and the modes that end go back to mode 1 (Next_Be_Free) or to an
// answer (Exit_Damage_Sub: Check_Passive first).
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, Com_Pl.c, Com_Sub.c, Ck_Pass.c), where they are known: this is the arcade's build of the
// same code, and it differs from that version in places (levels, tables, a few tests), the arcade's here. (Names
// only: that project is AGPL-3.0, and none of its code is here; what the routines do was read from the arcade's own
// program and checked call by call against it.)
//
// Registers: the modes are called by the computer's main step (0x060060B4) through its table, and nothing reads what
// they leave; the steps of modes 10-12 are called through the modes' own tables, the same. The routines with other
// callers leave the registers as the game's code does where those callers (or theirs) read them (tools/liveout.py).
// They call the routines not here through the game's calls (Guest::Call), so a call runs the rewrite of the routine
// called if there is one; this file's own routines it calls as C++ where nothing reads the registers after.
#include "computer.h"

#include <iterator>

namespace cps3recomp {

namespace {
    // --- the fighter's record (r4: the computer's; its kOpponent, the other's) ---
    constexpr uint32_t kSide = 0x08;          // word: 0 player 1, 1 player 2 (wu.id); the computer's tables are by it
    constexpr uint32_t kFacesRight = 0x0A;    // byte: the way it faces (rl_flag)
    constexpr uint32_t kOpponent = 0x0C;      // long: the other fighter's record (target_adrs)
    constexpr uint32_t kHitter = 0x14;        // long: what hit it last (dmg_adrs): a fighter or a projectile
    constexpr uint32_t kDoing = 0x26;         // word: what the fighter is doing (routine_no[1]): 0 free, 1 hit, 2
                                              // throwing, 3 thrown, 4 attacking
    constexpr uint32_t kDoingHow = 0x28;      // word: its detail (routine_no[2]): hit, 25 stunned
    constexpr uint32_t kDoingStep = 0x2A;     // word: its step (routine_no[3])
    constexpr uint32_t kX = 0x64, kY = 0x68;  // words: where it is, along the floor and up from it
    constexpr uint32_t kHealth = 0x9E;        // word (vital_new); in a projectile's record, its strength
    constexpr uint32_t kPose = 0x20E;         // byte: its pose (pat_status): 14-30 crouching, 32 and 33 low
    constexpr uint32_t kAttackKind = 0x20F;   // byte: the kind of its attack (kind_of_waza): the high five bits the kind
                                              // (0 a normal), the low ones its strength
    constexpr uint32_t kMoveId = 0x213;       // byte: the move (sp_tech_id): for a throw, 1 one that can be broken
    constexpr uint32_t kMoveKind = 0x215;     // byte: the kind of its animation (cg_type): 9 and 12 getting up, 64 the
                                              // attack's end
    constexpr uint32_t kGuardBits = 0x2E1;    // byte: how its attack is guarded (att.guard): 8 low, 16 high
    constexpr uint32_t kHitOk = 0x2FE;        // byte: its attack can hit now (att_hit_ok)
    constexpr uint32_t kCharacter = 0x3C0;    // word: the character (player_number); byte 0x3C1 its low byte
    constexpr uint32_t kSuperArt = 0x3F0;     // long: the super art's state (sa): word +0 the art (0-2), byte +10 not 0
                                              // while it may be used
    constexpr uint32_t kPy = 0x3F8;           // long (py): its byte +0 set: the fighter is to stand up at once
    constexpr uint32_t kFighterSize = 0x498;
    constexpr uint32_t kFighters = 0x02068C6C; // the fighters' records (plw)

    // --- the computer's tables: two of each, by kSide ---
    constexpr uint32_t kMode = 0x02015572;        // words, four a side (CP_No): the mode, its step, two more words
    constexpr uint32_t kPlanPlace = 0x02015582;   // words, eight a side (CP_Index)
    constexpr uint32_t kPattern = 0x020155A2;     // word: the plan chosen (Pattern_Index)
    constexpr uint32_t kTimer = 0x020155A6;       // word: frames to wait (Timer_00)
    constexpr uint32_t kTimerReload = 0x020155AA; // word: a second timer, a mash's speed (Timer_01): 8 the hit was a
                                                  // projectile's
    constexpr uint32_t kRange = 0x020155B6;       // word: which range the gap is in, 0 close - 3 far (Area_Number)
    constexpr uint32_t kLever = 0x020155BA;       // word: the lever and buttons chosen this frame (Lever_Buff)
    constexpr uint32_t kJumpLever = 0x020155BE;   // word: a lever kept to hold again (Lever_Pool)
    constexpr uint32_t kAnswer = 0x020155F8;      // word: the answer chosen (VS_Tech)
    constexpr uint32_t kGuardType = 0x020155FE;   // word: the guard: 0 as the attack needs, 1 standing, 2 crouching (Guard_Type)
    constexpr uint32_t kShell = 0x02015610;       // long: the projectile the computer is against (Shell_Address)
    constexpr uint32_t kAnswering = 0x0201561C;   // byte (Passive_Flag)
    constexpr uint32_t kRecovering = 0x0201561E;  // byte (Flip_Flag)
    constexpr uint32_t kOtherAttacking = 0x0201562A; // byte: the other's attack is out (Attack_Flag)
    constexpr uint32_t kCounter = 0x02015648;     // byte (Counter_Attack): bit 1 an answer not to be broken off
    constexpr uint32_t kAttacksSeen = 0x02015750; // word: the other's attacks counted (Attack_Counter)
    constexpr uint32_t kLastLevers = 0x0201576E;  // words, twenty a side: the levers of the last twenty frames (Resume_Lever)
    constexpr uint32_t kHitSeen = 0x020157CA;     // byte (Receive_Flag): the computer is to hold down as it is hit
    constexpr uint32_t kOtherAttackKind = 0x02015829; // byte: the kind of the other's last attack (Type_of_Attack)
    constexpr uint32_t kAttackKinds = 0x0201583E; // bytes, four a side: the kinds of the other's last four attacks
    constexpr uint32_t kGuardedAttack = 0x02015848; // word: kAttacksSeen when the guard was last chosen (Guard_Counter)
    constexpr uint32_t kParries = 0x02016B85;     // byte: parries in a row (Flip_Counter); 0xFF: no more now

    // --- the game's, for everyone ---
    constexpr uint32_t kPassiveMode = 0x0201562C; // byte: 4 the first look of an answer, 0 a later one (Passive_Mode)
    constexpr uint32_t kRank = 0x0201584C;        // byte: which set of level tables
    constexpr uint32_t kLevelBonus = 0x0201584D;  // byte: added to some levels (CC_Value)
    constexpr uint32_t kDemo = 0x020154AF;        // byte: not 0 for attract mode's fights (Demo_Flag)
    constexpr uint32_t kWeakSide = 0x02016AD6;    // byte: the side that plays at the lowest level, outside a demo (Weak_PL)
    constexpr uint32_t kBrokenInto = 0x02016B00;  // byte: 2: a challenger broke in: the computer at its best (Break_Into_CPU)
    constexpr uint32_t kBossRank = 0x02016B33;    // byte: not 0: the boss's tables (row 18), not the character's (EM_Rank)
    constexpr uint32_t kLevelNow = 0x0200DCF4;    // byte: the level just worked out (Lv)
    constexpr uint32_t kDiceNow = 0x0200DCF5;     // byte: the random number just taken (Rnd)
    constexpr uint32_t kWins = 0x02011382;        // words, a side: rounds won
    constexpr uint32_t kClock = 0x02011376;       // word: the round's clock
    constexpr uint32_t kChosen = 0x02011387;      // bytes, a side: the character played (My_char)
    constexpr uint32_t kHumanSide = 0x02015503;   // byte: a side, whose kChosen is looked at
    constexpr uint32_t kThrowLevel = 0x0206AC63;  // byte: the row of the level of 18 for breaking throws
    constexpr int kOroOrShin = 15, kMakoto = 17, kTwelve = 19; // characters (kCharacter)

    // --- tables in the program ---
    constexpr uint32_t kActives = 0x0613E544;     // 21 longs: each character's routine for mode 2 (Computer00-20)
    constexpr uint32_t kVsShells = 0x0613E640;    // 21 longs: each character's routine for mode 8 (Shell00 ...)
    constexpr uint32_t kLastPlans = 0x0613E6F5;   // bytes, four a character: the last plan of each kind (Pattern_Insurance)
    constexpr uint32_t kKnockedDown = 0x061773C2; // bytes, by kDoingHow: 2 thrown into the air, not 0 knocked down
                                                  // (PL_Blow_Off_Data)
    constexpr uint32_t kRecoverable = 0x0617735E; // bytes, by kDoingHow: not 0 in a parry's (PL_Damage_Data)
    constexpr uint32_t kGuardChoices = 0x06175EBA; // bytes: [character, 18 the boss: 0xB0][level][16 at random] (Guard_Data)
    constexpr uint32_t kHoldDown = 0x0613FEAC;    // bytes: [character or 18][level of 8]: the chance of holding down as
                                                  // it is knocked down (Receive_Data)
    constexpr uint32_t kGetUps = 0x0613EBA4;      // bytes: [character: 128][level of 4][32 at random]: how to get up, the
                                                  // step less one (Get_Up_Data)
    constexpr uint32_t kGetUpChecks = 0x0613FB64; // bytes: [character: 40][the step: 5]: by the range, 1: not here, the
                                                  // step at +5 instead (Get_Up_Action_Check_Data)
    constexpr uint32_t kGetUpMoves = 0x0613F624;  // words: [character: 64][level of 4: 16][4 at random: the move, its
                                                  // power]: the special move as it gets up; 0xFF: the super art
                                                  // (Get_Up_Action_Tech_Data)
    constexpr uint32_t kSuperArtMoves = 0x061772A0; // words: [character: 6][the art]: its move, -1 none (Arts_Super_Name_Data)
    constexpr uint32_t kStunMashes = 0x06176E3C;  // bytes: [level of 8][8 at random]: frames between a stun's mashes
                                                  // (Faint_Rapid_Data)
    constexpr uint32_t kAirSteps = 0x0617731E;    // bytes: [level of 4][16 at random]: what to do in the air (mode 11's
                                                  // step)
    constexpr uint32_t kThrowMashes = 0x06176DFC; // bytes: [level of 8][8 at random]: frames between a throw's mashes
                                                  // (Rapid_Exit_Data)
    constexpr uint32_t kThrowBreaks = 0x06175950; // bytes: [character or 18: 18][level of 18]: the chance of breaking a
                                                  // throw (Exit_Throw_Data)
    constexpr uint32_t kParryChances = 0x06175ACA; // bytes: [character or 18: 8][level of 8] (Flip_Term_Data)
    constexpr uint32_t kParryAgainChances = 0x06175B72; // bytes: [character: 8][level of 8]: parry the next hit
    constexpr uint32_t kShellParryChances = 0x06175C1A; // bytes: [character: 8][level of 8]: parry a projectile's next hit
                                                  // (Shell_Renzoku_Flip_Data)
    constexpr uint32_t kParryAttackChances = 0x06175CC2; // bytes: [kRank: 0xA8][character or 18: 8][level of 8]: an
                                                  // attack after the parry (Flip_Attack_Data)

    // --- the game's routines called ---
    constexpr uint32_t kDice128 = 0x0611E0D6;     // random_32_com: the next of a table of 128 (0-31)
    constexpr uint32_t kDice64 = 0x0611E0EE;      // random_16_com: the next of a table of 64 (0-15)
    constexpr uint32_t kLevelOf4 = 0x0600F9D2;    // Setup_Lv04 (r4 the row)
    constexpr uint32_t kLevelOf8 = 0x0600FA14;    // Setup_Lv08 (r4 the row; computer_entry.cpp)
    constexpr uint32_t kLevelOf10 = 0x0600FA84;   // Setup_Lv10 (r4 the row)
    constexpr uint32_t kLevelOf18 = 0x0600FAC4;   // Setup_Lv18 (r4 the row)
    constexpr uint32_t kCheckHit = 0x0600777C;    // Check_Damage (r4): hit: mode 10 (computer_entry.cpp)
    constexpr uint32_t kCheckCaught = 0x0600831A; // Check_Caught (r4): thrown: mode 13
    constexpr uint32_t kCheckRecovery = 0x06007F64; // Check_Flip (r4): a parry may be made: mode 12
    constexpr uint32_t kClearFlags = 0x060087E4;  // Clear_Com_Flag (r4)
    constexpr uint32_t kBeFree = 0x06008706;      // Next_Be_Free (r4): back to mode 1 (computer_moves.cpp)
    constexpr uint32_t kMash = 0x060082D6;        // Com_Rapid_Sub (r4, r5 the buttons, r6 the word of the way)
    constexpr uint32_t kLeverFor = 0x06009FF4;    // Setup_Guard_Lever (r4, r5 0 forward, 1 back): the lever's bit
    constexpr uint32_t kCheckAnswer = 0x06010DEA; // Check_Passive (r4): an answer to the other has begun (not 0)
    constexpr uint32_t kChooseAnswer = 0x06012588; // Ck_Passive_Term (r4): an answer chosen (computer_answers.cpp)
    constexpr uint32_t kReact = 0x060109AC;       // Select_Passive (r4): the reaction to kAnswer played; -1 none
    constexpr uint32_t kSpecialMove = 0x060084EC; // Command_Attack_SP (r4, r5 the character, r6 the move, r7 its power):
                                                  // not 0 once it is done
    constexpr uint32_t kClearDash = 0x060B5C9E;   // dash_flag_clear (r4 the side)

    // The modes (computer_entry.cpp)
    constexpr int16_t kFree = 1, kGuardMode = 7, kGuardProjectile = 9, kHit = 10, kInAir = 11, kParry = 12;
    constexpr int16_t kThrowing = 15;
    // kDoing
    constexpr int16_t kDoingFree = 0, kDoingHit = 1, kDoingThrown = 3, kDoingAttack = 4;
    constexpr int16_t kStunned = 25; // kDoingHow, hit

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
        uint8_t FieldByte(uint32_t field) { return g.Read8(me + field); }
        int16_t Character() { return Field(kCharacter); }

        uint32_t At(uint32_t table, int32_t bytesPerSide) { return table + static_cast<uint32_t>(side * bytesPerSide); }
        uint8_t Byte(uint32_t table) { return g.Read8(At(table, 1)); }
        void SetByte(uint32_t table, int32_t value) { g.Write8(At(table, 1), static_cast<uint8_t>(value)); }
        int16_t Word(uint32_t table) { return static_cast<int16_t>(g.Read16(At(table, 2))); }
        void SetWord(uint32_t table, int32_t value) { g.Write16(At(table, 2), static_cast<uint16_t>(value)); }

        // The mode's four words (0 the mode, 1 its step, 2 and 3 the step's)
        uint32_t ModeAt(int n) { return At(kMode, 8) + static_cast<uint32_t>(n * 2); }
        int16_t ModeWord(int n) { return static_cast<int16_t>(g.Read16(ModeAt(n))); }
        void SetModeWord(int n, int32_t value) { g.Write16(ModeAt(n), static_cast<uint16_t>(value)); }
        void SetMode(int16_t mode) {
            SetModeWord(0, mode);
            for (int n = 1; n < 4; n++) {
                SetModeWord(n, 0);
            }
        }
        uint32_t PlaceAt(int n) { return At(kPlanPlace, 16) + static_cast<uint32_t>(n * 2); }
        int16_t Place(int n) { return static_cast<int16_t>(g.Read16(PlaceAt(n))); }
        void SetPlace(int n, int32_t value) { g.Write16(PlaceAt(n), static_cast<uint16_t>(value)); }

        // The character's row of the level tables: the boss's (18) when kBossRank says so (Setup_EM_Rank_Index)
        int16_t Row() { return g.Read8(kBossRank) != 0 ? 18 : Character(); }
        // The weak side outside a demo plays at a level given
        bool Weak() { return g.Read8(kDemo) == 0 && g.Read8(kWeakSide) == static_cast<uint32_t>(side); }
    };

    // The routine's own frame on the stack (what it pushes), so that what it calls finds the stack pointer where the
    // game's code has it (a callee that leaves an address on the stack in a register leaves the same one)
    struct Frame {
        Frame(Guest &guest, uint32_t bytes)
            : g(guest), size(bytes) {
            g.R(15) -= size;
        }
        ~Frame() { g.R(15) += size; }
        Guest &g;
        const uint32_t size;
    };

    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }
    void CallWith(Guest &g, uint32_t routine, uint32_t r4, uint32_t r5 = 0, uint32_t r6 = 0) {
        g.R(4) = r4;
        g.R(5) = r5;
        g.R(6) = r6;
        g.Call(routine);
    }
    // A routine of the game's, r4 the computer's record only (r5-r7 as they are)
    uint32_t Call(Guest &g, uint32_t routine, uint32_t r4) {
        g.R(4) = r4;
        g.Call(routine);
        return g.R(0);
    }
    // Setup_Guard_Lever (r4, r5 0 forward, 1 back): the lever's bit for that way (r6, r7 as they are)
    uint32_t LeverFor(Guest &g, uint32_t me, uint32_t way) {
        g.R(4) = me;
        g.R(5) = way;
        g.Call(kLeverFor);
        return g.R(0);
    }
    int16_t Dice(Guest &g, uint32_t which) {
        g.Call(which);
        return static_cast<int16_t>(g.R(0));
    }
    // One of the level routines (Setup_Lv04 ...), row 0
    int16_t Level(Guest &g, uint32_t which) {
        g.R(4) = 0;
        g.Call(which);
        return static_cast<int16_t>(g.R(0));
    }
    // A level of the game's, but the best (`best`) when a challenger broke in and the lowest (0) for the weak side
    int16_t LevelFor(Side &s, uint32_t which, int16_t best) {
        int16_t level = Level(s.g, which);
        if (s.g.Read8(kBrokenInto) == 2) {
            level = best;
        }
        if (s.Weak()) {
            level = 0;
        }
        return level;
    }


    // --- small steps shared by the modes ---

    // 060111AC (Setup_EM_Rank_Index): the character's row of the level tables, the boss's 18
    bool BossRowLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(2) = kBossRank;
        g.R(3) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kBossRank)));
        g.R(0) = Signed(s.Row());
        g.SetT(g.R(3) == 0);
        g.Return(10);
        return true;
    }

    // 06008BF0 (Pattern_Insurance, r5 which kind, r6 the plan to fall back on): a plan past the character's last of
    // that kind is put back to r6. Leaves r5 the last plan's place, r7 kPattern, as the routine does.
    bool PlanCheckLift(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t last = kLastPlans + static_cast<uint32_t>(s.Character() * 4) + Signed(static_cast<int16_t>(g.R(5)));
        const uint8_t lastPlan = g.Read8(last);
        const int16_t plan = s.Word(kPattern);
        if (!(lastPlan >= plan)) {
            s.SetWord(kPattern, static_cast<int32_t>(g.R(6)));
        }
        g.R(0) = Signed(s.side * 2);
        g.R(2) = lastPlan;
        g.R(3) = Signed(plan);
        g.R(5) = last;
        g.R(7) = kPattern;
        g.SetT(lastPlan >= plan);
        g.Return(20);
        return true;
    }

    // 0600775A (Exit_Damage_Sub): out of a hit or a guard: everything cleared, then an answer to the other if one is
    // called for (Check_Passive), else free again. (The registers those leave.)
    void AfterHit(Guest &g, uint32_t me) {
        Frame frame(g, 8);
        Call(g, kClearFlags, me);
        if (Call(g, kCheckAnswer, me) != 0) {
            g.SetT(false);
            return;
        }
        Call(g, kBeFree, me);
    }
    void AfterHit(Side &s) {
        AfterHit(s.g, s.me);
    }
    bool AfterHitLift(Guest &g) {
        AfterHit(g, g.R(4));
        g.Return(20);
        return true;
    }

    // 0600846A (Be_Catch): the computer throwing: mode 15, everything cleared
    bool BeThrowingLift(Guest &g) {
        Side s(g, g.R(4));
        s.SetMode(kThrowing);
        Call(g, kClearFlags, s.me);
        g.Return(20);
        return true;
    }

    // 06008778 (Next_Be_Float): thrown into the air by a hit: mode 11, everything cleared, and what to do in the air
    // chosen (its step) by the level of four, at random. Leaves r0 the step, r3 the table, r4 the dice, as the
    // routine does.
    bool BeInAirLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        s.SetModeWord(0, kInAir);
        s.SetModeWord(2, 0);
        s.SetModeWord(3, 0);
        Call(g, kClearFlags, s.me);
        const int16_t level = Level(g, kLevelOf4);
        const int16_t dice = Dice(g, kDice64);
        const int8_t step = static_cast<int8_t>(g.Read8(kAirSteps + Signed(level * 16) + Signed(dice)));
        s.SetModeWord(1, step);
        g.R(0) = Signed(step);
        g.R(3) = kAirSteps;
        g.R(4) = Signed(dice);
        g.Return(40);
        return true;
    }

    // 060069F6 (Check_Guard_Type, r5 the attacker: the other or a projectile): the guard's lever: back, and down
    // too for a crouching guard (kGuardType 2) or, as the attack needs (0), against one guarded low but not high,
    // but for an attacker crouching (poses 14-30). Leaves the registers as the routine does.
    void GuardLever(Guest &g, uint32_t me, uint32_t attacker) {
        Frame frame(g, 20);
        Side s(g, me);
        g.R(3) = kLeverFor;
        s.SetWord(kLever, static_cast<int32_t>(LeverFor(g, me, 1)));
        const uint16_t type = static_cast<uint16_t>(s.Word(kGuardType));
        g.R(1) = kGuardType;
        const auto down = [&]() {
            g.R(3) = 2;
            g.R(0) = Signed(s.side * 2);
            g.R(2) = Signed(s.Word(kLever) | 2);
            s.SetWord(kLever, static_cast<int32_t>(g.R(2)));
            g.SetT(false);
        };
        switch (type) {
        case 0: {
            const uint8_t pose = g.Read8(attacker + kPose);
            g.R(0) = kPose;
            g.R(3) = 14;
            g.R(4) = pose;
            if (pose >= 14) {
                g.R(1) = 30;
                if (pose <= 30) {
                    g.SetT(false);
                    return;
                }
            }
            const uint8_t bits = g.Read8(attacker + kGuardBits);
            g.R(0) = bits;
            if (bits & 16) {
                g.SetT(false);
                return;
            }
            if (!(bits & 8)) {
                g.SetT(true);
                return;
            }
            down();
            return;
        }
        case 1:
            g.R(0) = 1;
            g.SetT(true);
            return;
        case 2:
            down();
            return;
        default:
            g.R(0) = type;
            g.SetT(false);
            return;
        }
    }
    bool GuardLeverLift(Guest &g) {
        GuardLever(g, g.R(4), g.R(5));
        g.Return(30);
        return true;
    }

    // 06011248 (Next_Be_Guard, r5 the attacker, r6 the guard): guard: mode 7 for 10 frames, a dash given up, the
    // guard's lever held
    void StartGuard(Guest &g, uint32_t me, uint32_t attacker, int16_t guard) {
        Frame frame(g, 16);
        Side s(g, me);
        s.SetMode(kGuardMode);
        s.SetWord(kTimer, 10);
        Call(g, kClearDash, Signed(s.side));
        s.SetWord(kGuardType, guard);
        GuardLever(g, me, attacker);
    }
    bool StartGuardLift(Guest &g) {
        StartGuard(g, g.R(4), g.R(5), static_cast<int16_t>(g.R(6)));
        g.Return(40);
        return true;
    }


    // --- parrying ---

    // 060112BC (r4 the other): may the other's attack be parried? Not a special move's (but when the side
    // kHumanSide plays character 15), nor a kind 0 or 1
    bool ParryableLift(Guest &g) {
        const uint32_t other = g.R(4);
        g.R(1) = kChosen;
        g.R(3) = kHumanSide;
        const uint8_t chosen = g.Read8(kChosen + g.Read8(kHumanSide));
        const uint8_t kind = g.Read8(other + kAttackKind);
        bool yes;
        if (chosen != kOroOrShin && (kind & 0xF8)) {
            yes = false;
            g.SetT(false);
        } else if (kind == 0) {
            g.R(3) = 0;
            yes = false;
            g.SetT(true);
        } else {
            g.R(3) = static_cast<uint32_t>(static_cast<int8_t>(kind));
            yes = kind != 1;
            g.SetT(!yes);
        }
        g.R(0) = yes ? 1 : 0;
        g.Return(16);
        return true;
    }

    // 060110E0: Makoto with her third super art ready (it adds to the parry's chance)
    bool MakotoReadyLift(Guest &g) {
        const uint32_t me = g.R(4);
        g.R(0) = 0;
        if (static_cast<int16_t>(g.Read16(me + kCharacter)) != kMakoto) {
            g.SetT(false);
        } else if (static_cast<int8_t>(g.Read8(g.Read32(me + kSuperArt) + 10)) != -1) {
            g.SetT(false);
        } else {
            const int32_t side = static_cast<int16_t>(g.Read16(me + kSide));
            const uint32_t offset = Signed(static_cast<int16_t>(side * kFighterSize));
            g.R(1) = kFighters + kSuperArt;
            g.R(3) = kFighterSize;
            const int16_t art = static_cast<int16_t>(g.Read16(g.Read32(kFighters + kSuperArt + offset)));
            g.SetT(art == 2);
            g.R(0) = art == 2 ? 2 : static_cast<uint32_t>(0);
        }
        g.Return(16);
        return true;
    }

    // 060111F0 (Flip_Term_Correct): taken from the dice of a parry: 1 at 48 health or less, 2 behind in rounds, 1 with
    // under 15 on the clock, 20 for Makoto's ready super art
    bool ParryOddsLift(Guest &g) {
        Frame frame(g, 12);
        Side s(g, g.R(4));
        const uint32_t offset = Signed(static_cast<int16_t>(s.side * kFighterSize));
        int16_t odds = static_cast<int16_t>(g.Read16(kFighters + kHealth + offset)) > 48 ? 0 : 1;
        if (static_cast<int16_t>(g.Read16(kWins + Signed(s.side * 2))) < static_cast<int16_t>(g.Read16(kWins + Signed((s.side ^ 1) * 2)))) {
            odds += 2;
        }
        if (static_cast<int16_t>(g.Read16(kClock)) < 15) {
            odds += 1;
        }
        g.R(5) = kWins;
        g.R(6) = Signed(s.side);
        g.R(4) = s.me;
        g.Call(0x060110E0);
        if (g.R(0) != 0) {
            odds += 20;
        }
        g.R(0) = Signed(odds);
        g.Return(30);
        return true;
    }

    // 0601111A (Check_Flip_Term): parry the other's attack? When it may be parried, a dice (less the odds) under the
    // character's chance by the level of eight (7 when a challenger broke in, 2 for the weak side)
    bool ParryPossibleLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        g.R(4) = s.Other();
        g.Call(0x060112BC);
        if (g.R(0) == 0) {
            g.R(0) = 0;
            g.Return(20);
            return true;
        }
        g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf8)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 7);
        }
        if (s.Weak()) {
            g.Write8(kLevelNow, 2);
        }
        g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice128)));
        g.R(4) = s.me;
        g.Call(0x060111F0);
        g.Write8(kDiceNow, static_cast<uint8_t>(g.Read8(kDiceNow) - g.R(0)));
        const uint32_t row = kParryChances + Signed(s.Row() * 8);
        const uint8_t chance = g.Read8(row + Signed(static_cast<int8_t>(g.Read8(kLevelNow))));
        const bool parry = !(static_cast<int8_t>(g.Read8(kDiceNow)) >= chance);
        g.R(1) = row;
        g.R(0) = parry ? 1 : 0;
        g.Return(40);
        return true;
    }

    // 06007B10 (Check_Flip_GO, r5 1 to parry in any case): the parry's lever, when the other's attack can hit (or
    // r5 says): forward, or down against a low attack (poses 32, 33). The same lever as the frame before (and r5
    // 0) is too late: a guard instead, and no more parries (kParries 0xFF). Else a parry more. Leaves the
    // registers as the routine does.
    bool ParryGo(Guest &g, uint32_t me, int16_t always) {
        Frame frame(g, 24);
        Side s(g, me);
        const uint32_t other = s.Other();
        const uint8_t hitOk = g.Read8(other + kHitOk);
        if (hitOk == 0 && always == 0) {
            g.R(1) = Signed(always);
            g.R(0) = 0;
            g.SetT(true);
            return false;
        }
        g.R(1) = hitOk != 0 ? static_cast<uint32_t>(static_cast<int8_t>(hitOk)) : Signed(always);
        const uint8_t pose = g.Read8(other + kPose);
        g.R(4) = pose;
        if (pose == 33 || pose == 32) {
            g.R(3) = 2;
            s.SetWord(kLever, 2);
        } else {
            g.R(3) = kLeverFor;
            s.SetWord(kLever, static_cast<int32_t>(LeverFor(g, me, 0)));
        }
        if (always == 0) {
            const uint16_t last = g.Read16(kLastLevers + Signed(static_cast<int8_t>(s.side * 40)));
            const uint16_t now = static_cast<uint16_t>(s.Word(kLever));
            g.R(1) = last;
            g.R(2) = now;
            g.R(3) = Signed(s.side * 2);
            g.R(4) = Signed(s.side);
            if (last == now) {
                g.R(6) = 0;
                CallWith(g, 0x06011248, me, other, 0);
                s.SetByte(kParries, 0xFF);
                g.R(2) = s.At(kParries, 1);
                g.R(3) = 0xFF;
                g.R(0) = 0;
                return false;
            }
        }
        const int32_t parries = static_cast<int8_t>(s.Byte(kParries)) + 1;
        s.SetByte(kParries, parries);
        g.R(1) = kParries;
        g.R(2) = s.At(kParries, 1);
        g.R(3) = Signed(parries);
        g.R(0) = 1;
        g.SetT(false);
        return true;
    }
    bool ParryGoLift(Guest &g) {
        ParryGo(g, g.R(4), static_cast<int16_t>(g.R(5)));
        g.Return(40);
        return true;
    }

    // 06011324 (Next_Be_Flip, r5 0, or the frames of a parry against a projectile): parry: mode 12 for 9 frames, the
    // count of parries started again. Against a projectile, the lever held at once (and its time kept); else as
    // Check_Flip_GO says.
    bool StartParryLift(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        const int16_t shellTime = static_cast<int16_t>(g.R(5));
        const uint32_t other = s.Other();
        s.SetMode(kParry);
        s.SetWord(kTimer, 9);
        s.SetByte(kParries, 0);
        if (shellTime == 0) {
            ParryGo(g, s.me, 0);
            g.Return(40);
            return true;
        }
        const uint8_t pose = g.Read8(other + kPose);
        if (pose == 33 || pose == 32) {
            s.SetWord(kLever, 2);
        } else {
            s.SetWord(kLever, static_cast<int32_t>(LeverFor(g, s.me, 0)));
        }
        s.SetModeWord(2, 1);
        s.SetWord(kTimerReload, shellTime);
        g.Return(40);
        return true;
    }

    // 06007E44: after a parry of a projectile, another? (a projectile's hit that goes on: kTimerReload 8, the
    // projectile strong enough): 1 a parry more; past three, the character's chance by the level of eight against
    // the dice (less the odds): -1 a guard instead. 0 not.
    int16_t ParryProjectileAgain(Side &s) {
        Frame frame(s.g, 12);
        Guest &g = s.g;
        s.SetByte(kParries, s.Byte(kParries) + 1);
        if (s.Word(kTimerReload) != 8) {
            return 0;
        }
        const uint32_t hitter = g.Read32(s.me + kHitter);
        if (!(static_cast<int16_t>(g.Read16(hitter + kHealth)) >= 0x100)) {
            return 0;
        }
        if (!(s.Byte(kParries) >= 3)) {
            return 1;
        }
        int32_t dice = Dice(g, kDice128);
        dice -= static_cast<int32_t>(Call(g, 0x060111F0, s.me));
        const uint16_t level = static_cast<uint16_t>(Level(g, kLevelOf8));
        const uint8_t chance = g.Read8(kShellParryChances + Signed(s.Character() * 8) + level);
        return static_cast<int32_t>(static_cast<uint16_t>(dice)) >= chance ? -1 : 1;
    }
    bool ParryProjectileAgainLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = Signed(ParryProjectileAgain(s));
        g.Return(40);
        return true;
    }

    // 06007EE8: after a parry, parry the next hit too? Not against a normal attack (kind 0 or 1: the lever let go);
    // under three parries yes; then the character's chance by the level of eight against the dice (less the odds)
    bool ParryAgainLift(Guest &g) {
        Frame frame(g, 12);
        Side s(g, g.R(4));
        const uint8_t kind = g.Read8(s.Other() + kAttackKind);
        bool again;
        if (kind == 0 || kind == 1) {
            s.SetWord(kLever, 0);
            again = false;
        } else if (!(s.Byte(kParries) >= 3)) {
            again = true;
        } else {
            int32_t dice = Dice(g, kDice128);
            dice -= static_cast<int32_t>(Call(g, 0x060111F0, s.me));
            const uint16_t level = static_cast<uint16_t>(Level(g, kLevelOf8));
            const uint8_t chance = g.Read8(kParryAgainChances + Signed(s.Character() * 8) + level);
            again = !(static_cast<int32_t>(static_cast<uint16_t>(dice)) >= chance);
        }
        g.R(0) = again ? 1 : 0;
        g.Return(40);
        return true;
    }

    // 06008048 (Check_Flip_Attack): an attack after the parry? A dice (less the odds) under the chance, by kRank,
    // the character and the level of eight (8 when a challenger broke in, 0 for the weak side): then answer 13 as a
    // counter attack. (r5-r7 as the calls leave them.)
    bool ParryAttack(Side &s) {
        Frame frame(s.g, 20);
        Guest &g = s.g;
        const int16_t level = LevelFor(s, kLevelOf8, 8);
        int32_t dice = Dice(g, kDice128);
        dice -= static_cast<int32_t>(Call(g, 0x060111F0, s.me));
        const int16_t row = static_cast<int16_t>(Call(g, 0x060111AC, s.me));
        const uint32_t chances = kParryAttackChances + Signed(static_cast<int16_t>(g.Read8(kRank) * 0xA8)) + Signed(row * 8);
        const int8_t chance = static_cast<int8_t>(g.Read8(chances + Signed(level)));
        if (static_cast<int16_t>(dice) >= chance) {
            g.R(0) = 0;
            return false;
        }
        s.SetByte(kRecovering, 0);
        s.SetWord(kAnswer, 13);
        s.SetByte(kCounter, 1);
        g.R(0) = 1;
        return true;
    }
    bool ParryAttackLift(Guest &g) {
        Side s(g, g.R(4));
        ParryAttack(s);
        g.Return(40);
        return true;
    }


    // --- the modes ---

    // 060067EC (Check_Counter_Attack and Check_Hamari): a counter attack to the other's last attack? Not when far
    // (range 3). By its kind: 8 answer 28, 24, 32 and 48 answer 14 (the result then the side's offset, as the game's
    // code leaves it: nothing for player 1). Else, close or middle, the other repeating the same attack (of kind 0 or
    // 1, or Hugo's 3) three or four times at random: answer 32.
    int32_t CounterAttack(Side &s) {
        Guest &g = s.g;
        if (s.Word(kRange) >= 3) {
            return 0;
        }
        switch (s.Byte(kOtherAttackKind) & 0xF8) {
        case 8:
            s.SetWord(kAnswer, 28);
            return s.side * 2;
        case 24:
        case 32:
        case 48:
            s.SetWord(kAnswer, 14);
            return s.side * 2;
        }
        if (s.Word(kRange) >= 2) {
            return 0;
        }
        const uint8_t kind = g.Read8(s.At(kAttackKinds, 4));
        int16_t times = static_cast<int16_t>((Dice(g, kDice128) & 1) + 3);
        if (static_cast<int16_t>(g.Read16(s.Other() + kCharacter)) == 4 && kind == 3) {
            times--;
        } else if (kind != 0 && kind != 1) {
            return 0;
        }
        for (int16_t n = 1; n < times; n++) {
            if (g.Read8(s.At(kAttackKinds, 4) + static_cast<uint32_t>(n)) != kind) {
                return 0;
            }
        }
        s.SetWord(kAnswer, 32);
        return 32;
    }

    // 06006B80 (Ck_Exit_Guard_Sub): is the other's attack still coming? While it is out: when the computer is hit
    // (blocking it: but its first frames, 4-7 of the hit, ...), or the other is attacking
    bool StillAttacked(Side &s, uint32_t other) {
        Guest &g = s.g;
        if (s.Byte(kOtherAttacking) == 0) {
            return false;
        }
        if (s.Field(kDoing) == kDoingHit) {
            if (s.Field(kDoingStep) == 0) {
                return true;
            }
            const int16_t how = s.Field(kDoingHow);
            if (how >= 4 && how <= 7 && s.Field(0x186) == 0 && s.Byte(kOtherAttacking) == 0) {
                return false;
            }
            return true;
        }
        if (static_cast<int16_t>(g.Read16(other + kDoing)) != kDoingAttack) {
            return false;
        }
        return s.Byte(kOtherAttacking) != 0;
    }

    // 06006A90 (Ck_Exit_Guard): go on guarding? Yes for the guard's frames (kTimer), and after them while the
    // attack is still coming; a new attack of the other's (kAttacksSeen) chooses the guard again: by the guard's
    // level of ten (10 when a challenger broke in, 2 for the weak side, plus the bonus) and the dice
    bool GoOnGuarding(Side &s, uint32_t other) {
        Guest &g = s.g;
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        if (s.Word(kTimer) != 0) {
            return true;
        }
        s.SetWord(kTimer, 1);
        if (!StillAttacked(s, other)) {
            return false;
        }
        if (s.Word(kGuardedAttack) == s.Word(kAttacksSeen)) {
            return true;
        }
        s.SetWord(kGuardedAttack, s.Word(kAttacksSeen));
        int16_t level = Level(g, kLevelOf10);
        if (g.Read8(kBrokenInto) == 2) {
            level = 10;
        }
        if (s.Weak()) {
            level = 2;
        }
        level = static_cast<int16_t>(level + g.Read8(kLevelBonus));
        const uint32_t row = g.Read8(kBossRank) != 0 ? 0xC60 : Signed(static_cast<int16_t>(s.Character() * 0xB0));
        const uint32_t choices = kGuardChoices + row + Signed(level * 16);
        s.SetWord(kGuardType, static_cast<int8_t>(g.Read8(choices + static_cast<uint32_t>(Dice(g, kDice64)))));
        return true;
    }

    // 060066E8 (Com_Guard), mode 7: unless hit, thrown or parrying, or thrown into the air (Next_Be_Float), it guards
    // while the attack lasts. Then an answer to the other if one is called for (Ck_Passive_Term: the reaction played,
    // not to be broken off), or a counter attack, or free again.
    void Guard(Side &s) {
        Guest &g = s.g;
        if (Call(g, kCheckHit, s.me) || Call(g, kCheckCaught, s.me) || Call(g, kCheckRecovery, s.me)) {
            return;
        }
        if (s.Field(kDoing) == kDoingHit && g.Read8(kKnockedDown + Signed(s.Field(kDoingHow))) == 2) {
            g.R(4) = s.me;
            g.Call(0x06008778);
            return;
        }
        const uint32_t other = s.Other();
        if (GoOnGuarding(s, other)) {
            GuardLever(g, s.me, other);
            return;
        }
        s.SetByte(kAnswering, 0);
        g.Write8(kPassiveMode, 4);
        if (Call(g, kChooseAnswer, s.me) != 0) {
            Call(g, kReact, s.me);
            s.SetByte(kCounter, s.Byte(kCounter) | 2);
            return;
        }
        if (CounterAttack(s) == 0) {
            Call(g, kBeFree, s.me);
            return;
        }
        if (static_cast<int32_t>(Call(g, kReact, s.me)) == -1) {
            Call(g, kBeFree, s.me);
        }
    }

    // 060069C6 (Check_No12_Shell_Guard): the projectile more than 48 behind the computer (Twelve's guard of one)
    bool ShellPassed(Guest &g, uint32_t me, uint32_t shell) {
        const int16_t x = static_cast<int16_t>(g.Read16(me + kX)), shellX = static_cast<int16_t>(g.Read16(shell + kX));
        if (g.Read8(me + kFacesRight) != 0) {
            return shellX < static_cast<int16_t>(x - 48);
        }
        return shellX > static_cast<int16_t>(x + 48);
    }

    // 06006914 (Com_Guard_VS_Shell), mode 9: unless thrown or parrying, it guards the projectile (kShell) for the
    // frames of kTimer; then, once it is no longer hit (Twelve: once the projectile has passed it), or the projectile
    // is gone, it is out (Exit_Damage_Sub, as often as one of those holds), and looks again the next frame.
    void GuardProjectile(Side &s) {
        Guest &g = s.g;
        if (Call(g, kCheckCaught, s.me) || Call(g, kCheckRecovery, s.me)) {
            return;
        }
        const uint32_t shell = g.Read32(s.At(kShell, 4));
        GuardLever(g, s.me, shell);
        if (s.Word(kTimer) != 0) {
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            return;
        }
        if (s.Character() == kTwelve) {
            if (ShellPassed(g, s.me, shell)) {
                AfterHit(s);
            }
        } else if (s.Field(kDoing) != kDoingHit) {
            AfterHit(s);
        }
        if (static_cast<int16_t>(g.Read16(shell + kDoing - 2)) == 2) { // (its routine_no[0], +0x24: 2 going)
            AfterHit(s);
        }
        if (static_cast<int16_t>(g.Read16(shell + kSide)) != 13) { // (its id: 13 a projectile)
            AfterHit(s);
        }
        s.SetWord(kTimer, 1);
    }

    // 06006C22 (Com_Active) and 06006D30 (Com_VS_Shell), modes 2 and 8: unless hit, thrown or parrying, the plan is
    // checked (Pattern_Insurance, the kind and the plan to fall back on) and the character's routine plays it
    void ByCharacter(Side &s, uint32_t routines, int kind) {
        Guest &g = s.g;
        if (Call(g, kCheckHit, s.me) || Call(g, kCheckCaught, s.me) || Call(g, kCheckRecovery, s.me)) {
            return;
        }
        CallWith(g, 0x06008BF0, s.me, static_cast<uint32_t>(kind), 0);
        Call(g, g.Read32(routines + static_cast<uint32_t>(s.Character()) * 4), s.me);
    }
    void Active(Side &s) {
        ByCharacter(s, kActives, 0);
    }
    void VsProjectile(Side &s) {
        ByCharacter(s, kVsShells, 2);
    }


    // --- mode 10: hit ---

    // The step to get up with checked against the range: where the character's table says it is not for this range
    // (1), the step it gives instead
    void CheckGetUp(Side &s) {
        Guest &g = s.g;
        const uint32_t checks = kGetUpChecks + Signed(static_cast<int16_t>(s.Character() * 40));
        const uint8_t step = g.Read8(s.ModeAt(1) + 1);
        const uint32_t row = checks + Signed(static_cast<int8_t>((step - 1) * 5));
        if (g.Read8(row + Signed(s.Word(kRange))) == 1) {
            s.SetModeWord(1, g.Read8(checks + Signed(static_cast<int8_t>(step * 5)) + 5));
        }
    }

    // 06006E00 (Damage_1st): hit: the lever back and down. First: to stand at once (py) step 9; not knocked down,
    // step 1; knocked down, it may hold down as it falls (kHitSeen: a chance by the level of eight). On the floor
    // (its step 1, the fighter's step past 0): how to get up, by the level of four (3 broken into, 0 weak) at random,
    // checked against the range; a guard as it rises (step 0 still) chosen by the guard's level of ten.
    void HitFirst(Side &s) {
        Guest &g = s.g;
        s.SetWord(kLever, static_cast<int32_t>(LeverFor(g, s.me, 1)));
        s.SetWord(kLever, s.Word(kLever) | 2);
        switch (s.ModeWord(2)) {
        case 0: {
            if (g.Read8(g.Read32(s.me + kPy)) != 0) {
                s.SetModeWord(1, 9);
                return;
            }
            if (g.Read8(kKnockedDown + Signed(s.Field(kDoingHow))) == 0) {
                s.SetModeWord(1, 1);
                return;
            }
            s.SetModeWord(2, s.ModeWord(2) + 1);
            const int16_t level = LevelFor(s, kLevelOf8, 8);
            const int8_t dice = static_cast<int8_t>(Dice(g, kDice128));
            const uint8_t row = static_cast<uint8_t>(Call(g, 0x060111AC, s.me));
            const int8_t chance = static_cast<int8_t>(g.Read8(kHoldDown + row * 8u + static_cast<uint8_t>(level)));
            if (static_cast<uint32_t>(static_cast<int32_t>(chance)) > static_cast<uint32_t>(static_cast<int32_t>(dice))) {
                s.SetByte(kHitSeen, 1);
            }
            return;
        }
        case 1: {
            if (s.Field(kDoingStep) == 0) {
                s.SetModeWord(2, 0);
                return;
            }
            const int16_t level = LevelFor(s, kLevelOf4, 3);
            const uint8_t dice = static_cast<uint8_t>(Dice(g, kDice128));
            const uint32_t getUps = kGetUps + Signed(s.Character() * 128) + static_cast<uint8_t>(level) * 32u;
            s.SetModeWord(1, g.Read8(getUps + dice) + 1);
            s.SetModeWord(2, 0);
            CheckGetUp(s);
            if (s.ModeWord(1) != 0) {
                return;
            }
            int16_t guardLevel = LevelFor(s, kLevelOf10, 10);
            const uint8_t guardDice = static_cast<uint8_t>(Dice(g, kDice64));
            guardLevel = static_cast<int16_t>(guardLevel + static_cast<int8_t>(g.Read8(kLevelBonus)));
            const uint32_t level16 = static_cast<uint8_t>(guardLevel) * 16u;
            const uint32_t row = g.Read8(kBossRank) != 0 ? 0xC60 : Signed(static_cast<int16_t>(s.Character() * 0xB0));
            s.SetWord(kGuardType, static_cast<int8_t>(g.Read8(kGuardChoices + row + level16 + guardDice)));
            GuardLever(g, s.me, s.Other());
            return;
        }
        }
    }

    // 06007106 (Damage_2nd): the guard's lever while hit; stunned, step 9; holding down if it was to (kHitSeen) and
    // the fighter may; out of the hit, Exit_Damage_Sub
    void HitSecond(Side &s) {
        Guest &g = s.g;
        GuardLever(g, s.me, s.Other());
        if (s.Field(kDoingHow) == kStunned) {
            s.SetModeWord(1, 9);
            s.SetModeWord(2, 0);
            return;
        }
        if (s.Byte(kHitSeen) != 0 && g.Read8(kFighters + 0x402 + Signed(static_cast<int16_t>(s.side * kFighterSize))) != 0) {
            s.SetWord(kLever, 2);
        }
        if (s.Field(kDoing) != kDoingHit) {
            AfterHit(s);
        }
    }

    // 06007186 (Damage_5th): getting up with move 46 (power 8): once the fighter's rise begins (kMoveKind 9) the
    // move is put in, then waited out
    void HitFifth(Side &s) {
        Guest &g = s.g;
        if (s.Field(kDoingStep) == 0) {
            s.SetModeWord(1, 0);
            s.SetModeWord(2, 0);
            return;
        }
        switch (s.ModeWord(2)) {
        case 0:
            if (s.Field(kDoing) != kDoingHit) {
                AfterHit(s);
                return;
            }
            if (s.FieldByte(kMoveKind) == 9) {
                s.SetModeWord(2, s.ModeWord(2) + 1);
                s.SetPlace(1, 0);
            }
            return;
        case 1:
            g.R(7) = 8;
            CallWith(g, kSpecialMove, s.me, Signed(s.Character()), 46);
            if (g.R(0) != 0) {
                s.SetModeWord(2, s.ModeWord(2) + 1);
            }
            return;
        default:
            if (s.Field(kDoing) != kDoingAttack || s.FieldByte(kMoveKind) == 64) {
                AfterHit(s);
            }
            return;
        }
    }

    // The special move chosen to get up with (kPlanPlace 0, its power 7) put in (Command_Attack_SP): true once done
    bool GetUpMove(Side &s) {
        s.g.R(7) = Signed(s.Place(7));
        CallWith(s.g, kSpecialMove, s.me, Signed(s.Character()), Signed(s.Place(0)));
        return s.g.R(0) != 0;
    }

    // 06007262 (Damage_6th): getting up with a special move: as the fighter's rise begins (kMoveKind 12) the move,
    // and its power, chosen by the level of four at random (0xFF: the first, with power 8, or the super art's when
    // it is ready); it is put in, and once it is done, Exit_Damage_Sub
    void HitSixth(Side &s) {
        Guest &g = s.g;
        if (s.Field(kDoingStep) == 0) {
            s.SetModeWord(1, 0);
            s.SetModeWord(2, 0);
            return;
        }
        if (s.Field(kDoingHow) == kStunned) {
            s.SetModeWord(1, 9);
            s.SetModeWord(2, 0);
            return;
        }
        s.SetWord(kLever, static_cast<int32_t>(LeverFor(g, s.me, 1)));
        s.SetWord(kLever, s.Word(kLever) | 2);
        switch (s.ModeWord(2)) {
        case 0: {
            if (s.Field(kDoing) != kDoingHit) {
                AfterHit(s);
                return;
            }
            if (s.FieldByte(kMoveKind) != 12) {
                return;
            }
            CheckGetUp(s);
            s.SetModeWord(2, s.ModeWord(2) + 1);
            s.SetPlace(1, 0);
            const int16_t level = LevelFor(s, kLevelOf4, 3);
            const uint32_t pick = static_cast<uint8_t>((Dice(g, kDice128) & 3) * 2) * 2u;
            const uint32_t moves = kGetUpMoves + Signed(s.Character() * 64) + static_cast<uint8_t>(level) * 16u;
            s.SetPlace(0, static_cast<int16_t>(g.Read16(moves + pick)));
            s.SetPlace(7, static_cast<int16_t>(g.Read16(moves + pick + 2)));
            if (s.Place(0) != 0xFF) {
                return;
            }
            s.SetPlace(0, static_cast<int16_t>(g.Read16(moves)));
            s.SetPlace(7, 8);
            const uint32_t art = g.Read32(kFighters + kSuperArt + Signed(static_cast<int16_t>(s.side * kFighterSize)));
            if (g.Read8(art + 10) == 0) {
                return;
            }
            const uint32_t arts = kSuperArtMoves + Signed(static_cast<int8_t>(static_cast<int8_t>(s.FieldByte(kCharacter + 1)) * 6));
            const int16_t move = static_cast<int16_t>(g.Read16(arts + Signed(static_cast<int16_t>(g.Read16(art)) * 2)));
            if (move != -1) {
                s.SetPlace(0, move);
            }
            return;
        }
        case 1:
            if (GetUpMove(s)) {
                s.SetModeWord(2, s.ModeWord(2) + 1);
            }
            return;
        default:
            if (GetUpMove(s)) {
                AfterHit(s);
            }
            return;
        }
    }

    // 060075BC (Damage_7th, steps 6-8): getting up guarding: 6 as the attack needs, 7 standing, 8 crouching; held
    // until the fighter is free and the other's attack over, then Exit_Damage_Sub
    void HitSeventh(Side &s) {
        Guest &g = s.g;
        if (s.ModeWord(2) == 0) {
            if (s.Field(kDoing) != kDoingHit) {
                AfterHit(s);
                return;
            }
            s.SetModeWord(2, s.ModeWord(2) + 1);
            const int16_t step = s.ModeWord(1);
            s.SetWord(kGuardType, step == 6 ? 0 : step == 7 ? 1 : 2);
            return;
        }
        GuardLever(g, s.me, s.Other());
        if (s.FieldByte(kMoveKind) != 64 && s.Field(kDoing) != kDoingFree) {
            return;
        }
        if (s.Byte(kOtherAttacking) != 0) {
            return;
        }
        AfterHit(s); // (the game's code also looks at byte 0x3C7 here, but never gets there)
    }

    // 06007682 (Damage_8th, step 9): stunned: the buttons mashed (at a speed by the level of eight, 8 broken into, 0
    // weak, at random) while it lasts; out of the hit, Exit_Damage_Sub
    void HitStunned(Side &s) {
        Guest &g = s.g;
        if (s.Field(kDoing) != kDoingHit) {
            AfterHit(s);
        }
        switch (s.ModeWord(2)) {
        case 0: {
            if (s.Field(kDoingHow) != kStunned) {
                return;
            }
            s.SetModeWord(2, s.ModeWord(2) + 1);
            s.SetWord(kTimer, 1);
            const int16_t level = LevelFor(s, kLevelOf8, 8);
            const int16_t dice = static_cast<int16_t>(Dice(g, kDice64) & 7);
            s.SetWord(kTimerReload, g.Read8(kStunMashes + Signed(level * 8) + Signed(dice)));
            return;
        }
        case 1:
            CallWith(g, kMash, s.me, 0, s.ModeAt(3));
            s.SetWord(kLever, static_cast<int32_t>(g.R(0)));
            return;
        }
    }

    void HitThird(Side &) {}
    void HitFourth(Side &) {}

    // 06006D80 (Com_Damage), mode 10: unless thrown, its step
    void Hit(Side &s) {
        if (Call(s.g, kCheckCaught, s.me)) {
            return;
        }
        switch (s.ModeWord(1)) {
        case 0: HitFirst(s); break;
        case 1: HitSecond(s); break;
        case 2: HitThird(s); break;
        case 3: HitFourth(s); break;
        case 4: HitFifth(s); break;
        case 5: HitSixth(s); break;
        case 6:
        case 7:
        case 8: HitSeventh(s); break;
        case 9: HitStunned(s); break;
        default: Call(s.g, s.g.Read32(0x0613E694 + Signed(s.ModeWord(1) * 4)), s.me); break; // (not met)
        }
    }


    // --- mode 11: in the air ---

    // 06007862 (Float_2nd): the air recovery: button 16 once, then free once the fighter is, or hit again
    void AirRecover(Side &s) {
        if (s.ModeWord(2) == 0) {
            s.SetModeWord(2, s.ModeWord(2) + 1);
            s.SetWord(kLever, 16);
            return;
        }
        if (s.Field(kDoing) == kDoingFree) {
            Call(s.g, kBeFree, s.me);
            return;
        }
        Call(s.g, kCheckHit, s.me);
    }

    // 060078A6 and 06007988 (Float_3rd, Float_4th): the lever toward (0) or away from (1) the other, every 3 frames
    // after the first 4; out of the hit, free (and the step goes on all the same)
    void AirLever(Side &s, uint32_t way) {
        Guest &g = s.g;
        if (s.Field(kDoing) != kDoingHit) {
            Call(g, kBeFree, s.me);
        }
        if (s.ModeWord(2) == 0) {
            s.SetModeWord(2, s.ModeWord(2) + 1);
            s.SetWord(kTimer, 4);
            s.SetWord(kJumpLever, static_cast<int32_t>(LeverFor(g, s.me, way)));
            s.SetWord(kLever, s.Word(kJumpLever));
            return;
        }
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        if (s.Word(kTimer) != 0) {
            return;
        }
        s.SetWord(kTimer, 3);
        s.SetWord(kLever, s.Word(kJumpLever));
    }
    void AirToward(Side &s) {
        AirLever(s, 0);
    }
    void AirAway(Side &s) {
        AirLever(s, 1);
    }

    // 06007818 (Com_Float), mode 11: unless thrown or parrying, its step: 0 as a hit's second (Damage_2nd), 1 an air
    // recovery, 2 toward, 3 away
    void InAir(Side &s) {
        if (Call(s.g, kCheckCaught, s.me) || Call(s.g, kCheckRecovery, s.me)) {
            return;
        }
        switch (s.ModeWord(1)) {
        case 0: HitSecond(s); break;
        case 1: AirRecover(s); break;
        case 2: AirToward(s); break;
        case 3: AirAway(s); break;
        default: Call(s.g, s.g.Read32(0x0613E6BC + Signed(s.ModeWord(1) * 4)), s.me); break; // (not met)
        }
    }


    // --- mode 12: parrying ---

    // 06007A6C (Flip_Zero): the parry: while the other attacks, its lever at the moment its attack can hit
    // (Check_Flip_GO); then 9 frames to parry again (Check_Flip), then Exit_Damage_Sub. The other's attack over first,
    // Exit_Damage_Sub at once.
    void ParryFirst(Side &s) {
        Guest &g = s.g;
        const uint32_t other = s.Other();
        switch (s.ModeWord(2)) {
        case 0:
            if (static_cast<int16_t>(g.Read16(other + kDoing)) != kDoingAttack) {
                AfterHit(s);
                return;
            }
            if (!ParryGo(g, s.me, 0)) {
                return;
            }
            s.SetModeWord(2, s.ModeWord(2) + 1);
            s.SetWord(kTimer, 9);
            return;
        case 1:
            if (Call(g, kCheckRecovery, s.me)) {
                return;
            }
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            if (s.Word(kTimer) == 0) {
                AfterHit(s);
            }
            return;
        }
    }

    // 06007BC6 (Flip_1st): the parry made in the air: on the floor again, Exit_Damage_Sub
    void ParryInAir(Side &s) {
        if (!(s.Field(kY) > 0)) {
            AfterHit(s);
        }
    }

    // 06007BD6 (Flip_2nd): the parry done (the fighter out of its parry): an attack after it (Check_Flip_Attack) or
    // Exit_Damage_Sub. Character 15 first parries again where it can (another parry, step 0 again, or answer 31), or
    // gives up while the other's attack goes on
    void ParryDone(Side &s) {
        Guest &g = s.g;
        if (g.Read8(kRecoverable + Signed(s.Field(kDoingHow))) != 0) {
            return;
        }
        const uint32_t other = s.Other();
        const auto react = [&]() {
            if (static_cast<int32_t>(Call(g, kReact, s.me)) == -1) {
                AfterHit(s);
            }
        };
        if (s.Character() != kOroOrShin) {
            if (ParryAttack(s)) {
                react();
            } else {
                AfterHit(s);
            }
            return;
        }
        if (ParryGo(g, s.me, 0)) {
            g.R(4) = s.me;
            g.Call(0x06007EE8);
            if (g.R(0) != 0) {
                s.SetModeWord(1, 0);
                s.SetModeWord(2, 1);
                return;
            }
            if (ParryAttack(s)) {
                s.SetWord(kAnswer, 31);
                s.SetByte(kParries, 0xFF);
                react();
            } else {
                AfterHit(s);
            }
            return;
        }
        if (static_cast<int32_t>(g.Read8(other + 0x212)) > static_cast<int16_t>(g.Read16(other + 0x458)) && s.Byte(kOtherAttacking) != 0) {
            return;
        }
        if (ParryAttack(s)) {
            s.SetWord(kAnswer, 13);
            react();
        } else {
            AfterHit(s);
        }
    }

    // 06007D28 (Flip_3rd): a parry of a projectile: another (step 2), or a parry of its next hit in 12 frames (step
    // 4), or else guard it: mode 9 for 10 frames, no more parries, a dash given up, the lever back
    void ParryProjectile(Side &s) {
        Guest &g = s.g;
        if (g.Read8(kRecoverable + Signed(s.Field(kDoingHow))) == 0) {
            return;
        }
        switch (ParryProjectileAgain(s)) {
        case 0:
            s.SetModeWord(1, 2);
            return;
        case 1:
            s.SetModeWord(1, 4);
            s.SetWord(kTimer, 12);
            return;
        default:
            s.SetMode(kGuardProjectile);
            s.SetWord(kTimer, 10);
            s.SetByte(kParries, 0xFF);
            Call(g, kClearDash, Signed(s.side));
            s.SetWord(kLever, static_cast<int32_t>(LeverFor(g, s.me, 1)));
            return;
        }
    }

    // 06007DE4 (Flip_4th): the next parry, after kTimer: its lever (Check_Flip_GO, in any case; not counted), then
    // step 0's wait
    void ParryNext(Side &s) {
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        if (s.Word(kTimer) != 0) {
            return;
        }
        ParryGo(s.g, s.me, 1);
        s.SetByte(kParries, s.Byte(kParries) - 1);
        s.SetModeWord(1, 0);
        s.SetModeWord(2, 1);
        s.SetWord(kTimer, 9);
    }

    // 06007A1E (Com_Flip), mode 12: unless hit or thrown, its step
    void Parrying(Side &s) {
        if (Call(s.g, kCheckHit, s.me) || Call(s.g, kCheckCaught, s.me)) {
            return;
        }
        switch (s.ModeWord(1)) {
        case 0: ParryFirst(s); break;
        case 1: ParryInAir(s); break;
        case 2: ParryDone(s); break;
        case 3: ParryProjectile(s); break;
        case 4: ParryNext(s); break;
        default: Call(s.g, s.g.Read32(0x0613E6CC + Signed(s.ModeWord(1) * 4)), s.me); break; // (not met)
        }
    }


    // --- mode 13: caught ---

    // 0600826C (Decide_Exit_Catch): break the throw? A dice under the chance by the character and the level of 18
    // (from kThrowLevel, plus the bonus; 19 when a challenger broke in)
    bool BreakThrow(Side &s) {
        Frame frame(s.g, 16);
        Guest &g = s.g;
        g.R(4) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kThrowLevel)));
        g.Call(kLevelOf18);
        int16_t level = static_cast<int16_t>(g.R(0) + g.Read8(kLevelBonus));
        if (g.Read8(kBrokenInto) == 2) {
            level = 19;
        }
        const uint8_t dice = static_cast<uint8_t>(Dice(g, kDice128));
        const int16_t row = static_cast<int16_t>(Call(g, 0x060111AC, s.me));
        const uint8_t chance = g.Read8(kThrowBreaks + Signed(static_cast<int16_t>(row * 18)) + Signed(level));
        return !(dice >= chance);
    }
    bool BreakThrowLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = BreakThrow(s) ? 1 : 0;
        g.Return(30);
        return true;
    }

    // 06008126 (Com_Caught), mode 13: thrown. First, against a throw that can be broken (move 1), the buttons mashed
    // after 12 frames at a speed by the level of eight (8 broken into, 0 weak), at random; else the throw broken at
    // once or not (Decide_Exit_Catch). Then, held, the mash; let go, free; hit, mode 10.
    void Caught(Side &s) {
        Guest &g = s.g;
        const uint32_t other = s.Other();
        switch (s.ModeWord(1)) {
        case 0:
            s.SetModeWord(1, s.ModeWord(1) + 1);
            s.SetModeWord(2, 0);
            if (g.Read8(other + kMoveId) == 1) {
                s.SetWord(kTimer, 12);
                const int16_t level = LevelFor(s, kLevelOf8, 8);
                const int16_t dice = static_cast<int16_t>(Dice(g, kDice64) & 7);
                s.SetWord(kTimerReload, g.Read8(kThrowMashes + Signed(level * 8) + Signed(dice)));
                return;
            }
            s.SetWord(kTimer, BreakThrow(s) ? 1 : 0);
            s.SetWord(kTimerReload, 1);
            return;
        case 1:
            if (s.Field(kDoing) == kDoingThrown) {
                CallWith(g, kMash, s.me, 0x3F0, s.ModeAt(2));
                s.SetWord(kLever, static_cast<int32_t>(g.R(0)));
                return;
            }
            if (s.Field(kDoing) == kDoingFree) {
                Call(g, kBeFree, s.me);
                return;
            }
            Call(g, kCheckHit, s.me);
            return;
        }
    }


    // --- the lifts ---
    template <void (*F)(Side &)>
    bool Lifted(Guest &g) {
        Side s(g, g.R(4));
        F(s);
        g.Return(60);
        return true;
    }

    // (free: everything, for the modes and their steps, called through tables by routines that read nothing after;
    // r1-r7 and T for a routine with a result whose callers keep nothing there)
    constexpr uint32_t kAll = 0x17FFF, kResult = 0x100FE;
    const Lift kLifts[] = {
        {"ComputerBossRow", 0x060111AC, 0x14, 0x8D1E57EC6021F127ull, BossRowLift, true, kResult},
        {"ComputerPlanCheck", 0x06008BF0, 0x28, 0x38D66E1E1890153Full, PlanCheckLift, true, 0x13F1F},
        {"ComputerAfterHit", 0x0600775A, 0x22, 0x4B30DF093694955Bull, AfterHitLift, true},
        {"ComputerBeThrowing", 0x0600846A, 0x3A, 0xB0334850A63F526Aull, BeThrowingLift, true},
        // {"ComputerBeInAir", 0x06008778, 0x6C, 0x7C5ACF5E983BC48Dull, BeInAirLift, true},
        {"ComputerGuardLever", 0x060069F6, 0x86, 0xA6B5B0768C73F48Bull, GuardLeverLift, true},
        {"ComputerStartGuard", 0x06011248, 0x74, 0xA4E628A525D50F87ull, StartGuardLift, true, 0x120FF},
        {"ComputerParryable", 0x060112BC, 0x3C, 0x6E593511FB30C57Cull, ParryableLift, true, 0x130FE},
        {"ComputerMakotoReady", 0x060110E0, 0x3A, 0xBBD4BA2ED61B4859ull, MakotoReadyLift, true, kResult},
        {"ComputerParryOdds", 0x060111F0, 0x58, 0x7705C5187224CCB9ull, ParryOddsLift, true, kResult},
        {"ComputerParryPossible", 0x0601111A, 0x92, 0xEF46A17A9857DF09ull, ParryPossibleLift, true, 0x1003C},
        {"ComputerParryGo", 0x06007B10, 0xB6, 0xD755A06BC727ACF5ull, ParryGoLift, true},
        {"ComputerStartParry", 0x06011324, 0xD2, 0xABA3B32B9AAEDEFBull, StartParryLift, true, 0x179FF},
        // {"ComputerParryProjectileAgain", 0x06007E44, 0xA4, 0x71D73F1C8B61B8AFull, ParryProjectileAgainLift, true, kAll},
        // {"ComputerParryAgain", 0x06007EE8, 0x7C, 0xB8DE9D8AB37BFF18ull, ParryAgainLift, true, kResult},
        {"ComputerParryAttack", 0x06008048, 0xDE, 0xE74E92FF41561850ull, ParryAttackLift, true, 0x1201E},
        {"ComputerBreakThrow", 0x0600826C, 0x6A, 0x5090487D780E83FAull, BreakThrowLift, true, kAll},
        {"ComputerActive", 0x06006C22, 0x50, 0x9D66CBDA1A336A0Aull, Lifted<Active>, true, kAll},
        // {"ComputerVsProjectile", 0x06006D30, 0x50, 0x7E59C4793D089352ull, Lifted<VsProjectile>, true, kAll},
        {"ComputerGuards", 0x060066E8, 0x104, 0xDB11F78608FDAE1Eull, Lifted<Guard>, true, kAll},
        {"ComputerGuardsProjectile", 0x06006914, 0xB2, 0xB095507E9828CF96ull, Lifted<GuardProjectile>, true, kAll},
        {"ComputerHit", 0x06006D80, 0x5C, 0xB65E7CC798D3CC9Dull, Lifted<Hit>, true, kAll},
        {"ComputerHitFirst", 0x06006E00, 0x306, 0xF5B0F617EF248564ull, Lifted<HitFirst>, true, kAll},
        {"ComputerHitSecond", 0x06007106, 0x78, 0x0D65FADC087B4F90ull, Lifted<HitSecond>, true, kAll},
        // {"ComputerHitFifth", 0x06007186, 0xDC, 0x7A50FBA1992170DEull, Lifted<HitFifth>, true, kAll},
        {"ComputerHitSixth", 0x06007262, 0x35A, 0xA625F0FA7C8E4140ull, Lifted<HitSixth>, true, kAll},
        {"ComputerHitSeventh", 0x060075BC, 0xC6, 0x09F2873D3E6BF0B1ull, Lifted<HitSeventh>, true, kAll},
        // {"ComputerHitStunned", 0x06007682, 0xD8, 0xB53A3E882F5C9CA5ull, Lifted<HitStunned>, true, kAll},
        // {"ComputerInAir", 0x06007818, 0x4A, 0x8E20948A4799E7CAull, Lifted<InAir>, true, kAll},
        // {"ComputerAirRecover", 0x06007862, 0x44, 0x0EE3F35C37E0B457ull, Lifted<AirRecover>, true, kAll},
        // {"ComputerAirToward", 0x060078A6, 0xE2, 0x8D76D97A044440FDull, Lifted<AirToward>, true, kAll},
        // {"ComputerAirAway", 0x06007988, 0x96, 0xBBBC2D91930C76F4ull, Lifted<AirAway>, true, kAll},
        {"ComputerParrying", 0x06007A1E, 0x4E, 0x4780079B2C108B64ull, Lifted<Parrying>, true, kAll},
        {"ComputerParryFirst", 0x06007A6C, 0x84, 0x398A56622B90502Cull, Lifted<ParryFirst>, true, kAll},
        // {"ComputerParryInAir", 0x06007BC6, 0x10, 0x74ED3FE551432F5Bull, Lifted<ParryInAir>, true, kAll},
        {"ComputerParryDone", 0x06007BD6, 0x13C, 0xDD6920BC45884077ull, Lifted<ParryDone>, true, kAll},
        // {"ComputerParryProjectile", 0x06007D28, 0xBC, 0x801AF44EB150DF8Full, Lifted<ParryProjectile>, true, kAll},
        // {"ComputerParryNext", 0x06007DE4, 0x60, 0x987CDB07F8223C5Aull, Lifted<ParryNext>, true, kAll},
        {"ComputerCaught", 0x06008126, 0x146, 0xE5EBE4C853636CD9ull, Lifted<Caught>, true, kAll},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerTactics() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
