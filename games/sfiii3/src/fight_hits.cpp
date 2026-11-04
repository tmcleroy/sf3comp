// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's hit tests, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h): two
// boxes' overlap (0x0608E908), and what tests with it: the throws (0x0608E43C) and the attacks (0x0608E5C4) of the
// records queued for the frame's hit tests (fight_match.cpp's HitCheck calls them), and the two fighters' push boxes
// (0x0611DBA4). What the results then make of a hit (guarded, parried, a counter hit: set_judge_result, 0x0608C7F8, and
// what it calls) is fight_judge.cpp's. (A hit's damage, its scaling and its taking: lifts.cpp's BaseDamage,
// ScaleDamage, TakeDamage.)
//
// Each frame the records that may hit or be hit (fighters, projectiles, effects with boxes) are queued (q_hit_push);
// the throws are tested first (the first fighter met by a throw is caught), then the attacks (each record's boxes that
// may be hit against every other's live attack boxes: the deepest overlap wins, and an attack hits once).
//
// A box is four words (sfiii3.h's kBoxes): its left edge and width across, from the owner's place and turned with its
// facing (a record's byte +0x0A, rl_flag: the box mirrored about the owner), then its bottom and height up. Two boxes
// meet when each one's span across and up overlaps the other's; the tests take one unsigned comparison for each (the
// gap between the near edges, as an unsigned number, below the two sizes together).
#include "fight_core.h"
#include "sfiii3.h"

#include <iterator>

namespace cps3recomp {

bool ModelViewerOn(); // model.cpp

namespace fighting {

namespace {
    constexpr uint32_t kFacing = 0x0A;       // byte: not 0 facing right (rl_flag): boxes mirrored
    constexpr uint32_t kX = 0x64, kY = 0x68; // words: where it is (xyz[0], xyz[1])

    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }

    // Records the port has meet nothing. While the model viewer is on, player 2's fighter, kept unseen beside the
    // character, is hit by none of the character's attacks or projectiles and grabbed by none of its throws: every test
    // with its record says the boxes are apart (those called from 0x0608E83E, the character's and its projectiles'
    // attacks: AttackCheck; from 0x0608E55E, a throw's catch: CatchCheck, Hugo's 360 seen; and from 0x0611DBD8, the two
    // fighters' push boxes: BodiesTouch). Otherwise none: the game's own test.
    bool Unmet(uint32_t one, uint32_t two) {
        return (one == sfiii3::kPlayer[1] || two == sfiii3::kPlayer[1]) && ModelViewerOn();
    }

    // 0608E908 (hit_check_subroutine): whether the box at r6 of the record r4 meets the box at r7 of the record r5;
    // r0 0 if not, else how deep they overlap across (the smaller of the overlap from either side). Leaves the
    // registers as the game's code (its callers read r1, r2 and r5-r7): r6 and r7 past the boxes' first two words, or
    // past their third with the test up made; r1 the first box's left edge in the stage, or the second's owner's height;
    // r2 the sizes across together, or the second box's height; r5 the second record, or the sizes up together.
    bool HitTest(Guest &g) {
        const uint32_t one = g.R(4), two = g.R(5);
        Frame frame(g, 12); // (r11, r13, r14)
        // (across: each box's left edge in the stage)
        int32_t left1 = Word(g, g.R(6));
        const int32_t width1 = Word(g, g.R(6) + 2);
        g.R(6) += 4;
        if (g.Read8(one + kFacing) != 0) {
            left1 = -left1 - width1;
        }
        left1 += Word(g, one + kX);
        int32_t left2 = Word(g, g.R(7));
        const int32_t width2 = Word(g, g.R(7) + 2);
        g.R(7) += 4;
        if (g.Read8(two + kFacing) != 0) {
            left2 = -left2 - width2;
        }
        left2 += Word(g, two + kX);
        // (the gap from box 1's left edge to box 2's right one, against the two widths)
        const int32_t across = left2 + (width2 - left1);
        const int32_t widths = width2 + width1;
        g.R(1) = Signed(left1);
        g.R(3) = Extended(across);
        g.R(2) = Extended(widths);
        g.R(4) = one;
        g.R(5) = two;
        // (the model viewer's unseen player 2 is met by nothing: the boxes said to be apart across, as any two are)
        if (Extended(across) >= Extended(widths) || Unmet(one, two)) {
            g.SetT(true);
            g.R(0) = 0;
            g.Return(30);
            return true;
        }
        // (up: the same with the bottoms and heights)
        const int32_t bottom1 = Word(g, g.R(6)), height1 = Word(g, g.R(6) + 2);
        g.R(6) += 2;
        const int32_t bottom2 = Word(g, g.R(7)), height2 = Word(g, g.R(7) + 2);
        g.R(7) += 2;
        const int32_t up = Word(g, one + kY) + bottom1 - Word(g, two + kY) - bottom2 + height1;
        const int32_t heights = height1 + height2;
        g.R(1) = Extended(Word(g, two + kY));
        g.R(2) = Extended(height2);
        g.R(3) = Extended(bottom2);
        g.R(4) = Extended(up);
        g.R(5) = Extended(heights);
        if (Extended(up) >= Extended(heights)) {
            g.SetT(true);
            g.R(0) = 0;
            g.Return(40);
            return true;
        }
        // (how deep: the overlap from the nearer side)
        int32_t depth = static_cast<int16_t>(across);
        const int32_t other = static_cast<int16_t>(widths) - depth;
        g.R(2) = Signed(other);
        g.R(4) = Extended(across);
        g.SetT(depth > other);
        if (depth > other) {
            depth = widths - across;
            g.R(2) = Signed(depth);
        }
        g.R(0) = Extended(depth);
        g.Return(50);
        return true;
    }

    // --- the hit queue: what may hit what this frame ---
    constexpr uint32_t kQueue = 0x0200EBC4;   // longs: the records queued this frame, fighters and the rest (q_hit_push)
    constexpr uint32_t kQueued = 0x0200EEC8;  // word: how many (hpq_in)
    constexpr uint32_t kResults = 0x0200EC44; // 16 bytes each, by the queue's order (hs): a word of results (0x1000
                                              // its catch made, 0x100 it is caught, 0x10 its attack hit, 1 it is hit),
                                              // the attack's box (+2, a byte) and the box hit (+3), the other's place
                                              // in the queue (+4 whom it hit, +6 who hit it), the boxes (+8 the
                                              // attack's, +12 the one hit)
    constexpr uint32_t kTargets = 0x0200EE48; // longs, 11: the boxes an attack may hit on the one tested: its body's
                                              // four, its hands' four, its own attack's last two, its push box
                                              // (dmdat_adrs)
    constexpr uint32_t kDeepest = 0x0200EE88; // words, by the queue's order: the deepest hit on it so far (mkm_wk)
    constexpr uint32_t kPlayersHeld = 0x02068C67; // byte: the players' controls not taken (pcon_dp_flag)
    constexpr uint32_t kCatchableWhenHurt = 0x0618C30B; // bytes, by the hurt step (routine_no[2]): a throw may catch
                                                        // it there (dm_oiuchi_catch)
    // ... in a record
    constexpr uint32_t kKind = 0x06;          // word: 1 a fighter, 8 ... (work_id)
    constexpr uint32_t kId = 0x08;            // word (id)
    constexpr uint32_t kSteps = 0x24;         // words: its steps (routine_no[0..]): [1] 1 being hurt, [2] how, [3] ...
    constexpr uint32_t kPose = 0x20E;         // byte (pat_status)
    constexpr uint32_t kMoveKind = 0x20F;     // byte (kind_of_waza)
    constexpr uint32_t kCgType = 0x215;       // byte (cg_type)
    constexpr uint32_t kBodyBoxOut = 0x28E;   // word: its body's boxes out (cg_ja.bhix)
    constexpr uint32_t kAttackOut = 0x298;    // word: its attack's boxes out (cg_ja.atix)
    constexpr uint32_t kBodyBoxes = 0x2A0, kHandBoxes = 0x2A8; // longs: four boxes each (h_bod, h_han)
    constexpr uint32_t kCatchBox = 0x2B8, kCaughtBox = 0x2C0;  // longs: a throw's box, the box a throw catches (h_cat,
                                                               // h_cau)
    constexpr uint32_t kAttackBoxes = 0x2C8;  // long: four (h_att)
    constexpr uint32_t kPushBox = 0x2D4;      // long (h_hos)
    constexpr uint32_t kAttackFlags = 0x2E0;  // byte (att.dipsw): 2 it hits projectiles and is hit by them, 4 the
                                              // hands' boxes only, 0x40 the push box too, 0x60 a throw that takes the hurt
    constexpr uint32_t kAttackGuard = 0x2E1;  // byte (att.guard): 0x18 a throw that takes anyone
    constexpr uint32_t kHits = 0x2FC;         // word: the kinds it may hit (vs_id)
    constexpr uint32_t kAttackLive = 0x2FE;   // byte: its attack may still hit (att_hit_ok)
    constexpr uint32_t kOwner = 0x3BE;        // word: whose it is (master_id)
    constexpr uint32_t kReflected = 0x3C5;    // byte (refrected)
    constexpr uint32_t kUncatchable = 0x3C7;  // byte (tsukamarenai_flag)
    constexpr uint32_t kHitTest = 0x0608E908;

    uint32_t Queued(Guest &g, int16_t at) {
        return g.Read32(kQueue + Signed(at) * 4);
    }
    uint32_t Results(int16_t at) {
        return kResults + Signed(at) * 16;
    }

    // 0608E43C (catch_hit_check): each fighter's throw (its catch box out, its attack live) tested against each other
    // fighter's caught box, unless it is caught already, can't be caught now (tsukamarenai_flag), or is being hurt (a
    // throw catches the hurt only in one step of it, 25; one that takes the hurt, in the steps dm_oiuchi_catch allows, or
    // knocked about: cg_type 10; one that takes anyone, any): the first met is caught (the results, the boxes noted,
    // the throw's attack spent). Leaves r5-r7 as the game's code: the last test's, or the last record or results looked
    // at in r5.
    bool CatchCheck(Guest &g) {
        Frame frame(g, 40); // (r8-r14, pr, two longs)
        for (int16_t mine = 0; mine < Word(g, kQueued); mine++) {
            if (g.Read16(Results(mine)) & 0x1000) {
                continue;
            }
            const uint32_t thrower = Queued(g, mine);
            if (Word(g, thrower + kKind) != 1 || g.Read8(thrower + kAttackLive) == 0) {
                continue;
            }
            const uint32_t catchBox = g.Read32(thrower + kCatchBox);
            if (Word(g, catchBox + 2) == 0) {
                continue;
            }
            for (int16_t theirs = 0; theirs < Word(g, kQueued); theirs++) {
                if (theirs == mine || (g.Read16(Results(theirs)) & 0x100)) {
                    continue;
                }
                const uint32_t other = Queued(g, theirs);
                g.R(5) = other;
                if (Word(g, other + kKind) != 1) {
                    continue;
                }
                const uint32_t caughtBox = g.Read32(other + kCaughtBox);
                if (Word(g, caughtBox + 2) == 0) {
                    continue;
                }
                if (!(g.Read8(thrower + kAttackGuard) & 0x18)) {
                    if (Byte(g, other + kUncatchable) != 0) {
                        continue;
                    }
                    const bool hurt = Word(g, other + kSteps + 2) == 1 && Word(g, other + kSteps + 6) != 0;
                    if (!(g.Read8(thrower + kAttackFlags) & 0x60)) {
                        if (hurt && Word(g, other + kSteps + 4) != 25) {
                            continue;
                        }
                    } else if (hurt && g.Read8(other + kCgType) != 10 &&
                               Byte(g, kCatchableWhenHurt + Signed(Word(g, other + kSteps + 4))) == 0) {
                        continue;
                    }
                }
                g.R(4) = thrower;
                g.R(6) = catchBox;
                g.R(7) = caughtBox;
                g.Call(kHitTest);
                if (g.R(0) == 0) {
                    continue;
                }
                // (caught)
                g.Write16(Results(mine), static_cast<uint16_t>(g.Read16(Results(mine)) | 0x1000));
                g.Write16(Results(mine) + 4, static_cast<uint16_t>(theirs));
                g.R(5) = Results(theirs);
                g.Write16(Results(theirs), static_cast<uint16_t>(g.Read16(Results(theirs)) | 0x100));
                g.Write16(Results(theirs) + 6, static_cast<uint16_t>(mine));
                g.Write8(thrower + kAttackLive, 0);
                g.Write32(Results(mine) + 8, catchBox);
                g.Write32(Results(theirs) + 12, caughtBox);
                break;
            }
        }
        g.Return(200);
        return true;
    }

    // 0608E5C4 (attack_hit_check): for each record queued not hit or caught already, its boxes that may be hit (its
    // body's, its hands', its attack's last two, its push box: kTargets), tested against each live attack's four boxes of
    // each other record queued that may hit it: by kind (vs_id; projectiles that hit projectiles, kAttackFlags 2, and
    // reflected ones whatever their kind), not its own projectiles, nor their owner; an attack's last two boxes not
    // against the other's last two; the hands' boxes and the other's attack's only from in front (and with kAttackFlags 4,
    // only the hands' while its body's are out); the push box only by an attack that says so (0x40), not while the
    // players' controls are not taken, nor against a throw's move, nor pose 38. The deepest overlap so far on it wins (the
    // results, the boxes noted, the attack spent: none of its boxes tests the rest after the first four). Leaves r1 and
    // r5-r7 as the game's code (its caller's next routine's reading is not followed by liveout.py).
    bool AttackCheck(Guest &g) {
        Frame frame(g, 60); // (r8-r14, pr, seven longs)
        for (int16_t theirs = 0; theirs < Word(g, kQueued); theirs++) {
            if (g.Read16(Results(theirs)) & 0x1101) {
                continue;
            }
            const uint32_t other = Queued(g, theirs);
            // (its boxes that may be hit)
            uint32_t body = g.Read32(other + kBodyBoxes), hands = g.Read32(other + kHandBoxes);
            for (uint32_t n = 0; n < 4; n++, body += 8, hands += 8) {
                g.Write32(kTargets + n * 4, body);
                g.Write32(kTargets + 16 + n * 4, hands);
            }
            g.R(5) = kTargets;
            g.R(6) = hands;
            g.R(7) = body;
            const uint32_t its = g.Read32(other + kAttackBoxes);
            g.R(1) = its + 16;
            g.Write32(kTargets + 32, its + 16);
            g.Write32(kTargets + 36, its + 24);
            g.Write32(kTargets + 40, g.Read32(other + kPushBox));
            for (int16_t mine = 0; mine < Word(g, kQueued); mine++) {
                if (mine == theirs || (g.Read16(Results(mine)) & 0x1110)) {
                    continue;
                }
                const uint32_t attacker = Queued(g, mine);
                if (Word(g, attacker + kAttackOut) == 0) {
                    continue;
                }
                g.R(1) = Signed(Byte(g, attacker + kAttackLive));
                if (g.R(1) == 0) {
                    continue;
                }
                // (whom it may hit: by kind, but projectiles that hit projectiles, and reflected ones)
                const int16_t kind = Word(g, attacker + kKind), otherKind = Word(g, other + kKind);
                const bool projectiles = g.Read8(attacker + kAttackFlags) & 2, otherProjectiles = g.Read8(other + kAttackFlags) & 2;
                bool may = projectiles && (otherProjectiles || (otherKind != 1 && Byte(g, other + kReflected) != 0));
                if (!may) {
                    g.R(5) = Signed(kind);
                    may = (kind == 1 || kind == 8) && otherProjectiles;
                }
                if (!may) {
                    g.R(1) = Signed(Word(g, attacker + kHits));
                    if ((g.R(1) & Signed(otherKind)) == 0) {
                        continue;
                    }
                }
                // (not its own projectiles, nor their owner)
                if (kind != 1) {
                    if (Word(g, attacker + kOwner) == Word(g, other + (otherKind == 1 ? kId : kOwner))) {
                        continue;
                    }
                } else if (otherKind != 1 && Byte(g, other + kReflected) == 0 && Word(g, attacker + kId) == Word(g, other + kOwner)) {
                    continue;
                }
                const uint32_t boxes = g.Read32(attacker + kAttackBoxes);
                for (int16_t box = 0; box < 4; box++) {
                    const uint32_t attack = boxes + Signed(box) * 8;
                    if (Word(g, attack + 2) != 0) {
                        for (int16_t target = 0; target < 11; target++) {
                            if (target > 3 && Byte(g, attacker + kAttackLive) == 0) {
                                goto next; // (spent: on to the next record queued)
                            }
                            const uint32_t at = g.Read32(kTargets + Signed(target) * 4);
                            if (Word(g, at + 2) == 0) {
                                continue;
                            }
                            g.R(5) = Signed(box);
                            if ((box == 2 || box == 3) && (target == 8 || target == 9)) {
                                continue;
                            }
                            const uint8_t flags = g.Read8(attacker + kAttackFlags);
                            if (target > 3 && target < 10) {
                                // (from in front only, but when the two face the same way)
                                if (!((Byte(g, attacker + kFacing) + Byte(g, other + kFacing)) & 1)) {
                                    const int16_t x = Word(g, attacker + kX), otherX = Word(g, other + kX);
                                    if (Byte(g, attacker + kFacing) != 0 ? x > otherX : x < otherX) {
                                        continue;
                                    }
                                }
                                if ((flags & 4) && (target > 7 || Word(g, other + kBodyBoxOut) == 0)) {
                                    continue;
                                }
                            }
                            if (target == 10) {
                                if (!(flags & 0x40) || (g.Read8(other + kMoveKind) & 0x60)) {
                                    continue;
                                }
                                g.R(1) = Signed(Byte(g, kPlayersHeld));
                                if (g.R(1) != 0 || g.Read8(other + kPose) == 38) {
                                    continue;
                                }
                            }
                            g.R(4) = attacker;
                            g.R(5) = other;
                            g.R(6) = attack;
                            g.R(7) = at;
                            g.Call(kHitTest);
                            const int16_t depth = static_cast<int16_t>(g.R(0));
                            g.R(6) = g.R(0);
                            g.R(7) = Signed(theirs) * 2;
                            if (depth <= Word(g, kDeepest + g.R(7))) {
                                continue;
                            }
                            // (the deepest yet)
                            g.R(5) = Results(mine);
                            g.Write16(Results(mine), static_cast<uint16_t>(g.Read16(Results(mine)) | 0x10));
                            g.Write16(Results(mine) + 4, static_cast<uint16_t>(theirs));
                            g.Write8(Results(mine) + 2, static_cast<uint8_t>(box));
                            g.Write16(Results(theirs), static_cast<uint16_t>(g.Read16(Results(theirs)) | 1));
                            g.Write16(Results(theirs) + 6, static_cast<uint16_t>(mine));
                            g.Write8(Results(theirs) + 3, static_cast<uint8_t>(target));
                            g.Write8(attacker + kAttackLive, 0);
                            g.Write16(kDeepest + g.R(7), static_cast<uint16_t>(g.R(6)));
                            g.Write32(Results(mine) + 8, attack);
                            g.R(1) = kTargets + Signed(target) * 4;
                            g.Write32(Results(theirs) + 12, at);
                        }
                    }
                    g.R(1) = Signed(box) + 1;
                }
            }
        next:;
        }
        g.R(1) = kQueued; // (as the loop's test leaves it)
        g.Return(400);
        return true;
    }

    // 0608C7F8 (set_judge_result): each record queued that was hit or caught (its results' 1 or 0x100) given what it
    // took: caught (set_caught_status, 0x0608C994) or struck (set_struck_status, 0x0608CE68: guarded, parried, hit, a
    // counter hit; r4 its place in the queue); r0 1 when any was. Leaves r3 the count queued, r4 and the rest the last
    // routine's, or the last results looked at in r4.
    bool JudgeResults(Guest &g) {
        Frame frame(g, 32); // (r8-r14, pr)
        bool any = false;
        for (int16_t at = 0; at < Word(g, kQueued); at++) {
            g.R(4) = Results(at);
            g.R(3) = g.Read16(Results(at));
            if (g.R(3) & 0x101) {
                any = true;
                g.R(4) = Signed(at);
                g.Call(g.R(3) & 0x100 ? 0x0608C994 : 0x0608CE68);
            }
            g.R(3) = Signed(Word(g, kQueued));
        }
        g.R(0) = any ? 1 : 0;
        g.Return(60);
        return true;
    }

    // --- the fighters' bodies ---
    constexpr uint32_t kLastUp = 0x72;        // word: its height last frame (old_pos[1])
    constexpr uint32_t kSticking = 0x3C9;     // byte: 1 or 2, the way it is stuck against a wall (micchaku_flag)
    constexpr uint32_t kPushed = 0x3CB;       // byte: 1 or 2, the way it was pushed (hos_em_flag)
    constexpr uint32_t kOrder = 0x02016B8E;   // byte: player 1 on the right (ichikannkei)
    constexpr uint32_t kPushBy = 0x0611DCBE;  // meri_case_switch (r4 the overlap): r0 how far to push
    constexpr uint32_t kPlaceOrder = 0x0611E056; // check_work_position (r4, r5 the records): r0 1 the first on the
                                                 // right

    // 0611DBA4 (check_body_touch): the two fighters' push boxes (kPushBox: its first word not 0 while it is out): where
    // they overlap, each pushed away from the other by an amount for the overlap (meri_case_switch), but not into a
    // wall it is stuck against; which way by the order they stand in (both on the ground: ichikannkei, else
    // check_work_position), and noted (hos_em_flag); else neither noted. (Its registers are free.)
    bool BodiesTouch(Guest &g) {
        Frame frame(g, 20); // (r12-r14, pr, macl)
        const uint32_t one = kPlayers, two = kPlayers + kPlayerSize;
        int32_t overlap = 0;
        if (Word(g, g.Read32(one + kPushBox)) != 0 && Word(g, g.Read32(two + kPushBox)) != 0) {
            g.R(4) = one;
            g.R(5) = two;
            g.R(6) = g.Read32(one + kPushBox);
            g.R(7) = g.Read32(two + kPushBox);
            g.Call(kHitTest);
            overlap = static_cast<int16_t>(g.R(0));
        }
        if (overlap == 0) {
            g.Write8(one + kPushed, 0);
            g.Write8(two + kPushed, 0);
            g.Return(30);
            return true;
        }
        g.R(4) = Extended(overlap);
        g.Call(kPushBy);
        const int32_t by = static_cast<int16_t>(g.R(0));
        bool oneRight;
        if (Word(g, one + kLastUp) > 0 || Word(g, two + kLastUp) > 0) {
            g.R(4) = one;
            g.R(5) = two;
            g.Call(kPlaceOrder);
            oneRight = g.R(0) != 0;
        } else {
            oneRight = g.Read8(kOrder) != 0;
        }
        // (pushed apart, unless stuck against the wall it would go into)
        const auto push = [&](uint32_t player, int32_t sign, uint8_t wall) {
            const int32_t amount = g.Read8(player + kSticking) != wall ? by : 0;
            g.Write16(player + kX, static_cast<uint16_t>(Word(g, player + kX) + sign * amount));
        };
        push(one, oneRight ? 1 : -1, oneRight ? 1 : 2);
        push(two, oneRight ? -1 : 1, oneRight ? 2 : 1);
        g.Write8(one + kPushed, oneRight ? 2 : 1);
        g.Write8(two + kPushed, oneRight ? 1 : 2);
        g.Return(60);
        return true;
    }

    // (free: r3, r4 and T, which no caller reads: liveout.py)
    const Lift kLifts[] = {
        {"JudgeResults", 0x0608C7F8, 0x68, 0x13841ADF29F3F7A4ull, JudgeResults, true, 0x10004},
        {"BodiesTouch", 0x0611DBA4, 0x11A, 0xD3123604D2084F56ull, BodiesTouch, true, 0x110FF},
        {"CatchCheck", 0x0608E43C, 0x188, 0x7E0FDF650D28869Dull, CatchCheck, true, 0x1001F},
        {"AttackCheck", 0x0608E5C4, 0x32A, 0xD4A4A35310C84965ull, AttackCheck, true, 0x1001D},
        {"HitTest", 0x0608E908, 0x82, 0x4B446CC884A7C1DCull, HitTest, true, 0x10018},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightHits() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
