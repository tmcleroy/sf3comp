// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "renderer.h"

#include "machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace cps3recomp {

using machine::Machine;

// A blend's bits (colour numbers' 13 and up pick a bank of the palette the game fills with shadow or highlight colours)
// applied to a colour of another board's: each component scaled as the bank's colours are, on the whole, against the
// first bank's (worked out once a picture, for the banks used)
uint32_t Renderer::Blended(const uint32_t *palette, uint32_t bank, uint32_t pixel) {
    bank &= 0xF;
    if (m_blendFrame != m_drawCount || !m_blendKnown[bank]) {
        if (m_blendFrame != m_drawCount) {
            m_blendFrame = m_drawCount;
            m_blendKnown = {};
        }
        uint64_t base[3] = {}, made[3] = {};
        for (uint32_t i = 1; i < 0x2000; i++) {
            const uint32_t a = palette[i], b = palette[(bank << 13) | i];
            if (a == 0) {
                continue;
            }
            for (int c = 0; c < 3; c++) {
                base[c] += (a >> (8 * c)) & 0xFF;
                made[c] += (b >> (8 * c)) & 0xFF;
            }
        }
        for (int c = 0; c < 3; c++) {
            m_blendScale[bank][c] = base[c] ? std::min<uint32_t>(static_cast<uint32_t>(made[c] * 256 / base[c]), 512) : 256;
        }
        m_blendKnown[bank] = true;
        if (getenv("CPS3RECOMP_BLEND_LOG")) {
            fprintf(stderr, "blend bank %u: %u %u %u\n", bank, m_blendScale[bank][0], m_blendScale[bank][1], m_blendScale[bank][2]);
        }
    }
    uint32_t out = 0;
    for (int c = 0; c < 3; c++) {
        const uint32_t v = std::min<uint32_t>((((pixel >> (8 * c)) & 0xFF) * m_blendScale[bank][c]) >> 8, 255);
        out |= v << (8 * c);
    }
    return out;
}

// One 16x16 tile of character RAM (a byte a pixel), zoomed by scaleX / scaleY (16.16), into the buffer as colour
// numbers: pixel | colour * granularity. A pixel of 0 is not drawn. A "blend" object instead sets bits of what is
// under it, which pick shadow or highlight colours.
void Renderer::DrawTile(const Machine &m, const Clip &clip, uint32_t code, uint32_t colour, int granularity, bool flipX, bool flipY,
                        int sx, int sy, bool blend, uint32_t scaleX, uint32_t scaleY) {
    if (!scaleX || !scaleY) {
        return;
    }
    const uint32_t paletteBase = (static_cast<uint32_t>(granularity) * colour) & 0x1FFFF;
    const uint8_t *source = m.CharRam() + static_cast<size_t>(code & 0x7FFF) * 256;
    const int height = static_cast<int>((scaleY * 16 + 0x8000) >> 16), width = static_cast<int>((scaleX * 16 + 0x8000) >> 16);
    if (!width || !height) {
        return;
    }
    int dx = (16 << 16) / width, dy = (16 << 16) / height;
    int ex = sx + width, ey = sy + height;
    int xBase = 0, yIndex = 0;
    if (flipX) {
        xBase = (width - 1) * dx;
        dx = -dx;
    }
    if (flipY) {
        yIndex = (height - 1) * dy;
        dy = -dy;
    }
    if (sx < clip.left) {
        xBase += (clip.left - sx) * dx;
        sx = clip.left;
    }
    if (sy < clip.top) {
        yIndex += (clip.top - sy) * dy;
        sy = clip.top;
    }
    ex = std::min(ex, clip.right + 1);
    ey = std::min(ey, clip.bottom + 1);
    if (ex <= sx) {
        return;
    }
    for (int y = sy; y < ey; y++, yIndex += dy) {
        const uint8_t *row = source + (yIndex >> 16) * 16;
        uint32_t *dest = &m_buffer[static_cast<size_t>(y) * kBufferWidth];
        int xIndex = xBase;
        if (!blend) {
            for (int x = sx; x < ex; x++, xIndex += dx) {
                const uint8_t c = row[xIndex >> 16];
                if (c) {
                    dest[x] = c | paletteBase;
                }
            }
        } else if (granularity == 64) {
            for (int x = sx; x < ex; x++, xIndex += dx) {
                const uint8_t c = row[xIndex >> 16];
                if (c) {
                    dest[x] |= static_cast<uint32_t>(c & 0xF) << 13;
                }
            }
        } else {
            for (int x = sx; x < ex; x++, xIndex += dx) {
                const uint8_t c = row[xIndex >> 16];
                if (c) {
                    dest[x] |= (static_cast<uint32_t>(c & 1) << 15) | ((colour & 1) << 16);
                }
            }
        }
    }
}

// One line of a tilemap (64 x 64 tiles of 16x16 in sprite RAM), at the buffer's line `line`
void Renderer::DrawTilemapLine(const Machine &m, const uint32_t *regs, int line, const Clip &clip) {
    if (!(regs[1] & 0x00008000u)) {
        return;
    }
    int scrollX = static_cast<int>((regs[0] >> 16) & 0xFFFF);
    int scrollY = static_cast<int>(regs[0] & 0xFFFF);
    const bool lineScroll = (regs[1] >> 14) & 1;
    const bool flipAllX = (regs[1] >> 11) & 1, flipAllY = (regs[1] >> 10) & 1;
    const uint32_t lineBase = ((regs[2] >> 24) & 0x7F) << 10;
    const uint32_t mapBase = ((regs[2] >> 16) & 0x7F) << 10;
    scrollY += 4;
    int mapLine = (line + scrollY) & 0x3FF;
    if (flipAllY) {
        mapLine ^= 0x3FF;
    }
    const int xFlipMask = flipAllX ? 0x3F : 0;
    const int tileLine = mapLine / 16 + 1, subLine = mapLine % 16;
    if (lineScroll) {
        scrollX += static_cast<int>((m.SpriteRam(lineBase | ((mapLine + 16) & 0x3FF)) >> 16) & 0x3FF);
    }
    scrollX -= margin; // (the columns added at the left show the map further left)
    const Clip row = {clip.left, clip.right, line, line};
    for (int x = clip.left / 16; x < clip.right / 16 + 2; x++) {
        const uint32_t tile = m.SpriteRam(mapBase + ((tileLine & 63) << 6) + (((x + (scrollX >> 4)) & 63) ^ xFlipMask)); // (>> 4: rounding down)
        const uint32_t code = tile >> 17;
        const bool flipX = (((tile >> 12) & 1) ^ (xFlipMask & 1)) != 0;
        const bool flipY = (tile >> 11) & 1;
        const bool blend = (tile >> 10) & 1;
        const int granularity = ((tile >> 9) & 1) ? 64 : 256;
        DrawTile(m, row, code, tile & 0x1FF, granularity, flipX, flipY, x * 16 - (scrollX & 15), line - subLine, blend, 0x10000, 0x10000);
    }
}

// The text layer: 64 x 32 characters of 8x8, 4 bits a pixel, from SS RAM, over everything
void Renderer::DrawText(const Machine &m, uint32_t *pixels, int width, int height) {
    const uint8_t *ss = m.SsRam();
    const uint32_t *palette = m.Palette();
    const int scrollY = (-static_cast<int>(m.SsVerticalScroll())) & 0x100;
    for (int line = 0; line < height; line++) {
        const int y = line / 8, sub = line - y * 8;
        int offset = ((line + scrollY) / 8 * 128) & 0x1FFF;
        const int rowScroll = ss[((line + scrollY - 1) & 0x1FF) * 2 + 0x2000];
        uint32_t *dest = pixels + static_cast<size_t>(line) * width;
        for (int x = 0; x < 64; x++, offset += 2) {
            const uint16_t data = static_cast<uint16_t>(ss[offset] | (ss[offset + 1] << 8));
            const uint32_t tile = data & 0x1FF;
            const uint32_t colour = ((data >> 9) & 0x1F) + (static_cast<uint32_t>(m.SsPaletteBase()) << 5);
            const bool flipY = (data >> 14) & 1, flipX = (data >> 15) & 1;
            const uint8_t *chr = ss + 0x4000 + tile * 32 + (flipY ? 7 - sub : sub) * 4;
            const uint32_t base = (colour * 16) & 0x1FFFF;
            for (int copy = 0; copy < 2; copy++) {
                const int sx = copy * 512 + x * 8 - rowScroll + margin;
                for (int px = 0; px < 8; px++) {
                    const int dx = sx + px;
                    if (dx < 0 || dx >= width) {
                        continue;
                    }
                    const int p = flipX ? 7 - px : px;
                    // (pixel 0 is the low half of the first byte, pixel 1 its high half, and so on)
                    const uint8_t c = (chr[p >> 1] >> ((p & 1) * 4)) & 0xF;
                    if (c) {
                        dest[dx] = palette[(base + c) & 0x1FFFF];
                    }
                }
            }
        }
    }
}

void Renderer::Draw(const Machine &m, std::vector<uint32_t> &pixels, int &width, int &height) {
    m_drawCount++;
    const machine::VideoRegisters &regs = m.Video();
    width = static_cast<int>((regs.crtc[1] >> 16) & 0xFFFF) - static_cast<int>(regs.crtc[0] & 0xFFFF);
    height = static_cast<int>((regs.crtc[5] >> 16) & 0xFFFF) - static_cast<int>(regs.crtc[4] & 0xFFFF);
    if (width <= 0 || width > 512) {
        width = 384;
    }
    if (height <= 0 || height > 256) {
        height = 224;
    }
    width += 2 * margin;
    if (!overlay || pixels.size() != static_cast<size_t>(width) * height) {
        pixels.assign(static_cast<size_t>(width) * height, 0);
    }
    m_buffer.assign(static_cast<size_t>(kBufferWidth) * kBufferHeight, 0);

    // The whole picture's zoom (0x40 is none; up to 0x80, twice as much shown)
    const uint32_t zoomX = std::min<uint32_t>(regs.crtc[3] & 0xFF, 0x80), zoomY = std::min<uint32_t>(regs.crtc[7] & 0xFF, 0x80);
    const uint32_t fx = (zoomX << 16) / 0x40, fy = (zoomY << 16) / 0x40;
    Clip clip = {0, width - 1, 0, height - 1};
    if (zoomX != 0x40 || zoomY != 0x40) {
        clip = {0, static_cast<int>(((static_cast<uint32_t>(width) * fx + 0x8000) >> 16)) - 1, 0,
                static_cast<int>(((static_cast<uint32_t>(height) * fy + 0x8000) >> 16)) - 1};
        clip.right = std::min(clip.right, kBufferWidth - 1);
        clip.bottom = std::min(clip.bottom, kBufferHeight - 1);
    }

    // The objects, in the list's order (later ones over earlier)
    const uint32_t *list = m.SpriteList();
    static constexpr int kTiles[4] = {8, 1, 2, 4};
    for (uint32_t entry = 0; entry < std::min<uint32_t>(m.SpriteCount(), 0x2000 / 16); entry++) {
        const uint32_t v1 = list[entry * 4], v2 = list[entry * 4 + 1], v3 = list[entry * 4 + 2];
        if (drawGroup && !drawGroup(drawGroupContext, m.SpriteGroups()[entry], entry)) {
            continue;
        }
        const uint32_t code = v1 >> 17;
        const bool flipX = (v1 >> 12) & 1, flipY = (v1 >> 11) & 1, blend = (v1 >> 10) & 1;
        const int granularity = ((v1 >> 9) & 1) ? 64 : 256;
        const uint32_t colour = v1 & 0x1FF;
        int x = static_cast<int>((v2 >> 16) & 0x3FF), y = static_cast<int>(v2 & 0x3FF);
        const uint32_t drawHeight = ((v3 >> 24) & 0x7F) + 1, drawWidth = ((v3 >> 16) & 0x7F) + 1;
        const int tilesY = (v3 >> 2) & 3, tilesX = v3 & 3;
        if (tilesY == 0) {
            continue;
        }
        if (tilesX == 0) {
            // (no width: lines of a tilemap, placed like an object's)
            const uint32_t *tilemap = &regs.tilemap[((v3 >> 4) & 3) * 4];
            for (uint32_t yy = 0; yy < drawHeight; yy++) {
                const int line = (~(y - static_cast<int>(yy)) - 18) & 0x3FF;
                if (line >= clip.top && line <= clip.bottom) {
                    DrawTilemapLine(m, tilemap, line, clip);
                }
            }
            continue;
        }
        const int countY = kTiles[tilesY], countX = kTiles[tilesX];
        const uint32_t xinc = (drawWidth << 16) / countX, yinc = (drawHeight << 16) / countY;
        uint32_t scaleX = xinc / 16, scaleY = yinc / 16;
        // (rounded up, so that zoomed tiles leave no gaps between them)
        if (scaleX & 0xFFFF) {
            scaleX += (1 << 16) / 16;
        }
        if (scaleY & 0xFFFF) {
            scaleY += (1 << 16) / 16;
        }
        x += flipX ? -static_cast<int>(drawWidth / 2) : static_cast<int>(drawWidth / 2);
        y += static_cast<int>(drawHeight / 2);
        if (!flipX) {
            x -= static_cast<int>((countX * xinc) >> 16);
        } else {
            x += static_cast<int>(((countX - 1) * xinc) >> 16);
        }
        if (flipY) {
            y -= static_cast<int>(((countY - 1) * yinc) >> 16);
        }
        uint32_t count = 0;
        for (int xx = 0; xx < countX; xx++) {
            int cx = flipX ? x - static_cast<int>((xx * xinc) >> 16) : x + static_cast<int>((xx * xinc) >> 16);
            cx &= 0x3FF;
            if (cx & 0x200) {
                cx -= 0x400;
            }
            cx += margin;
            for (int yy = 0; yy < countY; yy++) {
                int cy = flipY ? y + static_cast<int>((yy * yinc) >> 16) : y - static_cast<int>((yy * yinc) >> 16);
                cy = ((0x3FF - cy) - 17) & 0x3FF;
                if (cy & 0x200) {
                    cy -= 0x400;
                }
                DrawTile(m, clip, code + count, colour, granularity, flipX, flipY, cx, cy, blend, scaleX, scaleY);
                count++;
            }
        }
    }

    // The buffer to the screen, zoomed if the picture is
    const uint32_t *palette = m.Palette();
    uint32_t sy = 0;
    for (int row = 0; row < height; row++, sy += fy) {
        const uint32_t *source = &m_buffer[static_cast<size_t>(std::min<uint32_t>(sy >> 16, kBufferHeight - 1)) * kBufferWidth];
        uint32_t *dest = &pixels[static_cast<size_t>(row) * width];
        uint32_t sx = 0;
        for (int col = 0; col < width; col++, sx += fx) {
            const uint32_t colour = source[std::min<uint32_t>(sx >> 16, kBufferWidth - 1)];
            if (!overlay || (colour & 0x1FFF)) { // (overlaid: where nothing was drawn, what the pixels had shows)
                dest[col] = palette[colour & 0x1FFFF];
            } else if (colour) {
                // (overlaid, a blend object over nothing of this board's: what the pixels had, made as much darker or
                // lighter as that blend makes this board's colours)
                dest[col] = Blended(palette, colour >> 13, dest[col]);
            }
        }
    }
    if (text) {
        DrawText(m, pixels.data(), width, height);
    }
}

} // namespace cps3recomp
