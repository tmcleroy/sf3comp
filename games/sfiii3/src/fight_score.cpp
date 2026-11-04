// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's score, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h): the points
// a hit scores (0x0609E488) and a player's score drawn on the panel (0x0609E57A).
//
// How the score works. Each player has three longs (Score[2][3], at 0x0201139C, twelve bytes a player): the score of
// the way of playing now (Play_Type, a word at 0x020113B4: the first long for 0, the second for 1), and all the points
// ever scored (the third). A hit's points come from a table by the kind of hit (Score_Data: longs at 0x06194A3C) and
// are added by 0x0609E488 as the hit's damage is taken (TakeDamage in lifts.cpp, and two more of the game's hits):
// to the third always, to the score only when a person plays the side (a computer's side scores nothing it shows); the
// first long never goes past 99,999,900, the second is not held. The panel shows the score only as it changes: the
// adding draws it (0x0609E57A: its digits from the first that is not 0, on the text layer, before the two 0s the panel
// always has), and so do the panel's set-up and the bonus stages' counts. Nothing draws it otherwise: a score changed
// from outside stays as it was drawn, until the next hit's points. So the port's features that change it ask for it to
// be drawn (AskScoresDrawn), and the round's time (fight_round.cpp) draws it at its next frame.
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    // --- the game's ---
    constexpr uint32_t kScores = 0x0201139C;    // longs, three a player (Score)
    constexpr uint32_t kPlayType = 0x020113B4;  // word: which of the first two longs is the score (Play_Type)
    constexpr uint32_t kPoints = 0x06194A3C;    // longs: a hit's points, by its kind (Score_Data)
    constexpr uint32_t kMostScore = 99999900;   // (the first long held at it)
    constexpr uint32_t kStageNow = 0x02026BB0;  // byte: the stage fought on (bg_w.stage): 21 and 22 the bonus stages,
                                                // where the score is not drawn as points are scored
    constexpr uint32_t kDigitsAt = 0x06194684;  // words, two a player: where the score's last digit goes on the text
                                                // layer, by kDigitsSet
    constexpr uint32_t kDigitsSet = 0x0206AC62; // byte: which of the two (signed)
    constexpr uint32_t kDrawDigit = 0x060D5E44; // r4 the column, r5 the row, r6 the size, r7 the digit: draws it
    constexpr uint32_t kDrawScore = 0x0609E57A;

    // --- the attacker's record (a player's, or one of its effects': a projectile) ---
    constexpr uint32_t kKind = 0x06;     // word: 1 a player's record (wu.work_id)
    constexpr uint32_t kSide = 0x08;     // word: a player's side (wu.id)
    constexpr uint32_t kMaster = 0x3B8;  // long: an effect's player (my_master)
    constexpr uint32_t kMasterSide = 0x3BE; // word: its side (master_id)

    uint32_t ScoresOf(int32_t side) {
        return kScores + Signed(static_cast<int8_t>(side * 12)); // (the offset cut to a signed byte, as the code has it)
    }
    uint32_t PlayerOf(int32_t side) {
        return kPlayers + Signed(static_cast<int16_t>(side * static_cast<int32_t>(kPlayerSize)));
    }
    // A side's score is shown: a person plays it, and not on a bonus stage
    bool Shown(Guest &g, int32_t side) {
        if (g.Read8(PlayerOf(side) + kOperator) == 0) {
            return false;
        }
        const int8_t stage = Byte(g, kStageNow);
        return stage != 22 && stage != 21;
    }

    // 0609E57A: a player's score (r4 the side) drawn: its digits from the ten millions down to the hundreds worked out
    // (the game's divisions, one for the digit and one for the next power of ten, as the code makes them), then those
    // from the first that is not 0 drawn on the text layer, ending at the player's place (kDigitsAt). A score of 0
    // draws nothing (the panel's 00 is its own). Leaves the registers as the game's code: those of the last digit's
    // drawing, r3 0 and T clear; with nothing drawn, r0 the side times 4, r1 the last division's, r2 and r7 -1, r3
    // kDigitsSet's place, r4 the side, r5 0, r6 10.
    bool DrawScore(Guest &g) {
        const uint32_t side = g.R(4);
        Frame frame(g, 44); // (r10-r14, pr and macl pushed, and eight words for the digits)
        const uint32_t scores = ScoresOf(static_cast<int32_t>(side));
        uint32_t left = g.Read32(scores + Signed(Word(g, kPlayType)) * 4);
        int16_t digit[8] = {};
        uint32_t power = 10000000;
        int16_t first = -1;      // (the first digit that is not 0)
        g.R(2) = scores; // (what the divisions find in r2, as the code leaves it; r3 the division's own address)
        for (int16_t place = 6; place >= 1; place--) {
            g.R(1) = left;
            g.R(0) = power;
            g.R(4) = side;
            g.R(5) = Signed(place);
            g.R(6) = power;
            g.R(7) = Signed(first);
            g.R(3) = kDivideUnsigned;
            g.Call(kDivideUnsigned);
            digit[place] = static_cast<int16_t>(g.R(0));
            const uint32_t taken = static_cast<uint32_t>(static_cast<int32_t>(digit[place])) * power;
            left -= taken;
            g.R(2) = taken;
            if (first < 0 && digit[place] != 0) {
                first = place;
            }
            // (the next power of ten)
            g.R(1) = power;
            g.R(0) = 10;
            g.R(5) = Signed(place - 1);
            g.R(7) = Signed(first);
            g.R(3) = kDivide;
            g.Call(kDivide);
            power = g.R(0);
        }
        // (the digits drawn, the last at the player's place)
        const int32_t set = Byte(g, kDigitsSet);
        const uint32_t places = kDigitsAt + Signed(static_cast<int16_t>(side) * 4);
        int32_t column = Word(g, places + Signed(set * 2)) - first - 1;
        g.R(0) = Signed(static_cast<int16_t>(side) * 4);
        g.R(2) = Signed(first);
        g.R(3) = kDigitsSet;
        g.R(4) = side;
        g.R(5) = 0;
        g.R(6) = power;
        g.R(7) = Signed(first);
        if (first < 1) {
            g.SetT(false);
            return true;
        }
        for (int16_t place = first; place >= 1; place--, column++) {
            g.R(0) = Signed(place * 2);
            g.R(4) = Signed(column);
            g.R(5) = 0;
            g.R(6) = 16;
            g.R(7) = Signed(digit[place]);
            g.Call(kDrawDigit);
        }
        g.R(3) = 0;
        g.SetT(false);
        return true;
    }
    bool DrawScoreLift(Guest &g) {
        DrawScore(g);
        g.Return(400);
        return true;
    }

    // 0609E488 (Additinal_Score_DM): a hit's points (r5 its kind) for the attacker (r4: a player's record, or an
    // effect's of a player's; none for an effect of anything else's), added to its side's points all told and, when a
    // person plays the side, to its score (the first long held at 99,999,900), and the score drawn
    bool AddPoints(Guest &g) {
        const uint32_t attacker = g.R(4);
        int32_t side;
        if (Word(g, attacker + kKind) == 1) {
            side = Word(g, attacker + kSide);
        } else {
            if (Word(g, g.Read32(attacker + kMaster) + kKind) != 1) {
                g.Return(20);
                return true;
            }
            side = Word(g, attacker + kMasterSide);
        }
        const uint32_t points = g.Read32(kPoints + (g.R(5) & 0xFFFF) * 4);
        const uint32_t scores = ScoresOf(side);
        g.Write32(scores + 8, g.Read32(scores + 8) + points);
        if (g.Read8(PlayerOf(side) + kOperator) != 0) {
            if (Word(g, kPlayType) == 0) {
                g.Write32(scores, g.Read32(scores) + points);
                if (g.Read32(scores) >= kMostScore) {
                    g.Write32(scores, kMostScore);
                }
            } else {
                g.Write32(scores + 4, g.Read32(scores + 4) + points);
            }
        }
        if (Shown(g, side)) {
            g.R(4) = Signed(static_cast<int16_t>(side));
            g.Call(kDrawScore);
        }
        g.Return(60);
        return true;
    }

    // (free: r0-r7 and T, which no caller of 0609E488 reads; 0609E57A's are all the game's)
    const Lift kLifts[] = {
        {"AddPoints", 0x0609E488, 0xF2, 0x6038545F2C85ED4Eull, AddPoints, true, 0x100FF},
        {"DrawScore", 0x0609E57A, 0x1C0, 0xFDAD1A871E2775E0ull, DrawScoreLift, true, 0},
    };

    bool g_asked = false; // (asked before a frame, drawn in it: nothing carried from one frame to the next)
} // namespace

void AskScoresDrawn() {
    g_asked = true;
}

bool ScoresDrawAsked() {
    const bool asked = g_asked;
    g_asked = false;
    return asked;
}

// The scores drawn, for a feature: the registers and T as they were (the game's code around knows nothing of it)
void DrawScores(Guest &g) {
    uint32_t r[8];
    for (unsigned n = 0; n < 8; n++) {
        r[n] = g.R(n);
    }
    const bool t = g.T();
    for (int32_t side = 0; side < 2; side++) {
        if (Shown(g, side)) {
            g.R(4) = Signed(side);
            g.Call(kDrawScore);
        }
    }
    for (unsigned n = 0; n < 8; n++) {
        g.R(n) = r[n];
    }
    g.SetT(t);
}

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightScore() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
