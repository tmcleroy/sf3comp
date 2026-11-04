// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's round, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h): the round's
// time each frame (0x06096CE8), and its end: the time over (0x06117C26) and the knock-out (0x06117D28).
//
// The round's time. Once a frame of a match the game's main task (0x06095B36) runs Time_Control: while the fighters may
// fight (Allow_a_battle_f) and the time is not held (Demo_Time_Stop, or a bonus stage: Bonus_Game_Flag), the round's
// clock goes on (count_cont_main: lifts.cpp's ClockTick, which ticks the seconds and flashes the clock) and so does the
// play time, a count of seconds of fighting the computer's levels go by (Control_Time: a second each 60 frames, by
// Time_in_Time, up to Limit_Time).
//
// The round's end. Each frame of a round the players' routine (plcnt_move, 0x06116DF0) first looks for a time over
// (time_over_check): the clock run out with no player about to be knocked out by the hit it is taking (will_die), the
// one with more health wins (player 1 on a draw; the round's management tells a draw from the health later); then,
// after the players have moved, for a knock-out (settle_check): a player whose health went below nothing is "dead"
// (the record's dead_flag), one or both. A super art that brings its fighter back (check_sa_resurrection) undoes that;
// otherwise the round is concluded: Conclusion_Flag set, Conclusion_Type 0 a knock-out, 1 a double knock-out, 2 a time
// over, the winner and the loser noted (Winner_id, Loser_id), the knock-out's kind added to Round_Result (from the
// loser's record: dm_koa), and the players' routine moved on to the round's end (setup_settle_rno: its step 2, the end's
// kind beside it: pcon_rno). In a fight of people's (Demo_Flag; not attract mode's) the big message (K.O., double K.O.,
// time over) is asked for (request_center_message). One end is the game's own: Akuma beaten by Shin Akuma's finish
// while gouki_wins says so (Round_Result's bit 0x800), where the music fades and the end is the fourth kind, the round
// not concluded yet. The round's management (Game_Management, 0x060992B0, at its third step) then sees Conclusion_Flag
// and goes on to count the win.
//
// (The tag team battle's lift on 0x06117D28, battle.cpp's KnockOutLift, is registered after these and so runs first:
// in a tag a fighter knocked out with a partner standing is not the round's end. Otherwise this rewrite runs.)
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    // --- the round's time ---
    constexpr uint32_t kFightOn = 0x02011389;   // byte: the fighters may fight (Allow_a_battle_f)
    constexpr uint32_t kTimeHeld = 0x020155F6;  // byte: the time held (Demo_Time_Stop)
    constexpr uint32_t kBonusGame = 0x02016B3A; // word: a bonus stage (Bonus_Game_Flag)
    constexpr uint32_t kPlayTime = 0x02011372;  // word: seconds fought (Control_Time)
    constexpr uint32_t kFramesToSecond = 0x02011374; // word: frames to its next second (Time_in_Time)
    constexpr uint32_t kMostPlayTime = 0x02016AD4;   // word: where it stops (Limit_Time)
    constexpr uint32_t kClockGoesOn = 0x060D7BAE;    // count_cont_main (lifts.cpp's ClockTick)

    // --- the round's end ---
    constexpr uint32_t kSettleSteps = 0x02068C5C;   // words: the players' routine's step, the end's kind, its step
                                                    // (pcon_rno)
    constexpr uint32_t kHitsChecked = 0x0200EE44;   // byte (ca_check_flag)
    constexpr uint32_t kPlayersHeld = 0x02068C67;   // byte: the players' controls not taken (pcon_dp_flag)
    constexpr uint32_t kWinner = 0x0201137F, kLoser = 0x02011380; // bytes (Winner_id, Loser_id)
    constexpr uint32_t kRoundResult = 0x0201137C;   // word: how the round ended, bits (Round_Result)
    constexpr uint32_t kConcluded = 0x020113D8;     // word (Conclusion_Flag)
    constexpr uint32_t kConclusion = 0x020113C0;    // word: 0 a knock-out, 1 a double, 2 a time over (Conclusion_Type)
    constexpr uint32_t kPeoplePlay = 0x020154AF;    // byte: not attract mode's fight (Demo_Flag)
    constexpr uint32_t kRoundClock = 0x02028678;    // long: the round's clock, the panel's (round_timer)
    constexpr uint32_t kGoukiWins = 0x02016B32;     // byte (gouki_wins)
    constexpr uint32_t kShinGoukiMusic = 0x02016B79; // byte (Shin_Gouki_BGM)
    constexpr uint16_t kShinGoukiFinish = 0x800;    // (Round_Result's bit)
    // ... in a player's record
    constexpr uint32_t kHealth = 0x9E;        // word (vital_new)
    constexpr uint32_t kDamage = 0xA2;        // word: the damage of the hit taking hold (dm_vital)
    constexpr uint32_t kKnockOutKind = 0x336; // word (dm_koa)
    constexpr uint32_t kDead = 0x3FD;         // byte (dead_flag)
    constexpr uint32_t kChipDeath = 0x41D;    // byte: knocked out by a guarded hit (kezurijini_flag)
    // ... and the routines called
    constexpr uint32_t kBroughtBack = 0x06117E66;   // check_sa_resurrection (r4 the record): r0 1 when it was
    constexpr uint32_t kRebirth = 0x06117E8C;       // check_sa_type_rebirth (r4): its super art brings it back
    constexpr uint32_t kGoukiWinsSet = 0x06117FD0;  // setup_gouki_wins
    constexpr uint32_t kSettle = 0x06117CCC;        // setup_settle_rno (r4 the kind)
    constexpr uint32_t kCentreMessage = 0x0609C274; // request_center_message (r4 which)
    constexpr uint32_t kMusicFade = 0x0609C948;     // Control_Music_Fade (r4 the frames)
    constexpr uint32_t kWillDie = 0x06117C9E;

    constexpr uint32_t kOne = kPlayers, kTwo = kPlayers + kPlayerSize;
    uint32_t Player(int32_t side) {
        return kPlayers + Signed(static_cast<int16_t>(side * static_cast<int32_t>(kPlayerSize)));
    }

    // 06096CE8 (Time_Control): the clock and the play time, a frame on. Leaves r5 the play time's limit or the frames'
    // place, r6 and r7 as the clock leaves them (the caller reads them); with the time held, r5-r7 as they came. (The
    // port's: the scores drawn first, when a feature asked: fight_score.cpp.)
    bool TimeControl(Guest &g) {
        if (ScoresDrawAsked()) {
            DrawScores(g);
        }
        if (g.Read8(kFightOn) == 0 || g.Read8(kTimeHeld) != 0 || g.Read16(kBonusGame) != 0) {
            g.Return(12);
            return true;
        }
        Frame frame(g, 4); // (pr)
        g.Call(kClockGoesOn);
        const int16_t most = Word(g, kMostPlayTime);
        g.R(5) = Signed(most);
        if (Word(g, kPlayTime) >= most) {
            g.Write16(kPlayTime, static_cast<uint16_t>(most));
            g.Return(20);
            return true;
        }
        g.R(5) = kFramesToSecond;
        g.Write16(kFramesToSecond, static_cast<uint16_t>(Word(g, kFramesToSecond) - 1));
        if (Word(g, kFramesToSecond) == 0) {
            g.Write16(kFramesToSecond, 60);
            g.Write16(kPlayTime, static_cast<uint16_t>(Word(g, kPlayTime) + 1));
        }
        g.Return(30);
        return true;
    }

    // 06117CCC (setup_settle_rno): the players' routine to the round's end, of the kind r4 (its step 2), their controls
    // not taken. Leaves r0 0, r1 kPlayersHeld's place, r2 1, r3 kHitsChecked's, r5 kSettleSteps'.
    bool Settle(Guest &g) {
        g.Write16(kSettleSteps, 2);
        g.Write16(kSettleSteps + 2, static_cast<uint16_t>(g.R(4)));
        g.Write16(kSettleSteps + 4, 0);
        g.Write8(kHitsChecked, 0);
        g.Write8(kPlayersHeld, 1);
        g.R(0) = 0;
        g.R(1) = kPlayersHeld;
        g.R(2) = 1;
        g.R(3) = kHitsChecked;
        g.R(5) = kSettleSteps;
        g.Return(12);
        return true;
    }

    // 06117C9E (will_die): no player is taking more damage than its health (r0 1): a time over may be called
    bool WillDie(Guest &g) {
        g.R(0) = Word(g, kOne + kDamage) > Word(g, kOne + kHealth) || Word(g, kTwo + kDamage) > Word(g, kTwo + kHealth) ? 0 : 1;
        g.Return(16);
        return true;
    }

    // 06117C26 (time_over_check): a time over, when the clock has run out and no hit is knocking a player out: the one
    // with more health the winner (player 1 on a draw), the round concluded (kind 2) and the damage under way dropped.
    // r0 1 when it was.
    bool TimeOverCheck(Guest &g) {
        Frame frame(g, 16); // (r12-r14, pr)
        g.Call(kWillDie);
        if (g.R(0) == 0 || g.Read32(kRoundClock) != 0) {
            g.R(0) = 0;
            g.Return(20);
            return true;
        }
        const bool twoWins = Word(g, kOne + kHealth) < Word(g, kTwo + kHealth);
        g.Write8(kWinner, twoWins ? 1 : 0);
        g.Write8(kLoser, twoWins ? 0 : 1);
        g.Call(kGoukiWinsSet);
        g.Write16(kConcluded, 1);
        g.Write16(kConclusion, 2);
        g.R(4) = 2;
        g.Call(kSettle);
        if (g.Read8(kPeoplePlay) != 0) {
            g.R(4) = 2;
            g.Call(kCentreMessage);
        }
        g.Write16(kTwo + kDamage, 0);
        g.Write16(kOne + kDamage, 0);
        g.Write16(kRoundResult, static_cast<uint16_t>(g.Read16(kRoundResult) | 1));
        g.R(0) = 1;
        g.Return(60);
        return true;
    }

    // 06117E66 (check_sa_resurrection): a fighter (r4) whose super art brings it back is not knocked out after all
    // (r0 1), nor knocked out by a guarded hit
    bool BroughtBack(Guest &g) {
        const uint32_t player = g.R(4);
        Frame frame(g, 8); // (r14, pr)
        g.Call(kRebirth);
        if (g.R(0) == 0) {
            g.Return(12);
            return true;
        }
        g.R(4) = 0;
        g.Write8(player + kChipDeath, 0);
        g.Write8(player + kDead, 0);
        g.R(0) = 1;
        g.Return(16);
        return true;
    }

    // The round concluded, of the kind given, and the message asked for in a fight of people's (r4 the kind, the last
    // routine's leftovers in r6 and r7, as the game's code leaves them: r0 the Demo_Flag read, r3 its place or the
    // message's routine's)
    void Conclude(Guest &g, int kind) {
        g.R(4) = Signed(kind);
        if (kind == 1) {
            g.Write16(kConcluded, 1);
            g.Write16(kConclusion, 1);
            g.Call(kSettle);
            g.R(3) = kPeoplePlay;
        } else {
            g.Call(kSettle);
            g.Write16(kConcluded, 1);
            g.Write16(kConclusion, 0);
            g.R(1) = kPeoplePlay;
            g.R(2) = kConclusion;
            g.R(3) = kConcluded;
        }
        g.R(0) = Signed(Byte(g, kPeoplePlay));
        g.SetT(g.R(0) == 0);
        if (g.R(0) != 0) {
            if (kind == 1) {
                g.R(2) = kCentreMessage;
            } else {
                g.R(3) = kCentreMessage;
            }
            g.R(4) = Signed(kind);
            g.Call(kCentreMessage);
        }
    }

    // 06117D28 (settle_check): a knock-out, of one player (the other the winner) or of both. Undone for a player its
    // super art brings back (and looked at again); otherwise the round is concluded (but for Shin Akuma's finish:
    // above). Leaves the registers as the game's code: with nobody knocked out, r0 the dead flags' sum, r3 player 2's
    // flag twice, r4 kWinner's place, T clear; else those of the last routine it called.
    bool SettleCheck(Guest &g) {
        Frame frame(g, 24); // (r11-r14, pr, macl)
        for (;;) {
            g.R(4) = kWinner;
            const int32_t twice = static_cast<int32_t>(Byte(g, kTwo + kDead)) * 2;
            const int32_t dead = Byte(g, kOne + kDead) + twice;
            g.R(0) = Signed(dead);
            g.R(3) = Signed(twice);
            g.SetT(dead == 3);
            if (dead == 3) {
                // (both: unless one is brought back, a double knock-out)
                g.R(4) = kOne;
                g.Call(kBroughtBack);
                if (g.R(0) != 0) {
                    continue;
                }
                g.R(4) = kTwo;
                g.Call(kBroughtBack);
                if (g.R(0) != 0) {
                    continue;
                }
                Conclude(g, 1);
                g.Return(80);
                return true;
            }
            if (dead != 1 && dead != 2) {
                g.Return(20); // (nobody knocked out)
                return true;
            }
            const int32_t loser = dead == 1 ? 0 : 1;
            g.Write8(kWinner, static_cast<uint8_t>(1 - loser));
            g.Write8(kLoser, static_cast<uint8_t>(loser));
            g.R(4) = Player(Byte(g, kLoser));
            g.Call(kBroughtBack);
            if (g.R(0) != 0) {
                g.SetT(false);
                g.Return(40); // (brought back)
                return true;
            }
            g.Call(kGoukiWinsSet);
            g.Write16(kRoundResult, static_cast<uint16_t>(g.Read16(kRoundResult) | g.Read16(Player(Byte(g, kLoser)) + kKnockOutKind)));
            if ((g.Read16(kRoundResult) & kShinGoukiFinish) && g.Read8(kGoukiWins) != 0) {
                // (Shin Akuma's finish: the music fades, and the end is its own)
                g.R(2) = kShinGoukiMusic;
                g.R(3) = kMusicFade;
                g.Write8(kShinGoukiMusic, 1);
                g.R(4) = 150;
                g.Call(kMusicFade);
                g.R(4) = 4;
                g.Call(kSettle);
                g.Return(80);
                return true;
            }
            Conclude(g, 0);
            g.Return(80);
            return true;
        }
    }

    // --- the round's management ---
    constexpr uint32_t kBreakInto = 0x02011386;    // byte: a challenger breaking in (Break_Into)
    constexpr uint32_t kManageResult = 0x02017008; // byte: what the step says (MANAGE_X)
    constexpr uint32_t kManageStep = 0x020154A6;   // word: the management's step (C_No[0]; its low byte is sfiii3.h's
                                                   // kMatchState)
    constexpr uint32_t kManageSteps = 0x061944D4;  // twelve longs: the steps' routines (Management_Jmp_Tbl)
    constexpr uint32_t kMusicFading = 0x0609C960;  // BGM_Fade_Sub
    constexpr uint32_t kMusicControl = 0x0609CA5E; // BGM_Control
    constexpr uint32_t kNoBreakingIn = 0x0201138E; // byte: -1: no challenger may break in now (Forbid_Break)
    constexpr uint32_t kClockEnds = 0x060D7D04;    // the clock stopped at the round's end (count_end's)
    constexpr uint32_t kRoundCounts = 0x060998D8;  // the round's counts kept (losses, double knock-outs, time overs: by
                                                   // the conclusion and Play_Type)
    constexpr uint32_t kEndFrames = 0x02015434;    // word: frames the round's end has (900 from the conclusion)

    // 060992B0 (Game_Management): the round's management, a frame on: nothing while a challenger breaks in (r0 0);
    // else the step's routine (C_No[0]: 0 the round's set-up, 1 its start, 2 the fight, 3 the conclusion, ...), then
    // the music's fading and control; r0 what the step said (MANAGE_X). (The game copies the steps' table onto the
    // stack first: the frame holds it.)
    bool GameManagement(Guest &g) {
        Frame frame(g, 52); // (pr, and the table of twelve)
        if (g.Read8(kBreakInto) != 0) {
            g.R(0) = 0;
            g.Return(30);
            return true;
        }
        g.Write8(kManageResult, 0);
        g.Call(g.Read32(kManageSteps + Signed(Word(g, kManageStep)) * 4));
        g.Call(kMusicFading);
        g.Call(kMusicControl);
        g.R(0) = Signed(Byte(g, kManageResult));
        g.Return(40);
        return true;
    }

    // 0609989E (Game_Manage_3rd): the fight: in a fight of people's, once the round is concluded (above), the next step
    // (the conclusion's), no challenger may break in, the fighters may not fight, the clock stops, the round's counts
    // are kept and the end given its 900 frames. Leaves the registers as the game's code (it is called through the
    // table: its callers are not known): with nothing done, r2 and r3 the Demo_Flag's place and value, or r0 and r1
    // the Conclusion_Flag's; else r1 kEndFrames' place, r3 900, the rest as the counts leave them.
    bool ManageFight(Guest &g) {
        g.R(2) = kPeoplePlay;
        g.R(3) = Signed(Byte(g, kPeoplePlay));
        g.SetT(g.R(3) == 0);
        if (g.R(3) == 0) {
            g.Return(8);
            return true;
        }
        g.R(0) = kConcluded;
        g.R(1) = Signed(Word(g, kConcluded));
        g.SetT(g.R(1) == 0);
        if (g.R(1) == 0) {
            g.Return(10);
            return true;
        }
        Frame frame(g, 4); // (pr)
        g.Write16(kManageStep, static_cast<uint16_t>(Word(g, kManageStep) + 1));
        g.Write8(kNoBreakingIn, 0xFF);
        g.Write8(kFightOn, 0);
        g.R(0) = kFightOn; // (as the code leaves them for the clock's routine)
        g.R(1) = kNoBreakingIn;
        g.R(2) = 0;
        g.R(3) = kClockEnds;
        g.R(4) = kManageStep;
        g.Call(kClockEnds);
        g.Call(kRoundCounts);
        g.R(3) = 900;
        g.R(1) = kEndFrames;
        g.Write16(kEndFrames, 900);
        g.Return(40);
        return true;
    }

    // (free: what liveout.py found no caller reads; settle_check's and setup_settle_rno's leftovers reach further up
    // through tail jumps: all the game's)
    const Lift kLifts[] = {
        {"TimeControl", 0x06096CE8, 0x50, 0x181C44D88BE3D525ull, TimeControl, true, 0x1001F},
        {"TimeOverCheck", 0x06117C26, 0x78, 0xA480EB5B57B085EBull, TimeOverCheck, true, 0x100FE},
        {"WillDie", 0x06117C9E, 0x2E, 0xE522B23981A9207Dull, WillDie, true, 0x100FE},
        {"Settle", 0x06117CCC, 0x1A, 0x3F43FB65093FD49Aull, Settle, true, 0},
        {"SettleCheck", 0x06117D28, 0x13E, 0xBC1AF61E0640BFDBull, SettleCheck, true, 0x10005},
        {"BroughtBack", 0x06117E66, 0x26, 0x2B2F1953176FD4F7ull, BroughtBack, true, 0x1003E},
        {"GameManagement", 0x060992B0, 0x76, 0x5BBD0E3B334882AFull, GameManagement, true, 0x1003F},
        {"ManageFight", 0x0609989E, 0x3A, 0xE46A39945A5132C8ull, ManageFight, true, 0},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightRound() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
