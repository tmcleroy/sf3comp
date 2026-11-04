// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's fighters' moves, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h):
// a fighter's step in a fight, from the top down to the layer every character shares: what runs it, the hit's stop,
// the timers, where its input may start a move or cancel one ("can act"), and the dispatch into its normal moves,
// special moves, super arts and throws. Frame data in training mode is these routines' doing: a move's startup and
// active frames are its animation's (fight_boxes.cpp), its recovery ends where the step routines give the fighter
// back to its normal steps or a cancel takes it.
//
// A fighter's routine numbers (routine_no, +0x24 on): [0] 4 in a fight (fight_input.cpp's Player_move runs
// plmain_lv_00[it]); [1] what it is doing: 0 normal (standing, walking, crouching, jumping: Player_normal), 1 hurt
// (Player_damage), 2 throwing (Player_catch), 3 being thrown (Player_caught), 4 attacking (Player_attack); [2] which
// (in an attack: below 16 one of the normal moves every character shares, plpat_lv_00; from 16 the character's own
// specials, supers and command throws, plxx_extra_attack_table by character); [3] its step in that.
//
//   player_mv_4000 (0x06119A6C)   the step in a fight: the extra jump's timer (check_extra_jump_timer), the lever's
//                                 reading for moves (check_lever_data, but in a super art's freeze), the hit's stop
//                                 (check_hit_stop) or else the step (plmain_lv_02 by [1]), the slide's timer, the
//                                 timers, the super art gauges (about_gauge_process)
//   check_hit_stop (0x06119B02)   the frames a hit holds the fighter still: hit_stop (+0x44, the attacker's and the
//                                 hit one's, with dm_stop +0x32E taken in); below 0 the animation still runs
//   look_after_timers (0x06119C96) the frames it can't be thrown, a throw may be broken, a quick stand may be made, a
//                                 projectile's hits are counted, and the stun's recovery
//   check_lever_data (0x0611AAF8) the step's own reading of the input (process_ndcca by [1]): from normal steps the
//                                 moves it may start (process_normal), in an attack the cancels (process_attack and
//                                 check_cg_cancel_data: the frame's cancels, cg_cancel), a throw's end (process_catch)
//   Player_normal (0x061214A4), Player_attack (0x06126120): the step's flags, then the routine by [2]; Attack_00000
//                                 (0x0612621A), Attack_01000 (0x061262AA): a normal move on the ground and one that
//                                 moves, the first two of the shared normal moves' steps
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }
    uint32_t ByteExtended(int32_t value) {
        return Signed(static_cast<int8_t>(value));
    }

    // --- in a fighter's record (plw) ---
    constexpr uint32_t kFacing = 0x0A, kFacingMove = 0x0B; // bytes: the way it faces, the way its move faces (rl_flag,
                                                          // rl_waza)
    constexpr uint32_t kTarget = 0x0C;          // long: the other fighter's record (target_adrs)
    constexpr uint32_t kSteps = 0x24;           // words: routine_no[0..3]
    constexpr uint32_t kDoing = 0x26;           // word: routine_no[1]: 0 normal, 1 hurt, 2 throwing, 3 thrown, 4 attacking
    constexpr uint32_t kWhich = 0x28;           // word: routine_no[2]: which step of those
    constexpr uint32_t kStep = 0x2A;            // word: routine_no[3]: its step
    constexpr uint32_t kHitStop = 0x44;         // word: frames held still by a hit (hit_stop): below 0 its animation runs
    constexpr uint32_t kDrawnAt = 0x5E;         // word: its place in the drawing order (next_z)
    constexpr uint32_t kY = 0x68;               // word: its height (xyz[1]'s whole part)
    constexpr uint32_t kCgType = 0x215;         // byte (cg_type): 20 the move's end, 1 its moving part
    constexpr uint32_t kPose = 0x20E;           // byte (pat_status): from 32 in the air
    constexpr uint32_t kCancels = 0x221;        // byte: what the frame may be cancelled into (cg_cancel: 0x40 a super art,
                                                // 0x20 a special move or taunt, 0x10 a repeated press, 8 a chain, 4 a
                                                // normal move, 2 a dash, 1 a high jump)
    constexpr uint32_t kPriority = 0x232;       // byte (cg_prio): 1 in front of the other, 2 behind
    constexpr uint32_t kMyDrawn = 0x26C;        // word (my_priority)
    constexpr uint32_t kCancelHit = 0x2FF;      // byte: its attack has hit or been guarded (meoshi_hit_flag): the special
                                                // and super cancels then open
    constexpr uint32_t kHurtStop = 0x32E;       // word: the hit stop its hit gives it (dm_stop)
    constexpr uint32_t kInput = 0x3B8;          // long: its input record (cp: +1 the lever's byte, +6 sw_now)
    constexpr uint32_t kCharacter = 0x3C0;      // word (player_number)
    constexpr uint32_t kSlideTimer = 0x3C2;     // word (zuru_timer)
    constexpr uint32_t kSlideCount = 0x3C4;     // word (zuru_ix_counter)
    constexpr uint32_t kSliding = 0x3C6;        // byte (zuru_flag)
    constexpr uint32_t kUncatchable = 0x3C7;    // byte: frames it can't be thrown (tsukamarenai_flag)
    constexpr uint32_t kHolding = 0x3CE, kHeld = 0x3CF; // bytes (tsukami_f, tsukamare_f: sfiii3.h's kThrown)
    constexpr uint32_t kGuardMode = 0x3D2;      // byte (guard_flag): 0 it may guard, 3 not
    constexpr uint32_t kGuarding = 0x3D3;       // byte (guard_chuu: sfiii3.h's kBlocking)
    constexpr uint32_t kHurtPlaced = 0x3D8;     // byte (dm_hos_flag)
    constexpr uint32_t kScrollSet = 0x3DC;      // byte (scr_pos_set_flag)
    constexpr uint32_t kRunning = 0x3E4;        // byte (running_f)
    constexpr uint32_t kCancelTimer = 0x3E5;    // byte: frames a move may still be cancelled at its start (cancel_timer)
    constexpr uint32_t kNormalAttack = 0x3E8;   // word: the buttons of the normal move under way (current_attack)
    constexpr uint32_t kMoveData = 0x3EC;       // long: the move's entry (as: r_no, char_ix the animation, data_ix its
                                                // movement)
    constexpr uint32_t kArts = 0x3F0;           // long: its super art record (sa: +20 saeff_ok, +21 saeff_mp)
    constexpr uint32_t kStun = 0x3F8;           // long: its stun record (py: +0 flag, the long +8 the stun taken (its
                                                // high word), +12 the recovery a frame)
    constexpr uint32_t kQuickStandTimer = 0x3FE; // word (ukemi_ok_timer)
    constexpr uint32_t kQuickStandOk = 0x402, kQuickStood = 0x403; // bytes (uot_cd_ok_flag, ukemi_success)
    constexpr uint32_t kLeverInFreeze = 0x41B;  // byte (sa_stop_lvdir)
    constexpr uint32_t kSuperFreeze = 0x41C;    // byte: 1 in a super art's freeze, 2 as it begins (sa_stop_flag)
    constexpr uint32_t kFreezeLeft = 0x418;     // word (sa_stop_sai)
    constexpr uint32_t kWarned = 0x428;         // byte (caution_flag)
    constexpr uint32_t kProjectileHits = 0x430, kProjectileTimer = 0x432; // words (bullet_hcnt, bhcnt_timer)
    constexpr uint32_t kBreakTimer = 0x434;     // byte (cat_break_ok_timer)
    constexpr uint32_t kMayBreak = 0x435, kUnbreakable = 0x436; // bytes (cat_break_reserve, hazusenai_flag)
    constexpr uint32_t kAtemi = 0x448;          // byte (atemi_flag)
    constexpr uint32_t kKept = 0x44A;           // word (dm_vital_backup)
    constexpr uint32_t kKeptUse = 0x44D;        // byte (dm_vital_use)
    constexpr uint32_t kCommand = 0x450;        // word (cmd_request)
    constexpr uint32_t kFreezeBegan = 0x456;    // word: the game's frame its super art's freeze began (just_sa_stop_timer)
    constexpr uint32_t kAttacksLive = 0x458;    // word (total_att_hit_ok)
    constexpr uint32_t kAfterHitStopOk = 0x45C; // byte (hsjp_ok)
    constexpr uint32_t kHighJump = 0x45D;       // byte (high_jump_flag)
    constexpr uint32_t kOnCar = 0x462;          // byte: standing on the bonus game's car (bs2_on_car)
    constexpr uint32_t kAllowed = 0x46C;        // word (permited_koa: sfiii3.h's kInputCapacity)

    // --- the game's ---
    constexpr uint32_t kTimerFreeze = 0x02015732; // byte: the round's timers held, a super art's freeze (Timer_Freeze)
    constexpr uint32_t kGameTimer = 0x0201136C;   // word (Game_timer)
    constexpr uint32_t kBonusGame = 0x02016B3A;   // word: 21 the car (Bonus_Game_Flag)
    constexpr uint32_t kCarFloor = 0x02016B94;    // word (bs2_floor[2])
    constexpr uint32_t kFightSteps = 0x065EAF84;  // longs, by routine_no[1] (plmain_lv_02)
    constexpr uint32_t kReadings = 0x065EAFC0;    // longs, by routine_no[1] (process_ndcca)
    constexpr uint32_t kNormalSteps = 0x065EBB34; // longs, by routine_no[2] (plpnm_lv_00)
    constexpr uint32_t kSharedAttacks = 0x065EC8D4; // longs, 16, by routine_no[2] (plpat_lv_00)
    constexpr uint32_t kOwnAttacks = 0x065EC914;  // longs, by character (plxx_extra_attack_table)
    constexpr uint32_t kExtraJumpTimer = 0x0611CC86, kLeverData = 0x0611AAF8, kHitStopCheck = 0x06119B02;
    constexpr uint32_t kSelectHitStop = 0x06119C6C, kTimers = 0x06119C96, kGauges = 0x06119D4A;
    constexpr uint32_t kCharMove = 0x06089848, kAfterHitStop = 0x06089812;
    constexpr uint32_t kQuickStandCheck = 0x0611C958;  // check_ukemi_flag (r4): r0
    constexpr uint32_t kMyThrowOff = 0x0611C89C, kOtherThrowOff = 0x0611C8E8; // check_my_tk_power_off,
                                                    // check_em_tk_power_off (r4, r5 the other)
    constexpr uint32_t kJumpGuard = 0x0611BB00;     // jumping_guard_type_check (r4)
    constexpr uint32_t kNormalFlags = 0x061214FC;   // setup_normal_process_flags
    constexpr uint32_t kLanded = 0x06126E14, kCancelTimerSet = 0x06126DF4; // hoken_muriyari_chakuchi, get_cancel_timer
    constexpr uint32_t kAutoDir = 0x0611E9F6;       // setup_lvdir_after_autodir (r4)
    constexpr uint32_t kSetMove = 0x060890E0;       // set_char_move_init (r4, r5 the list, r6 the index)
    constexpr uint32_t kMoveSet = 0x0611D6B0;       // setup_mvxy_data (r4, r5 the movement)
    constexpr uint32_t kMoveSpeed = 0x0611D928;     // add_mvxy_speed (r4)
    constexpr uint32_t kJumping = 0x0611D41C;       // jumping_union_process (r4, r5)
    // ... what the readings call (r4 the fighter; r0 not 0 when it started something)
    constexpr uint32_t kOnFloorCheck = 0x0611D5D6;  // check_ashimoto_ex: landed
    constexpr uint32_t kFullGauge = 0x0611EC1A, kFullGauge2 = 0x0611F0E0; // check_full_gauge_attack(2) (r5)
    constexpr uint32_t kSuperArt = 0x0611F5C8, kSpecial = 0x0611FC74, kTaunt = 0x0612080E;
    constexpr uint32_t kThrow = 0x06120982, kLeap = 0x061202AE, kTriangleJump = 0x0611CBE0;
    constexpr uint32_t kRepeated = 0x06121100, kChain = 0x0612125C, kNormal = 0x06120378;
    constexpr uint32_t kDash = 0x0611CDA0, kHighJumpCheck = 0x0611CF04;
    constexpr uint32_t kJumpingCgType = 0x0611B5AC; // jumping_cg_type_check (r4)
    constexpr uint32_t kToStand = 0x0611AB2E, kToCrouch = 0x0611AB5A; // TO_nm_01000, TO_nm_09000
    constexpr uint32_t kToThrowEnd = 0x0611AB44, kToThrowEndAir = 0x0611AB70; // TO_nm_36000, TO_nm_37000

    // 06119A6C (player_mv_4000): a fighter's step in a fight (above). Leaves r0-r7 as the routines it calls do
    // (Player_move's callers read r0 and r7).
    bool PlayerFight(Guest &g) {
        const uint32_t fighter = g.R(4);
        {
            Frame frame(g, 12); // (r14, r13, pr)
            g.R(0) = kAllowed;
            g.Write16(fighter + kAllowed, 0);
            g.R(3) = kExtraJumpTimer;
            g.R(4) = fighter;
            g.Call(kExtraJumpTimer);
            g.R(0) = g.Read8(fighter + kSuperFreeze);
            g.SetT(g.R(0) == 1);
            if (g.R(0) != 1) {
                g.R(2) = kLeverData;
                g.R(4) = fighter;
                g.Call(kLeverData);
            }
            g.R(0) = ByteExtended(Byte(g, fighter + kHeld));
            g.SetT(g.R(0) == 0);
            g.R(4) = fighter;
            if (g.R(0) != 0) {
                // (thrown: no hit stop)
                g.Write16(fighter + kHurtStop, 0);
                g.R(0) = kHitStop;
                g.Write16(fighter + kHitStop, 0);
            }
            g.Call(kHitStopCheck);
            g.SetT(g.R(0) == 0);
            if (g.R(0) == 0) {
                // (the step: normal, hurt, throwing, thrown, attacking)
                g.R(0) = kFightSteps;
                g.R(2) = Extended(Word(g, fighter + kDoing)) << 2;
                g.R(3) = g.Read32(kFightSteps + g.R(2));
                g.R(4) = fighter;
                g.Call(g.R(3));
                // (the slide's timer, but in a freeze or a hit's stop)
                g.R(2) = kTimerFreeze;
                g.R(3) = ByteExtended(Byte(g, kTimerFreeze));
                if (g.R(3) == 0) {
                    g.R(0) = kHitStop;
                    g.R(1) = Extended(Word(g, fighter + kHitStop));
                    if (g.R(1) == 0) {
                        g.R(0) = kSlideTimer;
                        g.R(3) = Extended(Word(g, fighter + kSlideTimer));
                        if (static_cast<int32_t>(g.R(3)) > 0) {
                            g.R(2) = Extended(Word(g, fighter + kSlideTimer)) - 2;
                            g.Write16(fighter + kSlideTimer, static_cast<uint16_t>(g.R(2)));
                        }
                    }
                }
                g.R(0) = kSliding;
                g.R(3) = Extended(Word(g, fighter + kSlideTimer));
                const bool sliding = static_cast<int32_t>(g.R(3)) < 0;
                g.SetT(!sliding);
                if (sliding) {
                    g.R(2) = 1;
                    g.Write8(fighter + kSliding, 1);
                } else {
                    g.Write8(fighter + kSliding, 0);
                }
            }
            g.R(2) = kTimerFreeze;
            g.R(3) = ByteExtended(Byte(g, kTimerFreeze));
            g.SetT(g.R(3) == 0);
            if (g.R(3) == 0) {
                g.R(4) = fighter;
                g.Call(kTimers);
            }
        }
        g.R(4) = fighter;
        g.Call(kGauges);
        g.Return(60);
        return true;
    }

    // check_hit_stop's frame of the stop (below): 1 when it holds the fighter
    uint32_t HitStopFrame(Guest &g, uint32_t fighter, uint32_t other) {
        uint32_t held = 0;
        g.R(0) = kHitStop;
        g.R(1) = Extended(Word(g, fighter + kHitStop));
        g.SetT(g.R(1) == 0);
        if (g.R(1) != 0) {
            held = 1;
            const int16_t stop = Word(g, fighter + kHitStop);
            g.R(3) = Extended(stop);
            g.SetT(stop > 0);
            if (stop > 0) {
                g.R(2) = Extended(stop) - 1;
                g.Write16(fighter + kHitStop, static_cast<uint16_t>(g.R(2)));
                g.R(0) = g.Read8(fighter + kSuperFreeze);
                g.SetT(g.R(0) == 2);
                if (g.R(0) == 2) {
                    // (a super art's freeze as it begins)
                    g.R(1) = kGameTimer;
                    g.R(2) = Extended(Word(g, fighter + kFreezeBegan));
                    g.R(3) = Extended(Word(g, kGameTimer));
                    if (g.R(2) == g.R(3)) {
                        g.R(2) = Extended(Word(g, fighter + kHitStop)) + 1;
                        g.Write16(fighter + kHitStop, static_cast<uint16_t>(g.R(2)));
                    }
                    g.R(3) = Extended(Word(g, fighter + kHitStop));
                    g.R(0) = kFreezeLeft;
                    g.R(2) = Extended(Word(g, fighter + kFreezeLeft));
                    g.SetT(static_cast<int32_t>(g.R(3)) > static_cast<int32_t>(g.R(2)));
                    if (static_cast<int32_t>(g.R(3)) <= static_cast<int32_t>(g.R(2))) {
                        g.R(2) = g.Read32(fighter + kInput);
                        g.R(0) = ByteExtended(Byte(g, g.R(2) + 1));
                        g.R(1) = fighter + kLeverInFreeze;
                        g.Write8(fighter + kLeverInFreeze, static_cast<uint8_t>(g.R(0)));
                        g.R(0) = kSuperFreeze;
                        g.Write8(fighter + kSuperFreeze, 1);
                    }
                }
            } else {
                // (below 0: counted up, the animation runs on)
                g.R(3) = kCharMove;
                g.R(0) = kHitStop;
                g.R(2) = Extended(stop) + 1;
                g.Write16(fighter + kHitStop, static_cast<uint16_t>(g.R(2)));
                g.R(4) = fighter;
                g.Call(kCharMove);
            }
            // (hurt or thrown, in no step of its own, and the other neither: not held)
            g.R(0) = kStep;
            g.R(2) = Extended(Word(g, fighter + kStep));
            g.SetT(g.R(2) == 0);
            if (g.R(2) == 0) {
                g.R(0) = Extended(Word(g, fighter + kDoing));
                g.R(4) = g.R(0);
                g.SetT(g.R(0) == 1);
                if (g.R(0) != 1) {
                    g.SetT(g.R(0) == 3);
                }
                if (g.R(0) == 1 || g.R(0) == 3) {
                    g.R(0) = kDoing;
                    g.R(4) = Extended(Word(g, other + kDoing));
                    g.R(0) = g.R(4);
                    g.SetT(g.R(0) == 1);
                    if (g.R(0) != 1) {
                        g.SetT(g.R(0) == 3);
                        if (g.R(0) != 3) {
                            held = 0;
                        }
                    }
                }
            }
            g.R(0) = kHitStop;
            g.R(3) = Extended(Word(g, fighter + kHitStop));
            g.SetT(g.R(3) == 0);
            if (g.R(3) == 0) {
                g.R(0) = kAfterHitStopOk;
                g.R(2) = ByteExtended(Byte(g, fighter + kAfterHitStopOk));
                g.SetT(g.R(2) == 0);
                if (g.R(2) != 0) {
                    g.R(3) = kAfterHitStop;
                    g.R(4) = fighter;
                    g.Call(kAfterHitStop);
                }
            }
        }
        g.R(0) = ByteExtended(Byte(g, fighter + kSuperFreeze));
        g.SetT(g.R(0) == 0);
        if (g.R(0) != 0) {
            g.R(3) = kTimerFreeze;
            g.Write8(kTimerFreeze, 1);
        }
        return held;
    }

    // 06119B02 (check_hit_stop): the hit's stop: with both the fighter's hit_stop and its dm_stop, the longer of the two
    // kept (select_hit_stop: as hit_stop if it is in a step of its own, as dm_stop, and then nothing more this frame, if
    // not). A hit_stop above 0 counts down (a super art's freeze as it begins kept a frame on the game's frame it began,
    // and at its last frames the lever taken and the freeze on); below 0 counts up with the animation still running. It
    // holds the fighter (r0 1) unless it is hurt or thrown in no step of its own and the other is neither; at its end
    // the animation the stop jumps to (char_move_cmhs). A super art's freeze holds the round's timers. Leaves r0 the
    // answer, r5-r7 as the routines it calls do (its caller reads them: liveout.py; r1-r4 and T free).
    bool CheckHitStop(Guest &g) {
        const uint32_t fighter = g.R(4);
        {
            Frame frame(g, 24); // (r14, r13, r12, r11, pr and a long)
            const uint32_t other = g.Read32(fighter + kTarget);
            uint32_t held = 0;
            bool over = false; // (the hit's stop given to dm_stop: nothing more this frame)
            if (Word(g, fighter + kHurtStop) != 0 && Word(g, fighter + kHitStop) != 0) {
                if (Word(g, fighter + kStep) != 0) {
                    g.R(5) = Extended(Word(g, fighter + kHurtStop));
                    g.R(0) = kHitStop;
                    g.R(4) = Extended(Word(g, fighter + kHitStop));
                    g.Call(kSelectHitStop);
                    g.R(1) = fighter + kHitStop;
                    g.Write16(fighter + kHitStop, static_cast<uint16_t>(g.R(0)));
                    g.R(0) = kHurtStop;
                    g.Write16(fighter + kHurtStop, 0);
                } else {
                    g.R(5) = Extended(Word(g, fighter + kHitStop));
                    g.R(0) = kHurtStop;
                    g.R(4) = Extended(Word(g, fighter + kHurtStop));
                    g.Call(kSelectHitStop);
                    g.R(1) = fighter + kHurtStop;
                    g.Write16(fighter + kHurtStop, static_cast<uint16_t>(g.R(0)));
                    g.Write16(fighter + kHitStop, 0);
                    over = true;
                }
            }
            if (over) {
                g.R(0) = 0; // (it doesn't hold)
            } else {
                held = HitStopFrame(g, fighter, other);
                g.R(0) = held;
            }
        }
        g.Return(40);
        return true;
    }

    // 06119C6C (select_hit_stop): of two hit stops (r4, r5), the longer, with r4's sign. Leaves r2 and r3 the two
    // as compared, r4 the answer, r5 |r5|, r6 1 if r4 was below 0, T its opposite.
    bool SelectHitStop(Guest &g) {
        g.R(6) = 0;
        if (static_cast<int32_t>(Extended(static_cast<int32_t>(g.R(4)))) < 0) {
            g.R(4) = 0 - g.R(4);
            g.R(6) = 1;
        }
        g.R(3) = Extended(static_cast<int32_t>(g.R(4)));
        if (static_cast<int32_t>(Extended(static_cast<int32_t>(g.R(5)))) < 0) {
            g.R(5) = 0 - g.R(5);
        }
        g.R(2) = Extended(static_cast<int32_t>(g.R(5)));
        if (static_cast<int32_t>(g.R(3)) < static_cast<int32_t>(g.R(2))) {
            g.R(4) = g.R(5);
        }
        g.R(6) = ByteExtended(static_cast<int32_t>(g.R(6)));
        g.SetT(g.R(6) == 0);
        if (g.R(6) != 0) {
            g.R(4) = 0 - g.R(4);
        }
        g.R(0) = Extended(static_cast<int32_t>(g.R(4)));
        g.Return(14);
        return true;
    }

    // 06119C96 (look_after_timers): a frame off the timers: it can't be thrown, a throw may be broken, a quick stand
    // may be made (made, check_ukemi_flag, and the chance over either way), a projectile's hits counted; and the stun
    // taken recovers a frame's worth, but in a hit's stop. (free: r0-r5 and T, which no caller reads: liveout.py)
    bool LookAfterTimers(Guest &g) {
        const uint32_t fighter = g.R(4);
        {
            Frame frame(g, 12); // (r14, r13, pr)
            const auto countDown = [&](uint32_t offset) {
                if (g.Read8(fighter + offset) != 0) {
                    g.Write8(fighter + offset, static_cast<uint8_t>(g.Read8(fighter + offset) - 1));
                }
            };
            countDown(kUncatchable);
            countDown(kBreakTimer);
            if (g.Read8(fighter + kQuickStandOk) != 0) {
                g.Write16(fighter + kQuickStandTimer, static_cast<uint16_t>(g.Read16(fighter + kQuickStandTimer) - 1));
                if (Word(g, fighter + kQuickStandTimer) <= 0) {
                    g.Write16(fighter + kQuickStandTimer, 0);
                    g.Write8(fighter + kQuickStandOk, 0);
                    g.Write8(fighter + kQuickStood, 0);
                } else {
                    g.R(2) = kQuickStandCheck;
                    g.R(4) = fighter;
                    g.Call(kQuickStandCheck);
                    if (g.R(0) != 0) {
                        g.R(3) = 1;
                        g.Write16(fighter + kQuickStandTimer, 0);
                        g.Write8(fighter + kQuickStandOk, 0);
                        g.Write8(fighter + kQuickStood, 1);
                    }
                }
            }
            if (g.Read16(fighter + kProjectileHits) != 0) {
                const int16_t left = static_cast<int16_t>(g.Read16(fighter + kProjectileTimer) - 1);
                g.Write16(fighter + kProjectileTimer, static_cast<uint16_t>(left));
                if (left <= 0) {
                    g.Write16(fighter + kProjectileHits, 0);
                }
            }
            // (the stun's recovery)
            const uint32_t stun = g.Read32(fighter + kStun);
            if (g.Read16(stun + 8) != 0 && g.Read16(fighter + kHitStop) == 0) {
                g.Write32(stun + 8, g.Read32(stun + 8) - g.Read32(stun + 12));
                if (Word(g, g.Read32(fighter + kStun) + 8) <= 0) {
                    g.Write32(g.Read32(fighter + kStun) + 8, 0);
                }
            }
        }
        g.Return(40);
        return true;
    }

    // 0611AAF8 (check_lever_data): in a fight (routine_no[0] 4), the step's own reading of the input (process_ndcca by
    // routine_no[1], jumped to). Leaves every register as the game's code.
    bool LeverData(Guest &g) {
        const uint32_t fighter = g.R(4);
        g.R(0) = Extended(Word(g, fighter + kSteps));
        g.SetT(g.R(0) == 4);
        if (g.R(0) != 4) {
            g.Return(8);
            return true;
        }
        g.R(2) = Extended(Word(g, fighter + kDoing)) << 2;
        g.R(0) = kReadings;
        g.R(3) = g.Read32(kReadings + g.R(2));
        g.R(4) = fighter;
        g.Call(g.R(3));
        g.Return(20);
        return true;
    }

    // A test of the readings' (r4 the fighter, r5 as given or as it is): r0 not 0 when it started something
    bool Started(Guest &g, uint32_t fighter, uint32_t test) {
        g.R(3) = test;
        g.R(4) = fighter;
        g.Call(test);
        g.SetT(g.R(0) == 0);
        return g.R(0) != 0;
    }

    // 0611C6F6 (check_cg_cancel_data): the frame's cancels (cg_cancel) tried, while the attack has hit or been guarded
    // (meoshi_hit_flag) a super art (0x40; then no cancel left) and a special move or a taunt (0x20); and whatever it
    // hit, a repeated press (0x10), a chain (8), another normal move by other buttons (4); after a hit or guard again,
    // a dash (2) and a high jump (1). r0 1 when one started. Leaves every register as the game's code.
    bool CancelData(Guest &g) {
        const uint32_t fighter = g.R(4);
        uint32_t answer = 0;
        {
            Frame frame(g, 8); // (r14, pr)
            const auto cancels = [&] { return g.Read8(fighter + kCancels); };
            answer = [&]() -> uint32_t {
                g.R(3) = ByteExtended(static_cast<int8_t>(cancels()));
                if (g.R(3) == 0) {
                    g.SetT(true);
                    return 0;
                }
                g.R(3) = ByteExtended(Byte(g, fighter + kCancelHit));
                if (g.R(3) != 0) {
                    g.R(0) = cancels();
                    g.SetT((g.R(0) & 0x40) == 0);
                    if (g.R(0) & 0x40) {
                        g.R(5) = 0;
                        if (Started(g, fighter, kFullGauge) || Started(g, fighter, kSuperArt)) {
                            g.R(3) = 0;
                            g.Write8(fighter + kCancels, 0);
                            return 1;
                        }
                    }
                    g.R(0) = cancels();
                    g.SetT((g.R(0) & 0x20) == 0);
                    if (g.R(0) & 0x20) {
                        if (Started(g, fighter, kSpecial) || Started(g, fighter, kTaunt)) {
                            return 1;
                        }
                    }
                }
                g.R(0) = cancels();
                g.SetT((g.R(0) & 0x10) == 0);
                if ((g.R(0) & 0x10) && Started(g, fighter, kRepeated)) {
                    return 1;
                }
                g.R(0) = cancels();
                g.SetT((g.R(0) & 8) == 0);
                if ((g.R(0) & 8) && Started(g, fighter, kChain)) {
                    return 1;
                }
                g.R(0) = cancels();
                g.SetT((g.R(0) & 4) == 0);
                if (g.R(0) & 4) {
                    // (another normal move: by buttons other than the one under way's)
                    g.R(2) = fighter + kNormalAttack;
                    g.R(3) = 0x770;
                    g.R(0) = g.Read16(g.Read32(fighter + kInput) + 6) & 0x770;
                    g.R(2) = Extended(Word(g, fighter + kNormalAttack));
                    g.SetT(g.R(0) == g.R(2));
                    if (g.R(0) != g.R(2)) {
                        g.R(2) = kNormal;
                        g.R(4) = fighter;
                        g.Call(kNormal);
                        g.SetT(g.R(0) == 0);
                        if (g.R(0) != 0) {
                            return 1;
                        }
                    }
                }
                g.R(3) = ByteExtended(Byte(g, fighter + kCancelHit));
                g.SetT(g.R(3) == 0);
                if (g.R(3) == 0) {
                    return 0;
                }
                g.R(0) = cancels();
                g.SetT((g.R(0) & 2) == 0);
                if ((g.R(0) & 2) && Started(g, fighter, kDash)) {
                    return 1;
                }
                g.R(0) = cancels();
                g.SetT((g.R(0) & 1) == 0);
                if ((g.R(0) & 1) && Started(g, fighter, kHighJumpCheck)) {
                    g.R(3) = 1;
                    g.Write8(fighter + kHighJump, 1);
                    return 1;
                }
                return 0;
            }();
        }
        g.R(0) = answer;
        g.Return(30);
        return true;
    }

    // 0611C5CE (process_attack): an attack's reading of the input, from its second step: landed (check_ashimoto_ex),
    // or in its first frames (cancel_timer, counted down out of a hit's stop) a move started over it (a full gauge's,
    // a super art, a special move, a taunt, a throw, a leap); a normal move's super art by a full gauge (then no cancel
    // left), a jump's triangle jump; then the frame's cancels (check_cg_cancel_data), and with none, on the floor (or
    // the car), the jump's frame types (jumping_cg_type_check, jumped to). Leaves every register as the game's code.
    bool ProcessAttack(Guest &g) {
        const uint32_t fighter = g.R(4);
        bool landing = false;
        {
            Frame frame(g, 8); // (r14, pr)
            landing = [&] {
                g.R(0) = Extended(Word(g, fighter + kStep));
                g.SetT(g.R(0) == 0);
                if (g.R(0) != 0) {
                    if (Started(g, fighter, kOnFloorCheck)) {
                        return false;
                    }
                    g.R(0) = ByteExtended(Byte(g, fighter + kCancelTimer));
                    g.SetT(g.R(0) == 0);
                    if (g.R(0) != 0) {
                        g.R(3) = Extended(Word(g, fighter + kHitStop));
                        g.SetT(g.R(3) == 0);
                        if (g.R(3) == 0) {
                            g.R(0) = kCancelTimer;
                            g.R(3) = ByteExtended(Byte(g, fighter + kCancelTimer)) - 1;
                            g.Write8(fighter + kCancelTimer, static_cast<uint8_t>(g.R(3)));
                        }
                    }
                    g.R(0) = ByteExtended(Byte(g, fighter + kCancelTimer));
                    g.SetT(g.R(0) == 0);
                    if (g.R(0) != 0) {
                        g.R(5) = 0;
                        if (Started(g, fighter, kFullGauge)) {
                            return false;
                        }
                        g.R(5) = 0;
                        for (const uint32_t test : {kFullGauge2, kSuperArt, kSpecial, kTaunt, kThrow, kLeap}) {
                            if (Started(g, fighter, test)) {
                                return false;
                            }
                        }
                    }
                    g.R(0) = kWhich;
                    g.R(2) = 16;
                    g.R(3) = Extended(Word(g, fighter + kWhich));
                    g.SetT(static_cast<int32_t>(g.R(3)) >= 16);
                    if (static_cast<int32_t>(g.R(3)) < 16) {
                        g.R(5) = 1;
                        g.R(1) = kFullGauge;
                        g.R(4) = fighter;
                        g.Call(kFullGauge);
                        g.SetT(g.R(0) == 0);
                        if (g.R(0) != 0) {
                            g.R(3) = 0;
                            g.R(0) = kCancels;
                            g.Write8(fighter + kCancels, 0);
                            return false;
                        }
                    }
                    g.R(0) = Extended(Word(g, fighter + kWhich));
                    g.SetT(g.R(0) == 3);
                    if (g.R(0) == 3) {
                        g.R(2) = kTriangleJump;
                        g.R(4) = fighter;
                        g.Call(kTriangleJump);
                        g.SetT(g.R(0) == 0);
                        if (g.R(0) != 0) {
                            return false;
                        }
                    }
                }
                g.R(4) = fighter;
                g.Call(0x0611C6F6); // (check_cg_cancel_data)
                g.SetT(g.R(0) == 0);
                if (g.R(0) != 0) {
                    return false;
                }
                g.R(0) = kStep;
                g.R(3) = Extended(Word(g, fighter + kStep));
                g.SetT(g.R(3) == 0);
                if (g.R(3) == 0) {
                    return false;
                }
                g.R(0) = kY;
                g.R(2) = Extended(Word(g, fighter + kY));
                g.SetT(g.R(2) == 0);
                if (g.R(2) != 0) {
                    g.R(0) = kOnCar;
                    g.R(3) = ByteExtended(Byte(g, fighter + kOnCar));
                    g.SetT(g.R(3) == 0);
                    if (g.R(3) == 0) {
                        return false;
                    }
                }
                return true; // (on the floor: the jump's frame types)
            }();
        }
        if (landing) {
            g.R(4) = fighter;
            g.R(3) = kJumpingCgType;
            g.Call(kJumpingCgType);
        }
        g.Return(40);
        return true;
    }

    // 0611C504 (process_catch): a throw's end, from its second step: its frame type 64 (the throw done) or 0xFF (let
    // go) takes the thrower back to its normal steps, standing or in the air by its pose (TO_nm_36000 or 37000, and
    // TO_nm_01000 or 09000, jumped to). Leaves every register as the game's code.
    bool ProcessCatch(Guest &g) {
        const uint32_t fighter = g.R(4);
        g.R(0) = kStep;
        g.R(3) = Extended(Word(g, fighter + kStep));
        g.SetT(g.R(3) == 0);
        if (g.R(3) == 0) {
            g.Return(8);
            return true;
        }
        g.R(0) = g.Read8(fighter + kCgType);
        g.R(5) = 32;
        g.SetT(g.R(0) == 64);
        uint32_t to = 0;
        if (g.R(0) == 64) {
            g.R(0) = kPose;
            g.R(3) = g.Read8(fighter + kPose);
            g.SetT(g.R(3) >= 32);
            if (g.R(3) >= 32) {
                to = g.R(2) = kToThrowEndAir;
            } else {
                to = g.R(3) = kToThrowEnd;
            }
        } else {
            g.R(1) = 0xFF;
            g.SetT(g.R(0) == 0xFF);
            if (g.R(0) != 0xFF) {
                g.Return(10);
                return true;
            }
            g.R(0) = kPose;
            g.R(2) = g.Read8(fighter + kPose);
            g.SetT(g.R(2) >= 32);
            if (g.R(2) >= 32) {
                to = g.R(2) = kToCrouch;
            } else {
                to = g.R(3) = kToStand;
            }
        }
        g.Call(to);
        g.Return(20);
        return true;
    }

    // 061214FC (setup_normal_process_flags): a normal step's flags: drawn at its own place, no running, the stun's
    // flag, it may guard, not guarding nor throwing nor thrown, the scroll set, no hurt placing, no quick stand, no
    // slide, no super art freeze, no catching of attacks, no caution, the super art effects off, no quick stand chance,
    // no cancel timer, a throw neither breakable nor not, no command asked, no hit stop jump, and but in a high jump's
    // step (17) no high jump. Leaves r0 0x45D (or 17), r3 the super art record, r5 0, T as the game's code.
    bool NormalFlags(Guest &g) {
        const uint32_t fighter = g.R(4);
        g.R(5) = 0;
        g.Write16(fighter + kDrawnAt, g.Read16(fighter + kMyDrawn));
        g.Write8(fighter + kRunning, 0);
        g.Write8(g.Read32(fighter + kStun), 0);
        for (const uint32_t offset : {kGuardMode, kGuarding, kHolding, kHeld, kHurtPlaced, kQuickStood, kSuperFreeze,
                                      kAtemi, kWarned}) {
            g.Write8(fighter + offset, 0);
        }
        g.Write8(fighter + kScrollSet, 1);
        g.Write16(fighter + kSlideTimer, 0);
        g.Write16(fighter + kSlideCount, 0);
        g.Write8(g.Read32(fighter + kArts) + 20, 0);
        g.R(3) = g.Read32(fighter + kArts);
        g.Write8(g.R(3) + 21, 0);
        g.Write16(fighter + kQuickStandTimer, 0);
        for (const uint32_t offset : {kQuickStandOk, kCancelTimer, kUnbreakable, kMayBreak, kAfterHitStopOk}) {
            g.Write8(fighter + offset, 0);
        }
        g.Write16(fighter + kCommand, 0);
        g.R(0) = Extended(Word(g, fighter + kWhich));
        g.SetT(g.R(0) == 17);
        if (g.R(0) != 17) {
            g.R(0) = kHighJump;
            g.Write8(fighter + kHighJump, 0);
        }
        g.Return(40);
        return true;
    }

    // The drawing order after a step: in front of the other (by its cg_prio) or behind, from `base`
    void DrawnAfter(Guest &g, uint32_t fighter, int front, int behind) {
        g.R(0) = kPriority;
        g.R(1) = ByteExtended(Byte(g, fighter + kPriority));
        g.SetT(g.R(1) == 0);
        if (g.R(1) == 0) {
            return;
        }
        g.R(0) = g.Read8(fighter + kPriority);
        g.SetT(g.R(0) == 1);
        g.R(2) = Extended(Word(g, fighter + kDrawnAt)) + static_cast<uint32_t>(g.R(0) == 1 ? front : behind);
        g.R(0) = kDrawnAt;
        g.Write16(fighter + kDrawnAt, static_cast<uint16_t>(g.R(2)));
    }

    // 061214A4 (Player_normal): a fighter's normal steps (standing, walking, crouching, jumping, landing, getting up):
    // the flags (setup_normal_process_flags), the throws' power off where they are over (its own and the other's),
    // the step by routine_no[2] (plpnm_lv_00); then drawn by the other's place: in front, or behind. Leaves every
    // register as the game's code.
    bool PlayerNormal(Guest &g) {
        const uint32_t fighter = g.R(4);
        {
            Frame frame(g, 8); // (r14, pr)
            g.Call(kNormalFlags);
            g.R(5) = g.Read32(fighter + kTarget);
            g.R(2) = kMyThrowOff;
            g.R(4) = fighter;
            g.Call(kMyThrowOff);
            g.R(5) = g.Read32(fighter + kTarget);
            g.R(3) = kOtherThrowOff;
            g.R(4) = fighter;
            g.Call(kOtherThrowOff);
            g.R(2) = Extended(Word(g, fighter + kWhich)) << 2;
            g.R(0) = kNormalSteps;
            g.R(3) = g.Read32(kNormalSteps + g.R(2));
            g.R(4) = fighter;
            g.Call(g.R(3));
            g.R(0) = kPriority;
            g.R(1) = ByteExtended(Byte(g, fighter + kPriority));
            g.SetT(g.R(1) == 0);
            if (g.R(1) != 0) {
                g.R(3) = g.Read32(fighter + kTarget);
                g.R(2) = Extended(Word(g, g.R(3) + kMyDrawn));
                g.Write16(fighter + kDrawnAt, static_cast<uint16_t>(g.R(2)));
                DrawnAfter(g, fighter, 1, -3);
            }
        }
        g.Return(30);
        return true;
    }

    // 06126120 (Player_attack): a fighter's attacks: the step's flags (as Player_normal's, but it may not guard), the
    // other's throw's power off; at an attack's first step caution_flag, the damage kept and the attacks' count
    // cleared; the guard a jump's attack allows (jumping_guard_type_check); then the attack by routine_no[2]: from 16 the
    // character's own (specials, super arts, command throws: plxx_extra_attack_table by character), below the shared
    // normal moves (plpat_lv_00, the super art effects off); then drawn just behind the other, or by its cg_prio four
    // in front or further behind. Leaves every register as the game's code.
    bool PlayerAttack(Guest &g) {
        const uint32_t fighter = g.R(4);
        {
            Frame frame(g, 12); // (r14, r13, pr)
            g.Write16(fighter + kDrawnAt, g.Read16(fighter + kMyDrawn));
            g.Write8(fighter + kRunning, 0);
            g.Write8(g.Read32(fighter + kStun), 0);
            g.Write8(fighter + kGuardMode, 3);
            for (const uint32_t offset : {kGuarding, kHolding, kHeld, kHurtPlaced, kQuickStood, kSuperFreeze,
                                          kQuickStandOk, kUnbreakable, kMayBreak}) {
                g.Write8(fighter + offset, 0);
            }
            g.Write8(fighter + kScrollSet, 1);
            g.Write16(fighter + kSlideTimer, 0);
            g.Write16(fighter + kSlideCount, 0);
            g.Write16(fighter + kQuickStandTimer, 0);
            g.R(0) = kMayBreak;
            g.R(3) = kOtherThrowOff;
            g.R(5) = g.Read32(fighter + kTarget);
            g.R(4) = fighter;
            g.Call(kOtherThrowOff);
            g.R(0) = kStep;
            g.R(2) = Extended(Word(g, fighter + kStep));
            g.R(4) = fighter;
            g.SetT(g.R(2) == 0);
            if (g.R(2) == 0) {
                // (its first step)
                g.R(3) = 1;
                g.Write8(fighter + kWarned, 1);
                g.Write16(fighter + kKept, 0);
                g.Write8(fighter + kKeptUse, 0);
                g.Write16(fighter + kAttacksLive, 0);
                g.R(0) = kAfterHitStopOk;
                g.Write8(fighter + kAfterHitStopOk, 0);
            }
            g.R(3) = kJumpGuard;
            g.Call(kJumpGuard);
            g.R(0) = kWhich;
            g.R(2) = Extended(Word(g, fighter + kWhich));
            g.R(3) = 15;
            g.SetT(static_cast<int32_t>(g.R(2)) > 15);
            if (static_cast<int32_t>(g.R(2)) > 15) {
                // (the character's own moves)
                g.R(1) = Extended(Word(g, fighter + kCharacter)) << 2;
                g.R(0) = kOwnAttacks;
                g.R(2) = g.Read32(kOwnAttacks + g.R(1));
                g.R(4) = fighter;
                g.Call(g.R(2));
            } else {
                // (the normal moves every character shares)
                g.R(2) = g.Read32(fighter + kArts);
                g.Write8(g.R(2) + 20, 0);
                g.R(2) = g.Read32(fighter + kArts);
                g.R(0) = 21;
                g.Write8(g.R(2) + 21, 0);
                g.R(2) = Extended(Word(g, fighter + kWhich)) << 2;
                g.R(0) = kSharedAttacks;
                g.R(1) = g.Read32(kSharedAttacks + g.R(2));
                g.R(4) = fighter;
                g.Call(g.R(1));
            }
            g.R(3) = g.Read32(fighter + kTarget);
            g.R(2) = Extended(Word(g, g.R(3) + kMyDrawn)) - 3;
            g.R(0) = kDrawnAt;
            g.Write16(fighter + kDrawnAt, static_cast<uint16_t>(g.R(2)));
            DrawnAfter(g, fighter, 4, -4);
        }
        g.Return(60);
        return true;
    }

    // The first step of a shared normal move (Attack_00000, Attack_01000): its next step; set on the floor
    // (hoken_muriyari_chakuchi); facing its move's way, and the lever turned with it (setup_lvdir_after_autodir); the
    // frames it may be cancelled at its start (get_cancel_timer); its animation begun (set_char_move_init: the normal
    // moves' list, 4, the move's entry's animation)
    void BeginNormal(Guest &g, uint32_t fighter) {
        g.R(0) = kStep;
        g.R(3) = Extended(Word(g, fighter + kStep)) + 1;
        g.Write16(fighter + kStep, static_cast<uint16_t>(g.R(3)));
        g.R(4) = fighter;
        g.Call(kLanded);
        g.R(0) = ByteExtended(Byte(g, fighter + kFacingMove));
        g.Write8(fighter + kFacing, static_cast<uint8_t>(g.R(0)));
        g.R(3) = kAutoDir;
        g.R(4) = fighter;
        g.Call(kAutoDir);
        g.R(4) = fighter;
        g.Call(kCancelTimerSet);
        g.R(5) = 4;
        g.R(3) = kSetMove;
        g.R(0) = Extended(Word(g, g.Read32(fighter + kMoveData) + 2));
        g.R(6) = g.R(0);
        g.R(4) = fighter;
    }

    // 0612621A (Attack_00000): a normal move on the ground: begun (above), then its animation run until its frame type
    // says its end (20): then the step after the attack (routine_no[2] 4), cg_type cleared, the scroll set again.
    // Leaves every register as the game's code.
    bool Attack00000(Guest &g) {
        const uint32_t fighter = g.R(4);
        g.R(3) = 0;
        g.R(0) = kScrollSet;
        g.Write8(fighter + kScrollSet, 0);
        const int16_t step = Word(g, fighter + kStep);
        g.R(0) = Extended(step);
        g.SetT(step == 0);
        if (step == 0) {
            {
                Frame frame(g, 8); // (r14, pr)
                BeginNormal(g, fighter);
            }
            g.Call(kSetMove);
        } else if (step == 1) {
            {
                Frame frame(g, 8);
                g.R(3) = kCharMove;
                g.R(4) = fighter;
                g.Call(kCharMove);
                g.R(0) = g.Read8(fighter + kCgType);
                g.SetT(g.R(0) == 20);
                if (g.R(0) == 20) {
                    // (its end)
                    g.R(2) = 1;
                    g.R(0) = kScrollSet;
                    g.R(3) = 0;
                    g.Write16(fighter + kWhich, 4);
                    g.Write8(fighter + kCgType, 0);
                    g.Write8(fighter + kScrollSet, 1);
                }
            }
        } else {
            g.SetT(false);
        }
        g.Return(30);
        return true;
    }

    // 061262AA (Attack_01000): a normal move that moves: begun (as above) with its movement (setup_mvxy_data, by the
    // move's entry); its animation run to its moving part (frame type 1: the speed added, add_mvxy_speed, then step 2),
    // through the air (jumping_union_process, 3) and on (step 3: its animation). Leaves every register as the game's
    // code.
    bool Attack01000(Guest &g) {
        const uint32_t fighter = g.R(4);
        const int16_t step = Word(g, fighter + kStep);
        g.R(0) = Extended(step);
        g.SetT(step == 0);
        uint32_t then = 0;
        {
            Frame frame(g, 8); // (r14, pr)
            switch (step) {
            case 0:
                BeginNormal(g, fighter);
                g.Call(kSetMove);
                g.R(4) = fighter;
                g.R(0) = kMoveData;
                g.R(3) = kMoveSet;
                g.R(5) = Extended(Word(g, g.Read32(fighter + kMoveData) + 4));
                g.R(0) = g.R(5);
                then = kMoveSet;
                break;
            case 1:
                g.SetT(false);
                g.R(3) = kCharMove;
                g.R(4) = fighter;
                g.Call(kCharMove);
                g.R(0) = g.Read8(fighter + kCgType);
                g.SetT(g.R(0) == 1);
                if (g.R(0) == 1) {
                    g.R(2) = kMoveSpeed;
                    g.R(4) = fighter;
                    g.Call(kMoveSpeed);
                    g.R(3) = 2;
                    g.R(0) = kCgType;
                    g.R(2) = 0;
                    g.Write16(fighter + kStep, 2);
                    g.Write8(fighter + kCgType, 0);
                }
                break;
            case 2:
                g.SetT(true);
                g.R(5) = 3;
                g.R(2) = kJumping;
                g.R(4) = fighter;
                then = kJumping;
                break;
            case 3:
                g.SetT(true);
                g.R(4) = fighter;
                g.R(2) = kCharMove;
                then = kCharMove;
                break;
            default:
                g.SetT(false);
                break;
            }
        }
        if (then) {
            g.Call(then);
        }
        g.Return(30);
        return true;
    }

    // (free: what liveout.py shows no caller reads, the leftovers of a routine called through a table kept as they are)
    const Lift kLifts[] = {
        {"PlayerFight", 0x06119A6C, 0x96, 0xA8DEBCC834F3E24Bull, PlayerFight, true, 0x1007E},
        {"CheckHitStop", 0x06119B02, 0x42, 0xDEAC8060C21C11A0ull, CheckHitStop, true, 0x1001E},
        {"SelectHitStop", 0x06119C6C, 0x2A, 0x4CE0CFAD38BA1FCCull, SelectHitStop, true, 0x1001E},
        {"LookAfterTimers", 0x06119C96, 0xB4, 0xF127CEA7BBD7F1BCull, LookAfterTimers, true, 0x1003F},
        {"LeverData", 0x0611AAF8, 0x22, 0xB4C82A1C8E1FE551ull, LeverData, true},
        {"CancelData", 0x0611C6F6, 0xA4, 0xD04C8739581D6DF3ull, CancelData, true},
        {"ProcessAttack", 0x0611C5CE, 0xAA, 0x9045859C289767DFull, ProcessAttack, true},
        {"ProcessCatch", 0x0611C504, 0x4C, 0x45DC23F811BF5F8Bull, ProcessCatch, true},
        {"NormalFlags", 0x061214FC, 0x84, 0x12C1D80D8892FDCDull, NormalFlags, true},
        {"PlayerNormal", 0x061214A4, 0x58, 0x5332378B76324AC7ull, PlayerNormal, true},
        {"PlayerAttack", 0x06126120, 0xFA, 0x7C5C920E1AA7830Aull, PlayerAttack, true},
        {"Attack00000", 0x0612621A, 0x90, 0x7ADF3F6E2C54443Dull, Attack00000, true},
        {"Attack01000", 0x061262AA, 0x9A, 0x065AAC2EF9C9125Full, Attack01000, true},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightMoves() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
