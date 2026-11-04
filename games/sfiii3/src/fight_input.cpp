// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's players' input, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h):
// where a fighter's lever and buttons come from each frame (0x06092294, 0x060B6242).
//
// How a fighter gets its input. Once a frame the fighter's own routine (0x061195E0, r4 its record, r5 the person's
// lever and buttons) puts a word in the place its record names (+0x3B8): a person's controls as they are (bits 0-3
// the lever: up 1, down 2, one way 4, the other 8; then the three punches, the three kicks); the computer's choice
// (CPU_Sub, computer_entry.cpp's ComputerPlays) put in the same form (below); or, while attract mode's demonstration
// fights play (a byte at 0x02016B6C 0xFF), the next word of the fight's recording (below), put in that form too.
//
// The fighter's frame (Player_move, 0x061195E0) then: the input taken from a fighter being knocked out, held by the
// game (wkey_flag) or changing form (metamor_over, Gill's); its last frame's steps and place kept (old_rno, old_pos)
// and its last moves' (get_saikinnno_idouryou), its guard, its health; the lever and buttons read for its moves
// (waza_check: the commands), or let go by while a super art's freeze holds it (key_thru); the lever's records in its
// command work; then the routine for its step (plmain_lv_00, by routine_no[0]: on its way in, fighting, its end).
//
// The demonstration's recordings (0x0617AF14 on, by the demonstration's number at 0x02016B72: 0x1C20 bytes a side) are
// set up by 0x060922C8, which also sets 0x02016B6C to 0xFF; but nothing in the arcade's program calls it (no call, no
// jump, no pointer to it): attract mode's fights are the computer's against the computer's, and the recordings are
// never played. checkfight.sh plays them (setting the flag and the places as 0x060922C8 would) to check the rewrites.
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }

    constexpr uint32_t kRecordings = 0x02017020; // longs, a side each: where the demonstration's recording has got to
    constexpr uint32_t kRecordingPlays = 0x02016B6C; // byte: 0xFF while a demonstration's recording plays

    // ... in a fighter's record
    constexpr uint32_t kSide = 0x08;          // word (wu.id)
    constexpr uint32_t kSteps = 0x24;         // words, 8 (wu.routine_no)
    constexpr uint32_t kLastSteps = 0x34;     // words, 8 (wu.old_rno)
    constexpr uint32_t kPlace = 0x64;         // longs, 3: across, up, in (wu.xyz: their whole parts in the high words)
    constexpr uint32_t kLastPlace = 0x70;     // words, 3 (wu.old_pos)
    constexpr uint32_t kHealth = 0x9E, kLastHealth = 0xA0; // words (wu.vital_new, vital_old)
    constexpr uint32_t kCommandWork = 0x17E;  // words: the lever's last press, their sum, the buttons new, held
                                              // (wu.cmwk[10] ... [13])
    constexpr uint32_t kRenewAttack = 0x306;  // word (wu.renew_attack)
    constexpr uint32_t kControls = 0x3B8;     // long: where its lever and buttons go (cp; its word 0 sw_lvbt, 2
                                              // sw_new, 6 sw_now, 14 lgp: the lever's last press)
    constexpr uint32_t kLastGuard = 0x3D1, kGuard = 0x3D2; // bytes (old_gdflag, guard_flag)
    constexpr uint32_t kHeld = 0x3FC;         // byte: its controls held by the game (wkey_flag)
    constexpr uint32_t kDead = 0x3FD;         // byte (dead_flag)
    constexpr uint32_t kSuperFreeze = 0x41C;  // byte: 1 while a super art's freeze holds it (sa_stop_flag)
    constexpr uint32_t kFacingKept = 0x437;   // byte: it may not turn (hurimukenai_flag)
    constexpr uint32_t kChanging = 0x474;     // byte (metamor_over)
    // ... and the routines called
    constexpr uint32_t kComputer = 0x06006040;    // the computer's choice (CPU_Sub; computer_entry.cpp)
    constexpr uint32_t kRecorded = 0x06092294, kAsPlayers = 0x060B6242; // (below)
    constexpr uint32_t kLastMoves = 0x0611A4D2;   // get_saikinnno_idouryou (r4 the record)
    constexpr uint32_t kCommands = 0x060B2934;    // waza_check (r4)
    constexpr uint32_t kLetGo = 0x060B2960;       // key_thru (r4)
    constexpr uint32_t kStepRoutines = 0x065EAF70; // longs, by routine_no[0] (plmain_lv_00)

    // 061195E0 (Player_move): the fighter's frame (r4 its record, r5 a person's lever and buttons): its input (above),
    // and what it keeps of the last frame; then the routine for its step, jumped to (its leftovers reach the players'
    // routine: r0 and r7 it reads). Leaves r0 the steps' table, r1 the step's offset in it, r2 the routine, r3 0x7FFF, r4
    // the record for it.
    bool PlayerMove(Guest &g) {
        const uint32_t fighter = g.R(4);
        uint32_t step = 0;
        {
            Frame frame(g, 16); // (r8, r13, r14, pr)
            const uint32_t controls = g.Read32(fighter + kControls);
            bool changing = false;
            if (g.Read8(kRecordingPlays) == 0xFF) {
                g.R(4) = Extended(Word(g, fighter + kSide));
                g.Call(kRecorded);
                g.R(4) = g.R(0);
                g.Call(kAsPlayers);
                g.Write16(controls, static_cast<uint16_t>(g.R(0)));
            } else {
                if (g.Read8(fighter + kOperator) != 0) {
                    g.R(3) = controls;
                    g.Write16(controls, static_cast<uint16_t>(g.R(5)));
                } else {
                    g.R(3) = kComputer;
                    g.R(4) = fighter;
                    g.Call(kComputer);
                    g.R(2) = kAsPlayers;
                    g.R(4) = g.R(0);
                    g.Call(kAsPlayers);
                    g.Write16(controls, static_cast<uint16_t>(g.R(0)));
                }
                g.R(0) = kChanging;
                g.R(1) = Signed(Byte(g, fighter + kChanging));
                changing = g.R(1) != 0;
            }
            // (no input while changing form, knocked out or held)
            const auto none = [&] {
                g.R(0) = kControls;
                g.R(3) = g.Read32(fighter + kControls);
                g.Write16(g.R(3), 0);
            };
            if (changing) {
                none();
            }
            g.R(0) = kDead;
            g.R(1) = Signed(Byte(g, fighter + kDead));
            if (g.R(1) != 0) {
                none();
            }
            g.R(0) = kHeld;
            g.R(1) = Signed(Byte(g, fighter + kHeld));
            if (g.R(1) != 0) {
                none();
            }
            g.R(0) = kHeld;
            g.R(2) = Signed(Byte(g, fighter + kHeld));
            g.R(3) = Signed(Byte(g, fighter + kDead)) + g.R(2);
            g.R(5) = 0;
            if (g.R(3) == 0) {
                g.R(0) = kFacingKept;
                g.Write8(fighter + kFacingKept, 0);
            }
            // (the last frame's steps and place)
            for (uint32_t n = 0; n < 8; n++) {
                g.R(1) = Extended(Word(g, fighter + kSteps + n * 2));
                g.Write16(fighter + kLastSteps + n * 2, static_cast<uint16_t>(g.R(1)));
            }
            g.R(2) = fighter + kSteps + 14;
            g.R(3) = 8;
            g.R(4) = 16;
            g.R(5) = 8;
            g.R(6) = 8;
            g.R(7) = 14;
            for (uint32_t n = 0; n < 3; n++) {
                g.R(1) = Extended(Word(g, fighter + kPlace + n * 4));
                g.Write16(fighter + kLastPlace + n * 2, static_cast<uint16_t>(g.R(1)));
            }
            g.R(2) = fighter + kPlace + 8;
            g.R(3) = 3;
            g.R(5) = 12;
            g.R(6) = 3;
            g.R(7) = 3;
            g.SetT(true);
            g.R(4) = fighter;
            g.Call(kLastMoves);
            g.Write8(fighter + kLastGuard, g.Read8(fighter + kGuard));
            g.Write16(fighter + kRenewAttack, 0);
            g.Write16(fighter + kLastHealth, g.Read16(fighter + kHealth));
            // (its commands read, or let go by in a super art's freeze)
            g.R(3) = Extended(Word(g, fighter + kHealth));
            g.R(0) = g.Read8(fighter + kSuperFreeze);
            g.SetT(g.R(0) == 1);
            const uint32_t reader = g.R(0) == 1 ? kLetGo : kCommands;
            g.R(2) = reader;
            g.R(4) = fighter;
            g.Call(reader);
            const uint32_t now = g.Read32(fighter + kControls);
            const int16_t press = Word(g, now + 14);
            g.Write16(fighter + kCommandWork, static_cast<uint16_t>(press));
            g.Write16(fighter + kCommandWork + 2, static_cast<uint16_t>((Word(g, fighter + kCommandWork + 2) + press) & 0x7FFF));
            g.Write16(fighter + kCommandWork + 4, g.Read16(now + 2));
            g.Write16(fighter + kCommandWork + 6, g.Read16(now + 6));
            step = Extended(Word(g, fighter + kSteps)) << 2;
        }
        // (the step's routine)
        g.R(0) = kStepRoutines;
        g.R(1) = step;
        g.R(2) = g.Read32(kStepRoutines + step);
        g.R(3) = 0x7FFF;
        g.R(4) = fighter;
        g.Call(g.R(2));
        g.Return(100);
        return true;
    }

    // 06092294: the next word of the demonstration fight's recording for a side (r4), and the recording moved on.
    // Leaves r4 the word, r5 the side's place in kRecordings, r2 where its recording now is.
    bool NextRecorded(Guest &g) {
        const uint32_t place = kRecordings + Signed(static_cast<int16_t>(g.R(4))) * 4;
        const uint32_t at = g.Read32(place);
        const uint32_t word = Signed(Word(g, at));
        g.Write32(place, at + 2);
        g.R(0) = word & 0xFFFF;
        g.R(2) = at + 2;
        g.R(4) = word;
        g.R(5) = place;
        g.Return(10);
        return true;
    }

    // 060B6242: a lever and buttons (r4) as the computer and the recordings keep them (the kicks at bits 7-9) put in a
    // person's form (the kicks at bits 8-10, bit 7 unused); r0 the word. Leaves r4 the word, r5 its low seven bits,
    // r2 0x700.
    bool AsPlayersInput(Guest &g) {
        const uint32_t low = g.R(4) & 0x7F;
        const uint32_t kicks = ((g.R(4) & 0xFFFF) << 1) & 0x700;
        g.R(2) = 0x700;
        g.R(4) = kicks | low;
        g.R(5) = low;
        g.R(0) = kicks | low;
        g.Return(8);
        return true;
    }

    // (free: r1-r7 and T for the first, r1-r6 and T for the second, whose callers read r7: liveout.py; Player_move's
    // leftovers are those of the step's routine, but for r1-r6 and T, which no caller reads)
    const Lift kLifts[] = {
        {"PlayerMove", 0x061195E0, 0x1AE, 0xA017029C1BCE23C9ull, PlayerMove, true, 0x1007E},
        {"NextRecorded", 0x06092294, 0x16, 0x0E25CAEB4E08C3AFull, NextRecorded, true, 0x100FE},
        {"AsPlayersInput", 0x060B6242, 0x12, 0xAA619E8E79F01418ull, AsPlayersInput, true, 0x1007E},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightInput() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
