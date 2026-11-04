// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's animations and boxes, written again as C++ (rewrites: runtime/lift.h; the parts of the fight:
// fight_core.h): how a record (a fighter's, a projectile's, an effect's) steps through its animation, and where each
// frame's boxes and attack are put in it. Training mode's frame data and box overlay read what these leave.
//
// An animation (set_char_ad, +0x200) is a list of frames, each cgd_type longs (4 or 6), the record's frame at cg_ix
// longs into it (+0x204); a frame's first word below 0x100 is a command instead (jumps, loops, tests of the lever
// and the like: the game's table of 125 at 0x0618A9D0, run as the game's code). A frame's longs are copied into the
// record from +0x214 on (setupCharTableData), and say:
//   +0x214 cg_ctr       the frames it is shown (char_move counts it down; at 0 the next frame or command)
//   +0x215 cg_type      what the step routines watch for (20 the move's end, 1 its moving part, 0xFF done...);
//                       with 0x80 a place to come back to (cg_wca_ix, +0x22C)
//   +0x216 cg_se        a sound (its low four bits the picture's flip and priority; 0x800 one of a list at random)
//   +0x218 cg_olc_ix    the frame's overlapping pictures (and in its low four bits a jump's offset, cg_jphos)
//   +0x21C cg_att_ix    the frame's attack (above six bits of other things; below 0 a new attack, that may hit
//                       again: set_new_attnum)
//   +0x21E cg_hit_ix    the frame's boxes (an index into the record's table of box sets, hit_ix_table +0x288; its
//                       low 13 bits also the moves it may be cancelled into, cg_meoshi +0x230)
//   +0x220 cg_extdat, +0x221 cg_cancel (what it may be cancelled into: 0x40 a super art, 0x20 a special move,
//                       0x10 a repeated press, 8 a chain, 4 a normal move, 2 a dash, 1 a high jump; fight_moves.cpp),
//                       +0x222 cg_effect and +0x223 cg_eftype (an effect started), +0x226 cg_rival (a throw's
//                       hold on the other), and in a frame of six, +0x228 cg_add_xy (a step across and up) and
//                       +0x22B cg_status (the pose, pat_status, with 0x80)
//
// The boxes: a frame's box set (cg_ja, eight words at +0x28C) gives, for each kind of box, which of the character's
// boxes are out (an index into the character's table of that kind); set_jugde_area (0x0608C4A4) turns those into
// pointers in the record (the hurt boxes +0x2A0, four, and the other hurt boxes +0x2A8, four; the throw's box +0x2B8;
// the box a throw catches +0x2C0; the attack's boxes +0x2C8, four; the push box +0x2D4): sfiii3.h's kBoxes, what the
// hit tests (fight_hits.cpp) and training mode's overlay read. The attack (set_new_attnum, 0x0608C2CC) is sixteen
// bytes from the character's table of attacks put at +0x2DC (att), its packed fields taken apart.
//
// The routines here: char_move (0x06089848: a frame of the animation), check_cgd_patdat (0x0608BD6A: a new frame
// taken up) and check_cgd_patdat2 (0x0608C0DC: the same without its sound and steps), set_new_attnum, set_jugde_area,
// set_char_move_init (0x060890E0) and set_char_move_init2 (0x060891E4: an animation begun, at its start or at a
// frame), exset_char_move_init (0x060892B8: another animation's frame taken over as it is), char_move_z, char_move_wca,
// char_move_index (a frame of it begun again), char_move_cmhs (the animation a hit's stop jumps to), setup_comm_back
// (where to come back to) and the sound's filter for Gill's change (0x0608C028).
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

    // --- in a record ---
    constexpr uint32_t kKind = 0x06;            // word: 1 a fighter (work_id)
    constexpr uint32_t kId = 0x08;              // word (id)
    constexpr uint32_t kFacing = 0x0A;          // byte: not 0 facing right (rl_flag)
    constexpr uint32_t kX = 0x64, kY = 0x68;    // longs: across and up, the whole parts in the high words (xyz)
    constexpr uint32_t kBeganAs = 0xAC;         // words: the animation's routine, index and frame begun (cmoa: koc,
                                                // ix, pat)
    constexpr uint32_t kComeBack = 0x12C;       // words: where a command's jump comes back to (cmbk)
    constexpr uint32_t kComeBack3 = 0x13C;      // words (cmb3)
    constexpr uint32_t kAfterHitStop = 0x144;   // words: the animation to go to as a hit's stop ends (cmhs)
    constexpr uint32_t kHitsMade = 0x17A, kHitsMade2 = 0x188; // words (cmwk[8], cmwk[15])
    constexpr uint32_t kAnimations = 0x1AC;     // longs, 12: the character's lists of animations, by routine (char_table)
    constexpr uint32_t kSoundLists = 0x1DC;     // long: lists of sounds to pick from (se_random_table)
    constexpr uint32_t kSteps = 0x1E0;          // long: the frames' steps across and up (step_xy_table)
    constexpr uint32_t kOverlapTable = 0x1EC;   // long (olc_ix_table)
    constexpr uint32_t kOverlap = 0x1F0;        // words, 4 (cg_olc)
    constexpr uint32_t kHoldTable = 0x1F8;      // long: a throw's holds (rival_catch_tbl)
    constexpr uint32_t kHold = 0x1FC;           // long: the hold now, or 0 (curr_rca)
    constexpr uint32_t kAnimation = 0x200;      // long: the animation (set_char_ad)
    constexpr uint32_t kFrame = 0x204;          // word: the frame in it, in longs (cg_ix)
    constexpr uint32_t kRoutine = 0x206;        // word: the list it is from (now_koc: 4 the normal moves...)
    constexpr uint32_t kIndex = 0x208;          // word: its place in the list (char_index)
    constexpr uint32_t kFrameSize = 0x20C;      // word: longs a frame (cgd_type: 4 or 6)
    constexpr uint32_t kPose = 0x20E;           // byte (pat_status)
    constexpr uint32_t kMoveKind = 0x20F;       // byte (kind_of_waza)
    constexpr uint32_t kFrameData = 0x214;      // the frame's longs from here (cg_ctr on, as above)
    constexpr uint32_t kHold_ = 0x214;          // byte: frames left of it (cg_ctr)
    constexpr uint32_t kCgType = 0x215;         // byte (cg_type)
    constexpr uint32_t kSound = 0x216;          // word (cg_se)
    constexpr uint32_t kOverlapIx = 0x218;      // word (cg_olc_ix)
    constexpr uint32_t kAttackIx = 0x21C;       // word (cg_att_ix)
    constexpr uint32_t kBoxSetIx = 0x21E;       // word (cg_hit_ix)
    constexpr uint32_t kEffect = 0x222, kEffectType = 0x223; // bytes (cg_effect, cg_eftype)
    constexpr uint32_t kRival = 0x226;          // word (cg_rival)
    constexpr uint32_t kStepIx = 0x228;         // word (cg_add_xy)
    constexpr uint32_t kNextFrame = 0x22A;      // byte: the frame to go to next, from 1 (cg_next_ix), or 0
    constexpr uint32_t kStatus = 0x22B;         // byte (cg_status)
    constexpr uint32_t kComeBackFrame = 0x22C;  // word (cg_wca_ix)
    constexpr uint32_t kJumpOffset = 0x22E;     // word (cg_jphos)
    constexpr uint32_t kCancels = 0x230;        // word: the moves it may be cancelled into (cg_meoshi)
    constexpr uint32_t kPriority = 0x232, kFlip = 0x233; // bytes (cg_prio, cg_flip)
    constexpr uint32_t kLastPicture = 0x234;    // word (old_cgnum)
    constexpr uint32_t kBoxSets = 0x288;        // long: the character's box sets, 16 bytes each (hit_ix_table)
    constexpr uint32_t kBoxSet = 0x28C;         // words, 8: the frame's (cg_ja: boix, bhix, haix, mf, caix, cuix,
                                                // atix, hoix)
    constexpr uint32_t kBodyTable = 0x29C, kBodyBoxes = 0x2A0;     // longs: the hurt boxes (body_adrs, h_bod: four)
    constexpr uint32_t kHandTable = 0x2A4, kHandBoxes = 0x2A8;     // (hand_adrs, h_han: four, by bhix + haix)
    constexpr uint32_t kCatchTable = 0x2B4, kCatchBox = 0x2B8;     // a throw's box (catch_adrs, h_cat)
    constexpr uint32_t kCaughtTable = 0x2BC, kCaughtBox = 0x2C0;   // the box a throw catches (caught_adrs, h_cau)
    constexpr uint32_t kAttackTable = 0x2C4, kAttackBoxes = 0x2C8; // the attack's boxes (attack_adrs, h_att: four)
    constexpr uint32_t kPushTable = 0x2D0, kPushBox = 0x2D4;       // (hosei_adrs, h_hos)
    constexpr uint32_t kAttacks = 0x2D8;        // long: the character's attacks, 16 bytes each (att_ix_table)
    constexpr uint32_t kAttack = 0x2DC;         // 16 bytes: the attack now (att: reaction, level, mkh_ix, but_ix,
                                                // dipsw, guard, dir, free, pow, impact, piyo, ng_type, hs_me, hs_you,
                                                // hit_mark, dmg_mark)
    constexpr uint32_t kOneHit = 0x2EC;         // word (zu_flag)
    constexpr uint32_t kElement = 0x2EE;        // word (at_attribute)
    constexpr uint32_t kChip = 0x2F0;           // word (kezuri_pow)
    constexpr uint32_t kArtsPoints = 0x2F2;     // word (add_arts_point)
    constexpr uint32_t kSlide = 0x2F6;          // word (att_zuru)
    constexpr uint32_t kAttackDir = 0x2FA;      // word (dir_atthit)
    constexpr uint32_t kHits = 0x2FC;           // word (vs_id)
    constexpr uint32_t kAttackLive = 0x2FE;     // byte (att_hit_ok)
    constexpr uint32_t kCancelHit = 0x2FF;      // byte (meoshi_hit_flag)
    constexpr uint32_t kAttackClass = 0x300;    // word (at_koa)
    constexpr uint32_t kNoKnockOut = 0x303;     // byte (no_death_attack)
    constexpr uint32_t kJumpAttack = 0x304;     // byte (jump_att_flag)
    constexpr uint32_t kRenewed = 0x306;        // word (renew_attack)
    constexpr uint32_t kAttackNumber = 0x308;   // word (attack_num)
    constexpr uint32_t kStarted = 0x366;        // byte (K5_init_flag)
    constexpr uint32_t kStepped = 0x367;        // byte (K5_exec_ok)
    constexpr uint32_t kOwner = 0x3BC;          // word: an effect's: 1 a fighter's (my_master's work_id)
    constexpr uint32_t kOwnerId = 0x3BE;        // word (master_id)
    constexpr uint32_t kHolding = 0x3CC;        // word: the character held (tsukami_num)
    constexpr uint32_t kWarned = 0x428;         // byte (caution_flag)
    constexpr uint32_t kFirstCancel = 0x429;    // byte (tc_1st_flag)
    constexpr uint32_t kAttacksLive = 0x458;    // word (total_att_hit_ok)
    constexpr uint32_t kAfterHitStopOk = 0x45C; // byte (hsjp_ok)
    constexpr uint32_t kAirNormal = 0x46E;      // byte (ja_nmj_rno)
    constexpr uint32_t kChanged = 0x471;        // byte: Gill changed (metamorphose)

    // --- the game's ---
    constexpr uint32_t kCommands = 0x0618A9D0;     // longs, 125: the animations' commands (decode_chcmd), r4 the
                                                   // record and r5 the command: r0 0 to stop for this frame
    constexpr uint32_t kJumpOffsets = 0x0618A9A8;  // words, 16 (jphos_table)
    constexpr uint32_t kChips = 0x0618A9C8;        // words (kezuri_pow_table)
    constexpr uint32_t kAttackClasses = 0x0618AC04; // words, by the move's kind (acatkoa_table)
    constexpr uint32_t kChangedSounds = 0x0618AC86; // words: Gill's sounds changed, from 0x160
    constexpr uint32_t kEffectStarts = 0x061B8BD0; // longs: an effect started (effinitjptbl), r4, r5 its type
    constexpr uint32_t kSoundCalls = 0x0618CCE4;   // longs: a sound asked for (sound_effect_request), r4, r5
    constexpr uint32_t kAttackRequest = 0x0200EBC0; // word: the last new attack's number (att_req)
    constexpr uint32_t kChangedFlags = 0x020690DD; // bytes, a player's record's +0x471 (plw[].metamorphose)
    constexpr uint32_t kTestFlag = 0x02000094;     // byte (test_flag)
    constexpr uint32_t kRandom = 0x0611E0EE;       // random_16 (lifts.cpp's NextRandom)
    constexpr uint32_t kDivideBy = 0x0612D584;     // the compiler's division, r1 by r0 (called as it is)
    constexpr uint32_t kCharMove = 0x06089848, kNewFrame = 0x0608BD6A, kNewFrame2 = 0x0608C0DC;
    constexpr uint32_t kNewAttack = 0x0608C2CC, kSetBoxes = 0x0608C4A4, kSoundFilter = 0x0608C028;
    constexpr uint32_t kSetMoveAt = 0x060891E4, kComeBackSet = 0x0608BCF6;
    constexpr uint32_t kGradeRenew = 0x060942F8;   // grade_add_att_renew (r4)
    constexpr uint32_t kGradeSame = 0x060949FA;    // grade_add_onaji_waza (r4 the player)
    constexpr uint32_t kAttackDirection = 0x0611E186; // cal_attdir (r4): r0
    constexpr uint32_t kGaugeOnAttack = 0x0611E3C4;   // add_sp_arts_gauge_init (r4)

    // The frame's longs copied into the record from +0x214 (setupCharTableData), cgd_type of them, from the
    // animation's frame. Leaves r2 the count, r3 cgd_type, r4 the count, r5 and r6 past the last copied, as the
    // game's loop does (and T set).
    void CopyFrame(Guest &g, uint32_t record) {
        uint32_t from = g.Read32(record + kAnimation) + (Extended(Word(g, record + kFrame)) << 2);
        uint32_t to = record + kFrameData;
        uint32_t n = 0;
        for (; static_cast<int32_t>(Extended(n)) < Word(g, record + kFrameSize); n++) {
            g.Write32(to, g.Read32(from));
            from += 4;
            to += 4;
        }
        g.R(2) = Extended(n);
        g.R(3) = Extended(Word(g, record + kFrameSize));
        g.R(4) = n;
        g.R(5) = from;
        g.R(6) = to;
        g.SetT(true);
    }

    // The frame's boxes and overlapping pictures, by its cg_olc_ix and cg_hit_ix, from the character's tables into the
    // record (cg_olc, cg_ja); leaves r0 the last word copied, r1 the box set's place in the record, r2 the table, r3
    // the set's place in it
    void CopyBoxSet(Guest &g, uint32_t record) {
        uint32_t from = (static_cast<uint32_t>(g.Read16(record + kOverlapIx)) << 3) + g.Read32(record + kOverlapTable);
        for (uint32_t n = 0; n < 8; n += 2) {
            g.Write16(record + kOverlap + n, g.Read16(from + n));
        }
        from = (static_cast<uint32_t>(g.Read16(record + kBoxSetIx)) << 4) + g.Read32(record + kBoxSets);
        for (uint32_t n = 0; n < 16; n += 2) {
            g.Write16(record + kBoxSet + n, g.Read16(from + n));
        }
        g.R(0) = Extended(Word(g, from + 14));
        g.R(1) = record + kBoxSet;
        g.R(2) = g.Read32(record + kBoxSets);
        g.R(3) = from;
    }

    // The frame's cg_att_ix and cg_hit_ix taken apart (both frames' kinds): the moves it may be cancelled into, the
    // attack (the high ten bits of the long the two words make) and the box set (its next nine), and a new attack
    // set. Leaves r0-r2 as the game's code (or what set_new_attnum leaves).
    void TakeApart(Guest &g, uint32_t record) {
        g.Write16(record + kCancels, static_cast<uint16_t>(Word(g, record + kBoxSetIx) & 0x1FFF));
        const uint32_t both = (static_cast<uint32_t>(g.Read16(record + kAttackIx)) << 16) | g.Read16(record + kBoxSetIx);
        g.Write16(record + kAttackIx, static_cast<uint16_t>(Word(g, record + kAttackIx) >> 6));
        const uint32_t shifted = both << 3;
        g.R(2) = 0x1FF;
        g.R(1) = record + kBoxSetIx;
        g.Write16(record + kBoxSetIx, static_cast<uint16_t>((shifted >> 16) & 0x1FF));
        g.R(0) = Extended(Word(g, record + kAttackIx));
        g.SetT(g.R(0) == 0);
        if (g.R(0) != 0) {
            g.R(4) = record;
            g.Call(kNewAttack);
        }
    }

    // The frame's jump offset, overlap index, picture's flip and priority, and its sound (one of a list at random)
    // taken from the words of the frame: as both kinds of frame do. Leaves r0-r4 and T as the game's code (r8 is the
    // game's own, put back).
    void TakeLooks(Guest &g, uint32_t record) {
        const uint32_t overlap = g.Read16(record + kOverlapIx);
        g.R(4) = 15;
        g.R(1) = record + kFlip;
        g.Write16(record + kJumpOffset, g.Read16(kJumpOffsets + ((overlap & 15) << 1)));
        g.Write16(record + kOverlapIx, static_cast<uint16_t>(overlap >> 4));
        g.Write8(record + kFlip, static_cast<uint8_t>(g.Read8(record + kSound + 1) & 3));
        g.Write8(record + kPriority, static_cast<uint8_t>((g.Read16(record + kSound) & 15) >> 2));
        g.R(0) = kSound;
        g.R(3) = static_cast<uint32_t>(g.Read16(record + kSound) >> 4);
        g.Write16(record + kSound, static_cast<uint16_t>(g.R(3)));
        g.R(2) = g.Read16(record + kSound);
        g.R(3) = 0x800;
        g.SetT((g.R(2) & 0x800) == 0);
        if (g.R(2) & 0x800) {
            // (one of a list, by the dice)
            const uint32_t list = g.Read32(g.Read32(record + kSoundLists) + ((g.R(2) & 0x7FF) << 2));
            g.R(2) = 0x7FF;
            g.R(0) = (g.R(2) & g.Read16(record + kSound)) << 2;
            g.R(1) = kRandom;
            g.Call(kRandom);
            g.R(0) <<= 1;
            g.R(3) = Extended(Word(g, list + g.R(0)));
            g.R(0) = kSound;
            g.Write16(record + kSound, static_cast<uint16_t>(g.R(3)));
        }
    }

    // A frame's end of things: a cg_type with 0x80 a place to come back to (cg_wca_ix), cg_type then 0. Leaves r0-r3
    // and T as the game's code (check_cgd_patdat2 clears cg_type with r2: `zeroR2`).
    void TakeComeBack(Guest &g, uint32_t record, bool zeroR2 = false) {
        g.R(0) = kCgType;
        g.R(2) = g.Read8(record + kCgType);
        g.R(3) = 0xFF;
        g.SetT(g.R(2) == 0xFF);
        if (g.R(2) == 0xFF) {
            return;
        }
        g.R(0) = g.R(2);
        g.SetT((g.R(0) & 0x80) == 0);
        if ((g.R(0) & 0x80) == 0) {
            return;
        }
        if (zeroR2) {
            g.R(2) = 0;
        }
        g.R(1) = record + kComeBackFrame;
        g.R(0) &= 0x7F;
        g.Write16(record + kComeBackFrame, static_cast<uint16_t>(g.R(0)));
        g.R(0) = kCgType;
        g.Write8(record + kCgType, 0);
    }

    // 0608BD6A (check_cgd_patdat): the record's new frame taken up: its longs copied in (above), in a frame of six its
    // step across (the way it faces) and up, and its pose; then (both kinds) the cancels, the attack and the box set,
    // an effect started; the jump offset, the overlap, the flip, the priority and the sound asked for; a fighter's hold
    // on the one it throws (a throw's frame's cg_rival: the hold table's entry by it and the character held); the
    // overlap's and the boxes' words (CopyBoxSet), the boxes placed (set_jugde_area); the place to come back to. Leaves
    // every register as the game's code (its callers' are unknown).
    bool NewFrame(Guest &g) {
        const uint32_t record = g.R(4);
        {
            Frame frame(g, 20); // (r14, r13, r8, pr and the long it works in)
            CopyFrame(g, record);
            const int16_t size = Word(g, record + kFrameSize);
            g.R(4) = g.R(15);
            g.R(0) = Extended(size);
            if (size == 6 || size == 4) {
                if (size == 6) {
                    g.R(0) = kStepIx;
                    g.R(1) = Extended(Word(g, record + kStepIx));
                    g.SetT(g.R(1) == 0);
                    if (g.R(1) != 0) {
                        // (a step across, the way it faces, and up: in 256ths)
                        const uint32_t step = g.Read32(record + kSteps) + (static_cast<uint32_t>(g.Read16(record + kStepIx)) << 1);
                        const uint32_t across = Extended(Word(g, step)) << 8, up = Extended(Word(g, step + 2)) << 8;
                        g.R(0) = ByteExtended(Byte(g, record + kFacing));
                        g.SetT(g.R(0) == 0);
                        g.R(5) = across;
                        const uint32_t x = g.Read32(record + kX);
                        g.Write32(record + kX, g.R(0) != 0 ? x + across : x - across);
                        g.R(6) = step + 2;
                        g.R(3) = up;
                        g.R(2) = g.Read32(record + kY) + up;
                        g.Write32(record + kY, g.R(2));
                        g.R(0) = kY;
                    }
                    g.R(0) = g.Read8(record + kStatus);
                    g.SetT((g.R(0) & 0x80) == 0);
                    if (g.R(0) & 0x80) {
                        g.R(1) = record + kPose;
                        g.R(0) = ByteExtended(Byte(g, record + kStatus)) & 0x7F;
                        g.Write8(record + kPose, static_cast<uint8_t>(g.R(0)));
                    }
                }
                g.R(3) = 0x1FFF;
                TakeApart(g, record);
                g.R(0) = ByteExtended(Byte(g, record + kEffect));
                g.SetT(g.R(0) == 0);
                if (g.R(0) != 0) {
                    g.R(3) = static_cast<uint32_t>(g.Read8(record + kEffect)) << 2;
                    g.R(2) = g.Read32(kEffectStarts + g.R(3));
                    g.R(0) = kEffectType;
                    g.R(5) = g.Read8(record + kEffectType);
                    g.R(4) = record;
                    g.Call(g.R(2));
                }
            }
            TakeLooks(g, record);
            g.R(0) = kSound;
            g.R(1) = Extended(Word(g, record + kSound));
            g.SetT(g.R(1) == 0);
            if (g.R(1) != 0) {
                g.R(2) = static_cast<uint32_t>(g.Read16(record + kSound)) << 2;
                const uint32_t call = g.Read32(kSoundCalls + g.R(2));
                g.R(0) = kSoundCalls;
                g.R(4) = record;
                g.Call(kSoundFilter);
                g.R(5) = g.R(0);
                g.R(4) = record;
                g.Call(call);
            }
            // (a fighter's hold on the one it throws)
            g.R(0) = Extended(Word(g, record + kKind));
            g.SetT(g.R(0) == 1);
            if (g.R(0) == 1) {
                g.R(0) = kRival;
                g.R(2) = Extended(Word(g, record + kRival));
                g.SetT(g.R(2) == 0);
                if (g.R(2) == 0) {
                    g.R(0) = kHold;
                    g.Write32(record + kHold, 0);
                } else {
                    g.R(3) = g.Read32(record + kHoldTable);
                    g.R(2) = ((g.Read16(record + kRival) + Extended(Word(g, record + kHolding)) - 24) << 3) + g.R(3);
                    g.R(0) = kHold;
                    g.Write32(record + kHold, g.R(2));
                }
            }
            CopyBoxSet(g, record);
            g.R(4) = record;
            g.Call(kSetBoxes);
            TakeComeBack(g, record);
        }
        g.Return(120);
        return true;
    }

    // 0608C0DC (check_cgd_patdat2): a frame taken up from the longs already in the record (exset_char_move_init's):
    // as check_cgd_patdat but with no frame copied, no step, no effect and no sound asked for (the sound's word taken
    // apart, one of a list picked all the same), and the overlap taken for every record. Leaves every register as the
    // game's code.
    [[maybe_unused]] bool NewFrame2(Guest &g) {
        const uint32_t record = g.R(4);
        {
            Frame frame(g, 16); // (r14, r8, pr and the long it works in)
            const int16_t size = Word(g, record + kFrameSize);
            g.R(0) = Extended(size);
            if (size == 6 || size == 4) {
                if (size == 6) {
                    g.R(0) = g.Read8(record + kStatus);
                    g.SetT((g.R(0) & 0x80) == 0);
                    if (g.R(0) & 0x80) {
                        g.R(1) = record + kPose;
                        g.R(0) = ByteExtended(Byte(g, record + kStatus)) & 0x7F;
                        g.Write8(record + kPose, static_cast<uint8_t>(g.R(0)));
                    }
                }
                g.R(3) = 0x1FFF;
                g.R(4) = g.R(15);
                TakeApart(g, record);
            }
            TakeLooks(g, record);
            g.R(0) = Extended(Word(g, record + kKind));
            g.SetT(g.R(0) == 1);
            if (g.R(0) == 1) {
                g.R(0) = kRival;
                g.R(2) = Extended(Word(g, record + kRival));
                g.SetT(g.R(2) == 0);
                if (g.R(2) == 0) {
                    g.R(3) = 0;
                    g.R(0) = kHold;
                    g.Write32(record + kHold, 0);
                } else {
                    g.R(3) = g.Read32(record + kHoldTable);
                    g.R(1) = ((g.Read16(record + kRival) + Extended(Word(g, record + kHolding)) - 24) << 3) + g.R(3);
                    g.R(0) = kHold;
                    g.Write32(record + kHold, g.R(1));
                }
            }
            CopyBoxSet(g, record);
            g.R(4) = record;
            g.Call(kSetBoxes);
            TakeComeBack(g, record, true);
        }
        g.Return(100);
        return true;
    }

    // 0608C028 (check_xcopy_filter_se_req): the sound a frame asks for (cg_se), changed for Gill changed (a fighter
    // with metamorphose, or an effect of such a fighter's): the sounds from 0x160 on by the game's table. r0 the sound;
    // leaves r5 the frame's word, r6 and r7 the table and its offset when they were looked at.
    bool SoundFilter(Guest &g) {
        const uint32_t record = g.R(4);
        g.R(5) = Extended(Word(g, record + kSound));
        g.R(2) = 0x160;
        const uint32_t sound = g.R(5) & 0xFFFF;
        g.R(3) = sound;
        g.SetT(static_cast<int32_t>(sound) >= 0x160);
        g.R(0) = sound;
        if (sound < 0x160) {
            g.Return(8);
            return true;
        }
        g.R(7) = 0xFFFFFEA0;
        g.R(6) = kChangedSounds;
        const auto changed = [&] {
            g.R(0) = ((static_cast<uint32_t>(g.Read16(record + kSound)) + g.R(7)) << 1);
            g.R(0) = g.Read16(kChangedSounds + g.R(0));
        };
        if (Word(g, record + kKind) == 1) {
            g.R(3) = ByteExtended(Byte(g, record + kChanged));
            g.SetT(g.R(3) == 0);
            if (g.R(3) != 0) {
                changed();
            }
        } else if (Word(g, record + kOwner) != 1) {
            g.R(0) = Extended(Word(g, record + kOwner));
            g.SetT(false);
            g.R(0) = sound;
        } else {
            g.R(3) = 1;
            g.R(2) = g.Read16(record + kOwnerId);
            g.SetT(static_cast<int32_t>(g.R(2)) > 1);
            if (static_cast<int32_t>(g.R(2)) <= 1) {
                g.R(3) = Extended(static_cast<int16_t>(g.R(2)) * 0x498);
                g.R(2) = ByteExtended(static_cast<int8_t>(g.Read8(kChangedFlags + g.R(3))));
                g.SetT(g.R(2) == 0);
                if (g.R(2) != 0) {
                    changed();
                }
            }
        }
        g.Return(20);
        return true;
    }

    // 0608C2CC (set_new_attnum): the frame's attack taken up (cg_att_ix, the index into the character's attacks):
    // renew_attack set; the attacks' count moved on (never 0); an index below 0 a new attack that may hit (its number,
    // att_hit_ok; for a fighter, caution_flag and the count of its attacks, and its grade's note); the sixteen bytes
    // copied in (att), and their packed parts taken apart: level's 0x80 one hit for the reaction (zu_flag), 0x40 a
    // jump's attack, its fifth and sixth bits the element, 8 no knock out, the level its low three; guard's high two the
    // chip's divisor (by the table), dir's high four the slide, piyo's the super art gauge it gives, ng_type's the kinds
    // it hits; the attack's direction worked out (cal_attdir); a new one adds to the gauge (add_sp_arts_gauge_init,
    // jumped to). (free: r0-r5 and T, which no caller reads: liveout.py)
    bool NewAttack(Guest &g) {
        const uint32_t record = g.R(4);
        bool fresh = false;
        {
            Frame frame(g, 12); // (r14, r13, pr)
            g.Write16(record + kRenewed, g.Read16(record + kAttackIx));
            uint16_t request = static_cast<uint16_t>((g.Read16(kAttackRequest) + 1) & 0x7FFF);
            g.Write16(kAttackRequest, request);
            if (request == 0) {
                g.Write16(kAttackRequest, static_cast<uint16_t>(g.Read16(kAttackRequest) + 1));
            }
            if (Word(g, record + kAttackIx) < 0) {
                g.Write16(record + kAttackIx, static_cast<uint16_t>(-Word(g, record + kAttackIx)));
                g.Write16(record + kAttackNumber, g.Read16(kAttackRequest));
                g.Write8(record + kAttackLive, 1);
                g.Write8(record + kCancelHit, 0);
                fresh = true;
                if (Word(g, record + kKind) == 1) {
                    g.Write8(record + kWarned, 1);
                    g.Write16(record + kAttacksLive, static_cast<uint16_t>(g.Read16(record + kAttacksLive) + 1));
                }
                g.R(4) = record;
                g.Call(kGradeRenew);
            }
            const uint32_t from = g.Read32(record + kAttacks) + (Extended(Word(g, record + kAttackIx)) << 4);
            for (uint32_t n = 0; n < 16; n++) {
                g.Write8(record + kAttack + n, g.Read8(from + n));
            }
            const uint8_t level = g.Read8(record + kAttack + 1);
            g.Write16(record + kOneHit, level & 0x80);
            g.Write8(record + kJumpAttack, level & 0x40);
            g.Write16(record + kElement, (level >> 4) & 3);
            g.Write8(record + kNoKnockOut, level & 8);
            g.Write8(record + kAttack + 1, level & 7);
            const uint8_t guard = g.Read8(record + kAttack + 5);
            g.Write16(record + kChip, g.Read16(kChips + (((guard >> 6) & 3) << 1)));
            g.Write8(record + kAttack + 5, guard & 0x3F);
            const uint8_t dir = g.Read8(record + kAttack + 6);
            g.Write16(record + kSlide, (dir >> 4) & 7);
            g.Write8(record + kAttack + 6, dir & 15);
            const uint8_t piyo = g.Read8(record + kAttack + 10);
            g.Write16(record + kArtsPoints, piyo >> 4);
            g.Write8(record + kAttack + 10, piyo & 15);
            const uint8_t kinds = g.Read8(record + kAttack + 11);
            g.Write16(record + kHits, kinds >> 4);
            g.Write8(record + kAttack + 11, kinds & 15);
            g.R(4) = record;
            g.Call(kAttackDirection);
            g.Write16(record + kAttackDir, static_cast<uint16_t>(g.R(0)));
        }
        if (fresh) {
            g.R(4) = record;
            g.Call(kGaugeOnAttack);
        }
        g.Return(100);
        return true;
    }

    // 0608C4A4 (set_jugde_area): the frame's boxes placed: each kind's pointer in the record (sfiii3.h's kBoxes) the
    // character's table of that kind plus the box set's index for it (the hurt boxes and the attack's four boxes of
    // eight bytes, 32 a set; a throw's, the caught one's and the push box one of eight; the other hurt boxes by bhix
    // and haix together). Leaves r0 0x2A8, r2 the other hurt boxes' table, r3 their pointer.
    bool SetBoxes(Guest &g) {
        const uint32_t record = g.R(4);
        const auto index = [&](uint32_t n) { return static_cast<uint32_t>(g.Read16(record + kBoxSet + n * 2)); };
        g.Write32(record + kBodyBoxes, g.Read32(record + kBodyTable) + (index(0) << 5));
        g.Write32(record + kCatchBox, g.Read32(record + kCatchTable) + (index(4) << 3));
        g.Write32(record + kCaughtBox, g.Read32(record + kCaughtTable) + (index(5) << 3));
        g.Write32(record + kAttackBoxes, g.Read32(record + kAttackTable) + (index(6) << 5));
        g.Write32(record + kPushBox, g.Read32(record + kPushTable) + (index(7) << 3));
        g.R(2) = g.Read32(record + kHandTable);
        g.R(3) = ((index(1) + index(2)) << 5) + g.R(2);
        g.Write32(record + kHandBoxes, g.R(3));
        g.R(0) = kHandBoxes;
        g.Return(30);
        return true;
    }

    // 06089848 (char_move): a frame of the record's animation: K5_exec_ok, and a frame less of the one shown; when it
    // runs out the next (cg_next_ix's, or the one after), its commands run (each in the game's table, until one says to
    // stop for this frame) and the frame taken up (check_cgd_patdat, jumped to). Leaves every register as the game's
    // code.
    bool CharMove(Guest &g) {
        const uint32_t record = g.R(4);
        g.Write8(record + kStepped, 1);
        g.R(3) = static_cast<uint32_t>(g.Read8(record + kHold_)) - 1;
        g.Write8(record + kHold_, static_cast<uint8_t>(g.R(3)));
        g.R(3) &= 0xFF;
        g.R(0) = kHold_;
        g.SetT(g.R(3) == 0);
        if (g.R(3) != 0) {
            g.Return(10);
            return true;
        }
        bool take = false;
        {
            Frame frame(g, 16); // (r14, r13, pr, macl)
            const uint8_t next = g.Read8(record + kNextFrame);
            if (next != 0) {
                g.R(2) = Extended(Word(g, record + kFrameSize));
                g.R(3) = static_cast<uint32_t>(static_cast<int16_t>(g.R(2)) * static_cast<int16_t>(next - 1));
                g.Write16(record + kFrame, static_cast<uint16_t>(g.R(3)));
            } else {
                g.R(3) = Extended(Word(g, record + kFrameSize));
                g.R(2) = Extended(Word(g, record + kFrame)) + g.R(3);
                g.Write16(record + kFrame, static_cast<uint16_t>(g.R(2)));
            }
            g.R(0) = kFrame;
            for (;;) {
                const uint32_t at = g.Read32(record + kAnimation) + (Extended(Word(g, record + kFrame)) << 2);
                g.R(0) = kAnimation;
                g.R(2) = g.Read16(at);
                g.R(3) = 0x100;
                g.SetT(g.R(2) >= 0x100);
                if (g.R(2) >= 0x100) {
                    take = true; // (a frame: taken up)
                    break;
                }
                // (a command)
                g.R(0) = kCommands;
                g.R(5) = at;
                g.R(2) <<= 2;
                g.R(1) = g.Read32(kCommands + g.R(2));
                g.R(4) = record;
                g.Call(g.R(1));
                g.SetT(g.R(0) == 0);
                if (g.R(0) == 0) {
                    break; // (it stops here this frame)
                }
                g.R(0) = kFrame;
                g.R(3) = Extended(Word(g, record + kFrameSize));
                g.R(2) = Extended(Word(g, record + kFrame)) + g.R(3);
                g.Write16(record + kFrame, static_cast<uint16_t>(g.R(2)));
            }
        }
        if (take) {
            g.R(4) = record;
            g.Call(kNewFrame);
        }
        g.Return(40);
        return true;
    }

    // An animation begun (both set_char_move_inits): the routine's list and index noted, the animation (the
    // character's list's entry), the frame's longs cleared, the two longs before the animation (its frame size, pose,
    // kind of move...) put before them. Leaves r0 0x20C, r1 and r5 as the routines do (by `pointer`), r2 and r3 the two
    // longs, r6 the animation less 8.
    void BeginAnimation(Guest &g, uint32_t record, uint32_t routine, uint32_t index) {
        g.Write16(record + kRoutine, static_cast<uint16_t>(routine));
        g.Write16(record + kIndex, static_cast<uint16_t>(index));
        const uint32_t list = g.Read32(record + kAnimations + (Extended(static_cast<int32_t>(routine)) << 2));
        const uint32_t animation = g.Read32(list + (Extended(static_cast<int32_t>(index)) << 2));
        g.Write32(record + kAnimation, animation);
        for (uint32_t n = 0; n < 24; n += 4) {
            g.Write32(record + kFrameData + n, 0);
        }
        g.R(3) = g.Read32(animation - 4);
        g.Write32(record + kFrameData - 4, g.R(3));
        g.R(2) = g.Read32(animation - 8);
        g.Write32(record + kFrameData - 8, g.R(2));
        g.R(6) = animation - 8;
        g.R(0) = kFrameSize;
    }

    // The kind of attack by the move's kind (at_koa), for a fighter or effect (work_id's low four bits). Leaves r0, r2,
    // r3 as the game's code where it is set.
    void AttackClass(Guest &g, uint32_t record) {
        g.R(0) = Extended(Word(g, record + kKind));
        g.SetT((g.R(0) & 15) == 0);
        if (g.R(0) & 15) {
            g.R(3) = static_cast<uint32_t>(g.Read8(record + kMoveKind)) << 1;
            g.R(2) = Extended(Word(g, kAttackClasses + g.R(3)));
            g.R(0) = kAttackClass;
            g.Write16(record + kAttackClass, static_cast<uint16_t>(g.R(2)));
        }
    }

    // 060890E0 (set_char_move_init): an animation begun at its start (r5 the routine's list, r6 the index): the frame
    // before the first (so char_move takes up the first), shown one frame; the place to come back to, the pictures'
    // and the hits' counts cleared; cmoa noted; the kind of attack; for a fighter tc_1st_flag and ja_nmj_rno cleared,
    // and a normal or special move noted for its grade; then the first frame (char_move, jumped to). Leaves every
    // register as the game's code.
    bool SetMove(Guest &g) {
        const uint32_t record = g.R(4);
        const uint32_t routine = g.R(5), index = g.R(6);
        BeginAnimation(g, record, routine, index);
        g.R(5) = record + kFrameData - 8;
        g.Write16(record + kFrame, static_cast<uint16_t>(-Word(g, record + kFrameSize)));
        g.Write8(record + kHold_, 1);
        g.Write8(record + kNextFrame, 0);
        g.Write16(record + kLastPicture, 0);
        g.Write16(record + kComeBackFrame, 0);
        g.Write16(record + kBeganAs, g.Read16(record + kRoutine));
        g.R(2) = Extended(Word(g, record + kIndex));
        g.Write16(record + kBeganAs + 2, static_cast<uint16_t>(g.R(2)));
        g.Write16(record + kBeganAs + 4, 1);
        g.Write16(record + kHitsMade, 0);
        g.Write16(record + kHitsMade2, 0);
        g.R(6) = 6;
        AttackClass(g, record);
        g.R(0) = Extended(Word(g, record + kKind));
        g.SetT(g.R(0) == 1);
        if (g.R(0) == 1) {
            g.Write8(record + kFirstCancel, 0);
            const int16_t now = Word(g, record + kRoutine);
            g.R(0) = Extended(now);
            g.R(4) = g.R(0);
            g.SetT(now == 4 || now == 5);
            if (now == 4 || now == 5) {
                g.R(0) = Extended(Word(g, record + kId));
                g.R(3) = kGradeSame;
                g.R(4) = g.R(0);
                {
                    Frame frame(g, 16); // (r14, r13, r12, pr)
                    g.Call(kGradeSame);
                }
            }
            g.Write8(record + kAirNormal, 0);
        }
        g.R(0) = kStarted;
        g.R(4) = record;
        g.Write8(record + kStarted, 1);
        g.Call(kCharMove);
        g.Return(60);
        return true;
    }

    // 060891E4 (set_char_move_init2): an animation begun at a frame (r5 the list, r6 the index, r7 the frame from 1;
    // the long on the stack not 0 to keep the pose and the kind of move it had): as set_char_move_init, but cmoa noted
    // only if none is, nothing for a fighter. Leaves every register as the game's code.
    bool SetMoveAt(Guest &g) {
        const uint32_t record = g.R(4);
        const uint32_t routine = g.R(5), index = g.R(6), frameNumber = g.R(7);
        const bool keep = Word(g, g.R(15) + 2) != 0;
        const uint8_t pose = g.Read8(record + kPose), kind = g.Read8(record + kMoveKind);
        BeginAnimation(g, record, routine, index);
        g.R(1) = record + kFrameData - 8;
        g.R(6) = Extended(Word(g, record + kFrameSize));
        g.R(7) = static_cast<uint32_t>(static_cast<int16_t>(g.R(6)) * static_cast<int16_t>(frameNumber - 1)) - g.R(6);
        g.Write16(record + kFrame, static_cast<uint16_t>(g.R(7)));
        g.R(6) = 1;
        g.R(5) = 0;
        g.Write8(record + kHold_, 1);
        g.Write8(record + kNextFrame, 0);
        g.Write16(record + kLastPicture, 0);
        g.Write16(record + kComeBackFrame, 0);
        g.R(0) = kBeganAs + 4;
        g.R(3) = Extended(Word(g, record + kBeganAs + 4));
        if (g.R(3) == 0) {
            g.R(2) = Extended(Word(g, record + kRoutine));
            g.Write16(record + kBeganAs, static_cast<uint16_t>(g.R(2)));
            g.R(3) = Extended(Word(g, record + kIndex));
            g.Write16(record + kBeganAs + 2, static_cast<uint16_t>(g.R(3)));
            g.Write16(record + kBeganAs + 4, 1);
        }
        if (keep) {
            g.R(3) = ByteExtended(static_cast<int8_t>(pose));
            g.Write8(record + kPose, pose);
            g.R(1) = record + kMoveKind;
            g.Write8(record + kMoveKind, kind);
        }
        AttackClass(g, record);
        g.R(0) = kStarted;
        g.Write8(record + kStarted, 1);
        g.Call(kCharMove);
        g.Return(60);
        return true;
    }

    // 060892B8 (exset_char_move_init): another animation (r5, r6) taken over at the frame the record is at, as it is: its
    // frame's longs copied in but the frames left of the one shown kept; cmoa noted; the frame taken up
    // (check_cgd_patdat2, jumped to). Leaves every register as the game's code.
    [[maybe_unused]] bool SetMoveData(Guest &g) {
        const uint32_t record = g.R(4);
        g.Write16(record + kRoutine, static_cast<uint16_t>(g.R(5)));
        g.Write16(record + kIndex, static_cast<uint16_t>(g.R(6)));
        const uint32_t list = g.Read32(record + kAnimations + (Extended(static_cast<int32_t>(g.R(5))) << 2));
        g.Write32(record + kAnimation, g.Read32(list + (Extended(static_cast<int32_t>(g.R(6))) << 2)));
        const uint8_t shown = g.Read8(record + kHold_);
        CopyFrame(g, record); // (r5 past the animation's longs read, r6 past the record's written: here r6 and r7)
        g.Write8(record + kHold_, shown);
        g.R(7) = g.R(6);
        g.R(6) = g.R(5);
        g.R(5) = 1;
        g.R(4) = record;
        g.Write16(record + kBeganAs, g.Read16(record + kRoutine));
        g.R(2) = Extended(Word(g, record + kIndex));
        g.Write16(record + kBeganAs + 2, static_cast<uint16_t>(g.R(2)));
        g.R(3) = kNewFrame2;
        g.Write16(record + kBeganAs + 4, 1);
        g.R(0) = kStarted;
        g.Write8(record + kStarted, 1);
        g.Call(kNewFrame2);
        g.Return(50);
        return true;
    }

    // 06089350 (char_move_z): the frame shown again from its first frame (the next frame forgotten with test_flag
    // on), then char_move
    bool CharMoveZ(Guest &g) {
        const uint32_t record = g.R(4);
        g.R(5) = 1;
        g.R(3) = kTestFlag;
        g.R(0) = g.Read8(kTestFlag);
        if (g.R(0) != 0) {
            g.R(2) = 0;
            g.Write8(record + kNextFrame, 0);
        }
        g.Write8(record + kHold_, 1);
        g.R(0) = kStarted;
        g.Write8(record + kStarted, 1);
        g.Call(kCharMove);
        g.Return(20);
        return true;
    }

    // A frame begun again at the one numbered (from 1): cg_next_ix cleared, cg_ix before it, shown one frame,
    // K5_init_flag (char_move_wca, char_move_index). Leaves r0 0x366, r3 0, r5 1, as the game's code; gives the
    // frame size and cg_ix.
    uint32_t FrameAt(Guest &g, uint32_t record, uint32_t number, uint32_t &size) {
        g.R(3) = 0;
        g.Write8(record + kNextFrame, 0);
        size = Extended(Word(g, record + kFrameSize));
        const uint32_t at = static_cast<uint32_t>(static_cast<int16_t>(size) * static_cast<int16_t>(number - 1)) - size;
        g.Write16(record + kFrame, static_cast<uint16_t>(at));
        g.R(5) = 1;
        g.Write8(record + kHold_, 1);
        g.R(0) = kStarted;
        g.Write8(record + kStarted, 1);
        return at;
    }

    // 06089378 (char_move_wca): the animation taken up again at the place to come back to (cg_wca_ix), then char_move
    bool CharMoveWca(Guest &g) {
        const uint32_t record = g.R(4);
        uint32_t size = 0;
        g.R(2) = FrameAt(g, record, Extended(Word(g, record + kComeBackFrame)), size);
        g.Call(kCharMove);
        g.Return(20);
        return true;
    }

    // 060893D8 (char_move_index): the animation taken up again at the frame r5 (from 1), then char_move; r6 left the
    // frame size
    bool CharMoveIndex(Guest &g) {
        const uint32_t record = g.R(4);
        uint32_t size = 0;
        FrameAt(g, record, g.R(5), size);
        g.R(6) = size;
        g.Call(kCharMove);
        g.Return(20);
        return true;
    }

    // 06089812 (char_move_cmhs): as a hit's stop ends with hsjp_ok, where the animation was is noted (setup_comm_back)
    // and the one cmhs names begun at its frame (set_char_move_init2)
    [[maybe_unused]] bool CharMoveAfterHitStop(Guest &g) {
        const uint32_t record = g.R(4);
        {
            Frame frame(g, 8); // (r14, pr)
            g.R(0) = kAfterHitStopOk;
            g.R(3) = ByteExtended(Byte(g, record + kAfterHitStopOk));
            g.SetT(g.R(3) == 0);
            if (g.R(3) != 0) {
                g.R(3) = kComeBackSet;
                g.R(4) = record;
                g.Call(kComeBackSet);
                g.R(2) = 0;
                g.R(0) = kAfterHitStopOk;
                g.R(3) = 0;
                g.Write8(record + kAfterHitStopOk, 0);
                Frame scf(g, 4);
                g.Write32(g.R(15), 0);
                g.R(7) = Extended(Word(g, record + kAfterHitStop + 4));
                g.R(6) = Extended(Word(g, record + kAfterHitStop + 2));
                g.R(5) = Extended(Word(g, record + kAfterHitStop));
                g.R(0) = kAfterHitStop;
                g.R(4) = record;
                g.Call(kSetMoveAt);
            }
        }
        g.Return(20);
        return true;
    }

    // A place to come back to (cmbk, cmb3: the routine's list, the index, the frame after this one from 1), the frame's
    // number by the game's division (r1 by r0). Leaves r0 the number, r1 its place, r2 the division, r3 the index.
    void ComeBackTo(Guest &g, uint32_t record, uint32_t place) {
        g.Write16(place, g.Read16(record + kRoutine));
        g.R(2) = kDivideBy;
        g.R(3) = Extended(Word(g, record + kIndex));
        g.Write16(place + 2, static_cast<uint16_t>(g.R(3)));
        g.R(1) = Extended(Word(g, record + kFrame));
        g.R(0) = Extended(Word(g, record + kFrameSize));
        {
            Frame frame(g, 4); // (pr)
            g.Call(kDivideBy);
        }
        g.R(0) += 2;
        g.R(1) = g.R(4) + (place - record) + 4;
        g.Write16(g.R(1), static_cast<uint16_t>(g.R(0)));
    }

    // 0608BCF6 (setup_comm_back): K5_init_flag, and where the animation is noted to come back to (cmbk)
    bool ComeBack(Guest &g) {
        const uint32_t record = g.R(4);
        g.R(3) = 1;
        g.Write8(record + kStarted, 1);
        ComeBackTo(g, record, record + kComeBack);
        g.Return(30);
        return true;
    }

    // 0608BD40 (setup_comm_abbak): where the animation is, noted to come back to (cmb3)
    bool ComeBack3(Guest &g) {
        const uint32_t record = g.R(4);
        ComeBackTo(g, record, record + kComeBack3);
        g.Return(30);
        return true;
    }

    // (free: what liveout.py shows no caller reads; the others leave every register as the game's code)
    const Lift kLifts[] = {
        {"CharMove", 0x06089848, 0x74, 0x6888F6634000EDB3ull, CharMove, true},
        {"NewFrame", 0x0608BD6A, 0x46, 0xE6F2A9C9FC873B84ull, NewFrame, true},
        // {"NewFrame2", 0x0608C0DC, 0x7C, 0xD99D10BF70A53419ull, NewFrame2, true}, (not reached by any check yet: left to the game\'s code)
        {"SoundFilter", 0x0608C028, 0x8A, 0x3C999D32713112F1ull, SoundFilter, true},
        {"NewAttack", 0x0608C2CC, 0x178, 0x045204C5324D8A4Bull, NewAttack, true, 0x1003F},
        {"SetBoxes", 0x0608C4A4, 0x88, 0x2D56E9A28BD3133Eull, SetBoxes, true},
        {"SetMove", 0x060890E0, 0xE0, 0x6107DEBD33FB159Eull, SetMove, true},
        {"SetMoveAt", 0x060891E4, 0xD4, 0x96253040DBFA2E16ull, SetMoveAt, true},
        // {"SetMoveData", 0x060892B8, 0x72, 0x5CA395A56B0CDA53ull, SetMoveData, true}, (not reached by any check yet: left to the game\'s code)
        {"CharMoveZ", 0x06089350, 0x1A, 0x769F41A893845161ull, CharMoveZ, true},
        {"CharMoveWca", 0x06089378, 0x2A, 0xDFA5346F3D7E85D4ull, CharMoveWca, true},
        {"CharMoveIndex", 0x060893D8, 0x26, 0x71AEEDA3C4BBE442ull, CharMoveIndex, true},
        // {"CharMoveAfterHitStop", 0x06089812, 0x36, 0xFDD2DC45AFE5B91Dull, CharMoveAfterHitStop, true}, (not reached by any check yet: left to the game\'s code)
        {"ComeBack", 0x0608BCF6, 0x30, 0xB61202223C7144FFull, ComeBack, true},
        {"ComeBack3", 0x0608BD40, 0x2A, 0xEFD3B0D3D1821A3Full, ComeBack3, true},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightBoxes() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
