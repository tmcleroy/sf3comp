// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's stage camera in a super art's zoom, written again as C++ (rewrites: runtime/lift.h): the camera sent to
// a place a fighter asks for (its super art's flash, a throw's close-up) and back, over six frames, and the picture's
// frame drawn in and out around it.
//
// A fighter that wants the camera somewhere says so in the zoom's request words (0x020156B6, zoom_request_flag: bits
// 8-11 across, 12-15 up, 0-7 a zoom of the picture's frame; last frame's at 0x020156B8), with the place (0x020156D8,
// 0x020156DA: scr_req_x, scr_req_y) and how far in (0x020156BA, zoom_request_level). The fighters' layer then starts a
// scripted move (chase_start_check): where it is to go is kept (chase_x, chase_y), and the move's speed worked out so
// that it gets there in six frames, faster each frame (cal_bg_speed_data_x/_y: a start and a step, both the distance
// over 1 + 2 + ... + 6, the remainder added at once). Each frame the move goes on (chase_xy_move) until its frames
// are done; as the request ends the camera is sent back the same way to where the fighters have it (wxy). While a
// move is on, the stage's layers are drawn from their scripted places (chase_xy: stage_camera.cpp's Scripted).
//
// The frame's zoom (zoom_ud_check, once a frame from each stage's routine): with a zoom asked for, the picture's frame
// is drawn in a step a frame (Frame_Up) to 64 less the level, around the place asked for (if it is past the camera's
// ends; else the picture's middle); and as it ends, out a step a frame (Frame_Down) to 64, when the zoom is put back
// (Zoomf_Init).
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, bg_sub.c): this is the arcade's build of the same code, which differs here and there (no
// zoom check on bg_f_x for an entrance; the middle across mirrored with 0x02000005 set). Names only: that project is
// AGPL-3.0, and none of its code is here; what the routines do was read from the arcade's own program and checked call
// by call against it.
//
// Registers: the callers are the fighters' layer's routine (0x060BACB4, bg_base_move_common), a few stages' own copy of
// it, and every stage's routine (zoom_ud_check), compiled C that reads none of r0-r7 or T after the call (the reads
// tools/liveout.py reports are of r8-r14 it saves, or a value passed up a chain of tail jumps to a stage's routine that
// does not read it), so a rewrite leaves those as it likes (Lift::free). The speed routines, called by
// chase_start_check, leave r0 and r7 as the game's code does. The game's divide (0x0612D428) and remainder (0x0612D5D4)
// are called through Guest::Call: they leave the status register's M and Q bits as they are compared.
#include "lift.h"

#include <iterator>

namespace cps3recomp {

namespace {
    // --- the stage's head (bg_w, 0x02026BAC: stage_camera.cpp) ---
    constexpr uint32_t kStage = 0x02026BAC;
    constexpr uint32_t kMovedAcross = kStage + 0x12; // word: how far the fighters' layer moved across (bg2_sp_x)
    constexpr uint32_t kMovedUp = kStage + 0x14;     // word: ... up (bg2_sp_y)
    constexpr uint32_t kFrameZooming = kStage + 0x24; // byte: the frame's zoom begun (frame_flag)
    constexpr uint32_t kChase = kStage + 0x25;        // byte: scripted moves under way (chase_flag): 1 there, 2 back
                                                      // (across), 0x10, 0x20 (up)
    constexpr uint32_t kChaseWas = kStage + 0x26;     // byte: ... as noted for later (old_chase_flag)
    constexpr uint32_t kFrameZoomed = kStage + 0x27;  // byte: (old_frame_flag)
    constexpr uint32_t kLeftOfMiddle = kStage + 0x28; // word: the picture's left edge less its middle, 192 (pos_offset)
    constexpr uint32_t kFrameAcross = kStage + 0x2E, kFrameUp = kStage + 0x30; // words: the frame's size, 64 whole (bg_f_x, bg_f_y)
    constexpr uint32_t kMovedAcrossNow = kStage + 0x40; // word: (bg2_sp_x2)
    constexpr uint32_t kFrameWanted = kStage + 0x44;  // word: the frame's size wanted, 64 less the level (frame_deff)
    constexpr uint32_t kZoomMiddleX = kStage + 0x46, kZoomMiddleY = kStage + 0x48; // words: what the frame closes on (center_x, center_y)
    constexpr uint32_t kLayers = kStage + 0x54, kLayerSize = 0x90;
    constexpr uint32_t kLayerAt = 0x02026FF4; // long: the layer being worked on (bgw_ptr)

    // --- a layer (bgw) ---
    constexpr uint32_t kFamily = 0x02;              // word: its number (fam_no)
    constexpr uint32_t kColumn = 0x0A;              // word: its scroll across (position_x)
    constexpr uint32_t kShownUp = 0x1C;             // long, 16.16: where it is shown up (xy[1])
    constexpr uint32_t kAcross = 0x20;              // long, 16.16: where it is (wxy[0])
    constexpr uint32_t kWide = 0x34;                // long: not 0, a layer wider than the board's 1024 (zuubun)
    constexpr uint32_t kHomeAcross = 0x48, kHomeUp = 0x4A; // words (pos_x_work, pos_y_work)
    constexpr uint32_t kChaseAcross = 0x58, kChaseUp = 0x5C; // longs, 16.16: its place in a scripted move (chase_xy)
    constexpr uint32_t kRightEnd = 0x66, kLeftEnd = 0x6A, kTopEnd = 0x6E; // words (r_limit2, l_limit2, y_limit2)
    constexpr uint32_t kShakenAcross = 0x8C, kShakenUp = 0x8E; // words: its place with a shake's (abs_x, abs_y)

    // --- the zoom's requests and the move's own ---
    constexpr uint32_t kRequest = 0x020156B6;     // word (zoom_request_flag)
    constexpr uint32_t kRequestWas = 0x020156B8;  // word (zoom_req_flag_old)
    constexpr uint32_t kRequestLevel = 0x020156BA; // word (zoom_request_level)
    constexpr uint32_t kRequestX = 0x020156D8, kRequestY = 0x020156DA; // words (scr_req_x, scr_req_y)
    constexpr uint32_t kChaseX = 0x0202804E, kChaseY = 0x02028050; // words: where the move goes (chase_x, chase_y)
    constexpr uint32_t kChaseFramesX = 0x02028052, kChaseFramesY = 0x02028054; // words: its frames left (chase_time_x, _y)
    constexpr uint32_t kSpeed = 0x02028010; // (bg_mvxy) longs: the speed across, up, then their steps; words +0x10, +0x12
    constexpr uint32_t kSpeedX = kSpeed + 0x0, kSpeedY = kSpeed + 0x4, kStepX = kSpeed + 0x8, kStepY = kSpeed + 0xC;
    constexpr uint32_t kHeldForEntrance = 0x0202802A; // byte (bg_app_stop)
    constexpr uint32_t kStageEntrance = 0x0202805B;   // byte: the stage's own entrance on (bg_app)
    constexpr uint32_t kBonusGame = 0x02016B3A;       // word (Bonus_Game_Flag)
    constexpr uint32_t kMirrored = 0x02000005;        // byte: not 0, the frame's middle across taken the other way
    constexpr uint32_t kFrameIn = 0x06005E44, kFrameOut = 0x06005E62, kFrameDone = 0x06005DA8; // (Frame_Up, Frame_Down, Zoomf_Init)
    constexpr uint32_t kSpeedAcrossFor = 0x060B6FFC, kSpeedUpFor = 0x060B70A2; // (cal_bg_speed_data_x, _y)
    constexpr uint32_t kDivide = 0x0612D428, kRemainder = 0x0612D5D4; // r1 / r0 and r1 % r0, signed, in r0
    constexpr int32_t kChaseFrames = 6;

    int32_t S16(uint32_t v) {
        return static_cast<int16_t>(v);
    }
    int32_t Word(Guest &g, uint32_t at) {
        return S16(g.Read16(at));
    }
    void SetWord(Guest &g, uint32_t at, int32_t value) {
        g.Write16(at, static_cast<uint16_t>(value));
    }
    void SetBits(Guest &g, uint32_t at, uint8_t on, uint8_t off) {
        g.Write8(at, g.Read8(at) | on);
        g.Write8(at, g.Read8(at) & static_cast<uint8_t>(~off));
    }
    uint32_t Layer(Guest &g) {
        return g.Read32(kLayerAt);
    }
    uint32_t LayerNumbered(int32_t n) {
        return kLayers + static_cast<uint32_t>(S16(static_cast<uint32_t>(n * kLayerSize)));
    }


    // --- the move's speed ---

    // 060B6FFC, 060B70A2 (cal_bg_speed_data_x, _y: r4 the layer's number, r5 the frames; the place from chase_x or
    // chase_y): the speed of a scripted move from the layer's scripted place to the one wanted, so many frames, each
    // frame's step one more than the one before: start and step the distance (16.16) over 1 + 2 + ... + frames, the
    // remainder added to the place at once. The place's fraction is cleared first.
    void SpeedFor(Guest &g, uint32_t place, uint32_t wanted, uint32_t speed, uint32_t step, uint32_t frameWord) {
        const uint32_t layer = LayerNumbered(S16(g.R(4)));
        const int32_t frames = S16(g.R(5));
        SetWord(g, layer + place + 2, 0);
        const int32_t steps = static_cast<int32_t>(static_cast<uint32_t>(frames) * static_cast<uint32_t>(frames - 1)) / 2 + frames;
        const uint32_t distance = static_cast<uint32_t>(static_cast<uint16_t>(Word(g, wanted) - Word(g, layer + place))) << 16;
        uint32_t each = 0, rest = 0;
        if (frames != 0) {
            g.R(1) = distance;
            g.R(0) = static_cast<uint32_t>(steps);
            g.Call(kRemainder);
            rest = g.R(0);
            g.R(1) = distance;
            g.R(0) = static_cast<uint32_t>(steps);
            g.Call(kDivide);
            each = g.R(0);
        }
        g.Write32(speed, each);
        g.Write32(step, each);
        g.Write32(layer + place, g.Read32(layer + place) + rest);
        SetWord(g, frameWord, 0);
        g.R(0) = 0;
        g.R(7) = kStage;
    }
    bool SpeedAcrossFor(Guest &g) {
        SpeedFor(g, kChaseAcross, kChaseX, kSpeedX, kStepX, kSpeed + 0x10);
        g.Return(160);
        return true;
    }
    bool SpeedUpFor(Guest &g) {
        SpeedFor(g, kChaseUp, kChaseY, kSpeedY, kStepY, kSpeed + 0x12);
        g.Return(160);
        return true;
    }
    void CallSpeed(Guest &g, uint32_t routine, uint32_t layer, int32_t wanted) {
        g.R(4) = static_cast<uint32_t>(Word(g, layer + kFamily));
        g.R(5) = kChaseFrames;
        g.R(6) = static_cast<uint32_t>(wanted);
        g.Call(routine);
    }


    // --- the scripted move ---

    // 060B7D94 (chase_start_check): a scripted move begun as a request starts or changes, or ended as one ends.
    // Across: a new place asked for, the move there from where the layer is shown (its shaken place on a wide layer, its
    // scroll on another, from the picture's middle); a request ended, the move back to where the camera is. Up: a request
    // on, the move to the place asked for from where it is shown (not below 0); one ended, back to where it is shown.
    void ChaseStart(Guest &g) {
        const uint32_t layer = Layer(g);
        const uint16_t request = g.Read16(kRequest), was = g.Read16(kRequestWas);
        if (request & 0x0F00) {
            const int32_t x = Word(g, kRequestX);
            if (Word(g, kChaseX) != x) {
                SetWord(g, kChaseX, x);
                const int32_t from = g.Read32(layer + kWide) ? Word(g, layer + kShakenAcross) : Word(g, layer + kColumn);
                SetWord(g, layer + kChaseAcross, from + Word(g, kLeftOfMiddle));
                SetWord(g, kChaseFramesX, kChaseFrames);
                CallSpeed(g, kSpeedAcrossFor, layer, Word(g, kChaseX));
                SetBits(g, kChase, 1, 2);
                g.Write8(kChaseWas, 1);
            }
        } else if (S16(was & 0x0F00 & ~(request & 0x0F00))) {
            SetBits(g, kChase, 2, 1);
            SetWord(g, kChaseX, Word(g, layer + kAcross));
            SetWord(g, kChaseFramesX, kChaseFrames);
            CallSpeed(g, kSpeedAcrossFor, layer, Word(g, kChaseX));
        }
        if (g.Read16(kRequest) & 0xF000) {
            SetBits(g, kChase, 0x10, 0x20);
            SetBits(g, kChaseWas, 0x10, 0x20);
            const int32_t y = Word(g, kRequestY);
            if (Word(g, kChaseY) == y) {
                return;
            }
            SetWord(g, kChaseY, y);
            const int32_t shown = Word(g, layer + kShakenUp);
            SetWord(g, layer + kChaseUp, shown < 0 ? 0 : shown);
        } else {
            const uint16_t now = g.Read16(kRequest);
            if (!S16(g.Read16(kRequestWas) & 0xF000 & ~(now & 0xF000))) {
                return;
            }
            SetBits(g, kChase, 0x20, 0x10);
            SetWord(g, kChaseY, Word(g, layer + kShownUp));
        }
        SetWord(g, kChaseFramesY, kChaseFrames);
        CallSpeed(g, kSpeedUpFor, layer, Word(g, kChaseY));
    }

    // One frame of a scripted move's axis: its frames counted down, and while some are left the speed grows by its step
    // and the place moves by it; a move back done, the move ended and the place where it was going
    void ChaseStep(Guest &g, uint32_t layer, uint8_t there, uint8_t back, uint32_t frames, uint32_t speed, uint32_t step,
                   uint32_t place, uint32_t wanted) {
        const auto frame = [&]() {
            SetWord(g, frames, Word(g, frames) - 1);
            if (Word(g, frames) > 0) {
                g.Write32(speed, g.Read32(speed) + g.Read32(step));
                g.Write32(layer + place, g.Read32(layer + place) + g.Read32(speed));
                return true;
            }
            return false;
        };
        if (g.Read8(kChase) & there) {
            frame();
        }
        if (g.Read8(kChase) & back) {
            if (!frame()) {
                const uint8_t keep = there == 1 ? 0xF0 : 0x0F;
                g.Write8(kChase, g.Read8(kChase) & keep);
                g.Write8(kChaseWas, g.Read8(kChaseWas) & keep);
                SetWord(g, layer + place, Word(g, wanted));
            }
        }
    }

    // 060B7F64 (chase_xy_move): a frame of the scripted moves under way, each kept within the layer's ends (across, and
    // below its top), and how far the layer is from its start noted for the others (kMovedAcross, kMovedUp)
    void ChaseMove(Guest &g) {
        const uint32_t layer = Layer(g);
        if (g.Read8(kChase) & 0x0F) {
            SetWord(g, kMovedAcross, 0);
            SetWord(g, kMovedAcrossNow, 0);
            ChaseStep(g, layer, 1, 2, kChaseFramesX, kSpeedX, kStepX, kChaseAcross, kChaseX);
            if (Word(g, layer + kChaseAcross) > Word(g, layer + kRightEnd)) {
                SetWord(g, layer + kChaseAcross, Word(g, layer + kRightEnd));
                SetWord(g, layer + kChaseAcross + 2, 0);
            }
            if (Word(g, layer + kChaseAcross) < Word(g, layer + kLeftEnd)) {
                SetWord(g, layer + kChaseAcross, Word(g, layer + kLeftEnd));
                SetWord(g, layer + kChaseAcross + 2, 0);
            }
            const int32_t moved = Word(g, layer + kChaseAcross) - Word(g, layer + kHomeAcross);
            SetWord(g, kMovedAcrossNow, moved);
            SetWord(g, kMovedAcross, moved);
        }
        if (g.Read8(kChase) & 0xF0) {
            ChaseStep(g, layer, 0x10, 0x20, kChaseFramesY, kSpeedY, kStepY, kChaseUp, kChaseY);
            if (Word(g, layer + kChaseUp) > Word(g, layer + kTopEnd)) {
                SetWord(g, layer + kChaseUp, Word(g, layer + kTopEnd));
                SetWord(g, layer + kChaseUp + 2, 0);
            }
            SetWord(g, kMovedUp, Word(g, layer + kChaseUp) - Word(g, layer + kHomeUp));
        }
    }


    // --- the picture's frame ---

    // 060B8A34 (zoom_ud_check): the picture's frame drawn in or out a step this frame. Not during a stage's own entrance,
    // an entrance that holds the camera, or a bonus game.
    void FrameZoom(Guest &g) {
        if (g.Read8(kStageEntrance) || g.Read8(kHeldForEntrance) || g.Read16(kBonusGame)) {
            return;
        }
        const int32_t zooming = g.Read16(kRequest) & 0xFF;
        SetWord(g, kFrameWanted, 64 - Word(g, kRequestLevel));
        const bool begun = S16(~g.Read16(kRequestWas) & g.Read16(kRequest) & 0xFF) != 0;
        if (begun && g.Read8(kFrameZooming) == 0) {
            // (what it closes on: the place asked for, from the picture's left edge, when that is past the camera's ends;
            // else the picture's middle)
            g.Write8(kFrameZooming, 1);
            g.Write8(kFrameZoomed, 1);
            SetWord(g, kZoomMiddleY, 224 - Word(g, kRequestY));
            const uint32_t fighters = LayerNumbered(1);
            const int32_t x = Word(g, kRequestX);
            if (x < Word(g, fighters + kLeftEnd) || Word(g, fighters + kRightEnd) < x) {
                const int32_t wide = g.Read32(fighters + kWide) ? 512 : 0;
                SetWord(g, kZoomMiddleX, x + wide);
                const int32_t left = Word(g, fighters + kAcross) + wide - Word(g, kLeftOfMiddle);
                SetWord(g, kZoomMiddleX, Word(g, kZoomMiddleX) - left);
                if (g.Read8(kMirrored)) {
                    SetWord(g, kZoomMiddleX, Word(g, kLeftOfMiddle) * 2 - Word(g, kZoomMiddleX));
                }
            } else {
                SetWord(g, kZoomMiddleX, 192);
                SetWord(g, kZoomMiddleY, 224);
            }
        }
        const int32_t size = Word(g, kFrameAcross);
        const auto step = [&](uint32_t routine, int32_t by) {
            g.R(4) = static_cast<uint32_t>(Word(g, kZoomMiddleX));
            g.R(5) = static_cast<uint32_t>(Word(g, kZoomMiddleY));
            g.R(6) = g.R(7) = 1;
            g.Call(routine);
            SetWord(g, kFrameAcross, Word(g, kFrameAcross) + by);
            SetWord(g, kFrameUp, Word(g, kFrameUp) + by);
        };
        const auto out = [&]() {
            step(kFrameOut, 1);
            if (Word(g, kFrameAcross) == 64) {
                g.Write8(kFrameZooming, 0);
                g.Call(kFrameDone);
            }
        };
        if (zooming) {
            if (size > Word(g, kFrameWanted)) {
                step(kFrameIn, -1);
            } else if (size < Word(g, kFrameWanted)) {
                out();
            }
        } else if (size < 64) {
            out();
        }
    }


    // --- the lifts ---
    template <void (*F)(Guest &)>
    bool Lifted(Guest &g) {
        F(g);
        g.Return(80);
        return true;
    }

    // (free: r0-r7 and T, nothing reading them after: see the head of this file; the speed routines' r0 and r7 left)
    constexpr uint32_t kFree = 0x100FF;
    const Lift kLifts[] = {
        {"StageChaseSpeedAcross", 0x060B6FFC, 0xA6, 0x1855EB5CF644738Full, SpeedAcrossFor, true, 0x1007E},
        {"StageChaseSpeedUp", 0x060B70A2, 0xA6, 0x04E56B4673F4C043ull, SpeedUpFor, true, kFree},
        {"StageChaseStart", 0x060B7D94, 0x1D0, 0x62CF16E8E263C269ull, Lifted<ChaseStart>, true, kFree},
        {"StageChaseMove", 0x060B7F64, 0x1B4, 0x605B3B3DFF3863D8ull, Lifted<ChaseMove>, true, kFree},
        {"StageFrameZoom", 0x060B8A34, 0x242, 0x1473AC61A3863BBAull, Lifted<FrameZoom>, true, kFree},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterStageZoom() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
