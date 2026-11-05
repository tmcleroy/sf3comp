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

#include <array>
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

// A tile as it is rasterised (a GPU's work item): the buffer's pixels it covers (sx..ex, sy..ey, clipped), the steps through
// its 16x16 texels (16.16: the index at sx, sy and the step per pixel, negative when flipped), its first texel's place
// in the textures, and what it writes: mode 0 its texel | base (the colour's row), 1 a blend (texel & 15 into bits
// 13-16), 2 a blend (texel & 1 into bit 15, and base); and whose it is (`producer`: 0 the game's, 1 a guest stage's)
struct Tile {
    int32_t sx = 0, sy = 0, ex = 0, ey = 0, xBase = 0, yIndex = 0, dx = 0, dy = 0, source = 0, base = 0, mode = 0, producer = 0;
};
// The frame made ready to draw: its draw list and tiles in the board's order, the picture's size, the buffer's part
// drawn into, the text layer's settings
struct Frame {
    Screen screen;
    std::vector<DrawItem> items;
    std::vector<Tile> tiles;
    int width = 0, height = 0, bufferWidth = 0, bufferHeight = 0, margin = 0;
    int textScrollY = 0;
    uint32_t textPalette = 0;
    // A guest producer's layers under the game's (a stage from another game, docs/NATIVE.md "Guest stages"): its tiles
    // in `tiles` too (Tile::producer 1), drawn into a buffer of their own, its picture's zoom (`guestScreen`), its
    // palette and textures (`guestVideo`). The picture is the guest's, with the game's drawn over it as one picture over
    // another (Rasteriser::overlay's rule: where the game drew nothing the guest's shows, and a blend of the game's over
    // nothing of its own scales the guest's colour by as much as it does the game's)
    bool guest = false;
    Screen guestScreen;
    int guestBufferWidth = 0, guestBufferHeight = 0;
    const Video *guestVideo = nullptr;
};
// Which of a frame's objects are drawn (none: all): a rule on each object, with its context. For a frame made of two
// producers' layers (a guest stage's under the game's: docs/NATIVE.md), each producer's part.
using KeepObject = bool (*)(const void *context, const Object &o, uint32_t index); // (index: its place in the frame's objects)
// (widescreen: `margin` columns more on each side, the world `shift` further left, as Renderer's)
// (`shiftY`: the objects drawn so many rows lower, as Renderer's)
void BuildFrame(const Video &v, const uint8_t *spriteRam, int margin, int shift, Frame &f, KeepObject keep = nullptr, const void *keepContext = nullptr,
                int shiftY = 0);
// ... and a guest producer's layers added under them (Frame::guest): its objects that `keep` says are its stage, with the
// same margin (and no shift: its own camera is put where the game's is); false (and none added) when its picture is not
// the game's size
bool AddGuest(const Video &guest, const uint8_t *guestSpriteRam, Frame &f, KeepObject keep, const void *keepContext);
// The blend's scales of a palette's 16 banks against its first (Rasteriser::Blended's, all of them): three a bank, red
// first, in 256ths (a GPU draws a game's blend over a guest's colour with them)
void BlendScales(const uint16_t *colours, uint32_t scales[16][3]);

// The picture (0x00BBGGRR) from the interface: the draw list, the textures and palette, the tilemaps and line scroll
// tables in sprite RAM (the game's own memory, 512 KB big-endian) and the text layer's map and characters (SS RAM, the
// game's memory: 32 KB, a byte per two of the board's addresses)
class Rasteriser {
public:
    void Draw(const Video &v, const uint8_t *spriteRam, const uint8_t *ssRam, std::vector<uint32_t> &pixels, int &width, int &height);
    // One picture of two producers: a guest's stage (its objects that `guestKeep` says are its stage) under this frame's
    // layers (`keep`'s choice), drawn as one frame (Frame::guest), the same pixels as the guest's picture drawn and this
    // one drawn over it (overlay). With no guest (null), Draw
    void DrawWithGuest(const Video *guest, const uint8_t *guestSpriteRam, KeepObject guestKeep, const void *guestContext, const Video &v,
                       const uint8_t *spriteRam, const uint8_t *ssRam, std::vector<uint32_t> &pixels, int &width, int &height);
    // Widescreen (as Renderer's): columns added on each side, the tilemaps drawn on into them, the objects and the text
    // layer moved over; and the world (objects, tilemaps) drawn `shift` columns further left than centred
    int margin = 0, shift = 0;
    // The objects drawn (KeepObject), and whether the text layer is
    KeepObject keep = nullptr;
    const void *keepContext = nullptr;
    bool text = true;
    // Drawn over the picture already in `pixels` (when it is of this frame's size), as Renderer's overlay: where this
    // frame drew nothing the picture shows; a blend of this frame's over nothing of its own makes the picture's colour
    // as much darker or lighter as that blend makes this producer's colours (the palette's bank against its first, on
    // the whole). So a guest stage's picture with the game's layers over it, as the board model composes it.
    bool overlay = false;
    // The objects drawn so many rows lower (the ring out's way down), and where nothing is drawn this colour (0x01BBGGRR:
    // the top byte says it is set; 0: the board's), as Renderer's
    int shiftY = 0;
    uint32_t backdrop = 0;
    // A frame made ready before (BuildFrame, AddGuest) drawn from the given interface (and its guest's, Frame::guestVideo)
    // and SS RAM, with this rasteriser's text and overlay settings
    void DrawFrame(const Frame &f, const Video &v, const uint8_t *ssRam, std::vector<uint32_t> &pixels, int &width, int &height);
    // The last frame drawn, made ready (for a GPU to draw the same)
    const Frame &LastFrame() const {
        return m_frame;
    }

private:
    static constexpr int kBufferWidth = 1024, kBufferHeight = 448;
    std::vector<uint32_t> m_buffer, m_guestBuffer;
    Frame m_frame;
    void Rasterise(const Frame &f, const uint8_t *textures, const uint8_t *guestTextures);
    void Resolve(const Frame &f, const uint16_t *colours, std::vector<uint32_t> &pixels, int width, int height, bool over);
    void Text(const Frame &f, const uint16_t *colours, const uint8_t *ss, std::vector<uint32_t> &pixels, int width, int height);
    std::array<bool, 16> m_blendKnown{};
    uint32_t m_blendScale[16][3] = {};
    uint32_t Blended(const uint16_t *colours, uint32_t bank, uint32_t pixel);
};

} // namespace cps3recomp::native
