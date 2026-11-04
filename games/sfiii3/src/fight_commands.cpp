// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's command reader, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h):
// how a fighter's lever and buttons, frame by frame, become its special moves, super arts, dashes, parries and the rest
// (CMD_MAIN). waza_check (fight_input.cpp) picks up the frame's input (sw_pick_up, 0x060B59BE, and pl_lvr_set,
// 0x060B55F8) and reads it for the commands (cmd_move, 0x060B2BCC).
//
// The input picked up. Into the player's lever record (t_pl_lvr, chk_pl: 68 bytes): the lever and buttons held (sw_new),
// last frame's (sw_old), what changed, what is newly pressed (sw_now), the lever alone (sw_lever), the lever's newly
// pressed ways (now_lvbt), the buttons pressed and let go (shot_up, shot_down); a count of the frames each of the ten
// (up, down, the two ways, the six buttons) and the four diagonals has been held. The lever's two ways are the other
// way round for a fighter facing back (rl_waza, or rl_flag on the car stage, check_rl_on_car); two or three punches (or
// kicks) pressed together this frame or the last stand for the strongest (bit 7, or bit 11, set). The player's command
// record (wcp, 0x406 bytes) gets its copy, the lever's way forward or back (lever_dir), three button pairs together
// (ca14, ca25, ca36), the lever and buttons mashed (lgp, from a table by how many of them changed).
//
// The commands. Each character has up to 56 (its table at 0x02006284 by player_number, a pointer each), one a slot: 0-1
// the dashes, 2 the high jump, 3-6 and 12 the parries' windows, 20 on the special moves, 38 on the super arts; a slot
// with its frames-left word (waza_flag) at -1 is not the character's. A command is a list of steps (12 words of its own
// first: the frames its flag lasts, the inputs that cancel it, ...; then a step: its type, its time, two counts and the
// lever or buttons it wants), its state in the player's command work (waza_work: 28 bytes a command, the step's type,
// time and counts, where the next step is, and the counts the step types keep). Each frame each command's step is moved
// on by the frame's input (chk_move_jp, by the step's type): met, the next is read (check_next) and, if not a "wait"
// (10), tried at once; the last met (the next type 28), the command is done (command_ok): its flag gets the frames it
// lasts (reset) and the player's waza_no its number, for the moves' code to see; a step's time run out, the command
// starts again (type 0: its first step read again). Then each command done counts its frames down (command_ok_move),
// to 0 at once for an input that cancels it (dead_lvr_check: w_dead, w_dead2).
//
// The step types (the PlayStation 2 decompilation's names check_0 ... check_26 are one below these numbers) that the
// characters' commands use:
//   0 Start: the command's first step read            1 LeverAtWay: the lever to a way (or any of a set, newly; or let
//   2 Charge: the lever held some frames, then moved      go) within the time
//   5 Mash: one strength's button pressed fast (the   6 Exactly: the lever and buttons pressed now those wanted
//     5th press; then a press within 9, 12 or 15      7 AllFourWays: up, down and both ways, each within 14 frames
//     frames keeps it going)                          10 LeverMoved: the lever moved to the way this frame (elsewhere,
//   11 Parry: the lever let go, then tapped to the       the command starts again)
//      way (the parries' windows)                     15 Presses: a punch (or kick) 3 times in the time
//   19 Renew: the lever to the way sets the flag       20 SetAndOn: the lever to the way sets the flag, and on
//      again and the time again                       21 nothing
//   22 LeverAtNew: 1, the way newly pressed            24 Tap: the lever let go, tapped to the way, let go again
//   25 LeverNewlyAt: 1 by the ways newly pressed       27 LetGo: the lever at the way, then away from it
// The table's others (3, 4, 8, 9, 12-14, 16-18, 23, 26: buttons held and let go, back then down, the air parry, the
// quick stand, the lever held, buttons together, the full circle) are in no character's commands: never met, they are
// left the game's code (called through the table, should one be).
//
// Registers: the step types' routines are called through the table (chk_move_jp, copied to RAM at 0x02004FB4) by
// cmd_move, which reads none of r0-r7 after; but the last one's r5-r7 are cmd_move's, waza_check's and the fighter's
// routine's (fight_input.cpp: PlayerMove), which calls its step's routine through a pointer: kept exact.
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    // --- the reader's ---
    constexpr uint32_t kPlayer = 0x02026B28;        // long: the fighter read (cmd_pl)
    constexpr uint32_t kId = 0x02026B24;            // word: its player, 0 or 1 (cmd_id)
    constexpr uint32_t kNumbers = 0x0202630C;       // words, a player each: the command being read (waza_type)
    constexpr uint32_t kData = 0x02026308;          // long: where its table is read (cmd_tbl_ptr)
    constexpr uint32_t kWork = 0x02026306;          // word (sw_work)
    constexpr uint32_t kLever = 0x02026310;         // long: the player's lever record (chk_pl)
    constexpr uint32_t kStep = 0x02026314;          // long: the command's work (waza_ptr)
    constexpr uint32_t kStates = 0x02026318;        // the players' command records (wcp), 0x406 bytes each
    constexpr uint32_t kStepsAll = 0x020256C4;      // the players' command work (waza_work), 0x620 bytes each
    constexpr uint32_t kRoutines = 0x02004FB4;      // longs: the step types' routines (chk_move_jp)
    constexpr uint32_t kTables = 0x02006284;        // longs: each character's command table (pl_cmd)
    constexpr uint32_t kDiagonals = 0x02006444;     // words: the four diagonals' bits (lvr_chk_tbl)
    constexpr uint32_t kMashed = 0x06199630;        // words: by how many of four bits are set (lever_gacha_tbl)
    constexpr uint32_t kOnCar = 0x0611C9FC;         // check_rl_on_car (r4 the fighter): r0 not 0 on the car stage's
    constexpr uint32_t kDone = 28;                  // a step's type: the command is done
    // ... in a fighter's record
    constexpr uint32_t kSide = 0x08;                // word (wu.id)
    constexpr uint32_t kFacing = 0x0A, kTurned = 0x0B; // bytes (rl_flag; rl_waza: its moves the other way round)
    constexpr uint32_t kY = 0x68;                   // word: its height (xyz[1])
    constexpr uint32_t kInput = 0x3B8;              // long: its command record (cp)
    constexpr uint32_t kCharacter = 0x3C0;          // word (player_number)
    // ... in a lever record (chk_pl)
    constexpr uint32_t kHeld = 0x00, kLastHeld = 0x02, kChanged = 0x04, kPressed = 0x06; // words (sw_new, sw_old,
                                                    // sw_chg, sw_now)
    constexpr uint32_t kLastPressed = 0x08;         // word (old_now)
    constexpr uint32_t kWaysNew = 0x0A, kWaysBefore = 0x0C, kWays = 0x0E; // words: the lever's ways newly pressed, last
                                                    // frame's and now's, the diagonals as theirs (now_lvbt, old_lvbt,
                                                    // new_lvbt)
    constexpr uint32_t kLeverNow = 0x10;            // word: the lever alone (sw_lever)
    constexpr uint32_t kButtonsDown = 0x12, kButtonsUp = 0x14, kButtonsChanged = 0x16; // words (shot_up, shot_down,
                                                    // shot_ud)
    constexpr uint32_t kCounts = 0x20;              // words: frames held: up, down, the two ways, the six buttons
                                                    // (up_cnt ... s6_cnt), then the diagonals
    constexpr uint32_t kWayCount = 0x24, kOtherWayCount = 0x26; // (left_cnt, right_cnt)
    constexpr uint32_t kMove = 0x3E;                // word: the command just done (waza_no)
    // ... in a command record (wcp)
    constexpr uint32_t kWcpLever = 0x00;            // word: the lever's ways and the buttons, from the controls (sw_lvbt)
    constexpr uint32_t kWcpHeld = 0x02, kWcpLastHeld = 0x04, kWcpPressed = 0x06, kWcpLetGo = 0x08, kWcpChanged = 0x0A;
    constexpr uint32_t kWcpLastPressed = 0x0C;      // word (old_now)
    constexpr uint32_t kWcpMashing = 0x0E;          // word (lgp)
    constexpr uint32_t kWcpTogether = 0x10;         // bytes, 3: light and medium... pressed together (ca14, ca25, ca36)
    constexpr uint32_t kWcpWayCount = 0x13, kWcpOtherWayCount = 0x14; // bytes (calf, calr)
    constexpr uint32_t kWcpLeverWay = 0x15;         // byte: 1 forward, 2 back, 0 neither (lever_dir)
    constexpr uint32_t kFlags = 0x16;               // words: each command's frames left (waza_flag), -1 not the
                                                    // character's
    constexpr uint32_t kLasts = 0x86;               // words: each command's frames it lasts (reset)
    // ... in a command's work (waza_work)
    constexpr uint32_t kType = 0x00, kTime = 0x02, kCount = 0x04, kWants = 0x06; // words (w_type, w_int, free1, w_lvr:
                                                    // bit 15 the lever exactly at its low four bits)
    constexpr uint32_t kNext = 0x08;                // long: the next step (w_ptr)
    constexpr uint32_t kCount2 = 0x0C;              // word (free2)
    constexpr uint32_t kCancel = 0x0E, kCancel2 = 0x10; // words: inputs that cancel it (w_dead, w_dead2)
    constexpr uint32_t kSeen = 0x12, kShots = 0x14, kLastShots = 0x16; // words (tame.flag, shot_flag, shot_flag2)
    constexpr uint32_t kCount3 = 0x18;              // word (free3)
    constexpr uint32_t kMet = 0x1A;                 // word (shot_ok)

    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }
    int16_t Id(Guest &g) {
        return Word(g, kId);
    }
    // The player's command record (the game's sum, kept to 16 bits)
    uint32_t State(Guest &g) {
        return kStates + Extended(Id(g) * 0x406);
    }
    // The command being read, and its flag's and frames' places
    int16_t Number(Guest &g) {
        return Word(g, kNumbers + Signed(Id(g)) * 2);
    }
    uint32_t FlagAt(Guest &g) {
        return State(g) + kFlags + Signed(Number(g)) * 2;
    }
    uint32_t LastsAt(Guest &g) {
        return State(g) + kLasts + Signed(Number(g)) * 2;
    }
    uint32_t StepAt(Guest &g) {
        return g.Read32(kStep);
    }
    uint32_t LeverAt(Guest &g) {
        return g.Read32(kLever);
    }
    void Add(Guest &g, uint32_t at, int value) {
        g.Write16(at, static_cast<uint16_t>(Word(g, at) + value));
    }
    // A word set and read back as the game's code does (sw_work); its value zero-extended
    uint32_t SetWork(Guest &g, uint32_t value) {
        g.Write16(kWork, static_cast<uint16_t>(value));
        return g.Read16(kWork);
    }

    void Step(Guest &g);

    // 060B550C (command_ok): the command done: its flag the frames it lasts and, but for a quick stand's (14), the step
    // type 0 (it starts again next frame) and the player's waza_no its number. Leaves r5 kId, r6 the number x 2, r7
    // kNumbers.
    void CommandDone(Guest &g) {
        const uint32_t number = Signed(Number(g));
        g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
        g.R(5) = kId;
        g.R(6) = number * 2;
        g.R(7) = kNumbers;
        const uint32_t step = StepAt(g);
        if (Word(g, step + kType) != 14) {
            g.Write16(step + kType, 0);
            g.Write16(LeverAt(g) + kMove, static_cast<uint16_t>(number));
        }
    }

    // 060B2F32 (check_next): the next step read (its type, time, two counts, what it wants) and, but for a wait (10),
    // tried at once
    void NextStep(Guest &g) {
        const uint32_t step = StepAt(g);
        uint32_t at = g.Read32(step + kNext);
        g.Write16(step + kType, g.Read16(at));
        g.Write16(step + kTime, g.Read16(at + 2));
        g.Write16(step + kCount, g.Read16(at + 4));
        g.Write16(step + kCount2, g.Read16(at + 4));
        g.Write16(step + kWants, g.Read16(at + 6));
        g.Write32(step + kNext, at + 8);
        if (Word(g, step + kType) != 10) {
            Step(g);
        }
    }

    // A step met: the command done if it was its last, else the next step
    void Met(Guest &g) {
        if (Word(g, g.Read32(StepAt(g) + kNext)) == kDone) {
            CommandDone(g);
        } else {
            NextStep(g);
        }
    }

    // 060B55A6 (dead_lvr_check): the input held this frame one that cancels the command: the step type 0. Leaves r5
    // the lever record.
    bool Cancelled(Guest &g) {
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        g.R(5) = lever;
        const uint32_t held = g.Read16(lever + kHeld);
        const int16_t cancel = Word(g, step + kCancel), cancel2 = Word(g, step + kCancel2);
        if ((cancel != 0 && Signed(cancel) == held) || (cancel2 != 0 && Signed(cancel2) == held)) {
            g.Write16(step + kType, 0);
            return true;
        }
        return false;
    }

    // A step's time counted down: run out, the command starts again (the step type 0); true while it lasts
    bool TimeGoes(Guest &g) {
        const uint32_t step = StepAt(g);
        Add(g, step + kTime, -1);
        if (Word(g, step + kTime) < 0) {
            g.Write16(step + kType, 0);
            return false;
        }
        return true;
    }

    // The command's first step read again from its table (cmd_tbl_ptr): after the 12 words of the command's own, a
    // step's (the second count from the first, or given)
    void FirstStep(Guest &g, uint32_t step, bool counts) {
        const uint32_t at = g.Read32(kData) + 24;
        g.Write16(step + kType, g.Read16(at));
        g.Write16(step + kTime, g.Read16(at + 2));
        if (counts) {
            g.Write16(step + kCount, g.Read16(at + 4));
        }
        g.Write16(step + kCount2, g.Read16(at + 4));
        g.Write16(step + kWants, g.Read16(at + 6));
        g.Write32(kData, at + 8);
        g.Write32(step + kNext, at + 8);
    }

    // 060B2EBC (check_init), type 0: the command's first step read, its counts cleared, and tried at once
    void Start(Guest &g) {
        const uint32_t step = StepAt(g);
        FirstStep(g, step, true);
        for (uint32_t at : {kSeen, kShots, kLastShots, kMet, kCount3}) {
            g.Write16(step + at, 0);
        }
        Step(g);
    }

    // The lever as a step wants it (types 1, 22, 24, 25): bit 15 set, exactly the way given (its low four bits; the
    // number kept in sw_work); 0, let go; else any of the ways given, if `anyNew` allows
    enum class Wanted { Not, Met };
    Wanted LeverWanted(Guest &g, uint32_t lever, bool anyNew) {
        const uint32_t step = StepAt(g);
        const int16_t wants = Word(g, step + kWants);
        if (wants & 0x8000) {
            g.R(5) = kWork;
            const uint32_t way = SetWork(g, wants & 15);
            return way == lever ? Wanted::Met : Wanted::Not;
        }
        if (wants == 0) {
            return lever == 0 ? Wanted::Met : Wanted::Not;
        }
        return anyNew && (Signed(wants) & lever) ? Wanted::Met : Wanted::Not;
    }

    // 060B2F76, type 1 (check_0): the lever at the way within the step's time (any of a set: newly pressed)
    void LeverAtWay(Guest &g) {
        (void)TimeGoes(g);
        const uint32_t lever = static_cast<uint16_t>(Word(g, LeverAt(g) + kLeverNow) & 15);
        if (Cancelled(g)) {
            return;
        }
        const bool newly = (g.Read16(LeverAt(g) + kWaysNew) & 15) != 0;
        if (LeverWanted(g, lever, newly) == Wanted::Met) {
            Met(g);
        }
    }

    // A count of frames to wait run out, or the step's time: the count again from the first, the time down (types 2
    // and 3)
    void Again(Guest &g, uint32_t step) {
        g.Write16(step + kCount2, g.Read16(step + kCount));
        Add(g, step + kTime, -1);
        if (Word(g, step + kTime) < 0) {
            g.Write16(step + kType, 0);
        }
    }

    // 060B3036, type 2 (check_1): a charge: the lever held at the way (exactly, bit 15; else any of them) for the count's
    // frames, then moved off it within the step's time. Leaves r5 0, r6 the ways wanted, r7 the lever record.
    void Charge(Guest &g) {
        if (Cancelled(g)) {
            return;
        }
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        const int16_t wants = Word(g, step + kWants);
        const uint32_t way = SetWork(g, wants & 15);
        g.R(5) = 0;
        g.R(6) = way;
        g.R(7) = lever;
        const uint32_t now = g.Read16(lever + kLeverNow);
        const bool exact = (wants & 0x8000) != 0;
        if (exact ? now == way : (now & way) != 0) {
            if (exact) {
                Add(g, step + kCount2, -1);
                if (Word(g, step + kSeen) == 0 && Word(g, step + kCount2) < 0) {
                    g.Write16(step + kSeen, 1);
                }
            } else if (Word(g, step + kSeen) == 0) {
                Add(g, step + kCount, -1);
                if (Word(g, step + kCount) < 0) {
                    g.Write16(step + kSeen, 1);
                }
            }
            return;
        }
        if (Word(g, step + kSeen) != 0) {
            g.Write16(step + kSeen, 0);
            Met(g);
            return;
        }
        Again(g, step);
    }

    // 060B33AA, type 5 (check_4): one strength's button pressed fast: the presses of each of the three counted in the
    // step's time (its second... the strongest first); the fifth (none of the command's frames left), or one more
    // while they last, does it (or keeps it going: its time 9, 12 or 15). Leaves r5 the player's waza_type's place, r6
    // 0, r7 kLever or the number x 2.
    void Mash(Guest &g) {
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        const uint32_t pressed = g.Read16(lever + kPressed);
        const bool punches = Word(g, step + kWants) == 16;
        const uint32_t first = punches ? 0x10 : 0x100;
        const uint32_t counts[3] = {kSeen, kShots, kLastShots};
        for (int n = 0; n < 3; n++) {
            if (pressed & (first << n)) {
                Add(g, step + counts[n], 1);
            }
        }
        Add(g, step + kTime, -1);
        g.R(6) = 0;
        if (Word(g, step + kTime) < 0) {
            for (uint32_t at : counts) {
                g.Write16(step + at, 0);
            }
            g.Write16(step + kTime, g.Read16(step + kCount));
        }
        g.R(5) = kNumbers + Signed(Id(g)) * 2;
        g.R(7) = kLever;
        // (the strongest first: its count, and the time it keeps the command going)
        const struct {
            uint32_t count;
            uint16_t time;
        } strengths[3] = {{kLastShots, 9}, {kShots, 12}, {kSeen, 15}};
        for (const auto &strength : strengths) {
            if (Word(g, FlagAt(g)) != 0) {
                if (Word(g, step + kTime) > 0 && Word(g, step + strength.count) != 0) {
                    g.R(7) = Signed(Number(g)) * 2;
                    g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
                    g.Write16(step + strength.count, 0);
                    g.Write16(step + kTime, strength.time);
                    return;
                }
            } else if (Word(g, step + strength.count) >= 5) {
                g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
                g.Write16(step + strength.count, 0);
                g.Write16(step + kTime, strength.time);
                g.Write16(LeverAt(g) + kMove, static_cast<uint16_t>(Number(g)));
                return;
            }
        }
    }

    // 060B361C, type 6 (check_5): the lever and buttons pressed this frame exactly those wanted, within the time
    void Exactly(Guest &g) {
        (void)TimeGoes(g);
        if (Cancelled(g)) {
            return;
        }
        if (Signed(Word(g, StepAt(g) + kWants)) == g.Read16(LeverAt(g) + kPressed)) {
            Met(g);
        }
    }

    // 060B3674, type 7 (check_6): the lever up, down and both ways, each within 14 frames of the last (the count), all
    // within the step's time (run out: the command's first step read again). Leaves r5 16, r6 14, r7 kLever.
    void AllFourWays(Guest &g) {
        const uint32_t step = StepAt(g);
        Add(g, step + kTime, -1);
        if (Word(g, step + kTime) < 0) {
            FirstStep(g, step, false);
            for (uint32_t at : {kSeen, kShots, kLastShots}) {
                g.Write16(step + at, 0);
            }
            g.Write16(step + kCount, 14);
            g.Write16(step + kMet, 0);
        } else {
            Add(g, step + kCount, -1);
            if (Word(g, step + kCount) <= 0) {
                g.Write16(step + kCount, 14);
                g.Write16(step + kMet, 0);
            }
        }
        for (uint32_t way = 1; way <= 8; way <<= 1) {
            if (g.Read16(LeverAt(g) + kLeverNow) == way) {
                g.Write16(step + kMet, static_cast<uint16_t>(g.Read16(step + kMet) | way));
                g.Write16(step + kCount, 14);
            }
        }
        g.R(5) = 16;
        g.R(6) = 14;
        g.R(7) = kLever;
        if (Word(g, step + kMet) == 15) {
            if (Word(g, g.Read32(step + kNext)) == kDone) {
                CommandDone(g);
                return;
            }
            g.Write16(step + kMet, 0);
            NextStep(g);
        }
    }

    // 060B39CA, type 10 (check_9): the lever moved this frame to the way (exactly, bit 15; let go, 0; or any of them);
    // moved elsewhere, the command starts again. Leaves r5 kLever, r6 15, r7 0.
    void LeverMoved(Guest &g) {
        (void)TimeGoes(g);
        g.R(5) = kLever;
        g.R(6) = 15;
        g.R(7) = 0;
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        const int16_t wants = Word(g, step + kWants);
        const bool moved = (g.Read16(lever + kWaysBefore) & 15) != (g.Read16(lever + kWays) & 15);
        const auto again = [&] { g.Write16(step + kType, 0); };
        if (wants & 0x8000) {
            const int16_t way = static_cast<int16_t>(SetWork(g, wants & 15));
            if (!moved) {
                return;
            }
            if (Word(g, lever + kLeverNow) == way) {
                Met(g);
            } else {
                again();
            }
        } else if (wants == 0) {
            if (Word(g, lever + kWays) == 0) {
                Met(g);
            } else if (moved) {
                again();
            }
        } else if (moved) {
            if (g.Read16(lever + kLeverNow) & Signed(wants)) {
                Met(g);
            } else {
                again();
            }
        }
    }

    // 060B3B30, type 11 (check_10): a parry: the lever let go (step 0), then tapped to the way (1: its window open, the
    // flag its frames, the count 10 more; the other parries' windows shorter closed; on the ground, the air's two (5, 6) only in the air and each closed
    // while the other's count lasts), held no more than 5 frames (2:
    // let go, the window stays; held, or the lever back, it closes), then nothing else pressed (3: the lever newly
    // pressed elsewhere, back, or the same way again, closes it) while the count lasts (4). Leaves r4 kId, r5
    // kStates, r6 0, r7 kStep, the player's command work, or its flags' place.
    void Parry(Guest &g) {
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        g.R(4) = kId;
        g.R(5) = kStates;
        g.R(6) = 0;
        g.R(7) = kStep;
        const auto close = [&] { g.Write16(FlagAt(g), 0); };
        switch (Word(g, step + kMet)) {
        case 0:
            if (Word(g, lever + kLeverNow) == 0) {
                Add(g, step + kMet, 1);
            }
            return;
        case 1: {
            const int16_t height = Word(g, g.Read32(kPlayer) + kY);
            if (height <= 0 && (Number(g) == 5 || Number(g) == 6)) {
                return;
            }
            if ((g.Read16(lever + kWaysNew) & 15) == 0) {
                return;
            }
            if (g.Read16(lever + kLeverNow) != Signed(Word(g, step + kWants))) {
                g.Write16(step + kMet, 0);
                return;
            }
            Add(g, step + kMet, 1);
            g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
            g.Write16(step + kCount3, static_cast<uint16_t>(Word(g, LastsAt(g)) + 10));
            g.Write16(step + kTime, 6);
            g.R(7) = kStepsAll;
                    // (the other windows: those shorter than this one's closed)
            const uint32_t flags = State(g) + kFlags;
            static const int kParries[] = {3, 4, 5, 6, 12};
            const int16_t number = Number(g);
            bool listed = false;
            for (int parry : kParries) {
                listed |= parry == number;
            }
            if (!listed) {
                return;
            }
            if (number != 5 && number != 6) {
                g.R(7) = flags;
            }
            for (int other : kParries) {
                if (other != number && Word(g, flags + number * 2) > Word(g, flags + other * 2)) {
                    g.Write16(flags + other * 2, 0);
                }
            }
            if (number == 5 || number == 6) {
                const uint32_t mine = kStepsAll + Extended(Id(g) * 0x620);
                if (Word(g, mine + (number == 5 ? 6 : 5) * 28 + kCount3) > 0) {
                    g.Write16(flags + number * 2, 0);
                }
            }
            return;
        }
        case 2:
            Add(g, step + kTime, -1);
            Add(g, step + kCount3, -1);
            if (Word(g, step + kTime) > 0) {
                if (Word(g, lever + kLeverNow) == 0) {
                    Add(g, step + kMet, 1);
                } else if (g.Read16(lever + kLeverNow) & 8) {
                    close();
                    Add(g, step + kMet, 1);
                } else if (g.Read16(lever + kLeverNow) != Signed(Word(g, step + kWants))) {
                    close();
                    Add(g, step + kMet, 1);
                }
            } else {
                close();
                Add(g, step + kMet, 1);
            }
            return;
        case 3: {
            Add(g, step + kCount3, -1);
            if (Word(g, step + kCount3) < 0) {
                g.Write16(step + kType, 0);
                return;
            }
            const uint32_t pressed = g.Read16(lever + kPressed);
            if ((pressed & 8) || pressed == Signed(Word(g, step + kWants))) {
                close();
            } else if (pressed & 15) {
                Add(g, step + kMet, 1);
                close();
            }
            return;
        }
        case 4:
            Add(g, step + kCount3, -1);
            if (Word(g, step + kCount3) < 0) {
                g.Write16(step + kType, 0);
            }
            return;
        }
    }

    // 060B483A, type 15 (check_14): a punch (wanted 0x10) or kick pressed three times in the count's frames: the
    // command done (its time 10 for an odd number, else 6); while its frames last, one more press in its time keeps it
    // going. Leaves r5 0, r6 the player's waza_type's place, r7 kNumbers.
    void Presses(Guest &g) {
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        Add(g, step + kTime, -1);
        const uint32_t pressed = g.Read16(lever + kPressed);
        if (pressed & (Word(g, step + kWants) == 16 ? 0x70 : 0x700)) {
            Add(g, step + kSeen, 1);
        }
        g.R(5) = 0;
        g.R(6) = kNumbers + Signed(Id(g)) * 2;
        g.R(7) = kNumbers;
        const uint16_t time = (Number(g) & 1) ? 10 : 6;
        const auto again = [&] {
            g.Write16(step + kSeen, 0);
            g.Write16(step + kTime, g.Read16(step + kCount));
        };
        if (Word(g, FlagAt(g)) != 0) {
            if (Word(g, step + kTime) > 0) {
                return;
            }
            if (Word(g, step + kSeen) != 0) {
                g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
                g.Write16(step + kSeen, 0);
                g.Write16(step + kTime, time);
                return;
            }
            again();
            return;
        }
        if (Word(g, step + kSeen) >= 3) {
            g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
            g.Write16(step + kSeen, 0);
            g.Write16(step + kTime, time);
            g.Write16(LeverAt(g) + kMove, static_cast<uint16_t>(Number(g)));
            return;
        }
        if (Word(g, step + kTime) < 0) {
            again();
        }
    }

    // Whether the lever is at the way a step wants (types 19, 20): exactly (bit 15: a number in sw_work), let go (0), or
    // any of them; the ways' change (`changed`, else the newly pressed) needed for the first and last. Sets r7 as the
    // game's code: 15 (or the lever), kWork, and the flag's place by the form when met.
    bool LeverTo(Guest &g, uint32_t lever, uint32_t now, bool changed, uint32_t notMet7) {
        const uint32_t step = StepAt(g), at = LeverAt(g);
        const int16_t wants = Word(g, step + kWants);
        const bool newly = changed ? (g.Read16(at + kWaysBefore) & 15) != (g.Read16(at + kWays) & 15)
                                   : (g.Read16(at + kWaysNew) & 15) != 0;
        g.R(7) = notMet7;
        if (wants & 0x8000) {
            if (!newly) {
                return false;
            }
            if (changed) {
                g.R(7) = kWork;
            }
            if (SetWork(g, wants & 15) != lever) {
                return false;
            }
            g.R(7) = Signed(Number(g)) * 2;
            return true;
        }
        if (wants == 0) {
            if (now != 0) {
                return false;
            }
        } else if (!newly || !(Signed(wants) & lever)) {
            return false;
        }
        g.R(7) = State(g);
        if (!changed && wants == 0) {
            g.R(7) = Signed(Number(g)) * 2;
        }
        return true;
    }

    // 060B4BBA, type 19 (check_18): the lever moved to the way: the command's flag its frames, and the step's time the
    // count again. Leaves r5 kNumbers, r6 kStates, r7 (LeverTo).
    void Renew(Guest &g) {
        if (!TimeGoes(g)) {
            return;
        }
        const uint32_t lever = static_cast<uint16_t>(Word(g, LeverAt(g) + kLeverNow) & 15);
        if (Cancelled(g)) {
            return;
        }
        g.R(5) = kNumbers;
        g.R(6) = kStates;
        if (LeverTo(g, lever, g.Read16(LeverAt(g) + kLeverNow), true, 15)) {
            const uint32_t step = StepAt(g);
            g.Write16(step + kTime, g.Read16(step + kCount));
            g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
        }
    }

    // 060B4D2C, type 20 (check_19): the lever newly to the way: the command's flag its frames, and on to the next step.
    // Leaves r5 kStates, r6 kNumbers, r7 (LeverTo).
    void SetAndOn(Guest &g) {
        (void)TimeGoes(g);
        const uint32_t lever = static_cast<uint16_t>(Word(g, LeverAt(g) + kLeverNow) & 15);
        if (Cancelled(g)) {
            return;
        }
        g.R(5) = kStates;
        g.R(6) = kNumbers;
        if (LeverTo(g, lever, g.Read16(LeverAt(g) + kLeverNow), false, lever)) {
            g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
            NextStep(g);
        }
    }

    // 060B4E5A, type 21 (check_20): nothing
    void Nothing(Guest &) {}

    // 060B4E84, type 22 (check_21): the lever at the way (exactly, bit 15, newly pressed unless the way is 0, let go;
    // let go, 0; or any of them, newly) within the time. Leaves r5 the lever record or the way, r6 15, r7 kWork (bit
    // 15).
    void LeverAtNew(Guest &g) {
        (void)TimeGoes(g);
        const uint32_t lever = static_cast<uint16_t>(Word(g, LeverAt(g) + kLeverNow) & 15);
        if (Cancelled(g)) {
            return;
        }
        g.R(6) = 15;
        const uint32_t step = StepAt(g);
        const int16_t wants = Word(g, step + kWants);
        const bool newly = (g.Read16(LeverAt(g) + kWaysNew) & 15) != 0;
        bool met;
        if (wants & 0x8000) {
            g.R(7) = kWork;
            const uint32_t way = SetWork(g, wants & 15);
            g.R(5) = way;
            met = way == 0 ? lever == 0 : newly && way == lever;
        } else if (wants == 0) {
            met = lever == 0;
        } else {
            met = newly && (Signed(wants) & lever) != 0;
        }
        if (met) {
            Met(g);
        }
    }

    // 060B510C, type 24 (check_23): the lever let go (0), then moved to the way (1: the flag its frames, the third
    // count 3 more), let go within 5 frames (2: held elsewhere, the command starts again; back, or held on, the flag
    // cleared), then nothing newly pressed while the count lasts (3: back, the same way or another, the flag cleared;
    // another, the command starts again). Leaves r5 kStep, r6 0 or the player's command record, r7 kStates.
    void Tap(Guest &g) {
        const uint32_t step = StepAt(g), lever = LeverAt(g);
        g.R(5) = kStep;
        g.R(6) = 0;
        g.R(7) = kStates;
        const auto close = [&] { g.Write16(FlagAt(g), 0); };
        switch (Word(g, step + kMet)) {
        case 0:
            if (Word(g, lever + kLeverNow) == 0) {
                Add(g, step + kMet, 1);
            }
            return;
        case 1:
            if ((g.Read16(lever + kWaysBefore) & 15) == (g.Read16(lever + kWays) & 15)) {
                return;
            }
            if (g.Read16(lever + kLeverNow) != Signed(Word(g, step + kWants))) {
                return;
            }
            Add(g, step + kMet, 1);
            g.R(6) = State(g);
            g.Write16(FlagAt(g), g.Read16(LastsAt(g)));
            g.Write16(step + kCount3, static_cast<uint16_t>(Word(g, LastsAt(g)) + 3));
            g.Write16(step + kTime, 6);
            return;
        case 2:
            Add(g, step + kTime, -1);
            Add(g, step + kCount3, -1);
            if (Word(g, step + kTime) > 0) {
                if (Word(g, lever + kLeverNow) == 0) {
                    Add(g, step + kMet, 1);
                } else if (g.Read16(lever + kLeverNow) & 8) {
                    close();
                    Add(g, step + kMet, 1);
                } else if (g.Read16(lever + kLeverNow) != Signed(Word(g, step + kWants))) {
                    close();
                    g.Write16(step + kType, 0);
                }
            } else {
                close();
                Add(g, step + kMet, 1);
            }
            return;
        case 3: {
            Add(g, step + kCount3, -1);
            if (Word(g, step + kCount3) < 0) {
                g.Write16(step + kType, 0);
                return;
            }
            const uint32_t pressed = g.Read16(lever + kPressed);
            if ((pressed & 8) || pressed == Signed(Word(g, step + kWants))) {
                close();
            } else if (pressed & 15) {
                close();
                g.Write16(step + kType, 0);
            }
            return;
        }
        }
    }

    // 060B531C and 060B53D2, types 25 and 26 (check_24, check_25): the lever at the way (exactly, bit 15; let go, 0;
    // or any of them), by the ways newly pressed (25) or the lever as it is, within the time. Leaves r5 the lever
    // record or kWork (bit 15).
    void LeverIs(Guest &g, uint32_t field) {
        (void)TimeGoes(g);
        const uint32_t lever = static_cast<uint16_t>(Word(g, LeverAt(g) + field) & 15);
        if (Cancelled(g)) {
            return;
        }
        if (LeverWanted(g, lever, true) == Wanted::Met) {
            Met(g);
        }
    }
    void LeverNewlyAt(Guest &g) {
        LeverIs(g, kWaysNew);
    }

    // 060B5488, type 27 (check_26): the lever pressed to the way (seen), then neither pressed to it nor at it. Leaves
    // r5 the way, r6 kWork.
    void LetGo(Guest &g) {
        const uint32_t lever = LeverAt(g);
        const uint32_t pressed = static_cast<uint16_t>(Word(g, lever + kPressed) & 15);
        const uint32_t now = static_cast<uint16_t>(Word(g, lever + kLeverNow) & 15);
        if (Cancelled(g)) {
            return;
        }
        g.R(6) = kWork;
        const uint32_t step = StepAt(g);
        const uint32_t way = SetWork(g, Word(g, step + kWants) & 15);
        g.R(5) = way;
        if (pressed == way) {
            g.Write16(step + kSeen, 1);
            return;
        }
        if (now != way && Word(g, step + kSeen) != 0) {
            Met(g);
        }
    }

    // The step types' routines, by type (chk_move_jp)
    struct StepType {
        uint32_t address;
        void (*fn)(Guest &);
    };
    const StepType kStepTypes[] = {
        {0x060B2EBC, Start}, {0x060B2F76, LeverAtWay}, {0x060B3036, Charge}, {0, nullptr}, {0, nullptr},
        {0x060B33AA, Mash}, {0x060B361C, Exactly}, {0x060B3674, AllFourWays}, {0, nullptr}, {0, nullptr},
        {0x060B39CA, LeverMoved}, {0x060B3B30, Parry}, {0, nullptr}, {0, nullptr}, {0, nullptr},
        {0x060B483A, Presses}, {0, nullptr}, {0, nullptr}, {0, nullptr}, {0x060B4BBA, Renew},
        {0x060B4D2C, SetAndOn}, {0x060B4E5A, Nothing}, {0x060B4E84, LeverAtNew}, {0, nullptr}, {0x060B510C, Tap},
        {0x060B531C, LeverNewlyAt}, {0, nullptr}, {0x060B5488, LetGo},
    };

    // The command's step moved on by its type's routine (through the table, as the game's code: one not this file's
    // called)
    void Step(Guest &g) {
        const int16_t type = Word(g, StepAt(g) + kType);
        const uint32_t routine = g.Read32(kRoutines + Signed(type) * 4);
        if (type >= 0 && static_cast<size_t>(type) < std::size(kStepTypes) && kStepTypes[type].address == routine &&
            kStepTypes[type].fn != nullptr) {
            kStepTypes[type].fn(g);
        } else {
            g.Call(routine);
        }
    }

    // 060B5558 (command_ok_move): a command done counts its frames down (r4 its number): to 0 at once for an input that
    // cancels it. Leaves r0 the player's command record's offset, r5 the lever record.
    void CountDown(Guest &g, uint32_t number) {
        const bool cancelled = Cancelled(g);
        const uint32_t offset = Extended(Id(g) * 0x406);
        const uint32_t flag = kStates + kFlags + offset + Extended(static_cast<int32_t>(number)) * 2;
        g.Write16(flag, cancelled ? 0 : static_cast<uint16_t>(Word(g, flag) - 1));
        g.R(0) = offset;
    }

    // 060B2BCC (cmd_move): every command of the fighter's moved on by the frame's input, then those done counted down
    void CommandsRead(Guest &g) {
        Frame frame(g, 44); // (r8-r14, pr, macl, a word)
        const uint32_t fighter = g.Read32(kPlayer);
        g.Write16(kId, g.Read16(fighter + kSide));
        const uint32_t table = g.Read32(kTables + Signed(Word(g, fighter + kCharacter)) * 4);
        for (uint32_t number = 0; number < 56; number++) {
            const uint32_t flags = State(g) + kFlags;
            if (Word(g, flags + number * 2) == -1) {
                continue;
            }
            g.Write16(kNumbers + Signed(Id(g)) * 2, static_cast<uint16_t>(number));
            g.Write32(kData, g.Read32(table + number * 4));
            g.Write32(kStep, kStepsAll + Extended(Id(g) * 0x620) + number * 28);
            Step(g);
        }
        for (uint32_t number = 0; number < 56; number++) {
            const int16_t flag = Word(g, State(g) + kFlags + number * 2);
            if (flag == -1 || flag == 0) {
                continue;
            }
            g.Write32(kStep, kStepsAll + Extended(Id(g) * 0x620) + number * 28);
            CountDown(g, number);
        }
    }

    // 060B55F8 (pl_lvr_set): the frame's lever and buttons (the command record's sw_lvbt) into the lever record: the
    // two ways swapped for a fighter whose moves go the other way (rl_waza; on the car stage, rl_flag); two or three
    // punches (kicks) newly together, or let go together, the strongest (bit 7, or 11) held too; what is held, newly
    // pressed, let go, changed; the button pairs together this frame or the last; the mashing.
    void LeverSet(Guest &g) {
        const uint32_t state = State(g), fighter = g.Read32(kPlayer);
        uint16_t held = g.Read16(state + kWcpLever);
        const uint16_t ways = held & 12;
        {
            Frame frame(g, 32); // (r10-r14, pr, macl, a word)
            g.R(4) = fighter;
            g.Call(kOnCar);
        }
        const bool turned = g.R(0) != 0 ? g.Read8(fighter + kFacing) != 0 : g.Read8(fighter + kTurned) != 0;
        if (turned && ways != 0) {
            held = static_cast<uint16_t>((held & 0x0FF3) | (ways ^ 12));
        }
        const uint32_t lever = LeverAt(g);
        g.Write16(state + kWcpLastPressed, g.Read16(lever + kPressed));
        g.Write16(lever + kLastPressed, g.Read16(lever + kPressed));
        g.Write16(lever + kWaysBefore, g.Read16(lever + kWays));
        const uint16_t newly = g.Read16(state + kWcpLever) & ~g.Read16(lever + kWaysBefore);
        const uint16_t letGo = g.Read16(lever + kHeld) & ~held;
        const auto two = [](uint16_t buttons) { return buttons == 3 || buttons == 5 || buttons == 6 || buttons == 7; };
        for (const uint16_t shift : {4, 8}) {
            const uint16_t strongest = static_cast<uint16_t>(0x08 << shift);
            if (two((newly >> shift) & 15) || two((letGo >> shift) & 15)) {
                g.Write16(state + kWcpLever, g.Read16(state + kWcpLever) | strongest);
                held |= strongest;
            } else {
                g.Write16(state + kWcpLever, g.Read16(state + kWcpLever) & ~strongest);
                held &= ~strongest;
            }
        }
        g.Write16(lever + kWays, g.Read16(state + kWcpLever));
        g.Write16(lever + kLastHeld, g.Read16(lever + kHeld));
        g.Write16(lever + kHeld, held);
        const uint16_t last = g.Read16(lever + kLastHeld);
        g.Write16(lever + kPressed, held & ~last);
        g.Write16(lever + kWaysNew, g.Read16(state + kWcpLever) & ~g.Read16(lever + kWaysBefore));
        g.Write16(lever + kChanged, g.Read16(lever + kPressed) | (last & ~held));
        g.Write16(lever + kLeverNow, held & 15);
        g.Write16(lever + kButtonsDown, g.Read16(lever + kPressed) & 0x770);
        g.Write16(lever + kButtonsUp, last & ~held & 0x770);
        g.Write16(lever + kButtonsChanged, g.Read16(lever + kButtonsUp) | g.Read16(lever + kButtonsDown));
        const uint16_t pairs = g.Read16(state + kWcpLastPressed) | g.Read16(lever + kPressed);
        const uint16_t together[3] = {0x110, 0x220, 0x440};
        for (int n = 0; n < 3; n++) {
            g.Write8(state + kWcpTogether + n, (pairs & together[n]) == together[n] ? 1 : 0);
        }
        const uint32_t input = g.Read32(fighter + kInput);
        const auto mashed = [&](uint32_t bits) { return Word(g, kMashed + bits * 2); };
        const uint16_t pressed = g.Read16(input + kWcpPressed);
        uint16_t mash = static_cast<uint16_t>(mashed(pressed & 15) << 2);
        mash = static_cast<uint16_t>(mash + (mashed(g.Read16(input + kWcpLetGo) & 15) << 1));
        mash = static_cast<uint16_t>(mash + (mashed((pressed >> 4) & 7) << 1));
        mash = static_cast<uint16_t>(mash + mashed((pressed >> 8) & 7));
        g.Write16(state + kWcpMashing, mash);
    }

    // 060B59BE (sw_pick_up): the frame's input picked up (pl_lvr_set), the frames each of the lever's ways, the
    // buttons and the diagonals held counted, the command record's copy of it, the lever's way (forward or back by
    // the facing), each way held but not long. Leaves r4 kStates, r5 0, r6 kId, r7 kLever.
    void PickUp(Guest &g) {
        {
            Frame frame(g, 24); // (r12-r14, pr, macl, a word)
            LeverSet(g);
        }
        const uint32_t lever = LeverAt(g);
        const uint32_t held = g.Read16(lever + kHeld);
        for (uint32_t n = 0; n < 10; n++) {
            const uint32_t at = lever + kCounts + n * 2;
            g.Write16(at, (held & (1u << n)) ? static_cast<uint16_t>(g.Read16(at) + 1) : 0);
        }
        g.Write16(kWork, 0x400);
        for (uint32_t n = 0; n < 4; n++) {
            const uint32_t at = lever + kCounts + 20 + n * 2;
            g.Write16(at, (held & Signed(Word(g, kDiagonals + n * 2))) ? static_cast<uint16_t>(g.Read16(at) + 1) : 0);
        }
        const uint32_t state = State(g);
        g.Write16(state + kWcpHeld, g.Read16(lever + kHeld));
        g.Write16(state + kWcpLastHeld, g.Read16(lever + kLastHeld));
        g.Write16(state + kWcpChanged, g.Read16(lever + kChanged));
        g.Write16(state + kWcpPressed, g.Read16(lever + kPressed));
        g.Write16(state + kWcpLetGo, g.Read16(lever + kButtonsUp));
        const uint32_t ways = g.Read16(state + kWcpLever) & 12;
        uint8_t way = 0;
        if (ways != 0) {
            const bool right = g.Read8(g.Read32(kPlayer) + kFacing) != 0;
            way = (ways & (right ? 8 : 4)) ? 1 : 2;
        }
        g.Write8(state + kWcpLeverWay, way);
        const int16_t left = Word(g, lever + kWayCount), rightCount = Word(g, lever + kOtherWayCount);
        g.Write8(state + kWcpWayCount, left != 0 && left < 12 ? 1 : 0);
        g.Write8(state + kWcpOtherWayCount, rightCount != 0 && rightCount < 12 ? 1 : 0);
        g.R(4) = kStates;
        g.R(5) = 0;
        g.R(6) = kId;
        g.R(7) = kLever;
    }

    // --- the lifts ---
    template <void (*fn)(Guest &)> bool Lifted(Guest &g) {
        fn(g);
        g.Return(100);
        return true;
    }
    bool CountDownLifted(Guest &g) {
        CountDown(g, g.R(4));
        g.Return(60);
        return true;
    }

    // (free: r0-r4 and T, which no caller reads; r5-r7 reach the fighter's routine: above)
    constexpr uint32_t kFree = 0x1001F;
    const Lift kLifts[] = {
        {"CommandsRead", 0x060B2BCC, 0x2F0, 0xA8CB636E96B42FB6ull, Lifted<CommandsRead>, true, kFree},
        {"PickUp", 0x060B59BE, 0x2E0, 0xDAA10D8767B179FAull, Lifted<PickUp>, true, kFree},
        {"LeverSet", 0x060B55F8, 0x3C6, 0x14D74C5F1B5C53F5ull, Lifted<LeverSet>, true, 0x170FF},
        {"CountDown", 0x060B5558, 0x4E, 0xE8254E4711D1230Bull, CountDownLifted, true, 0x103FE},
        {"StepStart", 0x060B2EBC, 0x76, 0xCE1761450BF56D10ull, Lifted<Start>, true, kFree},
        {"StepLeverAt", 0x060B2F76, 0xC0, 0x0FBDDEBF17A2A3C2ull, Lifted<LeverAtWay>, true, kFree},
        {"StepCharge", 0x060B3036, 0x11E, 0x1A5AD8827879327Cull, Lifted<Charge>, true, kFree},
        {"StepMash", 0x060B33AA, 0x272, 0xB8D81988166070C7ull, Lifted<Mash>, true, kFree},
        {"StepExactly", 0x060B361C, 0x58, 0x96496EA98ACA4AA0ull, Lifted<Exactly>, true, kFree},
        {"StepAllFourWays", 0x060B3674, 0x14A, 0x47884C09DE3F9A47ull, Lifted<AllFourWays>, true, kFree},
        {"StepLeverMoved", 0x060B39CA, 0x118, 0x65D3465D637B84CEull, Lifted<LeverMoved>, true, kFree},
        {"StepParry", 0x060B3B30, 0x632, 0xB643C556C1A53696ull, Lifted<Parry>, true, kFree},
        {"StepPresses", 0x060B483A, 0x1C4, 0xE7BBB694715726C0ull, Lifted<Presses>, true, kFree},
        {"StepRenew", 0x060B4BBA, 0x172, 0xA2827C31250D2663ull, Lifted<Renew>, true, kFree},
        {"StepSetAndOn", 0x060B4D2C, 0x12E, 0xCD7F5D6443D63EFCull, Lifted<SetAndOn>, true, kFree},
        {"StepLeverAtNew", 0x060B4E84, 0x110, 0x88FBBF93ACD1AEBBull, Lifted<LeverAtNew>, true, kFree},
        {"StepTap", 0x060B510C, 0x210, 0x022AA403D6562945ull, Lifted<Tap>, true, kFree},
        {"StepLeverNewlyAt", 0x060B531C, 0xB6, 0x361EC6E1D2C1ED2Dull, Lifted<LeverNewlyAt>, true, kFree},
        {"StepLetGo", 0x060B5488, 0x6E, 0xF2FDBC256AFFB393ull, Lifted<LetGo>, true, kFree},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightCommands() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
