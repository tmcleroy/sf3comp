// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer's moves (0x06066874 and what it calls).
// The computer's design, its record's fields and the other parts: computer.h.
//
// How the computer plays a plan. In mode 2 (active), once a frame 0x06006C22 runs the plan's check (0x06008BF0) and
// then the routine of the fighter's character (one of 21, from the table at 0x0613E544: Gill's 0x06064744, Alex's
// 0x06065794, Ryu's 0x06066874 ... Computer00-20 in the game's source). A character's routine is a switch on the plan
// chosen (kPattern): each plan (Pattern02_0000 ... for Ryu) is a little script, a switch on its step (the first of
// kPlanPlace), each step one call of the shared routines here (Com_Sub in the game's source): hold the lever this way,
// wait, look, walk, jump, a normal attack, a special move's command, choose the next plan at random or by the range.
// Each of those runs for as many frames as it needs, its own phase in the second of kPlanPlace, and when it is done it
// moves the plan on to its next step; a plan's last step goes back to mode 1 (Next_Be_Free), where the next plan is
// chosen.
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, Com_Sub.c, active02.c), where they are known: this is the arcade's build of the same
// code. (Names only: that project is AGPL-3.0, and none of its code is here; what the routines do was read from the
// arcade's own program and checked call by call against it.)
//
// A step's routine is called with the plan's arguments, and it does what it does over several frames: an attack, for
// example (Normal_Attack, Command_Attack), first waits until the fighter's last attack may be cancelled into it
// (Check_Start_..._Attack), then the frames of kComboSpeed (the level's reaction speed), then puts in its buttons or
// its command a lever a frame (Command_Type_00 and the like, from the character's list of commands), mashes the
// button if the move says so (Check_Rapid, Rapid_Sub), and once the attack is out, its reaction (Reaction_Sub)
// decides from whether it hit what comes next: the next step, a follow-up, or the plan's end. A jump's attack
// (Jump_Attack_Term) jumps, guards in the air if it must (Check_Air_Guard), attacks when the gap and the height meet
// the plan's ranges or against one in the air (Check_VS_Air_Attack), and waits to land (Check_Landed). Throughout, an
// answer to the other's attack (Check_Passive) or a guard (Check_Guard) can break the plan off.
//
// Registers: the routines here are reached by tail jumps from the plans (a plan's step ends in `jmp` to one of them),
// so what each leaves in r0-r7 reaches the plan's caller; where liveout.py could not see that every caller ignores
// them, the rewrites leave the registers as the game's code does, and the stack pointer what they call sees is the
// game's (Frame). They call the routines not here (and each other) through the game's calls (Guest::Call), so a call
// runs the rewrite of the routine called if there is one.
//
// The check (checklifts.sh) compares a rewrite only when it is the outermost one running: while one is compared, the
// routines it calls run as the game's code. With the entry's rewrites on (computer_entry.cpp), these are never the
// outermost; they were checked in layers, SFIII3_LIFTS_ONLY naming those of one layer.
#include "computer.h"

#include <cstdint>
#include <iterator>

namespace cps3recomp {

namespace {
    // --- the fighter's record (r4: the computer's; its kOpponent, the other's) ---
    constexpr uint32_t kSide = 0x08;            // word: 0 player 1, 1 player 2 (wu.id); the computer's tables are by it
    constexpr uint32_t kFacesRight = 0x0A;      // byte: the way it faces (rl_flag)
    constexpr uint32_t kMovesFaceRight = 0x0B;  // byte: the way its moves face (rl_waza): forward is 8 on the lever, else 4
    constexpr uint32_t kOpponent = 0x0C;        // long: the other fighter's record (target_adrs)
    constexpr uint32_t kDoing = 0x26;           // word: what the fighter is doing (routine_no[1]): 2 throwing, 4 in an
                                                // attack (the rest: computer_entry.cpp)
    constexpr uint32_t kX = 0x64, kY = 0x68;    // words: where it is, along the floor and up from it
    constexpr uint32_t kWasUp = 0x72;           // word: its height the frame before (old_pos[1])
    constexpr uint32_t kRising = 0x80;          // word: its speed upward (mvxy.a[1]): below 0 coming down
    constexpr uint32_t kHealth = 0x9E;          // word: its health (vital_new), 160 full
    constexpr uint32_t kPose = 0x20E;           // byte: its pose (pat_status)
    constexpr uint32_t kAttackReach = 0x210;    // byte: how far its attack reaches, a class (hit_range)
    constexpr uint32_t kMoveKind = 0x215;       // byte: the kind of its animation (cg_type): 0x40 the attack's end, free
                                                // to act again
    constexpr uint32_t kCancel = 0x221;         // byte: what the attack may be cancelled into now (cg_cancel): 4 a
                                                // normal, 8 a lever attack (and a normal but for reaction 14), 2 a dash,
                                                // 0x20 a special move, 0x40 a move of bit 0x8000, 1 a high jump
    constexpr uint32_t kStuck = 0x3C9;          // byte (micchaku_flag): pressed against the other
    constexpr uint32_t kPushed = 0x3CB;         // byte (hos_em_flag)
    constexpr uint32_t kHitBy = 0x312;          // byte (hf.hit.player)
    constexpr uint32_t kCharacter = 0x3C0;      // word: the character (player_number)
    constexpr uint32_t kSuperArt = 0x3F0;       // long: the super art's state (sa): word +0 the art chosen (0-2),
                                                // byte +10 -1 while it may be used (ok)
    constexpr uint32_t kHolding = 0x3CE;        // byte (tsukami_f): holding the other in a throw
    constexpr uint32_t kRunning = 0x3E4;        // byte (running_f): a dash under way
    constexpr uint32_t kAttackStep = 0x298;     // word (cg_ja.atix): not 0 once the attack's hit box is out
    constexpr uint32_t kPermitted = 0x46C;      // word (permited_koa): bit 0x10: a normal (a target combo) may follow
    constexpr uint32_t kFighterSize = 0x498;
    constexpr uint32_t kFighters = 0x02068C6C;  // the fighters' records (plw)

    // --- the computer's tables: two of each, by kSide ---
    constexpr uint32_t kMode = 0x02015572;      // words, four a side (CP_No): the mode (1 free: a plan is chosen; 2 active;
                                                // 6 answering ...)
    constexpr uint32_t kPlanPlace = 0x02015582; // words, eight a side (CP_Index): [0] the plan's step, [1] the phase of
                                                // the step's routine, [2], [3] the routine's own
    constexpr uint32_t kPattern = 0x020155A2;   // word: the plan chosen (Pattern_Index)
    constexpr uint32_t kTimer = 0x020155A6;     // word: frames to wait (Timer_00)
    constexpr uint32_t kTimer2 = 0x020155AA;    // word (Timer_01): a walk keeps the way it faced in it
    constexpr uint32_t kGap = 0x020155B2;       // word: the gap between the two along the floor (PL_Distance)
    constexpr uint32_t kRange = 0x020155B6;     // word: which range the gap is in, 0 close - 3 far (Area_Number)
    constexpr uint32_t kLever = 0x020155BA;     // word: the lever and buttons chosen this frame (Lever_Buff): lever
                                                // up 1, down 2, left 4, right 8, the buttons above
    constexpr uint32_t kJumpLever = 0x020155BE; // word: the lever of the jump to be made (Lever_Pool): up and toward
    constexpr uint32_t kFreeLever = 0x02015618; // word: the lever a walk holds (Free_Lever)
    constexpr uint32_t kAnswering = 0x0201561C; // byte (Passive_Flag)
    constexpr uint32_t kRecovering = 0x0201561E; // byte (Flip_Flag)
    constexpr uint32_t kOtherLying = 0x02015624; // byte: the other is lying on the floor (Lie_Flag)
    constexpr uint32_t kLimited = 0x0201562D;   // byte (Limited_Flag)
    constexpr uint32_t kPierce = 0x02015646;    // byte (Pierce_Menu)
    constexpr uint32_t kBeforeLook = 0x02015742; // byte (Before_Look)
    constexpr uint32_t kTurnOver = 0x02015768;  // byte (Turn_Over)
    constexpr uint32_t kRunAgain = 0x020157CC;  // byte: run the computer again this frame (Disposal_Again)
    constexpr uint32_t kLeverNow = 0x0201582C;  // word: the lever to hold while nothing else is chosen (Lever_LR)
    constexpr uint32_t kHeldDown = 0x0201563E;  // word: the lever held down, kept (Lever_Squat): bit 2 crouching
    constexpr uint32_t kTechAddress = 0x020155C4; // long: the command being put in (Tech_Address): a list of words
    constexpr uint32_t kTechIndex = 0x020155CC; // word: where in it (Tech_Index): from 12, the words before the steps'
    constexpr uint32_t kContinue = 0x02015730;  // byte (Continue_Menu)
    constexpr uint32_t kBeforeJump = 0x0201564D; // byte (Before_Jump): set as a jump's attack lands
    constexpr uint32_t kCounter = 0x02015648;   // byte (Counter_Attack)
    constexpr uint32_t kRapid = 0x02015733;     // bytes, four a side (Rapid_No): [0] how the button is mashed (0 not), [1]
                                                // its phase, [2] what ends it (1 coming down, 2 the attack over, 4 a
                                                // time), [3] that end's own count
    constexpr uint32_t kRapidButtons = 0x0201573C; // word: what is pressed at each mash (Rapid_Index)
    constexpr uint32_t kLastLevers = 0x0201576E; // words, twenty a side: the levers of the last twenty frames (Resume_Lever)
    constexpr uint32_t kOtherAttacking = 0x0201562A; // byte: the other is attacking (Attack_Flag)
    constexpr uint32_t kWidth = 0x02015632;     // word: the computer's character's width (Com_Width_Data)
    constexpr uint32_t kAttacksSeen = 0x02015750; // word: the other's attacks counted (Attack_Counter)
    constexpr uint32_t kGuardedAttack = 0x02015848; // word: kAttacksSeen when the guard was last chosen (Guard_Counter)
    constexpr uint32_t kGuardType = 0x020155FE; // word: the guard chosen (Guard_Type), with 0x8000
    constexpr uint32_t kReturnMode = 0x02015812; // word: the mode an answer broke off (Return_CP_No)
    constexpr uint32_t kReturnPlace = 0x02015816; // word: its step to go on with (Return_CP_Index)
    constexpr uint32_t kReturnPattern = 0x0201581A; // word: its plan (Return_Pattern_Index)
    constexpr uint32_t kLastEffect = 0x02015830; // word: the last target combo's (Last_Eftype)
    constexpr uint32_t kComboSpeed = 0x020155AE; // byte: frames before the attack's button (Combo_Speed: Check_First_Menu)
    constexpr uint32_t kHitStock = 0x02015661;  // byte: the record's kHitBy, kept as the attack is put in (Stock_Hit_Flag)

    // --- the game's, for everyone ---
    constexpr uint32_t kChosen = 0x02011387;    // bytes, a side: the character played (My_char)
    constexpr uint32_t kPlayerId = 0x02015500;  // byte (Player_id)
    constexpr uint32_t kAirReach = 0x0613E75C;  // words, a character each: added to a range against one in the air
                                                // (Correct_VS_Air_Data)
    constexpr int kNecro = 5;

    // --- tables in the program ---
    constexpr uint32_t kCommands = 0x020062E4;   // longs in RAM, a character each: its commands (player_cmd), a long each
    constexpr uint32_t kPowerLevers = 0x06178ECE; // words: the lever held through a move of power 8-11 (Power_Lv_Data)
    constexpr uint32_t kDashTimes = 0x06178D7C;  // bytes: [character][2]: frames of a dash (Dash_Time_Data)
    constexpr uint32_t kRandomChoices = 0x06178E0E; // bytes: [kind][16 at random]: which of four plans (Com_Rnd_Select_Data)

    // --- the computer's level (computer_entry.cpp has the rest) ---
    constexpr uint32_t kRank = 0x0201584C;       // byte: which set of level tables
    constexpr uint32_t kLevelBonus = 0x0201584D; // byte: added to some levels (CC_Value)
    constexpr uint32_t kDemo = 0x020154AF;       // byte: not 0 for attract mode's fights (Demo_Flag)
    constexpr uint32_t kWeakSide = 0x02016AD6;   // byte: the side that plays at level 2, outside a demo (Weak_PL)
    constexpr uint32_t kBrokenInto = 0x02016B00; // byte: 1 or 2: a challenger broke in (Break_Into_CPU); 2: at its best
    constexpr uint32_t kLevelNow = 0x0200DCF4;   // byte: the level just worked out (Lv)
    constexpr uint32_t kComboSpeeds = 0x0616EAD4; // bytes: [character, 18 a broken-in fight's: 640 each][level of 20]
                                                 // [32 at random]: frames before an attack's button (Combo_Speed_Unit_Data)
    constexpr uint32_t kLookTimes = 0x06175850;  // bytes: [level of 8][32 at random] (LOOK_POSITION_Data)
    constexpr uint32_t kWaitTimes = 0x06175830;  // bytes: [level of 4][8 at random] (Wait_Time_Data)
    constexpr uint32_t kLevelOf4 = 0x0600F9D2;   // Setup_Lv04 (r4 the row)
    constexpr uint32_t kLevelOf8 = 0x0600FA14;   // Setup_Lv08 (r4 the row; computer_entry.cpp)
    constexpr uint32_t kLevelOf18 = 0x0600FAC4;  // Setup_Lv18 (r4 the row)
    constexpr uint32_t kDice128 = 0x0611E0D6;    // random_32_com: the next of a table of 128
    constexpr uint32_t kDice16 = 0x0611E11E;     // random_16_ex_com: the next of a table of 16
    constexpr uint32_t kLevelOf10 = 0x0600FA84;  // Setup_Lv10 (r4 the row)
    constexpr uint32_t kDiceNow = 0x0200DCF5;    // byte: the random number just taken (Rnd)
    constexpr uint32_t kBossRank = 0x02016B33;   // byte: not 0: the boss's tables (row 18) (EM_Rank)
    constexpr uint32_t kGuardChoices = 0x06175EBA; // bytes: [character, 18 the boss: 0xB0][level: 16][16 at random]:
                                                 // the guard to do, 3 none (Guard_Data)
    constexpr uint32_t kReachByClass = 0x061773DE; // words, by kAttackReach: an attack's reach (Hit_Range_Data)
    constexpr uint32_t kMashes = 0x06176E9C;     // bytes: [level of 8][8 at random]: how to mash (Rapid_SA_Data)
    constexpr uint32_t kHadouMoves = 0x0613E78C; // two longs a character: whether, and which move, needs the lever
                                                 // back first (Hadou_Check_Data)
    constexpr uint32_t kAttacking = 0x428;       // in a fighter's record, a byte: attacking (caution_flag)

    // --- the game's routines called ---
    constexpr uint32_t kDice64 = 0x0611E0EE;     // random_16_com: the next of a table of 64 (lifts.cpp's NextRandom)
    constexpr uint32_t kClearDash = 0x060B5C9E;  // dash_flag_clear (r4 the side)
    constexpr uint32_t kCheckAnswer = 0x06010DEA; // Check_Passive (r4): an answer to the other has begun (not 0)
    constexpr uint32_t kLookTime = 0x0600FB74;   // Setup_LP_Data (r4): how long to look, by level
    constexpr uint32_t kWaitTime = 0x0600FBCA;   // Setup_WT_Data (r4): how long to wait, by level
    constexpr uint32_t kCheckGuard = 0x06010F4C; // Check_Guard (r4): a guard has begun (not 0; computer_entry.cpp)
    constexpr uint32_t kBeFree = 0x06008706;     // Next_Be_Free
    constexpr uint32_t kNextEnd = 0x0600FD7E;    // Next_End
    constexpr uint32_t kGuardLever = 0x06009FF4; // Setup_Guard_Lever
    constexpr uint32_t kAnotherPlan = 0x0600FE10; // Next_Another_Menu
    constexpr uint32_t kAttackBusy = 0x06009D4C; // Check_Start_Normal_Attack
    constexpr uint32_t kFirstMenu = 0x0601047C;  // Check_First_Menu (r4): kComboSpeed set
    constexpr uint32_t kBeCaught = 0x0600846A;   // Be_Catch (r4): the computer caught in a throw
    constexpr uint32_t kLeverAttackBusy = 0x0600A046; // Check_Start_Lever_Attack
    constexpr uint32_t kCanCommand = 0x0600E108; // Check_Start_Command_Attack (r4, r5 the reaction, r6 the move): not 0
                                                 // while it may not be started
    constexpr uint32_t kDashHit = 0x0600F09E;    // Check_Dash_Hit (r4, r5 the move): a dash would hit the other
    constexpr uint32_t kShellAbove = 0x06011414; // Check_Diagonal_Shell (r4)
    constexpr uint32_t kHadou = 0x0600D7DE;      // Hadou_Check (r4, r5 the move): the lever is not yet back for it
    constexpr uint32_t kCheckRapid = 0x0600DDB8; // Check_Rapid (r4, r5 the move)
    constexpr uint32_t kMashSub = 0x0600DEA4;    // Rapid_Sub (r4)
    constexpr uint32_t kCommand0 = 0x0600F254;   // Command_Type_00 (r4, r5 the power, r6 the move, r7 Ex_Shot): the
                                                 // command's next lever; -1 when it is all put in
    constexpr uint32_t kCommand1 = 0x0600F574;   // Command_Type_01 (r4, r5 the power, r6 Ex_Shot)
    constexpr uint32_t kCommand6 = 0x0600F4C0;   // Command_Type_06 (r4, r5 the power, r6 the move, r7 Ex_Shot)
    constexpr uint32_t kReactionExit = 0x06010350; // Reaction_Exit_Sub (r4): the plan on to its next step
    constexpr uint32_t kLanded = 0x0600EFB8;     // Check_Landed (r4, r5 the reaction)
    constexpr uint32_t kSetLeverNow = 0x06011B76; // Setup_Lever_LR (r4, r5 the side, r6 the reaction's bits 12-15)
    constexpr uint32_t kClearHighJump = 0x060B5CD6; // hi_jump_flag_clear (r4 the side)
    constexpr uint32_t kStartJump = 0x0600F1DC;  // Jump_Init (r4, r5 the way)
    constexpr uint32_t kAirGuard = 0x0600B974;   // Check_Air_Guard (r4)
    constexpr uint32_t kVsAirAttack = 0x0600C196; // Check_VS_Air_Attack (r4, r5 Range_JX, r6 Range_JY, r7 the lever)
    constexpr uint32_t kAttackTime = 0x0600CABC; // Check_Term_ABS_Distance (r4)
    constexpr uint32_t kMeetsLimit = 0x0600B4AC; // Check_Term_Sub (r4, r5 a value, r6 its limit)
    constexpr uint32_t kHeightMeetsLimit = 0x0600CB34; // Check_Com_Add_Y (r4, r5 the height, r6 its limit)
    constexpr uint32_t kJumpAttackLever = 0x0600C164; // Check_SP_Jump_Attack (r4, r5 the lever)
    constexpr uint32_t kMeetsLimitAir = 0x0600B466; // Check_Term_Sub_Air (r4, r5 a gap, r6 its limit)
    constexpr uint32_t kHeightGap = 0x0600FC54;  // Ck_Distance_Height (r4): how far apart the two are upward
    constexpr uint32_t kReflexTime = 0x0600F93C; // Select_Reflection_Time (r4)
    constexpr uint32_t kSetupFollow = 0x06010898; // Setup_Follow (r4, r5 the follow-up)
    constexpr uint32_t kPackCommand = 0x061210EC; // datacmd_conpanecmd (r4 a command's lever): as the controls have it
    constexpr uint32_t kButtonForPower = 0x061210DC; // renbanshot_conpaneshot (r4 the command, r5 the power): its button
    constexpr uint32_t kFindProjectile = 0x060DBB8A; // get_vs_shell_adrs (r4 a fighter, r5 the side, r6 which, r7
                                                  // where to put it): not 0 when there is one
    constexpr uint32_t kFrontOrBack = 0x0600F15A; // Setup_Front_or_Back (r4, r5 the gap to it)
    constexpr uint32_t kHitsShell = 0x0600F184;  // Check_Hit_Shell (r4, r5 the projectile, r6 the dash)
    constexpr uint32_t kBehind = 0x06011A60;     // Check_Behind (r4, r5 the projectile)
    constexpr uint32_t kShellComing = 0x06011500; // Check_Ignore_Shell2 (r4 the projectile)
    constexpr uint32_t kShellAboveChances = 0x06177244; // bytes, by level of 8 (VS_Diagonal_Shell_Data)
    constexpr uint32_t kShellKind = 0x05;       // in a projectile's record, a byte (charset_id)
    constexpr uint32_t kTargetCombo = 0x060101D2; // Check_Meoshi_Attack (r4, r5 the reaction, r6 the power)
    constexpr uint32_t kReaction = 0x0600FE80;   // Reaction_Sub (r4, r5 the reaction, r6 the power): after the attack

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
        int8_t FieldByte(uint32_t field) { return static_cast<int8_t>(g.Read8(me + field)); }

        uint32_t At(uint32_t table, int32_t bytesPerSide) { return table + static_cast<uint32_t>(side * bytesPerSide); }
        uint8_t Byte(uint32_t table) { return g.Read8(At(table, 1)); }
        void SetByte(uint32_t table, int32_t value) { g.Write8(At(table, 1), static_cast<uint8_t>(value)); }
        int16_t Word(uint32_t table) { return static_cast<int16_t>(g.Read16(At(table, 2))); }
        void SetWord(uint32_t table, int32_t value) { g.Write16(At(table, 2), static_cast<uint16_t>(value)); }
        uint32_t PlaceAt(int n) { return At(kPlanPlace, 16) + static_cast<uint32_t>(n * 2); }
        int16_t Place(int n) { return static_cast<int16_t>(g.Read16(PlaceAt(n))); }
        void SetPlace(int n, int32_t value) { g.Write16(PlaceAt(n), static_cast<uint16_t>(value)); }
        uint32_t ModeAt(int n) { return At(kMode, 8) + static_cast<uint32_t>(n * 2); }
        int16_t Mode() { return static_cast<int16_t>(g.Read16(ModeAt(0))); }
    };

    // The routine's own frame on the stack (what it pushes), so that what it calls finds the stack pointer where the
    // game's code has it (a callee that leaves an address on the stack in a register leaves the same one)
    struct Frame {
        Frame(Guest &guest, uint32_t bytes)
            : g(guest), size(bytes), entry(guest.R(15)) {
            g.R(15) -= size;
        }
        ~Frame() { g.R(15) += size; }
        Guest &g;
        const uint32_t size;
        const uint32_t entry; // the stack pointer the routine was called with (its arguments beyond r7 there)
    };

    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }
    uint32_t SignedWord(Guest &g, uint32_t at) {
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(g.Read16(at))));
    }
    // A result tested as the game's code does (tst r0,r0): T set when it is 0
    bool Nonzero(Guest &g) {
        g.SetT(g.R(0) == 0);
        return g.R(0) != 0;
    }
    void CallWith(Guest &g, uint32_t routine, uint32_t r4, uint32_t r5 = 0, uint32_t r6 = 0) {
        g.R(4) = r4;
        g.R(5) = r5;
        g.R(6) = r6;
        g.Call(routine);
    }


    // --- dice and small tables ---

    // 0611E106 (random_32_ex_com): the next of a table of 32, from a counter that goes round it. The counter's place
    // and the table's are the routine's literals. Leaves r0 the number, r1 the table, r3 the counter plus one before it
    // was wrapped, r4 the counter's place, T clear.
    bool Dice32(Guest &g) {
        const uint32_t counter = g.Read32(0x0611E1BC), table = g.Read32(0x0611E1C0);
        const uint32_t next = SignedWord(g, counter) + 1;
        g.Write16(counter, static_cast<uint16_t>(next));
        g.Write16(counter, static_cast<uint16_t>(g.Read16(counter) & 31));
        g.R(0) = SignedWord(g, table + (SignedWord(g, counter) << 1));
        g.R(1) = table;
        g.R(3) = next;
        g.R(4) = counter;
        g.SetT(false);
        g.Return(12);
        return true;
    }

    // 061210DC: a word of the table at 0x065EB898 (the routine's literal), by the low four bits of the r5th word of r4's
    // list. Leaves r1 the table, T clear.
    bool TableByLowBits(Guest &g) {
        const uint32_t table = g.Read32(0x0612111C);
        const uint32_t item = g.Read16(g.R(4) + (Signed(static_cast<int16_t>(g.R(5))) << 1)) & 15;
        g.R(0) = SignedWord(g, table + item * 2);
        g.R(1) = table;
        g.SetT(false);
        g.Return(8);
        return true;
    }

    // 061210EC: a command word packed: its bits 8-10 moved down to 7-9, beside its low seven bits. Leaves r1 the low
    // seven bits, r2 0x700, r3 the moved bits, r4 the result, T clear.
    bool PackCommand(Guest &g) {
        const uint32_t word = g.R(4);
        const uint32_t high = Signed(static_cast<int16_t>(word) & 0x700) >> 1;
        g.R(1) = word & 127;
        g.R(2) = 0x700;
        g.R(3) = high;
        g.R(4) = high | (word & 127);
        g.R(0) = Signed(static_cast<int16_t>(g.R(4)));
        g.SetT(false);
        g.Return(10);
        return true;
    }


    // --- conditions on the fight (ETC_Term_00xx: r4 the computer, r5 the other) ---

    // The computer's own health, as the game's list of the fighters' records has it
    uint32_t OwnHealth(Guest &g, uint32_t me) {
        const uint32_t offset = Signed(static_cast<int16_t>(static_cast<int16_t>(g.Read16(me + kSide)) * kFighterSize));
        return SignedWord(g, kFighters + kHealth + offset);
    }

    // 06012338 (ETC_Term_0004): well ahead: the other under 80 health, the computer at 120 or more. Leaves r1-r3 and
    // T as the routine does.
    bool WellAhead(Guest &g) {
        const uint32_t theirs = SignedWord(g, g.R(5) + kHealth);
        g.R(2) = 80;
        g.R(3) = theirs;
        if (static_cast<int32_t>(theirs) >= 80) {
            g.R(0) = 0;
            g.SetT(true);
        } else {
            const uint32_t mine = OwnHealth(g, g.R(4));
            g.R(1) = kFighters + kHealth;
            g.R(2) = mine;
            g.R(3) = kFighterSize;
            g.SetT(static_cast<int32_t>(mine) >= 120);
            g.R(0) = g.T() ? 1 : 0;
        }
        g.Return(14);
        return true;
    }

    // 0601236A (ETC_Term_0005): ahead: the other has less health than the computer. Leaves r1-r3 and T as the routine
    // does.
    bool Ahead(Guest &g) {
        const uint32_t theirs = SignedWord(g, g.R(5) + kHealth);
        const uint32_t mine = OwnHealth(g, g.R(4));
        g.R(1) = mine;
        g.R(2) = kFighterSize;
        g.R(3) = theirs;
        g.SetT(static_cast<int32_t>(theirs) >= static_cast<int32_t>(mine));
        g.R(0) = g.T() ? 0 : 1;
        g.Return(12);
        return true;
    }


    // --- the lever ---

    // 0600FD36 (Ck_Distance_LvJ): the gap to the other (kGap: the other's x less the computer's, made positive) and the
    // jump toward it (kJumpLever: up and right, 9, when the other is to the right; up and left, 5, when not). Leaves
    // r0-r6 and T as the routine does.
    void JumpGap(Guest &g, uint32_t me) {
        Side s(g, me);
        const uint32_t mine = SignedWord(g, me + kX), theirs = SignedWord(g, s.Other() + kX);
        s.SetWord(kGap, static_cast<int32_t>(theirs - mine));
        const int32_t gap = s.Word(kGap);
        g.R(1) = theirs - mine;
        g.R(2) = mine;
        g.R(5) = kGap;
        g.R(6) = kJumpLever;
        if (gap > 0) {
            s.SetWord(kJumpLever, 9);
            g.R(0) = Signed(s.side * 2);
            g.R(3) = 9;
        } else {
            s.SetWord(kJumpLever, 5);
            s.SetWord(kGap, -gap);
            g.R(0) = Signed(s.side);
            g.R(3) = Signed(-gap);
            g.R(6) = s.At(kGap, 2);
        }
        g.SetT(false);
    }
    bool JumpGapLift(Guest &g) {
        JumpGap(g, g.R(4));
        g.Return(20);
        return true;
    }

    // 0600F1DC (Jump_Init): the lever for a jump: r5 0 toward the other, 2 straight up, any other away from it
    bool StartJump(Guest &g) {
        Side s(g, g.R(4));
        const int16_t way = static_cast<int16_t>(g.R(5));
        if (way == 2) {
            s.SetWord(kJumpLever, 1);
            s.SetWord(kLever, 1);
        } else {
            JumpGap(g, s.me);
            if (way != 0) {
                s.SetWord(kJumpLever, s.Word(kJumpLever) ^ 12); // (left for right)
            }
            s.SetWord(kLever, s.Word(kJumpLever));
        }
        g.Return(30);
        return true;
    }

    // 06009FF4 (Setup_Guard_Lever): the lever's bit for a way, as the fighter's moves face: r5 0 forward, 1 back (4
    // left, 8 right); 0 for any other. Leaves T as the routine's last test.
    uint32_t LeverFor(Guest &g, uint32_t me, uint32_t way) {
        const int8_t facing = static_cast<int8_t>(g.Read8(me + kMovesFaceRight));
        switch (way & 0xFFFF) {
        case 0: g.SetT(facing == 0); return facing == 0 ? 4 : 8;
        case 1: g.SetT(facing == 1); return facing == 1 ? 4 : 8;
        default: g.SetT(false); return 0;
        }
    }
    bool LeverForLift(Guest &g) {
        g.R(0) = LeverFor(g, g.R(4), g.R(5));
        g.Return(8);
        return true;
    }

    // 0600C164 (Check_SP_Jump_Attack, r5 a jump attack's lever and buttons): with bit 0x8000, forward and down are
    // added to them (and the bit goes)
    bool JumpAttackLever(Guest &g) {
        const uint32_t me = g.R(4);
        const uint32_t guard = Signed(static_cast<int16_t>(g.R(5)));
        if ((guard & 0x8000) == 0) {
            g.R(3) = 0x8000;
            g.R(5) = guard;
            g.R(0) = guard;
            g.SetT(true);
        } else {
            g.R(5) = 0;
            const uint32_t forward = LeverFor(g, me, 0);
            g.R(3) = 0x7FFF;
            g.R(2) = guard & 0x7FFF;
            g.R(4) = forward | g.R(2);
            g.R(0) = (g.R(4) & 0xFFFF) | 2;
        }
        g.Return(16);
        return true;
    }


    // --- tests ---

    // 0600B4AC (Check_Term_Sub, r5 a value, r6 a limit): does the value meet the limit? -1 always; with bit 0x8000
    // set, the value at most the rest; else at least the limit.
    bool MeetsLimit(int32_t value, int32_t limit) {
        if (limit == -1) {
            return true;
        }
        if (limit & 0x8000) {
            return value <= (limit & 0x7FFF);
        }
        return value >= limit;
    }
    bool MeetsLimitLift(Guest &g) {
        const int32_t limit = static_cast<int16_t>(g.R(6)), value = static_cast<int16_t>(g.R(5));
        g.R(0) = MeetsLimit(value, limit) ? 1 : 0;
        g.Return(8);
        return true;
    }

    // 0600B466 (Check_Term_Sub_Air, r5 a gap, r6 a limit): as MeetsLimit, an "at most" widened by the character's own
    // reach in the air (Correct_VS_Air_Data, by the character of the side Player_id names)
    bool AirGapMeetsLimit(Guest &g) {
        const int32_t value = static_cast<int16_t>(g.R(5));
        int32_t limit = static_cast<int16_t>(g.R(6));
        bool yes;
        if (limit == -1 || !(limit & 0x8000)) {
            yes = MeetsLimit(value, limit);
        } else {
            const uint8_t character = g.Read8(kChosen + g.Read8(kPlayerId));
            limit = static_cast<int16_t>(limit + static_cast<int16_t>(g.Read16(kAirReach + character * 2u))) & 0x7FFF;
            yes = value <= limit;
        }
        g.R(0) = yes ? 1 : 0;
        g.Return(12);
        return true;
    }

    // 0600B4F0 (Check_Term_Sub_Y, r5 a height, r6 a limit): as MeetsLimit, but an "at most" holds only while the other
    // is not rising
    bool OtherHeightMeetsLimit(Guest &g) {
        const int32_t limit = static_cast<int16_t>(g.R(6)), value = static_cast<int16_t>(g.R(5));
        bool yes = MeetsLimit(value, limit);
        if (limit != -1 && (limit & 0x8000) && static_cast<int16_t>(g.Read16(g.Read32(g.R(4) + kOpponent) + kRising)) > 0) {
            yes = false;
        }
        g.R(0) = yes ? 1 : 0;
        g.Return(10);
        return true;
    }

    // 0600CB34 (Check_Com_Add_Y, r5 a height, r6 a limit): as MeetsLimit, but an "at most" holds only while the
    // fighter is coming down
    bool HeightMeetsLimit(Guest &g) {
        const int32_t limit = static_cast<int16_t>(g.R(6)), value = static_cast<int16_t>(g.R(5));
        bool yes = MeetsLimit(value, limit);
        if (limit != -1 && (limit & 0x8000) && static_cast<int16_t>(g.Read16(g.R(4) + kRising)) >= 0) {
            yes = false;
        }
        g.R(0) = yes ? 1 : 0;
        g.Return(8);
        return true;
    }

    // 0600CABC (Check_Term_ABS_Distance): is it time for the jump's attack? Always when the computer has turned over
    // (kTurnOver); never for Necro; then when the other is within 48, or the fighter is coming down at 48 high or less.
    bool JumpAttackTime(Guest &g) {
        Side s(g, g.R(4));
        bool yes;
        if (s.Byte(kTurnOver) != 0) {
            yes = true;
        } else if (s.Byte(kChosen) == kNecro) {
            yes = false;
        } else if (s.Word(kGap) <= 48) {
            yes = true;
        } else if (s.Field(kRising) >= 0) {
            yes = false;
        } else {
            yes = s.Field(kY) <= 48;
        }
        g.R(0) = yes ? 1 : 0;
        g.Return(14);
        return true;
    }

    // 0600AEA2 (Check_Wait_Term, r5 an option): has the other got up? Not while it is hit and in pose 0x18; yes when it
    // is not lying down; lying down, only with the option 0 and its animation of kind 0x0B (getting up)
    bool OtherUp(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t other = s.Other();
        bool up;
        if (static_cast<int16_t>(g.Read16(other + kDoing)) == 1 && g.Read8(other + kPose) == 0x18) {
            up = false;
        } else if (s.Byte(kOtherLying) == 0) {
            up = true;
        } else if (static_cast<int16_t>(g.R(5)) != 0) {
            up = false;
        } else {
            up = g.Read8(other + kMoveKind) == 0x0B;
        }
        g.R(0) = up ? 1 : 0;
        g.Return(14);
        return true;
    }

    // Still in an attack that may not be cancelled into a normal (kDoing 4, not at its end, kCancel without 4)
    bool InAttack(Side &s) {
        return s.Field(kDoing) == 4 && s.g.Read8(s.me + kMoveKind) != 0x40 && (s.g.Read8(s.me + kCancel) & 4) == 0;
    }

    // 0600A046 (Check_Start_Lever_Attack): 1 while a lever attack may not start: in an attack that may not be cancelled
    // into one
    bool LeverAttackBusy(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = InAttack(s) && (g.Read8(s.me + kCancel) & 8) == 0 ? 1 : 0;
        g.Return(12);
        return true;
    }

    // 06009D4C (Check_Start_Normal_Attack, r5 the reaction): 1 while a normal attack may not start: in an attack that
    // may not be cancelled into one (kPermitted's 0x10 lets it; kCancel's 8 does too, but for reaction 14)
    bool AttackBusy(Guest &g) {
        Side s(g, g.R(4));
        bool busy = InAttack(s) && (g.Read16(s.me + kPermitted) & 0x10) == 0;
        if (busy && (g.Read8(s.me + kCancel) & 8) != 0 && static_cast<int16_t>(g.R(5)) == 14) {
            busy = false;
        }
        g.R(0) = busy ? 1 : 0;
        g.Return(14);
        return true;
    }


    // --- the plan's steps ---

    // 06008706 (Next_Be_Free): back to mode 1 (a new plan is chosen), the lever back to kLeverNow. Leaves r0 kLever,
    // r3 the lever, r5 the side's offset, r6 0, T clear.
    void BeFree(Guest &g, uint32_t me) {
        Side s(g, me);
        g.Write16(s.ModeAt(0), 1);
        for (int n = 1; n < 4; n++) {
            g.Write16(s.ModeAt(n), 0);
        }
        g.R(3) = Signed(s.Word(kLeverNow));
        s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
        g.R(0) = kLever;
        g.R(5) = Signed(s.side * 2);
        g.R(6) = 0;
        g.SetT(false);
    }
    bool BeFreeLift(Guest &g) {
        BeFree(g, g.R(4));
        g.Return(20);
        return true;
    }

    // 06008C24 (End_Pattern): the plan over: Next_Be_Free
    bool EndPattern(Guest &g) {
        BeFree(g, g.R(4));
        g.Return(22);
        return true;
    }

    // 0600FE10 (Next_Another_Menu): another plan (r6) in a mode (r5), from its first step; mode 1 is Next_Be_Free.
    // Leaves r0 0, r3 the third word's place, r5 0, r6 the side's plan place, T clear (or as BeFree, with r2 its address).
    void AnotherPlan(Guest &g, uint32_t me, uint32_t mode, uint32_t plan) {
        if (static_cast<int16_t>(mode) == 1) {
            g.R(2) = kBeFree;
            BeFree(g, me);
            return;
        }
        Side s(g, me);
        g.Write16(s.ModeAt(0), static_cast<uint16_t>(mode));
        s.SetWord(kPattern, static_cast<int32_t>(plan));
        for (int n = 0; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        g.R(0) = 0;
        g.R(3) = s.PlaceAt(2);
        g.R(5) = 0;
        g.R(6) = s.PlaceAt(0);
        g.SetT(false);
    }
    bool AnotherPlanLift(Guest &g) {
        AnotherPlan(g, g.R(4), g.R(5), g.R(6));
        g.Return(24);
        return true;
    }

    // The plan on to its next step (CP_Index[0] + 1). Leaves r0 the side's offset (16 a side), r1 kPlanPlace, r2 the
    // side's place, r3 the step.
    void NextStep(Side &s) {
        s.SetPlace(0, s.Place(0) + 1);
        s.g.R(0) = Signed(s.side * 16);
        s.g.R(1) = kPlanPlace;
        s.g.R(2) = s.PlaceAt(0);
        s.g.R(3) = Signed(s.Place(0));
    }

    // 06008C30 (Turn_Over_On): turned over (kTurnOver), and on to the next step at once (kRunAgain)
    bool TurnOverOn(Guest &g) {
        Side s(g, g.R(4));
        s.SetByte(kRunAgain, 1);
        s.SetByte(kTurnOver, 1);
        NextStep(s);
        g.R(5) = 1;
        g.Return(19);
        return true;
    }

    // 06008C56 (Only_Shot, r5 the buttons): the buttons pressed (the lever let go), and on to the next step
    bool OnlyShot(Guest &g) {
        Side s(g, g.R(4));
        s.SetWord(kLever, static_cast<int32_t>(g.R(5)));
        NextStep(s);
        g.SetT(false);
        g.Return(15);
        return true;
    }

    // 06008C74 (Lever_On, r5 0 forward, 1 back, other none; r6 up and down): the lever to hold from now on
    // (kLeverNow), held now; on to the next step at once
    bool LeverOn(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t way = g.R(5) & 0xFFFF;
        const uint32_t upDown = Signed(static_cast<int16_t>(g.R(6)));
        NextStep(s);
        s.SetByte(kRunAgain, 1);
        if (way == 0 || way == 1) {
            g.R(5) = way;
            s.SetWord(kLeverNow, static_cast<int32_t>(LeverFor(g, s.me, way)));
        } else {
            s.SetWord(kLeverNow, 0);
        }
        g.R(2) = SignedWord(g, s.At(kLeverNow, 2)) | upDown;
        s.SetWord(kLeverNow, static_cast<int32_t>(g.R(2)));
        g.R(3) = SignedWord(g, s.At(kLeverNow, 2));
        s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
        g.R(0) = kLever;
        g.R(1) = kPlanPlace;
        g.R(4) = Signed(s.side * 2);
        g.SetT(false);
        g.Return(66);
        return true;
    }

    // 06008CF8 (Lever_Off): the lever let go from now on; on to the next step at once
    bool LeverOff(Guest &g) {
        Side s(g, g.R(4));
        NextStep(s);
        s.SetByte(kRunAgain, 1);
        s.SetWord(kLeverNow, 0);
        g.R(0) = Signed(s.side * 2);
        g.R(2) = s.At(kLeverNow, 2);
        g.R(3) = 0;
        g.SetT(false);
        g.Return(21);
        return true;
    }

    // 06008D22 (Pierce_On): the plan is to go through (kPierce); on to the next step at once
    bool PierceOn(Guest &g) {
        Side s(g, g.R(4));
        s.SetByte(kRunAgain, 1);
        NextStep(s);
        s.SetByte(kPierce, 1);
        g.R(3) = Signed(s.Word(kLeverNow));
        s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
        g.R(0) = kLever;
        g.R(5) = Signed(s.side * 2);
        g.SetT(false);
        g.Return(26);
        return true;
    }

    // A step's routine done: the plan on to its next step, the routine's phase and words cleared, and the recovery and
    // limit flags (and, but in mode 6, the answer's) cleared. Leaves r0 the mode, r1 kMode, r3 the last flag's place,
    // r4 the side's plan place, r5 0.
    void StepDone(Side &s, bool clearAnswering) {
        Guest &g = s.g;
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kRecovering, 0);
        s.SetByte(kLimited, 0);
        g.R(3) = s.At(kLimited, 1);
        g.R(4) = s.PlaceAt(0);
        g.R(5) = 0;
        if (clearAnswering) {
            g.R(1) = kMode;
            g.R(0) = Signed(s.Mode());
            if (s.Mode() != 6) {
                s.SetByte(kAnswering, 0);
                g.R(3) = s.At(kAnswering, 1);
            }
        }
    }

    // The lever chosen this frame: kLeverNow. Leaves r0 kLever, r3 the lever, r4 the side's offset.
    void HoldLeverNow(Side &s) {
        s.g.R(3) = Signed(s.Word(kLeverNow));
        s.SetWord(kLever, static_cast<int32_t>(s.g.R(3)));
        s.g.R(0) = kLever;
        s.g.R(4) = Signed(s.side * 2);
    }

    // 0600A88A (Wait, r5 frames: 0 as the level has it): wait, holding kLeverNow
    bool Wait(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const int16_t frames = static_cast<int16_t>(g.R(5));
        g.R(6) = kPlanPlace;
        if (s.Place(1) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            g.R(6) = s.PlaceAt(0);
            if (frames == 0) {
                CallWith(g, kWaitTime, s.me, g.R(5), g.R(6));
                s.SetWord(kTimer, static_cast<int32_t>(g.R(0)));
            } else {
                s.SetWord(kTimer, frames);
            }
        } else {
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            if (s.Word(kTimer) == 0) {
                StepDone(s, true);
                g.R(6) = kPlanPlace;
            }
        }
        HoldLeverNow(s);
        g.SetT(false);
        g.Return(60);
        return true;
    }

    // 0600A960 (Look, r5 frames: 0 as the level has it): stand and look, holding kLeverNow, unless an answer to the
    // other begins; crouching (kLeverNow down) it looks 50 frames longer. Done, kBeforeLook is set.
    bool Look(Guest &g) {
        Frame frame(g, 28);
        Side s(g, g.R(4));
        const int16_t frames = static_cast<int16_t>(g.R(5));
        s.SetByte(kAnswering, 0);
        HoldLeverNow(s);
        const bool first = s.Place(1) == 0;
        CallWith(g, kCheckAnswer, s.me, g.R(5), g.R(6));
        if (Nonzero(g)) {
            g.Return(60);
            return true;
        }
        if (first) {
            s.SetPlace(1, s.Place(1) + 1);
            if (frames == 0) {
                CallWith(g, kLookTime, s.me, g.R(5), g.R(6));
                s.SetWord(kTimer, static_cast<int32_t>(g.R(0)));
            } else {
                g.R(3) = Signed(frames);
                s.SetWord(kTimer, frames);
            }
            g.R(1) = kLeverNow;
            g.R(0) = g.Read16(s.At(kLeverNow, 2));
            g.SetT((g.R(0) & 2) == 0);
            if (!g.T()) {
                g.R(3) = Signed(s.Word(kTimer) + 50);
                s.SetWord(kTimer, static_cast<int32_t>(g.R(3)));
                g.R(0) = Signed(s.side * 2);
                g.SetT(false);
            }
        } else {
            g.R(0) = Signed(s.side * 2);
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            g.R(3) = Signed(s.Word(kTimer));
            g.SetT(g.R(3) == 0);
            if (g.R(3) == 0) {
                s.SetPlace(0, s.Place(0) + 1);
                for (int n = 1; n < 4; n++) {
                    s.SetPlace(n, 0);
                }
                s.SetByte(kRecovering, 0);
                s.SetByte(kLimited, 0);
                s.SetByte(kBeforeLook, 1);
                g.R(0) = Signed(s.side);
                g.R(2) = s.At(kBeforeLook, 1);
                g.R(3) = 1;
            }
        }
        g.Return(60);
        return true;
    }

    // 060095E0 (Walk, r5 0 forward or 1 back, r6 frames): walk that way for so many frames, unless an answer to the
    // other begins (the other on its feet); turned round, pressed against the other or pushed, the plan is given up
    bool Walk(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        const uint32_t way = g.R(5) & 0xFFFF;
        const int16_t frames = static_cast<int16_t>(g.R(6));
        const int16_t phase = s.Place(1);
        if (phase == 0) {
            s.SetPlace(1, phase + 1);
            g.R(4) = Signed(s.side);
            g.Call(kClearDash);
            s.SetWord(kTimer, frames);
            s.SetWord(kTimer2, s.FieldByte(kFacesRight));
            g.R(4) = s.me;
            g.R(5) = way;
            g.R(0) = LeverFor(g, s.me, way);
            s.SetWord(kFreeLever, static_cast<int32_t>(g.R(0)));
        } else if (phase != 1) {
            g.R(0) = Signed(phase);
            g.R(4) = kPlanPlace;
            g.SetT(false);
            g.Return(30);
            return true;
        }
        g.R(0) = Signed(s.side);
        g.R(1) = kOtherLying;
        g.R(3) = Signed(static_cast<int8_t>(s.Byte(kOtherLying)));
        if (g.R(3) == 0) {
            CallWith(g, kCheckAnswer, s.me, g.R(5), g.R(6));
            if (Nonzero(g)) {
                g.Return(60);
                return true;
            }
        }
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        g.R(3) = Signed(s.Word(kTimer));
        g.R(1) = kTimer;
        if (g.R(3) == 0) {
            const uint32_t r5 = g.R(5);
            StepDone(s, true);
            const bool answering = s.Mode() == 6;
            g.R(0) = answering ? 6 : Signed(s.side);
            g.R(1) = kTimer;
            g.R(2) = kMode;
            g.R(4) = 0; // (the clearing's 0)
            g.R(5) = r5;
            g.SetT(answering);
        } else {
            g.R(2) = kTimer2;
            if (s.Word(kTimer2) != s.FieldByte(kFacesRight) || s.FieldByte(kStuck) != 0 || s.FieldByte(kPushed) != 0) {
                BeFree(g, s.me);
            }
            g.R(3) = Signed(s.Word(kFreeLever));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.R(0) = kLever;
            g.R(4) = Signed(s.side * 2);
            g.SetT(false);
        }
        g.Return(60);
        return true;
    }

    // 0600A708 (Com_Random_Select, r5 the mode, r6 r7 and two words on the stack the four plans, a third the kind of
    // chance): one of the four plans at random (by the chance table's kind); 0xFF: Next_End
    bool RandomPlan(Guest &g) {
        Frame frame(g, 40);
        Side s(g, g.R(4));
        const uint32_t stack = frame.entry;
        const int16_t plans[4] = {static_cast<int16_t>(g.R(6)), static_cast<int16_t>(g.R(7)), static_cast<int16_t>(g.Read16(stack + 2)),
                                  static_cast<int16_t>(g.Read16(stack + 6))};
        const int16_t kind = static_cast<int16_t>(g.Read16(stack + 10));
        const uint32_t mode = g.R(5);
        g.Call(kDice64);
        const uint8_t which = g.Read8(kRandomChoices + Signed(kind * 16) + g.R(0));
        s.SetWord(kLever, s.Word(kLeverNow));
        if (plans[which] == 0xFF) {
            CallWith(g, kNextEnd, s.me);
        } else {
            s.SetByte(kRunAgain, 1);
            AnotherPlan(g, s.me, mode, static_cast<uint32_t>(plans[which]));
        }
        g.Return(60);
        return true;
    }

    // 060098C6 (Normal_Attack, r5 the reaction after it, r6 the buttons and lever): a normal attack. First the lever is
    // set (back if the attack is a crouching one, kLeverNow down and back), then, once the fighter's last attack lets it,
    // the frames of kComboSpeed are waited and the buttons pressed; then the reaction (Reaction_Sub). An answer to the
    // other begun (Check_Passive) stops it at any time.
    bool NormalAttack(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const uint32_t buttons = g.R(6);
        const int16_t phase = s.Place(1);
        if (phase != 0 && phase != 1) {
            s.SetWord(kLever, s.Word(kLeverNow));
            s.SetByte(kHitStock, g.Read8(s.me + kHitBy));
            g.R(0) = Signed(s.side);
            g.R(2) = s.At(kHitStock, 1);
            g.R(3) = Signed(static_cast<int8_t>(g.Read8(s.me + kHitBy)));
            CallWith(g, kReaction, s.me, reaction, 0);
            g.Return(60);
            return true;
        }
        if (phase == 0) {
            g.R(0) = 0;
            g.R(3) = kCheckAnswer;
            g.R(4) = s.me;
            g.Call(kCheckAnswer);
            if (Nonzero(g)) {
                g.Return(60);
                return true;
            }
            const uint32_t lever = buttons & 0xFFFF;
            if (lever & 2) {
                g.R(5) = 1;
                s.SetWord(kLeverNow, static_cast<int32_t>(LeverFor(g, s.me, 1)));
                g.R(2) = s.At(kLeverNow, 2);
            } else {
                s.SetWord(kLeverNow, 0);
            }
            s.SetWord(kLeverNow, s.Word(kLeverNow) | static_cast<int32_t>(buttons & 2));
            g.R(2) = SignedWord(g, s.At(kLeverNow, 2));
            s.SetWord(kLever, static_cast<int32_t>(g.R(2)));
            g.R(0) = kLeverNow;
            g.R(3) = s.At(kLever, 2);
            g.R(4) = s.me;
            g.R(5) = reaction;
            g.R(6) = lever;
            g.Call(kAttackBusy);
            if (Nonzero(g)) {
                g.Return(60);
                return true;
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(0) = Signed(s.side * 16);
            g.R(1) = 2;
            g.R(2) = s.PlaceAt(1);
            g.R(3) = kFirstMenu;
            g.R(4) = s.me;
            g.Call(kFirstMenu);
        } else {
            g.R(0) = 1;
        }
        g.R(2) = kCheckAnswer;
        g.R(4) = s.me;
        g.Call(kCheckAnswer);
        if (Nonzero(g)) {
            g.Return(60);
            return true;
        }
        s.SetByte(kComboSpeed, s.Byte(kComboSpeed) - 1);
        if (s.Byte(kComboSpeed) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            s.SetWord(kLever, static_cast<int32_t>(buttons));
        }
        g.R(2) = SignedWord(g, s.At(kLeverNow, 2));
        g.R(0) = SignedWord(g, s.At(kLever, 2)) | g.R(2);
        s.SetWord(kLever, static_cast<int32_t>(g.R(0)));
        g.R(1) = kComboSpeed;
        g.R(3) = s.At(kLever, 2);
        g.R(4) = Signed(s.side * 2);
        g.SetT(false);
        g.Return(60);
        return true;
    }

    // The attack put in: the lever back to kLeverNow, the record's kHitBy kept, and the reaction (Reaction_Sub, the
    // last call, as a jump: what it leaves is the routine's). Leaves r0 the side, r2 kHitStock's place, r3 kHitBy.
    void ToReaction(Side &s, uint32_t reaction, uint32_t power, bool setLever) {
        Guest &g = s.g;
        if (setLever) {
            s.SetWord(kLever, s.Word(kLeverNow));
        }
        s.SetByte(kHitStock, g.Read8(s.me + kHitBy));
        g.R(0) = Signed(s.side);
        g.R(2) = s.At(kHitStock, 1);
        g.R(3) = Signed(static_cast<int8_t>(g.Read8(s.me + kHitBy)));
        CallWith(g, kReaction, s.me, reaction, power);
    }

    // Check_Passive, as the routines call it (r4 the record): true when an answer to the other has begun
    bool AnswerBegun(Side &s) {
        s.g.R(4) = s.me;
        s.g.Call(kCheckAnswer);
        return Nonzero(s.g);
    }

    // The routine's phase on (CP_Index[1] + 1) and Check_First_Menu (kComboSpeed), as the attacks do it
    void FirstMenu(Side &s) {
        Guest &g = s.g;
        s.SetPlace(1, s.Place(1) + 1);
        g.R(0) = Signed(s.side * 16);
        g.R(1) = 2;
        g.R(2) = s.PlaceAt(1);
        g.R(3) = kFirstMenu;
        g.R(4) = s.me;
        g.Call(kFirstMenu);
    }

    // kComboSpeed counted down: true when it runs out. Leaves r1 kComboSpeed, r2 the side's, r3 what is left.
    bool ComboSpeedDone(Side &s) {
        Guest &g = s.g;
        s.SetByte(kComboSpeed, s.Byte(kComboSpeed) - 1);
        g.R(0) = Signed(s.side);
        g.R(1) = kComboSpeed;
        g.R(2) = s.At(kComboSpeed, 1);
        g.R(3) = Signed(static_cast<int8_t>(s.Byte(kComboSpeed)));
        g.SetT(g.R(3) == 0);
        return g.R(3) == 0;
    }

    // 06009A32 (Small_Jump_Measure): back on the lever (Setup_Guard_Lever 1) while kHeldDown has down, else 0. Leaves
    // r1 kHeldDown.
    uint32_t SmallJump(Side &s) {
        Guest &g = s.g;
        g.R(1) = kHeldDown;
        if ((g.Read16(s.At(kHeldDown, 2)) & 2) == 0) {
            g.SetT(true);
            return 0;
        }
        g.R(5) = 1;
        return LeverFor(g, s.me, 1);
    }
    bool SmallJumpLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = SmallJump(s);
        g.Return(10);
        return true;
    }

    // 06009BD4 (Adjust_Attack, r5 the reaction after it, r6 the buttons and lever): a normal attack whose crouch is
    // the other's: crouching (down) unless the other is in pose 0x20 (Check_Squat), and back while kHeldDown has down
    bool AdjustAttack(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const uint32_t buttons = Signed(static_cast<int16_t>(g.R(6)));
        const int16_t phase = s.Place(1);
        if (phase != 0 && phase != 1) {
            ToReaction(s, reaction, 0, true);
            g.Return(60);
            return true;
        }
        if (phase == 0) {
            g.R(0) = 0;
            g.R(3) = kCheckAnswer;
            if (AnswerBegun(s)) {
                g.Return(60);
                return true;
            }
            CallWith(g, kAttackBusy, s.me, reaction, buttons & 0xFFFF);
            if (Nonzero(g)) {
                g.Return(60);
                return true;
            }
            FirstMenu(s);
        } else {
            g.R(0) = 1;
        }
        g.R(2) = kCheckAnswer;
        if (AnswerBegun(s)) {
            g.Return(60);
            return true;
        }
        if (ComboSpeedDone(s)) {
            // (the frames waited: the attack)
            const int32_t crouch = g.Read8(s.Other() + kPose) == 0x20 ? 0 : 2;
            g.R(2) = kPose;
            s.SetWord(kLever, static_cast<int32_t>(buttons) | crouch);
            s.SetWord(kLeverNow, crouch);
            g.R(4) = s.me;
            const uint32_t back = SmallJump(s);
            g.R(3) = SignedWord(g, s.At(kLever, 2)) | back;
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            s.SetPlace(1, s.Place(1) + 1);
            g.R(0) = Signed(s.Place(1));
        } else {
            g.R(2) = SignedWord(g, s.At(kLeverNow, 2));
            s.SetWord(kLever, static_cast<int32_t>(g.R(2)));
            g.R(4) = s.me;
            const uint32_t back = SmallJump(s);
            g.R(0) = back;
            g.R(3) = SignedWord(g, s.At(kLever, 2)) | back;
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
        }
        g.Return(60);
        return true;
    }

    // 06009D98 (Lever_Attack, r5 the reaction after it, r6 the lever's way (Setup_Guard_Lever), r7 the buttons and
    // the rest of the lever): an attack with the lever forward or back. Caught in a throw meanwhile (kDoing 2), it is
    // Be_Catch.
    bool LeverAttack(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const uint32_t way = g.R(6) & 0xFFFF;
        const uint32_t buttons = Signed(static_cast<int16_t>(g.R(7)));
        const int16_t phase = s.Place(1);
        if (phase != 0 && phase != 1) {
            if (s.Field(kDoing) == 2) {
                g.R(0) = 2;
                g.R(2) = kBeCaught;
                g.R(4) = s.me;
                g.Call(kBeCaught);
            } else {
                ToReaction(s, reaction, 0, false);
            }
            g.Return(60);
            return true;
        }
        if (phase == 0) {
            g.R(0) = 0;
            g.R(3) = kCheckAnswer;
            if (AnswerBegun(s)) {
                g.Return(60);
                return true;
            }
            CallWith(g, kLeverAttackBusy, s.me, way, buttons & 0xFFFF);
            if (Nonzero(g)) {
                g.Return(60);
                return true;
            }
            g.R(0) = Signed(s.side);
            g.R(3) = kClearDash;
            g.R(4) = Signed(s.side);
            g.Call(kClearDash);
            FirstMenu(s);
        } else {
            g.R(0) = 1;
        }
        g.R(2) = kCheckAnswer;
        if (AnswerBegun(s)) {
            g.Return(60);
            return true;
        }
        if (ComboSpeedDone(s)) {
            g.R(5) = way;
            g.R(4) = LeverFor(g, s.me, way) | buttons;
            g.R(3) = buttons;
            g.R(2) = s.At(kLever, 2);
            s.SetWord(kLever, static_cast<int32_t>(g.R(4)));
            s.SetPlace(1, s.Place(1) + 1);
            g.R(0) = Signed(s.Place(1));
            g.SetT(false);
        }
        g.Return(60);
        return true;
    }

    // Four words of arguments: r6, r7 and the words of the two longs on the stack
    void FourWords(Guest &g, uint32_t stack, int16_t (&words)[4], uint32_t first, uint32_t second) {
        words[0] = static_cast<int16_t>(first);
        words[1] = static_cast<int16_t>(second);
        words[2] = static_cast<int16_t>(g.Read16(stack + 2));
        words[3] = static_cast<int16_t>(g.Read16(stack + 6));
    }

    // 0600A67C (Branch_Unit_Area, r5 the mode, r6 r7 and two words on the stack a plan for each range): the plan for
    // the range the gap is in, at once
    bool PlanByRange(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        int16_t plans[4];
        FourWords(g, frame.entry, plans, g.R(6), g.R(7));
        const int16_t mode = static_cast<int16_t>(g.R(5));
        g.Write16(s.ModeAt(0), static_cast<uint16_t>(mode));
        s.SetWord(kLever, s.Word(kLeverNow));
        s.SetByte(kRunAgain, 1);
        g.R(1) = kRange;
        g.R(2) = s.At(kRunAgain, 1);
        g.R(4) = s.me;
        AnotherPlan(g, s.me, Signed(mode), Signed(plans[s.Word(kRange)]));
        g.Return(60);
        return true;
    }

    // 06064744, 06065794, 06066874 ... (Computer00-20, a character each, the table at 0x0613E544): the character's
    // plan (kPattern) played: the plan's routine from the character's table (the literal of the routine's ninth
    // instruction), jumped to. Leaves r0 the table, r1 kPattern, r2 the plan's routine, r3 its offset, T clear, for the
    // plan's routine (which reads none of them).
    bool PlayPlan(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t at = g.PC() + 16;
        const uint32_t table = g.Read32(((at & ~3u) + 4) + (g.Read16(at) & 0xFF) * 4u);
        g.R(1) = kPattern;
        g.R(3) = Signed(s.Word(kPattern)) << 2;
        g.R(0) = table;
        g.R(2) = g.Read32(table + g.R(3));
        g.SetT(false);
        g.Call(g.R(2));
        g.Return(12);
        return true;
    }

    // The command to put in (Tech_Address: the character's command `move & 0xFF`), from its first step, and the lever
    // back to kLeverNow
    void SetCommand(Side &s, uint32_t move) {
        Guest &g = s.g;
        const uint32_t commands = g.Read32(kCommands + (Signed(s.Field(kCharacter)) << 2));
        g.Write32(s.At(kTechAddress, 4), g.Read32(commands + (move & 0xFF) * 4));
        s.SetWord(kTechIndex, 12);
        s.SetWord(kLever, s.Word(kLeverNow));
    }
    // The word of the command where it has got to (its kind of step)
    int16_t CommandStep(Side &s) {
        Guest &g = s.g;
        return static_cast<int16_t>(g.Read16(g.Read32(s.At(kTechAddress, 4)) + (Signed(s.Word(kTechIndex)) << 1)));
    }
    // The fighter's animation may not be broken into (kMoveKind 0x40), or it is doing nothing (kDoing 0): the attack is
    // over
    bool AttackOver(Side &s) {
        return s.g.Read8(s.me + kMoveKind) == 0x40 || s.Field(kDoing) == 0;
    }

    // 0600D390 (Command_Attack, r5 the reaction after it, r6 the move (Tech_Number), r7 the power, a word on the stack
    // Ex_Shot): a special move: its command (the character's list of levers) put in a step a frame, once the frames of
    // kComboSpeed are waited; then the reaction. A move of power with 0x4000 holds the lever kPowerLevers has for it
    // after.
    bool CommandAttack(Guest &g) {
        Frame frame(g, 48);
        Side s(g, g.R(4));
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const uint32_t move = g.R(6) & 0xFFFF;
        const uint32_t power = Signed(static_cast<int16_t>(g.R(7)));
        const uint32_t level = power & 15;
        const uint32_t exShot = Signed(static_cast<int16_t>(g.Read16(frame.entry + 2)));
        const auto exit = [&](uint32_t r0) {
            g.R(0) = r0;
            g.Return(80);
            return true;
        };
        const auto toExit = [&]() {
            CallWith(g, kReactionExit, s.me, g.R(5), g.R(6));
            g.Return(80);
            return true;
        };
        int16_t phase = s.Place(1);
        switch (phase) {
        case 0:
            g.R(4) = Signed(s.side);
            g.Call(kClearDash);
            SetCommand(s, move);
            CallWith(g, kCanCommand, s.me, reaction, move & 0x80FF);
            if (Nonzero(g)) {
                return exit(g.R(0));
            }
            CallWith(g, kDashHit, s.me, move & 0x80FF, g.R(6));
            if (Nonzero(g)) {
                BeFree(g, s.me);
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(4) = s.me;
            g.Call(kFirstMenu);
            s.SetWord(kFreeLever, (power & 0x4000) ? static_cast<int16_t>(g.Read16(kPowerLevers + (level - 8) * 2)) : 0);
            [[fallthrough]];
        case 1:
            if (!ComboSpeedDone(s)) {
                s.SetWord(kLever, s.Word(kLeverNow));
                return exit(Signed(s.side));
            }
            CallWith(g, kHadou, s.me, move & 0x80FF, g.R(6));
            if (Nonzero(g)) {
                // (the lever not back yet: a frame more)
                if (AnswerBegun(s)) {
                    return exit(g.R(0));
                }
                s.SetByte(kComboSpeed, 1);
                return exit(Signed(s.side));
            }
            s.SetPlace(1, s.Place(1) + 1);
            CallWith(g, kCheckRapid, s.me, move, g.R(6));
            [[fallthrough]];
        case 2: {
            if (AnswerBegun(s)) {
                return exit(g.R(0));
            }
            const int16_t step = CommandStep(s);
            if (step == 2) {
                CallWith(g, kCommand1, s.me, level, exShot);
                if (g.R(0) == 0) {
                    return exit(0);
                }
                s.SetPlace(1, s.Place(1) + 1);
                return exit(Signed(s.Place(1)));
            }
            g.R(7) = exShot;
            if (step == 7) {
                CallWith(g, kCommand6, s.me, level, move);
                if (g.R(0) == 0) {
                    return exit(0);
                }
                s.SetPlace(1, s.Place(1) + 1);
                return exit(Signed(s.Place(1)));
            }
            CallWith(g, kCommand0, s.me, level, move);
            if (g.R(0) != 0xFFFFFFFF) {
                return exit(g.R(0));
            }
            // (all put in: a dash (moves 0 and 1) runs, or the attack is followed to its end)
            if ((move & 15) == 0 || (move & 15) == 1) {
                if (reaction == 12) {
                    s.SetPlace(1, 99);
                    s.SetWord(kTimer, g.Read8(kDashTimes + (Signed(s.Field(kCharacter)) << 1) + move));
                    return exit(move);
                }
                s.SetPlace(1, 4);
                return exit(4);
            }
            s.SetPlace(1, 3);
            return exit(3);
        }
        case 3:
            s.SetWord(kLever, s.Word(kLeverNow));
            if (g.Read8(kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize)) + kHolding) != 0) {
                return exit(Signed(static_cast<int16_t>(s.side * kFighterSize)));
            }
            if (AttackOver(s)) {
                return toExit();
            }
            s.SetWord(kLever, s.Word(kFreeLever));
            g.R(4) = s.me;
            g.Call(kMashSub);
            ToReaction(s, reaction, power, false);
            g.Return(80);
            return true;
        case 4:
            s.SetWord(kLever, s.Word(kLeverNow));
            if (g.Read8(s.me + kMoveKind) == 0x40 || g.Read8(s.me + kRunning) == 0) {
                return toExit();
            }
            return exit(kRunning);
        case 5:
            if (s.Word(kGap) > 112) {
                s.SetWord(kLever, 0x40);
            }
            if (AttackOver(s)) {
                return toExit();
            }
            return exit(kDoing);
        default:
            s.SetWord(kLever, s.Word(kLeverNow));
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            if (s.Word(kTimer) == 0) {
                return toExit();
            }
            return exit(Signed(s.side * 2));
        }
    }

    // 0600D874 (J_Command_Attack, r5 the reaction after it, r6 the move, r7 the power, a word on the stack Ex_Shot): a
    // special move put in as Command_Attack does, given up (Next_Be_Free) when a projectile comes from above
    // (Check_Diagonal_Shell); after it, the reaction, or for the others the landing (Check_Landed)
    bool JumpCommandAttack(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const uint32_t move = g.R(6) & 0xFFFF;
        const uint32_t power = Signed(static_cast<int16_t>(g.R(7)));
        const uint32_t level = power & 15;
        const uint32_t exShot = Signed(static_cast<int16_t>(g.Read16(frame.entry + 2)));
        const auto exit = [&](uint32_t r0) {
            g.R(0) = r0;
            g.Return(80);
            return true;
        };
        switch (s.Place(1)) {
        case 0:
            g.R(4) = Signed(s.side);
            g.Call(kClearDash);
            SetCommand(s, move);
            CallWith(g, kCanCommand, s.me, reaction, move & 0x80FF);
            if (Nonzero(g)) {
                return exit(g.R(0));
            }
            CallWith(g, kDashHit, s.me, move & 0x80FF, g.R(6));
            if (Nonzero(g)) {
                BeFree(g, s.me);
            }
            s.SetByte(kContinue, 0);
            s.SetPlace(1, s.Place(1) + 1);
            g.R(4) = s.me;
            g.Call(kFirstMenu);
            [[fallthrough]];
        case 1:
            if (AnswerBegun(s)) {
                return exit(g.R(0));
            }
            if (!ComboSpeedDone(s)) {
                s.SetWord(kLever, s.Word(kLeverNow));
                return exit(Signed(s.side));
            }
            g.R(4) = s.me;
            g.Call(kShellAbove);
            if (Nonzero(g)) {
                BeFree(g, s.me);
                g.Return(80);
                return true;
            }
            s.SetPlace(1, s.Place(1) + 1);
            [[fallthrough]];
        case 2: {
            if (AnswerBegun(s)) {
                return exit(g.R(0));
            }
            if (CommandStep(s) == 2) {
                CallWith(g, kCommand1, s.me, level, exShot);
                if (g.R(0) == 0) {
                    return exit(0);
                }
                s.SetPlace(1, s.Place(1) + 1);
                return exit(Signed(s.Place(1)));
            }
            g.R(7) = exShot;
            CallWith(g, kCommand0, s.me, level, move);
            if (g.R(0) != 0xFFFFFFFF) {
                return exit(g.R(0));
            }
            s.SetPlace(1, 99);
            return exit(99);
        }
        case 3:
            CallWith(g, kCheckRapid, s.me, move, g.R(6));
            s.SetPlace(1, s.Place(1) + 1);
            return exit(Signed(s.Place(1)));
        default:
            s.SetWord(kLever, s.Word(kLeverNow));
            s.SetByte(kHitStock, g.Read8(s.me + kHitBy));
            if (AttackOver(s)) {
                CallWith(g, kReactionExit, s.me, g.R(5), g.R(6));
            } else {
                g.R(4) = s.me;
                g.Call(kMashSub);
                if (static_cast<int32_t>(reaction) == 12) {
                    CallWith(g, kReaction, s.me, 12, power);
                } else {
                    CallWith(g, kLanded, s.me, reaction & 0xFFF, g.R(6));
                }
            }
            g.Return(80);
            return true;
        }
    }

    // A level: from a level routine (r4 the row), the bonus added if `bonus`, at its best when a challenger broke in, and
    // 2 for the weak side outside a demo; kept in kLevelNow as the game keeps it
    int8_t Level(Side &s, uint32_t routine, uint32_t row, bool bonus, int8_t best, bool weakSide = true) {
        Guest &g = s.g;
        g.R(4) = row;
        g.Call(routine);
        g.Write8(kLevelNow, static_cast<uint8_t>(g.R(0)));
        if (bonus) {
            g.Write8(kLevelNow, static_cast<uint8_t>(g.Read8(kLevelNow) + g.Read8(kLevelBonus)));
        }
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, static_cast<uint8_t>(best));
        }
        if (weakSide && g.Read8(kDemo) == 0 && g.Read8(kWeakSide) == static_cast<uint32_t>(s.side)) {
            g.Write8(kLevelNow, 2);
        }
        return static_cast<int8_t>(g.Read8(kLevelNow));
    }

    // 0600F87E (Select_Combo_Speed): the frames before an attack's button: one of 32 at random, by the character (a
    // broken-in fight's own) and the level of 18
    bool ComboSpeed(Guest &g) {
        Side s(g, g.R(4));
        g.Call(kDice128);
        const int8_t dice = static_cast<int8_t>(g.R(0));
        const int8_t level = Level(s, kLevelOf18, 8, true, 19);
        const uint8_t brokenInto = g.Read8(kBrokenInto);
        const uint32_t row = brokenInto == 1 || brokenInto == 2 ? 18 : Signed(s.Field(kCharacter));
        const uint32_t at = kComboSpeeds + Signed(static_cast<int16_t>(row * 640)) + Signed(level * 32) + Signed(dice);
        g.R(0) = Signed(static_cast<int8_t>(g.Read8(at)));
        g.Return(60);
        return true;
    }

    // 0600FB74 (Setup_LP_Data): how long to look: one of 32 at random by the level of 8
    bool LookTime(Guest &g) {
        Side s(g, g.R(4));
        const int8_t level = Level(s, kLevelOf8, 0, false, 7);
        g.Call(kDice128);
        g.R(0) = g.Read8(kLookTimes + Signed(level * 32) + g.R(0));
        g.Return(40);
        return true;
    }

    // 0600FBCA (Setup_WT_Data): how long to wait: one of 8 at random by the level of 4
    bool WaitTime(Guest &g) {
        Side s(g, g.R(4));
        const int8_t level = Level(s, kLevelOf4, 0, false, 3);
        g.Call(kDice64);
        g.R(0) = g.Read8(kWaitTimes + Signed(level * 8) + (g.R(0) & 7));
        g.Return(40);
        return true;
    }

    // 0601047C (Check_First_Menu): kComboSpeed: 1 at the plan's first step, else Select_Combo_Speed
    bool FirstMenuLift(Guest &g) {
        Side s(g, g.R(4));
        if (s.Place(0) == 0) {
            s.SetByte(kComboSpeed, 1);
            g.R(0) = Signed(s.side);
            g.R(1) = kPlanPlace;
            g.R(3) = 1;
            g.SetT(true);
        } else {
            Frame frame(g, 16);
            g.Call(0x0600F87E);
            s.SetByte(kComboSpeed, static_cast<int32_t>(g.R(0)));
        }
        g.Return(20);
        return true;
    }

    // 0600E108 (Check_Start_Command_Attack, r5 the reaction, r6 the move): 1 while the special move may not start:
    // being thrown, or in an attack that may not be cancelled into it (kCancel: 2 for moves 0 and 1, the dashes; 0x40
    // for a move of bit 0x8000; 0x20 any; 8 for reaction 14). Just after a jump's attack (kBeforeJump), it may: the flag
    // is cleared.
    bool CommandBusy(Guest &g) {
        Side s(g, g.R(4));
        const uint8_t cancel = g.Read8(s.me + kCancel);
        const uint32_t move = g.R(6) & 0xFFFF;
        bool busy;
        if (s.Byte(kBeforeJump) != 0) {
            s.SetByte(kBeforeJump, 0);
            busy = false;
        } else if (s.Field(kDoing) == 2) {
            busy = true;
        } else if (s.Field(kDoing) != 4 || g.Read8(s.me + kMoveKind) == 0x40) {
            busy = false;
        } else if (move == 0 || move == 1) {
            busy = (cancel & 2) == 0;
        } else if ((move & 0x8000) && (cancel & 0x40)) {
            busy = false;
        } else if (cancel & 0x20) {
            busy = false;
        } else if ((cancel & 8) && static_cast<int16_t>(g.R(5)) == 14) {
            busy = false;
        } else {
            busy = true;
        }
        g.R(0) = busy ? 1 : 0;
        g.Return(20);
        return true;
    }

    // 0600D7DE (Hadou_Check, r5 the move): for the character's one move that needs it (a projectile's command), 1 while
    // the lever has been forward in the last nine frames (Check_Resume_Lever): the command waits
    bool LeverNotBack(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t entry = kHadouMoves + (Signed(s.Field(kCharacter)) << 3);
        bool wait = false;
        if (g.Read32(entry) != 0 && g.Read32(entry + 4) == (g.R(5) & 0xFFFF)) {
            const uint16_t forward = g.Read8(s.me + kMovesFaceRight) != 0 ? 8 : 4;
            const uint32_t levers = kLastLevers + Signed(static_cast<int8_t>(g.Read8(s.me + kSide + 1) * 40));
            for (uint32_t n = 0; n <= 8 && !wait; n++) {
                wait = g.Read16(levers + n * 2) == forward;
            }
        }
        g.R(0) = wait ? 1 : 0;
        g.Return(30);
        return true;
    }

    // 0600DE3C (Setup_Rapid_End_Term, r5 the move): what ends the mash, from the move's bits 8-11; a time (4) is 60
    // frames
    void RapidEnd(Side &s, uint32_t move) {
        const int32_t kind = static_cast<int16_t>(move) & 0xF00;
        s.g.Write8(s.At(kRapid, 4) + 2, static_cast<uint8_t>(kind >> 8));
        if (kind == 0x400) {
            s.g.Write8(s.At(kRapid, 4) + 3, 60);
        }
    }
    bool RapidEndLift(Guest &g) {
        Side s(g, g.R(4));
        RapidEnd(s, g.R(5));
        g.Return(20);
        return true;
    }

    // 0600DDB8 (Check_Rapid, r5 the move): a move with bits 8-11 has its button mashed after it: how, one of eight at
    // random by the level of 8, and what ends it
    bool CheckRapid(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t move = g.R(5) & 0xFFFF;
        if ((move & 0xF00) == 0) {
            g.Write8(s.At(kRapid, 4), 0);
        } else {
            Frame frame(g, 28);
            const int8_t level = Level(s, kLevelOf8, 0, false, 7, false);
            g.Call(kDice128);
            g.Write8(s.At(kRapid, 4), g.Read8(kMashes + Signed(level * 8) + (g.R(0) & 7)));
            g.Write8(s.At(kRapid, 4) + 1, 0);
            RapidEnd(s, move);
            s.SetWord(kRapidButtons, 0x3F0);
        }
        g.Return(40);
        return true;
    }

    // 0600E014 (Check_Rapid_End): is the mash over? (and then it is stopped)
    bool RapidOver(Side &s) {
        Guest &g = s.g;
        const uint32_t rapid = s.At(kRapid, 4);
        const uint32_t fighter = kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize));
        bool over = false;
        switch (g.Read8(rapid + 2)) {
        case 1: over = s.Field(kRising) < 0; break;
        case 2:
            if (g.Read8(rapid + 3) == 0) {
                if (g.Read8(fighter + kAttacking) != 0) {
                    g.Write8(rapid + 3, 1);
                }
            } else if (g.Read8(rapid + 3) == 1) {
                over = g.Read8(fighter + kAttacking) == 0;
            }
            break;
        case 4:
            if (g.Read8(rapid + 3) == 0) {
                if (s.Field(kAttackStep) != 0) {
                    g.Write8(rapid + 3, 1);
                }
            } else if (g.Read8(rapid + 3) == 1) {
                g.Write8(rapid + 3, 0);
                over = true;
            }
            break;
        default: break;
        }
        if (over) {
            g.Write8(rapid, 0);
        }
        return over;
    }
    bool RapidOverLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = RapidOver(s) ? 1 : 0;
        g.Return(30);
        return true;
    }

    // 0600DEA4 (Rapid_Sub): the button mashed (kRapidButtons pressed every few frames, kTimer counting them), until
    // Check_Rapid_End says it is over. Mash 2 presses three times, pauses 24 frames, then every other frame; the others
    // every other frame.
    bool Mash(Guest &g) {
        Side s(g, g.R(4));
        if (RapidOver(s)) {
            g.Return(40);
            return true;
        }
        const uint32_t rapid = s.At(kRapid, 4);
        const uint8_t how = g.Read8(rapid), phase = g.Read8(rapid + 1);
        const auto press = [&]() {
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            if (s.Word(kTimer) != 0) {
                return false;
            }
            s.SetWord(kLever, s.Word(kRapidButtons));
            return true;
        };
        if (how == 2) {
            switch (phase) {
            case 0:
                g.Write8(rapid + 1, 1);
                s.SetWord(kTimer, 1);
                s.SetWord(kTimer2, 3);
                break;
            case 1:
                if (press()) {
                    s.SetWord(kTimer, 2);
                    s.SetWord(kTimer2, s.Word(kTimer2) - 1);
                    if (s.Word(kTimer2) == 0) {
                        g.Write8(rapid + 1, 2);
                        s.SetWord(kTimer2, 24);
                    }
                }
                break;
            case 2:
                s.SetWord(kTimer2, s.Word(kTimer2) - 1);
                if (s.Word(kTimer2) == 0) {
                    g.Write8(rapid + 1, 3);
                    s.SetWord(kTimer, 1);
                    s.SetWord(kTimer2, 2);
                }
                break;
            default:
                if (press()) {
                    s.SetWord(kTimer, s.Word(kTimer2));
                }
                break;
            }
        } else if (how != 0) {
            if (phase == 0) {
                g.Write8(rapid + 1, 1);
                s.SetWord(kTimer, 1);
                s.SetWord(kTimer2, 2);
            } else if (press()) {
                s.SetWord(kTimer, s.Word(kTimer2));
            }
        }
        g.Return(60);
        return true;
    }

    // 06010350 (Reaction_Exit_Sub): the attack done: the plan on to its next step, the routine's words, the recovery,
    // counter-attack, limit and jump flags cleared (and, but in mode 6, the answer's). Leaves r0 the mode (6) or the
    // side, r1 kMode, r3 the last flag's place, r5 0, r6 the side's plan place, T set in mode 6.
    void ReactionExit(Side &s) {
        Guest &g = s.g;
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        for (const uint32_t flag : {kRecovering, kCounter, kLimited, kBeforeJump}) {
            s.SetByte(flag, 0);
        }
        g.R(1) = kMode;
        g.R(3) = s.At(kBeforeJump, 1);
        g.R(5) = 0;
        g.R(6) = s.PlaceAt(0);
        g.R(0) = 6;
        g.SetT(s.Mode() == 6);
        if (s.Mode() != 6) {
            s.SetByte(kAnswering, 0);
            g.R(0) = Signed(s.side);
            g.R(3) = s.At(kAnswering, 1);
        }
    }
    bool ReactionExitLift(Guest &g) {
        Side s(g, g.R(4));
        ReactionExit(s);
        g.Return(40);
        return true;
    }

    // 0600FD7E (Next_End): the plan ended, unless a guard begins (Check_Guard): its step set to -1, the end. Leaves r0
    // 1 when the guard began; else r0 0, r3 the third word's place, r4 the side's plan place, r5 0, T set.
    bool NextEnd(Guest &g) {
        Side s(g, g.R(4));
        {
            Frame frame(g, 8);
            g.Call(kCheckGuard);
        }
        if (Nonzero(g)) {
            g.R(0) = 1;
        } else {
            s.SetPlace(0, -1);
            for (int n = 1; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            g.R(0) = 0;
            g.R(3) = s.PlaceAt(2);
            g.R(4) = s.PlaceAt(0);
            g.R(5) = 0;
        }
        g.Return(30);
        return true;
    }

    // 0600EFB8 (Check_Landed, r5 the reaction): has the jump landed? (on the floor now, from the air or out of an
    // attack) Then the lever let go and the plan ended (kContinue) or its reaction run, a guard looked for, and
    // kBeforeJump set. 1 when it has.
    bool Landed(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 20);
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const bool wasUp = s.Field(kWasUp) != 0, down = s.Field(kY) == 0;
        bool fromAir;
        if (wasUp && down) {
            fromAir = true;
        } else if (!wasUp && down && s.Field(kDoing) != 4) {
            fromAir = false;
        } else {
            g.R(0) = 0;
            g.R(2) = Signed(s.Field(kY));
            g.R(3) = Signed(s.Field(kWasUp));
            g.R(4) = kLever;
            g.R(6) = kContinue;
            g.R(7) = 0;
            g.SetT(!wasUp && down);
            g.Return(30);
            return true;
        }
        s.SetWord(kLever, 0);
        g.R(2) = 0; // (the height, on the floor)
        g.R(3) = Signed(static_cast<int8_t>(s.Byte(kContinue)));
        g.R(4) = s.me;
        g.R(6) = kContinue;
        g.R(7) = 0;
        if (s.Byte(kContinue) != 0) {
            g.Call(kNextEnd);
            s.SetByte(kBeforeJump, 1);
        } else {
            CallWith(g, kReaction, s.me, reaction, 0);
            if (fromAir) {
                g.R(3) = Signed(s.Word(kLeverNow));
                g.R(2) = Signed(s.Word(kLever)) | g.R(3);
                s.SetWord(kLever, static_cast<int32_t>(g.R(2)));
                g.R(3) = kCheckGuard;
            }
            g.R(4) = s.me;
            g.Call(kCheckGuard);
            if (fromAir || !Nonzero(g)) {
                s.SetByte(kBeforeJump, 1);
            }
        }
        g.R(0) = 1;
        g.Return(60);
        return true;
    }

    // 0600BE70 (Jump_Attack_Term, r5 Range_X, r6 Range_Y, r7 the reaction; on the stack the lever and buttons of the
    // attack, the way of the jump, Range_JX, Range_JY and the lever of an attack against one in the air): a jump and
    // its attack. Once the fighter's attack lets it, kComboSpeed is waited and the jump made (Jump_Init); in the air,
    // guarding if it must (Check_Air_Guard), the attack comes against one jumping (Check_VS_Air_Attack) or when the gap
    // and the height meet the ranges; then it waits to land (Check_Landed). From the attack on, the lever holds
    // kLeverNow too.
    bool JumpAttack(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 40);
        const uint32_t rangeX = Signed(static_cast<int16_t>(g.R(5))), rangeY = Signed(static_cast<int16_t>(g.R(6)));
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(7)));
        const uint32_t stack = frame.entry;
        const auto arg = [&](uint32_t n) { return Signed(static_cast<int16_t>(g.Read16(stack + n * 4 + 2))); };
        const uint32_t buttons = arg(0), way = arg(1), rangeJX = arg(2), rangeJY = arg(3), airLever = arg(4);
        const auto landed = [&](uint32_t r) {
            CallWith(g, kLanded, s.me, r, g.R(6));
            return Nonzero(g);
        };
        switch (s.Place(1)) {
        case 0:
            CallWith(g, kSetLeverNow, s.me, Signed(s.side), reaction & 0xF000);
            if (s.Field(kDoing) == 4 && g.Read8(s.me + kMoveKind) != 0x40) {
                break;
            }
            g.R(4) = Signed(s.side);
            g.Call(kClearHighJump);
            s.SetByte(kContinue, 0);
            g.Write8(s.me + kHitBy, 0);
            s.SetPlace(1, s.Place(1) + 1);
            g.R(4) = s.me;
            g.Call(kFirstMenu);
            [[fallthrough]];
        case 1:
            if (AnswerBegun(s) || !ComboSpeedDone(s)) {
                break;
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(4) = Signed(s.side);
            g.Call(kClearDash);
            CallWith(g, kStartJump, s.me, way, g.R(6));
            g.R(4) = s.me;
            g.Call(kShellAbove);
            if (Nonzero(g)) {
                BeFree(g, s.me);
            }
            break;
        case 2:
            if (s.Field(kY) > 0) {
                s.SetPlace(1, s.Place(1) + 1);
            } else {
                s.SetWord(kLever, s.Word(kJumpLever));
            }
            break;
        case 3:
            g.R(4) = s.me;
            g.Call(kAirGuard);
            if (landed(reaction)) {
                break;
            }
            g.R(7) = airLever;
            CallWith(g, kVsAirAttack, s.me, rangeJX, rangeJY);
            if (Nonzero(g)) {
                break;
            }
            g.R(4) = s.me;
            g.Call(kAttackTime); // (its answer not used)
            CallWith(g, kMeetsLimit, s.me, Signed(s.Word(kGap)), rangeX);
            if (!Nonzero(g)) {
                break;
            }
            CallWith(g, kHeightMeetsLimit, s.me, Signed(s.Field(kY)), rangeY);
            if (!Nonzero(g)) {
                break;
            }
            CallWith(g, kMeetsLimit, s.me, Signed(s.Field(kY)), rangeY);
            if (!Nonzero(g)) {
                break;
            }
            CallWith(g, kJumpAttackLever, s.me, buttons & 0xFFFF, g.R(6));
            s.SetWord(kLever, static_cast<int32_t>(g.R(0)));
            s.SetPlace(1, s.Place(1) + 1);
            s.SetByte(kHitStock, 0);
            break;
        case 4:
            g.R(4) = s.me;
            g.Call(kAirGuard);
            if (g.Read8(s.me + kHitBy) != 0) {
                s.SetByte(kHitStock, g.Read8(s.me + kHitBy));
            }
            landed(reaction & 127);
            break;
        case 5:
            if (!landed(reaction & 127)) {
                g.R(7) = 0xFFFFFFFF;
                CallWith(g, kCommand0, s.me, 8, 0xFFFFFFFF);
                if (g.R(0) == 0xFFFFFFFF) {
                    s.SetPlace(1, 99);
                }
            }
            break;
        default:
            s.SetByte(kHitStock, g.Read8(s.me + kHitBy));
            landed(reaction & 0xFFF);
            break;
        }
        if (s.Place(1) >= 3) {
            s.SetWord(kLever, s.Word(kLever) | s.Word(kLeverNow));
            g.R(0) = kLeverNow;
        } else {
            g.R(0) = Signed(s.Place(1));
        }
        g.Return(100);
        return true;
    }

    // 06011B76 (Setup_Lever_LR, r5 a side, r6 bits 12-15 of a reaction): kLeverNow from the bits: none, nothing; 0x1000
    // up, else 0x2000 down added; then 0x4000 forward, else 0x8000 back added. Leaves r5 the bits, r7 the side's
    // kLeverNow.
    bool SetLeverNow(Guest &g) {
        const uint32_t me = g.R(4);
        const int16_t side = static_cast<int16_t>(g.R(5));
        const int16_t bits = static_cast<int16_t>(g.R(6));
        const uint32_t lever = kLeverNow + Signed(side * 2);
        g.R(5) = Signed(bits);
        g.R(7) = lever;
        if (bits == 0) {
            g.Write16(lever, 0);
        } else {
            if (bits & 0x1000) {
                g.Write16(lever, 1);
            } else if (bits & 0x2000) {
                g.Write16(lever, static_cast<uint16_t>(g.Read16(lever) | 2));
            }
            if (bits & 0x4000) {
                g.Write16(lever, static_cast<uint16_t>(g.Read16(lever) | LeverFor(g, me, 0)));
            } else if (bits & 0x8000) {
                g.Write16(lever, static_cast<uint16_t>(g.Read16(lever) | LeverFor(g, me, 1)));
            }
        }
        g.Return(30);
        return true;
    }

    // The game's command reader (the lever's moves the fighter's commands are made of): 0x620 bytes a player at
    // 0x020256C4, 28 a command, started from the character's list of them (0x02006284: a long a character, a list of
    // the commands' data); 0x0202632E, 0x406 a player, a word a command, cleared with it
    constexpr uint32_t kCommandReader = 0x020256C4, kCommandLists = 0x02006284, kCommandDone = 0x0202632E;
    constexpr uint32_t kCharacterOf = 0x0206902C; // player 1's record's kCharacter (players kFighterSize apart)

    // 060B5D94: command `which` of player r4's command reader started again from its data (r6 the character's list).
    // Leaves r0 the player's offset in kCommandDone, r2 kCommandReader, r3 the player's kCommandDone, r5 the command's
    // word there, r6 its place in the reader, r7 0.
    void RestartCommand(Guest &g, uint32_t player, uint32_t which, uint32_t list) {
        uint32_t data = g.Read32(list + Signed(static_cast<int16_t>(which)) * 4) + 24;
        const uint32_t slot = kCommandReader + Signed(static_cast<int16_t>(static_cast<int16_t>(player) * 0x620)) +
                              Signed(static_cast<int16_t>(static_cast<int16_t>(which) * 28));
        g.Write16(slot, g.Read16(data));
        g.Write16(slot + 2, g.Read16(data + 2));
        g.Write16(slot + 4, g.Read16(data + 4));
        g.Write16(slot + 12, g.Read16(data + 4));
        g.Write16(slot + 6, g.Read16(data + 6));
        data += 8;
        g.Write32(slot + 8, data);
        for (const uint32_t n : {18u, 20u, 22u, 26u, 24u}) {
            g.Write16(slot + n, 0);
        }
        const uint32_t done = kCommandDone + Signed(static_cast<int16_t>(static_cast<int16_t>(player) * 0x406));
        g.Write16(done + (Signed(static_cast<int16_t>(which)) << 1), 0);
        g.R(0) = done - kCommandDone;
        g.R(2) = kCommandReader;
        g.R(3) = done;
        g.R(5) = done + (Signed(static_cast<int16_t>(which)) << 1);
        g.R(6) = slot;
        g.R(7) = 0;
        g.SetT(false); // (the shift of the command's number)
    }

    // 060B5CD6 (hi_jump_flag_clear, r4 the player): the high jump's command (2) read again from its start
    bool ClearHighJump(Guest &g) {
        const uint32_t player = Signed(static_cast<int16_t>(g.R(4)));
        const uint32_t character = SignedWord(g, kCharacterOf + Signed(static_cast<int16_t>(static_cast<int16_t>(player) * kFighterSize)));
        RestartCommand(g, player, 2, g.Read32(kCommandLists + (character << 2)));
        g.R(4) = player;
        g.Return(40);
        return true;
    }

    // 0600C196 (Check_VS_Air_Attack, r5 Range_JX, r6 Range_JY, r7 the lever of the attack): an attack at the other in
    // the air (or in a jumping pose): when the gap (Check_Term_Sub_Air) and the height between them (Ck_Distance_Height)
    // meet the ranges, it waits the reflex time (Select_Reflection_Time), then attacks: the lever (1), or with bit 0x4000
    // the command `lever & 0x3FFF` (-1, two phases on). Either way the plan ends after it (kContinue).
    bool VsAirAttack(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 32);
        const int16_t rangeJX = static_cast<int16_t>(g.R(5)), rangeJY = static_cast<int16_t>(g.R(6));
        const int16_t lever = static_cast<int16_t>(g.R(7));
        const auto result = [&](uint32_t r0) {
            g.R(0) = r0;
            g.Return(60);
            return true;
        };
        if ((rangeJX == -1 && rangeJY == -1) || lever == -1) {
            return result(0);
        }
        const uint32_t other = s.Other();
        const uint8_t pose = g.Read8(other + kPose);
        if (pose != 14 && pose != 20 && pose != 26 && static_cast<int16_t>(g.Read16(other + kY)) <= 0) {
            return result(0);
        }
        CallWith(g, kMeetsLimitAir, s.me, Signed(s.Word(kGap)), Signed(rangeJX));
        if (!Nonzero(g)) {
            return result(0);
        }
        g.R(4) = s.me;
        g.Call(kHeightGap);
        CallWith(g, kMeetsLimit, s.me, g.R(0), Signed(rangeJY));
        if (!Nonzero(g)) {
            return result(0);
        }
        if (s.Place(2) == 0) {
            s.SetPlace(2, s.Place(2) + 1);
            g.R(4) = s.me;
            g.Call(kReflexTime);
            s.SetWord(kTimer2, static_cast<int32_t>(g.R(0)));
            s.SetWord(kTimer2, s.Word(kTimer2) + 1);
            return result(0);
        }
        s.SetWord(kTimer2, s.Word(kTimer2) - 1);
        if (s.Word(kTimer2) != 0) {
            return result(0);
        }
        s.SetByte(kContinue, 1);
        if (lever & 0x4000) {
            s.SetPlace(1, s.Place(1) + 2);
            const uint32_t commands = g.Read32(kCommands + (Signed(s.Field(kCharacter)) << 2));
            g.Write32(s.At(kTechAddress, 4), g.Read32(commands + Signed(lever & 0x3FFF) * 4));
            return result(0xFFFFFFFF);
        }
        s.SetWord(kLever, lever);
        s.SetPlace(1, s.Place(1) + 1);
        return result(1);
    }

    // 0600B974 (Check_Air_Guard): in the air, guard? Not when kLeverNow already holds a lever, nor against an attack
    // already looked at (kGuardedAttack is kAttacksSeen), nor with no attack coming, nor out of its reach. Else the
    // attack is looked at once, and the guard chosen as on the ground (the level of 10, the bonus, at most 10; the
    // table of guards by character, 3 none): kLeverNow down and back, the guard's kind in kGuardType (with 0x8000).
    // Leaves r0 kLeverNow (or the guard), and the registers as the game's code does.
    bool AirGuard(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 36);
        const uint32_t other = s.Other();
        g.R(4) = kLeverNow;
        g.R(7) = other;
        g.R(3) = g.Read16(s.At(kLeverNow, 2));
        const auto leverNow = [&]() {
            g.R(0) = g.Read16(s.At(kLeverNow, 2));
            g.SetT(false); // (the side's offset shifted)
            g.Return(30);
            return true;
        };
        if (g.R(3) != 0) {
            g.SetT(false);
            return leverNow();
        }
        g.R(6) = kGuardedAttack;
        g.R(5) = kAttacksSeen;
        g.R(3) = SignedWord(g, s.At(kGuardedAttack, 2));
        g.R(2) = SignedWord(g, s.At(kAttacksSeen, 2));
        if (g.R(3) == g.R(2)) {
            g.SetT(true);
            return leverNow();
        }
        g.R(1) = kOtherAttacking;
        g.R(3) = Signed(static_cast<int8_t>(s.Byte(kOtherAttacking)));
        if (g.R(3) == 0) {
            g.SetT(true);
            return leverNow();
        }
        const int16_t reach = static_cast<int16_t>(static_cast<int16_t>(g.Read16(kReachByClass + g.Read8(other + kAttackReach) * 2u)) + 32 +
                                                   s.Word(kWidth));
        g.R(7) = Signed(s.side * 2);
        g.R(3) = Signed(s.Word(kGap));
        g.R(4) = Signed(reach);
        if (s.Word(kGap) > reach) {
            g.R(0) = 0;
            g.SetT(true);
            g.Return(30);
            return true;
        }
        // (the attack looked at: the guard's level and the dice)
        s.SetWord(kGuardedAttack, s.Word(kAttacksSeen));
        g.R(5) = s.At(kAttacksSeen, 2);
        g.R(6) = s.At(kGuardedAttack, 2);
        g.R(4) = 0;
        g.Call(kLevelOf10);
        g.Write8(kLevelNow, static_cast<uint8_t>(g.R(0)));
        if (g.Read8(kDemo) == 0 && g.Read8(kWeakSide) == static_cast<uint32_t>(s.side)) {
            g.Write8(kLevelNow, 2);
        }
        g.Call(kDice64);
        g.Write8(kDiceNow, static_cast<uint8_t>(g.R(0)));
        g.Write8(kLevelNow, static_cast<uint8_t>(g.Read8(kLevelNow) + g.Read8(kLevelBonus)));
        if (static_cast<int8_t>(g.Read8(kLevelNow)) >= 7) {
            g.Write8(kLevelNow, 10);
        }
        const uint32_t row = g.Read8(kBossRank) != 0 ? 18 : Signed(s.Field(kCharacter));
        const uint32_t choices = kGuardChoices + Signed(static_cast<int16_t>(row * 0xB0)) +
                                 Signed(static_cast<int16_t>(static_cast<int8_t>(g.Read8(kLevelNow)) * 16));
        g.R(1) = kDiceNow;
        g.R(3) = kGuardChoices;
        g.R(4) = row;
        g.R(5) = s.me;
        if (static_cast<int8_t>(g.Read8(choices + Signed(static_cast<int8_t>(g.Read8(kDiceNow))))) == 3) {
            s.SetWord(kLeverNow, 0);
            g.R(0) = 0;
            g.R(2) = 0;
            g.SetT(false);
            g.Return(60);
            return true;
        }
        g.Call(kDice16);
        s.SetWord(kGuardType, static_cast<int8_t>(g.Read8(choices + g.R(0))));
        g.R(5) = 1;
        s.SetWord(kLeverNow, static_cast<int32_t>(LeverFor(g, s.me, 1)));
        s.SetWord(kLeverNow, s.Word(kLeverNow) | 2);
        s.SetWord(kGuardType, s.Word(kGuardType) | 0x8000);
        g.R(0) = g.Read16(s.At(kGuardType, 2));
        g.R(1) = 0x8000;
        g.R(2) = Signed(s.side * 2);
        g.R(3) = s.At(kGuardType, 2);
        g.R(4) = s.me;
        g.SetT(false);
        g.Return(80);
        return true;
    }

    // 0600FE80 (Reaction_Sub, r5 the reaction, r6 the power): what the plan does once its attack is out, by the
    // reaction's low seven bits, from whether the attack hit (kHitStock: 1-3 guarded or hit, 4 and up a hit that
    // counts) and whether the fighter is still in it (kDoing 4, its animation not 0x40):
    //    0-7  a hit: the follow-up (Setup_Follow, the reaction's low twelve bits); guarded: the next step; missed and
    //         over: the follow-up
    //    9    guarded or hit: the next step; missed and over: the plan ended (Next_End)
    //   10    a hit: ended; guarded: the next step; missed and over: the next step
    //   11    as 10 (missed and over: ended), but not while the fighter attacks or holds the other
    //   12, 14  the next step, as a counter attack (kCounter)
    //   13    a hit: ended; a target combo may follow (Check_Meoshi_Attack); over: guarded or hit, back to the plan
    //         broken off for an answer (mode 6, plan 0) or the next step; missed, ended
    //   others  not while attacking or holding: hit, guarded or over, the next step
    // (Each ends in a jump to the routine named, which leaves what the call leaves; or the registers as the game's
    // code does.)
    bool Reaction(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 24);
        const uint32_t reaction = Signed(static_cast<int16_t>(g.R(5)));
        const uint32_t power = Signed(static_cast<int16_t>(g.R(6)));
        const uint32_t kind = reaction & 127;
        const uint32_t fighter = kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize));
        g.R(4) = kHitStock;
        g.R(6) = kFighters;
        if (kind < 15) {
            g.R(1) = kind * 2;
            g.R(0) = Signed(static_cast<int16_t>(g.Read16(0x0600FEB8 + kind * 2)));
        } else {
            g.R(0) = kind;
            g.R(1) = 15;
        }
        const auto jump = [&](uint32_t to, uint32_t r5) {
            g.R(4) = s.me;
            g.R(5) = r5;
            g.Call(to);
            g.Return(60);
            return true;
        };
        const auto stay = [&]() {
            g.Return(60);
            return true;
        };
        const uint8_t hit = s.Byte(kHitStock);
        // The fighter still in its attack (kDoing 4 and its animation not 0x40): r0 left as the game's test leaves it
        const auto stillIn = [&]() {
            if (s.Field(kDoing) != 4) {
                g.R(0) = Signed(s.Field(kDoing));
                g.SetT(false);
                return false;
            }
            g.R(0) = g.Read8(s.me + kMoveKind);
            g.SetT(g.R(0) == 0x40);
            return g.R(0) != 0x40;
        };
        // Attacking or holding the other (caution_flag, tsukami_f): r0, r1 and r6 as the game's tests leave them
        const auto busy = [&]() {
            g.R(0) = fighter;
            g.R(1) = Signed(static_cast<int8_t>(g.Read8(fighter + kAttacking)));
            g.R(3) = kFighterSize;
            if (g.R(1) != 0) {
                g.SetT(false);
                return true;
            }
            g.R(6) = fighter;
            g.R(0) = Signed(static_cast<int8_t>(g.Read8(fighter + kHolding)));
            g.SetT(g.R(0) == 0);
            return g.R(0) != 0;
        };
        const auto counts = [&]() { // (a hit that counts: kHitStock 4 and up)
            g.R(0) = Signed(s.side);
            g.R(3) = static_cast<uint32_t>(hit >> 2);
            g.SetT(g.R(3) == 0);
            return g.R(3) != 0;
        };
        const auto anyHit = [&]() {
            g.R(0) = Signed(s.side);
            g.R(3) = hit;
            g.SetT(hit == 0);
            return hit != 0;
        };
        switch (kind) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: {
            const uint32_t followUp = reaction & 0xFFF;
            if (counts()) {
                return jump(kSetupFollow, followUp);
            }
            if (anyHit()) {
                return jump(kReactionExit, g.R(5));
            }
            if (stillIn()) {
                return stay();
            }
            return jump(kSetupFollow, followUp);
        }
        case 9:
            if (anyHit()) {
                return jump(kReactionExit, g.R(5));
            }
            if (stillIn()) {
                return stay();
            }
            return jump(kNextEnd, g.R(5));
        case 10:
            if (counts()) {
                return jump(kNextEnd, g.R(5));
            }
            if (anyHit() || !stillIn()) {
                return jump(kReactionExit, g.R(5));
            }
            return stay();
        case 11:
            if (busy()) {
                return stay();
            }
            if (counts()) {
                return jump(kNextEnd, g.R(5));
            }
            if (anyHit()) {
                return jump(kReactionExit, g.R(5));
            }
            if (stillIn()) {
                return stay();
            }
            return jump(kNextEnd, g.R(5));
        case 12:
        case 14:
            g.R(4) = s.me;
            g.Call(kReactionExit);
            s.SetByte(kCounter, 1);
            g.R(0) = Signed(s.side);
            g.R(2) = s.At(kCounter, 1);
            g.R(3) = 1;
            return stay();
        case 13:
            if (counts()) {
                return jump(kNextEnd, g.R(5));
            }
            g.R(0) = g.Read16(s.me + kPermitted);
            g.SetT((g.R(0) & 0x10) == 0);
            if (g.R(0) & 0x10) {
                g.R(4) = s.me;
                g.R(5) = reaction;
                g.R(6) = power;
                g.Call(kTargetCombo);
                if (Nonzero(g)) {
                    return stay();
                }
            }
            s.SetWord(kLastEffect, -1);
            g.R(2) = s.At(kLastEffect, 2);
            g.R(3) = 0xFFFFFFFF;
            if (stillIn()) {
                return stay();
            }
            g.R(0) = Signed(s.side);
            g.R(1) = Signed(static_cast<int8_t>(hit));
            if (hit == 0) {
                g.SetT(true);
                return jump(kNextEnd, g.R(5));
            }
            g.R(4) = kMode;
            g.R(5) = Signed(s.side);
            g.R(0) = Signed(s.Mode());
            if (s.Mode() != 6) {
                g.SetT(false);
                return jump(kReactionExit, g.R(5));
            }
            g.R(2) = Signed(s.side * 2);
            g.R(0) = kPattern;
            g.R(3) = Signed(s.Word(kPattern));
            if (s.Word(kPattern) != 0) {
                g.SetT(false);
                return jump(kReactionExit, g.R(5));
            }
            // (back to the plan an answer broke off)
            g.Write16(s.ModeAt(0), g.Read16(s.At(kReturnMode, 2)));
            s.SetPlace(0, s.Word(kReturnPlace));
            for (int n = 1; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            s.SetWord(kPattern, s.Word(kReturnPattern));
            g.R(0) = kPattern;
            g.R(1) = Signed(s.Word(kReturnPlace));
            g.R(2) = Signed(s.side * 2);
            g.R(3) = Signed(s.Word(kReturnPattern));
            g.R(4) = Signed(s.side * 2);
            g.R(5) = 0;
            g.SetT(false);
            return stay();
        default:
            if (busy()) {
                return stay();
            }
            if (anyHit() || !stillIn()) {
                return jump(kReactionExit, g.R(5));
            }
            return stay();
        }
    }

    // 060107A6 (Check_SA_Active, r5 where to put the answer): which kind of plan the super art under way calls for,
    // when the art's gauge is ready (its byte 10 -1): Oro 3 with art I, else 2; Yun and Yang 1 with art III (Genei
    // Jin); Q 4 with art III (the PlayStation 2 build has Makoto, 17, where this has Q, 18); 0 for the rest (and then
    // nothing is put).
    bool SuperArtPlan(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t answer = g.R(5);
        uint32_t plan = 0;
        if (static_cast<int8_t>(g.Read8(g.Read32(s.me + kSuperArt) + 10)) == -1) {
            const uint32_t fighter = kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize));
            const int16_t art = static_cast<int16_t>(g.Read16(g.Read32(fighter + kSuperArt)));
            switch (s.Byte(kChosen)) {
            case 9: plan = art == 0 ? 3 : 2; break;
            case 3:
            case 10: plan = art == 2 ? 1 : 0; break;
            case 18: plan = art == 2 ? 4 : 0; break;
            default: break;
            }
            if (plan != 0) {
                g.Write16(answer, static_cast<uint16_t>(plan));
            }
        }
        g.R(0) = plan;
        g.Return(40);
        return true;
    }

    // 06011414 (Check_Diagonal_Shell): is a projectile coming down at the computer from above (one of the other's, or
    // one of eight)? Looked for only as often as the level of 8 has it (a die of 64 against the level's chance): one
    // not being thrown back, facing the computer, not behind it (Check_Behind), of a kind other than 2, and coming
    // (Check_Ignore_Shell2: 1).
    bool ShellAbove(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 28);
        g.R(4) = 0;
        g.Call(kLevelOf8);
        g.Write8(kLevelNow, static_cast<uint8_t>(g.R(0)));
        if (g.Read8(kDemo) == 0 && g.Read8(kWeakSide) == static_cast<uint32_t>(s.side)) {
            g.Write8(kLevelNow, 2);
        }
        g.Call(kDice64);
        g.Write8(kDiceNow, static_cast<uint8_t>(g.R(0)));
        g.Write8(kLevelNow, static_cast<uint8_t>(g.Read8(kLevelNow) + g.Read8(kLevelBonus)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 7);
        }
        const auto result = [&](uint32_t r0) {
            g.R(0) = r0;
            g.Return(80);
            return true;
        };
        if (static_cast<int8_t>(g.Read8(kDiceNow)) > g.Read8(kShellAboveChances + Signed(static_cast<int8_t>(g.Read8(kLevelNow))))) {
            return result(0);
        }
        const uint32_t other = s.Other();
        const uint32_t local = g.R(15); // (where the routine keeps the projectile found)
        for (int16_t n = 0; n < 8; n++) {
            const auto find = [&](uint32_t whose) {
                g.R(4) = whose;
                g.R(5) = Signed(static_cast<int16_t>(g.Read16(other + kSide)));
                g.R(6) = Signed(n);
                g.R(7) = local;
                g.Call(kFindProjectile);
                return g.R(0) != 0;
            };
            if (!find(other) && !find(s.me)) {
                return result(0);
            }
            const uint32_t shell = g.Read32(local);
            if (static_cast<int16_t>(g.Read16(shell + kDoing)) == 2) {
                continue;
            }
            if (static_cast<int8_t>(g.Read8(s.me + kMovesFaceRight)) == static_cast<int8_t>(g.Read8(shell + kFacesRight))) {
                continue;
            }
            g.R(4) = s.me;
            g.R(5) = shell;
            g.Call(kBehind);
            if (g.R(0) != 0) {
                continue;
            }
            if (g.Read8(shell + kShellKind) == 2) {
                continue;
            }
            g.R(4) = shell;
            g.Call(kShellComing);
            if (g.R(0) != 0) {
                return result(1);
            }
        }
        return result(0);
    }

    // 0600F09E (Check_Dash_Hit, r5 the move): would a dash (move 0 forward, 1 back) run into a projectile? For each of
    // eight (the other's, or the computer's own list), not one being thrown back: on the side the dash goes
    // (Setup_Front_or_Back) and within the dash's reach (Check_Hit_Shell).
    bool DashHit(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 36);
        const uint32_t move = g.R(5) & 0xFFFF;
        const auto result = [&](uint32_t r0) {
            g.R(0) = r0;
            g.Return(80);
            return true;
        };
        if (move != 0 && move != 1) {
            return result(0);
        }
        const uint32_t other = s.Other();
        const uint32_t local = g.R(15);
        for (int16_t n = 0; n < 8; n++) {
            const auto find = [&](uint32_t whose) {
                g.R(4) = whose;
                g.R(5) = Signed(static_cast<int16_t>(g.Read16(other + kSide)));
                g.R(6) = Signed(n);
                g.R(7) = local;
                g.Call(kFindProjectile);
                return g.R(0) != 0;
            };
            if (!find(other) && !find(s.me)) {
                return result(0);
            }
            const uint32_t shell = g.Read32(local);
            if (static_cast<int16_t>(g.Read16(shell + kDoing)) == 2) {
                continue;
            }
            g.R(4) = s.me;
            g.R(5) = Signed(static_cast<int16_t>(s.Field(kX) - static_cast<int16_t>(g.Read16(shell + kX))));
            g.Call(kFrontOrBack);
            const int16_t side = static_cast<int16_t>(g.R(0));
            if (move == 0 ? side == 1 : side == 0) {
                continue;
            }
            g.R(4) = s.me;
            g.R(5) = shell;
            g.R(6) = move;
            g.Call(kHitsShell);
            if (g.R(0) != 0) {
                return result(1);
            }
        }
        return result(0);
    }

    // 0600F254 (Command_Type_00, r5 the power, r6 the move, r7 Ex_Shot): the command's next step, a frame's lever. A
    // command is a list of words, four a step from kTechIndex (12 at the start): the step's lever (its fourth word, its
    // left and right as the moves face), the next step's kind fifth (28: the last). Not the last: the lever (packed as
    // the controls have it) and 1. The last: its lever with the button the power picks (or an EX move's two buttons,
    // when the gauge allows), and 0, the attack's phase moved on; -1 when the command is all put in (its eighth word
    // 0x80). Ryu's Denjin Hadoken (0x8015, but at power 8) holds its buttons instead (kLeverNow) for its charge; in mode
    // 10 the buttons are kept for a mash; Hugo's 0x8016 goes to phase 5 (the run); Ibuki's 31 ends at once.
    bool CommandStep0(Guest &g) {
        Side s(g, g.R(4));
        Frame frame(g, 44);
        const int16_t power = static_cast<int16_t>(g.R(5));
        const uint32_t move = g.R(6) & 0xFFFF;
        const int16_t exShot = static_cast<int16_t>(g.R(7));
        const uint32_t address = g.Read32(s.At(kTechAddress, 4));
        const uint32_t step = address + (Signed(s.Word(kTechIndex)) << 1);
        const auto flipped = [&]() { // (left and right as the moves face)
            if (g.Read8(s.me + kMovesFaceRight) != 0 && (g.Read16(s.At(kLever, 2)) & 12) != 0) {
                s.SetWord(kLever, s.Word(kLever) ^ 12);
            }
        };
        const auto result = [&](uint32_t r0) {
            g.R(0) = r0;
            g.Return(80);
            return true;
        };
        s.SetWord(kLever, g.Read16(step + 6) & 0x7FFF);
        if (g.Read16(step + 8) != 28) {
            g.R(4) = g.Read16(s.At(kLever, 2));
            g.Call(kPackCommand);
            s.SetWord(kLever, static_cast<int32_t>(g.R(0)));
            flipped();
            s.SetWord(kTechIndex, s.Word(kTechIndex) + 4);
            return result(1);
        }
        flipped();
        if (g.Read16(address + 14) == 0x80) {
            return result(0xFFFFFFFF);
        }
        s.SetWord(kTechIndex, 7);
        const uint32_t fighter = kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize));
        if (g.Read8(g.Read32(fighter + kSuperArt) + 12) != 0 && (exShot == 0x70 || exShot == 0x380)) {
            s.SetWord(kLever, s.Word(kLever) | exShot);
        } else {
            g.R(4) = address;
            g.R(5) = Signed(power);
            g.Call(kButtonForPower);
            s.SetWord(kLever, s.Word(kLever) | static_cast<int16_t>(g.R(0)));
        }
        if (s.Byte(kChosen) == 2 && move == 0x8015 && power != 8) {
            s.SetPlace(0, s.Place(0) + 1);
            s.SetWord(kLeverNow, s.Word(kLever) & 0x3F0);
            s.SetPlace(1, power == 10 ? 1 : 0);
            return result(0);
        }
        if (s.Mode() == 10) {
            s.SetWord(kRapidButtons, s.Word(kLever) & 0x3F0);
            s.SetWord(kJumpLever, s.Word(kLever) & 0x3F0);
        }
        if (s.Field(kCharacter) == 6 && move == 0x8016) {
            s.SetPlace(1, 5);
        } else if (s.Field(kCharacter) == 7 && move == 31) {
            g.R(4) = s.me;
            g.Call(kReactionExit);
        } else {
            s.SetPlace(1, s.Place(1) + 1);
        }
        return result(0);
    }

    // (free: r1-r7 and T for a routine with a result, r0-r7 and T for one with none, where C callers only call it)
    constexpr uint32_t kResult = 0x100FE, kNoResult = 0x100FF;
    // (The entries commented out are rewrites the check has not yet met, so not registered: ETC_Term_0004 (Ken's),
    // Turn_Over_On (Hugo's and Akuma's), Only_Shot (Akuma's), and six of the character routines, whose code is the
    // others' byte for byte but for their tables.)
    const Lift kLifts[] = {
        {"ComputerDice32", 0x0611E106, 24, 0x93BD163910B52512ull, Dice32, true, 0x108FE},
        {"ComputerTableByLowBits", 0x061210DC, 16, 0x065C5F1E0ADD13DDull, TableByLowBits, true},
        {"ComputerPackCommand", 0x061210EC, 20, 0xA7D2E1B3C10A406Eull, PackCommand, true},
        // {"ComputerWellAhead", 0x06012338, 0x32, 0xF05408CEA3803DC1ull, WellAhead, true, kResult},
        {"ComputerAhead", 0x0601236A, 0x24, 0xC24BBD769E324DB9ull, Ahead, true, kResult},
        {"ComputerJumpGap", 0x0600FD36, 0x48, 0x9FFA7E9EB96C3068ull, JumpGapLift, true},
        {"ComputerStartJump", 0x0600F1DC, 0x68, 0x86B7953CFBC0F80Eull, StartJump, true, 0x100FF},
        {"ComputerLeverFor", 0x06009FF4, 0x52, 0x8703FF0AF7C756ECull, LeverForLift, true},
        {"ComputerJumpAttackLever", 0x0600C164, 0x32, 0x0AA3870DCDB14671ull, JumpAttackLever, true, kResult},
        {"ComputerMeetsLimit", 0x0600B4AC, 0x32, 0x05D510F362DD9032ull, MeetsLimitLift, true, kResult},
        {"ComputerHeightMeetsLimit", 0x0600CB34, 0x3E, 0xDA24C846E94761C9ull, HeightMeetsLimit, true, kResult},
        {"ComputerJumpAttackTime", 0x0600CABC, 0x4E, 0x37D03C5B414958DEull, JumpAttackTime, true, kResult},
        {"ComputerOtherUp", 0x0600AEA2, 0x6E, 0xB3D33F1DCAAC40E7ull, OtherUp, true, kResult},
        {"ComputerLeverAttackBusy", 0x0600A046, 0x38, 0x1379569F1C19B12Aull, LeverAttackBusy, true, kResult},
        {"ComputerAttackBusy", 0x06009D4C, 0x4C, 0x2F1348DA6C8EBA32ull, AttackBusy, true, kResult},
        {"ComputerBeFree", 0x06008706, 0x48, 0x37378FA8F43CA43Aull, BeFreeLift, true},
        {"ComputerEndPattern", 0x06008C24, 6, 0xA998A4FA22CA6249ull, EndPattern, true},
        {"ComputerAnotherPlan", 0x0600FE10, 0x5E, 0xB8126AE1FF220605ull, AnotherPlanLift, true},
        // {"ComputerTurnOverOn", 0x06008C30, 0x26, 0xF22438774BD2F592ull, TurnOverOn, true},
        // {"ComputerOnlyShot", 0x06008C56, 0x1E, 0x7EC43C25FC75890Bull, OnlyShot, true},
        {"ComputerLeverOn", 0x06008C74, 0x84, 0x58BD8CF6305C2C26ull, LeverOn, true},
        {"ComputerLeverOff", 0x06008CF8, 0x2A, 0x92E1409D6A2588ACull, LeverOff, true},
        {"ComputerPierceOn", 0x06008D22, 0x34, 0xBBF29EF54323A291ull, PierceOn, true},
        {"ComputerWait", 0x0600A88A, 0xD6, 0x423665BC6EA4CE26ull, Wait, true},
        {"ComputerLook", 0x0600A960, 0x10C, 0x3B06F550D88207D9ull, Look, true},
        {"ComputerWalk", 0x060095E0, 0x166, 0x36F1A31B352AD188ull, Walk, true},
        {"ComputerNormalAttack", 0x060098C6, 0x16C, 0xE44D0F4E378ABA6Bull, NormalAttack, true},
        {"ComputerSmallJump", 0x06009A32, 0x16, 0x5A00EAD2E463E676ull, SmallJumpLift, true},
        {"ComputerAdjustAttack", 0x06009BD4, 0x162, 0x144BEC670CD3545Bull, AdjustAttack, true},
        {"ComputerLeverAttack", 0x06009D98, 0xFE, 0x243219E6E17ED363ull, LeverAttack, true},
        {"ComputerPlanByRange", 0x0600A67C, 0x62, 0xC68F95227F8693C1ull, PlanByRange, true},
        // {"ComputerPlaysGill", 0x06064744, 24, 0x2EEFB2A95139601Dull, PlayPlan, true},
        {"ComputerPlaysAlex", 0x06065794, 24, 0xBD311631115BDCFFull, PlayPlan, true},
        {"ComputerPlaysRyu", 0x06066874, 24, 0x64C1EC615A3C9C25ull, PlayPlan, true},
        {"ComputerPlaysYun", 0x06067C00, 24, 0xB8518537830609ADull, PlayPlan, true},
        {"ComputerPlaysDudley", 0x06068FC0, 24, 0x9F8D0CAF83107343ull, PlayPlan, true},
        {"ComputerPlaysNecro", 0x0606A110, 24, 0x96A90080F0885AF7ull, PlayPlan, true},
        // {"ComputerPlaysHugo", 0x0606B0DC, 24, 0x0712C00628F0F54Dull, PlayPlan, true},
        {"ComputerPlaysIbuki", 0x0606C024, 24, 0x98E1D7A7786EF4A5ull, PlayPlan, true},
        {"ComputerPlaysElena", 0x0606D7D0, 24, 0x68CC514D564FB49Dull, PlayPlan, true},
        {"ComputerPlaysOro", 0x0606EBFC, 24, 0xE6EFA9E72DEC69F5ull, PlayPlan, true},
        {"ComputerPlaysYang", 0x06070A18, 24, 0xB8518537830609ADull, PlayPlan, true},
        {"ComputerPlaysKen", 0x06071E38, 24, 0x3C82384568864D6Dull, PlayPlan, true},
        {"ComputerPlaysSean", 0x06073664, 24, 0xBD311631115BDCFFull, PlayPlan, true},
        // {"ComputerPlaysUrien", 0x06074D5C, 24, 0x2EEFB2A95139601Dull, PlayPlan, true},
        // {"ComputerPlaysAkuma", 0x06076180, 24, 0x3435DB0CD6D2A583ull, PlayPlan, true},
        // {"ComputerPlaysShinAkuma", 0x06079CA4, 24, 0x3435DB0CD6D2A583ull, PlayPlan, true},
        {"ComputerPlaysChunLi", 0x0607D708, 24, 0x64C1EC615A3C9C25ull, PlayPlan, true},
        {"ComputerPlaysMakoto", 0x0607E780, 24, 0xBD311631115BDCFFull, PlayPlan, true},
        // {"ComputerPlaysQ", 0x0607F8B4, 24, 0xBD311631115BDCFFull, PlayPlan, true},
        {"ComputerPlaysTwelve", 0x06080FC8, 24, 0x64C1EC615A3C9C25ull, PlayPlan, true},
        {"ComputerPlaysRemy", 0x06082B24, 24, 0x96A90080F0885AF7ull, PlayPlan, true},
        {"ComputerCommandAttack", 0x0600D390, 0x44E, 0xF8AE541593210A84ull, CommandAttack, true, kResult},
        {"ComputerJumpCommandAttack", 0x0600D874, 0x2C2, 0x1D7118BB9D29E502ull, JumpCommandAttack, true, kResult},
        {"ComputerComboSpeed", 0x0600F87E, 0x9C, 0xDAAFA4EBB8A55DE8ull, ComboSpeed, true, kResult},
        {"ComputerLookTime", 0x0600FB74, 0x56, 0x9CCB6F9EDBE2687Eull, LookTime, true, kResult},
        {"ComputerWaitTime", 0x0600FBCA, 0x56, 0xEBCCAD1C24113E86ull, WaitTime, true, kResult},
        {"ComputerFirstMenu", 0x0601047C, 0x38, 0x7F78EF7925DEA5AFull, FirstMenuLift, true},
        {"ComputerCommandBusy", 0x0600E108, 0xA2, 0xE32B87BB9B1E8834ull, CommandBusy, true, kResult},
        {"ComputerLeverNotBack", 0x0600D7DE, 0x7C, 0x08C09C8C296940A6ull, LeverNotBack, true, kResult},
        {"ComputerRapidEnd", 0x0600DE3C, 0x3A, 0x4530F836DF66532Cull, RapidEndLift, true, kNoResult},
        {"ComputerCheckRapid", 0x0600DDB8, 0x84, 0x40D4B1BDC544B8B8ull, CheckRapid, true, kNoResult},
        {"ComputerRapidOver", 0x0600E014, 0xF4, 0xEE0D4166CED3020Dull, RapidOverLift, true, kResult},
        {"ComputerMash", 0x0600DEA4, 0x170, 0x54D345AC75946B4Cull, Mash, true, kNoResult},
        {"ComputerReactionExit", 0x06010350, 0x74, 0xE8B3CB5942CCA38Dull, ReactionExitLift, true},
        {"ComputerNextEnd", 0x0600FD7E, 0x54, 0xB76A0648647012D7ull, NextEnd, true},
        {"ComputerLanded", 0x0600EFB8, 0xE6, 0x9D87B572165F5B50ull, Landed, true},
        {"ComputerJumpAttack", 0x0600BE70, 0x2F4, 0x85DCA9B327DD7664ull, JumpAttack, true, kResult},
        {"ComputerSetLeverNow", 0x06011B76, 0x9A, 0x95BDC4B28E3257E5ull, SetLeverNow, true, 0x1007F},
        {"ComputerClearHighJump", 0x060B5CD6, 0x24, 0x373A27F2CD6C0C12ull, ClearHighJump, true, 0x14},
        {"ComputerVsAirAttack", 0x0600C196, 0x166, 0x962089A960C3998Eull, VsAirAttack, true, kResult},
        {"ComputerAirGuard", 0x0600B974, 0x194, 0x3DE0946FB87F7C6Aull, AirGuard, true},
        {"ComputerReaction", 0x0600FE80, 0x350, 0xC63031721D36CBC3ull, Reaction, true},
        {"ComputerSuperArtPlan", 0x060107A6, 0xDA, 0xFA2BAF1CDF7E85DAull, SuperArtPlan, true, kResult},
        {"ComputerShellAbove", 0x06011414, 0xEC, 0x25088C3C3F1A7949ull, ShellAbove, true, kResult},
        {"ComputerDashHit", 0x0600F09E, 0xBC, 0xC30C1AC705D2E0D0ull, DashHit, true, kResult},
        {"ComputerAirGapMeetsLimit", 0x0600B466, 0x46, 0x374E3AFDD96D240Bull, AirGapMeetsLimit, true, kResult},
        {"ComputerOtherHeightMeetsLimit", 0x0600B4F0, 0x68, 0xB8E0B8612B503BA3ull, OtherHeightMeetsLimit, true, kResult},
        {"ComputerCommandStep0", 0x0600F254, 0x244, 0xE2267F4FE03BE50Aull, CommandStep0, true, kResult},
        {"ComputerRandomPlan", 0x0600A708, 0x92, 0xD3F8F075B257F2A6ull, RandomPlan, true, kResult},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerMoves() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
