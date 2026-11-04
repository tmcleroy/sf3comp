// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's stages' own routines, written again as C++ (rewrites: runtime/lift.h): each frame of a fight its stage's
// routine (one per stage, from a table in RAM at 0x0200649C by the stage's number: stage_camera.cpp's StageFrame) works
// the stage's layers one after another, each by its own step (a layer's +0x04, r_no_0: 0 setting it up, then its
// moves), then the zoom of the picture's frame (stage_zoom.cpp), the layers' scrolls and their objects for the board
// (stage_camera.cpp).
//
// The stages are numbered as the characters are, by whose stage it is: Gill 0, Alex 1 (and Ken's, 11: the same New
// York), Ryu 2, Yun 3, Dudley 4, Necro 5, Hugo 6, Ibuki 7, Elena 8, Oro 9, Yang 10, Sean 12, Urien 13, Akuma 14 (and
// Shin Akuma's, 15), Chun-Li 16, Makoto 17, Q 18 (and Twelve's, 19), Remy 20; 21 the bonus game's.
//
// A stage's routine (the game's own source calls them BG000 ... BG190): for each of its layers, the layer made the one
// being worked on (bgw_ptr), and the routine for its step called from a table of the stage's (a table in ROM copied to
// the stack and called through: the game's code is compiled C with a local table of routines); or for some layers a
// routine of their own called directly. Most stages' fighters' layer (layer 1) is set up by a routine of the stage's,
// then moved by bg_base_move_common (0x060BACB4: stage_camera.cpp), its nearer layer (layer 0) by bg_move_common
// (0x060BACE4: across and up as the camera moves, times its own speed). Then the picture's frame's zoom (0x060B8A34),
// the layers' scrolls (0x060B9A2A), and their scrolls and objects to the board (0x060B96EE, or the variants for some
// layers only: 0x060B975E and 0x060B9832, r4 the layer; 0x060B97AA).
//
// Names in the comments are those of the game's own source, as the PlayStation 2 version's decompilation has them
// (crowded-street/3s-decomp, bg000.c ... bg190.c), where they are known: this is the arcade's build of the same code.
// (Names only: that project is AGPL-3.0, and none of its code is here; what the routines do was read from the
// arcade's own program and checked call by call against it.)
//
// Registers: these routines are called through the table of stages (StageFrame) and the stages' tables of steps, by
// compiled C that reads none of r0-r7 or T after a call (tools/liveout.py: the reads it reports are of r8-r14, or of
// r4-r7, assumed for a call through a pointer that follows), so a rewrite leaves those as it likes (Lift::free). What
// they call they call through the game's calls (Guest::Call), so a call runs the rewrite of the routine called if
// there is one.
#include "lift.h"

#include <cstdint>
#include <initializer_list>
#include <iterator>

namespace cps3recomp {

namespace {
    // --- the stage (bg_w: stage_camera.cpp) ---
    constexpr uint32_t kStage = 0x02026BAC;
    constexpr uint32_t kLayers = kStage + 0x54, kLayerSize = 0x90;
    constexpr uint32_t kLayerAt = 0x02026FF4; // long: the layer being worked on (bgw_ptr)
    constexpr uint32_t kStep = 0x04;          // a layer's word: its step (r_no_0)

    // --- what a stage's routine calls after its layers ---
    constexpr uint32_t kFrameZoom = 0x060B8A34;    // the picture's frame's zoom (zoom_ud_check: stage_zoom.cpp)
    constexpr uint32_t kLayerScrolls = 0x060B9A2A; // each layer's scroll from its place (bg_pos_hosei2)
    constexpr uint32_t kToBoard = 0x060B96EE;      // every layer's scroll and objects to the board (Bg_Family_Set)
    constexpr uint32_t kOneToBoard = 0x060B975E;   // ... one layer's, r4 (Bg_Family_Set_appoint)
    constexpr uint32_t kOneToBoardOther = 0x060B9832; // ... one layer's the other way (Bg_Family_Set_2_appoint)
    constexpr uint32_t kToBoardOther = 0x060B97AA; // ... every layer's the other way (Bg_Family_Set_2)

    uint32_t Layer(int n) {
        return kLayers + static_cast<uint32_t>(n) * kLayerSize;
    }

    // One thing a stage's routine does: a layer's step from a table of routines, or a layer's own routine, or a routine
    // with no layer (r4 given, if it takes one)
    struct Part {
        enum Kind { kStepFrom, kLayerRoutine, kRoutine, kRoutineFor } kind;
        int layer;       // (kStepFrom, kLayerRoutine)
        uint32_t at;     // the table of routines by step (kStepFrom), or the routine
        int argument;    // (kRoutineFor: r4)
    };
    constexpr Part StepFrom(int layer, uint32_t table) {
        return {Part::kStepFrom, layer, table, 0};
    }
    constexpr Part LayerRoutine(int layer, uint32_t routine) {
        return {Part::kLayerRoutine, layer, routine, 0};
    }
    constexpr Part Routine(uint32_t routine) {
        return {Part::kRoutine, 0, routine, 0};
    }
    constexpr Part RoutineFor(uint32_t routine, int argument) {
        return {Part::kRoutineFor, 0, routine, argument};
    }

    void Run(Guest &g, std::initializer_list<Part> parts) {
        for (const Part &part : parts) {
            switch (part.kind) {
            case Part::kStepFrom: {
                const uint32_t layer = Layer(part.layer);
                g.Write32(kLayerAt, layer);
                const int32_t step = static_cast<int16_t>(g.Read16(layer + kStep));
                g.Call(g.Read32(part.at + static_cast<uint32_t>(step * 4)));
                break;
            }
            case Part::kLayerRoutine:
                g.Write32(kLayerAt, Layer(part.layer));
                g.Call(part.at);
                break;
            case Part::kRoutine:
                g.Call(part.at);
                break;
            case Part::kRoutineFor:
                g.R(4) = static_cast<uint32_t>(part.argument);
                g.Call(part.at);
                break;
            }
        }
    }

    // The common end: the frame's zoom, the scrolls, every layer to the board
    constexpr Part kZoom = Routine(kFrameZoom), kScrolls = Routine(kLayerScrolls), kBoard = Routine(kToBoard);


    // --- the stages' routines (BG000 ...): their layers, and their steps' tables in ROM ---

    // 060BBCE4 (BG000): Gill's (his entrance's pan across, Gill_Appear_Flag)
    void Gill(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E398), StepFrom(0, 0x0619E3A4), kZoom, kScrolls, kBoard});
    }
    // 060BC0B0 (BG010): Alex's and Ken's (New York)
    void Alex(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E3B8), StepFrom(0, 0x0619E3B0), LayerRoutine(2, 0x060BC244), kZoom, kScrolls, kBoard});
    }
    // 060BC2FC (BG020): Ryu's; the far layer and the layer of objects in step with it (5) to the board apart, one the
    // other way
    void Ryu(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E3C8), StepFrom(0, 0x0619E3C0), StepFrom(2, 0x0619E3C0), StepFrom(5, 0x0619E3D0), kZoom, kScrolls,
                RoutineFor(kOneToBoard, 1), RoutineFor(kOneToBoardOther, 0), RoutineFor(kOneToBoard, 2)});
    }
    // 060BC58C (BG030): Yun's
    void Yun(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E3E0), StepFrom(0, 0x0619E3D8), kZoom, kScrolls, kBoard});
    }
    // 060BC71C (BG040): Dudley's (its layers to the board the other way)
    void Dudley(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E3F4), StepFrom(0, 0x0619E3E8), kZoom, kScrolls, Routine(kToBoardOther)});
    }
    // 060BC8BC (BG050): Necro's
    void Necro(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E408), StepFrom(0, 0x0619E400), StepFrom(2, 0x0619E410), kZoom, kScrolls, kBoard});
    }
    // 060BCDC8 (BG060): Hugo's
    void Hugo(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E438), StepFrom(0, 0x0619E430), LayerRoutine(2, 0x060BCF90), kZoom, kScrolls, kBoard});
    }
    // 060BD064 (BG070): Ibuki's
    void Ibuki(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E448), StepFrom(0, 0x0619E440), LayerRoutine(2, 0x060BD294), kZoom, kScrolls, kBoard});
    }
    // 060BD364 (BG080): Elena's (three far layers with the same steps)
    void Elena(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E458), StepFrom(0, 0x0619E450), StepFrom(2, 0x0619E460), StepFrom(5, 0x0619E460), StepFrom(6, 0x0619E460),
                kZoom, kScrolls, kBoard});
    }
    // 060BD684 (BG090): Oro's (his entrance from the sky, and his win's: the layers' own routines)
    void Oro(Guest &g) {
        Run(g, {LayerRoutine(1, 0x060BD74A), LayerRoutine(0, 0x060BD6DC), LayerRoutine(2, 0x060BD800), LayerRoutine(6, 0x060BD800),
                LayerRoutine(5, 0x060BD800), kZoom, kScrolls, kBoard});
    }
    // 060BDB14 (BG100): Yang's
    void Yang(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E488), StepFrom(0, 0x0619E480), kZoom, kScrolls, kBoard});
    }
    // 060BDE58 (BG120): Sean's
    void Sean(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E4A8), StepFrom(0, 0x0619E4A0), LayerRoutine(2, 0x060BDFF0), LayerRoutine(6, 0x060BDFF0),
                LayerRoutine(5, 0x060BDFF0), kZoom, kScrolls, kBoard});
    }
    // 060BE0E4 (BG130): Urien's
    void Urien(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E4B0), StepFrom(0, 0x0619E4B8), LayerRoutine(2, 0x060BE22A), kZoom, kScrolls, kBoard});
    }
    // 060BE2FC (BG140): Akuma's and Shin Akuma's
    void Akuma(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E4C0), StepFrom(0, 0x0619E4C8), LayerRoutine(2, 0x060BE49C), kZoom, kScrolls, kBoard});
    }
    // 060BE5D8 (BG150): Chun-Li's
    void ChunLi(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E4D8), StepFrom(0, 0x0619E4D0), LayerRoutine(2, 0x060BE77C), kZoom, kScrolls, kBoard});
    }
    // 060BE85C (BG160): Makoto's
    void Makoto(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E4E8), StepFrom(0, 0x0619E4E0), LayerRoutine(2, 0x060BE9FC), kZoom, kScrolls, kBoard});
    }
    // 060BCB14 (BG180): Q's and Twelve's
    void Q(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E420), StepFrom(0, 0x0619E418), StepFrom(2, 0x0619E428), kZoom, kScrolls, kBoard});
    }
    // 060BEAB4 (BG190): Remy's
    void Remy(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E4F8), StepFrom(0, 0x0619E4F0), LayerRoutine(2, 0x060BEC48), kZoom, kScrolls, kBoard});
    }
    // 060BED24 (Bonus_bg): the bonus game's (no zoom)
    void Bonus(Guest &g) {
        Run(g, {StepFrom(1, 0x0619E508), StepFrom(0, 0x0619E500), kScrolls, kBoard});
    }


    // --- a layer's routines (the layer being worked on: kLayerAt) ---

    // A layer's fields (bgw: stage_camera.cpp)
    constexpr uint32_t kFamily = 0x02;      // word: its number (fam_no)
    constexpr uint32_t kTurn = 0x06;        // word: the step within its step (r_no_1)
    constexpr uint32_t kSpeedAcross = 0x10, kSpeedUp = 0x14; // longs, 16.16 (speed_x, speed_y)
    constexpr uint32_t kShownAcross = 0x18, kShownUp = 0x1C; // longs, 16.16: where it is shown (xy)
    constexpr uint32_t kAcross = 0x20;      // long, 16.16: where it is (wxy[0])
    constexpr uint32_t kWasAcross = 0x30;   // word (old_pos_x)
    constexpr uint32_t kWide = 0x34;        // long: a layer wider than the board's 1024 (zuubun)
    constexpr uint32_t kFromLeft = 0x40;    // word: an entrance's pan from the left (u_line)
    constexpr uint32_t kHomeAcross = 0x48, kHomeUp = 0x4A; // words: its place with the camera at its start (pos_x_work, pos_y_work)
    constexpr uint32_t kHeld = 0x50;        // long (hos_xy[0]: the game sets it, some stages a word of it only)
    constexpr uint32_t kCount = 0x60;       // word: frames to wait (free)
    constexpr uint32_t kTop = 0x6C, kTopEnd = 0x6E; // words: its top (y_limit, y_limit2)

    constexpr uint32_t kMoveAcross = 0x060B886E, kMoveUp = 0x060B8926; // (bg_x_move_check, bg_y_move_check: stage_camera.cpp)
    constexpr uint32_t kWithLayer = 0x060B630E; // r4 a layer: its objects placed with it (sync_fam_set3)
    constexpr uint32_t kStageEntrance = 0x0202805B; // byte: a stage's own entrance on (bg_app)
    constexpr uint32_t kArea = kStage + 0x05;       // byte: the stage's part (bg_w.area: 0 a fight's own)
    constexpr uint32_t kPlayerOne = 0x02068C6C;     // (plw[0]); +0x3C0 its character (player_number)

    uint32_t LayerNow(Guest &g) {
        return g.Read32(kLayerAt);
    }
    int32_t Word(Guest &g, uint32_t at) {
        return static_cast<int16_t>(g.Read16(at));
    }
    void SetWord(Guest &g, uint32_t at, int32_t value) {
        g.Write16(at, static_cast<uint16_t>(value));
    }
    void StepOn(Guest &g, uint32_t layer, uint32_t field = kStep) {
        SetWord(g, layer + field, Word(g, layer + field) + 1);
    }

    // A layer's first step (bg0101_init00 and the like): its step counted on, and its place across with the camera at
    // its start (`home`) where it is, is shown and was; kHeld the place it is shown (a long, or by some stages its low
    // word only: the place's fraction, as the game's code has it)
    enum Held { kHeldLong, kHeldWord };
    void Home(Guest &g, uint32_t layer, int32_t home, Held held) {
        StepOn(g, layer);
        SetWord(g, layer + kHomeAcross, home);
        SetWord(g, layer + kShownAcross, home);
        SetWord(g, layer + kWasAcross, home);
        const uint32_t shown = g.Read32(layer + kShownAcross);
        g.Write32(layer + kAcross, shown);
        if (held == kHeldLong) {
            g.Write32(layer + kHeld, shown);
        } else {
            g.Write16(layer + kHeld, static_cast<uint16_t>(shown));
        }
    }

    // A routine of the game's called (its arguments in r4 and r5: as many as it takes)
    struct CallTo {
        uint32_t at;
        int arguments;
        int32_t r4, r5;
    };
    void CallAll(Guest &g, std::initializer_list<CallTo> calls) {
        for (const CallTo &c : calls) {
            if (c.arguments > 0) {
                g.R(4) = static_cast<uint32_t>(c.r4);
            }
            if (c.arguments > 1) {
                g.R(5) = static_cast<uint32_t>(c.r5);
            }
            g.Call(c.at);
        }
    }
    constexpr CallTo Plain(uint32_t at) {
        return {at, 0, 0, 0};
    }
    constexpr CallTo With(uint32_t at, int32_t r4) {
        return {at, 1, r4, 0};
    }
    constexpr CallTo With(uint32_t at, int32_t r4, int32_t r5) {
        return {at, 2, r4, r5};
    }
    // (what a stage loads as its fighters' layer is set up: r4 which, r5 1)
    constexpr uint32_t kLoad = 0x06087024;

    // A layer's first step: home (above), not wider than the board, and the stage's things set going (its animated
    // parts, effects each run by a routine of the game's: the calls)
    void SetUp(Guest &g, int32_t home, Held held, std::initializer_list<CallTo> calls) {
        const uint32_t layer = LayerNow(g);
        Home(g, layer, home, held);
        g.Write32(layer + kWide, 0);
        CallAll(g, calls);
    }
    template <int32_t H, Held W>
    void SetUpOnly(Guest &g) {
        SetUp(g, H, W, {});
    }

    // A far layer's first step (bg020_sync_init and the like): home, its top 240 and its place up 0, its speeds; its
    // objects then placed with layer `family`
    void SetUpFar(Guest &g, int32_t speedAcross, int32_t speedUp, int family) {
        const uint32_t layer = LayerNow(g);
        Home(g, layer, 0x200, kHeldLong);
        g.Write32(layer + kWide, 0);
        SetWord(g, layer + kTopEnd, 0xF0);
        SetWord(g, layer + kTop, 0xF0);
        SetWord(g, layer + kHomeUp, 0);
        SetWord(g, layer + kShownUp, 0);
        g.Write32(layer + kSpeedAcross, static_cast<uint32_t>(speedAcross));
        g.Write32(layer + kSpeedUp, static_cast<uint32_t>(speedUp));
        CallAll(g, {With(kWithLayer, family)});
    }
    // ... and its later steps (bg020_sync_move): moved as the camera is, times its speeds, its objects with it
    void MoveFar(Guest &g, int family) {
        CallAll(g, {Plain(kMoveAcross), Plain(kMoveUp), With(kWithLayer, family)});
    }

    // 060BACE4 (bg_move_common): a nearer or farther layer moved across and up as the camera is, times its speeds
    void MoveWithCamera(Guest &g) {
        CallAll(g, {Plain(kMoveAcross), Plain(kMoveUp)});
    }
    // A step that only counts the layer's step on (its setting up done by another's)
    void StepOnly(Guest &g) {
        StepOn(g, LayerNow(g));
    }


    // --- Gill's stage: his entrance, the camera panning across to him ---
    constexpr uint32_t kGillHere = 0x060C13F8; // (gill_appear_check: not 0, no entrance of Gill's to show)
    constexpr int32_t kGillHome = 0x1D0;
    // The pan's start: 224 columns to the right of home (times the layer's speed) with Gill player 1, else 192 to the left
    void PanStart(Guest &g, uint32_t layer) {
        const bool fromRight = Word(g, kPlayerOne + 0x3C0) == 0;
        SetWord(g, layer + kFromLeft, fromRight ? 0 : 1);
        const uint32_t speed = g.Read32(layer + kSpeedAcross);
        g.Write32(layer + kShownAcross, fromRight ? g.Read32(layer + kShownAcross) + speed * 0xE0 : g.Read32(layer + kShownAcross) - speed * 0xC0);
        const uint32_t shown = g.Read32(layer + kShownAcross);
        g.Write32(layer + kAcross, shown);
        g.Write32(layer + kHeld, shown);
        g.Write16(layer + kWasAcross, static_cast<uint16_t>(shown)); // (the place's fraction, as the game's code has it)
    }
    // 060BBD76 (bg0001_init00): the fighters' layer: home, the stage's things; in a fight's own part with Gill to enter,
    // the pan's start; else its moves at once
    void GillFighters(Guest &g) {
        const uint32_t layer = LayerNow(g);
        Home(g, layer, kGillHome, kHeldWord);
        g.Write32(layer + kWide, 0);
        CallAll(g, {With(kLoad, 0xDC40, 1), Plain(0x060DD612), With(0x060EB982, 7), With(0x060EEA50, 2)});
        if (g.Read8(kArea) != 0) {
            SetWord(g, layer + kStep, 2);
            return;
        }
        g.Call(kGillHere);
        if (g.R(0) != 0) {
            SetWord(g, layer + kStep, 2);
            return;
        }
        PanStart(g, layer);
    }
    // 060BBEDA (bg0000_init00): the nearer layer: home; with Gill to enter, the stage's entrance on and the pan's start
    void GillNear(Guest &g) {
        const uint32_t layer = LayerNow(g);
        Home(g, layer, kGillHome, kHeldLong);
        g.Write32(layer + kWide, 0);
        g.Call(kGillHere);
        if (g.R(0) != 0) {
            SetWord(g, layer + kStep, 2);
            g.Write8(kStageEntrance, 0);
            return;
        }
        g.Write8(kStageEntrance, 1);
        PanStart(g, layer);
    }
    // 060BBFCA (bg0000_demo): the pan: 30 frames' wait, then across by the layer's speed a frame to its home, then the
    // stage's entrance over and the layer's moves begun
    void GillPan(Guest &g) {
        const uint32_t layer = LayerNow(g);
        switch (Word(g, layer + kTurn)) {
        case 0:
            StepOn(g, layer, kTurn);
            SetWord(g, layer + kCount, 30);
            break;
        case 1:
            SetWord(g, layer + kCount, Word(g, layer + kCount) - 1);
            if (Word(g, layer + kCount) <= 0) {
                StepOn(g, layer, kTurn);
            }
            break;
        case 2: {
            const bool fromLeft = Word(g, layer + kFromLeft) != 0;
            const uint32_t speed = g.Read32(layer + kSpeedAcross);
            g.Write32(layer + kAcross, fromLeft ? g.Read32(layer + kAcross) + speed : g.Read32(layer + kAcross) - speed);
            if (fromLeft ? Word(g, layer + kAcross) > kGillHome : Word(g, layer + kAcross) < kGillHome) {
                StepOn(g, layer, kTurn);
                SetWord(g, layer + kAcross, kGillHome);
                SetWord(g, layer + kAcross + 2, 0);
                g.Write32(layer + kShownAcross, g.Read32(layer + kAcross));
                SetWord(g, layer + kWasAcross, kGillHome);
            }
            break;
        }
        case 3:
            g.Write8(kStageEntrance, 0);
            SetWord(g, layer + kStep, 2);
            break;
        }
    }


    // --- Alex's (and Ken's) stage, Ryu's, Yun's, Dudley's, Necro's ---
    // A far layer (2) with its own routine (bg0103 and the like): set up (its speeds, its top, its number 2, its place up
    // 0 and the fractions of its places cleared: after its places are copied, or by Ibuki's before), then moved; its
    // objects with it
    struct Far {
        int32_t speedAcross, speedUp, top;
        bool clearedFirst;
    };
    template <const Far &F>
    void FarLayer(Guest &g) {
        const uint32_t layer = LayerNow(g);
        switch (Word(g, layer + kStep)) {
        case 0:
            if (F.clearedFirst) {
                SetWord(g, layer + kFamily, 2);
                SetWord(g, layer + kShownUp + 2, 0);
                SetWord(g, layer + kShownAcross + 2, 0);
            }
            Home(g, layer, 0x200, kHeldLong);
            SetWord(g, layer + kHomeUp, 0);
            SetWord(g, layer + kShownUp, 0);
            SetWord(g, layer + kFamily, 2);
            SetWord(g, layer + kShownUp + 2, 0);
            SetWord(g, layer + kShownAcross + 2, 0);
            SetWord(g, layer + kTopEnd, F.top);
            SetWord(g, layer + kTop, F.top);
            g.Write32(layer + kSpeedAcross, static_cast<uint32_t>(F.speedAcross));
            g.Write32(layer + kSpeedUp, static_cast<uint32_t>(F.speedUp));
            CallAll(g, {With(kWithLayer, 2)});
            break;
        case 1:
            MoveFar(g, 2);
            break;
        }
    }
    constexpr Far kAlexFar = {0xB000, 0xE000, 0xF0, false};   // 060BC244 (bg0103)
    constexpr Far kHugoFar = {0x10800, 0x10000, 0xF0, false}; // 060BCF90
    constexpr Far kIbukiFar = {0xE000, 0xF800, 0xC0, true};   // 060BD294

    // A layer's first step with its place shown measured from the picture's left edge (Hugo's and Q's nearer layer):
    // its step within its step 0, home 0x200, and kHeld its place less the picture's left edge (from its middle), up 0;
    // and the stage's things set going
    constexpr uint32_t kLeftOfMiddle = kStage + 0x28; // word (pos_offset)
    void SetUpFromEdge(Guest &g, std::initializer_list<CallTo> calls) {
        const uint32_t layer = LayerNow(g);
        SetWord(g, layer + kTurn, 0);
        StepOn(g, layer);
        g.Write32(layer + kWide, 0);
        SetWord(g, layer + kHomeAcross, 0x200);
        SetWord(g, layer + kShownAcross, 0x200);
        SetWord(g, layer + kWasAcross, 0x200);
        g.Write32(layer + kAcross, g.Read32(layer + kShownAcross));
        SetWord(g, layer + kHeld, Word(g, layer + kAcross) - Word(g, kLeftOfMiddle));
        SetWord(g, layer + kHeld + 2, 0);
        g.Write32(layer + kAcross + 4, 0);
        g.Write32(layer + kShownUp, 0);
        CallAll(g, calls);
    }
    void AlexFighters(Guest &g) { // 060BC150
        SetUp(g, 0x200, kHeldWord, {With(kLoad, 0xD8D0, 1), Plain(0x060DD7A4), Plain(0x060DD486), Plain(0x060DD612), Plain(0x060E0F34)});
    }
    void RyuFighters(Guest &g) { // 060BC470
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0xDF20, 1), Plain(0x060DD612), Plain(0x060F314E)});
    }
    void RyuFar(Guest &g) { // 060BC4E6 (bg020_sync_init)
        SetUpFar(g, 0xE000, 0xE000, 5);
    }
    void RyuFarMove(Guest &g) { // 060BC548 (bg020_sync_move)
        MoveFar(g, 5);
    }
    void YunNear(Guest &g) { // 060BC612
        SetUp(g, 0x200, kHeldLong, {With(0x060E3A00, 5)});
    }
    void YunFighters(Guest &g) { // 060BC678
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0xD990, 1), Plain(0x060DD486), Plain(0x060DD612), Plain(0x060DDC5E), Plain(0x060F0E6E), Plain(0x06112E52)});
    }
    void DudleyNear(Guest &g) { // 060BC7AE
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0xDB40, 1), With(0x060E10AE, 1), Plain(0x060DD612), With(0x060EB982, 3), Plain(0x060ED574)});
    }
    void NecroFighters(Guest &g) { // 060BC9D2
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0x9000, 1), Plain(0x060DD486), Plain(0x060DD612)});
    }
    void NecroFar(Guest &g) { // 060BCA80 (bg050_sync_init)
        SetUpFar(g, 0xE000, 0xE000, 2);
    }
    void NecroFarMove(Guest &g) { // 060BCAE2 (bg050_sync_move)
        MoveFar(g, 2);
    }


    // --- Q's (and Twelve's) stage, Hugo's, Ibuki's ---
    constexpr uint32_t kCameraAcross = 0x060B8620, kCameraUp = 0x060B87B8; // (stage_camera.cpp)
    constexpr uint32_t kChaseOn = 0x060B7D5E; // a super art's zoom's scripted move (bg_chase_move: stage_zoom.cpp)
    void QFighters(Guest &g) { // 060BCC68
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0x9000, 1), Plain(0x060DD486), Plain(0x060DD612), Plain(0x060E53FC), With(0x060EB982, 4),
                                    With(0x060E3A00, 6), With(0x060E3A00, 7)});
    }
    // 060BCCD0: the fighters' layer's moves (bg_base_move_common's, the zoom's scripted move by bg_chase_move)
    void QFightersMove(Guest &g) {
        CallAll(g, {Plain(kCameraAcross), Plain(kCameraUp), Plain(kChaseOn)});
    }
    void QFar(Guest &g) { // 060BCD0C
        SetUpFar(g, 0xE000, 0xE000, 2);
    }
    void QFarMove(Guest &g) { // 060BCD6E
        MoveFar(g, 2);
    }
    void HugoNear(Guest &g) { // 060BCE68
        SetUpFromEdge(g, {});
    }
    void HugoFighters(Guest &g) { // 060BCF0C
        SetUpFromEdge(g, {With(kLoad, 0xDA70, 1), Plain(0x060DD486), With(0x060EEA50, 0), With(0x060EEA50, 1), With(0x060EB982, 1), Plain(0x060E5A6E)});
    }
    // 060BD104: Ibuki's nearer layer (0): home 0, then a picture of its own made (its number from 0x06137854, its
    // place from 0x061376C6, set up by 0x0612E35E, 0x0612D9EC, 0x0612E054, 0x060B8E3C), 227 wide (kWide), and its
    // fields for it
    void IbukiNear(Guest &g) {
        Home(g, LayerNow(g), 0, kHeldLong);
        g.Call(0x0610D1B6);
        {
            const uint32_t at = LayerNow(g) + 0x46;
            g.R(4) = 1;
            g.Call(0x06137854);
            g.Write16(at, static_cast<uint16_t>(g.R(0)));
        }
        {
            const uint32_t at = LayerNow(g) + 0x2C;
            g.R(4) = static_cast<uint32_t>(Word(g, LayerNow(g) + 0x46));
            g.Call(0x061376C6);
            g.Write32(at, g.R(0));
        }
        g.R(4) = 0;
        g.R(5) = g.Read32(LayerNow(g) + 0x2C);
        g.Call(0x0612E35E);
        g.R(4) = g.Read32(LayerNow(g) + 0x2C);
        g.R(5) = 0;
        g.R(6) = 0x1000;
        g.Call(0x0612D9EC);
        CallAll(g, {With(0x0612E054, 16), With(0x060B8E3C, 0)});
        const uint32_t layer = LayerNow(g);
        g.Write32(layer + kWide, 0xE3);
        SetWord(g, layer + 0x38, 0x200);
        SetWord(g, layer + 0x40, 0x18F);
        SetWord(g, layer + 0x42, 112);
        g.Write32(layer + 0x3C, g.Read32(layer + 0x2C) + 0x800);
    }
    // 060BD1AE: ... its moves, and its picture's (0x060B9498, r4 0)
    void IbukiNearMove(Guest &g) {
        CallAll(g, {Plain(kMoveAcross), Plain(kMoveUp), With(0x060B9498, 0)});
    }
    // --- Elena's stage ---
    // 060BD464: the nearer layer: set up; layer number 0's sets its stage's things going (0x060EBC5E)
    void ElenaNear(Guest &g) {
        SetUp(g, 0x200, kHeldLong, {});
        if (Word(g, LayerNow(g) + kFamily) == 0) {
            g.Call(0x060EBC5E);
        }
    }
    void ElenaFighters(Guest &g) { // 060BD504
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0xD880, 1), Plain(0x060DD486), Plain(0x060DD612), With(0x060EB982, 0), With(0x060E513A, 0)});
    }
    // 060BD58C: the far layers (2, 5, 6: the sky, the sea, the hills?), each its speeds by its number; its objects with it
    void ElenaFar(Guest &g) {
        const uint32_t layer = LayerNow(g);
        Home(g, layer, 0x200, kHeldLong);
        g.Write32(layer + kWide, 0);
        SetWord(g, layer + kTopEnd, 0xF0);
        SetWord(g, layer + kTop, 0xF0);
        SetWord(g, layer + kHomeUp, 0);
        SetWord(g, layer + kShownUp, 0);
        switch (Word(g, layer + kFamily)) {
        case 2:
            g.Write32(layer + kSpeedAcross, 0xC000);
            g.Write32(layer + kSpeedUp, 0xFC00);
            break;
        case 5:
            g.Write32(layer + kSpeedAcross, 0xB000);
            g.Write32(layer + kSpeedUp, 0xFC00);
            break;
        case 6:
            g.Write32(layer + kSpeedAcross, 0x7000);
            g.Write32(layer + kSpeedUp, 0xF100);
            break;
        }
        CallAll(g, {With(kWithLayer, Word(g, LayerNow(g) + kFamily))});
    }
    void ElenaFarMove(Guest &g) { // 060BD624
        CallAll(g, {Plain(kMoveAcross), Plain(kMoveUp)});
        CallAll(g, {With(kWithLayer, Word(g, LayerNow(g) + kFamily))});
    }


    // --- Oro's stage: his entrance from the sky (the camera starts 192 lines up and comes down), his win's (up) ---
    constexpr uint32_t kRunning = 0x0200EECC;   // word: not 0, the game's own work paused (EXE_flag)
    constexpr uint32_t kPaused = 0x0201136E;    // word (Game_pause)
    constexpr uint32_t kOroWin = 0x02068C6A;    // byte: Oro's win's pan up: 2 going up, 3 done (win_sp_flag)
    constexpr uint32_t kEntranceFree = 0x0202819C; // bytes by side: its entrance has let the camera go (Appear_free)
    constexpr uint32_t kEntranceWhich = 0x0202819E; // byte: both Oro: whose entrance it follows (Appear_hv)
    constexpr uint32_t kEntrancesOver = 0x02028194; // word: 2, both entrances over (Appear_end)
    constexpr uint32_t kPlayerTwo = 0x02069104;     // (plw[1])
    constexpr int kOro = 9;
    bool Paused(Guest &g) {
        return g.Read16(kRunning) != 0 || g.Read16(kPaused) != 0;
    }
    // A layer's place up, shown and real (16.16), moved by `by`
    void MoveUp(Guest &g, uint32_t layer, uint32_t by) {
        g.Write32(layer + kShownUp, g.Read32(layer + kShownUp) + by);
        g.Write32(layer + kAcross + 4, g.Read32(layer + kAcross + 4) + by);
    }
    // 060BD91E (bg090_demo_check): with Oro fighting in a fight's own part, the entrance's steps (step 1) from 192 lines
    // up (times the layer's speed up); else the layer's moves (step 2)
    void OroEntranceCheck(Guest &g) {
        const uint32_t layer = LayerNow(g);
        const bool oro = Word(g, kPlayerOne + 0x3C0) == kOro || Word(g, kPlayerTwo + 0x3C0) == kOro;
        if (!oro || g.Read8(kArea) != 0) {
            SetWord(g, layer + kStep, 2);
            return;
        }
        SetWord(g, layer + kStep, 1);
        const uint32_t up = g.Read32(LayerNow(g) + kSpeedUp) * 0xC0;
        g.Write32(LayerNow(g) + kAcross + 4, up);
        g.Write32(layer + kShownUp, up);
    }
    // 060BD964 (demo90_base): the entrance's steps: wait until the entrance followed (the Oro's: player 2's, or with
    // both Oro the one Appear_hv names) lets the camera go; then down 10 lines a frame (times the layer's speed up) to
    // 0; then wait for both entrances to be over, and the layer's moves begun
    void OroEntrance(Guest &g) {
        if (Paused(g)) {
            return;
        }
        const uint32_t layer = LayerNow(g);
        switch (Word(g, layer + kTurn)) {
        case 0: {
            int side = 0;
            const bool one = Word(g, kPlayerOne + 0x3C0) == kOro, two = Word(g, kPlayerTwo + 0x3C0) == kOro;
            if (one && two) {
                side = g.Read8(kEntranceWhich) != 0 ? 1 : 0;
            } else if (two) {
                side = 1;
            }
            if (g.Read8(kEntranceFree + static_cast<uint32_t>(side)) != 0) {
                StepOn(g, layer, kTurn);
            }
            break;
        }
        case 1:
            g.Write32(layer + kShownUp, g.Read32(layer + kShownUp) - g.Read32(layer + kSpeedUp) * 10);
            g.Write32(LayerNow(g) + kAcross + 4, g.Read32(LayerNow(g) + kAcross + 4) - g.Read32(LayerNow(g) + kSpeedUp) * 10);
            if (Word(g, LayerNow(g) + kShownUp) <= 0) {
                StepOn(g, LayerNow(g), kTurn);
                g.Write32(LayerNow(g) + kShownUp, 0);
                g.Write32(LayerNow(g) + kAcross + 4, 0);
            }
            break;
        case 2:
            if (Word(g, kEntrancesOver) == 2) {
                StepOn(g, layer);
                SetWord(g, LayerNow(g) + kTurn, 0);
                g.Write32(LayerNow(g) + kShownUp, 0);
                g.Write32(LayerNow(g) + kAcross + 4, 0);
            }
            break;
        }
    }
    // 060BDA52 (jijii_win_bg): Oro's win: the fighters' layer up 10 lines a frame until past 176, then the pan done
    void OroWinUp(Guest &g) {
        if (Paused(g)) {
            return;
        }
        const uint32_t layer = LayerNow(g);
        if (Word(g, layer + kTurn) != 0 || g.Read8(kOroWin) != 2) {
            return;
        }
        MoveUp(g, layer, 0xA0000);
        if (Word(g, LayerNow(g) + kShownUp) > 0xB0) {
            StepOn(g, LayerNow(g), kTurn);
            g.Write8(kOroWin, 3);
        }
    }
    // 060BDAA8 (jijii_win_bg2): ... another layer up with it, 10 lines a frame times its speed up, while the fighters'
    // layer's goes up
    void OroWinUpWith(Guest &g) {
        if (Paused(g)) {
            return;
        }
        if (Word(g, Layer(1) + kTurn) != 0 || g.Read8(kOroWin) != 2) {
            return;
        }
        const uint32_t by = g.Read32(LayerNow(g) + kSpeedUp) * 10;
        MoveUp(g, LayerNow(g), by);
    }
    // 060BD718 (bg0901_init00), 060BD7B0 (bg0902_init00): the nearer and the fighters' layers' first step: home, then
    // the entrance's check; the fighters' sets the stage's things going
    void OroNear(Guest &g) {
        const uint32_t layer = LayerNow(g);
        SetWord(g, layer + kHomeAcross, 0x200);
        SetWord(g, layer + kShownAcross, 0x200);
        SetWord(g, layer + kWasAcross, 0x200);
        g.Write32(layer + kAcross, g.Read32(layer + kShownAcross));
        g.Write32(layer + kHeld, g.Read32(layer + kShownAcross));
        g.Write32(layer + kWide, 0);
        OroEntranceCheck(g);
    }
    void OroFighters(Guest &g) {
        OroNear(g);
        CallAll(g, {With(kLoad, 0xDEE0, 1), Plain(0x060DD486), Plain(0x060DD612), Plain(0x060F057E)});
    }
    // 060BD6DC (bg0901), 060BD74A (bg0902): the nearer and the fighters' layers: in Oro's win, up with it; else their
    // step from the stage's tables
    void OroNearLayer(Guest &g) {
        if (g.Read8(kOroWin) != 0) {
            g.Call(0x060BDAA8);
            return;
        }
        g.Call(g.Read32(0x0619E468 + static_cast<uint32_t>(Word(g, LayerNow(g) + kStep) * 4)));
    }
    void OroFightersLayer(Guest &g) {
        if (g.Read8(kOroWin) != 0) {
            g.Call(0x060BDA52);
            return;
        }
        g.Call(g.Read32(0x0619E474 + static_cast<uint32_t>(Word(g, LayerNow(g) + kStep) * 4)));
    }
    // 060BD800 (bg_fam0900): the far layers (2, 6, 5): in Oro's win up with it; else set up (speeds by its number, its
    // top 240), the entrance's steps, then moved; their objects with them
    void OroFar(Guest &g) {
        const uint32_t layer = LayerNow(g);
        if (g.Read8(kOroWin) != 0) {
            g.Call(0x060BDAA8);
            CallAll(g, {With(kWithLayer, Word(g, LayerNow(g) + kFamily))});
            return;
        }
        switch (Word(g, layer + kStep)) {
        case 0: {
            Home(g, layer, 0x200, kHeldLong);
            SetWord(g, layer + kHomeUp, 0);
            SetWord(g, layer + kShownUp, 0);
            const int family = Word(g, layer + kFamily);
            g.Write32(layer + kSpeedAcross, family == 2 ? 0xC000 : family == 6 ? 0x9000 : 0x12000);
            g.Write32(layer + kSpeedUp, 0x10000);
            SetWord(g, layer + kShownUp + 2, 0);
            SetWord(g, layer + kShownAcross + 2, 0);
            SetWord(g, layer + kTopEnd, 0xF0);
            SetWord(g, layer + kTop, 0xF0);
            g.Call(0x060BD91E);
            break;
        }
        case 1:
            g.Call(0x060BD964);
            break;
        case 2:
            CallAll(g, {Plain(kMoveAcross), Plain(kMoveUp)});
            break;
        default:
            return;
        }
        CallAll(g, {With(kWithLayer, Word(g, LayerNow(g) + kFamily))});
    }


    void IbukiFighters(Guest &g) { // 060BD240
        SetUp(g, 0x200, kHeldLong, {With(kLoad, 0xDCA0, 1), Plain(0x060DD612), With(0x060F6FF8, 3), Plain(0x0610E076)});
    }


    // --- the lifts ---
    template <void (*F)(Guest &)>
    bool Lifted(Guest &g) {
        F(g);
        g.Return(60);
        return true;
    }

    // (free: r0-r7 and T, nothing reading them after: see the head of this file)
    constexpr uint32_t kFree = 0x100FF;
    const Lift kLifts[] = {
        {"StageGill", 0x060BBCE4, 0x66, 0x30D3C5FE2D0992BFull, Lifted<Gill>, true, kFree},
        {"StageAlex", 0x060BC0B0, 0x78, 0x406EAAB3D9D0CFFDull, Lifted<Alex>, true, kFree},
        {"StageRyu", 0x060BC2FC, 0xC0, 0x6C9BC91F790AD311ull, Lifted<Ryu>, true, kFree},
        {"StageYun", 0x060BC58C, 0x5E, 0x80ECB9726A62FA09ull, Lifted<Yun>, true, kFree},
        {"StageDudley", 0x060BC71C, 0x66, 0x7F9911D7E75E9BD3ull, Lifted<Dudley>, true, kFree},
        {"StageNecro", 0x060BC8BC, 0x90, 0xC2FB6869A8334A32ull, Lifted<Necro>, true, kFree},
        {"StageHugo", 0x060BCDC8, 0x78, 0xA6A5619B5B2F06BAull, Lifted<Hugo>, true, kFree},
        {"StageIbuki", 0x060BD064, 0x78, 0xCCB2BFECEEAADB2Bull, Lifted<Ibuki>, true, kFree},
        {"StageElena", 0x060BD364, 0xD8, 0x25C18345BECAB574ull, Lifted<Elena>, true, kFree},
        {"StageOro", 0x060BD684, 0x58, 0x2D32F2B0D9281C2Full, Lifted<Oro>, true, kFree},
        {"StageYang", 0x060BDB14, 0x5E, 0x5A3AFF74360F1249ull, Lifted<Yang>, true, kFree},
        {"StageSean", 0x060BDE58, 0x90, 0x4384EF3694160A15ull, Lifted<Sean>, true, kFree},
        {"StageUrien", 0x060BE0E4, 0x78, 0x37AC1FBBF839A919ull, Lifted<Urien>, true, kFree},
        {"StageAkuma", 0x060BE2FC, 0x78, 0x934BD47754EF4DA3ull, Lifted<Akuma>, true, kFree},
        {"StageChunLi", 0x060BE5D8, 0x78, 0x83F709A0C4E084C8ull, Lifted<ChunLi>, true, kFree},
        {"StageMakoto", 0x060BE85C, 0x78, 0xE909E6F017F91331ull, Lifted<Makoto>, true, kFree},
        {"StageQ", 0x060BCB14, 0x90, 0x08C497B141B75AB2ull, Lifted<Q>, true, kFree},
        {"StageRemy", 0x060BEAB4, 0x78, 0x1205BA60E1ED9BA0ull, Lifted<Remy>, true, kFree},
        {"StageBonus", 0x060BED24, 0x58, 0x2D168912B1C4BFEFull, Lifted<Bonus>, true, kFree},
        // (the layers' routines)
        {"StageMoveWithCamera", 0x060BACE4, 0xE, 0xDF3593D3EE365783ull, Lifted<MoveWithCamera>, true, kFree},
        {"StageGillFighters", 0x060BBD76, 0x138, 0xDBE4D0486C6FABE4ull, Lifted<GillFighters>, true, kFree},
        {"StageGillPan", 0x060BBFCA, 0xD2, 0x634F3277DFF910AAull, Lifted<GillPan>, true, kFree},
        {"StageGillNear", 0x060BBEDA, 0xF0, 0xFB2F940534B41BFEull, Lifted<GillNear>, true, kFree},
        {"StageAlexSetUp", 0x060BC1D2, 0x36, 0xC45E0F74BAFD2FAEull, Lifted<SetUpOnly<0x200, kHeldLong>>, true, kFree},
        {"StageAlexFighters", 0x060BC150, 0x5A, 0x803729F1F364BFC0ull, Lifted<AlexFighters>, true, kFree},
        {"StageAlexFar", 0x060BC244, 0x9A, 0xA33C0F39BE1BCDDCull, Lifted<FarLayer<kAlexFar>>, true, kFree},
        {"StageRyuSetUp", 0x060BC3E4, 0x36, 0x236F16E74F6FAC63ull, Lifted<SetUpOnly<0x200, kHeldLong>>, true, kFree},
        {"StageRyuFighters", 0x060BC470, 0x4E, 0xD288753D6492B550ull, Lifted<RyuFighters>, true, kFree},
        {"StageRyuFar", 0x060BC4E6, 0x62, 0x76FE2951B94F1359ull, Lifted<RyuFar>, true, kFree},
        {"StageRyuFarMove", 0x060BC548, 0x16, 0x19C734DD6AAF6D0Dull, Lifted<RyuFarMove>, true, kFree},
        {"StageYunNear", 0x060BC612, 0x3E, 0xDEA289BD21BB8ADFull, Lifted<YunNear>, true, kFree},
        {"StageYunFighters", 0x060BC678, 0x60, 0x66C737C9F7399F6Full, Lifted<YunFighters>, true, kFree},
        {"StageDudleyNear", 0x060BC7AE, 0x5A, 0x1CA447E6F0414E84ull, Lifted<DudleyNear>, true, kFree},
        {"StageDudleyNearOn", 0x060BC808, 0xC, 0xFE4285EC89082566ull, Lifted<StepOnly>, true, kFree},
        {"StageDudleySetUp", 0x060BC840, 0x36, 0x967670F7B8F52DDCull, Lifted<SetUpOnly<0x200, kHeldLong>>, true, kFree},
        {"StageDudleyFightersOn", 0x060BC876, 0xC, 0x10C20D00EA294B6Aull, Lifted<StepOnly>, true, kFree},
        {"StageNecroSetUp", 0x060BC974, 0x36, 0x0DC419309D948B58ull, Lifted<SetUpOnly<0x200, kHeldLong>>, true, kFree},
        {"StageNecroFighters", 0x060BC9D2, 0x4E, 0x3CB68A5C9C561955ull, Lifted<NecroFighters>, true, kFree},
        {"StageNecroFar", 0x060BCA80, 0x62, 0x527755ECDD6D6398ull, Lifted<NecroFar>, true, kFree},
        {"StageNecroFarMove", 0x060BCAE2, 0x16, 0x27636A5F32E8EA55ull, Lifted<NecroFarMove>, true, kFree},
        {"StageQSetUp", 0x060BCBCC, 0x36, 0x9EE1C214D3F4CD68ull, Lifted<SetUpOnly<0x200, kHeldLong>>, true, kFree},
        {"StageQMove", 0x060BCC02, 0xE, 0x966A8C991BF7DE1Aull, Lifted<MoveWithCamera>, true, kFree},
        {"StageQFighters", 0x060BCC68, 0x68, 0x0ACD3352FB03491Cull, Lifted<QFighters>, true, kFree},
        {"StageQFightersMove", 0x060BCCD0, 0x14, 0x65E2ADD4F0F5075Full, Lifted<QFightersMove>, true, kFree},
        {"StageQFar", 0x060BCD0C, 0x62, 0x1D673C154B52018Dull, Lifted<QFar>, true, kFree},
        {"StageQFarMove", 0x060BCD6E, 0x16, 0x3C02A3E8F76F724Bull, Lifted<QFarMove>, true, kFree},
        {"StageHugoNear", 0x060BCE68, 0x56, 0xEA177FF6619FF5ACull, Lifted<HugoNear>, true, kFree},
        {"StageHugoFighters", 0x060BCF0C, 0x84, 0xC7E945583D8B89CEull, Lifted<HugoFighters>, true, kFree},
        {"StageHugoFar", 0x060BCF90, 0x9A, 0x128AD9DDD521B44Full, Lifted<FarLayer<kHugoFar>>, true, kFree},
        {"StageIbukiNear", 0x060BD104, 0xAA, 0xC2792E598979479Aull, Lifted<IbukiNear>, true, kFree},
        {"StageIbukiNearMove", 0x060BD1AE, 0x16, 0xAE66E314D3E18697ull, Lifted<IbukiNearMove>, true, kFree},
        {"StageIbukiFighters", 0x060BD240, 0x54, 0x704382DBBC13FE98ull, Lifted<IbukiFighters>, true, kFree},
        {"StageIbukiFar", 0x060BD294, 0x9C, 0xC188C29CA1C4F73Full, Lifted<FarLayer<kIbukiFar>>, true, kFree},
        {"StageElenaNear", 0x060BD464, 0x48, 0x080BDB3944E7C605ull, Lifted<ElenaNear>, true, kFree},
        {"StageElenaFighters", 0x060BD504, 0x60, 0x19AA26236AA33D63ull, Lifted<ElenaFighters>, true, kFree},
        {"StageElenaFar", 0x060BD58C, 0x98, 0x2CC05BCA2016D5EDull, Lifted<ElenaFar>, true, kFree},
        {"StageElenaFarMove", 0x060BD624, 0x1C, 0xE10DFD6566D91F2Aull, Lifted<ElenaFarMove>, true, kFree},
        {"StageOroEntranceCheck", 0x060BD91E, 0x46, 0x8E13DA08CDC34C12ull, Lifted<OroEntranceCheck>, true, kFree},
        {"StageOroEntrance", 0x060BD964, 0xEE, 0xC9E51D9703A797A4ull, Lifted<OroEntrance>, true, kFree},
        {"StageOroWinUp", 0x060BDA52, 0x56, 0x35C84C61C145ABB7ull, Lifted<OroWinUp>, true, kFree},
        {"StageOroWinUpWith", 0x060BDAA8, 0x4C, 0xEA70C56CB49412E3ull, Lifted<OroWinUpWith>, true, kFree},
        {"StageOroNearLayer", 0x060BD6DC, 0x3C, 0x6658C91AB2DE4105ull, Lifted<OroNearLayer>, true, kFree},
        {"StageOroFightersLayer", 0x060BD74A, 0x3C, 0x274D7840C06D06FBull, Lifted<OroFightersLayer>, true, kFree},
        {"StageOroFar", 0x060BD800, 0x11E, 0x8FE92B0B772457FEull, Lifted<OroFar>, true, kFree},
        {"StageOroNear", 0x060BD718, 0x32, 0x02631853A4FEA820ull, Lifted<OroNear>, true, kFree},
        {"StageOroFighters", 0x060BD7B0, 0x50, 0x7F867BF370806035ull, Lifted<OroFighters>, true, kFree},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterStageParts() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
