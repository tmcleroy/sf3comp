// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's computer player, written again as C++ (rewrites: runtime/lift.h): the computer's tactics: the modes in which it defends and is hit (7-13), the plan's mode (2), the choices of a plan, an answer and a reaction, and steps of the plans.
// The computer's design, its record's fields and the other parts: computer.h.
//
// How the computer chooses. In mode 1 (free) a plan is chosen (Select_Active): from the character's table for the
// range the gap is in, by the level of eight (how far the game has got) and a dice; a super art's plan when the art
// calls for it (Check_SA_Active). Through a plan, and as a hit or a guard ends, the computer looks for an answer to
// the other (Check_Passive): the other knocked into the air (wait for it to fall, mode 14), backing off, stunned or
// lying down, a projectile coming (mode 8, a plan by when it arrives and its kind, or a guard or a parry), or what
// the other's attack or movement calls for (computer_answers.cpp). An answer is a number (kAnswer) and its reaction a
// plan (Select_Passive): from the character's table by the range, the answer, the level and a column at random (the
// second half of the columns when its super art is ready or either fighter is low on health); a wait (by the level)
// before it, in mode 5, then mode 6 plays it.
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
// Each first looks at what has happened to the fighter (computer_entry.cpp's CheckHit, CheckCaught, CheckParry,
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
// called if there is one; this file's own routines it calls as C++ where nothing reads the registers after. The
// steps of the plans (Com_Sub, reached by tail jumps from the plans: computer_moves.cpp) leave every register as the
// game's code does, but where liveout.py found their callers read none, and the stack pointer what they call sees is
// the game's (Frame).
//
// Checked in layers (checklifts.sh): while a rewrite is compared what it calls runs as the game's own code, so the
// inner ones were met with only themselves registered (SFIII3_LIFTS_ONLY). The modes and steps the fights reach
// seldom (in the air after a hit, getting up with move 46, the rarer steps of a parry, Ryu's Denjin plans) are met
// in its training runs with the computer's mode set by hand.
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
    constexpr uint32_t kWasUp = 0x72;         // word: its height the frame before (old_pos[1])
    constexpr uint32_t kRising = 0x80;        // word: its speed upward (mvxy.a[1]): below 0 coming down
    constexpr uint32_t kHitByByte = 0x312;    // byte (hf.hit.player)
    constexpr uint32_t kHealth = 0x9E;        // word (vital_new); in a projectile's record, its strength
    constexpr uint32_t kPose = 0x20E;         // byte: its pose (pat_status): 14-30 crouching, 32 and 33 low
    constexpr uint32_t kAttackKind = 0x20F;   // byte: the kind of its attack (kind_of_waza): the high five bits the kind
                                              // (0 a normal), the low ones its strength
    constexpr uint32_t kMoveId = 0x213;       // byte: the move (sp_tech_id): for a throw, 1 one that can be broken
    constexpr uint32_t kMoveKind = 0x215;     // byte: the kind of its animation (cg_type): 9 and 12 getting up, 64 the
                                              // attack's end
    constexpr uint32_t kCancel = 0x221;       // byte: what its attack may be cancelled into now (cg_cancel): 4 a normal, 1
                                              // a high jump (computer_moves.cpp has the rest)
    constexpr uint32_t kGuardBits = 0x2E1;    // byte: how its attack is guarded (att.guard): 8 low, 16 high
    constexpr uint32_t kShellKind = 0x05;     // in a projectile's record, a byte (charset_id): 2 not one to guard
    constexpr uint32_t kAttackReach = 0x210;  // byte: how far its attack reaches, a class (hit_range)
    constexpr uint32_t kHitOk = 0x2FE;        // byte: its attack can hit now (att_hit_ok)
    constexpr uint32_t kStuck = 0x3C9;        // byte (micchaku_flag): pressed against the other
    constexpr uint32_t kPushed = 0x3CB;       // byte (hos_em_flag)
    constexpr uint32_t kCharacter = 0x3C0;    // word: the character (player_number); byte 0x3C1 its low byte
    constexpr uint32_t kSuperArt = 0x3F0;     // long: the super art's state (sa): word +0 the art (0-2), byte +10 not 0
                                              // while it may be used
    constexpr uint32_t kPy = 0x3F8;           // long (py): its byte +0 set: the fighter is to stand up at once
    constexpr uint32_t kAttackingByte = 0x428; // byte: not 0 while it attacks
    constexpr uint32_t kFighterSize = 0x498;
    constexpr uint32_t kFighters = 0x02068C6C; // the fighters' records (plw)

    // --- the computer's tables: two of each, by kSide ---
    constexpr uint32_t kMode = 0x02015572;        // words, four a side (CP_No): the mode, its step, two more words
    constexpr uint32_t kPlanPlace = 0x02015582;   // words, eight a side (CP_Index)
    constexpr uint32_t kPattern = 0x020155A2;     // word: the plan chosen (Pattern_Index)
    constexpr uint32_t kTimer = 0x020155A6;       // word: frames to wait (Timer_00)
    constexpr uint32_t kTimerReload = 0x020155AA; // word: a second timer, a mash's speed (Timer_01): 8 the hit was a
                                                  // projectile's
    constexpr uint32_t kGap = 0x020155B2;         // word: the gap between the two along the floor (PL_Distance)
    constexpr uint32_t kRange = 0x020155B6;       // word: which range the gap is in, 0 close - 3 far (Area_Number)
    constexpr uint32_t kLever = 0x020155BA;       // word: the lever and buttons chosen this frame (Lever_Buff)
    constexpr uint32_t kJumpLever = 0x020155BE;   // word: a lever kept to hold again (Lever_Pool)
    constexpr uint32_t kAnswer = 0x020155F8;      // word: the answer chosen (VS_Tech)
    constexpr uint32_t kGuardType = 0x020155FE;   // word: the guard: 0 as the attack needs, 1 standing, 2 crouching (Guard_Type)
    constexpr uint32_t kShell = 0x02015610;       // long: the projectile the computer is against (Shell_Address)
    constexpr uint32_t kAnswering = 0x0201561C;   // byte (Passive_Flag)
    constexpr uint32_t kParryFlag = 0x0201561E;   // byte (Flip_Flag): a parry under way
    constexpr uint32_t kFreeLever = 0x02015618;   // word: the lever Keep_Status holds (Free_Lever)
    constexpr uint32_t kWidth = 0x02015632;       // word: the computer's character's width (Com_Width_Data)
    constexpr uint32_t kGuarding = 0x0201563B;    // byte (Guard_Flag): guarding through a plan's wait
    constexpr uint32_t kDenjinTest = 0x02015834;  // word: Ryu's Denjin: the test for after its charge
    constexpr uint32_t kDenjinPlan = 0x02015838;  // byte: ... and the plan
    constexpr uint32_t kHeldDown = 0x0201563E;    // word: the lever held down, kept (Lever_Squat)
    constexpr uint32_t kOtherLying = 0x02015624;  // byte: the other is lying on the floor (Lie_Flag)
    constexpr uint32_t kComboSpeed = 0x020155AE;  // byte: frames before the attack's button or the jump (Combo_Speed)
    constexpr uint32_t kGetUpSeen = 0x02015663;   // byte: cleared as Wait_Get_Up begins
    constexpr uint32_t kTermTimer = 0x02015626;   // word: cleared as EM_Term begins
    constexpr uint32_t kLimited = 0x0201562D;     // byte (Limited_Flag)
    constexpr uint32_t kRunAgain = 0x020157CC;    // byte: on to the next step at once (Disposal_Again)
    constexpr uint32_t kLeverNow = 0x0201582C;    // word: the lever to hold while nothing else is chosen (Lever_LR)
    constexpr uint32_t kShellIgnore = 0x0201562F; // byte (Shell_Ignore_Timer): frames projectiles are let be
    constexpr uint32_t kOtherAttacking = 0x0201562A; // byte: the other's attack is out (Attack_Flag)
    constexpr uint32_t kCounter = 0x02015648;     // byte (Counter_Attack): bit 1 an answer not to be broken off
    constexpr uint32_t kPierce = 0x02015646;      // byte (Pierce_Menu): the plan goes through, unanswered
    constexpr uint32_t kStanding = 0x02015740;    // byte: frames the other has stood (Standing_Timer)
    constexpr uint32_t kShellRanges = 0x02015744; // words, three a side: the ranges against a projectile
    constexpr uint32_t kAttacksSeen = 0x02015750; // word: the other's attacks counted (Attack_Counter)
    constexpr uint32_t kLastAttackSeen = 0x02015754; // word: kAttacksSeen when last answered (Last_Attack_Counter)
    constexpr uint32_t kJumpTimers = 0x020157C0;  // bytes, four a side, by range (Jump_Pass_Timer): frames the range's
                                                  // answers are let be
    constexpr uint32_t kLastLevers = 0x0201576E;  // words, twenty a side: the levers of the last twenty frames (Resume_Lever)
    constexpr uint32_t kHitSeen = 0x020157CA;     // byte (Receive_Flag): the computer is to hold down as it is hit
    constexpr uint32_t kOtherAttackKind = 0x02015829; // byte: the kind of the other's last attack (Type_of_Attack)
    constexpr uint32_t kAttackKinds = 0x0201583E; // bytes, four a side: the kinds of the other's last four attacks
    constexpr uint32_t kGuardedAttack = 0x02015848; // word: kAttacksSeen when the guard was last chosen (Guard_Counter)
    constexpr uint32_t kParries = 0x02016B85;     // byte: parries in a row (Flip_Counter); 0xFF: no more now

    // --- the game's, for everyone ---
    constexpr uint32_t kAnswered = 0x0200DCF8;    // byte: an answer was chosen this time (PASSIVE_X)
    constexpr uint32_t kPlayTime = 0x02011372;    // word: how far the game has got (Control_Time): the levels are by it
    constexpr uint32_t kControls1 = 0x0206AA8C, kControls2 = 0x0206AA90; // words: the players' levers and buttons
    constexpr uint32_t kPassiveMode = 0x0201562C; // byte: 4 the first look of an answer, 0 a later one (Passive_Mode)
    constexpr uint32_t kRank = 0x0201584C;        // byte: which set of level tables
    constexpr uint32_t kLevelBonus = 0x0201584D;  // byte: added to some levels (CC_Value)
    constexpr uint32_t kDemo = 0x020154AF;        // byte: not 0 for attract mode's fights (Demo_Flag)
    constexpr uint32_t kWeakSide = 0x02016AD6;    // byte: the side that plays at the lowest level, outside a demo (Weak_PL)
    constexpr uint32_t kBrokenInto = 0x02016B00;  // byte: 2: a challenger broke in: the computer at its best (Break_Into_CPU)
    constexpr uint32_t kBossRank = 0x02016B33;    // byte: not 0: the boss's tables (row 18), not the character's (EM_Rank)
    constexpr uint32_t kLevelNow = 0x0200DCF4;    // byte: the level just worked out (Lv)
    constexpr uint32_t kDiceNow = 0x0200DCF5;     // byte: the random number just taken (Rnd)
    constexpr uint32_t kScreen = 0x02026C90;      // the screen's record: words +102 and +106 its edges' x
    constexpr uint32_t kScreenMargin = 0x02026BD4; // word: kept from the screen's edges
    constexpr uint32_t kFullHealth = 0x02016B30;  // word: a fighter's full health
    constexpr uint32_t kWins = 0x02011382;        // words, a side: rounds won
    constexpr uint32_t kClock = 0x02011376;       // word: the round's clock
    constexpr uint32_t kChosen = 0x02011387;      // bytes, a side: the character played (My_char)
    constexpr uint32_t kHumanSide = 0x02015503;   // byte: a side, whose kChosen is looked at
    constexpr uint32_t kThrowLevel = 0x0206AC63;  // byte: the row of the level of 18 for breaking throws
    constexpr int kOroOrShin = 15, kMakoto = 17, kTwelve = 19; // characters (kCharacter)

    // --- tables in the program ---
    // The level tables (Level_04/10/18_Data): rows of a word each level's play time ends at, a set for each kRank
    constexpr uint32_t kLevels4 = 0x061753D4, kLevels10 = 0x06175464, kLevels18 = 0x0617549A;
    constexpr uint32_t kCounterChances = 0x061771BC; // bytes, by level of 8 (0600FB06)
    constexpr uint32_t kReactionTimes = 0x06171F54; // bytes: [character played: 640, 0x2D00 a challenger's][level of 20]
                                                  // [32 at random]: frames before a reaction (Reflection_Time_Data)
    // The plans of mode 2 by the range (Select_Active): bytes [character: 256][level of 8: 32][32 at random]
    constexpr uint32_t kPlans[4] = {0x0613FF54, 0x06141454, 0x06142954, 0x06143E54};
    // ... and for a super art: bytes [kind - 1: 128][level of 4: 32][32 at random]
    constexpr uint32_t kSuperArtPlans[4] = {0x06145354, 0x06145554, 0x06145754, 0x06145954};
    // The reactions by the range (Select_Passive): bytes [character: 2048, 1024 for the last][answer: 64][level: 16]
    // [column: 16], 0xFF none
    constexpr uint32_t kReactions[4] = {0x061495B8, 0x06153DB8, 0x0615E5B8, 0x06168DB8};
    constexpr uint32_t kShellAnswers = 0x06176D2A; // bytes: [character or 18: 10][level of 10]: the chance of answering
                                                  // a projectile
    constexpr uint32_t kShellPlans = 0x061877B8;  // bytes: [character: 512][kind: 128][level of 8: 16][16 at random]:
                                                  // the plan of mode 8
    constexpr uint32_t kShellLimits = 0x06178D1C; // bytes, a character: frames within which a projectile is parried or
                                                  // guarded, not answered
    constexpr uint32_t kShellKinds = 0x06178BA7;  // bytes, by a projectile's byte 4: its kind for kShellPlans, 0xFF none
    constexpr uint32_t kJumpLengths = 0x06178E6E; // words, a character: how far its jump goes back
    constexpr uint32_t kYagyouPlans = 0x0613E74C; // bytes, 16 at random: Oro's Yagyou Dama's plans, less 100
    constexpr uint32_t kRapidButtons = 0x0201573C; // word: what is pressed at each mash (Rapid_Index)
    constexpr uint32_t kTechAddress = 0x020155C4; // long: the command being put in (Tech_Address; computer_moves.cpp)
    constexpr uint32_t kTechIndex = 0x020155CC;   // word: where in it (Tech_Index)
    constexpr uint32_t kCommands = 0x020062E4;    // longs in RAM, a character each: its commands (player_cmd), a long each
    constexpr uint32_t kHitStock = 0x02015661;    // byte: the record's kHitByByte, kept as the attack is put in (Stock_Hit_Flag)
    constexpr uint32_t kDenjinLevels = 0x061771C4; // bytes: [range: 16][16 at random]: Ryu's plan after the Denjin's charge
    constexpr uint32_t kAnswerable = 0x0613E858;  // bytes, by the other's kDoing: not 0 when what it does may be answered
    constexpr uint32_t kActives = 0x0613E544;     // 21 longs: each character's routine for mode 2 (Computer00-20)
    constexpr uint32_t kVsShells = 0x0613E640;    // 21 longs: each character's routine for mode 8 (Shell00 ...)
    constexpr uint32_t kLastPlans = 0x0613E6F5;   // bytes, four a character: the last plan of each kind (Pattern_Insurance)
    constexpr uint32_t kReachByClass = 0x061773DE; // words, by an attack's reach class (the record's byte 0x210):
                                                   // how far it reaches (Hit_Range_Data)
    constexpr uint32_t kKnockedDown = 0x061773C2; // bytes, by kDoingHow: 2 thrown into the air, not 0 knocked down
                                                  // (PL_Blow_Off_Data)
    constexpr uint32_t kCanParry = 0x0617735E;    // bytes, by kDoingHow: not 0 in a parry's action (PL_Damage_Data)
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
    constexpr uint32_t kCheckParry = 0x06007F64;  // Check_Flip (r4): a parry begun: mode 12
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
    constexpr uint32_t kDice32 = 0x0611E106;      // random_32_ex_com: the next of a table of 32 (computer_moves.cpp)
    constexpr uint32_t kSuperArtPlan = 0x060107A6; // Check_SA_Active (r4, r5 where to put the kind): the super art's
                                                  // plan's kind, not 0 for one (computer_moves.cpp)
    constexpr uint32_t kCheckGuard = 0x06010F4C;  // Check_Guard (r4): a guard has begun (computer_entry.cpp)
    constexpr uint32_t kOtherFalling = 0x060138E0; // Check_Blow_Off (r4, r5 the other, r6 an answer)
    constexpr uint32_t kShellCheck = 0x0601156A;  // (r4): a projectile coming: mode 8 (not 0)
    constexpr uint32_t kClearHighJump = 0x060B5CD6; // hi_jump_flag_clear (r4 the side; computer_moves.cpp)
    constexpr uint32_t kFirstMenu = 0x0601047C;   // Check_First_Menu (r4): kComboSpeed set (computer_moves.cpp)
    constexpr uint32_t kStartJump = 0x0600F1DC;   // Jump_Init (r4, r5 the way): the jump's lever (computer_moves.cpp)
    constexpr uint32_t kShellAbove = 0x06011414;  // Check_Diagonal_Shell (r4): a projectile from above (computer_moves.cpp)
    constexpr uint32_t kAirGuard = 0x0600B974;    // Check_Air_Guard (r4) (computer_moves.cpp)
    constexpr uint32_t kOtherUp = 0x0600AEA2;     // Check_Wait_Term (r4, r5 what): the other has got up (computer_moves.cpp)
    constexpr uint32_t kReactionExit = 0x06010350; // Reaction_Exit_Sub (r4): the plan on (computer_moves.cpp)
    constexpr uint32_t kSpecialTechnique = 0x06012EA0; // Check_Special_Technique (r4, r5 the other, r6, r7, three longs
                                                  // on the stack; computer_answers.cpp)
    constexpr uint32_t kTests = 0x0613E864;       // longs: the tests of the fight, by number (0x06011C10)
    constexpr uint32_t kEtcTests = 0x0613E888;    // ... and from the tenth on, ETC_Term's
    constexpr uint32_t kCommand1 = 0x0600F574;    // Command_Type_01 (r4, r5 the power, r6 Ex_Shot): a command of kind 2
                                                  // put in, not 0 once in (computer_moves.cpp)
    constexpr uint32_t kCheckRapid = 0x0600DDB8;  // Check_Rapid (r4, r5 the move): the mash after it (computer_moves.cpp)
    constexpr uint32_t kMashSub = 0x0600DEA4;     // Rapid_Sub (r4): that mash (computer_moves.cpp)
    constexpr uint32_t kCommandStep0 = 0x0600F254; // Command_Type_00 (r4, r5, r6, r7): a step of the command put in, -1
                                                  // once it is all in (computer_moves.cpp)
    constexpr uint32_t kReaction = 0x0600FE80;    // Reaction_Sub (r4, r5 the reaction, r6 the power): after an attack
                                                  // (computer_moves.cpp)
    constexpr uint32_t kDivideWords = 0x0612D584; // the compiler's division of words: r1 by r0, in r0 (called: it leaves
                                                  // the CPU's division flags as it does)
    constexpr uint32_t kJump = 0x0600B558;        // Jump (r4, r5 the way)
    constexpr uint32_t kTermTest = 0x06011C10;    // (r4, r5 the other, r6 the test): a test of the fight by its number
                                                  // (the table at 0x0613E864): 0 met, 1 the other plan, 2 not yet, 3 and up
                                                  // a reaction
    constexpr uint32_t kMeetsLimit = 0x0600B4AC;  // Check_Term_Sub (r4, r5 a value, r6 its limit; computer_moves.cpp)
    constexpr uint32_t kOtherHeightMeetsLimit = 0x0600B4F0; // Check_Term_Sub_Y (r4, r5 a height, r6 its limit)
    constexpr uint32_t kAnotherPlan = 0x0600FE10; // Next_Another_Menu (r4, r5 the mode, r6 the plan; computer_moves.cpp)
    constexpr uint32_t kSuperArtRange = 0x0600A422; // SA_Range_Check (r4, r5 which, r6 the test): not 0 out of range
    constexpr uint32_t kDenjinCheck = 0x0600A3B0; // DENJIN_Check (r4, r5, r6 the plan's place, r7 the test)
    constexpr uint32_t kYagyouCheck = 0x0600A3E2; // YAGYOU_Check (r4, r5 the plan's place, r6 the test)
    constexpr uint32_t kFindShell = 0x060DBB8A;   // get_vs_shell_adrs (r4 a fighter, r5 the side, r6 which, r7 where
                                                  // to put it): not 0 when there is one
    constexpr uint32_t kShellFront = 0x0608EAD6;  // (r4 a projectile): where its front is along the floor
    constexpr uint32_t kDivideLongs = 0x0612D428; // the compiler's division of longs: r1 by r0, in r0
    constexpr uint32_t kInPose = 0x06013280;      // Check_Specific_Term (r4, r5 the other, r6 the answer, r7 and two
                                                  // longs on the stack poses; computer_answers.cpp)

    // The modes (computer_entry.cpp)
    constexpr int16_t kFree = 1, kGuardMode = 7, kGuardProjectile = 9, kHit = 10, kInAir = 11, kParry = 12;
    constexpr int16_t kThrowing = 15, kWaitForFall = 14, kVsShellMode = 8;
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
            : g(guest), size(bytes), entry(guest.R(15)) {
            g.R(15) -= size;
        }
        ~Frame() { Pop(); }
        // The frame given back (before a tail jump: the routine jumped to sees the caller's stack pointer)
        void Pop() {
            if (live) {
                g.R(15) += size;
                live = false;
            }
        }
        Guest &g;
        const uint32_t size;
        const uint32_t entry; // the stack pointer the routine was called with (its arguments beyond r7 there)
        bool live = true;
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
        frame.Pop();
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
            frame.Pop();
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
        s.SetByte(kParryFlag, 0);
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
        if (Call(g, kCheckHit, s.me) || Call(g, kCheckCaught, s.me) || Call(g, kCheckParry, s.me)) {
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
        if (Call(g, kCheckCaught, s.me) || Call(g, kCheckParry, s.me)) {
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
        if (Call(g, kCheckHit, s.me) || Call(g, kCheckCaught, s.me) || Call(g, kCheckParry, s.me)) {
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

    // 060084EC (Command_Attack_SP, r5 the character, r6 the move, r7 its power): a special move put in as the fighter
    // gets up, in steps (Place 1): 0 the dashes' commands read again (dash_flag_clear), the move's command made the
    // one to put in (the character's commands, the move's low byte), the mash after it chosen (Check_Rapid), the mash's
    // and the jump's levers 0x90; 1 the command put in (Command_Type_01 for one of kind 2, else Command_Type_00), 1
    // once it is all in (Command_Type_00's -1 ends it at 99); 2 once the move is at its end (kMoveKind 64) the
    // jump's lever; then the mash after the move (Rapid_Sub), and 1 once the fighter is free and not attacking.
    // Leaves r0 and r7 as the routine does (its callers read them: liveout.py).
    bool SpecialMoveLift(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const uint8_t character = static_cast<uint8_t>(g.R(5));
        const uint32_t move = Signed(static_cast<int16_t>(g.R(6)));
        const uint32_t power = g.R(7);
        const auto result = [&](uint32_t r0, bool t) {
            g.R(0) = r0;
            g.SetT(t);
            g.Return(60);
            return true;
        };
        g.R(4) = kPlanPlace;
        switch (s.Place(1)) {
        case 0: {
            s.SetPlace(1, s.Place(1) + 1);
            g.R(3) = kClearDash;
            g.R(4) = Signed(s.side);
            g.Call(kClearDash);
            g.R(5) = move;
            const uint32_t commands = g.Read32(kCommands + Signed(static_cast<int8_t>(character) * 4));
            g.R(0) = commands;
            g.R(2) = (move & 0xFF) << 2;
            g.R(3) = g.Read32(commands + g.R(2));
            g.Write32(s.At(kTechAddress, 4), g.R(3));
            g.R(0) = Signed(s.side * 2);
            g.R(2) = s.At(kTechIndex, 2);
            g.R(3) = 12;
            s.SetWord(kTechIndex, 12);
            g.R(3) = kCheckRapid;
            g.R(4) = s.me;
            g.Call(kCheckRapid);
            g.R(4) = 0x90;
            g.R(0) = Signed(s.side * 2);
            s.SetWord(kRapidButtons, 0x90);
            g.R(3) = s.At(kJumpLever, 2);
            s.SetWord(kJumpLever, 0x90);
            return result(0, false);
        }
        case 1: {
            const uint32_t strength = Signed(static_cast<int16_t>(power)) & 15;
            g.R(1) = kTechAddress;
            g.R(4) = Signed(s.side);
            g.R(3) = Signed(s.side * 2);
            g.R(2) = g.Read32(s.At(kTechAddress, 4));
            const int16_t kind = static_cast<int16_t>(g.Read16(g.R(2) + (Signed(static_cast<int16_t>(g.Read16(s.At(kTechIndex, 2)))) << 1)));
            g.R(0) = Signed(kind);
            if (kind == 2) {
                g.R(6) = Signed(-1);
                g.R(3) = kCommand1;
                g.R(5) = strength;
                g.R(4) = s.me;
                g.Call(kCommand1);
                if (g.R(0) != 0) {
                    g.R(1) = 2;
                    g.R(0) = s.PlaceAt(0);
                    g.R(2) = s.PlaceAt(1);
                    g.R(3) = Signed(s.Place(1) + 1);
                    s.SetPlace(1, static_cast<int32_t>(g.R(3)));
                }
            } else {
                g.R(3) = kCommandStep0;
                g.R(7) = Signed(-1);
                g.R(6) = move;
                g.R(5) = strength;
                g.R(4) = s.me;
                g.Call(kCommandStep0);
                if (g.R(0) == Signed(-1)) {
                    g.R(3) = 99;
                    g.R(0) = s.PlaceAt(0);
                    g.R(2) = s.PlaceAt(1);
                    s.SetPlace(1, 99);
                }
            }
            return s.Place(1) == 2 ? result(1, true) : result(0, false);
        }
        case 2:
            g.R(0) = s.FieldByte(kMoveKind);
            if (g.R(0) == 64) {
                g.R(0) = kLever;
                g.R(4) = Signed(s.side * 2);
                g.R(3) = Signed(s.Word(kJumpLever));
                s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
                s.SetPlace(1, s.Place(1) + 1);
                g.R(0) = Signed(s.Place(1));
            }
            break;
        }
        g.R(3) = kMashSub;
        g.R(4) = s.me;
        g.Call(kMashSub);
        g.R(0) = 38;
        g.R(2) = Signed(s.Field(kDoing));
        if (g.R(2) != 0) {
            return result(0, false);
        }
        g.R(1) = kFighters + kAttackingByte;
        g.R(0) = Signed(static_cast<int16_t>(s.side * kFighterSize));
        g.R(3) = kFighterSize;
        g.R(2) = Signed(static_cast<int8_t>(g.Read8(g.R(1) + g.R(0))));
        return g.R(2) != 0 ? result(0, false) : result(1, true);
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
        if (Call(s.g, kCheckCaught, s.me) || Call(s.g, kCheckParry, s.me)) {
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
            if (Call(g, kCheckParry, s.me)) {
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
        if (g.Read8(kCanParry + Signed(s.Field(kDoingHow))) != 0) {
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
        if (g.Read8(kCanParry + Signed(s.Field(kDoingHow))) == 0) {
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


    // --- levels ---

    // The play time's level (Setup_Lv04, Setup_Lv10, Setup_Lv18): the first of `count` play times (a row of words)
    // that kPlayTime has not passed, or `count`. `last` the last play time compared, `ended` true when all were passed.
    int16_t PlayTimeLevel(Guest &g, uint32_t row, int16_t count, uint32_t &last, bool &ended) {
        int16_t level = 0;
        for (;;) {
            const int16_t now = static_cast<int16_t>(g.Read16(kPlayTime));
            last = Signed(static_cast<int16_t>(g.Read16(row + Signed(level * 2))));
            if (!(now > static_cast<int32_t>(last))) {
                ended = false;
                return level;
            }
            level++;
            if (level >= count) {
                ended = true;
                return level;
            }
        }
    }

    // 0600F9D2 (Setup_Lv04, r4 the row): the level of four, by kRank's set of rows (the offsets cut to signed bytes,
    // as the game's code has them). Leaves the registers as the routine does.
    bool LevelOf4Lift(Guest &g) {
        const int32_t rank = static_cast<int8_t>(g.Read8(kRank));
        const uint32_t row = kLevels4 + Signed(static_cast<int8_t>(rank * 6)) + Signed(static_cast<int8_t>(g.R(4) * 6));
        uint32_t last = 0;
        bool ended = false;
        const int16_t level = PlayTimeLevel(g, row, 3, last, ended);
        g.R(0) = Signed(level);
        g.R(1) = kLevels4;
        g.R(2) = ended ? Signed(level) : last;
        g.R(3) = Signed(static_cast<int16_t>(g.Read16(kPlayTime)));
        g.R(4) = Signed(level);
        g.R(5) = 3;
        g.R(6) = row;
        g.R(7) = kPlayTime;
        g.SetT(ended);
        g.Return(30);
        return true;
    }

    // 0600FA84 (Setup_Lv10, r4 the row): the level of ten. Leaves the registers as the routine does.
    bool LevelOf10Lift(Guest &g) {
        const int32_t rank = static_cast<int8_t>(g.Read8(kRank));
        const uint32_t row = kLevels10 + Signed(static_cast<int8_t>(rank * 18)) + Signed(static_cast<int8_t>(g.R(4) * 18));
        uint32_t last = 0;
        bool ended = false;
        const int16_t level = PlayTimeLevel(g, row, 9, last, ended);
        g.R(0) = Signed(level);
        g.R(1) = kLevels10;
        g.R(2) = last;
        g.R(3) = ended ? Signed(level) : Signed(static_cast<int16_t>(g.Read16(kPlayTime)));
        g.R(4) = Signed(level);
        g.R(5) = 9;
        g.R(6) = row;
        g.R(7) = kPlayTime;
        g.SetT(ended);
        g.Return(40);
        return true;
    }

    // 0600FAC4 (Setup_Lv18, r4 the row): the level of eighteen (its sets 306 bytes apart). Leaves the registers as
    // the routine does.
    bool LevelOf18Lift(Guest &g) {
        const uint32_t rowNumber = g.R(4);
        const uint32_t row = kLevels18 + Signed(static_cast<int16_t>(g.Read8(kRank) * 306)) + Signed(static_cast<int16_t>(rowNumber * 34));
        uint32_t last = 0;
        bool ended = false;
        const int16_t level = PlayTimeLevel(g, row, 17, last, ended);
        g.R(0) = Signed(level);
        g.R(1) = rowNumber;
        g.R(2) = last;
        g.R(3) = ended ? Signed(level) : Signed(static_cast<int16_t>(g.Read16(kPlayTime)));
        g.R(4) = Signed(level);
        g.R(5) = 17;
        g.R(6) = row;
        g.R(7) = kPlayTime;
        g.SetT(ended);
        g.Return(60);
        return true;
    }

    // 0600FB06: the chance of a counter to an approach, by the level of eight (7 broken into, 2 weak), kept in
    // kLevelNow
    bool CounterChanceLift(Guest &g) {
        Side s(g, g.R(4));
        g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf8)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 7);
        }
        if (s.Weak()) {
            g.Write8(kLevelNow, 2);
        }
        g.R(0) = g.Read8(kCounterChances + Signed(static_cast<int8_t>(g.Read8(kLevelNow))));
        g.Return(30);
        return true;
    }

    // 0600F93C (Select_Reflection_Time): the frames before a reaction: one of 32 at random, by the level of 18 (from
    // kThrowLevel, plus the bonus; 19 broken into, 2 weak) and the character played (a challenger's fight: the
    // table's last part). Leaves the registers as the routine does.
    bool ReactionTimeLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const int8_t dice = static_cast<int8_t>(Dice(g, kDice128));
        g.R(4) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kThrowLevel)));
        g.Call(kLevelOf18);
        uint32_t level = g.R(0) + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kLevelBonus)));
        const uint8_t brokenInto = g.Read8(kBrokenInto);
        if (brokenInto == 2) {
            level = 19;
        }
        g.R(2) = kDemo;
        g.R(3) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kDemo)));
        if (g.Read8(kDemo) == 0) {
            g.R(3) = s.me;
            if (g.Read8(kWeakSide) == static_cast<uint32_t>(s.side)) {
                level = 2;
            }
        }
        const uint32_t level32 = Signed(static_cast<int8_t>(level) * 32);
        uint32_t at;
        if (brokenInto == 1 || brokenInto == 2) {
            g.R(1) = 0x2D00;
            at = kReactionTimes + 0x2D00 + level32 + Signed(dice);
            g.SetT(true);
        } else {
            const uint8_t played = g.Read8(kChosen + Signed(s.side));
            g.R(1) = kChosen;
            g.R(3) = played;
            at = kReactionTimes + Signed(static_cast<int16_t>(played * 640)) + level32 + Signed(dice);
            g.SetT(false);
        }
        g.R(0) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(at)));
        g.R(4) = level;
        g.R(5) = kReactionTimes;
        g.R(6) = Signed(dice);
        g.R(7) = level32;
        g.Return(40);
        return true;
    }

    // 0600FCAC (Ck_Area for a projectile): which of the side's ranges against a projectile the gap is in, 3 past
    // them all
    int16_t RangeVsShell(Side &s) {
        for (int16_t range = 0; range < 3; range++) {
            const uint32_t limit = kShellRanges + Signed(static_cast<int8_t>(s.side * 6)) + Signed(range * 2);
            if (!(s.Word(kGap) > static_cast<int16_t>(s.g.Read16(limit)))) {
                return range;
            }
        }
        return 3;
    }
    bool RangeVsShellLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = Signed(RangeVsShell(s));
        g.Return(30);
        return true;
    }

    // 0600FCEE: the gap to the other along the floor, and the lever toward it (8 to the right, 4 to the left).
    // Leaves the registers as the routine does.
    bool GapAndLeverLift(Guest &g) {
        Side s(g, g.R(4));
        const int32_t x = s.Field(kX);
        g.R(2) = Signed(x);
        g.R(1) = Signed(static_cast<int16_t>(g.Read16(s.Other() + kX)) - x);
        s.SetWord(kGap, static_cast<int32_t>(g.R(1)));
        g.R(5) = kGap;
        if (s.Word(kGap) > 0) {
            s.SetWord(kLever, 8);
            g.R(0) = Signed(s.side * 2);
            g.R(3) = 8;
            g.R(6) = kLever;
        } else {
            s.SetWord(kLever, 4);
            g.R(3) = Signed(-s.Word(kGap));
            s.SetWord(kGap, static_cast<int32_t>(g.R(3)));
            g.R(0) = Signed(s.side);
            g.R(6) = s.At(kGap, 2);
        }
        g.SetT(false);
        g.Return(20);
        return true;
    }


    // --- the plan (Select_Active) ---

    // 060104B4 (Select_Active): the plan for mode 2, from the level of eight (7 broken into, 2 weak) and a dice of
    // 32: by the range, a row of the character's own table. When its super art calls for it (Check_SA_Active, which
    // kind), a super art's plan instead, by the level of four (3 broken into, 2 weak) and the range. Leaves the
    // registers as the routine does.
    bool ChoosePlanLift(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf8)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 7);
        }
        if (s.Weak()) {
            g.Write8(kLevelNow, 2);
        }
        g.Call(kDice32);
        g.Write8(kDiceNow, static_cast<uint8_t>(g.R(0)));
        const uint32_t kindAt = g.R(15); // (the answer's word, on the stack)
        g.R(4) = s.me;
        g.R(5) = kindAt;
        g.Call(kSuperArtPlan);
        const int16_t range = s.Word(kRange);
        const int32_t slot = range >= 0 && range < 3 ? range : 3;
        const int32_t level32 = static_cast<int8_t>(g.Read8(kLevelNow)) * 32;
        const int32_t dice = static_cast<int8_t>(g.Read8(kDiceNow));
        uint8_t plan;
        if (g.R(0) != 0) {
            g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf4)));
            if (g.Read8(kBrokenInto) == 2) {
                g.Write8(kLevelNow, 3);
            }
            if (s.Weak()) {
                g.Write8(kLevelNow, 2);
            }
            const int32_t kind = static_cast<int16_t>(g.Read16(kindAt));
            const uint32_t base = Signed((kind - 1) * 128);
            const uint32_t row = kSuperArtPlans[slot] + base + Signed(static_cast<int8_t>(g.Read8(kLevelNow)) * 32);
            plan = g.Read8(row + Signed(dice));
            g.R(1) = kRange;
            g.R(2) = Signed(dice);
            g.R(4) = base;
        } else {
            const uint32_t at = kPlans[slot] + Signed(s.Character() * 256) + Signed(level32) + Signed(dice);
            plan = g.Read8(at);
            g.R(1) = kPlans[slot];
            g.R(2) = at;
        }
        s.SetWord(kPattern, plan);
        g.R(0) = Signed(s.side * 2);
        g.R(3) = plan;
        g.SetT(false);
        g.Return(60);
        return true;
    }


    // --- the reaction (Select_Passive) ---

    // 06010CD8 (r4 the answer's kind, its top four bits): the reaction's level, kept in kLevelNow: for kind 0 the level
    // of four (3 broken into), else the level of eight, row 2 (7 broken into)
    void ReactionLevel(Side &s, int16_t kind) {
        Guest &g = s.g;
        if (kind == 0) {
            g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf4)));
            if (g.Read8(kBrokenInto) == 2) {
                g.Write8(kLevelNow, 3);
            }
            return;
        }
        g.R(4) = 2;
        g.Call(kLevelOf8);
        g.Write8(kLevelNow, static_cast<uint8_t>(g.R(0)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 7);
        }
    }

    // 06010D8C (r5 the side): the reaction's column: the dice (0-15), but 8 or more when its super art may be used or
    // either fighter is at 48 health or less (the second half of the table: the reactions of a close fight)
    int32_t ReactionColumn(Side &s) {
        Guest &g = s.g;
        const int32_t dice = static_cast<int8_t>(g.Read8(kDiceNow));
        const auto fighter = [&](int32_t side) { return kFighters + Signed(static_cast<int16_t>(side * kFighterSize)); };
        if (g.Read8(g.Read32(fighter(s.side) + kSuperArt) + 10) != 0) {
            return 8 | dice;
        }
        if (static_cast<int16_t>(g.Read16(fighter(s.side) + kHealth)) > 48 &&
            static_cast<int16_t>(g.Read16(fighter(s.side ^ 1) + kHealth)) > 48) {
            return dice;
        }
        return 8 | dice;
    }

    // 06010D18: the column of the reaction's table, kept in kDiceNow: against a repeated attack (answer 32) one of the
    // first eight at random, and the answer becomes 28; else ReactionColumn
    void ChooseColumn(Side &s) {
        Guest &g = s.g;
        if (s.Word(kAnswer) == 32) {
            g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice64) & 7));
            s.SetWord(kAnswer, 28);
            return;
        }
        g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice64)));
        g.Write8(kDiceNow, static_cast<uint8_t>(ReactionColumn(s)));
    }

    // 060109AC (Select_Passive): the reaction to the answer chosen (kAnswer: its low twelve bits the row, the top four
    // a kind for the level), a plan for the character's answer routine (mode 6): from one of four tables by the range
    // (by the projectile's ranges against one: answers 11 and 30), the character's part, the answer's row, the level's
    // part and a column (ChooseColumn). 0xFF there: none (-1), and that range's answers are let be for 120 frames
    // (kJumpTimers). Else the plan, and mode 5: a wait (Select_Reflection_Time) before it, but none for answers 19 and
    // 25 (mode 6 at once). Leaves the registers as the routine does.
    bool React(Guest &g, uint32_t me) {
        Frame frame(g, 32);
        Side s(g, me);
        if (s.Word(kAnswer) == 11 || s.Word(kAnswer) == 30) { // (each tested: both can not hold)
            Frame push(g, 4);
            s.SetWord(kRange, static_cast<int32_t>(Call(g, 0x0600FCAC, s.me)));
        }
        s.SetWord(kLastAttackSeen, s.Word(kAttacksSeen));
        s.SetByte(kStanding, 0);
        ReactionLevel(s, static_cast<int16_t>(s.Word(kAnswer) >> 12));
        if (s.Weak()) {
            g.Write8(kLevelNow, 1);
        }
        s.SetWord(kAnswer, s.Word(kAnswer) & 0xFFF);
        ChooseColumn(s);
        const int16_t range = s.Word(kRange);
        const int32_t slot = range >= 0 && range < 3 ? range : 3;
        const uint32_t perCharacter = slot < 3 ? 2048 : 1024;
        const uint32_t row = kReactions[slot] + Signed(s.Character()) * perCharacter + Signed(s.Word(kAnswer) * 64) +
                             Signed(static_cast<int8_t>(g.Read8(kLevelNow)) * 16);
        const int8_t entry = static_cast<int8_t>(g.Read8(row + Signed(static_cast<int8_t>(g.Read8(kDiceNow)))));
        const uint32_t timers = s.At(kJumpTimers, 4);
        if (static_cast<uint8_t>(entry) == 0xFF) {
            s.SetByte(kCounter, 0);
            s.SetByte(kAnswering, 0);
            g.Write8(timers + Signed(slot), 120);
            g.R(0) = static_cast<uint32_t>(-1);
            g.R(1) = kRange;
            g.R(2) = Signed(entry);
            g.R(3) = 0xFF;
            g.R(4) = timers;
            g.R(5) = kAnswering;
            g.R(6) = kCounter;
            g.R(7) = kPattern;
            g.SetT(true);
            return false;
        }
        s.SetWord(kPattern, static_cast<uint8_t>(entry));
        g.Write8(timers + Signed(slot), 0);
        s.SetByte(kAnswering, 1);
        for (int n = 1; n < 4; n++) {
            s.SetModeWord(n, 0);
        }
        {
            Frame push(g, 4);
            s.SetWord(kTimer, static_cast<int32_t>(Call(g, 0x0600F93C, s.me)));
        }
        const int16_t answer = s.Word(kAnswer);
        g.R(5) = Signed(answer);
        if (answer == 25 || answer == 19 || s.Word(kTimer) == 0) {
            s.SetModeWord(0, 6);
            for (int n = 0; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            g.R(3) = s.PlaceAt(2);
            g.R(4) = s.PlaceAt(0);
        } else {
            s.SetModeWord(0, 5);
            g.R(3) = 5;
            g.R(4) = Signed(s.side * 2);
        }
        g.R(0) = 1;
        g.SetT(false);
        return true;
    }
    bool ReactLift(Guest &g) {
        React(g, g.R(4));
        g.Return(80);
        return true;
    }


    // --- when to answer (Check_Passive) ---

    // 060138E0 (Check_Blow_Off, r5 the other, r6 an answer): the other knocked down and still in the air: the answer
    // r6 chosen
    bool OtherFallingLift(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t other = g.R(5);
        g.R(0) = 0;
        if (static_cast<int16_t>(g.Read16(other + kDoing)) != kDoingHit) {
            g.Return(10);
            return true;
        }
        g.R(2) = Signed(static_cast<int16_t>(g.Read16(other + kDoingHow)));
        g.R(3) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kKnockedDown + g.R(2))));
        if (g.R(3) == 0) {
            g.Return(14);
            return true;
        }
        g.R(3) = Signed(static_cast<int16_t>(g.Read16(other + kY)));
        if (g.R(3) == 0) {
            g.Return(16);
            return true;
        }
        s.SetWord(kAnswer, static_cast<int32_t>(g.R(6)));
        g.R(3) = s.At(kAnswer, 2);
        g.R(2) = kAnswered;
        g.Write8(kAnswered, 1);
        g.R(0) = 1;
        g.Return(20);
        return true;
    }

    // 060138AE (r5 the other, r6 an answer): the other stunned: the answer chosen, as a counter attack. Leaves the
    // registers as the routine does.
    bool OtherStunnedLift(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t other = g.R(5);
        s.SetByte(kCounter, 1);
        s.SetWord(kAnswer, static_cast<int32_t>(g.R(6)));
        g.R(3) = s.At(kAnswer, 2);
        g.R(6) = other + 0x24;
        g.R(7) = kCounter;
        const int16_t doing = static_cast<int16_t>(g.Read16(other + kDoing));
        g.R(0) = Signed(doing);
        if (doing == kDoingHit) {
            const int16_t how = static_cast<int16_t>(g.Read16(other + kDoingHow));
            g.R(0) = Signed(how);
            if (how == kStunned) {
                g.R(0) = 1;
                g.SetT(true);
                g.Return(16);
                return true;
            }
        }
        s.SetByte(kCounter, 0);
        g.R(3) = 0;
        g.R(0) = 0;
        g.SetT(false);
        g.Return(16);
        return true;
    }

    // 0601373A (r5 the other, r6 an answer): in a demo only, the other standing on the floor with its lever back
    // (the other's controls, read): the answer, as a counter attack
    bool OtherBacksOffLift(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t other = g.R(5);
        g.R(0) = 0;
        if (g.Read8(kDemo) == 0 || g.Read16(other + kDoing) != 0 || g.Read16(other + kY) != 0) {
            g.Return(12);
            return true;
        }
        const uint16_t controls = g.Read16(s.side == 0 ? kControls2 : kControls1);
        const uint16_t back = g.Read8(s.me + 0x0B) != 0 ? 4 : 8;
        if (!(controls & back)) {
            g.Return(16);
            return true;
        }
        s.SetByte(kCounter, 1);
        s.SetWord(kAnswer, static_cast<int32_t>(g.R(6)));
        g.Write8(kAnswered, 1);
        g.R(0) = 1;
        g.Return(20);
        return true;
    }

    // 060136C6 (r5 the other): the other on the floor, and the chance (by the level) against a dice: close or
    // middle, answer 25 to its backing off (0601373A)
    bool OtherOnFloorLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const uint32_t other = g.R(5);
        bool yes = false;
        if (g.Read16(other + kY) == 0) {
            const int16_t chance = static_cast<int16_t>(Call(g, 0x0600FB06, s.me));
            const int16_t dice = Dice(g, kDice128);
            if (chance >= dice && (s.Word(kRange) == 0 || s.Word(kRange) == 1)) {
                CallWith(g, 0x0601373A, s.me, other, 25);
                yes = g.R(0) != 0;
            }
        }
        g.R(0) = yes ? 1 : 0;
        g.Return(30);
        return true;
    }

    // 06013864: the other stunned (answer 2) or lying on the floor (pose 38, answer 0): its reaction (Select_Passive)
    bool OtherDownLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const uint32_t other = s.Other();
        g.Write32(g.R(15), other);
        CallWith(g, 0x060138AE, s.me, other, 2);
        if (g.R(0) == 0) {
            Frame arguments(g, 8);
            g.Write32(g.R(15), 38);
            g.Write32(g.R(15) + 4, 38);
            g.R(7) = 38;
            CallWith(g, kInPose, s.me, other, 0);
            if (g.R(0) == 0) {
                g.SetT(true);
                g.Return(30);
                return true;
            }
        }
        frame.Pop();
        React(g, s.me);
        g.Return(30);
        return true;
    }

    // 06010DEA (Check_Passive): does the computer answer the other now? Not in a counter attack nor going through
    // (kPierce). The other knocked into the air: wait for it to come down (mode 14; -1). Then, in order, until one
    // starts a reaction (1): the other on the floor backing off (0601373A), a projectile coming (0601156A); in the
    // middle of an answer or a parry, only a guard (Check_Guard); the other stunned or lying down (06013864); when
    // the other's action is not one to answer (0x0613E858), only the projectile; then the answer to the other's
    // attack as it begins (Ck_Passive_Term, kPassiveMode 4), a guard, and the answer to what it does later (mode 0).
    // Leaves the registers as the routine does.
    bool CheckAnswerLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const auto done = [&](int32_t result) {
            g.R(0) = static_cast<uint32_t>(result);
            g.Return(80);
            return true;
        };
        const auto answering = [&]() { return s.Byte(kAnswering) != 0 || s.Byte(kParryFlag) != 0; };
        if (s.Byte(kCounter) != 0 || s.Byte(kPierce) != 0) {
            return done(0);
        }
        const uint32_t other = s.Other();
        CallWith(g, kOtherFalling, s.me, other, 0);
        if (g.R(0) != 0) {
            s.SetMode(kWaitForFall);
            return done(-1);
        }
        g.R(4) = s.me;
        g.R(5) = other;
        g.Call(0x060136C6);
        if (g.R(0) != 0 && static_cast<int32_t>(Call(g, kReact, s.me)) != -1) {
            return done(1);
        }
        if (Call(g, kShellCheck, s.me) != 0) {
            return done(1);
        }
        if (answering()) {
            frame.Pop();
            g.R(4) = s.me;
            g.Call(kCheckGuard);
            g.Return(80);
            return true;
        }
        if (Call(g, 0x06013864, s.me) == 1) {
            return done(1);
        }
        if (g.Read8(kAnswerable + Signed(static_cast<int16_t>(g.Read16(other + kDoing)))) == 0) {
            frame.Pop();
            Call(g, kShellCheck, s.me);
            g.Return(80);
            return true;
        }
        g.Write8(kPassiveMode, 4);
        if (Call(g, kChooseAnswer, s.me) != 0 && static_cast<int32_t>(Call(g, kReact, s.me)) != -1) {
            return done(1);
        }
        if (Call(g, kCheckGuard, s.me) != 0) {
            return done(1);
        }
        if (answering()) {
            return done(0);
        }
        g.Write8(kPassiveMode, 0);
        if (Call(g, kChooseAnswer, s.me) != 0) {
            frame.Pop();
            Call(g, kReact, s.me);
            g.Return(80);
            return true;
        }
        return done(0);
    }


    // --- against a projectile ---

    // 06011A60 (Check_Behind, r5 a projectile): has it passed the computer already (behind it, as it faces)?
    bool ShellBehindLift(Guest &g) {
        const uint32_t me = g.R(4), shell = g.R(5);
        const int16_t x = static_cast<int16_t>(g.Read16(me + kX)), shellX = static_cast<int16_t>(g.Read16(shell + kX));
        bool behind;
        if (g.Read8(me + 0x0B) == 0) {
            behind = !(x >= shellX);
            g.R(2) = Signed(shellX);
            g.R(3) = Signed(x);
        } else {
            behind = x > shellX;
            g.R(2) = Signed(x);
            g.R(3) = Signed(shellX);
        }
        g.SetT(!behind == (g.Read8(me + 0x0B) == 0));
        g.R(0) = behind ? 1 : 0;
        g.Return(10);
        return true;
    }

    // 06011704 (r4 a projectile): one of the kinds the computer lets be (its byte 4: 13-15, 36-39, 77-80, 84, 85,
    // 122-126). 06011500 (Check_Ignore_Shell2, Check_Diagonal_Shell's: computer_moves.cpp) is the same code.
    bool ShellLetBeLift(Guest &g) {
        const uint8_t kind = g.Read8(g.R(4) + 4);
        const bool yes = (kind >= 36 && kind <= 39) || (kind >= 13 && kind <= 15) || kind == 84 || kind == 85 ||
                         (kind >= 77 && kind <= 80) || (kind >= 122 && kind <= 126);
        g.R(0) = yes ? 1 : 0;
        g.Return(20);
        return true;
    }

    // 0601176E (r5 a projectile): the frames before it reaches the computer: the gap to its front (0x0608EAD6) by its
    // speed (the divide 0x0612D428), made positive
    bool ArrivalLift(Guest &g) {
        Frame frame(g, 12);
        const uint32_t me = g.R(4), shell = g.R(5);
        g.Write32(g.R(15) + 4, me);
        g.Write32(g.R(15), shell);
        g.R(4) = shell;
        g.Call(kShellFront);
        g.R(4) = (g.R(0) << 16) - g.Read32(me + kX);
        g.R(1) = g.R(4);
        g.R(2) = 124;
        g.R(0) = g.Read32(shell + 124); // (its speed along the floor, 16.16)
        g.Call(kDivideLongs);
        g.R(5) = g.R(0);
        g.R(4) = Signed(static_cast<int16_t>(g.R(5)));
        const int16_t frames = static_cast<int16_t>(g.R(4));
        g.R(0) = frames >= 0 ? g.R(4) : Signed(static_cast<int16_t>(-static_cast<int32_t>(g.R(5))));
        g.SetT(frames >= 0);
        g.Return(30);
        return true;
    }

    void AgainstShell(Side &s, uint32_t shell, int16_t frames);
    bool AgainstShellLift(Guest &g) {
        Side s(g, g.R(4));
        AgainstShell(s, g.R(5), static_cast<int16_t>(g.R(6)));
        g.Return(40);
        return true;
    }

    // 060117B0 (r5 a projectile): let it be this time? A dice against the character's chance of answering by the
    // level of ten (9 broken into, 2 weak): let be, projectiles are not looked at for 60 frames (kShellIgnore)
    bool ShellIgnoreLift(Guest &g) {
        Frame frame(g, 12);
        Side s(g, g.R(4));
        g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf10)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 9);
        }
        if (s.Weak()) {
            g.Write8(kLevelNow, 2);
        }
        g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice128)));
        const uint32_t row = static_cast<uint8_t>(s.Row() * 10);
        const int8_t chance = static_cast<int8_t>(g.Read8(kShellAnswers + row + Signed(static_cast<int8_t>(g.Read8(kLevelNow)))));
        if (chance > static_cast<int8_t>(g.Read8(kDiceNow))) {
            g.R(0) = 0;
        } else {
            s.SetByte(kShellIgnore, 60);
            g.R(0) = 1;
        }
        g.Return(30);
        return true;
    }

    // 060119C6 (r6 which kind of projectile, 0xFF none): the plan against it, from the character's table by the
    // level of eight (7 broken into, 4 weak) at random; 0 none
    bool ShellPlanLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const uint16_t which = static_cast<uint16_t>(g.R(6));
        if (which == 0xFF) {
            g.R(0) = 0;
            g.R(3) = 0xFF;
            g.SetT(true);
            g.Return(10);
            return true;
        }
        g.Write8(kLevelNow, static_cast<uint8_t>(Level(g, kLevelOf8)));
        if (g.Read8(kBrokenInto) == 2) {
            g.Write8(kLevelNow, 7);
        }
        if (s.Weak()) {
            g.Write8(kLevelNow, 4);
        }
        g.Write8(kDiceNow, static_cast<uint8_t>(Dice(g, kDice64)));
        const uint32_t row = kShellPlans + Signed(s.Character() * 512) + which * 128u +
                             Signed(static_cast<int8_t>(g.Read8(kLevelNow)) * 16);
        g.R(1) = kDiceNow;
        g.R(0) = g.Read8(row + Signed(static_cast<int8_t>(g.Read8(kDiceNow))));
        g.Return(30);
        return true;
    }

    // 06011954 (r5 the projectile): guard it: mode 9 for 10 frames, a dash given up, the guard as it needs
    void GuardShell(Side &s, uint32_t shell) {
        s.SetMode(kGuardProjectile);
        s.SetWord(kTimer, 10);
        Call(s.g, kClearDash, Signed(s.side));
        s.SetWord(kGuardType, 0);
        GuardLever(s.g, s.me, shell);
    }

    // 06011866 (r5 the projectile, r6 the frames before it arrives): the plan against a projectile (kPattern, for the
    // character's routine of mode 8). Arriving within the character's limit: a parry (plan 9) if it may, else a
    // guard; further, a plan by its kind (060119C6). Then plan 0 is a guard (mode 9), 9 an answer, 10 a plan by the
    // kind 0 (or a guard).
    void AgainstShell(Side &s, uint32_t shell, int16_t frames) {
        Guest &g = s.g;
        if (!(frames > g.Read8(kShellLimits + Signed(s.Character())))) {
            s.SetWord(kPattern, Call(g, 0x0601111A, s.me) != 0 ? 9 : 0);
        } else {
            CallWith(g, 0x060119C6, s.me, shell, g.Read8(kShellKinds + g.Read8(shell + 4)));
            s.SetWord(kPattern, static_cast<int32_t>(g.R(0)));
        }
        switch (s.Word(kPattern)) {
        case 0:
            GuardShell(s, shell);
            return;
        case 9:
            s.SetByte(kAnswering, 1);
            return;
        case 10:
            CallWith(g, 0x060119C6, s.me, shell, 0);
            s.SetWord(kPattern, static_cast<int32_t>(g.R(0)));
            if (s.Word(kPattern) == 0) {
                GuardShell(s, shell);
            }
            return;
        }
    }

    // 0601156A: a projectile coming? (but while projectiles are let be, kShellIgnore, counting down, and in mode 8
    // already). Of the other's eight (get_vs_shell_adrs), the first going, toward the computer, not passed it, not a
    // kind let be, and not let be at random (060117B0): mode 8 against it (kShell), its plan by when it arrives.
    // (Not 0 then. When none is found, the routine leaves what its last test left in r0, as the game's code does.)
    bool ShellComingLift(Guest &g) {
        Frame frame(g, 40);
        Side s(g, g.R(4));
        if (s.Byte(kShellIgnore) != 0) {
            const int32_t left = static_cast<int8_t>(s.Byte(kShellIgnore)) - 1;
            s.SetByte(kShellIgnore, left);
            g.R(3) = Signed(left);
            g.R(4) = kShellIgnore;
            g.R(0) = 0;
            g.SetT(false);
            g.Return(20);
            return true;
        }
        g.R(3) = 0;
        g.R(4) = kShellIgnore;
        if (s.ModeWord(0) == 8) {
            g.R(0) = 0;
            g.SetT(true);
            g.Return(20);
            return true;
        }
        const uint32_t other = s.Other();
        const uint32_t found = g.R(15); // (where get_vs_shell_adrs puts the projectile)
        for (int16_t n = 0; n < 8; n++) {
            g.R(2) = Signed(n);
            g.Write32(g.R(15) + 4, Signed(n));
            g.R(3) = kFindShell;
            g.R(7) = found;
            g.R(6) = Signed(n);
            g.R(5) = Signed(static_cast<int16_t>(g.Read16(other + kSide)));
            g.R(4) = other;
            g.Call(kFindShell);
            if (g.R(0) == 0) {
                g.R(7) = found;
                g.R(6) = g.Read32(g.R(15) + 4);
                g.R(5) = Signed(static_cast<int16_t>(g.Read16(other + kSide)));
                g.R(3) = kFindShell;
                g.R(4) = s.me;
                g.Call(kFindShell);
                if (g.R(0) == 0) {
                    g.SetT(true);
                    g.Return(60);
                    return true;
                }
            }
            const uint32_t shell = g.Read32(found);
            g.R(1) = 38;
            g.R(0) = Signed(static_cast<int16_t>(g.Read16(shell + kDoing)));
            if (g.R(0) == 2) {
                continue;
            }
            g.R(3) = shell;
            g.R(2) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(s.me + 0x0B)));
            g.R(0) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(shell + kFacesRight)));
            if (g.R(0) == g.R(2)) {
                continue;
            }
            g.R(0) = Signed(static_cast<int16_t>(g.Read16(shell + 0x24)));
            if (g.R(0) != 1) {
                continue;
            }
            CallWith(g, 0x06011A60, s.me, shell, g.R(6));
            if (g.R(0) != 0) {
                continue;
            }
            g.R(0) = g.Read8(shell + kShellKind);
            if (g.R(0) == 2) {
                continue;
            }
            g.R(4) = shell;
            g.Call(0x06011704);
            if (g.R(0) != 0) {
                continue;
            }
            g.R(4) = s.me;
            g.R(5) = shell;
            g.Call(0x0601176E);
            const int16_t frames = static_cast<int16_t>(g.R(0));
            g.R(4) = s.me;
            g.R(5) = shell;
            g.Call(0x060117B0);
            if (g.R(0) != 0) {
                g.R(0) = 0;
                g.SetT(false);
                g.Return(60);
                return true;
            }
            s.SetMode(kVsShellMode);
            for (int k = 0; k < 4; k++) {
                s.SetPlace(k, 0);
            }
            g.Write32(s.At(kShell, 4), shell);
            g.R(2) = s.At(kShell, 4);
            g.R(3) = s.PlaceAt(3);
            CallWith(g, 0x06011866, s.me, shell, Signed(frames));
            g.R(0) = 1;
            g.Return(60);
            return true;
        }
        g.R(2) = 8;
        g.SetT(true);
        g.Return(60);
        return true;
    }


    // --- steps of the plans (Com_Sub: the character's plans call them; see computer_moves.cpp) ---

    // A routine of the answers' called with three longs on the stack (as the game's code pushes them), r4-r7 given
    void CallWithStack(Guest &g, uint32_t routine, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t a, uint32_t b,
                       uint32_t c) {
        g.R(15) -= 12;
        g.Write32(g.R(15), a);
        g.Write32(g.R(15) + 4, b);
        g.Write32(g.R(15) + 8, c);
        g.R(5) = r5;
        g.R(6) = r6;
        g.R(7) = r7;
        g.Call(routine);
        g.R(15) += 12;
    }

    // 06011C4C (r5 the other, r6 where to put a reaction): against some characters' attacks the plan's test gives way:
    // while the other attacks (kOtherAttacking), by the character it plays (kChosen): Alex's cross chop (0x06013A18,
    // 15), 3; Yun's and Yang's move 43 (Check_Special_Technique, computer_answers.cpp), Necro's 42, 2; Necro's and
    // Elena's of 0x06013404 (20, 4 and 5), 3 with the answer 15; Oro's 44 (8, 15), 3. The reaction (2 or 3) put at r6
    // and returned; else 0. Leaves the registers as the routine does.
    uint32_t TestGivesWay(Guest &g, uint32_t me, uint32_t other, uint32_t where) {
        Frame frame(g, 28);
        Side s(g, me);
        const auto result = [&](uint32_t r0, bool t) {
            g.R(0) = r0;
            g.SetT(t);
            return r0;
        };
        const auto put = [&](uint16_t reaction) {
            g.Write16(where, reaction);
            return result(reaction, false);
        };
        g.R(1) = kOtherAttacking;
        g.R(0) = Signed(s.side);
        g.R(3) = Signed(static_cast<int8_t>(s.Byte(kOtherAttacking)));
        if (g.R(3) == 0) {
            return result(0, true);
        }
        g.R(1) = kChosen;
        const uint8_t character = g.Read8(kChosen + Signed(static_cast<int16_t>(g.Read16(other + kSide))));
        g.R(0) = character;
        const uint32_t none = Signed(-1);
        g.R(4) = me;
        switch (character) {
        case 1: // (Alex)
            g.R(3) = 0x06013A18;
            g.R(6) = 15;
            g.R(5) = other;
            g.Call(0x06013A18);
            return g.R(0) == 0 ? result(0, true) : put(3);
        case 3: // (Yun)
        case 10: // (Yang)
            g.R(2) = 43;
            CallWithStack(g, kSpecialTechnique, other, 0, 0, 43, none, none);
            return g.R(0) == 0 ? result(0, true) : put(2);
        case 5: // (Necro)
            g.R(2) = 42;
            CallWithStack(g, kSpecialTechnique, other, 0, 0, 42, none, none);
            if (g.R(0) != 0) {
                return put(2);
            }
            [[fallthrough]];
        case 8: { // (Elena)
            g.R(character == 5 ? 3 : 2) = 0x06013404;
            g.R(7) = character == 5 ? 4 : 5;
            g.R(6) = 20;
            g.R(5) = other;
            g.R(4) = me;
            g.Call(0x06013404);
            if (g.R(0) == 0) {
                return result(0, true);
            }
            g.R(3) = 15;
            g.R(0) = Signed(s.side * 2);
            g.R(2) = s.At(kAnswer, 2);
            s.SetWord(kAnswer, 15);
            return put(3);
        }
        case 9: // (Oro)
            g.R(2) = 1;
            g.R(3) = 44;
            CallWithStack(g, kSpecialTechnique, other, 15, 8, 44, 1, none);
            return g.R(0) == 0 ? result(0, true) : put(3);
        default:
            return result(0, false);
        }
    }
    bool TestGivesWayLift(Guest &g) {
        TestGivesWay(g, g.R(4), g.R(5), g.R(6));
        g.Return(60);
        return true;
    }

    // 06011C10 (r4, r5 the other, r6 the test): a test of the fight by its number (the table at 0x0613E864), unless
    // against the other's attack it gives way first (0x06011C4C: that reaction). Leaves the registers as the routine
    // does.
    bool TermTestLift(Guest &g) {
        Frame frame(g, 20);
        const uint32_t me = g.R(4), other = g.R(5);
        const uint16_t test = static_cast<uint16_t>(g.R(6));
        const uint32_t where = g.R(15);
        g.Write32(where + 4, me);
        g.Write32(where + 8, other);
        g.Write16(where + 12, test);
        g.R(0) = test;
        g.R(6) = where;
        g.R(5) = other;
        g.R(4) = me;
        if (TestGivesWay(g, me, other, where) != 0) {
            g.R(0) = Signed(static_cast<int16_t>(g.Read16(where)));
            g.Return(30);
            return true;
        }
        g.SetT(true); // (tst r0,r0: 0)
        g.R(1) = kTests;
        g.R(0) = g.Read32(kTests + (Signed(static_cast<int16_t>(test)) << 2));
        g.R(5) = other;
        g.R(4) = me;
        g.Call(g.R(0));
        g.Return(60);
        return true;
    }

    // --- the tests of the fight (the table at 0x0613E864, 0x06011C10's: r4 the computer, r5 the other or a
    // projectile; 0 not yet, 1 met, -1 the other plan) ---

    // A wait of the test's own (kTermTimer: 0 the first time, when kTimer is set to `frames`): true once kTimer has
    // counted down to 0 (it is then left at 1 for the test's next call). Leaves r0 the side's offset, r3 kTimer.
    bool TermWaitStarted(Side &s, int16_t frames) {
        Guest &g = s.g;
        if (s.Word(kTermTimer) == 0) {
            s.SetWord(kTermTimer, s.Word(kTermTimer) + 1);
            g.R(0) = Signed(s.side * 2);
            g.R(3) = Signed(frames);
            s.SetWord(kTimer, frames);
            return false;
        }
        return true;
    }
    bool TermWaitOver(Side &s) {
        Guest &g = s.g;
        g.R(0) = Signed(s.side * 2);
        g.R(3) = Signed(static_cast<int16_t>(s.Word(kTimer) - 1));
        s.SetWord(kTimer, static_cast<int32_t>(g.R(3)));
        return g.R(3) == 0;
    }
    void TermWaitDone(Side &s) {
        s.g.R(0) = Signed(s.side * 2);
        s.g.R(3) = 1;
        s.SetWord(kTimer, 1);
    }
    bool TermResult(Guest &g, uint32_t r0, bool t) {
        g.R(0) = r0;
        g.SetT(t);
        g.Return(20);
        return true;
    }

    // 06011D5E (test 0): 30 frames
    bool TestWait30(Guest &g) {
        Side s(g, g.R(4));
        g.R(6) = kTimer;
        g.R(5) = kTermTimer;
        if (TermWaitStarted(s, 30) && TermWaitOver(s)) {
            TermWaitDone(s);
            return TermResult(g, 1, false);
        }
        return TermResult(g, 0, false);
    }

    // 06011DA6 (test 1): met at once
    bool TestAtOnce(Guest &g) {
        g.R(0) = 1;
        g.Return(4);
        return true;
    }

    // 06011DAA (test 2): met unless the other (here a projectile: its kSide 13) is still there and not throwing
    bool TestShellGone(Guest &g) {
        const uint32_t other = g.R(5);
        g.R(0) = Signed(static_cast<int16_t>(g.Read16(other + kDoing)));
        if (g.R(0) == 2) {
            return TermResult(g, 1, true);
        }
        g.R(0) = Signed(static_cast<int16_t>(g.Read16(other + kSide)));
        return g.R(0) == 13 ? TermResult(g, 0, true) : TermResult(g, 1, false);
    }

    // 06012084: the other is Ken (kChosen 11) with his second super art (its word 1) ready (byte +10 -1)
    bool KenArtReady(Guest &g, uint32_t me) {
        const uint32_t other = g.Read32(me + kOpponent);
        g.R(4) = other;
        const auto result = [&](uint32_t r0, bool t) {
            g.R(0) = r0;
            g.SetT(t);
            return r0 != 0;
        };
        if (static_cast<int8_t>(g.Read8(g.Read32(other + kSuperArt) + 10)) != -1) {
            return result(0, false);
        }
        const int16_t side = static_cast<int16_t>(g.Read16(other + kSide));
        g.R(1) = kChosen;
        if (g.Read8(kChosen + Signed(side)) != 11) {
            return result(0, false);
        }
        g.R(3) = kFighterSize;
        g.R(1) = kFighters + kSuperArt;
        const uint32_t art = g.Read32(kFighters + kSuperArt + Signed(static_cast<int16_t>(side * kFighterSize)));
        return static_cast<int16_t>(g.Read16(art)) == 1 ? result(1, true) : result(0, false);
    }
    bool KenArtReadyLift(Guest &g) {
        KenArtReady(g, g.R(4));
        g.Return(30);
        return true;
    }

    // 06011DC6, 06011E52, 06011EB2, 06011F12 (tests 3-6): `frames` frames, then met unless the other is Ken with his
    // second art ready (0x06012084)
    template <int16_t frames>
    bool TestWaitNotKen(Guest &g) {
        Frame frame(g, 12);
        Side s(g, g.R(4));
        g.R(4) = kTermTimer;
        if (!TermWaitStarted(s, frames) || !TermWaitOver(s)) {
            return TermResult(g, 0, false);
        }
        if (KenArtReady(g, s.me)) {
            return TermResult(g, 0, false);
        }
        TermWaitDone(s);
        return TermResult(g, 1, false);
    }

    // 06012064 (r4 a fighter, r5 a height): coming down, and below that height
    bool ComingDownBelow(Guest &g) {
        const uint32_t who = g.R(4);
        g.R(0) = 0x80;
        g.R(3) = Signed(static_cast<int16_t>(g.Read16(who + kRising)));
        if (static_cast<int32_t>(g.R(3)) >= 0) {
            return TermResult(g, 0, true);
        }
        g.R(0) = 104;
        g.R(5) = Signed(static_cast<int16_t>(g.R(5)));
        g.R(2) = Signed(static_cast<int16_t>(g.Read16(who + kY)));
        return static_cast<int32_t>(g.R(2)) >= static_cast<int32_t>(g.R(5)) ? TermResult(g, 0, true) : TermResult(g, 1, false);
    }

    // 06011F72 (test 7): the other coming down below 40 (0x06012064): met far off (range 3), else the other plan
    // (answer 26); otherwise met after 30 frames
    bool TestOtherLands(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const uint32_t other = g.R(5);
        g.R(4) = kTermTimer;
        const bool started = s.Word(kTermTimer) != 0;
        g.R(5) = 40;
        if (!started) {
            TermWaitStarted(s, 30);
        }
        g.R(4) = other;
        ComingDownBelow(g);
        if (g.R(0) != 0) {
            g.R(2) = 3;
            g.R(0) = Signed(s.side * 2);
            g.R(1) = kRange;
            g.R(3) = Signed(s.Word(kRange));
            if (static_cast<int32_t>(g.R(3)) >= 3) {
                return TermResult(g, 1, true);
            }
            g.R(3) = 26;
            g.R(2) = s.At(kAnswer, 2);
            s.SetWord(kAnswer, 26);
            return TermResult(g, Signed(-1), false);
        }
        if (TermWaitOver(s)) {
            TermWaitDone(s);
            return TermResult(g, 1, false);
        }
        return TermResult(g, 0, false);
    }

    // 06011FF8 (test 8): the computer back on the floor and not attacking, or 180 frames
    [[maybe_unused]] bool TestLanded(Guest &g) {
        Side s(g, g.R(4));
        g.R(6) = kTimer;
        g.R(5) = kTermTimer;
        if (s.Word(kTermTimer) == 0) {
            TermWaitStarted(s, 180);
        }
        g.R(0) = 114;
        g.R(2) = Signed(s.Field(kWasUp));
        if (g.R(2) == 0) {
            g.R(0) = 104;
            g.R(3) = Signed(s.Field(kY));
            if (g.R(3) == 0) {
                g.R(0) = Signed(s.Field(kDoing));
                if (g.R(0) != kDoingAttack) {
                    return TermResult(g, 1, false);
                }
            }
        }
        return TermWaitOver(s) ? TermResult(g, 1, true) : TermResult(g, 0, false);
    }

    // 06012270 (test 9): the other crouching (Check_Specific_Term's 0x060135DE: 29, 33, 32)
    bool TestOtherCrouching(Guest &g) {
        Frame frame(g, 4);
        g.R(3) = 32;
        g.R(2) = 0x060135DE;
        g.R(7) = 33;
        g.R(6) = 29;
        g.R(15) -= 4;
        g.Write32(g.R(15), 32);
        g.Call(0x060135DE);
        g.R(15) += 4;
        return g.R(0) != 0 ? TermResult(g, 1, false) : TermResult(g, 0, true);
    }

    // The computer's super art (its word: 0-2), through the fighters' table (r1 its place, r3 kFighterSize, r0 the
    // side's offset)
    uint32_t ArtByTable(Guest &g, uint32_t me) {
        g.R(3) = kFighterSize;
        g.R(1) = kFighters + kSuperArt;
        g.R(0) = Signed(static_cast<int16_t>(static_cast<int16_t>(g.Read16(me + kSide)) * kFighterSize));
        return g.Read32(g.R(1) + g.R(0));
    }

    // 06012292 (test 10): met unless the computer is Oro (kChosen 9) with his super art ready (byte +10 -1): then
    // its art's word (0, the first, not met)
    bool TestNotOroReady(Guest &g) {
        const uint32_t me = g.R(4);
        if (static_cast<int8_t>(g.Read8(g.Read32(me + kSuperArt) + 10)) != -1) {
            return TermResult(g, 1, false);
        }
        g.R(1) = kChosen;
        if (g.Read8(kChosen + Signed(static_cast<int16_t>(g.Read16(me + kSide)))) != 9) {
            return TermResult(g, 1, false);
        }
        g.R(2) = ArtByTable(g, me);
        const uint32_t art = Signed(static_cast<int16_t>(g.Read16(g.R(2))));
        return TermResult(g, art, art == 0);
    }

    // 060122F4 (test 11): the computer at 48 health or less
    [[maybe_unused]] bool TestLowHealth(Guest &g) {
        const uint32_t me = g.R(4);
        g.R(3) = kFighterSize;
        g.R(1) = kFighters + kHealth;
        g.R(0) = 48;
        g.R(2) = Signed(static_cast<int16_t>(g.Read16(g.R(1) + Signed(static_cast<int16_t>(static_cast<int16_t>(g.Read16(me + kSide)) * kFighterSize)))));
        return static_cast<int32_t>(g.R(2)) > 48 ? TermResult(g, 0, true) : TermResult(g, 1, false);
    }

    // 06012316 (test 12): met unless the other's pose is 32 or 33 (low) or 2
    bool TestOtherNotLow(Guest &g) {
        g.R(4) = g.Read8(g.R(5) + kPose);
        g.R(0) = g.R(4);
        const bool low = g.R(4) == 32 || g.R(4) == 33 || g.R(4) == 2;
        return low ? TermResult(g, 0, true) : TermResult(g, 1, false);
    }

    // 0601238E (test 15): 120 frames (its own phase, kPlanPlace 1: 0 the start), then met; before them, with the other
    // 112 or more away, the other plan with the lever 64 (-1). A later phase returns as it is.
    [[maybe_unused]] bool TestWait120(Guest &g) {
        Side s(g, g.R(4));
        g.R(6) = kTimer;
        g.R(5) = kPlanPlace;
        g.R(0) = Signed(s.Place(1));
        if (s.Place(1) == 0) {
            g.R(3) = 120;
            g.R(5) = s.PlaceAt(0);
            s.SetPlace(1, s.Place(1) + 1);
            s.SetWord(kTimer, 120);
        } else if (s.Place(1) != 1) {
            return TermResult(g, g.R(0), false);
        }
        if (TermWaitOver(s)) {
            return TermResult(g, 1, true);
        }
        g.R(2) = 112;
        g.R(1) = kGap;
        g.R(3) = Signed(s.Word(kGap));
        if (!(static_cast<int32_t>(g.R(3)) >= 112)) {
            return TermResult(g, 1, false);
        }
        g.R(3) = 64;
        g.R(2) = s.At(kLever, 2);
        s.SetWord(kLever, 64);
        g.R(0) = Signed(s.side * 2);
        return TermResult(g, Signed(-1), false);
    }

    // 06012410, 0601242C (tests 16, 17): the computer's super art the third (2) or the second (1): that word, else 0
    template <int16_t want>
    [[maybe_unused]] bool TestArt(Guest &g) {
        const int16_t art = static_cast<int16_t>(g.Read16(ArtByTable(g, g.R(4))));
        return art == want ? TermResult(g, Signed(art), true) : TermResult(g, 0, false);
    }

    // 06012448 (test 18): the computer's super art the first (0): then what is left in r0, the side's offset in the
    // fighters' table (so for player 2 only: player 1's is 0), else 0
    [[maybe_unused]] bool TestFirstArt(Guest &g) {
        g.R(2) = ArtByTable(g, g.R(4));
        g.R(2) = Signed(static_cast<int16_t>(g.Read16(g.R(2))));
        return g.R(2) == 0 ? TermResult(g, g.R(0), true) : TermResult(g, 0, false);
    }

    // 060121D4 (ETC_Term, r5 a test, r6 a mode, r7 a plan): one of the tests at 0x0613E888 (r4 the computer, r5 the
    // other): -1, not yet; 0, that plan instead (Next_Another_Menu, at once); else the plan's next step, at once
    // (its phase and words, the parry and limit flags cleared). Leaves the registers as the routine does.
    bool EtcTermLift(Guest &g) {
        Frame frame(g, 20);
        Side s(g, g.R(4));
        const uint16_t test = static_cast<uint16_t>(g.R(5));
        const uint32_t mode = g.R(6);
        const uint16_t plan = static_cast<uint16_t>(g.R(7));
        g.R(1) = kEtcTests;
        g.R(5) = s.Other();
        g.R(0) = Signed(static_cast<int16_t>(test)) << 2;
        g.R(3) = g.Read32(kEtcTests + g.R(0));
        g.R(4) = s.me;
        g.Call(g.R(3));
        g.R(4) = g.R(0);
        g.R(0) = Signed(static_cast<int16_t>(g.R(4)));
        if (g.R(0) == Signed(-1)) {
            g.SetT(true);
            g.Return(30);
            return true;
        }
        g.R(4) = Signed(static_cast<int16_t>(g.R(4)));
        g.R(5) = kRunAgain;
        g.R(6) = 1;
        g.R(0) = Signed(s.side);
        s.SetByte(kRunAgain, 1);
        if (g.R(4) == 0) {
            g.R(4) = s.me;
            g.R(3) = kAnotherPlan;
            g.R(6) = Signed(static_cast<int16_t>(plan));
            g.R(5) = mode;
            g.SetT(true);
            frame.Pop();
            g.Call(kAnotherPlan);
            g.Return(40);
            return true;
        }
        g.R(4) = 0;
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kParryFlag, 0);
        s.SetByte(kLimited, 0);
        g.R(5) = s.PlaceAt(0);
        g.R(3) = s.At(kLimited, 1);
        g.R(0) = Signed(s.side);
        g.SetT(false);
        g.Return(40);
        return true;
    }

    // 0600B19C (EM_Term, r5 Range_X, r6 Range_Y, r7 a test of the fight; on the stack the mode and the plan to go
    // to): wait (holding kLeverNow, unless an answer begins) until the test (0x06011C10, by its number) says: 0 the
    // gap and the height within the ranges (the other's height, or the computer's own for test 8): the plan's next
    // step; 1 the other plan; 3 and up a reaction (Select_Passive; up from 4 as a counter attack); 2 go on waiting
    bool UntilTermLift(Guest &g) {
        Frame frame(g, 40);
        Side s(g, g.R(4));
        const int16_t rangeX = static_cast<int16_t>(g.R(5)), rangeY = static_cast<int16_t>(g.R(6));
        const int16_t test = static_cast<int16_t>(g.R(7));
        const int16_t nextMode = static_cast<int16_t>(g.Read16(frame.entry + 2));
        const int16_t nextPlan = static_cast<int16_t>(g.Read16(frame.entry + 6));
        const uint32_t other = s.Other();
        s.SetWord(kLever, s.Word(kLeverNow));
        switch (s.Place(1)) {
        case 0:
            s.SetPlace(1, s.Place(1) + 1);
            s.SetWord(kTermTimer, 0);
            break;
        case 1:
            break;
        default:
            g.R(0) = Signed(s.Place(1));
            g.Return(40);
            return true;
        }
        if (Call(g, kCheckAnswer, s.me) != 0) {
            g.Return(40);
            return true;
        }
        CallWith(g, kTermTest, s.me, other, Signed(test));
        switch (static_cast<int32_t>(g.R(0))) {
        case 0:
            CallWith(g, kMeetsLimit, s.me, Signed(s.Word(kGap)), Signed(rangeX));
            if (g.R(0) == 0) {
                break;
            }
            if (test == 8) {
                CallWith(g, kMeetsLimit, s.me, Signed(s.Field(kY)), Signed(rangeY));
            } else {
                CallWith(g, kOtherHeightMeetsLimit, s.me, Signed(static_cast<int16_t>(g.Read16(other + kY))), Signed(rangeY));
            }
            if (g.R(0) == 0) {
                break;
            }
            s.SetByte(kRunAgain, 1);
            s.SetPlace(0, s.Place(0) + 1);
            for (int n = 1; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            s.SetByte(kParryFlag, 0);
            s.SetByte(kLimited, 0);
            g.R(0) = Signed(s.side);
            break;
        case 1:
            s.SetByte(kRunAgain, 1);
            frame.Pop();
            CallWith(g, kAnotherPlan, s.me, Signed(nextMode), Signed(nextPlan));
            break;
        case 2:
            break;
        default:
            if (static_cast<int32_t>(g.R(0)) != 3) {
                s.SetByte(kCounter, 1);
            }
            frame.Pop();
            Call(g, kReact, s.me);
            break;
        }
        g.Return(40);
        return true;
    }

    // 0600A07E (SA_Term, r5-r7 a plan for each super art, on the stack a test of the range): the super art's plan,
    // when it may be used (ready, and the fighter free to: byte 0x471 clear): some characters' only within a range
    // (SA_Range_Check), Necro's only against the other low enough, Elena's third only at half health or less, Ryu's
    // and Oro's with their own checks. Else, or the art having no plan (-1), the plan's next step.
    bool SuperArtTermLift(Guest &g) {
        Frame frame(g, 44);
        Side s(g, g.R(4));
        const uint32_t stack = g.R(15);
        const uint32_t plans = stack + 12; // (three words: the plans, as the routine keeps them on the stack)
        const uint16_t art2 = static_cast<uint16_t>(g.R(7));
        g.Write16(stack, static_cast<uint16_t>(g.R(5)));
        g.Write16(stack + 4, static_cast<uint16_t>(g.R(6)));
        g.Write16(stack + 8, art2);
        const uint16_t range = g.Read16(frame.entry + 2);
        const auto done = [&]() {
            g.Return(60);
            return true;
        };
        if (s.Byte(kAnswering) == 0 && Call(g, kCheckAnswer, s.me) != 0) {
            return done();
        }
        g.Write16(plans, g.Read16(stack));
        g.Write16(plans + 2, g.Read16(stack + 4));
        g.Write32(stack + 4, plans + 4);
        g.Write16(plans + 4, g.Read16(stack + 8));
        s.SetWord(kLever, s.Word(kLeverNow));
        const uint32_t fighter = kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize));
        g.Write32(stack, fighter);
        const auto planOfArt = [&]() {
            return Signed(static_cast<int16_t>(g.Read16(plans + Signed(static_cast<int16_t>(g.Read16(g.Read32(fighter + kSuperArt))) * 2))));
        };
        const auto nextStep = [&]() {
            s.SetPlace(0, s.Place(0) + 1);
            g.R(0) = Signed(s.side * 16);
            return done();
        };
        if (static_cast<int32_t>(planOfArt()) == -1 || g.Read8(fighter + 0x471) != 0) {
            return nextStep();
        }
        const uint32_t art = g.Read32(fighter + kSuperArt);
        g.Write32(stack, art);
        if (g.Read8(art + 10) == 0 && g.Read8(art + 9) == 0) {
            return nextStep();
        }
        s.SetByte(kRunAgain, 1);
        const auto inRange = [&](uint32_t which) {
            CallWith(g, kSuperArtRange, s.me, which, range);
            return g.R(0) != 0;
        };
        switch (s.Character()) {
        case 1: // (Alex)
        case 11: // (Ken)
            if (inRange(1)) {
                return done();
            }
            break;
        case 2: // (Ryu: the Denjin's charge)
            if (inRange(1)) {
                return done();
            }
            g.R(7) = range;
            CallWith(g, kDenjinCheck, s.me, art2, plans + 4);
            break;
        case 5: // (Necro)
            if (static_cast<int16_t>(g.Read16(s.Other() + kY)) >= 16) {
                return nextStep();
            }
            if (inRange(1)) {
                return done();
            }
            break;
        case 6: // (Hugo)
            if (inRange(0)) {
                return done();
            }
            break;
        case 8: { // (Elena)
            const uint32_t me = kFighters + Signed(static_cast<int16_t>(s.side * kFighterSize));
            if (static_cast<int16_t>(g.Read16(g.Read32(me + kSuperArt))) != 2) {
                return nextStep();
            }
            const int16_t full = static_cast<int16_t>(g.Read16(kFullHealth));
            if (static_cast<int16_t>(g.Read16(me + kHealth)) > static_cast<int16_t>(full / 2)) {
                return nextStep();
            }
            break;
        }
        case 9: // (Oro: the Yagyou's)
            g.R(5) = plans + 2;
            CallWith(g, kYagyouCheck, s.me, plans + 2, range);
            break;
        case 14:
        case 15: // (Akuma, Shin Akuma)
            if (inRange(1) || inRange(2)) {
                return done();
            }
            break;
        }
        CallWith(g, kAnotherPlan, s.me, 2, planOfArt());
        return done();
    }


    // 0600942C (Check_Arrival, r5 a gap, r6 the way: 3 away): far enough away (the gap at least r5, or pressed
    // against the other: kStuck), or near enough (the gap r5 or less, or kPushed)
    bool ArrivedLift(Guest &g) {
        Side s(g, g.R(4));
        const int16_t target = static_cast<int16_t>(g.R(5));
        g.R(7) = kGap;
        if (static_cast<int16_t>(g.R(6)) == 3) {
            g.R(3) = Signed(s.Word(kGap));
            g.R(0) = target > s.Word(kGap) ? s.FieldByte(kStuck) : 1u;
        } else if (s.FieldByte(kPushed) != 0) {
            g.R(0) = 1;
        } else {
            g.R(3) = Signed(s.Word(kGap));
            g.R(0) = target >= s.Word(kGap) ? 1 : 0;
        }
        g.Return(16);
        return true;
    }

    // 060092EA (Approach_Walk, r5 the gap to reach, r6 3 to walk away, else toward): walk (the lever toward the
    // other, or away) until the gap is reached (Check_Arrival): then the plan's next step; given up (free again) after
    // 120 frames, or as an answer begins (but while the other lies on the floor). The other's standing time is kept
    // through Check_Passive. Leaves the registers as the routine does.
    bool ApproachLift(Guest &g) {
        Frame frame(g, 28);
        Side s(g, g.R(4));
        const int16_t target = static_cast<int16_t>(g.R(5)), way = static_cast<int16_t>(g.R(6));
        switch (s.Place(1)) {
        case 0:
            s.SetPlace(1, s.Place(1) + 1);
            Call(g, kClearDash, Signed(s.side));
            s.SetWord(kTimer, 120);
            break;
        case 1:
            break;
        default:
            g.R(0) = Signed(s.Place(1));
            g.R(4) = kPlanPlace;
            g.SetT(false);
            g.Return(30);
            return true;
        }
        const int16_t standing = static_cast<int8_t>(s.Byte(kStanding));
        if (s.Byte(kOtherLying) == 0 && Call(g, kCheckAnswer, s.me) != 0) {
            g.SetT(false);
            g.Return(30);
            return true;
        }
        s.SetByte(kStanding, standing);
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        g.R(1) = kTimer;
        g.R(2) = s.At(kTimer, 2);
        if (s.Word(kTimer) == 0) {
            frame.Pop();
            Call(g, kBeFree, s.me);
            g.Return(30);
            return true;
        }
        CallWith(g, 0x0600942C, s.me, Signed(target), Signed(way));
        if (g.R(0) != 0) {
            s.SetByte(kRunAgain, 1);
            s.SetPlace(0, s.Place(0) + 1);
            for (int n = 1; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            s.SetByte(kParryFlag, 0);
            s.SetByte(kLimited, 0);
            g.R(1) = kMode;
            g.R(2) = s.At(kRunAgain, 1);
            g.R(4) = 0;
            if (s.ModeWord(0) == 6) {
                g.R(0) = 6;
                g.R(3) = s.At(kLimited, 1);
                g.SetT(true);
            } else {
                s.SetByte(kAnswering, 0);
                g.R(0) = Signed(s.side);
                g.R(3) = s.At(kAnswering, 1);
                g.SetT(false);
            }
            g.Return(30);
            return true;
        }
        g.R(4) = s.me;
        g.Call(0x0600FCEE);
        if (way == 3) {
            s.SetWord(kLever, s.Word(kLever) ^ 12);
            g.R(0) = Signed(s.Word(kLever));
            g.R(1) = s.At(kLever, 2);
            g.R(3) = Signed(s.side * 2);
            g.SetT(false);
        } else {
            g.R(0) = Signed(way);
            g.SetT(false);
        }
        g.Return(30);
        return true;
    }

    // The stage's edges as the computer sees them: the screen (0x02026C90: x of its left edge +102, its right +106)
    // and a margin (0x02026BD4)
    int32_t RightEdge(Guest &g) {
        return static_cast<int16_t>(g.Read16(kScreen + 106)) - static_cast<int16_t>(g.Read16(kScreenMargin));
    }
    int32_t LeftEdge(Guest &g) {
        return static_cast<int16_t>(g.Read16(kScreen + 102)) + static_cast<int16_t>(g.Read16(kScreenMargin));
    }

    // 06009D42 (Check_Squat): 0: nothing to see (Search_Back_Term asks it; the arcade's build has nothing more here)
    [[maybe_unused]] bool SquatCheckLift(Guest &g) {
        g.R(0) = 0;
        g.Return(4);
        return true;
    }

    // 06009242 (Search_Back_Term, r5 a distance, r6 a mode, r7 a plan): no room behind the computer for the distance
    // (the screen's edge): that plan instead; else the plan's next step. The lever kLeverNow. Leaves the registers as
    // the routine does.
    bool RoomBehindLift(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        const int32_t distance = static_cast<int32_t>(g.R(5));
        const int16_t mode = static_cast<int16_t>(g.R(6)), plan = static_cast<int16_t>(g.R(7));
        const int16_t x = s.Field(kX);
        bool noRoom;
        if (s.FieldByte(0x0B) != 0) {
            const int16_t back = static_cast<int16_t>(x - distance);
            g.R(1) = Signed(RightEdge(g));
            g.R(2) = Signed(x - distance);
            g.R(5) = Signed(back);
            noRoom = static_cast<int32_t>(g.R(1)) > back;
        } else {
            const int16_t back = static_cast<int16_t>(x + distance);
            g.R(1) = Signed(x);
            g.R(2) = Signed(LeftEdge(g));
            g.R(5) = Signed(back);
            noRoom = !(static_cast<int32_t>(g.R(2)) >= back);
        }
        if (noRoom) {
            g.R(1) = kAnotherPlan;
            CallWith(g, kAnotherPlan, s.me, Signed(mode), Signed(plan));
        } else {
            s.SetPlace(0, s.Place(0) + 1);
            g.R(2) = Signed(s.Place(0));
        }
        s.SetWord(kLever, s.Word(kLeverNow));
        g.R(0) = kLever;
        g.R(3) = Signed(s.Word(kLever));
        g.R(4) = Signed(s.side * 2);
        g.SetT(false);
        g.Return(30);
        return true;
    }

    // 060091C0 (Setup_KA_Jump): to keep away, jump back (Place 3: 2), or, with no room behind for the character's
    // jump, forward over the other (1). Leaves the registers as the routine does.
    bool KeepAwayJumpLift(Guest &g) {
        Side s(g, g.R(4));
        s.SetPlace(3, 2);
        const int16_t jump = static_cast<int16_t>(g.Read16(kJumpLengths + Signed(s.Character() * 2)));
        const int16_t x = s.Field(kX);
        g.R(3) = kScreenMargin;
        g.R(6) = kScreen;
        g.R(7) = Signed(static_cast<int16_t>(g.Read16(kScreenMargin)));
        g.R(1) = kPlanPlace;
        bool over;
        if (s.FieldByte(0x0B) != 0) {
            const int16_t back = static_cast<int16_t>(x - jump);
            g.R(0) = 106;
            g.R(2) = Signed(RightEdge(g));
            g.R(5) = Signed(back);
            over = static_cast<int32_t>(g.R(2)) > back;
            g.SetT(over);
        } else {
            const int16_t back = static_cast<int16_t>(x + jump);
            g.R(0) = 102;
            g.R(2) = Signed(LeftEdge(g));
            g.R(5) = Signed(back);
            over = !(static_cast<int32_t>(g.R(2)) >= back);
            g.SetT(!over);
        }
        if (over) {
            s.SetPlace(3, 1);
            g.R(1) = s.PlaceAt(0);
            g.R(0) = 1;
        }
        g.Return(20);
        return true;
    }

    // 06009232 (Setup_KA_Walk): to keep away, walk back (Place 3: 4)
    bool KeepAwayWalkLift(Guest &g) {
        Side s(g, g.R(4));
        s.SetPlace(3, 4);
        g.R(0) = Signed(s.side * 16);
        g.R(2) = s.PlaceAt(3);
        g.R(3) = 4;
        g.Return(8);
        return true;
    }

    // 06009100 (Keep_Away, r5 a gap, r6 how: 0 at random, 1 jumping, else walking): get away from the other to the
    // gap: first how (kept in Place 3: 1 jump forward, 2 back, 3 and 4 walk), then the jump (Jump) or the walk
    // (Approach_Walk, away)
    bool KeepAwayLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const int16_t target = static_cast<int16_t>(g.R(5));
        const int16_t how = static_cast<int16_t>(g.R(6));
        switch (s.Place(3)) {
        case 0:
            if (how == 0) {
                if (Dice(g, kDice64) >= 4) {
                    Call(g, 0x06009232, s.me);
                } else {
                    Call(g, 0x060091C0, s.me);
                }
            } else if (how == 1) {
                Call(g, 0x060091C0, s.me);
            } else {
                s.SetPlace(3, how + 1);
            }
            [[fallthrough]];
        case 1:
        case 2:
            frame.Pop();
            g.R(4) = s.me;
            g.R(5) = Signed(s.Place(3) - 1);
            g.Call(kJump);
            break;
        case 3:
        case 4:
            frame.Pop();
            CallWith(g, 0x060092EA, s.me, Signed(target), Signed(s.Place(3) - 1));
            break;
        default:
            g.R(0) = Signed(s.Place(3));
            g.SetT(false);
            break;
        }
        g.Return(30);
        return true;
    }


    // 0600B558 (Jump, r5 the way: 0 toward, 2 straight up, else away): a jump, in steps (Place 1): 0 holding
    // kLeverNow, once any attack is over, the high jump's command read again and the frames before it chosen
    // (Check_First_Menu); 1 those frames (kComboSpeed), then the jump's lever (Jump_Init), free again at once against
    // a projectile from above (Check_Diagonal_Shell); 2 the jump held until off the floor, then guarding in the air
    // (Check_Air_Guard); then, back on the floor, the plan's next step. An answer may break it off in the first two.
    // Leaves the registers as the routine does.
    bool JumpLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const uint32_t way = g.R(5);
        g.R(7) = kLeverNow;
        g.R(6) = kLever;
        g.R(4) = kLever;
        g.R(5) = kLeverNow;
        const auto done = [&]() {
            g.Return(40);
            return true;
        };
        const auto holdLeverNow = [&]() {
            g.R(6) = Signed(s.side * 2);
            g.R(4) = s.At(kLever, 2);
            g.R(5) = s.At(kLeverNow, 2);
            g.R(3) = Signed(s.Word(kLeverNow));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
        };
        const auto answered = [&]() {
            g.R(2) = kCheckAnswer;
            if (Call(g, kCheckAnswer, s.me) != 0) {
                g.SetT(false);
                return true;
            }
            return false;
        };
        switch (s.Place(1)) {
        case 0:
            holdLeverNow();
            if (answered()) {
                return done();
            }
            if (s.Field(kDoing) == kDoingAttack && s.FieldByte(kMoveKind) != 64) {
                g.R(0) = s.FieldByte(kMoveKind);
                g.SetT(false);
                return done();
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(3) = kClearHighJump;
            Call(g, kClearHighJump, Signed(s.side));
            frame.Pop();
            g.R(2) = kFirstMenu;
            Call(g, kFirstMenu, s.me);
            return done();
        case 1: {
            if (answered()) {
                return done();
            }
            const int32_t frames = static_cast<int8_t>(s.Byte(kComboSpeed)) - 1;
            s.SetByte(kComboSpeed, frames);
            g.R(0) = Signed(s.side);
            g.R(1) = kComboSpeed;
            g.R(2) = s.At(kComboSpeed, 1);
            g.R(3) = Signed(static_cast<int8_t>(frames));
            if (static_cast<int8_t>(frames) != 0) {
                g.SetT(false);
                return done();
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(3) = kStartJump;
            g.R(5) = Signed(static_cast<int16_t>(way));
            Call(g, kStartJump, s.me);
            g.R(2) = kShellAbove;
            if (Call(g, kShellAbove, s.me) == 0) {
                g.SetT(true);
                return done();
            }
            frame.Pop();
            g.R(3) = kBeFree;
            Call(g, kBeFree, s.me);
            return done();
        }
        case 2:
            g.R(5) = Signed(s.side * 2);
            g.R(3) = Signed(s.Word(kJumpLever));
            g.R(4) = s.At(kLever, 2);
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.R(0) = 104;
            g.R(2) = Signed(s.Field(kY));
            if (!(s.Field(kY) > 0)) {
                g.SetT(false);
                return done();
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(6) = s.At(kLever, 2);
            g.R(7) = s.At(kLeverNow, 2);
            g.R(3) = Signed(s.Word(kLeverNow));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.R(0) = Signed(s.side);
            frame.Pop();
            Call(g, kAirGuard, s.me);
            return done();
        default:
            holdLeverNow();
            g.R(0) = Signed(s.Field(kY));
            if (s.Field(kY) != 0) {
                g.SetT(false);
                return done();
            }
            s.SetPlace(0, s.Place(0) + 1);
            for (int n = 1; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            g.R(0) = 0;
            g.R(3) = s.PlaceAt(2);
            g.R(4) = 0;
            g.SetT(true);
            return done();
        }
    }


    // 0600B93C (Check_Start_Hi_Jump): 1 while a high jump may not start: in an attack (kDoing 4), not at its end, that
    // may be cancelled neither into a normal (kCancel 4) nor into a high jump (1). Leaves T as the routine does.
    bool HighJumpBusy(Guest &g, uint32_t me) {
        const auto result = [&](uint32_t r0, bool t) {
            g.R(0) = r0;
            g.SetT(t);
            return r0 != 0;
        };
        if (static_cast<int16_t>(g.Read16(me + kDoing)) != kDoingAttack) {
            return result(0, false);
        }
        if (g.Read8(me + kMoveKind) == 64) {
            return result(0, true);
        }
        const uint8_t cancel = g.Read8(me + kCancel);
        if ((cancel & 4) || (cancel & 1)) {
            return result(0, false);
        }
        return result(1, true);
    }
    bool HighJumpBusyLift(Guest &g) {
        HighJumpBusy(g, g.R(4));
        g.Return(14);
        return true;
    }

    // 0600B6F6 (Hi_Jump, r5 the character, r6 the way: as Jump's): a high jump, in steps (Place 1): 0 holding
    // kLeverNow, once the fighter may (Check_Start_Hi_Jump), the character's high jump command (its commands' third)
    // made the one to put in and the frames before it chosen (Check_First_Menu); 1 those frames (kComboSpeed), then
    // the jump's lever (Jump_Init), free again at once against a projectile from above (Check_Diagonal_Shell), the
    // lever let go; 2 the command put in (Command_Type_00), then the jump's lever with it (or with it once the
    // command's lever leaves bit 2 clear); 3 the jump held until off the floor, then guarding in the air
    // (Check_Air_Guard); then, back on the floor, the plan's next step. An answer may break it off in the first three.
    // Leaves the registers as the routine does.
    bool HighJumpLift(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const uint32_t character = g.R(5);
        const uint32_t way = g.R(6);
        g.R(0) = Signed(s.Place(1));
        g.R(4) = kLever;
        g.R(6) = kLeverNow;
        g.R(5) = kLeverNow;
        const auto done = [&]() {
            g.Return(60);
            return true;
        };
        const auto holdLeverNow = [&]() {
            g.R(0) = Signed(s.side);
            g.R(6) = Signed(s.side * 2);
            g.R(4) = s.At(kLever, 2);
            g.R(5) = s.At(kLeverNow, 2);
            g.R(3) = Signed(s.Word(kLeverNow));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.SetT(false);
        };
        const auto answered = [&]() {
            g.R(2) = kCheckAnswer;
            if (Call(g, kCheckAnswer, s.me) != 0) {
                g.SetT(false);
                return true;
            }
            return false;
        };
        // (kLever with kJumpLever's bits too)
        const auto addJumpLever = [&]() {
            g.R(0) = Signed(s.side);
            g.R(4) = Signed(s.side * 2);
            g.R(3) = s.At(kLever, 2);
            g.R(1) = Signed(static_cast<int16_t>(s.Word(kLever) | s.Word(kJumpLever)));
            g.R(2) = Signed(s.Word(kJumpLever));
            s.SetWord(kLever, static_cast<int32_t>(g.R(1)));
            g.SetT(false);
        };
        switch (s.Place(1)) {
        case 0:
            holdLeverNow();
            if (answered()) {
                return done();
            }
            g.R(4) = s.me;
            if (HighJumpBusy(g, s.me)) {
                g.SetT(false);
                return done();
            }
            g.SetT(true);
            s.SetPlace(1, s.Place(1) + 1);
            g.R(4) = s.me;
            g.R(1) = kCommands;
            g.R(0) = Signed(static_cast<int16_t>(character)) << 2;
            g.R(2) = s.At(kTechAddress, 4);
            g.R(3) = g.Read32(g.Read32(kCommands + g.R(0)) + 8);
            g.Write32(g.R(2), g.R(3));
            g.R(2) = kFirstMenu;
            frame.Pop();
            g.Call(kFirstMenu);
            return done();
        case 1: {
            if (answered()) {
                return done();
            }
            const int32_t frames = static_cast<int8_t>(s.Byte(kComboSpeed)) - 1;
            s.SetByte(kComboSpeed, frames);
            g.R(0) = Signed(s.side);
            g.R(1) = kComboSpeed;
            g.R(2) = s.At(kComboSpeed, 1);
            g.R(3) = Signed(static_cast<int8_t>(frames));
            if (static_cast<int8_t>(frames) != 0) {
                g.SetT(false);
                return done();
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(2) = s.At(kTechIndex, 2);
            g.R(3) = 12;
            s.SetWord(kTechIndex, 12);
            g.R(0) = Signed(s.side * 2);
            g.R(3) = kStartJump;
            g.R(5) = Signed(static_cast<int16_t>(way));
            Call(g, kStartJump, s.me);
            g.R(2) = kShellAbove;
            if (Call(g, kShellAbove, s.me) != 0) {
                g.R(3) = kBeFree;
                Call(g, kBeFree, s.me);
            }
            g.R(0) = Signed(s.side * 2);
            s.SetWord(kLever, 0);
            g.SetT(false);
            return done();
        }
        case 2:
            if (answered()) {
                return done();
            }
            g.R(2) = kCommandStep0;
            g.R(7) = Signed(-1);
            g.R(6) = Signed(-1);
            g.R(5) = 8;
            if (Call(g, kCommandStep0, s.me) == Signed(-1)) {
                s.SetPlace(1, s.Place(1) + 1);
                g.R(0) = Signed(s.side * 16);
                addJumpLever();
                return done();
            }
            g.R(0) = Signed(s.Word(kLever)) & 0xFFFF;
            if (g.R(0) & 2) {
                g.SetT(false);
                return done();
            }
            addJumpLever();
            return done();
        case 3:
            g.R(0) = Signed(s.side);
            g.R(5) = Signed(s.side * 2);
            g.R(4) = s.At(kLever, 2);
            g.R(3) = Signed(s.Word(kJumpLever));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.R(0) = 104;
            g.R(2) = Signed(s.Field(kY));
            g.SetT(false);
            if (!(s.Field(kY) > 0)) {
                return done();
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(0) = Signed(s.side);
            g.R(6) = s.At(kLeverNow, 2);
            g.R(3) = Signed(s.Word(kLeverNow));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.R(4) = s.me;
            frame.Pop();
            Call(g, kAirGuard, s.me);
            return done();
        default:
            holdLeverNow();
            Call(g, kAirGuard, s.me);
            g.R(0) = Signed(s.Field(kY));
            if (s.Field(kY) != 0) {
                g.SetT(false);
                return done();
            }
            s.SetPlace(0, s.Place(0) + 1);
            s.SetPlace(1, 0);
            s.SetPlace(2, 0);
            s.SetPlace(3, 0);
            g.R(0) = 0;
            g.R(3) = s.PlaceAt(2);
            g.SetT(true);
            return done();
        }
    }


    // A step's routine done: the plan on to its next step, the routine's words cleared, the parry and limit flags
    // cleared, and on to it at once (as EM_Term and the like do)
    void StepDone(Side &s) {
        s.SetByte(kRunAgain, 1);
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kParryFlag, 0);
        s.SetByte(kLimited, 0);
    }

    // 0600B30E (SHELL_Term, r5 a row of the limits, r6 a test, r7 a mode; on the stack a plan): wait for the
    // projectile against (kShell): the test (0x06011C10) -1: that plan instead; else once it is to arrive within the
    // character's limit (the row r5 of kShellLimits), the plan's next step. An answer may break it off.
    void UntilShell(Side &s, int16_t row, int16_t test, int16_t mode, int16_t plan) {
        Guest &g = s.g;
        const uint32_t shell = g.Read32(s.At(kShell, 4));
        s.SetWord(kLever, s.Word(kLeverNow));
        switch (s.Place(1)) {
        case 0:
            s.SetPlace(1, s.Place(1) + 1);
            s.SetWord(kTermTimer, 0);
            break;
        case 1:
            break;
        default:
            return;
        }
        if (Call(g, kCheckAnswer, s.me) != 0) {
            return;
        }
        CallWith(g, kTermTest, s.me, shell, Signed(test));
        if (static_cast<int32_t>(g.R(0)) == -1) {
            CallWith(g, kAnotherPlan, s.me, Signed(mode), Signed(plan));
            return;
        }
        CallWith(g, 0x0601176E, s.me, shell);
        const int16_t frames = static_cast<int16_t>(g.R(0));
        if (frames >= g.Read8(kShellLimits + Signed(row * 24) + Signed(s.Character()))) {
            return;
        }
        StepDone(s);
    }
    bool UntilShellLift(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        UntilShell(s, static_cast<int16_t>(g.R(5)), static_cast<int16_t>(g.R(6)), static_cast<int16_t>(g.R(7)),
                   static_cast<int16_t>(g.Read16(frame.entry + 2)));
        g.Return(40);
        return true;
    }

    // 0600A4AE (Check_SA), 0600A522 (Check_EX), 0600A5C0 (Check_SA_Full) (r5 a mode, r6 a plan): go on with the plan
    // only when the super art may be used (its byte 10), or an EX move (byte 12), or the gauge is full (bit 64 of the
    // fighter's word 0x46C); else that plan instead. The lever kLeverNow.
    void OnlyWithGauge(Side &s, bool ready, int16_t mode, int16_t plan) {
        if (ready) {
            s.SetPlace(0, s.Place(0) + 1);
        } else {
            s.SetModeWord(0, mode);
            s.SetByte(kRunAgain, 1);
            CallWith(s.g, kAnotherPlan, s.me, Signed(mode), Signed(plan));
        }
        s.SetWord(kLever, s.Word(kLeverNow));
    }
    uint32_t SuperArtOf(Side &s) {
        return s.g.Read32(kFighters + kSuperArt + Signed(static_cast<int16_t>(s.side * kFighterSize)));
    }
    [[maybe_unused]] bool SuperArtReadyLift(Guest &g) {
        Frame frame(g, 20);
        Side s(g, g.R(4));
        OnlyWithGauge(s, g.Read8(SuperArtOf(s) + 10) != 0, static_cast<int16_t>(g.R(5)), static_cast<int16_t>(g.R(6)));
        g.Return(30);
        return true;
    }
    [[maybe_unused]] bool ExReadyLift(Guest &g) {
        Frame frame(g, 20);
        Side s(g, g.R(4));
        OnlyWithGauge(s, g.Read8(SuperArtOf(s) + 12) != 0, static_cast<int16_t>(g.R(5)), static_cast<int16_t>(g.R(6)));
        g.Return(30);
        return true;
    }
    bool GaugeFullLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        s.SetByte(kRunAgain, 1);
        const bool full = (static_cast<uint16_t>(s.Field(0x46C)) & 64) != 0;
        const int16_t mode = static_cast<int16_t>(g.R(5)), plan = static_cast<int16_t>(g.R(6));
        if (full) {
            s.SetPlace(0, s.Place(0) + 1);
        } else {
            s.SetModeWord(0, mode);
            CallWith(g, kAnotherPlan, s.me, Signed(mode), Signed(plan));
        }
        s.SetWord(kLever, s.Word(kLeverNow));
        g.Return(30);
        return true;
    }

    // 0600A79A (Branch_Wait_Area, r5-r7 and a word on the stack: frames for each range): wait as long as the range
    // says, then the plan's next step
    bool WaitByRangeLift(Guest &g) {
        Frame frame(g, 12);
        Side s(g, g.R(4));
        if (s.Place(1) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            const int16_t times[4] = {static_cast<int16_t>(g.R(5)), static_cast<int16_t>(g.R(6)), static_cast<int16_t>(g.R(7)),
                                      static_cast<int16_t>(g.Read16(frame.entry + 2))};
            s.SetWord(kTimer, times[s.Word(kRange) & 3]);
        } else {
            s.SetWord(kTimer, s.Word(kTimer) - 1);
            if (s.Word(kTimer) == 0) {
                s.SetPlace(0, s.Place(0) + 1);
                for (int n = 1; n < 4; n++) {
                    s.SetPlace(n, 0);
                }
                s.SetByte(kParryFlag, 0);
                s.SetByte(kLimited, 0);
                if (s.ModeWord(0) != 6) {
                    s.SetByte(kAnswering, 0);
                }
            }
        }
        g.Return(30);
        return true;
    }

    // 0600ADA0 (Wait_Get_Up, r5 a lever: 0 none, else back (and down with bit 2), r6 what to wait for): hold the
    // lever (kLeverNow) until the other has got up (Check_Wait_Term), then the plan's next step, as an answer. Leaves
    // the registers as the routine does.
    bool WaitGetUpLift(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const int16_t lever = static_cast<int16_t>(g.R(5)), what = static_cast<int16_t>(g.R(6));
        if (s.Place(1) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            g.R(3) = kClearDash;
            Call(g, kClearDash, Signed(s.side));
            s.SetByte(kGetUpSeen, 0);
            g.R(2) = Signed(lever);
            if (lever != 0) {
                s.SetWord(kLeverNow, static_cast<int32_t>(LeverFor(g, s.me, 1)));
                g.R(2) = Signed(s.Word(kLeverNow) | (lever & 2));
                s.SetWord(kLeverNow, static_cast<int32_t>(g.R(2)));
            } else {
                s.SetWord(kLeverNow, 0);
            }
        }
        s.SetWord(kLever, s.Word(kLeverNow));
        g.R(3) = Signed(s.Word(kLever));
        g.R(0) = Signed(what);
        CallWith(g, kOtherUp, s.me, Signed(what), g.R(6));
        if (g.R(0) == 0) {
            g.SetT(true);
            g.Return(40);
            return true;
        }
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kRunAgain, 1);
        s.SetByte(kAnswering, 1);
        s.SetByte(kParryFlag, 0);
        s.SetByte(kLimited, 0);
        g.R(0) = Signed(s.side);
        g.R(3) = s.At(kLimited, 1);
        g.R(4) = 1;
        g.SetT(false);
        g.Return(40);
        return true;
    }


    // 06009746 (Forced_Guard, r5 the guard): guard now (Next_Be_Guard; free again first when the other is not
    // attacking or its attack would not reach), holding down too if the lever was held down (kHeldDown). Leaves the
    // registers as the routine does.
    bool ForcedGuardLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        const int16_t guard = static_cast<int16_t>(g.R(5));
        const uint32_t other = s.Other();
        if (s.Byte(kOtherAttacking) == 0) {
            g.R(3) = kBeFree;
            Call(g, kBeFree, s.me);
        }
        const int16_t reach = static_cast<int16_t>(g.Read16(kReachByClass + g.Read8(other + kAttackReach) * 2u) + s.Word(kWidth));
        if (s.Word(kGap) > reach) {
            g.R(3) = kBeFree;
            Call(g, kBeFree, s.me);
        }
        g.R(3) = 0x06011248;
        CallWith(g, 0x06011248, s.me, other, Signed(guard));
        g.R(3) = Signed(s.Word(kHeldDown));
        g.R(2) = Signed(s.Word(kLever) | s.Word(kHeldDown));
        s.SetWord(kLever, static_cast<int32_t>(g.R(2)));
        g.R(0) = kLever;
        g.R(4) = Signed(s.side * 2);
        g.SetT(false);
        g.Return(40);
        return true;
    }

    // 060097B2 (Provoke, r5 a lever or -1): a taunt: the lever held from now on (back and down as r5 says), and
    // once the fighter may (bit 128 of its word 0x46C), the taunt's buttons (0x240); then, once it is no longer
    // taunting, the plan on (Reaction_Exit_Sub). An answer may break it off first. Leaves the registers as the
    // routine does.
    bool ProvokeLift(Guest &g) {
        Frame frame(g, 28);
        Side s(g, g.R(4));
        const int16_t lever = static_cast<int16_t>(g.R(5));
        const auto taunt = [&]() {
            g.R(0) = static_cast<uint16_t>(s.Field(0x46C));
            if (!(g.R(0) & 128)) {
                g.SetT(true);
                return;
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(3) = 0x240;
            g.R(2) = s.At(kLever, 2);
            s.SetWord(kLever, 0x240);
            g.R(0) = Signed(s.side * 2);
            g.SetT(false);
        };
        switch (s.Place(1)) {
        case 0:
            if (Call(g, kCheckAnswer, s.me) != 0) {
                g.SetT(false);
                break;
            }
            s.SetPlace(1, s.Place(1) + 1);
            g.R(1) = 2;
            g.R(2) = s.PlaceAt(1);
            g.R(3) = Signed(s.Place(1));
            if (lever != -1) {
                s.SetWord(kLeverNow, static_cast<int32_t>(LeverFor(g, s.me, Signed(lever & 1))));
                g.R(3) = s.At(kLeverNow, 2);
                g.R(2) = Signed(s.Word(kLeverNow) | (lever & 2));
                s.SetWord(kLeverNow, static_cast<int32_t>(g.R(2)));
            }
            taunt();
            break;
        case 1:
            taunt();
            break;
        default:
            g.R(3) = Signed(s.Word(kLeverNow));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.R(4) = s.me + 0x24;
            g.R(0) = Signed(s.Field(kDoing));
            if (s.Field(kDoing) == kDoingAttack) {
                g.R(0) = Signed(s.Field(kDoingHow));
                if (s.Field(kDoingHow) == 30) {
                    g.SetT(true);
                    break;
                }
            }
            frame.Pop();
            g.R(2) = kReactionExit;
            Call(g, kReactionExit, s.me);
            break;
        }
        g.Return(40);
        return true;
    }

    // 0600AA6C (Keep_Status, r5 a lever, r6 a way or -1): hold the lever (kFreeLever, and the way's) for 10 frames,
    // and after them while the other attacks; then the plan's next step. Leaves the registers as the routine does.
    bool KeepStatusLift(Guest &g) {
        Frame frame(g, 28);
        Side s(g, g.R(4));
        const int16_t lever = static_cast<int16_t>(g.R(5)), way = static_cast<int16_t>(g.R(6));
        if (s.Place(1) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            g.R(5) = s.PlaceAt(0);
            g.R(3) = kClearDash;
            Call(g, kClearDash, Signed(s.side));
            s.SetWord(kTimer, 10);
            s.SetWord(kFreeLever, lever);
            g.R(0) = Signed(way);
            if (way != -1) {
                g.R(5) = Signed(way);
                const uint32_t bits = LeverFor(g, s.me, Signed(way));
                g.R(3) = Signed(s.Word(kFreeLever)) | bits;
                s.SetWord(kFreeLever, static_cast<int32_t>(g.R(3)));
            }
            g.R(0) = kLever;
            g.R(3) = Signed(s.Word(kFreeLever));
            g.R(4) = Signed(s.side * 2);
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
            g.SetT(false);
            g.Return(40);
            return true;
        }
        g.R(4) = kPlanPlace;
        g.R(5) = Signed(s.side * 2);
        s.SetWord(kLever, s.Word(kFreeLever));
        s.SetWord(kTimer, s.Word(kTimer) - 1);
        g.R(0) = Signed(s.side * 2);
        g.R(3) = Signed(s.Word(kTimer));
        if (s.Word(kTimer) != 0) {
            g.SetT(false);
            g.Return(40);
            return true;
        }
        s.SetWord(kTimer, 1);
        g.R(1) = kOtherAttacking;
        g.R(0) = Signed(s.side);
        g.R(3) = static_cast<uint32_t>(static_cast<int8_t>(s.Byte(kOtherAttacking)));
        if (s.Byte(kOtherAttacking) != 0) {
            g.SetT(false);
            g.Return(40);
            return true;
        }
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kParryFlag, 0);
        s.SetByte(kLimited, 0);
        g.R(2) = kMode;
        g.R(4) = s.PlaceAt(0);
        g.R(5) = 0;
        if (s.ModeWord(0) == 6) {
            g.R(0) = 6;
            g.R(3) = s.At(kLimited, 1);
            g.SetT(true);
        } else {
            s.SetByte(kAnswering, 0);
            g.R(0) = Signed(s.side);
            g.R(3) = s.At(kAnswering, 1);
            g.SetT(false);
        }
        g.Return(40);
        return true;
    }

    // 0600ABC0 (VS_Jump_Guard): against the other's jump: guard if it attacks (Check_Guard: 1); else, once it is
    // low (24 or less up), the plan's next step. Leaves the registers as the routine does.
    bool VsJumpGuardLift(Guest &g) {
        Frame frame(g, 12);
        Side s(g, g.R(4));
        if (s.Place(1) == 0) {
            if (Call(g, kCheckGuard, s.me) != 0) {
                g.R(0) = 1;
                g.SetT(false);
                g.Return(30);
                return true;
            }
            Call(g, kClearDash, Signed(s.side));
            s.SetPlace(1, s.Place(1) + 1);
            g.R(0) = Signed(s.Place(1));
            g.Return(30);
            return true;
        }
        if (Call(g, kCheckGuard, s.me) != 0) {
            g.R(0) = 1;
            g.SetT(false);
            g.Return(30);
            return true;
        }
        const uint32_t other = s.Other();
        g.R(3) = 24;
        g.R(2) = Signed(static_cast<int16_t>(g.Read16(other + kY)));
        if (static_cast<int32_t>(g.R(2)) > 24) {
            g.R(0) = 104;
            g.SetT(true);
            g.Return(30);
            return true;
        }
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kAnswering, 0);
        s.SetByte(kParryFlag, 0);
        s.SetByte(kLimited, 0);
        g.R(0) = Signed(s.side);
        g.R(2) = s.At(kLimited, 1);
        g.R(4) = 0;
        g.SetT(false);
        g.Return(30);
        return true;
    }


    // 0600AC74 (Wait_Lie, r5 a lever: 0 none, else back, and down with bit 2): hold the lever (kFreeLever) while the
    // other is knocked into the air (Check_Blow_Off), until it lies on the floor; then the plan's next step. Leaves
    // the registers as the routine does.
    bool WaitLieLift(Guest &g) {
        Frame frame(g, 28);
        Side s(g, g.R(4));
        const int16_t lever = static_cast<int16_t>(g.R(5));
        if (s.Place(1) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            g.R(3) = kClearDash;
            Call(g, kClearDash, Signed(s.side));
            s.SetByte(kGetUpSeen, 0);
            g.R(2) = Signed(lever);
            if (lever != 0) {
                s.SetWord(kFreeLever, static_cast<int32_t>(LeverFor(g, s.me, 1)));
                g.R(2) = Signed(s.Word(kFreeLever) | (lever & 2));
                s.SetWord(kFreeLever, static_cast<int32_t>(g.R(2)));
            } else {
                s.SetWord(kFreeLever, 0);
            }
        }
        g.R(6) = 0;
        s.SetWord(kLever, s.Word(kFreeLever));
        g.R(3) = kOtherFalling;
        CallWith(g, kOtherFalling, s.me, s.Other(), 0);
        if (g.R(0) != 0) {
            g.R(0) = Signed(s.side);
            g.R(1) = kOtherLying;
            g.R(3) = static_cast<uint32_t>(static_cast<int8_t>(s.Byte(kOtherLying)));
            if (s.Byte(kOtherLying) == 0) {
                g.SetT(true);
                g.Return(30);
                return true;
            }
            g.SetT(false);
        } else {
            g.SetT(true);
        }
        s.SetPlace(0, s.Place(0) + 1);
        for (int n = 1; n < 4; n++) {
            s.SetPlace(n, 0);
        }
        s.SetByte(kParryFlag, 0);
        s.SetByte(kLimited, 0);
        g.R(0) = Signed(s.side);
        g.R(3) = s.At(kLimited, 1);
        g.Return(30);
        return true;
    }

    // 0600B016 (Check_Exit_Guard, r5 0: wait for the other's attack): go on waiting? While the computer is hit; else,
    // r5 0, while the other is attacking, else while its attack is out
    bool StillToWait(Side &s, int16_t option) {
        if (s.Field(kDoing) == kDoingHit) {
            return true;
        }
        if (option == 0) {
            return static_cast<int16_t>(s.g.Read16(s.Other() + kDoing)) == kDoingAttack;
        }
        return s.Byte(kOtherAttacking) != 0;
    }
    bool StillToWaitLift(Guest &g) {
        Side s(g, g.R(4));
        g.R(0) = StillToWait(s, static_cast<int16_t>(g.R(5))) ? 1 : 0;
        g.Return(10);
        return true;
    }

    // 0600AF10 (Wait_Attack_Complete, r5 a lever, r6 as Check_Exit_Guard): hold the lever (kLeverNow: back, and
    // down with bit 2, guarding) until the other's attack is done; then the plan's next step
    bool WaitAttackDoneLift(Guest &g) {
        Frame frame(g, 32);
        Side s(g, g.R(4));
        const int16_t lever = static_cast<int16_t>(g.R(5)), option = static_cast<int16_t>(g.R(6));
        if (s.Place(1) == 0) {
            s.SetPlace(1, s.Place(1) + 1);
            Call(g, kClearDash, Signed(s.side));
            if (lever != 0) {
                s.SetWord(kLeverNow, static_cast<int32_t>(LeverFor(g, s.me, 1)));
                s.SetWord(kLeverNow, s.Word(kLeverNow) | (lever & 2));
                s.SetByte(kGuarding, 1);
            } else {
                s.SetWord(kLeverNow, 0);
            }
        }
        s.SetWord(kLever, s.Word(kLeverNow));
        if (!StillToWait(s, option)) {
            s.SetPlace(0, s.Place(0) + 1);
            for (int n = 1; n < 4; n++) {
                s.SetPlace(n, 0);
            }
            s.SetByte(kGuarding, 0);
            s.SetByte(kParryFlag, 0);
            s.SetByte(kLimited, 0);
            if (option == 0) {
                s.SetByte(kAnswering, 0);
            }
        }
        g.Return(40);
        return true;
    }


    // 06008C2A (Next_Be_Passive): free again (Next_Be_Free)
    bool BeFreeAgainLift(Guest &g) {
        g.R(3) = kBeFree;
        Call(g, kBeFree, g.R(4));
        g.Return(10);
        return true;
    }

    // 0600A422 (SA_Range_Check, r5 an art, r6 a gap, with 0x8000 "at least"): with that super art chosen, is the
    // other out of its range (the gap more than r6, or with 0x8000 less than r6's low bits)? Then the plan's next
    // step, and 1.
    bool SuperArtRangeLift(Guest &g) {
        Side s(g, g.R(4));
        const int16_t art = static_cast<int16_t>(g.R(5));
        if (static_cast<int16_t>(g.Read16(SuperArtOf(s))) != art) {
            g.R(0) = 0;
            g.Return(14);
            return true;
        }
        g.R(7) = kGap;
        const uint16_t range = static_cast<uint16_t>(g.R(6));
        const int16_t gap = s.Word(kGap);
        const bool out = (range & 0x8000) ? !(gap >= static_cast<int16_t>(range & 0x7FFF)) : gap > static_cast<int16_t>(range);
        if (out) {
            s.SetPlace(0, s.Place(0) + 1);
        }
        g.R(0) = out ? 1 : 0;
        g.Return(20);
        return true;
    }

    // 0600A3B0 (DENJIN_Check, r5 a plan, r6 where the plan is kept, r7 a test): with Ryu's third super art (the
    // Denjin Hadouken, charged), kept: the test (0x02015834) and the plan (byte 0x02015838) for after the charge,
    // and plan 55 (the charge) in its place
    bool DenjinLift(Guest &g) {
        Side s(g, g.R(4));
        if (static_cast<int16_t>(g.Read16(SuperArtOf(s))) != 2) {
            g.Return(14);
            return true;
        }
        s.SetWord(kDenjinTest, static_cast<int32_t>(g.R(7)));
        s.SetByte(kDenjinPlan, static_cast<int32_t>(g.R(5)));
        g.Write16(g.R(6), 55);
        g.R(0) = 1;
        g.Return(16);
        return true;
    }

    // 06008DC2 (Setup_DENJIN_LEVEL): the Denjin charged (plan 55's first step): on at once to the plan after it, the
    // word DENJIN_Check kept, or, none, one by the range at random (0x061771C4: 16 a range), in mode 2
    // (Next_Another_Menu, jumped to). Leaves the registers as the routine does.
    bool DenjinLevelLift(Guest &g) {
        Frame frame(g, 16);
        Side s(g, g.R(4));
        s.SetByte(kRunAgain, 1);
        const uint16_t after = static_cast<uint16_t>(s.Word(kDenjinTest));
        g.R(0) = Signed(s.side * 2);
        g.R(1) = kDenjinTest;
        if (after != 0) {
            g.R(2) = s.At(kRunAgain, 1);
            g.SetT(false);
            g.R(6) = after;
        } else {
            const uint32_t row = kDenjinLevels + Signed(s.Word(kRange) * 16);
            g.R(0) = Signed(s.Word(kRange) * 16);
            g.R(2) = kRange;
            g.R(3) = kDice64;
            g.Call(kDice64);
            g.R(3) = Signed(static_cast<int8_t>(g.Read8(row + g.R(0))));
            g.R(6) = g.R(3) & 0xFF;
            g.R(2) = kAnotherPlan;
        }
        if (after != 0) {
            g.R(3) = kAnotherPlan;
        }
        g.R(4) = s.me;
        g.R(5) = 2;
        frame.Pop();
        g.Call(kAnotherPlan);
        g.Return(40);
        return true;
    }

    // 06008F5C (Check_Exit_DENJIN): let the Denjin's charge go now? By the conditions DENJIN_Check kept (kDenjinPlan's
    // bits): 8, the other attacking; 1, against a jump (the first look: kPlanPlace 3 the range): near (0-2) as it
    // rises or once it comes down below 40, far as it rises moving away or comes down below 40; then by the way the
    // other moves (0 still, 1 toward, -1 away: 0x06012FB0): 2 toward, 4 away, 32 the other not lying down. Bit 1
    // clear, and no step yet (kPlanPlace 1): no. 1 to let go. Leaves the registers as the routine does (its caller,
    // Push_Shot, is jumped to by the plans, and they flow out).
    bool DenjinLetGo(Guest &g) {
        Frame frame(g, 24);
        Side s(g, g.R(4));
        const auto result = [&](uint32_t r0, bool t) {
            g.R(0) = r0;
            g.SetT(t);
            return r0 != 0;
        };
        const auto flags = [&]() { return s.Byte(kDenjinPlan); };
        g.R(4) = kDenjinPlan;
        g.R(3) = flags();
        if (!(flags() & 1) && s.Place(1) == 0) {
            return result(0, true);
        }
        if (flags() & 8) {
            g.R(1) = Signed(static_cast<int8_t>(s.Byte(kOtherAttacking)));
            if (g.R(1) != 0) {
                return result(1, false);
            }
        }
        // (the way the other moves: 0 still, -1 away (Check_Run_Away), 1 toward)
        const uint32_t other = s.Other();
        g.R(0) = 100;
        g.R(3) = Signed(static_cast<int16_t>(g.Read16(other + kX)));
        g.R(2) = other + 0x70;
        g.R(1) = Signed(static_cast<int16_t>(g.Read16(other + 0x70))); // (old_pos[0]: where it was the frame before)
        int16_t way = 0;
        if (g.R(3) != g.R(1)) {
            g.R(5) = other;
            g.R(3) = 0x06012FB0;
            g.R(4) = s.me;
            g.Call(0x06012FB0);
            way = g.R(0) != 0 ? -1 : 1;
        }
        g.R(4) = Signed(way);
        const auto byWay = [&]() {
            g.R(5) = Signed(way);
            if (way == 0) {
                return result(0, true);
            }
            g.R(0) = flags();
            if ((flags() & 2) && way == 1) {
                return result(1, true);
            }
            g.R(0) = flags();
            if ((flags() & 4) && way == -1) {
                return result(1, true);
            }
            g.R(4) = Signed(s.side);
            g.R(0) = flags();
            if (!(flags() & 32)) {
                return result(0, true);
            }
            g.R(0) = kOtherLying;
            g.R(3) = Signed(static_cast<int8_t>(s.Byte(kOtherLying)));
            return g.R(3) != 0 ? result(0, false) : result(1, true);
        };
        g.R(3) = flags();
        if (!(flags() & 1)) {
            return byWay();
        }
        g.R(2) = Signed(static_cast<int16_t>(g.Read16(other + kX)));
        if (g.R(2) == 0) {
            return byWay();
        }
        if (s.Place(2) == 0) {
            const uint32_t place = s.PlaceAt(0);
            g.R(1) = 4;
            g.R(3) = Signed(s.Place(2) + 1);
            g.R(2) = place + 4;
            s.SetPlace(2, static_cast<int32_t>(g.R(3)));
            g.R(5) = Signed(s.side);
            g.R(2) = kRange;
            g.R(3) = place;
            s.SetPlace(3, s.Word(kRange));
        }
        g.R(0) = Signed(s.Place(3));
        g.R(5) = 40;
        const int16_t rising = static_cast<int16_t>(g.Read16(other + kRising));
        const int16_t height = static_cast<int16_t>(g.Read16(other + kY));
        g.R(0) = 0x80;
        const auto comingDown = [&]() {
            g.R(0) = 0x80;
            g.R(2) = Signed(rising);
            if (rising >= 0) {
                g.SetT(true);
                return byWay();
            }
            g.R(0) = 104;
            g.R(2) = Signed(height);
            if (height > 40) {
                g.SetT(true);
                return byWay();
            }
            return result(1, false);
        };
        const int16_t range = s.Place(3);
        if (range == 0 || range == 1 || range == 2) {
            g.R(3) = Signed(rising);
            if (rising > 0) {
                return result(1, true);
            }
            return comingDown();
        }
        g.R(3) = Signed(rising);
        if (rising > 0) {
            g.R(0) = Signed(way);
            if (way == -1) {
                return result(1, true);
            }
        }
        return comingDown();
    }
    bool DenjinLetGoLift(Guest &g) {
        DenjinLetGo(g);
        g.Return(60);
        return true;
    }

    // 06008E22 (Push_Shot, r5 the charge wanted, 0xFF all of it): Ryu holding the Denjin Hadouken's charge (plans
    // 56-60): the lever kLeverNow; first, once the charge (its animation, 8 and 13, at the step r5 or past it) is as
    // wanted or it is to be let go (Check_Exit_DENJIN), step 99. Then out of the move (its end, kMoveKind 64, or the
    // fighter free) the plan goes on (Reaction_Exit_Sub); else, past the first step, the reaction (Reaction_Sub, 8,
    // r5), kHitBy kept. Leaves the registers as the routine does.
    bool PushShotLift(Guest &g) {
        Frame frame(g, 20);
        Side s(g, g.R(4));
        const int16_t power = static_cast<int16_t>(g.R(5));
        g.R(6) = kLeverNow;
        g.R(4) = kLever;
        const int16_t step = s.Place(1);
        g.R(0) = Signed(step);
        const auto holdLeverNow = [&]() {
            g.R(0) = Signed(s.side);
            g.R(5) = Signed(s.side * 2);
            g.R(4) = s.At(kLever, 2);
            g.R(6) = s.At(kLeverNow, 2);
            g.R(3) = Signed(s.Word(kLeverNow));
            s.SetWord(kLever, static_cast<int32_t>(g.R(3)));
        };
        const auto letGo = [&]() {
            g.R(4) = s.me;
            const bool go = DenjinLetGo(g);
            g.SetT(!go);
            if (go) {
                g.R(0) = 99;
                s.SetPlace(1, 99);
            }
        };
        const auto outOfMove = [&]() {
            g.R(0) = s.FieldByte(kMoveKind);
            if (g.R(0) == 64) {
                return true;
            }
            g.R(0) = 38;
            g.R(2) = Signed(s.Field(kDoing));
            return g.R(2) == 0;
        };
        const auto onward = [&]() {
            if (outOfMove()) {
                g.R(4) = s.me;
                g.R(3) = kReactionExit;
                frame.Pop();
                g.Call(kReactionExit);
            } else {
                g.R(6) = Signed(power);
                g.R(0) = Signed(s.side);
                g.R(5) = 8;
                g.R(3) = s.me + kHitByByte;
                g.R(4) = s.me;
                g.R(2) = s.At(kHitStock, 1);
                g.R(3) = Signed(static_cast<int8_t>(s.FieldByte(kHitByByte)));
                s.SetByte(kHitStock, static_cast<int32_t>(g.R(3)));
                g.R(3) = kReaction;
                frame.Pop();
                g.Call(kReaction);
            }
            g.Return(60);
            return true;
        };
        if (step == 1) {
            holdLeverNow();
            letGo();
            return onward();
        }
        if (step != 0) {
            return onward();
        }
        if (outOfMove()) {
            return onward();
        }
        holdLeverNow();
        g.R(0) = 0x206;
        g.R(0) = Signed(static_cast<int16_t>(g.Read16(s.me + 0x206)));
        if (g.R(0) == 8) {
            g.R(0) = Signed(static_cast<int16_t>(g.Read16(s.me + 0x208)));
            if (g.R(0) == 13) {
                // (the charge: the animation's place by its step, the compiler's division of words)
                g.R(1) = Signed(static_cast<int16_t>(g.Read16(s.me + 0x204)));
                g.R(2) = kDivideWords;
                g.R(0) = Signed(static_cast<int16_t>(g.Read16(s.me + 0x20C)));
                g.Call(kDivideWords);
                g.R(4) = Signed(static_cast<int16_t>(g.R(0)));
                if (static_cast<int16_t>(g.R(0)) >= power) {
                    g.R(0) = s.PlaceAt(0);
                    g.R(3) = s.PlaceAt(1);
                    s.SetPlace(1, 99);
                }
            }
        }
        letGo();
        g.Return(60);
        return true;
    }

    // 0600A3E2 (YAGYOU_Check, r5 where the plan is kept, r6 a plan or 0): with Oro's second super art (the
    // Yagyou Dama), that plan (or, 0, one of plans 100 and up at random: 0x0613E74C) in its place
    [[maybe_unused]] bool YagyouLift(Guest &g) {
        Side s(g, g.R(4));
        const uint32_t place = g.R(5);
        if (static_cast<int16_t>(g.Read16(SuperArtOf(s))) == 1) {
            uint32_t plan = g.R(6);
            if (static_cast<uint16_t>(plan) == 0) {
                plan = g.Read8(kYagyouPlans + static_cast<uint32_t>(Dice(g, kDice64))) + 100u;
            }
            g.Write16(place, static_cast<uint16_t>(plan));
            g.R(0) = 1;
        }
        g.Return(20);
        return true;
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
    // (The entries commented out are rewrites the check (checklifts.sh, CHECKLIFTS_RUNS=all) does not meet, so not
    // registered: Check_SA (no character's answer that calls it was met), Check_EX and YAGYOU_Check (met once each
    // in longer runs, the same, but not in the check's), Check_Squat (Search_Back_Term's branch to it not met), and
    // the tests 8, 11, 15-18 (no plan met used them).)
    const Lift kLifts[] = {
        {"ComputerBossRow", 0x060111AC, 0x14, 0x8D1E57EC6021F127ull, BossRowLift, true, kResult},
        {"ComputerPlanCheck", 0x06008BF0, 0x28, 0x38D66E1E1890153Full, PlanCheckLift, true, 0x13F1F},
        {"ComputerAfterHit", 0x0600775A, 0x22, 0x4B30DF093694955Bull, AfterHitLift, true},
        {"ComputerBeThrowing", 0x0600846A, 0x3A, 0xB0334850A63F526Aull, BeThrowingLift, true},
        {"ComputerBeInAir", 0x06008778, 0x6C, 0x7C5ACF5E983BC48Dull, BeInAirLift, true},
        {"ComputerGuardLever", 0x060069F6, 0x86, 0xA6B5B0768C73F48Bull, GuardLeverLift, true},
        {"ComputerStartGuard", 0x06011248, 0x74, 0xA4E628A525D50F87ull, StartGuardLift, true, 0x120FF},
        {"ComputerParryable", 0x060112BC, 0x3C, 0x6E593511FB30C57Cull, ParryableLift, true, 0x130FE},
        {"ComputerMakotoReady", 0x060110E0, 0x3A, 0xBBD4BA2ED61B4859ull, MakotoReadyLift, true, kResult},
        {"ComputerParryOdds", 0x060111F0, 0x58, 0x7705C5187224CCB9ull, ParryOddsLift, true, kResult},
        {"ComputerParryPossible", 0x0601111A, 0x92, 0xEF46A17A9857DF09ull, ParryPossibleLift, true, 0x1003C},
        {"ComputerParryGo", 0x06007B10, 0xB6, 0xD755A06BC727ACF5ull, ParryGoLift, true},
        {"ComputerStartParry", 0x06011324, 0xD2, 0xABA3B32B9AAEDEFBull, StartParryLift, true, 0x179FF},
        {"ComputerParryProjectileAgain", 0x06007E44, 0xA4, 0x71D73F1C8B61B8AFull, ParryProjectileAgainLift, true, kAll},
        {"ComputerParryAgain", 0x06007EE8, 0x7C, 0xB8DE9D8AB37BFF18ull, ParryAgainLift, true, kResult},
        {"ComputerParryAttack", 0x06008048, 0xDE, 0xE74E92FF41561850ull, ParryAttackLift, true, 0x1201E},
        {"ComputerBreakThrow", 0x0600826C, 0x6A, 0x5090487D780E83FAull, BreakThrowLift, true, kAll},
        {"ComputerActive", 0x06006C22, 0x50, 0x9D66CBDA1A336A0Aull, Lifted<Active>, true, kAll},
        {"ComputerVsProjectile", 0x06006D30, 0x50, 0x7E59C4793D089352ull, Lifted<VsProjectile>, true, kAll},
        {"ComputerGuards", 0x060066E8, 0x104, 0xDB11F78608FDAE1Eull, Lifted<Guard>, true, kAll},
        {"ComputerGuardsProjectile", 0x06006914, 0xB2, 0xB095507E9828CF96ull, Lifted<GuardProjectile>, true, kAll},
        {"ComputerHit", 0x06006D80, 0x5C, 0xB65E7CC798D3CC9Dull, Lifted<Hit>, true, kAll},
        {"ComputerHitFirst", 0x06006E00, 0x306, 0xF5B0F617EF248564ull, Lifted<HitFirst>, true, kAll},
        {"ComputerHitSecond", 0x06007106, 0x78, 0x0D65FADC087B4F90ull, Lifted<HitSecond>, true, kAll},
        {"ComputerSpecialMove", 0x060084EC, 0x1A2, 0x122C5F3B2707DDF9ull, SpecialMoveLift, true, 0x1297E},
        {"ComputerHitFifth", 0x06007186, 0xDC, 0x7A50FBA1992170DEull, Lifted<HitFifth>, true, kAll},
        {"ComputerHitSixth", 0x06007262, 0x35A, 0xA625F0FA7C8E4140ull, Lifted<HitSixth>, true, kAll},
        {"ComputerHitSeventh", 0x060075BC, 0xC6, 0x09F2873D3E6BF0B1ull, Lifted<HitSeventh>, true, kAll},
        {"ComputerHitStunned", 0x06007682, 0xD8, 0xB53A3E882F5C9CA5ull, Lifted<HitStunned>, true, kAll},
        {"ComputerInAir", 0x06007818, 0x4A, 0x8E20948A4799E7CAull, Lifted<InAir>, true, kAll},
        {"ComputerAirRecover", 0x06007862, 0x44, 0x0EE3F35C37E0B457ull, Lifted<AirRecover>, true, kAll},
        {"ComputerAirToward", 0x060078A6, 0xE2, 0x8D76D97A044440FDull, Lifted<AirToward>, true, kAll},
        {"ComputerAirAway", 0x06007988, 0x96, 0xBBBC2D91930C76F4ull, Lifted<AirAway>, true, kAll},
        {"ComputerParrying", 0x06007A1E, 0x4E, 0x4780079B2C108B64ull, Lifted<Parrying>, true, kAll},
        {"ComputerParryFirst", 0x06007A6C, 0x84, 0x398A56622B90502Cull, Lifted<ParryFirst>, true, kAll},
        {"ComputerParryInAir", 0x06007BC6, 0x10, 0x74ED3FE551432F5Bull, Lifted<ParryInAir>, true, kAll},
        {"ComputerParryDone", 0x06007BD6, 0x13C, 0xDD6920BC45884077ull, Lifted<ParryDone>, true, kAll},
        {"ComputerParryProjectile", 0x06007D28, 0xBC, 0x801AF44EB150DF8Full, Lifted<ParryProjectile>, true, kAll},
        {"ComputerParryNext", 0x06007DE4, 0x60, 0x987CDB07F8223C5Aull, Lifted<ParryNext>, true, kAll},
        {"ComputerCaught", 0x06008126, 0x146, 0xE5EBE4C853636CD9ull, Lifted<Caught>, true, kAll},

        {"ComputerLevelOf4", 0x0600F9D2, 0x42, 0x61C2C51A89EA616Eull, LevelOf4Lift, true},
        {"ComputerLevelOf10", 0x0600FA84, 0x40, 0xAC219188C2A9C752ull, LevelOf10Lift, true},
        {"ComputerLevelOf18", 0x0600FAC4, 0x42, 0x8FD75DDDE22B5800ull, LevelOf18Lift, true},
        {"ComputerCounterChance", 0x0600FB06, 0x46, 0xBAA417B1C15C4B09ull, CounterChanceLift, true, kResult},
        {"ComputerReactionTime", 0x0600F93C, 0x96, 0x8653BF43E4C8A2EDull, ReactionTimeLift, true},
        {"ComputerRangeVsShell", 0x0600FCAC, 0x42, 0x07E465C3519FA4DAull, RangeVsShellLift, true, 0x10FFE},
        {"ComputerGapAndLever", 0x0600FCEE, 0x48, 0x2AC84029732D383Dull, GapAndLeverLift, true, 0x120FF},
        {"ComputerChoosePlan", 0x060104B4, 0x1FA, 0x11388483DADBEA73ull, ChoosePlanLift, true},
        {"ComputerReact", 0x060109AC, 0x32C, 0xE7037431C19A74ECull, ReactLift, true},
        {"ComputerOtherFalling", 0x060138E0, 0x50, 0x4F4C6BEDAE24247Bull, OtherFallingLift, true, 0x1007E},
        {"ComputerOtherStunned", 0x060138AE, 0x32, 0x777F017813770AD9ull, OtherStunnedLift, true, 0x12018},
        {"ComputerOtherBacksOff", 0x0601373A, 0x6E, 0x3C8EDECA0B74D793ull, OtherBacksOffLift, true, 0x160FE},
        {"ComputerOtherOnFloor", 0x060136C6, 0x74, 0x2260A6CC4BE92996ull, OtherOnFloorLift, true, 0x100FE},
        {"ComputerOtherDown", 0x06013864, 0x4A, 0xE96DAF2CBA8900ACull, OtherDownLift, true, 0x1201E},
        {"ComputerCheckAnswer", 0x06010DEA, 0x162, 0xEA9D485FDBE655AAull, CheckAnswerLift, true, 0x10018},
        {"ComputerShellBehind", 0x06011A60, 0x66, 0xFBD27BBBD3150114ull, ShellBehindLift, true, 0x100FE},
        {"ComputerShellLetBe", 0x06011704, 0x6A, 0x881ACF8A97B054A7ull, ShellLetBeLift, true, 0x100FE},
        {"ComputerShellLetBeInJump", 0x06011500, 0x6A, 0x881ACF8A97B054A7ull, ShellLetBeLift, true, 0x100FE},
        {"ComputerShellArrival", 0x0601176E, 0x42, 0x7EDCA223658014B3ull, ArrivalLift, true, 0x102FE},
        {"ComputerShellIgnore", 0x060117B0, 0xB6, 0xA7D00798B6CB2548ull, ShellIgnoreLift, true, 0x100FE},
        {"ComputerShellPlan", 0x060119C6, 0x8C, 0xD85523218C674FCAull, ShellPlanLift, true, 0x120FC},
        {"ComputerAgainstShell", 0x06011866, 0x160, 0x49D001E3EB72C080ull, AgainstShellLift, true, kAll},
        {"ComputerShellComing", 0x0601156A, 0x19A, 0x0329C3C9CAE62D9Dull, ShellComingLift, true},
        {"ComputerTestWait30", 0x06011D5E, 0x48, 0x7BAEA02998CE672Eull, TestWait30, true},
        {"ComputerTestAtOnce", 0x06011DA6, 4, 0xEFFCEE7F69A41DEFull, TestAtOnce, true},
        {"ComputerTestShellGone", 0x06011DAA, 0x1C, 0x83BD164AD3C0D0C0ull, TestShellGone, true},
        {"ComputerTestWait10", 0x06011DC6, 0x8C, 0x8CE10430245853A8ull, TestWaitNotKen<10>, true},
        {"ComputerTestWait1", 0x06011E52, 0x60, 0x411799F756F8C188ull, TestWaitNotKen<1>, true},
        {"ComputerTestWait5", 0x06011EB2, 0x60, 0x2FF292907D2C929Cull, TestWaitNotKen<5>, true},
        {"ComputerTestWait60", 0x06011F12, 0x60, 0xAC730B30DA23A703ull, TestWaitNotKen<60>, true},
        {"ComputerTestOtherLands", 0x06011F72, 0x86, 0x84BFEE1C3DE617C9ull, TestOtherLands, true},
        // {"ComputerTestLanded", 0x06011FF8, 0x6C, 0xA3F774AF6718ED51ull, TestLanded, true},
        {"ComputerTestOtherCrouching", 0x06012270, 0x22, 0x619082BAA557854Cull, TestOtherCrouching, true},
        {"ComputerTestNotOroReady", 0x06012292, 0x62, 0x995CD62D3A6CA452ull, TestNotOroReady, true},
        // {"ComputerTestLowHealth", 0x060122F4, 0x22, 0x344F422B0CE1B782ull, TestLowHealth, true},
        {"ComputerTestOtherNotLow", 0x06012316, 0x22, 0x149965CAB3EB9488ull, TestOtherNotLow, true},
        // {"ComputerTestWait120", 0x0601238E, 0x82, 0x7A0DA5BD49F960C1ull, TestWait120, true},
        // {"ComputerTestThirdArt", 0x06012410, 0x1C, 0xAF0B9E442CDED474ull, TestArt<2>, true},
        // {"ComputerTestSecondArt", 0x0601242C, 0x1C, 0x17AA5DEE69D58586ull, TestArt<1>, true},
        // {"ComputerTestFirstArt", 0x06012448, 0x1C, 0x3818261862CA9C36ull, TestFirstArt, true},
        {"ComputerComingDownBelow", 0x06012064, 0x20, 0x419CAD5A2A6876FCull, ComingDownBelow, true},
        {"ComputerKenArtReady", 0x06012084, 0x46, 0x3D91BA4FCEC5CDD4ull, KenArtReadyLift, true},
        {"ComputerEtcTerm", 0x060121D4, 0x9C, 0xA2F2654780AF8B30ull, EtcTermLift, true},
        {"ComputerTermTest", 0x06011C10, 0x3C, 0x29EF733C25DC6D63ull, TermTestLift, true},
        {"ComputerTestGivesWay", 0x06011C4C, 0x112, 0x783CA1CCFFD7F7A3ull, TestGivesWayLift, true},
        {"ComputerUntilTerm", 0x0600B19C, 0x172, 0x5AA401B6B97FDF58ull, UntilTermLift, true, kResult},
        {"ComputerSuperArtTerm", 0x0600A07E, 0x332, 0x9F4041443CE32E1Dull, SuperArtTermLift, true, kResult},
        {"ComputerArrived", 0x0600942C, 0x8E, 0xB611D75BD8FBB156ull, ArrivedLift, true, kResult},
        {"ComputerApproach", 0x060092EA, 0x142, 0x98C494AD8E2C53C8ull, ApproachLift, true},
        // {"ComputerSquatCheck", 0x06009D42, 4, 0xEFFCED7F69A41C3Cull, SquatCheckLift, true},
        {"ComputerRoomBehind", 0x06009242, 0xA8, 0x0DB8C604F6388A8Bull, RoomBehindLift, true},
        {"ComputerKeepAwayJump", 0x060091C0, 0x72, 0xE24C6C3BF2CD9E54ull, KeepAwayJumpLift, true},
        {"ComputerKeepAwayWalk", 0x06009232, 0x10, 0x1523FA4A9DBB2057ull, KeepAwayWalkLift, true},
        {"ComputerKeepAway", 0x06009100, 0xC0, 0x661A068C63BA8A0Aull, KeepAwayLift, true},
        {"ComputerHighJump", 0x0600B6F6, 0x246, 0x0B72772181A0F764ull, HighJumpLift, true},
        {"ComputerHighJumpBusy", 0x0600B93C, 0x38, 0xC8114A747FC1AD81ull, HighJumpBusyLift, true},
        {"ComputerJump", 0x0600B558, 0x19E, 0xF04361017BF100D9ull, JumpLift, true},
        {"ComputerUntilShell", 0x0600B30E, 0x158, 0x3F6F3E68FD438AA0ull, UntilShellLift, true, kAll},
        // {"ComputerSuperArtReady", 0x0600A4AE, 0x74, 0xB11B4FA4E20073DFull, SuperArtReadyLift, true, kAll},
        // {"ComputerExReady", 0x0600A522, 0x74, 0x8116145AF7C9B74Dull, ExReadyLift, true, kAll},
        {"ComputerGaugeFull", 0x0600A5C0, 0x66, 0xAFE4B448B1CC93B3ull, GaugeFullLift, true, kAll},
        {"ComputerWaitByRange", 0x0600A79A, 0xF0, 0x8601B62552597ECBull, WaitByRangeLift, true, kAll},
        {"ComputerWaitGetUp", 0x0600ADA0, 0x102, 0xC63876D237C61896ull, WaitGetUpLift, true},
        {"ComputerForcedGuard", 0x06009746, 0x6C, 0x17B7B3DD7A52CABCull, ForcedGuardLift, true},
        {"ComputerProvoke", 0x060097B2, 0x114, 0x3A9A26E9991046FEull, ProvokeLift, true},
        {"ComputerKeepStatus", 0x0600AA6C, 0x154, 0x98225F16575BA57Bull, KeepStatusLift, true},
        {"ComputerVsJumpGuard", 0x0600ABC0, 0xB4, 0x687344B5B200A306ull, VsJumpGuardLift, true},
        {"ComputerWaitLie", 0x0600AC74, 0xF8, 0xA89E8DF4C6EC08FEull, WaitLieLift, true},
        {"ComputerStillToWait", 0x0600B016, 0x50, 0x48A7BA9A92BEB5A2ull, StillToWaitLift, true, kAll},
        {"ComputerWaitAttackDone", 0x0600AF10, 0x106, 0xEF7C4F9E483F295Dull, WaitAttackDoneLift, true, kAll},
        {"ComputerSuperArtRange", 0x0600A422, 0x8C, 0x988A96E6A3C2F63Dull, SuperArtRangeLift, true, 0x1007E},
        {"ComputerDenjinLevel", 0x06008DC2, 0x60, 0x747FDCA54713C3FFull, DenjinLevelLift, true},
        {"ComputerPushShot", 0x06008E22, 0x13A, 0xA85C5FD315F3B661ull, PushShotLift, true},
        {"ComputerDenjinLetGo", 0x06008F5C, 0x1A4, 0x6981CCE9000E29D4ull, DenjinLetGoLift, true},
        {"ComputerDenjin", 0x0600A3B0, 0x32, 0xC06062CE4F199EF8ull, DenjinLift, true, 0x1007F},
        {"ComputerBeFreeAgain", 0x06008C2A, 0x6, 0x7E5D0E68E47D42C4ull, BeFreeAgainLift, true},
        // {"ComputerYagyou", 0x0600A3E2, 0x40, 0x35F0595B67A655F6ull, YagyouLift, true, 0x1007F},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterComputerTactics() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
