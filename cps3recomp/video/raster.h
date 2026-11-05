// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The native interface's picture (docs/NATIVE.md), drawn on the CPU: the reference for a GPU renderer and the one the
// pixel check (level b) compares with renderer.cpp's from the board. Drawn as a GPU would: the frame's objects made
// into a draw list of quads (a 16x16 tile at a place and a drawn size, its palette row, flips, blend) and tilemap lines
// (a line of one of the four tilemaps at a line of the buffer), in the board's order, rasterised into a buffer of
// colour numbers, which is then resolved through the palette (with the picture's zoom) and the text layer put over it.
// The drawing rules are MAME's CPS-3 driver's (BSD-3-Clause), as renderer.cpp has them; the data is the interface's.
#pragma once

#include "native.h"

#include <cstdint>
#include <vector>

namespace cps3recomp::native {

// One thing to draw, in the board's order
struct DrawItem {
    enum Kind : uint8_t { kQuad, kLine } kind = kQuad;
    // a quad: the tile (in Textures), its palette row (colour, by granularity 64 or 256 colours), place and drawn size
    // (scale 16.16), flips and blend
    uint32_t tile = 0, colour = 0, scaleX = 0, scaleY = 0;
    int16_t x = 0, y = 0;
    bool granularity64 = false, flipX = false, flipY = false, blend = false;
    // a line: which tilemap, at which line of the buffer
    uint8_t tilemap = 0;
    int16_t line = 0;
    uint16_t group = 0; // (the main list's head it came from)
};

// The picture's size and zoom as the frame's registers set them
struct Screen {
    int width = 384, height = 224;
    uint32_t zoomX = 0x40, zoomY = 0x40;
    int clipRight = 383, clipBottom = 223; // (the buffer's part drawn into, zoomed)
};
Screen ScreenOf(const FrameRegisters &r);

// The frame's draw list from its objects
void BuildDrawList(const std::vector<Object> &objects, const FrameRegisters &r, const Screen &screen, std::vector<DrawItem> &out);

// The picture (0x00BBGGRR) from the interface: the draw list, the textures and palette, the tilemaps and line scroll
// tables in sprite RAM (the game's own memory, 512 KB big-endian) and the text layer's map and characters (SS RAM, the
// game's memory: 32 KB, a byte per two of the board's addresses)
class Rasteriser {
public:
    void Draw(const Video &v, const uint8_t *spriteRam, const uint8_t *ssRam, std::vector<uint32_t> &pixels, int &width, int &height);

private:
    static constexpr int kBufferWidth = 1024, kBufferHeight = 448;
    std::vector<uint32_t> m_buffer;
    std::vector<DrawItem> m_items;
};

} // namespace cps3recomp::native
