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
// The camera: the middle of the picture in the stage (signed words)
constexpr uint32_t kCameraX = 0x02026CB0, kCameraY = 0x02026CB4;
// ... and the ends it keeps the camera between in the stage (words, each twice; 272 and 748 on Ken's stage)
constexpr uint32_t kCameraRight[2] = {0x02026CF4, 0x02026CF6};
constexpr uint32_t kCameraLeft[2] = {0x02026CF8, 0x02026CFA};
// The players' records (checked: life and position change as the fight's pictures do)
constexpr uint32_t kPlayer[2] = {0x02068C6C, 0x02069104};
// ... and in them
constexpr uint32_t kFacing = 0x0A;          // byte: 0 facing left (the sprites' own way), else right
constexpr uint32_t kFreeze = 0x45;          // byte: frames of hit freeze left
constexpr uint32_t kX = 0x64, kY = 0x68;    // signed words: in the stage, y up from the floor
constexpr uint32_t kLife = 0x9F;            // byte: 160 full
constexpr uint32_t kBusy = 0x3D1;           // word: low byte not 0 while busy
constexpr uint32_t kBlocking = 0x3D3;       // byte: 1-4 while guarding
constexpr uint32_t kAttacking = 0x428;      // byte: not 0 while attacking
constexpr uint32_t kThrown = 0x3CF;         // byte: not 0 while being thrown
constexpr uint32_t kInputCapacity = 0x46C;  // word: 0 while the player's input is not taken
constexpr uint32_t kRecoveryTime = 0x187;   // byte
constexpr uint32_t kHitCount = 0x33E;       // word: hits taken, all told
// The boxes: pointers to lists of boxes of 8 bytes (left, width, bottom, height: signed words, from the player's
// place, x turned with the facing)
struct BoxList {
    uint32_t offset;
    int count;
    int kind; // fight::BoxKind
};
constexpr BoxList kBoxes[] = {
    {0x2D4, 1, 2}, // push
    {0x2A0, 4, 0}, // hurt
    {0x2A8, 4, 0}, // hurt (extended)
    {0x2C8, 4, 1}, // attack
    {0x2B8, 1, 3}, // throw
    {0x2C0, 1, 4}, // throwable
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
// open (player 2's 0x406 on); the "connection" word of the record reads 0xFFF1 the frame a parry lands
constexpr uint32_t kParryWindow[4] = {0x02026335, 0x02026337, 0x02026339, 0x02026347};
constexpr uint32_t kParryWindowPlayer2 = 0x406;
constexpr uint32_t kConnection = 0x32E;

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
