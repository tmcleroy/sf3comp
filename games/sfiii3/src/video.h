// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The video hand-offs' places, by game (video_palette.cpp, video_registers.cpp, video_lists.cpp, video_textures.cpp,
// video_forms.cpp). The three games share their engine: New Generation's and 2nd Impact's routines that hand the video
// chip its frame do what 3rd Strike's do, most of them the same code, at other addresses and with their RAM elsewhere
// (a block of the engine's moved as a whole). So one set of rewrites is written against a game's places, and made for
// each game: 3rd Strike's run on its board (the rewrites), the stage games' only dry, on the stage board, to hand the
// native interface the guest stage's layers (games/sfiii3/NATIVE.md, "Guest stages"). Where a stage game's routine is
// compiled otherwise (2nd Impact's screen registers and zoom, New Generation's text colours), its steps were read and
// are the same; the stage board's own native check (CPS3RECOMP_NATIVE_CHECK=1 with a stage on) compares what is handed
// over with what its chips took.
#pragma once

#include "lift.h"

#include <cstdint>

namespace cps3recomp {

struct VideoPlaces {
    // --- the DMA queue (video_palette.cpp) ---
    uint32_t dmaState;       // long: 0 idle, 1 a character DMA, 2 a palette DMA from the queue, 3 one started at once
    uint32_t fillSet;        // long: the queues' set requests go to
    uint32_t drainSet;       // long: the set the DMAs take from
    uint32_t paletteQueue;   // 16 bytes a request, 0x200 a set
    uint32_t paletteCount;   // longs, by set: requests waiting
    uint32_t paletteNext;    // longs, by set: the next to start
    uint32_t charQueue;      // 12 bytes a transfer, 0xFFC a set
    uint32_t charCount;      // longs, by set
    uint32_t charNext;       // longs, by set
    uint32_t kindLog;        // words, 0x2EA bytes a set: 1 a character request, 2 a palette request
    uint32_t kindPlace;      // longs, by set
    uint32_t charListAt;     // long: where in character RAM the list is made (its low 20 bits)
    uint32_t charListBank;   // long: the bank it is in
    uint32_t textColourBase; // long: where the text layer's colours go (in the CPU's window on colour RAM)
    uint32_t charBank;       // word: the CPU's character RAM bank now (its low 3 bits)
    uint32_t charBankBefore; // word: the one before
    // --- the frame's registers (video_registers.cpp) ---
    uint32_t shakeX, shakeY;               // words: the whole picture's shake
    uint32_t objectOffsetX, objectOffsetY; // words: the objects' offsets
    uint32_t objectLayers;   // 16 bytes each: the next x (long), x (long), the next y, y
    uint32_t tilemapLayers;  // 16 bytes each: the same
    uint32_t tilemapWork;    // 10 bytes each: x, y, the settings' two words, the bases
    uint32_t tilemapOffsetX, tilemapOffsetY;
    uint32_t tilemapSettings; // words, four bytes a tilemap: the next settings' low bits, the settings'
    uint32_t tilemapFlips;    // ... the next flips, the flips
    uint32_t tilemapBases;    // longs, 16 bytes a tilemap: the next map base, the next line base, the map base, the line base
    uint32_t tileBanksAsked;  // byte: the tilemaps' tile banks to be set again
    uint32_t tileBankSettings, tileBankNext; // words: the tile banks' settings, and the next ones
    uint32_t tileMasks;       // (program) a word of mask and a word of the settings' bit, for each of three banks
    uint32_t textLayer;       // the text layer's x (next, now) and y (next, now), longs
    uint32_t textOffsetX, textShakeX, textOffsetY, textShakeY;
    uint32_t turnSet;         // byte: the screen's turn last set up
    uint32_t screenAsked;     // byte: the screen's registers asked for
    uint32_t mode;            // the mode's words: the picture's edges, eight words
    uint32_t sprites;         // word: the object DMA's word
    uint32_t textKind;        // long: the text layer's kind
    uint32_t textKinds;       // (program) eight bytes a kind: the text layer's settings
    uint32_t turnedOffsets;   // (program) 16 bytes a kind: the layers' offsets with the screen turned
    uint32_t zoomAsked;       // byte: the whole picture's zoom asked for
    uint32_t zoom;            // eight words: the zoom
    uint32_t centreX, centreY; // words: the zoom's centre
    // --- the object lists' heads (video_lists.cpp) ---
    uint32_t headsAsked;      // byte: the heads to be written this vertical blank
    uint32_t prioritySet;     // byte: which set of priorities the frame filled (1: priorities, 0: priorities2)
    uint32_t priorities, priorities2; // longs, 128 each: a priority's first node
    uint32_t screenPlace;     // words: the empty heads' block word, the first head's position, the pads'
    // --- character RAM's areas for the lists (video_textures.cpp, video_forms.cpp) ---
    uint32_t freePages, takenPages, ownerPages; // words
    uint32_t divideError;     // long: an error noted by the divide (by 0)
    // --- the routines they call (in a dry run: their native forms, video_forms.cpp) ---
    uint32_t setCharBank, restoreCharBank, setTileBanks, modeChanged, divide, takeArea, areaPlace;
};

// A routine of a game's: where it is, and its code's size and hash (tools/lifthash.py on that game's program)
struct VideoRoutine {
    uint32_t address, size;
    uint64_t hash;
};
// Each game's hand-off routines
struct VideoRoutines {
    VideoRoutine askPalette, startNext, textColours, objectScrolls, tilemapRegisters, screenRegisters, pictureZoom, textPalette, listHeads,
        clearListArea, emptyFrame;
};

extern const VideoPlaces kThirdStrikeVideo, kNewGenerationVideo, kSecondImpactVideo;

// The rewrites, for a game's places (video_*.cpp): each game's table made from them (RegisterVideo)
template <const VideoPlaces &P> bool AskPalette(Guest &g);
template <const VideoPlaces &P> bool StartNextDma(Guest &g);
template <const VideoPlaces &P> bool TextColours(Guest &g);
template <const VideoPlaces &P> bool ObjectScrolls(Guest &g);
template <const VideoPlaces &P> bool TilemapRegisters(Guest &g);
template <const VideoPlaces &P> bool ScreenRegisters(Guest &g);
template <const VideoPlaces &P> bool PictureZoom(Guest &g);
template <const VideoPlaces &P> bool TextPalette(Guest &g);
template <const VideoPlaces &P> bool ListHeads(Guest &g);
template <const VideoPlaces &P> bool ClearListArea(Guest &g);
template <const VideoPlaces &P> void EmptyFrame(Guest &g);
// ... and the native forms of what they call
template <const VideoPlaces &P> void RegisterVideoForms();

} // namespace cps3recomp
