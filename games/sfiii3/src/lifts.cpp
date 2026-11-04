// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's routines rewritten as readable C++, and its hooks (cps3recomp/runtime/lift.h). Each names the
// routine's address and the hash of its code (tools/lifthash.py on the deciphered program), so it runs only on the
// code it was written from; a rewrite is checked against the game's code with CPS3RECOMP_LIFT_CHECK=1.
#include "game.h"
#include "lift.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <deque>
#include <vector>

namespace cps3recomp {

namespace {
    // 0611E0EE: the next of the 64 numbers of a table, from a counter that goes round them: the game's dice. The
    // counter's place and the table's are in the routine's literal pool (0611E1B4, 0611E1B8: 020155E8 and 065EB434),
    // read as the routine reads them. Leaves r0 the number, r1 the table, r3 the counter plus one before it was
    // wrapped, r4 the counter's place, and T clear (the shift's).
    bool NextRandom(Guest &g) {
        const uint32_t counter = g.Read32(0x0611E1B4), table = g.Read32(0x0611E1B8);
        const uint32_t next = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(counter))) + 1;
        g.Write16(counter, static_cast<uint16_t>(next));
        g.Write16(counter, static_cast<uint16_t>(static_cast<int16_t>(g.Read16(counter)) & 63));
        const uint32_t offset = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(counter))) << 1;
        g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(table + offset)));
        g.R(1) = table;
        g.R(3) = next;
        g.R(4) = counter;
        g.SetT(false);
        g.Return(13); // (eleven instructions and rts, as the original counts)
        return true;
    }


    // --- the round's clock ---
    //
    // The clock is a word at 0x02011376 (seconds left), with a word of frames to its next tick at 0x02011378 that is
    // reloaded from the timer speed's frames (a word at 0x02028688, the board's timer speed setting's); the panel
    // keeps its own copy of the seconds at 0x02028678. Under 30 seconds (or 10 at the last) it flashes: 0x02028682
    // says the flashing is on, 0x02028680 counts the frames to the next flash, 0x02028683 is the flash's place in its
    // four colours.
    constexpr uint32_t kClock = 0x02011376, kClockFrames = 0x02011378, kClockReload = 0x02028688, kClockShown = 0x02028678;
    constexpr uint32_t kFlashing = 0x02028682, kFlashFrames = 0x02028680, kFlashPlace = 0x02028683;
    constexpr uint32_t kFlashEvery = 0x061B7884, kFlashColours = 0x061B7888; // (tables of words: by the speed of flashing, by place)
    constexpr uint32_t kSetPalette = 0x060D57CA; // (r4 the palette, r5-r7 and a long on the stack: the panel's colours)
    constexpr uint32_t kDrawClock = 0x060D6482;  // (r4, r5 the tens, r6 the units: draws the clock's digits)
    constexpr uint32_t kDivide = 0x0612D428;     // (r1 / r0, signed, the quotient in r0)

    // A call of the game's with a long on the stack beyond the registers (taken off again after)
    void CallWithLong(Guest &g, uint32_t address, uint32_t extra) {
        g.R(15) -= 4;
        g.Write32(g.R(15), extra);
        g.Call(address);
        g.R(15) += 4;
    }

    // 060D7CB8: the clock's flash. Each call takes a frame off its count; when that runs out the count starts again
    // (fast with r4 1, under ten seconds; slow with 0) and the panel's clock takes the next of its four colours.
    // Leaves r0 the flash's place after it moved on (1-4: 4 as it wraps to 0), or the count with nothing done; r7 5.
    bool ClockFlash(Guest &g) {
        const int16_t left = static_cast<int16_t>(g.Read16(kFlashFrames) - 1);
        g.Write16(kFlashFrames, static_cast<uint16_t>(left));
        if (left >= 0) {
            g.Return(10);
            return true; // (r0 as it came: the caller's)
        }
        g.Write16(kFlashFrames, g.Read16(kFlashEvery + static_cast<uint32_t>(static_cast<int8_t>(g.R(4))) * 2));
        const uint32_t colour = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kFlashColours + static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kFlashPlace))) * 2)));
        g.R(4) = 22;
        g.R(5) = 0;
        g.R(6) = 4;
        g.R(7) = 5;
        CallWithLong(g, kSetPalette, colour);
        const int8_t place = static_cast<int8_t>(g.Read8(kFlashPlace) + 1);
        g.Write8(kFlashPlace, static_cast<uint8_t>(place));
        g.R(0) = static_cast<uint32_t>(static_cast<int8_t>(g.Read8(kFlashPlace)));
        if (place == 4) {
            g.Write8(kFlashPlace, 0);
        }
        g.Return(30);
        return true;
    }


    // 060D7BAE: the clock, once a frame in a round. Nothing while the game is held (0x02011386), while 0x0611C87C says
    // the round is not timed, or while 0x0200EECC or 0x0201136E is set; nothing either with no time left. Under 30
    // seconds the clock starts flashing (slowly; fast from 10); each frame the frames to the next tick go down, and
    // at the tick a second goes, the panel's copy is set, and the digits drawn (at 0, the clock's colours set once
    // more and 00 drawn). Leaves r0 as the game's code would (all its callers read).
    bool ClockTick(Guest &g) {
        if (const int8_t held = static_cast<int8_t>(g.Read8(0x02011386))) {
            g.R(0) = static_cast<uint32_t>(held);
            g.Return(8);
            return true;
        }
        g.Call(0x0611C87C);
        if (g.R(0) != 0) {
            g.Return(8);
            return true;
        }
        for (const uint32_t at : {0x0200EECCu, 0x0201136Eu}) {
            g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(at)));
            if (g.R(0) != 0) {
                g.Return(8);
                return true;
            }
        }
        const int16_t clock = static_cast<int16_t>(g.Read16(kClock));
        if (clock == 0) {
            g.Return(12);
            return true;
        }
        const uint16_t frames = g.Read16(kClockFrames), reload = g.Read16(kClockReload);
        g.R(7) = kFlashing; // (as the game's code leaves them for what it calls: r5 the flash's count's place, r6 0)
        g.R(5) = kFlashFrames;
        g.R(6) = 0;
        const auto flash = [&](int fast) {
            g.R(4) = static_cast<uint32_t>(fast);
            g.Call(0x060D7CB8);
        };
        if (g.Read8(kFlashing) != 0) {
            g.R(0) = static_cast<uint32_t>(clock == 10 ? static_cast<int16_t>(reload) : clock); // (at 10, read to compare)
            if (clock == 10 && frames == reload) {
                g.Write16(kFlashFrames, 0); // (the fast flashing begun at once)
                flash(1);
            } else {
                flash(clock <= 10 ? 1 : 0);
            }
        } else {
            g.R(0) = static_cast<uint32_t>(clock);
            if (clock == 30 && frames == reload) {
                g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(reload));
                g.Write8(kFlashing, 1); // (the flashing begun)
                g.Write16(kFlashFrames, 0);
                flash(0);
            }
        }
        // (the frames to the next tick)
        if (g.Read16(kClockFrames) != 0) {
            g.Write16(kClockFrames, static_cast<uint16_t>(g.Read16(kClockFrames) - 1));
            g.Return(20);
            return true;
        }
        g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kClockReload)));
        g.Write16(kClockFrames, static_cast<uint16_t>(g.R(0)));
        g.Write16(kClock, static_cast<uint16_t>(g.Read16(kClock) - 1));
        if (g.Read16(kClock) == 0) {
            // (time up: the clock's colours once more)
            g.R(4) = 22;
            g.R(5) = 0;
            g.R(6) = 4;
            g.R(7) = 5;
            CallWithLong(g, kSetPalette, 8);
        }
        g.Write16(kClockShown, g.Read16(kClock));
        const uint32_t seconds = g.Read16(kClock);
        g.R(1) = seconds;
        g.R(0) = 10;
        g.Call(kDivide);
        const uint32_t tens = g.R(0);
        uint32_t units = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kClock))) - tens * 10;
        g.R(0) = static_cast<uint32_t>(static_cast<int16_t>(g.Read16(kClock)));
        g.R(1) = tens * 10; // (and r1-r3 as it leaves them for the digits' drawing)
        g.R(2) = tens;
        g.R(3) = kClockShown;
        g.R(4) = 0;
        g.R(5) = tens;
        if (g.R(0) == 0) {
            units = 0;
            g.R(5) = 0;
        }
        g.R(6) = units;
        g.Call(kDrawClock);
        g.Return(40);
        return true;
    }


    // --- a hit's damage ---
    //
    // A player's record (r4): +0x9E its health (a word, 160 full; -1 knocked out), +0xA0 the round's health at its
    // start, +0xA2 the damage of the hit taking hold, +0x332 its stun, +0x324 -1 unless the hit is a throw's or the
    // like, +0x340 set when it must not survive at nothing, +0x3F8 a pointer to its stun gauge (byte +0 stunned, word +2
    // the most, word +8 what it has taken), +0x3FD knocked out, +0x448/+0x44A the damage kept for the combo readout.
    constexpr uint32_t kHealth = 0x9E, kRoundHealth = 0xA0, kDamage = 0xA2, kStunTaken = 0x332, kThrowHit = 0x324;
    constexpr uint32_t kNoSurviving = 0x340, kStunGauge = 0x3F8, kKnockedOut = 0x3FD;

    // 06124BC4: the hit's damage and stun taken. Not for one already knocked out. The hit's effects are noted
    // (0x0609E488, but for a light normal move's first frames) and the hit's other sources run (0x0611E510); then the
    // damage comes off the health. A hit that leaves -1 or -2 on one with health at the round's start leaves it at
    // nothing instead, unless it is a throw's; so does one on a fighter that may not survive. Below nothing it is
    // knocked out (the first knock-out of the round noted, 0x0608F3FC); otherwise the stun goes on its gauge, and
    // at the most the fighter is stunned. The hit's damage and stun are then cleared.
    bool TakeDamage(Guest &g) {
        const uint32_t player = g.R(4);
        const auto word = [&](uint32_t offset) { return static_cast<int16_t>(g.Read16(player + offset)); };
        const auto setWord = [&](uint32_t offset, int value) { g.Write16(player + offset, static_cast<uint16_t>(value)); };
        if (g.Read8(player + kKnockedOut) == 0) {
            if (word(kDamage) != 0) {
                // (a light normal move in its first frames is left out)
                const bool light = word(0x26) == 1 && word(0x28) <= 11 && word(0x2A) == 0;
                if (!light) {
                    g.R(4) = g.Read32(player + 0x14);
                    g.R(5) = g.Read16(player + 0x334);
                    g.Call(0x0609E488);
                }
            }
            g.R(4) = player;
            g.Call(0x0611E510);
            setWord(0x44A, g.Read8(player + 0x448) != 0 ? word(kDamage) : 0);
            g.Write8(player + 0x44D, 0);
            setWord(kHealth, word(kHealth) - word(kDamage));
            if (word(kThrowHit) == -1 && word(kRoundHealth) > 0 && word(kHealth) < 0 && word(kHealth) > -3) {
                setWord(kHealth, 0); // (survived at nothing)
            }
            if (g.Read8(player + kNoSurviving) != 0 && word(kHealth) < 0) {
                setWord(kHealth, 0);
            }
            if (word(kHealth) < 0) {
                setWord(kHealth, -1); // (knocked out)
                g.Write8(player + kKnockedOut, 1);
                g.Write8(0x02068C6B, 1);
                if (word(kThrowHit) != -1) {
                    g.Write8(player + 0x41D, 1);
                }
                if (g.Read8(0x02068C66) == 0) {
                    g.Call(0x0608F3FC); // (the round's first knock-out)
                    g.Write8(0x02068C66, 1);
                }
            } else {
                const uint32_t gauge = g.Read32(player + kStunGauge);
                if (g.Read8(gauge) == 0) {
                    const int taken = static_cast<int16_t>(g.Read16(gauge + 8) + word(kStunTaken));
                    g.Write16(gauge + 8, static_cast<uint16_t>(taken));
                    if (taken >= static_cast<int16_t>(g.Read16(gauge + 2))) {
                        g.Write32(gauge + 8, 0); // (stunned: the gauge empty again)
                        g.Write8(gauge, 1);
                    }
                }
            }
        }
        setWord(kDamage, 0);
        setWord(kStunTaken, 0);
        g.Return(80);
        return true;
    }


    // A signed division by the game's own routine (0612D428), so that every quotient is its: r1 / r0
    int32_t Quotient(Guest &g, int32_t dividend, int32_t divisor) {
        g.R(1) = static_cast<uint32_t>(dividend);
        g.R(0) = static_cast<uint32_t>(divisor);
        g.Call(kDivide);
        return static_cast<int32_t>(g.R(0));
    }

    // 0611E27C: a hit's damage as it is scaled. A fighter low on health takes less (its character's "guts": a table
    // of six by how much health is left, a sixteenth of the bar each, or a nineteenth for the characters whose bar is
    // 192), then the combo's scaling ((32 - the hits so far, +0x440) of 32 ... as the record's own ratio has it, +0xA4
    // and +0xA6, the board's damage level); never less than 1. Leaves r1 at the damage's place in the record.
    constexpr uint32_t kFullHealth = 0x02016B30, kGuts = 0x065EB574;
    bool ScaleDamage(Guest &g) {
        const uint32_t player = g.R(4);
        const auto word = [&](uint32_t offset) { return static_cast<int32_t>(static_cast<int16_t>(g.Read16(player + offset))); };
        if (word(kDamage) == 0) {
            g.Return(8);
            return true;
        }
        const int32_t full = static_cast<int16_t>(g.Read16(kFullHealth));
        const int32_t health = word(kHealth);
        if (health < Quotient(g, full * 10, 10)) {
            // (guts: the less health left, the less damage)
            int32_t level = full == 0xC0 ? Quotient(g, health, 19) : (health + (health < 0 ? 15 : 0)) >> 4;
            level = std::min<int32_t>(static_cast<int16_t>(level), 5);
            const int32_t character = word(0x3C0);
            const int32_t guts = static_cast<int16_t>(g.Read16(kGuts + static_cast<uint32_t>(static_cast<int16_t>(character * 12)) + static_cast<uint32_t>(level) * 2));
            g.Write16(player + kDamage, static_cast<uint16_t>(Quotient(g, static_cast<int16_t>(guts) * static_cast<int16_t>(word(kDamage)), word(0xA4))));
        }
        // (the combo's scaling, then the ratio)
        g.Write16(player + kDamage, static_cast<uint16_t>(Quotient(g, static_cast<int32_t>(static_cast<uint32_t>(32 - word(0x440)) * static_cast<uint32_t>(word(kDamage))), word(0xA4))));
        g.Write16(player + kDamage, static_cast<uint16_t>(Quotient(g, static_cast<int16_t>(word(0xA4)) * static_cast<int16_t>(word(kDamage)), word(0xA6))));
        g.R(1) = player + kDamage;
        if (word(kDamage) <= 0) {
            g.Write16(player + kDamage, 1);
        }
        g.Return(60);
        return true;
    }


    // 0609E36C: a hit's damage, from the attacker's (r4) attack to the defender's (r5) record: the attack's damage
    // (the game's table of them, by the attack's byte +0x2E4) times the board's damage level's percentage (its own for
    // character 15, the red Akuma), then the attacker's and the defender's handicaps (eighths, +0x45E and +0x460, with
    // their word +6 at 1). Leaves r1 at the defender's damage, r6 8 and r7 the damage level's place.
    constexpr uint32_t kAttackDamage = 0x061946EC, kDamageLevels = 0x06194A2C, kDamageLevel = 0x0201137A;
    bool BaseDamage(Guest &g) {
        const uint32_t attacker = g.R(4), defender = g.R(5);
        const auto word = [&](uint32_t at) { return static_cast<int32_t>(static_cast<int16_t>(g.Read16(at))); };
        const int32_t attack = word(kAttackDamage + static_cast<uint32_t>(g.Read8(attacker + 0x2E4)) * 2);
        const int32_t level = word(kDamageLevel);
        const int32_t percent = static_cast<int8_t>(g.Read8(kDamageLevels + static_cast<uint32_t>(level) + (word(attacker + 0x3C0) == 15 ? 8 : 0)));
        g.Write16(defender + kDamage, static_cast<uint16_t>(Quotient(g, static_cast<int16_t>(percent) * static_cast<int16_t>(attack), 100)));
        if (word(attacker + 6) == 1) {
            g.Write16(defender + kDamage, static_cast<uint16_t>(Quotient(g, static_cast<int16_t>(word(attacker + 0x45E)) * static_cast<int16_t>(word(defender + kDamage)), 8)));
        }
        if (word(defender + 6) == 1) {
            g.Write16(defender + kDamage, static_cast<uint16_t>(Quotient(g, static_cast<int16_t>(word(defender + 0x460)) * static_cast<int16_t>(word(defender + kDamage)), 8)));
        }
        g.R(1) = defender + kDamage;
        g.R(5) = defender;
        g.R(6) = 8;
        g.R(7) = static_cast<uint32_t>(level);
        g.Return(60);
        return true;
    }

    const Lift kLifts[] = {
        {"NextRandom", 0x0611E0EE, 24, 0xF56C31F46D9EC36Aull, NextRandom, true},
        {"ClockTick", 0x060D7BAE, 0x30, 0xD096A0977F288F4Bull, ClockTick, true, 0x100FE},
        {"TakeDamage", 0x06124BC4, 0x40, 0x19433AC288C4268Aull, TakeDamage, true, 0x100FF},
        {"ScaleDamage", 0x0611E27C, 0x3C, 0xB1FAE055D7A4391Bull, ScaleDamage, true, 0x1007D},
        {"BaseDamage", 0x0609E36C, 0x30, 0x67829EC126F0AE14ull, BaseDamage, true, 0x1001D},
        {"ClockFlash", 0x060D7CB8, 0x48, 0x21663C64E30E0582ull, ClockFlash, true, 0x1007E},
    };

    // 06000528: the vertical blank's interrupt, once a frame. (A demonstration of a hook, and a debugging aid:
    // SFIII3_VBLANKS=1 says every 600th, counted only for the message.)
    void VerticalBlank(Guest &) {
        static const bool say = getenv("SFIII3_VBLANKS") != nullptr;
        static unsigned long long count = 0;
        if (say && ++count % 600 == 0) {
            fprintf(stderr, "vertical blanks: %llu\n", count);
        }
    }
} // namespace

// A source file's lifts registered (debugging aid, SFIII3_LIFTS_ONLY=NAME,NAME: only those, to check a rewrite that the
// others call)
void RegisterGameLifts(const Lift *lifts, size_t count) {
    if (const char *only = getenv("SFIII3_LIFTS_ONLY")) {
        static std::deque<std::vector<Lift>> kept;
        std::vector<Lift> &some = kept.emplace_back();
        const std::string names = std::string(",") + only + ",";
        for (size_t n = 0; n < count; n++) {
            if (names.find(std::string(",") + lifts[n].name + ",") != std::string::npos) {
                some.push_back(lifts[n]);
            }
        }
        RegisterLifts(some.data(), some.size());
    } else {
        RegisterLifts(lifts, count);
    }
}

// The computer player's rewrites (computer.h), a source file for each part of it
void RegisterComputerEntry();
void RegisterComputerTactics();
void RegisterComputerAnswers();
void RegisterComputerMoves();
void RegisterStageCamera();

void RegisterLifts3rdStrike() {
    RegisterGameLifts(kLifts, std::size(kLifts));
    RegisterComputerEntry();
    RegisterComputerTactics();
    RegisterComputerAnswers();
    RegisterComputerMoves();
    RegisterStageCamera();
    AddHook("VerticalBlank", 0x06000528, 8, 0xF41697FE6C5AEE4Dull, VerticalBlank);
}

} // namespace cps3recomp
