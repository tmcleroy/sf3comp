// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The CPS-3's picture, drawn from the board's memories as the video chip draws it: the object list (objects of
// 16x16 tiles, each zoomed, and lines of the four tilemaps, which the list places like objects) into a buffer of
// colour numbers, that buffer zoomed to the screen, then the text layer ("SS") over it. How each is drawn is MAME's
// CPS-3 driver's (BSD-3-Clause), written again here.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace cps3recomp::machine {
class Machine;
}

namespace cps3recomp {

class Renderer {
public:
    // Draws the frame into `pixels` (0x00BBGGRR, `width` x `height`, which the board's registers set: 384 x 224
    // or 496 x 224)
    void Draw(const machine::Machine &m, std::vector<uint32_t> &pixels, int &width, int &height);
    // Columns added on each side (widescreen): the tilemaps drawn on into them, the objects and the text layer moved
    // over by as much; 0 is the board's picture
    int margin = 0;
    // ... and the objects and tilemaps (the world) drawn that many columns further left than centred in them (the text
    // layer, the game's panel, stays centred): a wide picture kept on the stage near its ends while the game's camera
    // goes as far as the board's (-margin to margin)
    int shift = 0;
    // Draws only the objects whose main-list group this says yes to (none: all), and not the text layer when `text` is
    // off: for pictures made of more than one machine's
    bool (*drawGroup)(void *context, uint16_t group, uint32_t object) = nullptr;
    void *drawGroupContext = nullptr;
    bool text = true;
    // Over the pixels given (another machine's picture, the same size): only what the objects drew replaces them
    bool overlay = false;

private:
    std::vector<uint32_t> m_buffer; // colour numbers, up to twice the screen each way (the whole-picture zoom)
    static constexpr int kBufferWidth = 1024, kBufferHeight = 448;
    struct Clip {
        int left, right, top, bottom; // inclusive
    };
    void DrawTile(const machine::Machine &m, const Clip &clip, uint32_t code, uint32_t colour, int granularity, bool flipX, bool flipY,
                  int sx, int sy, bool blend, uint32_t scaleX, uint32_t scaleY);
    void DrawTilemapLine(const machine::Machine &m, const uint32_t *regs, int line, const Clip &clip);
    void DrawText(const machine::Machine &m, uint32_t *pixels, int width, int height);
    uint32_t Blended(const uint32_t *palette, uint32_t bank, uint32_t pixel);
    uint64_t m_drawCount = 0, m_blendFrame = ~0ull;
    std::array<bool, 16> m_blendKnown{};
    uint32_t m_blendScale[16][3] = {};
};

} // namespace cps3recomp
