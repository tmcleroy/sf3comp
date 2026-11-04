// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's hit decisions, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h): what
// a hit, a throw, a guard or a parry does once the frame's hit tests (fight_hits.cpp) have found who met whom.
//
// For each record queued that was hit, set_judge_result calls set_struck_status (0x0608CE68), or for one caught by a
// throw set_caught_status (0x0608C994); then check_result_extra (0x0608C860) gives a trade (both fighters hit by the
// other) one hit stop.
//   A hit: by who hit whom (fighter or effect: 0x0608ECC4, 0x0608F008, 0x0608EEA0, 0x0608F290). Two fighters that
//     hit each other: the stronger kind of attack wins (a throw that takes the hurt, a throw or throw move, a special
//     move, a normal one: Traded), level ones trade. Then where it was hit, the ways it is attacked from and its lever
//     says, the damage and the stun (0x0608D7FC: by pose, handicaps, the combo's scalings), and what the defender makes
//     of it (0x0608D02C): in the air defense_sky (0x0608DAE8), on the ground defense_ground (0x0608DC44).
//   A parry: the window for the attack's height open (high, low, in the air, against a jump's attack; a red parry, made
//     while guarding, needs as many frames of it left as the table's, 0x0618BBE4); the parrier's hit stop -15, the
//     attacker's 16 and more for a normal move on the ground by its strength (0x0608D444); no damage but an effect's that
//     says so (0x0608D776).
//   A guard: the attack guardable that way (crouching, standing, in the air), the lever back the way it comes from (or
//     auto_guard), crouching or standing as it needs; its chip, the damage over the attack's divisor, which may knock
//     out: then it is a hit (0x0608D6E4); the guard's reaction 4-7 (0x0608D38C).
//   Else hit: the reaction (0x0608D300, then by height and where it was hit), routine 1, the spark, what the hit carries
//     (0x0608DFCC: the hit stops, att.hs_me and hs_you, 0x0608DA62), the combo counted, the attacker's animation's hit
//     frame (0x0608D638).
//   A throw: two throws that met, one stands (by kind, the parry windows open, then the frame); it may be parried or
//     guarded as an attack; else the thrower to its throw and the caught one held (routine 3).
//   And the helpers: the combo counted (add_combo_work 0x0608E098), the damage scaled by the combo's hits weighted by
//     kind (cal_combo_waribiki 0x0608E1FC), the stun by their number (0x0608E3E4), the score's index for the damage
//     (0x0608DA3C), the spark's place (0x0608CF62), the way an effect's hit turns the one hit (0x0608F240), and the
//     round's grades: hits, guards and parries counted (0x060942B2, 0x0609433E, 0x06094744).
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }

    // --- in a record ---
    constexpr uint32_t kSteps = 0x24;      // words: its steps (routine_no[0..3]): [1] 1 being hurt, [2] how, [3] the step
                                           // in it
    constexpr uint32_t kHow = 0x28;        // word: routine_no[2]: how it is hurt, guards or parries (its reaction)
    constexpr uint32_t kHitStop = 0x44;    // word: frames it stands still for a hit it made or took (hit_stop)
    constexpr uint32_t kHitQuake = 0x46;   // word: frames it shakes (hit_quake)
    constexpr uint32_t kHurtBy = 0x14;     // long: the record that hit it (dmg_adrs)
    constexpr uint32_t kKind = 0x06;       // word: 1 a fighter, 4 an effect (work_id)
    constexpr uint32_t kId = 0x08;         // word: a fighter's player, 0 or 1; an effect's kind (id)
    constexpr uint32_t kHealth = 0x9E;     // word (vital_new)
    constexpr uint32_t kDamage = 0xA2;     // word: the damage it is to take (dm_vital)
    constexpr uint32_t kChip = 0x2F0;      // word: an attack's damage guarded is its damage divided by this; 0 none
                                           // (kezuri_pow)
    constexpr uint32_t kNoKnockOut = 0x303; // byte: its damage can't knock out (no_death_attack)
    constexpr uint32_t kHitAs = 0x312;     // byte: what its attack made of the hit (hf.hit.player: 1 a hit on the
                                           // ground, 2 in the air, 0x10 guarded, 0x20 guarded in the air, 0x40 parried,
                                           // 0x80 parried in the air)
    constexpr uint32_t kGuardFlags = 0x2E1; // byte: how it may be guarded and parried (att.guard): 1 parried low, 2
                                            // high, 4 in the air, 8 guarded crouching, 0x10 standing, 0x20 in the air
    constexpr uint32_t kJumpAttack = 0x304; // byte: an attack from a jump (jump_att_flag)
    constexpr uint32_t kGuardDone = 0x324; // word: what a guard or parry made of it: its reaction, or -1
                                           // (dm_guard_success)
    constexpr uint32_t kHurtStop = 0x32E;  // word: the hit stop its hit gives it (dm_stop: below 0, a parry's)
    constexpr uint32_t kHurtQuake = 0x330; // word (dm_quake)
    constexpr uint32_t kNoDeath = 0x340;   // byte: the hit can't knock it out (dm_nodeathattack)
    constexpr uint32_t kEffectData = 0x348; // long: an effect's own record (my_effadrs)
    constexpr uint32_t kInput = 0x3B8;     // long: its player's input record (cp: the parry windows' frames, waza_flag,
                                           // and the lever, sw_lvbt)
    constexpr uint32_t kCantDefend = 0x3BC; // long: what it may not do now (spmv_ng_flag): 0x10 guard on the ground,
                                            // 0x20 in the air, 0x100 parry high, 0x200 low, 0x400 in the air, 0x800
                                            // against a jump's attack
    constexpr uint32_t kGuardMode = 0x3D2; // byte: 1 it can't guard, 2 nor parry, 3 neither (guard_flag)
    constexpr uint32_t kGuarding = 0x3D3;  // byte: frames into a guard, 1-4 a guard just begun (guard_chuu)
    constexpr uint32_t kMoveState = 0x3F8; // long: its record of the move under way (py): its first byte not 0 in one
    constexpr uint32_t kLever = 0x41A;     // byte: the ways the lever has been held (saishin_lvdir)
    constexpr uint32_t kAutoGuard = 0x45B; // byte: it guards whatever the lever (auto_guard)
    constexpr uint32_t kParryKind = 0x470; // byte: 1 a parry while standing, 2 while hurt (kind_of_blocking)
    // ... in its input record (cp)
    constexpr uint32_t kLeverHeld = 0x00;  // word: 1 forward, 2 down (sw_lvbt)
    constexpr uint32_t kParryHigh = 0x1C, kParryLow = 0x1E; // words: frames left of the windows (waza_flag[3], [4])
    constexpr uint32_t kParryAir = 0x20, kParryUp = 0x22;   // (waza_flag[5], [6])
    constexpr uint32_t kParryJump = 0x2E;  // (waza_flag[12]: against a jump's attack, standing)
    constexpr uint32_t kMoveKind = 0x20F;  // byte: the move's kind (kind_of_waza): 0-7 a normal one by strength
    constexpr uint32_t kY = 0x68;          // word: its height (xyz[1], a long with its fraction)
    constexpr uint32_t kHowBefore = 0x38;  // word: its reaction before (old_rno[2])
    constexpr uint32_t kAnimationFrame = 0x204; // word (cg_ix)
    constexpr uint32_t kAnimationStep = 0x20C;  // word: an animation frame's size (cgd_type)
    constexpr uint32_t kPose = 0x20E;      // byte (pat_status)
    constexpr uint32_t kAnimationHold = 0x214; // byte (cg_ctr)
    constexpr uint32_t kCgType = 0x215;    // byte (cg_type)
    constexpr uint32_t kAnimationExtra = 0x220; // byte: the frame's orders on a hit (cg_extdat)
    constexpr uint32_t kAnimationNext = 0x22A;  // byte (cg_next_ix)
    constexpr uint32_t kHitsMade = 0x17A, kHitsMade2 = 0x188; // words (cmwk[8], cmwk[15])
    constexpr uint32_t kReaction = 0x2DC;  // byte: the reaction the attack gives (att.reaction)
    constexpr uint32_t kAttackFlags = 0x2E0; // byte (att.dipsw): 1 its hit stop given the one hit, 0x10 a special
                                             // move's reactions in the air
    constexpr uint32_t kAttackWay = 0x2E2; // byte (att.dir)
    constexpr uint32_t kStopMine = 0x2E8, kStopTheirs = 0x2E9; // bytes: the hit stops for the attacker and the one
                                                               // it meets (att.hs_me, hs_you)
    constexpr uint32_t kReactionStands = 0x2EC; // word: the attack's reaction stands, whatever is hit (zu_flag)
    constexpr uint32_t kElement = 0x2EE;   // word: fire, electricity, ice (at_attribute)
    constexpr uint32_t kAttackDir = 0x2FA; // word (dir_atthit)
    constexpr uint32_t kHurtFacing = 0x32C; // byte: the way it is hit towards (dm_rl)
    constexpr uint32_t kStun = 0x332;      // word: the stun its hit gives (dm_piyo)
    constexpr uint32_t kArtsPoints = 0x33A; // word: super art gauge the hit gives it (dm_arts_point)
    constexpr uint32_t kHitsTaken = 0x33E; // word (dm_count_up)
    constexpr uint32_t kOwnerRecord = 0x3B8; // long: an effect's owner (my_master)
    constexpr uint32_t kCharacter = 0x3C0; // word: a fighter's character (player_number)
    constexpr uint32_t kHitPoint = 0x3D9;  // byte: where it was hit: 0-1 the head, 4-7 the hands (dm_point)
    constexpr uint32_t kRunning = 0x3E4;   // byte (running_f)
    constexpr uint32_t kMoved = 0x414;     // word (move_distance)
    constexpr uint32_t kAttackStun = 0x2E6; // byte (att.piyo)
    constexpr uint32_t kScoreIx = 0x2F8;   // word: the score's index for its hit (at_ten_ix)
    constexpr uint32_t kHitHandicap = 0x43A, kThrowHandicap = 0x43C, kStunHandicap = 0x43E; // words (tk_dageki,
                                           // tk_nage, tk_kizetsu; +8 in the one hit: utk_*)
    constexpr uint32_t kKept = 0x44A;      // word: damage kept back (dm_vital_backup)
    constexpr uint32_t kKeptUse = 0x44D;   // byte: 1 add it, 2 half of it (dm_vital_use)
    constexpr uint32_t kFacing = 0x0A;     // byte: not 0 facing right (rl_flag)
    constexpr uint32_t kX = 0x64;          // word: across (xyz[0])
    constexpr uint32_t kThrowStepTurned = 0x11C, kThrowStepFacing = 0x124; // words: a throw's routine, step and frame for
                                           // the thrower, facing the other's way or not (cmcf, cmcr: koc, ix, pat)
    constexpr uint32_t kCharIndex = 0x208; // word (char_index)
    constexpr uint32_t kCatchBox = 0x2B8, kCaughtBox = 0x2C0; // longs: a throw's box, the box a throw catches
    constexpr uint32_t kSparkMark = 0x2DE; // byte: the spark's offset in the table (att.mkh_ix)
    constexpr uint32_t kThrowKind = 0x2E7; // byte: how the caught one is held (att.ng_type)
    constexpr uint32_t kSparkX = 0x314, kSparkY = 0x316; // words (hit_mark_x, _y)
    constexpr uint32_t kHolding = 0x3CC;   // word: the character held (tsukami_num)
    constexpr uint32_t kHoldingFlag = 0x3CE, kHeld = 0x3CF; // bytes (tsukami_f, tsukamare_f)
    constexpr uint32_t kMayBreak = 0x435, kUnbreakable = 0x436; // bytes: the throw may be broken, may not
                                                                // (cat_break_reserve, hazusenai_flag)
    constexpr uint32_t kEffectSet = 0x05;  // byte: an effect's (charset_id)
    constexpr uint32_t kOwnerHitHandicap = 0x27E, kOwnerThrowHandicap = 0x280, kOwnerStunHandicap = 0x282; // words: an
                                           // effect's, its owner's handicaps when it was made (olc_work_ix[0-2])
    constexpr uint32_t kOwnerRoutine = 0x284; // word: an effect's owner's routine when it was made (olc_work_ix[3])
    constexpr uint32_t kCantReflect = 0x305; // byte: an effect that can't be reflected (shell_vs_refrect)
    constexpr uint32_t kHitAsEffect = 0x313; // byte: what its attack made of an effect (hf.hit.effect)
    constexpr uint32_t kReflect = 0x3C4, kReflected = 0x3C5; // bytes: an effect's (dm_refrect, refrected)
    constexpr uint32_t kAtemi = 0x448, kAtemiPoint = 0x449; // bytes: a move that catches attacks, where (atemi_flag,
                                                            // atemi_point)

    // --- the game's ---
    constexpr uint32_t kTrade = 0x0201581E; // byte: the two fighters hit each other this frame (aiuchi_flag)
    constexpr uint32_t kNormalMove = 0x0618BB88; // bytes, by kind_of_waza / 8: 0 a normal move (sel_sp_ch_tbl)
    constexpr uint32_t kRedParry = 0x0618BBE4;   // words [normal][high, low]: the frames a window must have left for a
                                                 // parry while guarding (a red parry; grdb)
    constexpr uint32_t kRunningInto = 0x0618BB68; // words, by att.dir: not 0 a way that stops a run (Dsas_dir_table)
    constexpr uint32_t kParryStopMore = 0x0618BB94; // words, by a normal move's strength (4 a special move): the hit
                                                    // stop a parry on the ground adds to the attacker's 16
                                                    // (sel_hs_add_tbl)
    constexpr uint32_t kParryPointsTable = 0x0618C2BB; // bytes [Play_Type][40]: the bonus's points by the parries in a
                                                       // row (parisucc_pts)
    // ... the reactions' tables, by the reaction less 32: words, the reaction it becomes; bytes, not 0 one that depends
    // on the way the attack came
    constexpr uint32_t kAirReactions = 0x0618BDDE, kAirSpecialReactions = 0x0618BF2A; // (sky_nm_damage_tbl, _sp_)
    constexpr uint32_t kMirrorReactions = 0x0618BFD0; // (kagami_damage_tbl: poses 32 on)
    constexpr uint32_t kHandReactions = 0x0618C11C;   // (grd_hand_damage_tbl)
    constexpr uint32_t kHeadReactions = 0x0618C1C2, kBodyReactions = 0x0618C215; // bytes (hddm_, trdm_damage_tbl)
    constexpr uint32_t kPlayType = 0x020113B4;    // word: 0 against the computer, 1 two people (Play_Type)
    constexpr uint32_t kBonusGame = 0x02016B3A;   // word: a bonus game under way (Bonus_Game_Flag)
    constexpr uint32_t kParriesInRow = 0x02015823; // bytes [Play_Type][player] (paring_ctr_vs)
    constexpr uint32_t kParryPoints = 0x0201581F, kParryBonus = 0x02015821; // bytes, by player (paring_counter,
                                                                             // paring_bonus_r)
    // ... and the routines called
    constexpr uint32_t kDivide16 = 0x0612D584;   // r1 by r0, 16 bits
    constexpr uint32_t kGradeParry = 0x06094744; // grade_add_blocking (r4 the record)
    constexpr uint32_t kGradeHit = 0x060942B2;   // grade_add_clean_hits (r4 the attacker)
    constexpr uint32_t kGradeGuard = 0x0609433E; // grade_add_guard_success (r4 the player)
    constexpr uint32_t kGuardMissed = 0x06094CA8; // check_guard_miss (r4, r5, r6 the way)
    constexpr uint32_t kGuardGauge = 0x0611E478;  // add_sp_arts_gauge_guard (r4 the attacker)
    constexpr uint32_t kSpark = 0x060DCC7E;       // effect_02_init (r4 the attacker, r5 dm_point, r6 1 a hit 2 a guard,
                                                  // r7 dm_rl)
    constexpr uint32_t kMovesReset = 0x060B6168;  // waza_compel_all_init2 (r4)
    constexpr uint32_t kFallBack = 0x0611DAD6;    // remake_mvxy_PoGR (r4)
    constexpr uint32_t kComboCounted = 0x0608E098; // add_combo_work (r4, r5)
    constexpr uint32_t kWhichSide = 0x0611E056;   // check_work_position (r4, r5): r0 1 the first on the right
    constexpr uint32_t kElementReaction = 0x0608EB96; // change_damage_attribute (r4 the attacker, r5 its element, r6 the
                                                      // reaction): r0 the reaction
    constexpr uint32_t kHeadReaction = 0x0611E1D0, kBodyReaction = 0x0611E1E8; // get_kind_of_head_dm, _trunk_dm (r4
                                                  // the attack's way, dir_atthit; r5 dm_rl): r0 the reaction
    constexpr uint32_t kCommandsBack = 0x0608BD40; // setup_comm_abbak (r4)
    constexpr uint32_t kMoveZ = 0x06089350;       // char_move_z (r4)
    constexpr uint32_t kAttackLow = 0x0611D284;   // check_attbox_dir (r4 the defender): r0 0 or not by the height of
                                                  // the attack's box on it
    constexpr uint32_t kParryCounted = 0x0608DBEC, kGuardChip = 0x0608D6E4, kParryChip = 0x0608D776;
    constexpr uint32_t kAirPose = 0x0608CD34;     // check_pat_status (r4): r0 1 in a pose that counts as the air
    constexpr uint32_t kDefenseGround = 0x0608DC44, kDefenseSky = 0x0608DAE8;
    constexpr uint32_t kReactionSet = 0x0608D300, kSameStop = 0x0608DA62, kStatusCopy = 0x0608DFCC;
    constexpr uint32_t kBaseDamage = 0x0609E36C;  // cal_damage_vitality (r4, r5; lifts.cpp)
    constexpr uint32_t kScaleDamage = 0x0611E27C; // cal_dm_vital_gauge_hosei (r4 the one hit; lifts.cpp)
    constexpr uint32_t kScoreIndex = 0x0608DA3C;  // remake_score_index (r4 the damage): r0 the score's index
    constexpr uint32_t kComboDiscount = 0x0608E1FC, kStunDiscount = 0x0608E3E4; // cal_combo_waribiki (r4, r5),
                                                  // cal_combo_waribiki2 (r4 the one hit)
    constexpr uint32_t kStunTable = 0x065FBB90;   // bytes [character][16], by att.piyo: the stun (_add_piyo_gauge)
    constexpr uint32_t kGameTimer = 0x0201136C;   // word: frames (Game_timer)
    constexpr uint32_t kSparkOffsets = 0x061B959E; // words [mkh_ix][2]: a spark's place from the attacker's
                                                   // (hit_mark_hosei_table)
    constexpr uint32_t kBoxesMeet = 0x0608E9D0;   // cal_hit_mark_position (r4, r5 the records, r6, r7 their boxes)
    constexpr uint32_t kReactionFacing = 0x0608EC8A; // check_ttk_damage_request (r4 the reaction): r0 not 0 one hit
                                                     // towards the attacker's facing whatever
    constexpr uint32_t kParryWindows = 0x0608CD90, kCatchSparkPlace = 0x0608CDEA;
    constexpr uint32_t kHurtWayByEffect = 0x0608F240; // setup_dm_rl_pldm (r4, r5)
    constexpr uint32_t kEffectDamage = 0x0609E3FA; // cal_damage_vitality_eff (r4, r5)
    constexpr uint32_t kGuardStatus = 0x0608D38C, kParryStatus = 0x0608D444, kHitPatternExtra = 0x0608D638;

    uint32_t Player(int n) {
        return kPlayers + Signed(n) * kPlayerSize;
    }

    // 0608C860 (check_result_extra): when both fighters were hit this frame by the other (a trade: their records'
    // dmg_adrs each a fighter, each in its first frame of being hurt), aiuchi_flag set and the two given the same hit
    // stop, the longer of the two (dm_stop, a parry's taken as its size), and the stronger shake; their own hit stops
    // and shakes then cleared. Leaves the registers as the game's code (its caller's next routine, through a tail jump,
    // is not followed by liveout.py).
    bool ResultsExtra(Guest &g) {
        Frame frame(g, 12); // (r14, two longs)
        const uint32_t one = Player(0), two = Player(1);
        const auto firstHurt = [&](uint32_t p) {
            return Word(g, p + kSteps + 2) == 1 && Word(g, p + kSteps + 6) == 0;
        };
        const bool oneHurt = firstHurt(one);
        const bool twoHurt = firstHurt(two);
        g.R(4) = one;
        g.R(5) = kPlayerSize;
        g.R(3) = oneHurt ? 1 : 0;
        g.R(0) = twoHurt ? 1 : 0;
        g.R(6) = two;
        g.R(7) = one;
        if (!(oneHurt && twoHurt)) {
            g.SetT(true);
            g.Return(30);
            return true;
        }
        // (both hit by fighters: a trade)
        g.R(7) = g.Read32(one + kHurtBy);
        g.R(6) = g.Read32(two + kHurtBy);
        const bool byFighter1 = Word(g, g.R(7) + kKind) == 1, byFighter2 = Word(g, g.R(6) + kKind) == 1;
        g.R(3) = byFighter1 ? 1 : 0;
        g.R(0) = (byFighter1 ? 1 : 0) + (byFighter2 ? 2 : 0);
        if (g.R(0) != 3) {
            g.SetT(false);
            g.Return(40);
            return true;
        }
        g.Write8(kTrade, 1);
        int32_t stop1 = Word(g, one + kHurtStop), stop2 = Word(g, two + kHurtStop);
        stop1 = stop1 < 0 ? -stop1 : stop1;
        stop2 = stop2 < 0 ? -stop2 : stop2;
        int32_t quake = Word(g, one + kHurtQuake);
        g.R(1) = Extended(Word(g, two + kHurtQuake));
        if (quake < Word(g, two + kHurtQuake)) {
            quake = Word(g, two + kHurtQuake);
        }
        const int16_t longer = static_cast<int16_t>(stop1), shorter = static_cast<int16_t>(stop2);
        const auto stopBoth = [&](int32_t stop) {
            g.Write16(two + kHitStop, static_cast<uint16_t>(stop));
            g.Write16(one + kHitStop, static_cast<uint16_t>(stop));
            g.Write16(two + kHitQuake, static_cast<uint16_t>(quake));
            g.Write16(one + kHitQuake, static_cast<uint16_t>(quake));
        };
        if (longer > shorter) {
            stopBoth(stop1);
            g.R(3) = one;
            g.SetT(true);
        } else if (shorter != 0) {
            stopBoth(stop2);
            g.R(1) = Signed(shorter);
            g.R(3) = one;
            g.SetT(false);
        } else {
            g.R(1) = 0;
            g.R(3) = Signed(longer);
            g.SetT(true);
        }
        for (const uint32_t p : {two, one}) {
            g.Write16(p + kHurtStop, 0);
        }
        for (const uint32_t p : {two, one}) {
            g.Write16(p + kHurtQuake, 0);
        }
        for (const uint32_t p : {two, one}) {
            g.Write8(p + kNoDeath, 0);
        }
        g.R(0) = kNoDeath;
        g.R(6) = one;
        g.R(7) = 0;
        g.Return(80);
        return true;
    }

    // --- guarding and parrying (defense_ground, defense_sky): 0 parried, 1 guarded, 2 neither (a hit) ---
    void CallWith(Guest &g, uint32_t routine, uint32_t as, uint32_t ds) {
        g.R(4) = as;
        g.R(5) = ds;
        g.Call(routine);
    }
    uint32_t InputOf(Guest &g, uint32_t ds) {
        return g.Read32(ds + kInput);
    }
    uint32_t CantDefend(Guest &g, uint32_t ds) {
        return g.Read32(ds + kCantDefend);
    }
    // A move under way: its record's first byte not 0 (py->flag)
    bool InMove(Guest &g, uint32_t ds) {
        return g.Read8(g.Read32(ds + kMoveState)) != 0;
    }

    // 0608DBEC (blocking_point_count_up): a parry's kind noted (kind_of_blocking: 1 one made in a parry's own steps,
    // 31-35 of routine 0, a parry just after another; 2 one made being hurt or guarding, 4-7 of routine 1), and the
    // parry graded (grade_add_blocking, for the round's results; jumped to, on the arcade whatever spmv_ng_flag says).
    // Leaves r0-r3, r5 and T as the game's code before that jump (what that routine leaves of them reaches the callers:
    // they read r5).
    bool ParryCounted(Guest &g) {
        const uint32_t ds = g.R(4);
        g.Write8(ds + kParryKind, 0);
        g.R(3) = 0;
        const int16_t routine = Word(g, ds + kSteps + 2), how = Word(g, ds + kHow);
        g.R(2) = Signed(routine);
        if (routine == 0) {
            g.R(5) = Signed(how);
            g.R(3) = 30;
            if (how > 30) {
                g.R(1) = 36;
                if (how < 36) {
                    g.R(2) = 1;
                    g.Write8(ds + kParryKind, 1);
                }
            }
        }
        g.R(0) = Signed(routine);
        g.SetT(routine == 1);
        if (routine == 1) {
            g.R(0) = 40;
            g.R(3) = 3;
            g.R(5) = Signed(how);
            g.SetT(how > 3);
            if (how > 3) {
                g.R(1) = 8;
                g.SetT(how >= 8);
                if (how < 8) {
                    g.R(0) = kParryKind;
                    g.R(2) = 2;
                    g.Write8(ds + kParryKind, 2);
                }
            }
        }
        g.R(3) = kGradeParry;
        g.Call(kGradeParry);
        g.Return(30);
        return true;
    }

    // The damage a guarded or parried attack still does (chip): the damage divided by the attack's divisor (kezuri_pow;
    // at least 1). One that would knock the defender out is kept to its health when the attack can't knock out
    // (no_death_attack); else true: not defended after all (a hit: the chip knock-out), the defence's reaction noted
    // (dm_guard_success).
    bool Chip(Guest &g, uint32_t as, uint32_t ds, uint16_t defence) {
        if (Word(g, ds + kDamage) == 0) {
            return false;
        }
        g.R(1) = Extended(Word(g, ds + kDamage));
        g.R(0) = Extended(Word(g, as + kChip));
        g.Call(kDivide16);
        g.Write16(ds + kDamage, static_cast<uint16_t>(g.R(0)));
        if (Word(g, ds + kDamage) == 0) {
            g.Write16(ds + kDamage, 1);
        }
        if (Word(g, ds + kDamage) <= Word(g, ds + kHealth)) {
            return false;
        }
        if (g.Read8(as + kNoKnockOut) != 0) {
            g.Write16(ds + kDamage, g.Read16(ds + kHealth));
            return false;
        }
        g.Write16(ds + kGuardDone, defence);
        return true;
    }

    // 0608D6E4 (check_dm_att_guard): a guarded attack's chip (Chip, the guard's reaction noted), none when the attack
    // has no divisor; r0 1 when it knocks out (the guard fails). (Its callers read r0 only: liveout.py.)
    bool GuardChip(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        Frame frame(g, 4); // (pr)
        bool through = false;
        if (Word(g, as + kChip) == 0) {
            g.Write16(ds + kDamage, 0);
        } else {
            through = Chip(g, as, ds, g.Read16(ds + kHow));
        }
        g.R(0) = through ? 1 : 0;
        g.Return(40);
        return true;
    }

    // 0608D776 (check_dm_att_blocking): a parried attack's chip: only an effect's of kind 13 whose record says so (its
    // my_effadrs's byte +26, kz_blocking) with a divisor; any other parried does no damage. r0 1 when it knocks out (the
    // parry fails; r6, the parry's reaction, noted). (Its callers read r0 only: liveout.py.)
    bool ParryChip(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        const uint16_t parry = static_cast<uint16_t>(g.R(6));
        Frame frame(g, 8); // (pr, a long)
        bool through = false;
        if (Word(g, as + kKind) == 4 && Word(g, as + kId) == 13 && g.Read8(g.Read32(as + kEffectData) + 26) != 0 &&
            Word(g, as + kChip) != 0) {
            through = Chip(g, as, ds, parry);
        } else {
            g.Write16(ds + kDamage, 0);
        }
        g.R(0) = through ? 1 : 0;
        g.Return(40);
        return true;
    }

    // A parry made (defense_ground's and defense_sky's): counted (blocking_point_count_up), the attacker's hit noted
    // parried (0x40 on the ground, 0x80 in the air), the defender's reaction (`how`: 31 high, or 32 against an attack
    // low on it, check_attbox_dir, when `how` is 0; 33 low; 34 in the air; 35 in the air against an attack from below)
    // and its chip (check_dm_att_blocking, r6 the parry's kind as the game's code passes it): 0 parried, 2 a knock-out
    // through it
    uint32_t Parried(Guest &g, uint32_t as, uint32_t ds, uint8_t hitAs, uint16_t how, uint32_t kind) {
        g.R(4) = ds;
        g.Call(kParryCounted);
        g.Write8(as + kHitAs, hitAs);
        if (how == 0) {
            g.R(4) = ds;
            g.Call(kAttackLow);
            how = g.R(0) != 0 ? 32 : 31;
        }
        g.R(6) = kind;
        g.Write16(ds + kHow, how);
        CallWith(g, kParryChip, as, ds);
        return g.R(0) != 0 ? 2 : 0;
    }

    // 0608DC44 (defense_ground): what a defender on the ground makes of an attack (r4 the attacker, r5 the defender,
    // r6 the way the attack comes from, gddir): r0 0 parried, 1 guarded, 2 hit.
    //   A parry: not in a move (py), parrying allowed (guard_flag), the attack parriable high or low (att.guard 2, 1)
    //   and the defender's window for it open: high (waza_flag[3]; against a jump's attack, waza_flag[12]) unless it
    //   may not now (spmv_ng_flag 0x100), low (waza_flag[4]) unless 0x200, the high one first. Guarding as it is hit
    //   (guard_chuu 1-4: a red parry) the window must have as many frames left as the table's for the attack (grdb: a
    //   normal move's, a special one's), whatever the attack.
    //   A guard: the attack guardable (att.guard 8 crouching, 0x10 standing, both), guarding allowed (guard_flag,
    //   spmv_ng_flag 0x10), the lever held back the way the attack comes from (saishin_lvdir) and not forward, unless
    //   it guards whatever (auto_guard), and crouching (the lever down) or standing as the attack needs: reaction 6
    //   crouching, 5 standing (4 against an attack low on it); then its chip (check_dm_att_guard), which may knock out.
    // (The arcade's: no dead_flag, nor the PS2's extra tests of spmv_ng_flag.) Leaves r6 and r7 as the game's code (r1-r5
    // and T free: liveout.py).
    bool DefenseGround(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        const int8_t from = static_cast<int8_t>(g.R(6));
        Frame frame(g, 36); // (r8-r14, pr, a long)
        g.R(7) = 1;
        g.R(5) = 2;
        // (guarding as it is hit: a red parry's window, by the attack's kind)
        const uint8_t guarding = g.Read8(ds + kGuarding);
        const bool red = guarding != 0 && guarding < 5;
        const bool normal = red && g.Read8(kNormalMove + (g.Read8(as + kMoveKind) >> 3)) == 0;
        const uint32_t redWindow = kRedParry + (normal ? 4 : 0);
        const uint8_t guard = g.Read8(as + kGuardFlags);
        if (!InMove(g, ds) && !(g.Read8(ds + kGuardMode) & 2) && (guard & 3)) {
            const uint32_t input = InputOf(g, ds);
            if ((guard & 2) && !(CantDefend(g, ds) & 0x100)) {
                const uint32_t window = !red && g.Read8(as + kJumpAttack) != 0 ? kParryJump : kParryHigh;
                if (red ? Word(g, input + window) >= Word(g, redWindow) : Word(g, input + window) != 0) {
                    g.R(0) = Parried(g, as, ds, 0x40, 0, 5);
                    g.Return(120);
                    return true;
                }
            }
            if ((guard & 1) && !(CantDefend(g, ds) & 0x200)) {
                if (red ? Word(g, input + kParryLow) >= Word(g, redWindow + 2) : Word(g, input + kParryLow) != 0) {
                    g.R(0) = Parried(g, as, ds, 0x40, 33, 6);
                    g.Return(120);
                    return true;
                }
            }
        }
        g.R(0) = 2;
        if (!(guard & 0x18) || (g.Read8(ds + kGuardMode) & 1) || (CantDefend(g, ds) & 0x10)) {
            g.Return(60);
            return true;
        }
        const uint16_t lever = g.Read16(InputOf(g, ds) + kLeverHeld);
        if (g.Read8(ds + kAutoGuard) == 0) {
            g.R(6) = Signed(from);
            if (!(g.Read8(ds + kLever) & static_cast<uint8_t>(from)) || (lever & 1)) {
                g.Return(60);
                return true;
            }
        }
        g.R(7) = 5;
        g.R(6) = 6;
        // (crouching or standing, as the attack must be guarded)
        const bool crouching = lever & 2;
        if (((guard & 0x18) == 8 && !crouching) || ((guard & 0x18) == 0x10 && crouching)) {
            g.Return(80);
            return true;
        }
        g.Write16(ds + kHow, crouching ? 6 : 5);
        g.Write8(as + kHitAs, 0x10);
        if (!crouching) {
            g.R(4) = ds;
            g.Call(kAttackLow);
            if (g.R(0) == 0) {
                g.Write16(ds + kHow, 4);
            }
        }
        CallWith(g, kGuardChip, as, ds);
        g.R(0) = g.R(0) != 0 ? 2 : 1;
        g.Return(120);
        return true;
    }

    // 0608DAE8 (defense_sky): the same for a defender in the air: a parry (reaction 34) with its air window
    // (waza_flag[5]) unless it may not now (spmv_ng_flag 0x400), or (35) with waza_flag[6] unless 0x800, the attack
    // parriable in the air (att.guard 4); a guard (7) when the attack is guardable in the air (0x20), guarding allowed
    // (guard_flag, spmv_ng_flag 0x20) and the lever held back the way it comes from; then the chip. No red parry in the
    // air, nor auto_guard, nor a test of the lever forward. Leaves r6 and r7 as the game's code (r1-r5 and T free:
    // liveout.py).
    bool DefenseSky(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        const int8_t from = static_cast<int8_t>(g.R(6));
        Frame frame(g, 12); // (r13, r14, pr)
        const uint8_t guard = g.Read8(as + kGuardFlags);
        if (!InMove(g, ds) && !(g.Read8(ds + kGuardMode) & 2) && (guard & 4)) {
            const uint32_t input = InputOf(g, ds);
            if (!(CantDefend(g, ds) & 0x400) && Word(g, input + kParryAir) != 0) {
                g.R(0) = Parried(g, as, ds, 0x80, 34, 7);
                g.Return(80);
                return true;
            }
            if (!(CantDefend(g, ds) & 0x800) && Word(g, input + kParryUp) != 0) {
                g.R(0) = Parried(g, as, ds, 0x80, 35, 7);
                g.Return(80);
                return true;
            }
        }
        g.R(0) = 2;
        if (!(guard & 0x20) || (g.Read8(ds + kGuardMode) & 1) || (CantDefend(g, ds) & 0x20)) {
            g.Return(40);
            return true;
        }
        g.R(6) = Signed(from);
        if (!(g.Read8(ds + kLever) & static_cast<uint8_t>(from))) {
            g.Return(40);
            return true;
        }
        g.Write8(as + kHitAs, 0x20);
        g.Write16(ds + kHow, 7);
        CallWith(g, kGuardChip, as, ds);
        g.R(0) = g.R(0) != 0 ? 2 : 1;
        g.Return(80);
        return true;
    }

    // --- what a hit, guard or parry passes on to the one it meets ---

    // 0608D300 (dm_reaction_init_set): the defender's reaction, its routine_no[2], from the attack's (att.reaction):
    // a running defender (running_f 1) hit by 89 or 90 from a way the table says (Dsas_dir_table, by att.dir) and
    // running into it (move_distance towards the attacker: check_work_position says which side it is on) takes 99;
    // then changed for the attack's element (change_damage_attribute: fire, electricity, ice). (Its registers are free.)
    bool ReactionSet(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        Frame frame(g, 20); // (r8, r13, r14, pr, a long)
        g.Write16(ds + kHow, g.Read8(as + kReaction));
        const int16_t how = Word(g, ds + kHow);
        if ((how == 89 || how == 90) && Byte(g, ds + kRunning) == 1 && Word(g, kRunningInto + g.Read8(as + kAttackWay) * 2u) != 0) {
            CallWith(g, kWhichSide, as, ds);
            const int16_t moving = Word(g, ds + kMoved);
            if (g.R(0) != 0 ? moving > 0 : moving < 0) {
                g.Write16(ds + kHow, 99);
            }
        }
        g.R(6) = Extended(Word(g, ds + kHow));
        g.R(5) = g.Read16(as + kElement);
        g.R(4) = as;
        g.Call(kElementReaction);
        g.Write16(ds + kHow, static_cast<uint16_t>(g.R(0)));
        g.Return(60);
        return true;
    }

    // 0608DA62 (same_dm_stop): a fighter's attack that says so (att.dipsw 1) gives the one it hits its own hit stop
    // (att.hs_me) when that one is in the air or the hit takes more than two of its health beyond what it has (a
    // knock-out): as dm_stop, below 0 (a parry's kind, its sign) when either was. (r0-r5 and T free: liveout.py.)
    bool SameStop(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        if (Word(g, as + kKind) == 1 && (g.Read8(as + kAttackFlags) & 1) &&
            (Word(g, ds + kY) > 0 || Word(g, ds + kHealth) - Word(g, ds + kDamage) <= -3)) {
            const int8_t mine = Byte(g, as + kStopMine);
            const bool negative = (Word(g, ds + kHurtStop) < 0) != (mine < 0);
            g.Write16(ds + kHurtStop, static_cast<uint16_t>(negative ? -mine : mine));
        }
        g.Return(20);
        return true;
    }

    // 0608DFCC (dm_status_copy): what the hit carries copied from the attacker to the one it meets (dm_attlv, the
    // impact, the way, its hit stop and shake (att.hs_you, the shake its size), the attacker's weight, the button, ...,
    // whose player hit it: dm_plnum, a fighter's or an effect's owner's), the attacker given its own hit stop
    // (att.hs_me), and meoshi_hit_flag set. Leaves r1 and r2 as the game's code (r0, r3, r4 and T free: liveout.py).
    bool StatusCopy(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        g.Write16(ds + 0x328, g.Read8(as + 0x2DD));                                  // dm_attlv (att.level)
        g.Write8(ds + 0x32D, g.Read8(as + 0x2E5));                                   // dm_impact
        g.Write16(ds + 0x32A, g.Read16(as + 0x2FA));                                 // dm_dir (dir_atthit)
        g.Write16(ds + kHurtStop, static_cast<uint16_t>(Byte(g, as + kStopTheirs))); // dm_stop
        const int16_t quake = Byte(g, as + kStopTheirs);
        g.Write16(ds + kHurtQuake, static_cast<uint16_t>(quake < 0 ? -quake : quake)); // dm_quake
        g.Write8(ds + 0x31D, g.Read8(as + 0xA8));                                    // dm_weight (weight_level)
        g.Write16(ds + 0x31E, g.Read8(as + 0x2DF));                                  // dm_butt_type (att.but_ix)
        g.Write16(ds + 0x320, g.Read16(as + 0x2F6));                                 // dm_zuru (att_zuru)
        g.Write16(ds + 0x322, g.Read16(as + kElement));                              // dm_attribute
        g.Write16(ds + 0x334, g.Read16(as + 0x2F8));                                 // dm_ten_ix (at_ten_ix)
        g.Write16(ds + 0x336, g.Read16(as + 0x300));                                 // dm_koa (at_koa)
        g.Write16(ds + 0x35C, g.Read8(as + 0x2EB));                                  // hm_dm_side (att.dmg_mark)
        g.Write16(ds + 0x338, g.Read16(as + kKind));                                 // dm_work_id
        g.Write16(as + kHitStop, static_cast<uint16_t>(Byte(g, as + kStopMine)));    // hit_stop
        g.Write16(ds + 0x33A, g.Read16(as + 0x2F2));                                 // dm_arts_point (add_arts_point)
        g.Write8(ds + 0x343, g.Read8(as + kMoveKind));                               // dm_kind_of_waza
        g.Write8(ds + kNoDeath, g.Read8(as + kNoKnockOut));                          // dm_nodeathattack
        g.Write8(ds + 0x33C, g.Read8(as + kJumpAttack));                             // dm_jump_att_flag
        uint16_t player;
        if (Word(g, as + kKind) == 1) {
            g.Write8(ds + 0x341, g.Read8(as + 0x44E)); // dm_exdm_ix
            player = g.Read16(as + kCharacter);
            g.R(1) = ds + 0x338;
            g.SetT(true);
        } else {
            g.R(1) = g.Read32(as + kOwnerRecord);
            player = g.Read16(g.R(1) + kCharacter);
            g.SetT(false);
        }
        g.Write16(ds + 0x326, player); // dm_plnum
        g.Write8(as + 0x2FF, 1);       // meoshi_hit_flag
        g.R(0) = 0x2FF;
        g.R(2) = 1;
        g.R(3) = Extended(static_cast<int16_t>(player));
        g.Return(60);
        return true;
    }

    void Spark(Guest &g, uint32_t as, uint32_t ds, uint32_t kind);
    void ParriesOver(Guest &g, uint32_t ds);
    uint32_t Damage(Guest &g, uint32_t as, uint32_t ds, int8_t from);
    void Caught(Guest &g, uint32_t as, uint32_t ds, bool air, int8_t from, int16_t windows);

    // 0608D638 (hit_pattern_extdat_check): what the attacker's animation says to do on a hit (cg_extdat: 0x40 hold
    // the frame, cg_ctr 1; its low six bits the frame to go to, cg_ix, the commands' state put back first,
    // setup_comm_abbak; 0x80 move along z, char_move_z, jumped to). (The PS2's tests of cancels after it are not the
    // arcade's.) Leaves r1 as the game's code: the callers' callers read it (through tail jumps).
    bool HitPatternExtra(Guest &g) {
        const uint32_t as = g.R(4);
        const uint8_t extra = g.Read8(as + kAnimationExtra);
        const uint32_t frameTo = extra & 0x3F;
        const uint32_t which = (extra & 0xC0) + (frameTo != 0 ? 1 : 0);
        const bool moveZ = which == 0x80 || which == 0x81;
        {
            Frame frame(g, 12); // (r14, pr, macl)
            // (r1 as the switch's tests leave it)
            g.R(1) = frameTo != 0 ? 1 : 0;
            if (which != 1 && which != 0x40 && which != 0x41) {
                g.R(1) = moveZ ? which : 0x81;
            }
            if (which == 0x40 || which == 0x41) {
                g.Write8(as + kAnimationHold, 1);
            }
            if (which == 1 || which == 0x41 || which == 0x81) {
                g.R(4) = as;
                g.Call(kCommandsBack);
                const int16_t step = Word(g, as + kAnimationStep);
                g.Write16(as + kAnimationFrame, static_cast<uint16_t>(static_cast<int16_t>(frameTo - 1) * step - step));
                g.Write8(as + kAnimationNext, 0);
                g.R(1) = as + kAnimationFrame;
            }
        }
        if (moveZ) {
            g.R(4) = as;
            g.Call(kMoveZ);
        }
        g.Return(40);
        return true;
    }

    // The hit's end for the one hit, after its reaction is set (the end of plef_at_vs_player_damage_union): it is hurt
    // (routine 1, from its first step), the hit graded (grade_add_clean_hits), a guard missed noted (check_guard_miss),
    // the hit's spark (effect_02_init), what the hit carries (dm_status_copy, same_dm_stop), the attacker's hit counted
    // (cmwk[8], cmwk[15]) and the hits taken (dm_count_up), its height not below the floor, the combo counted
    // (add_combo_work), the attacker's animation's hit frame (hit_pattern_extdat_check); a parry-proof move (atemi)
    // hit elsewhere than it catches is undone, and the parries in a row for the bonus are over (paring_ctr_vs,
    // paring_counter, paring_bonus_r).
    void Hurt(Guest &g, uint32_t as, uint32_t ds, int8_t from) {
        g.Write16(ds + kSteps + 2, 1);
        g.Write16(ds + kSteps + 6, 0);
        g.R(4) = as;
        g.Call(kGradeHit);
        g.R(6) = Signed(from);
        CallWith(g, kGuardMissed, as, ds);
        Spark(g, as, ds, 1);
        CallWith(g, kStatusCopy, as, ds);
        CallWith(g, kSameStop, as, ds);
        g.Write16(as + kHitsMade, static_cast<uint16_t>(g.Read16(as + kHitsMade) + 1));
        g.Write16(as + kHitsMade2, static_cast<uint16_t>(g.Read16(as + kHitsMade2) + 1));
        g.Write16(ds + kHitsTaken, static_cast<uint16_t>(g.Read16(ds + kHitsTaken) + 1));
        if (Word(g, ds + kY) < 0) {
            g.Write32(ds + kY, 0);
        }
        CallWith(g, kComboCounted, as, ds);
        g.R(4) = as;
        g.Call(kHitPatternExtra);
        if (g.Read8(ds + kAtemi) != 0 && g.Read8(ds + kAtemiPoint) != g.Read8(ds + kHitPoint)) {
            g.Write8(ds + kAtemi, 0);
        }
        ParriesOver(g, ds);
    }
    // The hit's spark (effect_02_init: r5 where it was hit, dm_point; r6 1 a hit, 2 a guard; r7 the way it faces)
    void Spark(Guest &g, uint32_t as, uint32_t ds, uint32_t kind) {
        g.R(7) = Signed(Byte(g, ds + kHurtFacing));
        g.R(6) = kind;
        g.R(5) = g.Read8(ds + kHitPoint);
        g.R(4) = as;
        g.Call(kSpark);
    }
    void ParriesOver(Guest &g, uint32_t ds) {
        const uint32_t id = Signed(Word(g, ds + kId));
        g.Write8(kParriesInRow + Signed(Word(g, kPlayType)) * 2 + id, 0);
        g.Write8(kParryPoints + id, 0);
        g.Write8(kParryBonus + id, 0);
    }

    // A reaction looked up in one of the game's tables of words by the reaction (less 32): `index` as the game's code
    // takes it, unsigned (extu.w) or signed
    uint16_t ReactionWord(Guest &g, uint32_t table, int16_t how) {
        return g.Read16(table + static_cast<uint16_t>(how - 32) * 2u);
    }
    uint8_t ReactionByte(Guest &g, uint32_t table, int16_t how) {
        return g.Read8(table + Signed(static_cast<int16_t>(how - 32)));
    }

    // 0608D02C (plef_at_vs_player_damage_union): what an attack makes of a fighter it meets (r4 the attacker, a fighter
    // or an effect; r5 the fighter; r6 the way it comes from): in the air (or in a pose that counts as the air,
    // check_pat_status) defense_sky, on the ground defense_ground; parried, set_paring_status; guarded,
    // set_guard_status; else hit. Neither is tried when it can't defend (guard_flag 3), the attack can't be defended
    // (att.guard 0) or it is in a move: then it is hit on the ground in poses 10 and 12 (low), its height put to the
    // floor; and in the air or on the ground as it is when hurt already.
    // A hit: its reaction from the attack's (dm_reaction_init_set), in the air one of the air's (a special move's,
    // att.dipsw 0x10, or a normal one's); on the ground (unless the attack's reaction stands, zu_flag) one for the
    // pose (32 on: kagami) or where it was hit (dm_point 0-1 the head, 4-7 the hands, the rest the body, each with its
    // table of reactions that depend on the way the attack came, get_kind_of_head_dm, get_kind_of_trunk_dm). Then the
    // hit's end (Hurt). Leaves r1 as the game's code (the routines last called leave it: the rest is free).
    bool DamageUnion(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        uint32_t defended = 0; // (set_paring_status or set_guard_status, jumped to)
        {
            Frame frame(g, 20); // (r11-r14, pr)
            defended = Damage(g, as, ds, static_cast<int8_t>(g.R(6)));
        }
        if (defended != 0) {
            CallWith(g, defended, as, ds);
        }
        g.Return(200);
        return true;
    }
    uint32_t Damage(Guest &g, uint32_t as, uint32_t ds, int8_t from) {
        enum { Sky, Ground, HitInAir, HitOnGround } way;
        {
            g.Write16(ds + kGuardDone, 0xFFFF);
            const bool defenceless = g.Read8(ds + kGuardMode) == 3 || Byte(g, as + kGuardFlags) == 0 || InMove(g, ds);
            const auto inAir = [&] {
                if (Word(g, ds + kY) > 0) {
                    return true;
                }
                g.R(4) = ds;
                g.Call(kAirPose);
                return g.R(0) != 0;
            };
            const uint8_t pose = g.Read8(ds + kPose);
            if (defenceless && (pose == 10 || (pose == 12 && Word(g, ds + kY) < 6))) {
                g.Write32(ds + kY, 0);
                way = Ground;
            } else if (defenceless && Word(g, ds + kSteps + 2) == 1) {
                way = inAir() ? HitInAir : HitOnGround;
            } else {
                way = inAir() ? Sky : Ground;
            }
            if (way == Sky || way == Ground) {
                g.R(6) = Signed(from);
                CallWith(g, way == Sky ? kDefenseSky : kDefenseGround, as, ds);
                if (g.R(0) == 0 || g.R(0) == 1) {
                    return g.R(0) == 0 ? kParryStatus : kGuardStatus;
                }
                way = way == Sky ? HitInAir : HitOnGround;
            }
            // (hit: its reaction)
            g.Write8(as + kHitAs, way == HitInAir ? 2 : 1);
            CallWith(g, kReactionSet, as, ds);
            const int16_t how = Word(g, ds + kHow);
            if (way == HitInAir) {
                const uint32_t table = (g.Read8(as + kAttackFlags) & 0x10) ? kAirSpecialReactions : kAirReactions;
                g.Write16(ds + kHow, ReactionWord(g, table, how));
            } else if (Word(g, as + kReactionStands) == 0) {
                const uint8_t point = g.Read8(ds + kHitPoint);
                const auto byWay = [&](uint32_t routine) {
                    g.R(5) = Signed(Byte(g, ds + kHurtFacing));
                    g.R(4) = Extended(Word(g, as + kAttackDir));
                    g.Call(routine);
                    g.Write16(ds + kHow, static_cast<uint16_t>(g.R(0)));
                };
                if (g.Read8(ds + kPose) >= 32) {
                    g.Write16(ds + kHow, ReactionWord(g, kMirrorReactions, how));
                } else if (point == 0 || point == 1) {
                    if (ReactionByte(g, kHeadReactions, how) != 0) {
                        byWay(kHeadReaction);
                    }
                } else {
                    if (point >= 4 && point <= 7) {
                        g.Write16(ds + kHow, ReactionWord(g, kHandReactions, how));
                    }
                    if (ReactionByte(g, kBodyReactions, Word(g, ds + kHow)) != 0) {
                        byWay(kBodyReaction);
                    }
                }
            }
            Hurt(g, as, ds, from);
        }
        return 0;
    }

    // 0608D38C (set_guard_status): a guard made: unless the attack stops no one (att.hs_you and hs_me 0: the reaction
    // put back, old_rno[2]), the defender in its guard (routine 1, its first step), the guard's spark, what the hit
    // carries (dm_status_copy, same_dm_stop), its height not below the floor, no stun, the attacker's hit counted
    // (cmwk[8]), the super art gauge for the attack guarded (add_sp_arts_gauge_guard), none for the defender
    // (dm_arts_point), the guard graded (grade_add_guard_success). Then the attacker's animation's hit frame
    // (hit_pattern_extdat_check, jumped to). (On the arcade the spark is made whatever spmv_ng_flag says.)
    bool GuardStatus(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        {
            Frame frame(g, 16); // (r12-r14, pr)
            if (Byte(g, as + kStopTheirs) == 0 && Byte(g, as + kStopMine) == 0) {
                g.Write16(ds + kHow, g.Read16(ds + kHowBefore));
            } else {
                g.Write16(ds + kSteps + 2, 1);
                g.Write16(ds + kSteps + 6, 0);
                Spark(g, as, ds, 2);
                CallWith(g, kStatusCopy, as, ds);
                CallWith(g, kSameStop, as, ds);
                if (Word(g, ds + kY) < 0) {
                    g.Write32(ds + kY, 0);
                }
                g.Write16(ds + kStun, 0);
                g.Write16(as + kHitsMade, static_cast<uint16_t>(g.Read16(as + kHitsMade) + 1));
                g.R(4) = as;
                g.Call(kGuardGauge);
                g.Write16(ds + kArtsPoints, 0);
                g.R(4) = Extended(Word(g, ds + kId));
                g.Call(kGradeGuard);
            }
        }
        g.R(4) = as;
        g.Call(kHitPatternExtra);
        g.Return(100);
        return true;
    }

    // 0608D444 (set_paring_status): a parry made: unless the attack stops no one (the reaction put back), the defender
    // out of its hurt and guard (routine 0, step 0) with its moves' state put back (waza_compel_all_init2), what the hit
    // carries (dm_status_copy), no stun, cg_type 0, and the hit stops: the parrier's -15 (a parry's), the attacker's 16,
    // a normal move's on the ground more by its strength (sel_hs_add_tbl: kind_of_waza / 2, 0-3; a special move 4),
    // for the parries 31-35 (reaction 31 on the ground... 35); none for others. No shake for the parrier, its height not
    // below the floor, no super art gauge for it; a fighter in a pose of the air (14-30) parried in a normal move
    // falls back (remake_mvxy_PoGR); out of the bonus games, the parries in a row counted (paring_ctr_vs, at most 39) and
    // the bonus's points for them (paring_counter, by Play_Type), paring_bonus_r set; the attacker's hit counted
    // (cmwk[8]). Then the attacker's animation's hit frame (jumped to). (On the arcade the parries are counted whatever
    // spmv_ng_flag says.)
    bool ParryStatus(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        {
            Frame frame(g, 20); // (r11-r14, pr)
            if (Byte(g, as + kStopTheirs) == 0 && Byte(g, as + kStopMine) == 0) {
                g.Write16(ds + kHow, g.Read16(ds + kHowBefore));
            } else {
                const uint8_t kind = g.Read8(as + kMoveKind);
                const uint32_t strength = (kind & 0xF8) ? 4 : (kind >> 1) & 3;
                g.Write16(ds + kSteps + 2, 0);
                g.Write16(ds + kSteps + 6, 0);
                g.R(4) = ds;
                g.Call(kMovesReset);
                CallWith(g, kStatusCopy, as, ds);
                g.Write16(ds + kStun, 0);
                g.Write8(ds + kCgType, 0);
                const uint32_t which = static_cast<uint32_t>((Word(g, ds + kHow) - 31) * 2 + (Word(g, as + kY) > 0 ? 1 : 0));
                int16_t parrier = -15, attacker = 16;
                if (which >= 10) {
                    parrier = attacker = 0;
                } else if (which == 0 || which == 2 || which == 4) {
                    attacker = static_cast<int16_t>(Word(g, kParryStopMore + strength * 2) + 16);
                }
                g.Write16(ds + kHurtStop, static_cast<uint16_t>(parrier));
                g.Write16(as + kHitStop, static_cast<uint16_t>(attacker));
                g.Write16(as + kHitQuake, static_cast<uint16_t>(attacker));
                g.Write16(ds + kHurtQuake, 0);
                if (Word(g, ds + kY) < 0) {
                    g.Write32(ds + kY, 0);
                }
                g.Write16(ds + kArtsPoints, 0);
                const uint8_t pose = g.Read8(as + kPose);
                if (pose >= 14 && pose <= 30 && Word(g, as + kKind) == 1 && g.Read8(kNormalMove + (g.Read8(as + kMoveKind) >> 3)) == 0) {
                    g.R(4) = as;
                    g.Call(kFallBack);
                }
                if (Word(g, kBonusGame) == 0) {
                    const uint32_t id = Signed(Word(g, ds + kId));
                    g.Write8(kParryBonus + id, 1);
                    const uint32_t row = kParriesInRow + Signed(Word(g, kPlayType)) * 2 + id;
                    g.Write8(row, static_cast<uint8_t>(g.Read8(row) + 1));
                    if (g.Read8(row) > 39) {
                        g.Write8(row, 39);
                    }
                    const uint32_t points = kParryPointsTable + Signed(static_cast<int8_t>(Byte(g, kPlayType + 1) * 40));
                    g.Write8(kParryPoints + id, g.Read8(points + g.Read8(row) - 1));
                }
                g.Write16(as + kHitsMade, static_cast<uint16_t>(g.Read16(as + kHitsMade) + 1));
            }
        }
        g.R(4) = as;
        g.Call(kHitPatternExtra);
        g.Return(100);
        return true;
    }

    // A value scaled by a fighter's handicap word (tk_nage, tk_dageki, tk_kizetsu: x(32 + it)/32), which then drops
    // by 2 a hit, not below 0
    int32_t Handicapped(Guest &g, int32_t value, uint32_t at) {
        const int32_t scaled = value * (Word(g, at) + 32) / 32;
        const int16_t left = static_cast<int16_t>(Word(g, at) - 2);
        g.Write16(at, static_cast<uint16_t>(left < 0 ? 0 : left));
        return scaled;
    }

    // The damage more in some poses of the one hit: x1.25 in 3, 25 and 32; x1.5 in 7, 23 and 35; x2 in 1, 21 and 37
    void PoseScaled(Guest &g, uint32_t ds) {
        const uint8_t pose = g.Read8(ds + kPose);
        const auto scale = [&](int32_t percent) {
            g.R(1) = Signed(Word(g, ds + kDamage) * percent);
            g.R(0) = 100;
            g.Call(kDivide);
            g.Write16(ds + kDamage, static_cast<uint16_t>(g.R(0)));
        };
        if (pose == 32 || pose == 3 || pose == 25) {
            scale(125);
        } else if (pose == 7 || pose == 23 || pose == 35) {
            scale(150);
        } else if (pose == 1 || pose == 21 || pose == 37) {
            g.Write16(ds + kDamage, static_cast<uint16_t>(Word(g, ds + kDamage) * 2));
        }
    }

    // 0608D7FC (set_damage_and_piyo): a hit's damage and stun. The damage (cal_damage_vitality, lifts.cpp's
    // BaseDamage), more on the one hit in some poses (PoseScaled);
    // the stun from the table by the attacker's character and the attack's (att.piyo); both scaled by the attacker's
    // handicaps for a throw (routine 2: tk_nage), a hit (4: tk_dageki) and the stun (tk_kizetsu), which then drop, and
    // passed on to the one hit (utk_*); the score's index for the damage (remake_score_index); the combo's scaling of
    // the damage (cal_combo_waribiki), its health gauge's (cal_dm_vital_gauge_hosei, lifts.cpp's ScaleDamage) and the
    // combo's of the stun (cal_combo_waribiki2); a fighter's damage kept back for later added (dm_vital_use 1: all of
    // it, 2: half, the rest kept). (The arcade's: no stun_gauge_omake.) Leaves r0-r2, r7 and T as the game's code.
    bool DamageAndStun(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        Frame frame(g, 20); // (r12-r14, pr, macl)
        g.Call(kBaseDamage);
        g.Write16(ds + kStun, g.Read8(kStunTable + Signed(Word(g, as + kCharacter)) * 16 + g.Read8(as + kAttackStun)));
        PoseScaled(g, ds);
        if (Word(g, ds + kDamage) != 0) {
            const int16_t routine = Word(g, as + kSteps + 2);
            if (routine == 2) {
                g.Write16(ds + kDamage, static_cast<uint16_t>(Handicapped(g, Word(g, ds + kDamage), as + kThrowHandicap)));
            }
            if (routine == 4) {
                g.Write16(ds + kDamage, static_cast<uint16_t>(Handicapped(g, Word(g, ds + kDamage), as + kHitHandicap)));
            }
            g.Write16(ds + kThrowHandicap + 8, g.Read16(as + kThrowHandicap));
            g.Write16(ds + kHitHandicap + 8, g.Read16(as + kHitHandicap));
        }
        if (Word(g, ds + kStun) != 0) {
            g.Write16(ds + kStun, static_cast<uint16_t>(Handicapped(g, g.Read16(ds + kStun), as + kStunHandicap)));
            g.Write16(ds + kStunHandicap + 8, g.Read16(as + kStunHandicap));
        }
        g.R(4) = Extended(Word(g, ds + kDamage));
        g.Call(kScoreIndex);
        g.Write16(as + kScoreIx, static_cast<uint16_t>(g.R(0)));
        CallWith(g, kComboDiscount, as, ds);
        g.R(4) = ds;
        g.Call(kScaleDamage);
        g.R(4) = ds;
        g.Call(kStunDiscount);
        // (a fighter's damage kept back)
        g.R(0) = Extended(Word(g, as + kKind));
        g.SetT(Word(g, as + kKind) == 1);
        if (Word(g, as + kKind) == 1) {
            const uint8_t use = g.Read8(as + kKeptUse);
            g.R(0) = use;
            g.SetT(use == 1 || use == 2);
            if (use == 1 || use == 2) {
                if (use == 2) {
                    const int16_t kept = Word(g, as + kKept);
                    const int32_t rounded = kept + (kept < 0 ? 1 : 0);
                    g.SetT(rounded & 1);
                    g.Write16(as + kKept, static_cast<uint16_t>(rounded >> 1));
                }
                g.R(3) = Extended(Word(g, as + kKept));
                g.R(2) = Signed(Word(g, ds + kDamage) + Word(g, as + kKept));
                g.Write16(ds + kDamage, static_cast<uint16_t>(g.R(2)));
                g.R(0) = kDamage;
                if (use == 1) {
                    g.Write16(as + kKept, 0);
                    g.R(0) = kKept;
                }
            }
        }
        g.Return(120);
        return true;
    }

    // --- what was hit (set_judge_result's: fight_hits.cpp) ---
    constexpr uint32_t kQueue = 0x0200EBC4;   // longs: the records queued this frame for the hit tests (q_hit_push)
    constexpr uint32_t kResults = 0x0200EC44; // 16 bytes each, by the queue's order (hs): fight_hits.cpp
    constexpr uint32_t kHit = 0x10;           // long: the record its attack hit (hit_adrs)
    constexpr uint32_t kHitKind = 0x364, kHurtKind = 0x365; // bytes: the other's work_id (hit_work_id, dmg_work_id)
    constexpr uint32_t kAttackLive = 0x2FE;   // byte: its attack may still hit (att_hit_ok)
    constexpr uint32_t kGuardWay = 0x0611E136; // get_guard_direction (r4 the attacker, r5 the defender): r0 the way
    constexpr uint32_t kLeverWays = 0x0611E996; // setup_saishin_lvdir (r4 the defender, r5 the way)
    constexpr uint32_t kHurtWay = 0x0608DF2A;  // setup_dm_rl (r4, r5)
    constexpr uint32_t kSparkPlace = 0x0608CF62; // cal_hit_mark_pos (r4, r5, r6 the attacker's place in the queue, r7
                                                 // the other's)
    constexpr uint32_t kDamageAndStun = 0x0608D7FC, kDamageUnion = 0x0608D02C;
    constexpr uint32_t kPlayerHitsPlayer = 0x0608ECC4, kEffectHitsPlayer = 0x0608F008;
    constexpr uint32_t kPlayerHitsEffect = 0x0608EEA0, kEffectHitsEffect = 0x0608F290;

    uint32_t Results(int16_t at) {
        return kResults + Signed(at) * 16;
    }
    uint32_t Queued(Guest &g, int16_t at) {
        return g.Read32(kQueue + Signed(at) * 4);
    }
    int16_t Struck(Guest &g, int16_t at) { // (whom it hit: hs.my_hit)
        return static_cast<int16_t>(g.Read16(Results(at) + 4));
    }
    int16_t StruckBy(Guest &g, int16_t at) { // (who hit it: hs.dm_me)
        return Word(g, Results(at) + 6);
    }

    // 0608CE68 (set_struck_status): the record queued at r4 was hit (its results' 1): it and its attacker (dm_me) told
    // of each other (hit_adrs, dmg_adrs, the kinds), and what came of it decided by who hit whom: a fighter a fighter
    // (player_at_vs_player_dm), an effect a fighter (effect_at_vs_player_dm; but an effect that hit the fighter that
    // hit it back keeps its attack, att_hit_ok, and does nothing), a fighter an effect (player_at_vs_effect_dm), an
    // effect an effect (effect_at_vs_effect_dm); those jumped to, r4 the attacker's place, r5 the one hit's. (Waits,
    // as the game's code, until the attacker's results say whom it hit is this one: they always do.) Leaves r1 as the
    // game's code (the routines jumped to leave it).
    bool StruckStatus(Guest &g) {
        const int16_t ix = static_cast<int16_t>(g.R(4));
        const int16_t ix2 = StruckBy(g, ix);
        const uint32_t as = Queued(g, ix2), ds = Queued(g, ix);
        g.Write32(as + kHit, ds);
        g.Write32(ds + kHurtBy, as);
        g.Write8(as + kHitKind, static_cast<uint8_t>(g.Read16(ds + kKind)));
        g.Write8(ds + kHurtKind, static_cast<uint8_t>(g.Read16(as + kKind)));
        const bool fighter = Word(g, as + kKind) == 1, fighterHit = Word(g, ds + kKind) == 1;
        g.R(6) = ds;
        g.R(5) = as;
        g.R(3) = fighter ? 1 : 0;
        g.R(7) = Signed(ix2);
        uint32_t next;
        if (fighter && fighterHit) {
            next = kPlayerHitsPlayer;
        } else if (fighterHit) {
            if ((g.Read16(Results(ix)) & 0x10) && Struck(g, ix) == ix2) {
                g.Write8(as + kAttackLive, 1);
                g.R(0) = kAttackLive;
                g.R(2) = 1;
                g.R(4) = Results(ix);
                g.Return(40);
                return true;
            }
            next = kEffectHitsPlayer;
        } else if (fighter) {
            next = kPlayerHitsEffect;
        } else {
            next = kEffectHitsEffect;
        }
        g.R(0) = (fighter ? 1 : 0) + (fighterHit ? 2 : 0);
        g.R(2) = next;
        g.R(4) = Signed(ix2);
        g.R(5) = Signed(ix);
        g.Call(next);
        g.Return(40);
        return true;
    }

    // Which of two attacks that met each other wins (player_at_vs_player_dm's): a throw that takes the hurt (att.dipsw
    // 0x40) beats anything but another; a throw (0x20) or a move that is a throw (kind_of_waza 4) beats anything but a
    // throw that takes the hurt (which beats it), another throw or a throw move (both stand); a special move (kind 2)
    // loses to throws and throw moves, beats normal moves and stands with another special; a normal move loses to
    // anything but another normal move (both stand). Win: the other's hit undone; Lose: this one's; Both: both stand.
    enum class Trade { Both, Win, Lose };
    Trade Traded(Guest &g, uint32_t as, uint32_t ds) {
        const uint8_t mine = g.Read8(as + kAttackFlags), theirs = g.Read8(ds + kAttackFlags);
        const uint8_t myKind = g.Read8(as + kMoveKind), theirKind = g.Read8(ds + kMoveKind);
        if (mine & 0x40) {
            return theirs & 0x40 ? Trade::Both : Trade::Win;
        }
        if ((mine & 0x20) || (myKind & 4)) {
            if (theirs & 0x40) {
                return Trade::Lose;
            }
            return (theirs & 0x20) || (theirKind & 4) ? Trade::Both : Trade::Win;
        }
        if (myKind & 2) {
            if ((theirs & 0x60) || (theirKind & 4)) {
                return Trade::Lose;
            }
            return theirKind & 2 ? Trade::Both : Trade::Win;
        }
        // (a normal move)
        if ((theirs & 0x60) || (theirKind & 4) || (theirKind & 2)) {
            return Trade::Lose;
        }
        return Trade::Both;
    }

    // 0608ECC4 (player_at_vs_player_dm): a fighter's attack hit a fighter (r4 the attacker's place in the queue, r5 the
    // other's). When the two hit each other, which hit stands (Traded): a lost hit undone in both one's results and
    // nothing more; then where it was hit (dm_point, the box hit), the way it comes from (get_guard_direction) and the
    // lever's ways for it (setup_saishin_lvdir), the way it is hit towards (setup_dm_rl), the spark's place
    // (cal_hit_mark_pos), the damage and stun (set_damage_and_piyo), and what it makes of it
    // (plef_at_vs_player_damage_union, jumped to, r6 the way). (The arcade's: no pp_pulpara_hit.) Leaves r1 as the game's
    // code.
    bool PlayerHitsPlayer(Guest &g) {
        const int16_t ix2 = static_cast<int16_t>(g.R(4)), ix = static_cast<int16_t>(g.R(5));
        const uint32_t as = Queued(g, ix2), ds = Queued(g, ix);
        int8_t from = 0;
        {
            Frame frame(g, 36); // (r9-r14, pr, two longs)
            const uint32_t mine = Results(ix2), theirs = Results(ix);
            g.R(4) = mine;
            g.R(6) = kResults;
            g.R(7) = Signed(ix) * 16;
            if ((g.Read16(mine) & 1) && StruckBy(g, ix2) == ix) {
                // (they hit each other)
                g.R(5) = 4;
                const Trade trade = Traded(g, as, ds);
                if (trade == Trade::Lose) {
                    g.Write16(mine, g.Read16(mine) & 0x1101);
                    g.Write16(theirs, g.Read16(theirs) & 0x1110);
                    g.R(0) = g.Read16(theirs);
                    g.R(1) = 0x1110;
                    g.R(2) = theirs;
                    g.R(3) = 0x1101;
                    g.Return(60);
                    return true;
                }
                if (trade == Trade::Win) {
                    g.Write16(mine, g.Read16(mine) & 0x1110);
                    g.Write16(theirs, g.Read16(theirs) & 0x1101);
                }
            }
            g.Write8(ds + kHitPoint, g.Read8(theirs + 3));
            CallWith(g, kGuardWay, as, ds);
            from = static_cast<int8_t>(g.R(0));
            g.R(5) = Signed(from);
            g.R(4) = ds;
            g.Call(kLeverWays);
            CallWith(g, kHurtWay, as, ds);
            g.R(7) = Signed(ix);
            g.R(6) = Signed(ix2);
            CallWith(g, kSparkPlace, as, ds);
            CallWith(g, kDamageAndStun, as, ds);
        }
        g.R(6) = Signed(from);
        g.R(3) = kDamageUnion;
        CallWith(g, kDamageUnion, as, ds);
        g.Return(100);
        return true;
    }

    // 0608CD34 (check_pat_status): r0 1 in a pose that counts as the air (14-30). Leaves r1 as the game's code.
    bool AirPose(Guest &g) {
        const uint8_t pose = g.Read8(g.R(4) + kPose);
        g.R(3) = 14;
        g.R(5) = pose;
        g.SetT(pose >= 14);
        if (pose >= 14) {
            g.R(1) = 30;
            g.SetT(pose > 30);
        }
        g.R(0) = pose >= 14 && pose <= 30 ? 1 : 0;
        g.Return(10);
        return true;
    }

    // 0608CD90 (check_blocking_flag): which of the two have a parry window open (the high or the low, waza_flag[3] or
    // [4]): r0 1 the one caught (r5), 2 the thrower (r4), 3 both. (Callers read r0 only: liveout.py.)
    bool ParryWindows(Guest &g) {
        const auto open = [&](uint32_t record) {
            const uint32_t input = InputOf(g, record);
            return Word(g, input + kParryHigh) + Word(g, input + kParryLow) != 0;
        };
        g.R(0) = (open(g.R(5)) ? 1 : 0) + (open(g.R(4)) ? 2 : 0);
        g.Return(20);
        return true;
    }

    // 0608CDEA (set_catch_hit_mark_pos): a throw's spark's place: from the thrower's place by its table's offset for the
    // attack (att.mkh_ix, hit_mark_hosei_table, mirrored with its facing), else where its throw's box and the caught
    // one's meet (cal_hit_mark_position, jumped to). (Its registers are free.)
    bool CatchSparkPlace(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        const uint8_t mark = g.Read8(as + kSparkMark);
        if (mark == 0) {
            g.R(4) = ds;
            g.R(5) = as;
            g.R(6) = g.Read32(ds + kCaughtBox);
            g.R(7) = g.Read32(as + kCatchBox);
            g.Call(kBoxesMeet);
            g.Return(30);
            return true;
        }
        const uint32_t offset = kSparkOffsets + mark * 4u;
        const int16_t across = Word(g, offset);
        g.Write16(as + kSparkX, static_cast<uint16_t>(Word(g, as + kX) + (g.Read8(as + kFacing) != 0 ? -across : across)));
        g.Write16(as + kSparkY, static_cast<uint16_t>(Word(g, as + kY) + Word(g, offset + 2)));
        g.Return(30);
        return true;
    }

    // 0608DF2A (setup_dm_rl): the way the one hit is hit towards (dm_rl): the attacker's facing, but for a fighter's
    // attack whose reaction doesn't ask it (check_ttk_damage_request) and that is a throw (att.dipsw 0x60) or comes from
    // or into the air: away from the attacker, from where the two stand (its facing when level). Leaves r2 as the game's
    // code (the rest free: liveout.py).
    bool HurtWay(Guest &g) {
        const uint32_t as = g.R(4), ds = g.R(5);
        Frame frame(g, 12); // (r13, r14, pr)
        bool facing = true;
        if (Word(g, as + kKind) == 1) {
            g.R(4) = g.Read8(as + kReaction);
            g.Call(kReactionFacing);
            if (g.R(0) == 0 && (Word(g, ds + kY) > 0 || (g.Read8(as + kAttackFlags) & 0x60))) {
                const int16_t apart = static_cast<int16_t>(Word(g, ds + kX) - Word(g, as + kX));
                {
                    g.R(2) = Signed(apart);
                    if (apart > 0) {
                        g.R(2) = 1;
                        g.Write8(ds + kHurtFacing, 1);
                        facing = false;
                    } else if (apart < 0) {
                        g.Write8(ds + kHurtFacing, 0);
                        facing = false;
                    } else {
                        g.R(2) = ds + kHurtFacing;
                    }
                }
            }
        }
        if (facing) {
            g.Write8(ds + kHurtFacing, g.Read8(as + kFacing));
        }
        g.Return(40);
        return true;
    }

    // 0608C994 (set_caught_status): the record queued at r4 was caught by a throw (its results' 0x100). When the two
    // caught each other, one throw stands: a throw that takes the hurt (att.dipsw 0x40) beats one that doesn't, a throw
    // (0x20) beats a fighter with neither; else by the parry windows open (check_blocking_flag: the one with a window
    // open can't be thrown out of it, hazusenai_flag) and otherwise by the frame (Game_timer's last bit; both may
    // break the throw, cat_break_reserve). A lost throw undone in both one's results and nothing more. Then: the two
    // told of each other, caught at the head (dm_point 1), the way and the lever's ways (as a hit's), the spark's place
    // (set_catch_hit_mark_pos), the damage and stun; a throw may be parried or guarded as an attack is
    // (defense_sky, defense_ground: set_paring_status, set_guard_status jumped to; neither tried as for
    // plef_at_vs_player_damage_union), else the caught one's reaction from the throw's, in the air or not; the thrower
    // to its throw's step (the cmcr or cmcf one: by its facing against the other's, and whether the other is knocked
    // about, cg_type 10); the caught one held (routine 3, the throw's ng_type; at the thrower's height for ng_type 2),
    // the throw graded and a guard missed noted, its spark, what it carries (no damage, no hit stop now), the hits
    // counted, the thrower's animation's hit frame, the parries in a row over. Leaves r1 as the game's code.
    bool CaughtStatus(Guest &g) {
        const int16_t ix = static_cast<int16_t>(g.R(4));
        const int16_t ix2 = StruckBy(g, ix);
        const uint32_t as = Queued(g, ix2), ds = Queued(g, ix);
        uint32_t defended = 0;
        {
            Frame frame(g, 36); // (r8-r14, pr, a long)
            CallWith(g, kParryWindows, as, ds);
            const int16_t windows = static_cast<int16_t>(g.R(0));
            const uint32_t mine = Results(ix2), theirs = Results(ix);
            if (g.Read16(mine) & 0x100) {
                // (they caught each other)
                const uint8_t flags = g.Read8(as + kAttackFlags), otherFlags = g.Read8(ds + kAttackFlags);
                enum { Mine, Theirs, ByFrame } which;
                if (flags & 0x40) {
                    which = otherFlags & 0x40 ? ByFrame : Mine;
                } else if (flags & 0x20) {
                    which = otherFlags & 0x40 ? Theirs : otherFlags & 0x20 ? ByFrame : Mine;
                    if (!(otherFlags & 0x40)) {
                        g.R(1) = otherFlags;
                    }
                } else if (otherFlags & 0x60) {
                    which = Theirs;
                } else if (windows == 1) {
                    g.Write8(ds + kUnbreakable, 1);
                    which = Mine;
                } else if (windows == 2) {
                    g.Write8(as + kUnbreakable, 1);
                    which = Theirs;
                } else {
                    const uint32_t flag = windows == 3 ? kUnbreakable : kMayBreak;
                    g.Write8(ds + flag, 1);
                    g.Write8(as + flag, 1);
                    which = ByFrame;
                }
                if (which == ByFrame) {
                    which = g.Read16(kGameTimer) & 1 ? Mine : Theirs;
                }
                if (which == Theirs) {
                    g.Write16(mine, g.Read16(mine) & 0x111);
                    g.Write16(theirs, g.Read16(theirs) & 0x1011);
                    g.Return(80);
                    return true;
                }
                g.Write16(mine, g.Read16(mine) & 0x1011);
                g.Write16(theirs, g.Read16(theirs) & 0x111);
            }
            g.Write32(as + kHit, ds);
            g.Write32(ds + kHurtBy, as);
            g.Write8(as + kHitKind, static_cast<uint8_t>(g.Read16(ds + kKind)));
            g.Write8(ds + kHurtKind, static_cast<uint8_t>(g.Read16(as + kKind)));
            g.Write8(ds + kHitPoint, 1);
            CallWith(g, kGuardWay, as, ds);
            const int8_t from = static_cast<int8_t>(g.R(0));
            g.R(5) = Signed(from);
            g.R(4) = ds;
            g.Call(kLeverWays);
            CallWith(g, kHurtWay, as, ds);
            CallWith(g, kCatchSparkPlace, as, ds);
            CallWith(g, kDamageAndStun, as, ds);
            g.Write16(ds + kGuardDone, 0xFFFF);
            const auto inAir = [&] {
                if (Word(g, ds + kY) > 0) {
                    return true;
                }
                g.R(4) = ds;
                g.Call(kAirPose);
                return g.R(0) != 0;
            };
            bool air;
            if (g.Read8(ds + kGuardMode) == 3 || Byte(g, as + kGuardFlags) == 0 || InMove(g, ds)) {
                air = inAir();
            } else {
                air = Word(g, ds + kY) > 0;
                g.R(6) = Signed(from);
                CallWith(g, air ? kDefenseSky : kDefenseGround, as, ds);
                if (g.R(0) == 0 || g.R(0) == 1) {
                    defended = g.R(0) == 0 ? kParryStatus : kGuardStatus;
                }
            }
            if (defended == 0) {
                Caught(g, as, ds, air, from, windows);
            }
        }
        if (defended != 0) {
            CallWith(g, defended, as, ds);
        }
        g.Return(200);
        return true;
    }
    // (set_caught_status's, once the throw is not parried or guarded)
    void Caught(Guest &g, uint32_t as, uint32_t ds, bool air, int8_t from, int16_t windows) {
        g.Write8(as + kHitAs, air ? 2 : 1);
        g.Write16(ds + kHow, g.Read8(as + kReaction));
        // (the thrower's step: as it faces against the other, and the other knocked about)
        const bool knocked = Word(g, ds + kSteps + 2) == 1 && g.Read8(ds + kCgType) == 10;
        const int which = (knocked ? 1 : 0) + ((Byte(g, as + kFacing) + Byte(g, ds + kFacing)) & 1) * 2;
        const uint32_t step = which == 0 || which == 3 ? kThrowStepFacing : kThrowStepTurned;
        g.Write16(as + kSteps + 2, g.Read16(as + step));
        g.Write16(as + kHow, g.Read16(as + step + 2));
        g.Write16(as + kCharIndex, g.Read16(as + step + 4));
        g.Write16(as + kSteps + 6, 0);
        if (g.Read8(ds + kGuardMode) == 3 || (windows & 1)) {
            g.Write8(ds + kUnbreakable, 1);
        }
        // (held)
        g.Write16(as + kHolding, g.Read16(ds + kCharacter));
        g.Write8(as + kHoldingFlag, 1);
        g.Write8(ds + kHeld, 1);
        g.Write16(ds + kSteps + 2, 3);
        g.Write16(ds + kHow, g.Read8(as + kThrowKind));
        g.Write16(ds + kSteps + 6, 0);
        g.R(4) = as;
        g.Call(kGradeHit);
        g.R(6) = Signed(from);
        CallWith(g, kGuardMissed, as, ds);
        if (g.Read8(as + kThrowKind) == 2) {
            g.Write16(ds + kY, g.Read16(as + kY));
        }
        Spark(g, as, ds, 1);
        CallWith(g, kStatusCopy, as, ds);
        g.Write16(ds + kDamage, 0);
        g.Write16(ds + kHurtStop, 0);
        g.Write16(as + kHitStop, 0);
        g.Write16(as + kHitsMade, static_cast<uint16_t>(g.Read16(as + kHitsMade) + 1));
        g.Write16(as + kHitsMade2, static_cast<uint16_t>(g.Read16(as + kHitsMade2) + 1));
        g.Write16(ds + kHitsTaken, static_cast<uint16_t>(g.Read16(ds + kHitsTaken) + 1));
        g.R(4) = as;
        g.Call(kHitPatternExtra);
        ParriesOver(g, ds);
    }

    // 0608F008 (effect_at_vs_player_dm): an effect's attack (a projectile...) hit a fighter (r4 the effect's place in
    // the queue, r5 the fighter's): as a fighter's hit (player_at_vs_player_dm: where, the ways, the spark's place; its
    // way hit towards by setup_dm_rl_pldm), the damage for an effect (cal_damage_vitality_eff) and the stun by its
    // owner's character, both more in some poses (as set_damage_and_piyo's), and scaled by the handicaps its owner's
    // fighter gave it (olc_work_ix: 3 the owner's routine, 0 for a hit, 1 a throw, 2 the stun), which are passed on;
    // the score's index and the combo's scalings; then the effect's own damage 256 (spent), no atemi for the fighter
    // hit, and what it makes of it (plef_at_vs_player_damage_union, jumped to). Leaves r1 as the game's code.
    bool EffectHitsPlayer(Guest &g) {
        const int16_t ix2 = static_cast<int16_t>(g.R(4)), ix = static_cast<int16_t>(g.R(5));
        const uint32_t as = Queued(g, ix2), ds = Queued(g, ix);
        int8_t from = 0;
        {
            Frame frame(g, 32); // (r12-r14, pr, macl, three longs)
            g.Write8(ds + kHitPoint, g.Read8(Results(ix) + 3));
            CallWith(g, kGuardWay, as, ds);
            from = static_cast<int8_t>(g.R(0));
            g.R(5) = Signed(from);
            g.R(4) = ds;
            g.Call(kLeverWays);
            CallWith(g, kHurtWayByEffect, as, ds);
            g.R(6) = Signed(ix2);
            g.R(7) = Signed(ix);
            CallWith(g, kSparkPlace, as, ds);
            CallWith(g, kEffectDamage, as, ds);
            g.Write16(ds + kStun, g.Read8(kStunTable + Signed(Word(g, as + kCharacter)) * 16 + g.Read8(as + kAttackStun)));
            PoseScaled(g, ds);
            if (Word(g, g.Read32(as + kOwnerRecord) + kKind) == 1) {
                const int16_t routine = Word(g, as + kOwnerRoutine);
                if (routine == 2) {
                    g.Write16(ds + kDamage, static_cast<uint16_t>(Word(g, ds + kDamage) * (Word(g, as + kOwnerThrowHandicap) + 32) / 32));
                }
                if (routine == 4) {
                    g.Write16(ds + kDamage, static_cast<uint16_t>(Word(g, ds + kDamage) * (Word(g, as + kOwnerHitHandicap) + 32) / 32));
                }
                g.Write16(ds + kHitHandicap + 8, g.Read16(as + kOwnerHitHandicap));
                g.Write16(ds + kThrowHandicap + 8, g.Read16(as + kOwnerThrowHandicap));
                g.Write16(ds + kStun, static_cast<uint16_t>(static_cast<int32_t>(g.Read16(ds + kStun)) * (Word(g, as + kOwnerStunHandicap) + 32) / 32));
                g.Write16(ds + kStunHandicap + 8, g.Read16(as + kOwnerStunHandicap));
            }
            g.R(4) = Extended(Word(g, ds + kDamage));
            g.Call(kScoreIndex);
            g.Write16(as + kScoreIx, static_cast<uint16_t>(g.R(0)));
            CallWith(g, kComboDiscount, as, ds);
            g.R(4) = ds;
            g.Call(kScaleDamage);
            g.R(4) = ds;
            g.Call(kStunDiscount);
            g.Write16(as + kDamage, 0x100);
            g.Write8(ds + kAtemi, 0);
        }
        g.R(6) = Signed(from);
        CallWith(g, kDamageUnion, as, ds);
        g.Return(200);
        return true;
    }

    // An effect hit by a projectile of a fighter's (ids 122 and 123) takes damage as a fighter does
    // (cal_damage_vitality); any other is spent (256)
    bool Shootable(Guest &g, uint32_t record) {
        const int16_t id = Word(g, record + kId);
        return id == 122 || id == 123;
    }

    // 0608EEA0 (player_at_vs_effect_dm): a fighter's attack hit an effect (r4 the fighter's place, r5 the effect's):
    // the effect hit towards the fighter's facing, the spark's place, its damage (Shootable), what the fighter's hit
    // made of it (hf.hit.player: an effect of kind 2 that is shootable 1 in the air, 2 on the ground; others 0x10 in the
    // air, 0x20 on the ground), the effect hurt (routine 1, step 0), the spark (but for the effect 0x87 of kind 2: a
    // guard's for one that hits projectiles, att.dipsw 2, or 13 of the set 2; a hit's for others but 13), what the hit
    // carries; a fighter's hit on an unshootable effect of kind 2 doesn't spend its attack and halves the two's hit
    // stops. Then the fighter's animation's hit frame (jumped to). (The arcade's: no pp_pulpara_hit.) Leaves r1 as the
    // game's code.
    bool PlayerHitsEffect(Guest &g) {
        const int16_t ix2 = static_cast<int16_t>(g.R(4)), ix = static_cast<int16_t>(g.R(5));
        const uint32_t as = Queued(g, ix2), ds = Queued(g, ix);
        {
            Frame frame(g, 28); // (r11-r14, pr, two longs)
            g.Write8(ds + kHurtFacing, g.Read8(as + kFacing));
            g.R(7) = Signed(ix);
            g.R(6) = Signed(ix2);
            CallWith(g, kSparkPlace, as, ds);
            if (Shootable(g, ds)) {
                CallWith(g, kBaseDamage, as, ds);
            } else {
                g.Write16(ds + kDamage, 0x100);
            }
            const bool air = Word(g, ds + kY) > 0;
            const bool kind2 = Word(g, ds + kKind) == 2;
            if (kind2 && Shootable(g, ds)) {
                g.Write8(as + kHitAs, air ? 1 : 2);
            } else {
                g.Write8(as + kHitAs, air ? 0x10 : 0x20);
            }
            g.Write16(ds + kSteps + 2, 1);
            g.Write16(ds + kHow, 0);
            if (!(kind2 && Word(g, ds + kId) == 0x87)) {
                uint32_t spark = 0;
                if (g.Read8(ds + kAttackFlags) & 2) {
                    spark = 2;
                } else if (Word(g, ds + kId) != 13) {
                    spark = 1;
                } else if (g.Read8(ds + kEffectSet) == 2) {
                    spark = 2;
                }
                if (spark != 0) {
                    g.R(7) = Signed(Byte(g, ds + kHurtFacing));
                    g.R(6) = spark;
                    g.R(5) = 2;
                    g.R(4) = as;
                    g.Call(kSpark);
                }
            }
            CallWith(g, kStatusCopy, as, ds);
            if (kind2 && !Shootable(g, ds)) {
                g.Write8(as + kAttackLive, 1);
                const auto halve = [&](uint32_t at) {
                    const int16_t v = Word(g, at);
                    g.Write16(at, static_cast<uint16_t>((v + (v < 0 ? 1 : 0)) >> 1));
                };
                halve(as + kHitStop);
                halve(ds + kHurtStop);
            }
        }
        g.R(4) = as;
        g.Call(kHitPatternExtra);
        g.Return(100);
        return true;
    }

    // 0608F290 (effect_at_vs_effect_dm): an effect hit another (r4 the attacker's place, r5 the other's): each hit
    // towards the other's facing, the spark's place; the damage: two that hit projectiles (att.dipsw 2) both 128; an
    // effect of these hit by one that doesn't, both 128, and the one that doesn't reflected (dm_refrect, refrected) and
    // its attack kept, unless it can't be (shell_vs_refrect); an effect 13 hit by one of kind 4 takes its health and
    // gives it its own; a shootable one, a fighter's damage (cal_damage_vitality), the attacker spent; any other spent.
    // The attacker's hit noted (hf.hit.effect: 2 in the air, 1 on the ground), the one hit hurt (routine 1, step 0),
    // both a hit stop of 6, the way and the move's kind passed on, a shootable one's spark; then the attacker's
    // animation's hit frame (jumped to). Leaves r1 as the game's code.
    bool EffectHitsEffect(Guest &g) {
        const int16_t ix2 = static_cast<int16_t>(g.R(4)), ix = static_cast<int16_t>(g.R(5));
        const uint32_t as = Queued(g, ix2), ds = Queued(g, ix);
        {
            Frame frame(g, 28); // (r11-r14, pr, two longs)
            g.Write8(ds + kHurtFacing, g.Read8(as + kFacing));
            g.Write8(as + kHurtFacing, g.Read8(ds + kFacing));
            g.R(7) = Signed(ix);
            g.R(6) = Signed(ix2);
            CallWith(g, kSparkPlace, as, ds);
            if (g.Read8(ds + kAttackFlags) & 2) {
                g.Write16(ds + kDamage, 0x80);
                g.Write16(as + kDamage, 0x80);
                if (!(g.Read8(as + kAttackFlags) & 2) && g.Read8(as + kCantReflect) == 0) {
                    g.Write8(as + kReflect, 1);
                    g.Write8(as + kReflected, 1);
                    g.Write8(as + kAttackLive, 1);
                }
            } else if (Word(g, ds + kId) == 13) {
                if (Word(g, as + kKind) == 4) {
                    g.Write16(ds + kDamage, g.Read16(as + kHealth));
                    g.Write16(as + kDamage, g.Read16(ds + kHealth));
                }
            } else if (Shootable(g, ds)) {
                CallWith(g, kBaseDamage, as, ds);
                g.Write16(as + kDamage, 0x100);
            } else {
                g.Write16(ds + kDamage, 0x100);
            }
            g.Write8(as + kHitAsEffect, Word(g, ds + kY) > 0 ? 2 : 1);
            g.Write16(ds + kSteps + 2, 1);
            g.Write16(ds + kHow, 0);
            g.Write16(ds + kHitStop, 6);
            g.Write16(as + kHitStop, 6);
            g.Write16(ds + 0x32A, g.Read16(as + kAttackDir));    // dm_dir
            g.Write8(ds + 0x343, g.Read8(as + kMoveKind));      // dm_kind_of_waza
            if (Shootable(g, ds)) {
                g.R(7) = Signed(Byte(g, ds + kHurtFacing));
                g.R(6) = 1;
                g.R(5) = 2;
                g.R(4) = as;
                g.Call(kSpark);
            }
        }
        g.R(4) = as;
        g.Call(kHitPatternExtra);
        g.Return(100);
        return true;
    }

    // --- the hit path's helpers: the combo counted and its scalings, the score's index, the spark's place, the way
    // an effect's hit turns, the round's grades ---
    constexpr uint32_t kScoreTable = 0x0618BBA0;  // [16] {word: damage from, word: the score's index}
    constexpr uint32_t kComboKinds = 0x0202884C;  // words [player][10]: the hits by the kind's group
    constexpr uint32_t kComboScalings = 0x065FF1CC; // longs, by the kind of the move over 2: a table of [9][4] words
    constexpr uint32_t kDamageScalings = 0x065FF048, kDamageScalingsTwo = 0x065FEFC4; // ... [32] words: by the combo's
                                                  // weight (the second for two characters' second forms)
    constexpr uint32_t kSparks = 0x061B959E;      // [kind] {word across, word up}: an attack's spark (by att.mkh_ix)
    constexpr uint32_t kSparkBoxes = 0x0200EC4C, kSparkBoxes2 = 0x0200EC50; // longs, 16 bytes apart: the hit queue's
                                                  // boxes
    constexpr uint32_t kBoxesMiddle = 0x0608E9D0; // the middle of where two boxes overlap, the spark's place
    constexpr uint32_t kGrades = 0x02010C4C;      // the players' round records: 0x98 bytes a player, 76 a round
    constexpr uint32_t kGradeRound = 0x020113B5;  // byte: the round (for the records)
    constexpr uint32_t kGradeSteps = 0x02068C5C;  // word (pcon_rno[0]): 0 before the fight
    // ... in a record
    constexpr uint32_t kMoveKindOf = 0x20F;       // byte (kind_of_waza)
    constexpr uint32_t kComboRecord = 0x3F4, kComboRecord2 = 0x42C; // longs: the combo's records (the hits so far:
                                                  // a count, then by kind)
    constexpr uint32_t kKindHit = 0x3C8;          // byte: the last hit's kind
    constexpr uint32_t kNoCombo = 0x41D;          // byte: the hit not counted in a combo
    constexpr uint32_t kScreenY = 0x58, kSparkScreenY = 0x318; // words: its height on the screen, its spark's

    // A player's record of the round (grades), r0 its offset by the round, as the game's code works it out
    void Add(Guest &g, uint32_t at, int value) {
        g.Write16(at, static_cast<uint16_t>(Word(g, at) + value));
    }

    uint32_t RoundRecord(Guest &g, int32_t player) {
        const uint32_t round = static_cast<uint16_t>(Byte(g, kGradeRound));
        return kGrades + Extended(player * 0x98) + ((76 * round) & 0xFF);
    }

    // 0608DA3C (remake_score_index): the score's index for a hit's damage (r4): the table's last band it reaches.
    // Leaves r5 the band, r6 16, r7 the table.
    bool ScoreIndex(Guest &g) {
        const int16_t damage = static_cast<int16_t>(g.R(4));
        uint32_t band = 0;
        while (band < 16 && damage >= Word(g, kScoreTable + band * 4)) {
            band++;
        }
        g.R(5) = band;
        g.R(6) = 16;
        g.R(7) = kScoreTable;
        g.R(0) = Extended(Word(g, kScoreTable + band * 4 + 2));
        g.Return(20);
        return true;
    }

    // 0608E098 (add_combo_work): a hit (r4 the attacker, r5 the one hit) counted in the combo: its kind kept, the
    // hits by kind and by the kind's group, the hits so far
    bool ComboCounted(Guest &g) {
        const uint32_t attacker = g.R(4), hurt = g.R(5);
        g.R(0) = kNoCombo;
        g.R(1) = Signed(Byte(g, hurt + kNoCombo));
        if (g.R(1) != 0) {
            g.SetT(false);
            g.Return(10);
            return true;
        }
        const uint32_t kind = g.Read8(attacker + kMoveKindOf);
        const uint32_t record = g.Read32(hurt + kComboRecord);
        g.Write16(record + 2, static_cast<uint16_t>(kind));
        g.Write8(hurt + kKindHit, static_cast<uint8_t>(kind));
        const int16_t id = Word(g, hurt + 8);
        Add(g, record + 8 + kind * 2, 1);
        const uint32_t groups = kComboKinds + Signed(static_cast<int8_t>(id * 20));
        Add(g, groups + ((kind & 120) >> 3) * 2, 1);
        Add(g, g.Read32(hurt + kComboRecord), 1);
        const uint32_t second = g.Read32(hurt + kComboRecord2);
        Add(g, second + 8 + kind * 2, 1);
        Add(g, g.Read32(hurt + kComboRecord2), 1);
        g.R(0) = kComboRecord2;
        g.R(1) = g.Read32(hurt + kComboRecord2);
        g.R(2) = kComboKinds;
        g.R(3) = Extended(Word(g, g.R(1)));
        g.R(6) = second + 8;
        g.R(7) = groups;
        g.SetT(false); // (from the group's shift)
        g.Return(30);
        return true;
    }

    // 0608E1FC (cal_combo_waribiki): the damage (r5's dm_vital) scaled by the combo so far: each of its hits by kind
    // (the second record: 9 groups of 4 counts, two words each) weighted by the attack's kind's table, the sum's whole
    // part (rounded up, at most 31) the index of the scaling (in 32nds; the second forms' own for Gill and Oro's
    // second), at least 1. Leaves r1 the product, r7 the attacker's character (or its form's record).
    bool ComboDiscount(Guest &g) {
        const uint32_t attacker = g.R(4), hurt = g.R(5);
        if (Word(g, hurt + kDamage) == 0 || Word(g, g.Read32(hurt + kComboRecord2)) == 0) {
            g.Return(20);
            return true;
        }
        const uint32_t kind = g.Read8(attacker + kMoveKindOf) >> 1;
        const uint32_t weights = g.Read32(kComboScalings + kind * 4);
        uint32_t sum = 0;
        for (uint32_t group = 0; group < 9; group++) {
            for (uint32_t n = 0; n < 4; n++) {
                const uint32_t at = g.Read32(hurt + kComboRecord2) + 8 + group * 16 + n * 4;
                const int32_t hits = Word(g, at) + Word(g, at + 2);
                if (static_cast<int16_t>(hits) != 0) {
                    const int32_t weight = Word(g, weights + group * 8 + n * 2);
                    sum += static_cast<uint32_t>(weight * static_cast<int16_t>(hits)) << 8;
                }
            }
        }
        int16_t weight = static_cast<int16_t>(sum >> 16);
        if ((sum & 0xFFFF) != 0) {
            weight++;
        }
        uint32_t scalings = g.Read32(kDamageScalings + kind * 4);
        const int16_t character = Word(g, attacker + kCharacter);
        g.R(7) = Signed(character);
        if (character == 3 || character == 10) {
            const uint32_t form = g.Read32(attacker + 0x3F0);
            g.R(7) = form;
            if (Word(g, form) == 2 && Byte(g, form + 10) == -1) {
                scalings = g.Read32(kDamageScalingsTwo + kind * 4);
            }
        }
        if (weight > 31) {
            weight = 31;
        }
        const int32_t product = Word(g, scalings + Signed(weight) * 2) * Word(g, hurt + kDamage);
        g.R(1) = Signed(product);
        const int16_t damage = static_cast<int16_t>(static_cast<int16_t>(product) >> 5);
        g.Write16(hurt + kDamage, static_cast<uint16_t>(damage > 0 ? damage : 1));
        g.Return(100);
        return true;
    }

    // 0608E3E4 (cal_combo_waribiki_2): the stun (r4's dm_piyo) scaled by the combo's hits: (32 - 2 x hits)/32, at
    // least 1/32, and at least 1
    bool StunDiscount(Guest &g) {
        const uint32_t hurt = g.R(4);
        if (Word(g, hurt + kStun) == 0 || Word(g, g.Read32(hurt + kComboRecord)) == 0) {
            g.SetT(true);
            g.Return(10);
            return true;
        }
        int32_t part = 32 - Word(g, g.Read32(hurt + kComboRecord)) * 2;
        if (static_cast<int16_t>(part) <= 0) {
            part = 1;
        }
        const int32_t stun = static_cast<int32_t>(static_cast<int16_t>(part) * static_cast<uint32_t>(g.Read16(hurt + kStun)));
        g.Write16(hurt + kStun, static_cast<uint16_t>((stun < 0 ? stun + 31 : stun) >> 5));
        g.SetT(Word(g, hurt + kStun) == 0);
        if (Word(g, hurt + kStun) == 0) {
            g.Write16(hurt + kStun, 1);
        }
        g.Return(30);
        return true;
    }

    // 0608CF62 (cal_hit_mark_pos): where a hit's spark is (r4 the attacker, r5 the one hit, r6 and r7 their boxes' places
    // in the hit queue): an attack's own spark (att.mkh_ix) from its place (across, by its facing), else the middle of
    // where the boxes met (0x0608E9D0); and its height on the screen
    bool SparkPlace(Guest &g) {
        const uint32_t attacker = g.R(4);
        {
            Frame frame(g, 20); // (r14, pr, three words)
            const uint32_t mark = g.Read8(attacker + kSparkMark);
            if (mark != 0) {
                const uint32_t spark = kSparks + mark * 4;
                const int16_t x = Word(g, attacker + kX), offset = Word(g, spark);
                g.Write16(attacker + kSparkX, static_cast<uint16_t>(Byte(g, attacker + kFacing) != 0 ? x - offset : x + offset));
                g.R(1) = attacker + kSparkY;
                g.R(5) = spark;
                g.Write16(attacker + kSparkY, static_cast<uint16_t>(Word(g, spark + 2) + Word(g, attacker + kY)));
            } else {
                const uint32_t mine = g.R(6), theirs = g.R(7);
                g.R(0) = Extended(static_cast<int32_t>(mine));
                g.R(4) = g.R(5);
                g.R(7) = g.Read32(kSparkBoxes + (Extended(static_cast<int32_t>(mine)) << 4));
                g.R(6) = g.Read32(kSparkBoxes2 + (Extended(static_cast<int32_t>(theirs)) << 4));
                g.R(5) = attacker;
                g.R(3) = kBoxesMiddle;
                g.Call(kBoxesMiddle);
            }
            g.R(0) = kSparkScreenY;
            g.R(2) = Signed(Word(g, attacker + kScreenY)) - 8;
            g.Write16(attacker + kSparkScreenY, static_cast<uint16_t>(g.R(2)));
        }
        g.Return(40);
        return true;
    }

    // 0608F240 (setup_dm_rl_pldm): the way a fighter (r5) hit by an effect (r4) turns: the effect's facing; in the
    // air, for an effect that says so (att.dipsw 2), away from it (by their places across; level, the facing)
    bool HurtWayByEffect(Guest &g) {
        const uint32_t effect = g.R(4), hurt = g.R(5);
        g.Write8(hurt + kHurtFacing, g.Read8(effect + kFacing));
        if (Word(g, hurt + kY) > 0 && (g.Read8(effect + kAttackFlags) & 2)) {
            const int16_t apart = static_cast<int16_t>(Word(g, hurt + kX) - Word(g, effect + kX));
            g.Write8(hurt + kHurtFacing, apart != 0 ? (apart > 0 ? 1 : 0) : g.Read8(effect + kFacing));
        }
        g.Return(20);
        return true;
    }

    // 060942B2 (grade_add_clean_hits): a hit by a fighter (r4; an effect's: its owner's) counted in its player's
    // record of the round, once the fight is on. Leaves r0-r5 and T as the game's code.
    bool GradeHit(Guest &g) {
        g.R(3) = kGradeSteps;
        g.R(2) = Extended(Word(g, kGradeSteps));
        if (g.R(2) == 0) {
            g.SetT(true);
            g.Return(10);
            return true;
        }
        g.R(5) = Extended(Word(g, g.R(4) + 8));
        g.R(0) = Extended(Word(g, g.R(4) + kKind));
        if (g.R(0) != 1) {
            g.R(4) = g.Read32(g.R(4) + kOwnerRecord);
            g.R(0) = Extended(Word(g, g.R(4) + kKind));
            g.SetT(g.R(0) == 1);
            if (g.R(0) != 1) {
                g.Return(10);
                return true;
            }
            g.R(5) = Extended(Word(g, g.R(4) + 8));
        }
        g.SetT(true);
        g.R(1) = Signed(Byte(g, kGradeRound));
        g.R(5) = Extended(static_cast<int16_t>(g.R(5)) * 0x98);
        g.R(3) = kGrades + g.R(5);
        g.R(2) = g.R(3) + ((76 * (g.R(1) & 0xFFFF)) & 0xFF);
        g.R(0) = Signed(Word(g, g.R(2) + 12)) + 1;
        g.Write16(g.R(2) + 12, static_cast<uint16_t>(g.R(0)));
        g.Return(20);
        return true;
    }

    // 0609433E (grade_add_guard_success): a guard counted in the player's (r4) record of the round. Leaves r2 1 or the
    // player's records.
    bool GradeGuard(Guest &g) {
        const uint32_t player = g.R(4) & 0xFFFF;
        g.R(2) = 1;
        if (static_cast<int32_t>(player) > 1) {
            g.Return(10);
            return true;
        }
        g.R(2) = kGrades + Extended(static_cast<int16_t>(g.R(4)) * 0x98);
        const uint32_t at = RoundRecord(g, static_cast<int16_t>(g.R(4))) + 16;
        Add(g, at, 1);
        g.Return(20);
        return true;
    }

    // 06094744 (grade_add_blocking): a parry counted in the parrier's (r4) record of the round, by its kind (0 while
    // standing, 1 while hurt, 2 in the air): its health then kept, the count (at most 15). Leaves r0-r7 and T as the
    // game's code.
    bool GradeParry(Guest &g) {
        const uint32_t fighter = g.R(4);
        g.R(6) = 15;
        g.R(5) = kPlayType;
        g.R(7) = Extended(Word(g, fighter + 8));
        const uint32_t base = kGrades + Extended(static_cast<int16_t>(g.R(7)) * 0x98);
        const uint32_t round = (76 * (Signed(Byte(g, kGradeRound)) & 0xFFFF)) & 0xFF;
        const uint32_t kind = g.Read8(fighter + kParryKind);
        g.R(0) = kind;
        g.R(1) = base;
        g.R(2) = round;
        if (kind > 2) {
            g.SetT(false);
            g.Return(20);
            return true;
        }
        const uint32_t record = base + round;
        g.Write16(record + 56 + kind * 2, g.Read16(fighter + kHealth));
        const uint32_t count = 20 + kind * 2;
        g.R(0) = record;
        g.R(1) = count;
        g.R(3) = record + count;
        g.R(2) = Signed(Word(g, record + count)) + 1;
        g.Write16(record + count, static_cast<uint16_t>(g.R(2)));
        g.R(2) = Extended(static_cast<int32_t>(g.R(2)));
        g.SetT(static_cast<int32_t>(g.R(2)) > 15);
        if (static_cast<int32_t>(g.R(2)) > 15) {
            g.R(2) = 76;
            g.R(3) = 0x98;
            g.R(7) = record;
            g.R(0) = 15;
            g.Write16(record + count, 15);
        }
        g.Return(30);
        return true;
    }

    const Lift kLifts[] = {
        {"ResultsExtra", 0x0608C860, 0x12A, 0x5E96AF72EB69F185ull, ResultsExtra, true, 0x00004},
        {"ParryCounted", 0x0608DBEC, 0x48, 0x7602A93C2CD70889ull, ParryCounted, true, 0x1001F},
        {"GuardChip", 0x0608D6E4, 0x92, 0xB0B7D84AE782CDD2ull, GuardChip, true, 0x160FE},
        {"ParryChip", 0x0608D776, 0x86, 0x05E31CEF3384A8E3ull, ParryChip, true, 0x160FE},
        {"DefenseGround", 0x0608DC44, 0x2E6, 0x75DE7A6B98C29827ull, DefenseGround, true, 0x1003E},
        {"DefenseSky", 0x0608DAE8, 0x104, 0x8AA5AE5197CFBCA3ull, DefenseSky, true, 0x1003E},
        {"ReactionSet", 0x0608D300, 0x8C, 0x75B0B16154625048ull, ReactionSet, true, 0x100FF},
        {"SameStop", 0x0608DA62, 0x72, 0xB8DEC513143BF41Full, SameStop, true, 0x1003F},
        {"StatusCopy", 0x0608DFCC, 0xCC, 0x627444D8E4A88EC1ull, StatusCopy, true, 0x10019},
        {"HitPatternExtra", 0x0608D638, 0xAC, 0xBD946E26C3F21614ull, HitPatternExtra, true, 0x100FD},
        {"DamageUnion", 0x0608D02C, 0x2A0, 0x7787656606B74E09ull, DamageUnion, true, 0x100FD},
        {"GuardStatus", 0x0608D38C, 0x86, 0x3F33C36DA51100FAull, GuardStatus, true, 0x100FD},
        {"ParryStatus", 0x0608D444, 0x1B2, 0xC49A9B3FAC0ED7ECull, ParryStatus, true, 0x100FD},
        {"DamageAndStun", 0x0608D7FC, 0x240, 0x0A553122A48BD9B1ull, DamageAndStun, true, 0x00078},
        {"StruckStatus", 0x0608CE68, 0xFA, 0x1E9BBF130DEDFEE5ull, StruckStatus, true, 0x100FD},
        {"PlayerHitsPlayer", 0x0608ECC4, 0x1BA, 0x8634D3C5311A1052ull, PlayerHitsPlayer, true, 0x100FD},
        {"AirPose", 0x0608CD34, 0x1C, 0xDCECEF5C6DAF487Cull, AirPose, true, 0x1007C},
        {"ParryWindows", 0x0608CD90, 0x34, 0xB620B3D9CF48A7C0ull, ParryWindows, true, 0x102FE},
        {"CatchSparkPlace", 0x0608CDEA, 0x7E, 0x35E221706A831220ull, CatchSparkPlace, true, 0x100FF},
        {"HurtWay", 0x0608DF2A, 0x94, 0x685CB439B295281Aull, HurtWay, true, 0x100FB},
        {"CaughtStatus", 0x0608C994, 0x3A0, 0x1DE6440B3423BF52ull, CaughtStatus, true, 0x100FD},
        {"EffectHitsPlayer", 0x0608F008, 0x208, 0xB9B37B7732AF8605ull, EffectHitsPlayer, true, 0x100FD},
        {"PlayerHitsEffect", 0x0608EEA0, 0x158, 0x86B39048D6547452ull, PlayerHitsEffect, true, 0x100FD},
        {"EffectHitsEffect", 0x0608F290, 0x148, 0x8BC4E5B04F5A6460ull, EffectHitsEffect, true, 0x100FD},
        {"ScoreIndex", 0x0608DA3C, 0x26, 0xD98E2C27563736D6ull, ScoreIndex, true, 0x1007E},
        {"ComboCounted", 0x0608E098, 0x88, 0xDF38C01F2C3B62BEull, ComboCounted, true, 0x00018},
        {"ComboDiscount", 0x0608E1FC, 0x1E8, 0x8C3D124CD24204F6ull, ComboDiscount, true, 0x1007D},
        {"StunDiscount", 0x0608E3E4, 0x58, 0x28F133578D0B415Eull, StunDiscount, true, 0x0007D},
        {"SparkPlace", 0x0608CF62, 0x9E, 0xCCEF250AC7CC5D05ull, SparkPlace, true, 0x1001D},
        {"HurtWayByEffect", 0x0608F240, 0x4A, 0x3AF332A1D5618C8Bull, HurtWayByEffect, true, 0x100FF},
        {"GradeHit", 0x060942B2, 0x46, 0x6495D6200457E962ull, GradeHit, true, 0},
        {"GradeGuard", 0x0609433E, 0x2E, 0x1579B1D2ED09E8FEull, GradeGuard, true, 0x1503B},
        {"GradeParry", 0x06094744, 0x118, 0xD8BBD9A0547C0719ull, GradeParry, true, 0},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightJudge() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
