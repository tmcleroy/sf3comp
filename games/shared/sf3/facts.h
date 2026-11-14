// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// What the sources the Street Fighter III games share (games/shared/sf3) need to know of each game: the three are
// one engine, with the same variables at other addresses. A game's own src/facts.cpp gives them, each with how it
// was found (cps3recomp/tools/crossmap.py carries an address over from 3rd Strike by the routines' names; every one
// is then checked in the game).
#pragma once

#include <cstdint>

namespace cps3recomp {
class Machine;
}

namespace sf3 {

// A list of boxes a player's record points to (8 bytes a box: left, width, bottom, height, signed words from the
// player's place, x turned with the facing)
struct BoxList {
    uint32_t offset; // in the record: the pointer to this frame's boxes (0: the game has no such list)
    int count;
    int kind;        // fight::BoxKind
};

struct Facts {
    // The board's start-up (its memory checks, the region's warning, Capcom's logo) is over and the opening about
    // to begin at this frame (seen in pictures taken every 100 frames)
    uint64_t opening;
    uint32_t credits;      // byte: the credits (Ck_Coin, credit_use)
    uint32_t clock;        // byte: the round's clock, 99 down
    uint32_t clockFrames;  // word: the frames left of the clock's second (Counter_low: the clock goes down as it runs out)
    // A round is on: a character locked in by either side (0xFF) and the match's state 2 (players can move)
    uint32_t locked[2];
    uint32_t matchState;
    uint32_t player[2];    // the players' records (plw[0], plw[1])
    uint32_t life;         // in a record, byte: the health (the low byte of vital_new)
    int fullLife;
    // The board's game settings in the settings EEPROM (byte offsets, as 3rd Strike's board.cpp has them; 0: the game
    // has no such setting): each has a second copy 0x30 bytes on
    bool bonusGames;       // the game has bonus stages, and the setting for them (0x27)
    bool roundsBetweenTwo; // the rounds byte's high half is the rounds between two players (else one number for both)
    // The board's own wide picture (the settings' byte 0x20: 1): 495 columns, the panel laid out to the corners and the
    // camera's ends the game's for it, as it was shown on a 16:9 monitor. 2nd Impact's test menu has the setting
    // (Screen mode, the game page's first row); New Generation's leaves the row out. The game reads it as it starts, into this byte of RAM (1 wide), which it
    // reads from then on (mode_init_task lays the panel out by it, test_mode_exit sets the screen's timing by it); so it
    // is changed only with the board started again (widescreen.cpp). 0: the game's wide picture is not used (3rd
    // Strike's leaves its panel laid out for 384 columns): widescreen is then the sources' own
    uint32_t boardWide;

    // --- training mode (training.cpp) ---

    // Character select, each player (a byte: the low byte of SP_No[player][0]): 0 no player, 1 coming in, 2 choosing a
    // character, 3 the super art's coming, 4 choosing it, 5 chosen
    uint32_t selectState[2];
    // The camera: the middle of the picture in the stage (signed words: bg_w.bgw[1].wxy), and the ends it is kept
    // between (r_limit, l_limit). Each end is two words: the stage's end, then where the camera stops, the same but in
    // the board's wide picture (`boardWide`), where it stops 56 columns sooner (240 and 184, traced on Dudley's stage)
    uint32_t cameraX, cameraY, cameraRight, cameraLeft;
    int wall;              // the players' walls: this far beyond the camera's ends
    // The stage of the next fight (a byte: a character's number, whose stage it is; 0: not known, no choice offered)
    uint32_t stage;
    const char *const *stageNames; // the Stage row's choices after "The game's choice", each `stageNumbers`' stage
    const uint8_t *stageNumbers;
    int stages;
    const char *const *characters; // by the record's character number
    int characterCount;

    // In a player's record (the game's own names for them in brackets)
    struct Record {
        uint32_t computer;      // byte: 0 worked by the computer, 1 by a player (operator)
        uint32_t facing;        // byte: 0 facing left, else right (rl_flag)
        uint32_t hitBy;         // long: the record that hit it last (dmg_adrs)
        uint32_t doing, which;  // words: routine_no[1] (0 normal, 1 hurt, 2 throwing, 3 thrown, 4 attacking), [2]
        uint32_t freeze;        // byte: frames of hit freeze left (the low byte of hit_stop)
        uint32_t x, y;          // signed words: in the stage, y up from the floor (xyz[0], xyz[1])
        uint32_t animation;     // long: the animation's address (set_char_ad)
        uint32_t animationFrame; // word: the frame in it (cg_ix)
        uint32_t cancelHit;     // byte: its attack has hit or been guarded (meoshi_hit_flag)
        uint32_t hurtStop;      // word: the hit stop its being hit gives it (dm_stop): `parryStop` the frame a parry lands
        uint32_t hitCount;      // word: hits taken, all told (dm_count_up)
        uint32_t character;     // word: the character's number (player_number)
        uint32_t thrown;        // byte: not 0 while being thrown (tsukamare_f)
        uint32_t busy;          // byte: not 0 while it cannot guard (guard_flag)
        uint32_t blocking;      // byte: 1-4 while guarding (guard_chuu)
        uint32_t attacking;     // byte: not 0 while attacking (caution_flag)
        uint32_t inputTaken;    // byte: 0 while the player's input is not taken (permited_koa)
        uint32_t commands;      // long: its commands' record (cp)
        uint32_t gauge;         // long: its super art's record (sa)
        uint32_t stun;          // long: its stun's record (py)
        uint32_t attackBoxes;   // long: this frame's attack boxes (h_att), as in `boxes`
        int attackBoxCount;
        uint32_t throwBox;      // long: this frame's throw box (h_cat; 0: the game's throws are attack boxes)
        uint32_t hurtBoxes;     // long: this frame's hurt boxes (h_bod): a record with none is not in use
    } record;
    BoxList boxes[8];      // (a list with offset 0 ends them)
    // The super art's record: the gauge's fill (a word: the high half of gauge), the stocks and the most of them (words:
    // store, store_max), and the kind of gauge (a byte, gauge_type: 1 a timer that drains while the art lasts)
    struct Gauge {
        uint32_t fill, stocks, stocksMost, kind;
    } gauge;
    // ... and each player's byte that has the game draw the gauge again (sa_gauge_flash; 0: the game's display follows
    // the stocks by itself)
    uint32_t gaugeRedraw[2];
    // The stun's record: the most taken before being stunned (a word, genkai), the timer (a word, time), and how much has
    // been taken (a word: now.quantity.h)
    struct Stun {
        uint32_t most, timer, taken;
    } stun;
    // The parry windows in the commands' record (bytes counting the frames each stays open: the low bytes of
    // waza_flag[3], [4], [5] and [12]): forward, down, in the air, anti-air (0: the game has no such window)
    uint32_t parryWindow[4];
    int16_t parryStop;     // what hurtStop is set to as a parry lands
    // The projectiles: the game's object list 3, its first index a word at `effectList` + 6, each a record of the
    // players' shape `effectSize` bytes apart from `effects`, the next's index a word at +0x1C, -1 ending it; byte 2 of
    // a record says whose (0 player 1's)
    uint32_t effectList, effects, effectSize;
    // What the game has (the dummy's options are offered only for these)
    bool throwEscape;      // a throw is escaped with light punch and light kick as it takes hold
    bool quickStand;       // down tapped as a knocked-down player lands stands them up sooner
    bool stunMash;         // (the stun's facts are known: the dummy can mash out of it)
};
const Facts &TheFacts(); // (the game's src/facts.cpp)

// The board's settings EEPROM's bytes, by the offsets above (the game's bytes high first in each word); setting one sets
// its copy 0x30 bytes on too (base.cpp)
uint8_t SettingsByte(const cps3recomp::Machine &m, uint32_t offset);
void SetSettingsByte(cps3recomp::Machine &m, uint32_t offset, uint8_t value);

// base.cpp: a round is being fought
bool InRound();

} // namespace sf3
