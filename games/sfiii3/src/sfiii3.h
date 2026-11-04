// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's names: the places in main RAM (SH-2 addresses) the port's features use, with what each was found from.
#pragma once

#include <cstdint>

namespace sfiii3 {

// The credits (a byte): 0 at start-up, 1 and 2 after one and two coins (RAM compared before and after coins in
// attract mode); with it held at 1, Start goes to character select with no coin
constexpr uint32_t kCredits = 0x02007CE0;

// The round's clock (a byte, in seconds): 78, 72 and 61 where the screen showed those (RAM at three frames of a
// fight); held at 99 the clock shows 99 (98 for the moment before it is put back)
constexpr uint32_t kClock = 0x02011377;

// --- the fight (facts from Grouflon's 3rd_training_lua, made for the 990512 set; checked on this 990608 set where
// marked) ---

// A round is on: a character locked in by either side (0xFF) and the match's state 2 (players can move)
constexpr uint32_t kLocked[2] = {0x020154C6, 0x020154C8};
constexpr uint32_t kMatchState = 0x020154A7;
// Character select, each player: 0 no player, 1 coming in, 2 choosing a character, 3 the super art's coming, 4
// choosing it, 5 chosen (and so through the match)
constexpr uint32_t kSelectState[2] = {0x0201553D, 0x02015545};
// ... the super art the cursor is on (0-2) and the colours chosen (0-6) (Grouflon's 3rd_training_lua)
constexpr uint32_t kSelectArt[2] = {0x020154D3, 0x020154D5};
constexpr uint32_t kSelectColour[2] = {0x02015683, 0x02015684};
// The camera: the middle of the picture in the stage (signed words)
constexpr uint32_t kCameraX = 0x02026CB0, kCameraY = 0x02026CB4;
// ... and the ends it keeps the camera between in the stage (words, each twice; 272 and 748 on Ken's stage)
constexpr uint32_t kCameraRight[2] = {0x02026CF4, 0x02026CF6};
constexpr uint32_t kCameraLeft[2] = {0x02026CF8, 0x02026CFA};
// The players' records (checked: life and position change as the fight's pictures do)
constexpr uint32_t kPlayer[2] = {0x02068C6C, 0x02069104};
// ... and in them (the game's own names, as the PlayStation 2 version's decompilation has them, in brackets: the
// routines that set them are written again in fight_moves.cpp and fight_boxes.cpp)
constexpr uint32_t kFacing = 0x0A;          // byte: 0 facing left (the sprites' own way), else right (rl_flag)
constexpr uint32_t kFreeze = 0x45;          // byte: frames of hit freeze left (the low byte of kHitStop)
constexpr uint32_t kKnockedOut = 0x3FD;     // byte: knocked out (the round's knock-out routine, 0x06117D28, reads it)
constexpr uint32_t kX = 0x64, kY = 0x68;    // signed words: in the stage, y up from the floor (xyz[0], xyz[1])
constexpr uint32_t kLife = 0x9F;            // byte: 160 full (the low byte of vital_new)
constexpr uint32_t kBusy = 0x3D1;           // word: low byte not 0 while busy (old_gdflag, guard_flag)
constexpr uint32_t kBlocking = 0x3D3;       // byte: 1-4 while guarding (guard_chuu)
constexpr uint32_t kAttacking = 0x428;      // byte: not 0 while attacking (caution_flag: set at an attack's first step,
                                            // cleared by the normal steps)
constexpr uint32_t kThrown = 0x3CF;         // byte: not 0 while being thrown (tsukamare_f)
constexpr uint32_t kInputCapacity = 0x46C;  // word: 0 while the player's input is not taken (permited_koa)
constexpr uint32_t kRecoveryTime = 0x187;   // byte
constexpr uint32_t kHitCount = 0x33E;       // word: hits taken, all told (dm_count_up)
// What it is doing (routine_no[1]: 0 normal steps, 1 hurt, 2 throwing, 3 thrown, 4 attacking), which (routine_no[2]: in
// an attack below 16 a normal move, from 16 the character's own specials and super arts) and its step in that
// (routine_no[3]: 0 as it begins)
constexpr uint32_t kDoing = 0x26, kWhich = 0x28, kStep = 0x2A; // words
constexpr uint32_t kHitStop = 0x44;         // word: frames held still by a hit, given or taken (hit_stop: below 0 the
                                            // animation runs on)
constexpr uint32_t kHurtStop = 0x32E;       // word: the hit stop its being hit gives it (dm_stop: 0xFFF1, -15, a parry's)
constexpr uint32_t kConnection = kHurtStop;
// The animation: its address (set_char_ad), the frame in it (cg_ix, in longs), the list and index it is from (now_koc:
// 4 the normal moves, 5 the specials...; char_index), the longs a frame (cgd_type, 4 or 6)
constexpr uint32_t kAnimation = 0x200, kAnimationFrame = 0x204, kAnimationList = 0x206, kAnimationIndex = 0x208;
// ... the frame's own (copied in as each is taken up): the frames it is shown (cg_ctr, counted down), its type (cg_type:
// 20 a move's end, 1 its moving part), its cancels (cg_cancel: 0x40 a super art, 0x20 a special move, 0x10 a repeated
// press, 8 a chain, 4 a normal move, 2 a dash, 1 a high jump; the special and super cancels need kCancelHit)
constexpr uint32_t kFrameHold = 0x214, kFrameType = 0x215, kFrameCancels = 0x221; // bytes
constexpr uint32_t kCancelHit = 0x2FF;      // byte: its attack has hit or been guarded (meoshi_hit_flag)
constexpr uint32_t kCancelTimer = 0x3E5;    // byte: frames any special or super art may still be done over a move's start
                                            // (cancel_timer)
// The frame's box set (cg_ja: eight words, each an index into the character's table of that kind of box: the hurt
// boxes, the second hurt boxes' two, a third, the throw's, the caught one's, the attack's, the push box's); set_jugde_area
// (0x0608C4A4) turns them into the pointers below, each the table (the word before it) plus the index
constexpr uint32_t kBoxSet = 0x28C;
constexpr uint32_t kHurtBoxes = 0x2A0;      // long: four boxes (h_bod)
constexpr uint32_t kHurtBoxes2 = 0x2A8;     // long: four (h_han: the hands', by bhix + haix)
constexpr uint32_t kThrowBox = 0x2B8;       // long: one (h_cat)
constexpr uint32_t kThrowableBox = 0x2C0;   // long: one (h_cau)
constexpr uint32_t kAttackBoxes = 0x2C8;    // long: four (h_att)
constexpr uint32_t kPushBox = 0x2D4;        // long: one (h_hos)
// The frame's attack (att: 16 bytes from the character's table of them by the frame's cg_att_ix, set_new_attnum
// 0x0608C2CC): +0 the reaction, +1 the level (0-7), +5 how it is guarded and parried, +6 its direction, +8 its power,
// +10 the stun, +11 the throw's kind, +12 and +13 the hit stops for the attacker and the one hit
constexpr uint32_t kAttack = 0x2DC;
constexpr uint32_t kAttackLive = 0x2FE;     // byte: the attack may still hit (att_hit_ok)
// The boxes: pointers to lists of boxes of 8 bytes (left, width, bottom, height: signed words, from the player's
// place, x turned with the facing)
struct BoxList {
    uint32_t offset;
    int count;
    int kind; // fight::BoxKind
};
constexpr BoxList kBoxes[] = {
    {kPushBox, 1, 2},      // push
    {kHurtBoxes, 4, 0},    // hurt
    {kHurtBoxes2, 4, 0},   // hurt (extended)
    {kAttackBoxes, 4, 1},  // attack
    {kThrowBox, 1, 3},     // throw
    {kThrowableBox, 1, 4}, // throwable
};
constexpr int kFullLife = 160;
// The super art meter: the gauge's fill, and the count of full bars (the second is the one the game keeps; the first
// follows it); the most of each for the art chosen; a flag that has the game redraw it
constexpr uint32_t kGauge[2] = {0x020695B5, 0x020695E1};
constexpr uint32_t kMeterCount[2] = {0x020695BF, 0x020695EB};
constexpr uint32_t kMeterCountShown[2] = {0x020286AB, 0x020286DF};
constexpr uint32_t kMaxGauge[2] = {0x020695B3, 0x020695DF};
constexpr uint32_t kMaxMeterCount[2] = {0x020695BD, 0x020695E9};
constexpr uint32_t kMeterUpdate[2] = {0x020157C8, 0x020157C9};
// Stun: the most a player can take before being stunned (a byte), its timer two bytes on, and how much has been taken
// (the high byte of the long six bytes on)
constexpr uint32_t kStunMost[2] = {0x020695F7, 0x0206960B};
// Parrying: each player's four windows (forward, down, in the air, anti-air), each a byte counting the frames it stays
// open (player 2's 0x406 on); the "connection" word of the record (kHurtStop, above) reads 0xFFF1 the frame a parry
// lands
constexpr uint32_t kParryWindow[4] = {0x02026335, 0x02026337, 0x02026339, 0x02026347};
constexpr uint32_t kParryWindowPlayer2 = 0x406;

// The stage of the next fight (a byte): a character's number, its stage (written before the fight's stage is set up, it
// is the stage fought on: Elena's 8, from the training script's notes; all 21 seen in pictures, Gill 0 to Remy 20)
constexpr uint32_t kStage = 0x020154F5;

// The character each player chose (a byte each, its number as in the record's 0x3C0: Gill 0, Alex 1...), from the
// training script; written while no match is on, it is the character fought with (Gill, the boss, too: seen in pictures)
constexpr uint32_t kChosen[2] = {0x02011387, 0x02011388};
// Rounds won in the match now, each player (a byte; P1's 0x02011383, P2's two on), and the round's number (from 0):
// seen in RAM over versus matches (a win counted at the match's state 6, after the knock-out's 3)
constexpr uint32_t kRoundsWon[2] = {0x02011383, 0x02011385};
constexpr uint32_t kRoundNumber = 0x0201138A;

} // namespace sfiii3
