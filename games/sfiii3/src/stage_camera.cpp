// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's stage camera, written again as C++ (rewrites: runtime/lift.h): how the stage follows the fighters
// across and up, and how its other layers follow it (parallax), once a frame of a fight.
//
// The stage's state (the game's own source calls it bg_w) is at 0x02026BAC: a head of 0x54 bytes, then seven layers
// (bgw) of 0x90 bytes each from 0x02026C00. Layer 1 (0x02026C90) is the one the fighters stand on: its middle across
// is the camera (sfiii3.h's kCameraX, 0x02026CB0) and its ends are the camera's (kCameraRight/kCameraLeft). Layer 0 and
// the others are the stage's nearer and farther parts, moved by as much as the camera moved times their own speed. The
// routine of the layer being worked on finds it through a pointer at 0x02026FF4 (bgw_ptr).
//
// Each frame (0x060B71BC, TATE00, its fourth step 0x060B7246) the stage's own routine runs (one per stage, from a table
// in RAM at 0x0200649C, by the stage's number at +0x4A); each stage's routine works its layers, the fighters' layer
// first, with the routines here:
//   the fighters' layer (bg_base_move_common, 0x060BACB4): across (0x060B8620), then up (0x060B87B8), then the
//     scripted moves a super art's zoom asks for (0x060B7D94, 0x060B7F64: the game's, not here)
//   another layer (bg_x_move_check 0x060B886E, bg_y_move_check 0x060B8926): moved by the camera's move this frame
//     (the head's +0x40 across and +0x14 up) times its own speed (+0x10, +0x14: 16.16 fixed point)
//   then every layer's scroll for the board, a shake's offset added (bg_pos_hosei2, 0x060B9A2A), given to the board
//     with its objects' place (Bg_Family_Set, 0x060B96EE)
//
// Across: where the camera would like to be is worked out from where each fighter is in the picture (0x060B8510):
// each fighter's place (+0x60 of its record) a character's half width (a table by character, 0x065EB304) towards the
// side it is on, against the picture's left edge; each is in the left quarter (1), the right quarter (2) or between
// (0) of the 384 columns (a byte table by column, 0x0619DF64; the board's wide mode's 496, 0x0619E0E4), and the pair
// chooses one of nine routines (a table in RAM, 0x020064F8: scr_x_mv_jp), which moves the camera's wish (a 16.16
// number at 0x02027008, ideal_w) by as much as a fighter is past its quarter's edge (the edges 64 columns in from
// each side), or to their middle when both are near an edge. The step is then cut to the most a frame (+0x4E of the
// head) and to 80 in 100, and the layer is moved by it and kept between its ends.
//
// Up: the camera rises as the higher fighter goes above 88 (1.75 lines a line above), as far as the layer's limit.
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, bg_sub.c), where they are known: this is the arcade's build of the same code. (Names
// only: that project is AGPL-3.0, and none of its code is here; what the routines do was read from the arcade's own
// program and checked call by call against it.)
//
// Registers: these routines' callers are the stages' routines and each other, compiled C that reads none of r0-r7 or
// T after a call (tools/liveout.py: the reads it reports are of r8-r14, or r4-r7 assumed for a call through a pointer
// that follows), so a rewrite leaves those as it likes (Lift::free). They call the routines not here through the
// game's calls (Guest::Call), so a call runs the rewrite of the routine called if there is one; this file's own
// routines they call as C++.
#include "lift.h"

#include <algorithm>
#include <iterator>

namespace cps3recomp {

namespace {
    // --- the stage's head (bg_w, 0x02026BAC) ---
    constexpr uint32_t kStage = 0x02026BAC;
    constexpr uint32_t kRoutine = kStage + 0x00;   // byte: the frame's step (bg_routine): 0-2 setting up, 3 the stage's own
    constexpr uint32_t kMovedAcross = kStage + 0x12; // word: how far the fighters' layer moved across this frame, as
                                                   // shown (bg2_sp_x)
    constexpr uint32_t kMovedUp = kStage + 0x14;   // word: ... up (bg2_sp_y)
    constexpr uint32_t kStops = kStage + 0x22;     // word: bits the camera's moves clear (scr_stop)
    constexpr uint32_t kChase = kStage + 0x25;     // byte: scripted moves under way (chase_flag): bits 0-3 across, 4-7 up
    constexpr uint32_t kLeftOfMiddle = kStage + 0x28; // word: the picture's left edge less its middle, 192 (pos_offset)
    constexpr uint32_t kQuakeAcross = kStage + 0x2A, kQuakeUp = kStage + 0x2C; // words: a shake's frames left
    constexpr uint32_t kMovedAcrossNow = kStage + 0x40; // word: ... across, as it is (bg2_sp_x2)
    constexpr uint32_t kStageNumber = kStage + 0x4A; // word: which stage's routine runs (bg_index)
    constexpr uint32_t kMostAFrame = kStage + 0x4E; // word: the most the camera moves across in a frame (max_x)
    constexpr uint32_t kLayerAt = 0x02026FF4;      // long: the layer being worked on (bgw_ptr)

    // --- a layer (bgw, 0x90 bytes) ---
    constexpr uint32_t kSpeedAcross = 0x10, kSpeedUp = 0x14; // longs: its speed against the camera's, 16.16
    constexpr uint32_t kShownAcross = 0x18, kShownUp = 0x1C; // longs: where it is shown (xy), 16.16
    constexpr uint32_t kAcross = 0x20, kUp = 0x24;           // longs: where it is (wxy), 16.16: the fighters' layer's is the camera
    constexpr uint32_t kWasAcross = 0x30;   // word: its place across at the frame before (old_pos_x)
    constexpr uint32_t kHomeAcross = 0x48, kHomeUp = 0x4A; // words: its place with the camera at its start (pos_x_work, pos_y_work)
    constexpr uint32_t kChaseAcross = 0x58, kChaseUp = 0x5C; // longs: its place in a scripted move (chase_xy)
    constexpr uint32_t kRightEnd = 0x66, kLeftEnd = 0x6A, kTopEnd = 0x6E; // words: its ends (r_limit2, l_limit2, y_limit2)

    // --- the fighters, the game's ---
    constexpr uint32_t kFighters = 0x02068C6C, kFighterSize = 0x498; // (plw)
    constexpr uint32_t kInPicture = 0x60, kInPictureUp = 0x62; // words: a fighter's place for the camera (scr_mv_x, scr_mv_y)
    constexpr uint32_t kCharacter = 0x3C0;  // word (player_number)
    constexpr uint32_t kAgainstWall = 0x3C9; // byte: 1 against the left end, 2 the right (micchaku_flag)
    constexpr uint32_t kHalfWidths = 0x065EB304; // words by character: half a fighter's width (satse)

    // --- the camera's wish and the rest ---
    constexpr uint32_t kWish = 0x02027008;       // long, 16.16: where the camera would be (ideal_w); its high word the column
    constexpr uint32_t kHeld = 0x02028028;       // word: not 0, the camera held (bg_stop)
    constexpr uint32_t kHeldForEntrance = 0x0202802A; // byte: not 0, held for an entrance (bg_app_stop)
    constexpr uint32_t kBonusGame = 0x02016B3A;  // word: a bonus game on: no scripted moves (Bonus_Game_Flag)
    constexpr uint32_t kWideScreen = 0x0206AC62; // byte: the board's wide picture (496 columns)
    constexpr uint32_t kQuarters = 0x0619DF64, kWideQuarters = 0x0619E0E4; // bytes by column: 1 left, 2 right, 0 between
    constexpr uint32_t kCameraMoves = 0x020064F8; // longs in RAM: the nine routines by the quarters (scr_x_mv_jp)
    constexpr uint32_t kStageRoutines = 0x0200649C; // longs in RAM: each stage's routine (ta_move_tbl)
    constexpr uint32_t kSteps = 0x0619C1D0;       // longs: the frame's four steps (TATE00's)
    constexpr uint32_t kChaseStart = 0x060B7D94;  // chase_start_check: a scripted move begun (a super art's zoom)
    constexpr uint32_t kChaseMove = 0x060B7F64;   // chase_xy_move: one under way moved on
    constexpr uint32_t kDivide = 0x0612D428;      // r1 / r0, signed, the quotient in r0

    int32_t S16(uint16_t v) {
        return static_cast<int16_t>(v);
    }

    // The layer being worked on (through kLayerAt), its fields
    struct Layer {
        Guest &g;
        uint32_t at;
        explicit Layer(Guest &guest)
            : g(guest), at(guest.Read32(kLayerAt)) {}
        int32_t Word(uint32_t field) { return S16(g.Read16(at + field)); }
        void SetWord(uint32_t field, int32_t value) { g.Write16(at + field, static_cast<uint16_t>(value)); }
        uint32_t Long(uint32_t field) { return g.Read32(at + field); }
        void SetLong(uint32_t field, uint32_t value) { g.Write32(at + field, value); }
        // A 16.16 place put at a whole column (its fraction 0)
        void SetWhole(uint32_t field, int32_t value) {
            SetWord(field, value);
            SetWord(field + 2, 0);
        }
    };

    int32_t Word(Guest &g, uint32_t at) {
        return S16(g.Read16(at));
    }
    void SetWord(Guest &g, uint32_t at, int32_t value) {
        g.Write16(at, static_cast<uint16_t>(value));
    }
    bool Scripted(Guest &g, uint8_t bits) {
        return (g.Read8(kChase) & bits) != 0;
    }
    uint32_t Fighter(int n) {
        return kFighters + static_cast<uint32_t>(n) * kFighterSize;
    }
    int32_t HalfWidth(Guest &g, uint32_t fighter) {
        return Word(g, kHalfWidths + static_cast<uint32_t>(Word(g, fighter + kCharacter) * 2));
    }
    int32_t WishColumn(Guest &g) {
        return Word(g, kWish);
    }


    // --- moving a layer ---

    // 060B8140 (Bg_mv_tw, r4 across, r5 up, 16.16): the layer moved, where it is and where it is shown
    void MoveLayer(Guest &g, uint32_t across, uint32_t up) {
        Layer l(g);
        l.SetLong(kShownAcross, l.Long(kShownAcross) + across);
        l.SetLong(kShownUp, l.Long(kShownUp) + up);
        l.SetLong(kAcross, l.Long(kAcross) + across);
        l.SetLong(kUp, l.Long(kUp) + up);
    }

    // 060B81A8, 060B81CC (x_right_check, x_left_check: the same code, r4 the columns): the camera's wish moved by so many
    // columns times the layer's speed, its stops' two low bits cleared. (Reached only by the moves' branches, not called:
    // part of the moves' rewrites, not registered by themselves.)
    void MoveWish(Guest &g, int32_t columns) {
        SetWord(g, kStops, Word(g, kStops) & 0xFFFC);
        Layer l(g);
        g.Write32(kWish, g.Read32(kWish) + static_cast<uint32_t>(static_cast<int32_t>(l.Long(kSpeedAcross)) * columns));
    }


    // --- the nine moves (scr_x_mv_jp; each names the quarters of player 1 and player 2) ---
    //
    // A fighter's outer side: its place and half its width towards the camera's edge it is near. The edges: the wish's
    // column, less the picture's left edge (kLeftOfMiddle), plus 64 (the left quarter's edge), or plus it, less 63.
    int32_t LeftQuarterEdge(Guest &g) {
        return WishColumn(g) - Word(g, kLeftOfMiddle) + 64;
    }
    int32_t RightQuarterEdge(Guest &g) {
        return WishColumn(g) + Word(g, kLeftOfMiddle) - 63;
    }
    int32_t LeftSide(Guest &g, uint32_t fighter) {
        return Word(g, fighter + kInPicture) - HalfWidth(g, fighter);
    }
    int32_t RightSide(Guest &g, uint32_t fighter) {
        return Word(g, fighter + kInPicture) + HalfWidth(g, fighter);
    }
    // (the columns, as the game's code keeps them: a word)
    void Step(Guest &g, int32_t columns) {
        MoveWish(g, S16(static_cast<uint16_t>(columns)));
    }
    // Both near the same edge, or one at each: to their middle (their sides' middle), unless the one the camera would
    // go towards is against the stage's end on that side
    void ToMiddle(Guest &g) {
        const uint32_t one = Fighter(0), two = Fighter(1);
        const int32_t widths = S16(static_cast<uint16_t>(HalfWidth(g, two) - HalfWidth(g, one))) >> 1;
        const int32_t places = S16(static_cast<uint16_t>(Word(g, one + kInPicture) + Word(g, two + kInPicture))) >> 1;
        const int32_t columns = S16(static_cast<uint16_t>(places + widths - WishColumn(g)));
        if (columns < 0) {
            if (g.Read8(two + kAgainstWall) != 1) {
                MoveWish(g, columns);
            }
        } else if (g.Read8(one + kAgainstWall) != 2) {
            MoveWish(g, columns);
        }
    }
    // ... the same with the two the other way about (scr_12_21: player 1 at the right, player 2 at the left)
    void ToMiddleOtherWay(Guest &g) {
        const uint32_t one = Fighter(0), two = Fighter(1);
        const int32_t widths = S16(static_cast<uint16_t>(HalfWidth(g, one) - HalfWidth(g, two))) >> 1;
        const int32_t places = S16(static_cast<uint16_t>(Word(g, one + kInPicture) + Word(g, two + kInPicture))) >> 1;
        const int32_t columns = S16(static_cast<uint16_t>(places + widths - WishColumn(g)));
        if (columns < 0) {
            if (g.Read8(one + kAgainstWall) != 1) {
                MoveWish(g, columns);
            }
        } else if (g.Read8(two + kAgainstWall) != 2) {
            MoveWish(g, columns);
        }
    }

    // 060B8220 (scr_10_21): player 2 in the left quarter: left by as far as its left side is past the edge
    bool P2Left(Guest &g) {
        Step(g, LeftSide(g, Fighter(1)) - LeftQuarterEdge(g));
        g.Return(30);
        return true;
    }
    // 060B8242 (scr_10_22): player 2 in the right quarter
    bool P2Right(Guest &g) {
        Step(g, RightSide(g, Fighter(1)) - RightQuarterEdge(g));
        g.Return(30);
        return true;
    }
    // 060B8264 (scr_11_20): player 1 in the left quarter
    bool P1Left(Guest &g) {
        Step(g, LeftSide(g, Fighter(0)) - LeftQuarterEdge(g));
        g.Return(30);
        return true;
    }
    // 060B8286 (scr_11_21): both in the left quarter: by the one further left (player 2's if they are level)
    bool BothLeft(Guest &g) {
        const uint32_t one = Fighter(0), two = Fighter(1);
        const uint32_t further = Word(g, one + kInPicture) >= Word(g, two + kInPicture) ? two : one;
        Step(g, LeftSide(g, further) - LeftQuarterEdge(g));
        g.Return(40);
        return true;
    }
    // 060B82CE (scr_11_22): player 1 at the left, player 2 at the right
    bool P1LeftP2Right(Guest &g) {
        ToMiddle(g);
        g.Return(50);
        return true;
    }
    // 060B8354 (scr_12_20): player 1 in the right quarter
    bool P1Right(Guest &g) {
        Step(g, RightSide(g, Fighter(0)) - RightQuarterEdge(g));
        g.Return(30);
        return true;
    }
    // 060B8376 (scr_12_21): player 1 at the right, player 2 at the left
    bool P1RightP2Left(Guest &g) {
        ToMiddleOtherWay(g);
        g.Return(50);
        return true;
    }
    // 060B83E0 (scr_12_22): both in the right quarter: by the one further right (player 2's if they are level)
    bool BothRight(Guest &g) {
        const uint32_t one = Fighter(0), two = Fighter(1);
        const uint32_t further = Word(g, one + kInPicture) > Word(g, two + kInPicture) ? one : two;
        Step(g, RightSide(g, further) - RightQuarterEdge(g));
        g.Return(40);
        return true;
    }


    // --- the fighters' layer ---

    // 060B8510 (bg_base_x_move_sub): which quarter of the picture each fighter is in, and that pair's move run
    void ChooseMove(Guest &g) {
        const int32_t left = WishColumn(g) - Word(g, kLeftOfMiddle);
        const bool wide = g.Read8(kWideScreen) != 0;
        const int32_t last = wide ? 0x1EF : 0x17F;
        const uint32_t quarters = wide ? kWideQuarters : kQuarters;
        int32_t quarter[2];
        for (int n = 0; n < 2; n++) {
            const uint32_t fighter = Fighter(n);
            // (its outer side, towards the camera's side it is on)
            const int32_t side = Word(g, fighter + kInPicture) < WishColumn(g) ? LeftSide(g, fighter) : RightSide(g, fighter);
            const int32_t column = std::clamp(S16(static_cast<uint16_t>(side - left)), 0, last);
            quarter[n] = static_cast<int8_t>(g.Read8(quarters + static_cast<uint32_t>(column)));
        }
        g.Call(g.Read32(kCameraMoves + static_cast<uint32_t>((quarter[0] * 16 + quarter[1]) * 4)));
    }

    // 060B8620 (bg_base_x_move_check): the camera across. Unless it is held, its wish is worked out (ChooseMove) from
    // where it is; the step to it, cut to the most a frame and to 80 in 100, moves the layer, which is kept between its
    // ends. Then how far it moved is noted (kMovedAcross, kMovedAcrossNow) for the other layers, and the place a
    // scripted move would start from, if none is under way.
    void CameraAcross(Guest &g) {
        SetWord(g, kMovedAcross, 0);
        SetWord(g, kMovedAcrossNow, 0);
        if (Word(g, kHeld) == 0 && g.Read8(kHeldForEntrance) == 0) {
            {
                Layer l(g);
                l.SetWord(kWasAcross, l.Word(Scripted(g, 0x0F) ? kChaseAcross : kAcross));
                const int32_t was = l.Word(kAcross);
                g.Write32(kWish, l.Long(kAcross));
                ChooseMove(g);
                int32_t step = S16(static_cast<uint16_t>(WishColumn(g) - was));
                if (step != 0) {
                    const int32_t most = Word(g, kMostAFrame);
                    const int32_t size = std::min(step < 0 ? -step : step, most);
                    // (by the game's divide, which leaves the status register's M and Q bits as they are compared)
                    g.R(1) = static_cast<uint32_t>(S16(static_cast<uint16_t>(size)) * 80);
                    g.R(0) = 100;
                    g.Call(kDivide);
                    const int32_t cut = static_cast<int32_t>(g.R(0));
                    step = step < 0 ? -cut : cut;
                }
                g.Write32(kWish, static_cast<uint32_t>(step) << 16);
            }
            MoveLayer(g, g.Read32(kWish), 0);
            Layer l(g);
            if (l.Word(kAcross) < l.Word(kLeftEnd)) {
                l.SetWhole(kAcross, l.Word(kLeftEnd));
                l.SetWhole(kShownAcross, l.Word(kLeftEnd));
            }
            if (l.Word(kAcross) > l.Word(kRightEnd)) {
                l.SetWhole(kAcross, l.Word(kRightEnd));
                l.SetWhole(kShownAcross, l.Word(kRightEnd));
            }
        }
        Layer l(g);
        SetWord(g, kMovedAcross, l.Word(kShownAcross) - l.Word(kHomeAcross));
        SetWord(g, kMovedAcrossNow, l.Word(kAcross) - l.Word(kHomeAcross));
        if (!Scripted(g, 0x0F)) {
            l.SetWord(kChaseAcross, l.Word(kAcross));
        }
    }

    // 060B87B8 (bg_base_y_move_check): the camera up. Unless it is held: as the higher fighter is above 88, 1.75 lines
    // a line above, as far as the layer's top. How far it is up is noted for the other layers (kMovedUp).
    void CameraUp(Guest &g) {
        Layer l(g);
        if (Word(g, kHeld) == 0 && g.Read8(kHeldForEntrance) == 0) {
            const int32_t one = Word(g, Fighter(0) + kInPictureUp), two = Word(g, Fighter(1) + kInPictureUp);
            const int32_t above = S16(static_cast<uint16_t>((one > two ? one : two) - 88));
            l.SetLong(kShownUp, 0);
            l.SetLong(kUp, 0);
            if (above > 0) {
                const uint32_t up = static_cast<uint32_t>(0x1C000 * above);
                l.SetLong(kShownUp, up);
                l.SetLong(kUp, up);
                if (l.Word(kShownUp) > l.Word(kTopEnd)) {
                    l.SetWhole(kShownUp, l.Word(kTopEnd));
                    l.SetWhole(kUp, l.Word(kTopEnd));
                    SetWord(g, kStops, Word(g, kStops) & 0x7FFF);
                }
            }
        }
        SetWord(g, kMovedUp, l.Word(kShownUp) - l.Word(kHomeUp));
    }

    // 060BACB4 (bg_base_move_common): the fighters' layer, a frame: across, up, then (not in a bonus game) a super art's
    // zoom's scripted move begun or moved on
    void FightersLayer(Guest &g) {
        CameraAcross(g);
        CameraUp(g);
        if (Word(g, kBonusGame) == 0) {
            g.Call(kChaseStart);
            if (g.Read8(kChase) != 0) {
                g.Call(kChaseMove);
            }
        }
    }


    // --- the other layers ---

    // 060B886E (bg_x_move_check): a layer across: as far as the camera moved (kMovedAcrossNow) times its speed, from
    // its place with the camera at its start; in a scripted move, its scripted place
    void LayerAcross(Guest &g) {
        Layer l(g);
        const bool scripted = Scripted(g, 0x0F);
        l.SetWord(kWasAcross, l.Word(scripted ? kChaseAcross : kAcross));
        const uint32_t moved = static_cast<uint32_t>(static_cast<int32_t>(l.Long(kSpeedAcross)) * Word(g, kMovedAcrossNow));
        if (scripted) {
            l.SetLong(kChaseAcross, moved);
            l.SetWord(kChaseAcross, l.Word(kChaseAcross) + l.Word(kHomeAcross));
        } else {
            l.SetLong(kShownAcross, moved);
            l.SetLong(kAcross, moved);
            l.SetWord(kShownAcross, l.Word(kShownAcross) + l.Word(kHomeAcross));
            l.SetWord(kAcross, l.Word(kShownAcross));
        }
        if (!Scripted(g, 0x0F)) {
            l.SetWord(kChaseAcross, l.Word(kAcross));
        }
    }

    // 060B8926 (bg_y_move_check): a layer up: as far as the camera is up (kMovedUp) times its speed, no higher than its
    // top, from its place with the camera at its start; in a scripted move, its scripted place
    void LayerUp(Guest &g) {
        Layer l(g);
        const uint32_t moved = static_cast<uint32_t>(static_cast<int32_t>(l.Long(kSpeedUp)) * Word(g, kMovedUp));
        const uint32_t field = Scripted(g, 0xF0) ? kChaseUp : kShownUp;
        l.SetLong(field, moved);
        if (l.Word(kTopEnd) < l.Word(field)) {
            l.SetWhole(field, l.Word(kTopEnd));
        }
        l.SetWord(field, l.Word(field) + l.Word(kHomeUp));
        if (field == kShownUp) {
            l.SetLong(kUp, l.Long(kShownUp));
        }
    }


    // --- the layers to the board ---

    constexpr uint32_t kLayers = 0x02026C00, kLayerSize = 0x90;
    constexpr uint32_t kLayerCount = kStage + 0x10; // word: the stage's layers (scno)
    constexpr uint32_t kColumn = 0x0A, kLine = 0x0C; // words: the layer's scroll, as the board takes it (0-1023)
    constexpr uint32_t kShakenAcross = 0x8C, kShakenUp = 0x8E; // words: its place with a shake's (abs_x, abs_y)
    constexpr uint32_t kShakeAcross = 0x0619DBCC, kShakeUp = 0x0619DCD0; // words by a shake's frame (quake_x_tbl, quake_y_tbl)
    constexpr uint32_t kSetScroll = 0x0612E51C;   // (r4 the layer, r5 across, r6 up: its scroll for the board, Scrn_Move_Set)
    constexpr uint32_t kSetFamily = 0x0612DD48;   // (r4 the layer + 1, r5, r6: its objects' place, Family_Set_W)

    uint32_t LayerAt(int32_t n) {
        return kLayers + static_cast<uint32_t>(S16(static_cast<uint16_t>(n * kLayerSize)));
    }

    // 060B9A2A (bg_pos_hosei2): each layer's scroll for the board from where it is (or its scripted place), a shake's
    // offset added: across from the picture's left edge, both kept to the board's 1024
    void LayerScrolls(Guest &g) {
        for (int32_t n = 0; n < Word(g, kLayerCount); n++) {
            const uint32_t at = LayerAt(n);
            const int32_t across = Word(g, at + (Scripted(g, 0x0F) ? kChaseAcross : kAcross));
            const int32_t left = Word(g, kLeftOfMiddle);
            const int32_t shake = Word(g, kShakeAcross + static_cast<uint32_t>(Word(g, kQuakeAcross) * 2));
            SetWord(g, at + kColumn, ((((across & 0x3FF) - left) & 0x3FF) + shake) & 0x3FF);
            SetWord(g, at + kShakenAcross, across - left + shake);
            const int32_t up = Word(g, at + (Scripted(g, 0xF0) ? kChaseUp : kShownUp));
            const int32_t shakeUp = Word(g, kShakeUp + static_cast<uint32_t>(Word(g, kQuakeUp) * 2));
            SetWord(g, at + kLine, ((up & 0x3FF) + shakeUp) & 0x3FF);
            SetWord(g, at + kShakenUp, up + shakeUp);
        }
    }

    // 060B96EE (Bg_Family_Set): each layer's scroll given to the board, and its objects placed with it (the other way
    // across, and up from line 768)
    void LayersToBoard(Guest &g) {
        for (int32_t n = 0; n < Word(g, kLayerCount); n++) {
            const uint32_t at = LayerAt(n);
            const int32_t column = Word(g, at + kColumn), line = Word(g, at + kLine);
            g.R(4) = static_cast<uint32_t>(n);
            g.R(5) = static_cast<uint32_t>(column);
            g.R(6) = static_cast<uint32_t>(line);
            g.Call(kSetScroll);
            g.R(4) = static_cast<uint32_t>(n + 1);
            g.R(5) = static_cast<uint32_t>(-column & 0x3FF);
            g.R(6) = static_cast<uint32_t>((0x300 - (line & 0x3FF)) & 0x3FF);
            g.Call(kSetFamily);
        }
    }


    // --- the frame ---

    // 060B7246 (ta0_move): the stage's own routine, then a shake's frames counted down
    void StageFrame(Guest &g) {
        g.Call(g.Read32(kStageRoutines + static_cast<uint32_t>(Word(g, kStageNumber) * 4)));
        for (const uint32_t quake : {kQuakeAcross, kQuakeUp}) {
            if (Word(g, quake) > 0) {
                SetWord(g, quake, Word(g, quake) - 1);
            }
        }
    }

    // 060B71BC (TATE00): the frame's step, by kRoutine: three of setting up (ta0_init00-02), then the stage's frame
    // (StageFrame)
    void StageStep(Guest &g) {
        const int32_t step = static_cast<int8_t>(g.Read8(kRoutine));
        g.Call(g.Read32(kSteps + static_cast<uint32_t>(step * 4)));
    }


    // --- the lifts ---
    template <void (*F)(Guest &)>
    bool Lifted(Guest &g) {
        F(g);
        g.Return(60);
        return true;
    }
    bool MoveLayerLift(Guest &g) {
        MoveLayer(g, g.R(4), g.R(5));
        g.Return(20);
        return true;
    }

    // (free: r0-r7 and T, nothing reading them after: see the head of this file)
    constexpr uint32_t kFree = 0x100FF;
    const Lift kLifts[] = {
        {"StageStep", 0x060B71BC, 0x2E, 0x32A55CF342B00875ull, Lifted<StageStep>, true, kFree},
        {"StageFrame", 0x060B7246, 0x3A, 0x58A18BB95FE8FB09ull, Lifted<StageFrame>, true, kFree},
        {"StageMoveLayer", 0x060B8140, 0x24, 0xC8C4A2BF70C9037Cull, MoveLayerLift, true, kFree},
        {"StageP2Left", 0x060B8220, 0x22, 0xFADAFA4B8CAC09AEull, P2Left, true, kFree},
        {"StageP2Right", 0x060B8242, 0x22, 0x8A45A6B6F7850A47ull, P2Right, true, kFree},
        {"StageP1Left", 0x060B8264, 0x22, 0x2AAA1201E541B1ECull, P1Left, true, kFree},
        {"StageBothLeft", 0x060B8286, 0x48, 0x380C9C68049BE055ull, BothLeft, true, kFree},
        {"StageP1LeftP2Right", 0x060B82CE, 0x86, 0x6AFA1E4AE2992E7Eull, P1LeftP2Right, true, kFree},
        {"StageP1Right", 0x060B8354, 0x22, 0xDA385081FA4D52BFull, P1Right, true, kFree},
        {"StageP1RightP2Left", 0x060B8376, 0x6A, 0x5C732E8D6606727Dull, P1RightP2Left, true, kFree},
        {"StageBothRight", 0x060B83E0, 0x44, 0x9BCD883266CD8B12ull, BothRight, true, kFree},
        {"StageChooseMove", 0x060B8510, 0xF8, 0xDE5039CBF0DAEE70ull, Lifted<ChooseMove>, true, kFree},
        {"StageCameraAcross", 0x060B8620, 0x164, 0x850BEA175DF5A0D4ull, Lifted<CameraAcross>, true, kFree},
        {"StageCameraUp", 0x060B87B8, 0xB6, 0x8F9E65125DFE3916ull, Lifted<CameraUp>, true, kFree},
        {"StageLayerAcross", 0x060B886E, 0xB8, 0x5EE4F8FBD5234AE3ull, Lifted<LayerAcross>, true, kFree},
        {"StageLayerUp", 0x060B8926, 0x96, 0xFBDCD9497C792531ull, Lifted<LayerUp>, true, kFree},
        {"StageLayersToBoard", 0x060B96EE, 0x70, 0x0BB4974CE80A5216ull, Lifted<LayersToBoard>, true, kFree},
        {"StageLayerScrolls", 0x060B9A2A, 0xEE, 0x561ED706B013141Dull, Lifted<LayerScrolls>, true, kFree},
        {"StageFightersLayer", 0x060BACB4, 0x30, 0x0B4ACE5DAB5774AEull, Lifted<FightersLayer>, true, kFree},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp
void RegisterStageZoom();  // stage_zoom.cpp
void RegisterStageParts(); // stage_parts.cpp

// (the stage's other rewrites too, the zoom's and the stages' own)
void RegisterStageCamera() {
    RegisterGameLifts(kLifts, std::size(kLifts));
    RegisterStageZoom();
    RegisterStageParts();
}

} // namespace cps3recomp
