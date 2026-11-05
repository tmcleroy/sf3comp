// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "raster.h"

#include <algorithm>

namespace cps3recomp::native {

namespace {
    uint32_t Long(const uint8_t *ram, uint32_t word) {
        const uint8_t *p = ram + ((word * 4) & 0x7FFFC);
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }
    uint32_t Rgb(uint16_t c) {
        // (the video DAC's low three bits of each component are grounded)
        return (static_cast<uint32_t>(c & 0x1F) << 3) | (static_cast<uint32_t>((c >> 5) & 0x1F) << 11) | (static_cast<uint32_t>((c >> 10) & 0x1F) << 19);
    }
    struct Clip {
        int left, right, top, bottom; // inclusive
    };
    constexpr int kBufferRight = 1023, kBufferBottom = 447; // (the buffer: up to twice the screen each way, for the zoom)
} // namespace

Screen ScreenOf(const FrameRegisters &r) {
    Screen s;
    s.width = static_cast<int>(r.screen[2]) - static_cast<int>(r.screen[1]);
    s.height = static_cast<int>(r.screen[10]) - static_cast<int>(r.screen[9]);
    if (s.width <= 0 || s.width > 512) {
        s.width = 384;
    }
    if (s.height <= 0 || s.height > 256) {
        s.height = 224;
    }
    s.zoomX = std::min<uint32_t>(r.screen[7] & 0xFF, 0x80);
    s.zoomY = std::min<uint32_t>(r.screen[15] & 0xFF, 0x80);
    s.clipRight = s.width - 1;
    s.clipBottom = s.height - 1;
    if (s.zoomX != 0x40 || s.zoomY != 0x40) {
        const uint32_t fx = (s.zoomX << 16) / 0x40, fy = (s.zoomY << 16) / 0x40;
        s.clipRight = std::min(static_cast<int>((static_cast<uint32_t>(s.width) * fx + 0x8000) >> 16) - 1, kBufferRight);
        s.clipBottom = std::min(static_cast<int>((static_cast<uint32_t>(s.height) * fy + 0x8000) >> 16) - 1, kBufferBottom);
    }
    return s;
}

// Each object: a tilemap's lines (an object with no width) within the buffer's lines drawn, or its tiles as quads, each
// zoomed to the object's drawn size over its count of tiles (rounded up, so that zoomed tiles leave no gaps)
void BuildDrawList(const std::vector<Object> &objects, const FrameRegisters &r, const Screen &screen, std::vector<DrawItem> &out) {
    out.clear();
    static constexpr int kTiles[4] = {8, 1, 2, 4};
    for (const Object &o : objects) {
        const uint32_t v1 = o.words[0], v2 = o.words[1], v3 = o.words[2];
        const uint32_t code = v1 >> 17;
        const bool flipX = (v1 >> 12) & 1, flipY = (v1 >> 11) & 1, blend = (v1 >> 10) & 1, granularity64 = (v1 >> 9) & 1;
        const uint32_t colour = v1 & 0x1FF;
        int x = static_cast<int>((v2 >> 16) & 0x3FF), y = static_cast<int>(v2 & 0x3FF);
        const uint32_t drawHeight = ((v3 >> 24) & 0x7F) + 1, drawWidth = ((v3 >> 16) & 0x7F) + 1;
        const int tilesY = (v3 >> 2) & 3, tilesX = v3 & 3;
        if (tilesY == 0) {
            continue;
        }
        if (tilesX == 0) {
            for (uint32_t yy = 0; yy < drawHeight; yy++) {
                const int line = (~(y - static_cast<int>(yy)) - 18) & 0x3FF;
                if (line >= 0 && line <= screen.clipBottom) {
                    DrawItem d;
                    d.kind = DrawItem::kLine;
                    d.tilemap = static_cast<uint8_t>((v3 >> 4) & 3);
                    d.line = static_cast<int16_t>(line);
                    d.group = o.group;
                    out.push_back(d);
                }
            }
            continue;
        }
        const int countY = kTiles[tilesY], countX = kTiles[tilesX];
        const uint32_t xinc = (drawWidth << 16) / countX, yinc = (drawHeight << 16) / countY;
        uint32_t scaleX = xinc / 16, scaleY = yinc / 16;
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
            for (int yy = 0; yy < countY; yy++) {
                int cy = flipY ? y + static_cast<int>((yy * yinc) >> 16) : y - static_cast<int>((yy * yinc) >> 16);
                cy = ((0x3FF - cy) - 17) & 0x3FF;
                if (cy & 0x200) {
                    cy -= 0x400;
                }
                DrawItem d;
                d.kind = DrawItem::kQuad;
                d.tile = code + count;
                d.colour = colour;
                d.scaleX = scaleX;
                d.scaleY = scaleY;
                d.x = static_cast<int16_t>(cx);
                d.y = static_cast<int16_t>(cy);
                d.granularity64 = granularity64;
                d.flipX = flipX;
                d.flipY = flipY;
                d.blend = blend;
                d.group = o.group;
                out.push_back(d);
                count++;
            }
        }
    }
}

// A 16x16 tile zoomed (scale 16.16) to its drawn size at sx, sy, clipped, as the steps through its pixels
static bool MakeTile(const Clip &clip, uint32_t code, uint32_t colour, int granularity, bool flipX, bool flipY, int sx, int sy, bool blend, uint32_t scaleX,
                     uint32_t scaleY, Tile &t) {
    if (!scaleX || !scaleY) {
        return false;
    }
    const int height = static_cast<int>((scaleY * 16 + 0x8000) >> 16), width = static_cast<int>((scaleX * 16 + 0x8000) >> 16);
    if (!width || !height) {
        return false;
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
    if (ex <= sx || ey <= sy) {
        return false;
    }
    t.sx = sx;
    t.sy = sy;
    t.ex = ex;
    t.ey = ey;
    t.xBase = xBase;
    t.yIndex = yIndex;
    t.dx = dx;
    t.dy = dy;
    t.source = static_cast<int32_t>((code & 0x7FFF) * 256);
    if (!blend) {
        t.mode = 0;
        t.base = static_cast<int32_t>((static_cast<uint32_t>(granularity) * colour) & 0x1FFFF);
    } else if (granularity == 64) {
        t.mode = 1;
        t.base = 0;
    } else {
        t.mode = 2;
        t.base = static_cast<int32_t>((colour & 1) << 16);
    }
    return true;
}

void BuildFrame(const Video &v, const uint8_t *spriteRam, int margin, int shift, Frame &f, KeepObject keep, const void *keepContext, int shiftY) {
    const FrameRegisters &r = v.registers;
    f.screen = ScreenOf(r);
    f.width = f.screen.width + 2 * margin;
    f.height = f.screen.height;
    Clip clip = {0, f.width - 1, 0, f.screen.clipBottom};
    if (f.screen.zoomX != 0x40 || f.screen.zoomY != 0x40) {
        clip.right = std::min(static_cast<int>((static_cast<uint32_t>(f.width) * ((f.screen.zoomX << 16) / 0x40) + 0x8000) >> 16) - 1, kBufferRight);
    }
    f.bufferWidth = std::min(std::max(f.width, clip.right + 1), kBufferRight + 1);
    f.bufferHeight = std::min(std::max(f.height, clip.bottom + 1), kBufferBottom + 1);
    f.tiles.clear();
    if (keep) {
        static thread_local std::vector<Object> kept;
        kept.clear();
        for (uint32_t i = 0; i < v.objects.size(); i++) {
            const Object &o = v.objects[i];
            if (keep(keepContext, o, i)) {
                kept.push_back(o);
            }
        }
        BuildDrawList(kept, r, f.screen, f.items);
    } else {
        BuildDrawList(v.objects, r, f.screen, f.items);
    }
    Tile t;
    for (const DrawItem &d : f.items) {
        if (d.kind == DrawItem::kQuad) {
            if (MakeTile(clip, d.tile, d.colour, d.granularity64 ? 64 : 256, d.flipX, d.flipY, d.x + margin - shift, d.y + shiftY, d.blend, d.scaleX, d.scaleY, t)) {
                f.tiles.push_back(t);
            }
            continue;
        }
        // A tilemap's line (64 x 64 tiles of 16x16 in sprite RAM): its scroll, line scroll, flips and bases
        const uint16_t *tm = r.tilemap[d.tilemap];
        const uint32_t regs[3] = {(static_cast<uint32_t>(tm[0]) << 16) | tm[1], (static_cast<uint32_t>(tm[2]) << 16) | tm[3],
                                  (static_cast<uint32_t>(tm[4]) << 16) | tm[5]};
        if (!(regs[1] & 0x00008000u)) {
            continue;
        }
        const int line = d.line;
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
            scrollX += static_cast<int>((Long(spriteRam, lineBase | ((mapLine + 16) & 0x3FF)) >> 16) & 0x3FF);
        }
        scrollX -= margin - shift; // (the columns added at the left show the map further left)
        const Clip row = {clip.left, clip.right, line, line};
        for (int x = clip.left / 16; x < clip.right / 16 + 2; x++) {
            const uint32_t tile = Long(spriteRam, mapBase + ((tileLine & 63) << 6) + (((x + (scrollX >> 4)) & 63) ^ xFlipMask));
            const bool flipX = (((tile >> 12) & 1) ^ (xFlipMask & 1)) != 0;
            if (MakeTile(row, tile >> 17, tile & 0x1FF, ((tile >> 9) & 1) ? 64 : 256, flipX, (tile >> 11) & 1, x * 16 - (scrollX & 15), line - subLine,
                         (tile >> 10) & 1, 0x10000, 0x10000, t)) {
                f.tiles.push_back(t);
            }
        }
    }
    // The text layer's settings
    f.textScrollY = (-static_cast<int>((r.text[0x11] << 8) | r.text[0x10])) & 0x100;
    f.textPalette = r.text[0x12];
    f.margin = margin;
}

// A blend's bits (colour numbers' 13 and up pick a bank of the palette the game fills with shadow or highlight colours)
// applied to a colour of another producer's: each component scaled as the bank's colours are, on the whole, against the
// first bank's (worked out once a frame, for the banks used), as Renderer::Blended
uint32_t Rasteriser::Blended(const uint16_t *colours, uint32_t bank, uint32_t pixel) {
    bank &= 0xF;
    if (!m_blendKnown[bank]) {
        uint64_t base[3] = {}, made[3] = {};
        for (uint32_t i = 1; i < 0x2000; i++) {
            const uint32_t a = Rgb(colours[i]), b = Rgb(colours[(bank << 13) | i]);
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
    }
    uint32_t out = 0;
    for (int c = 0; c < 3; c++) {
        const uint32_t v = std::min<uint32_t>((((pixel >> (8 * c)) & 0xFF) * m_blendScale[bank][c]) >> 8, 255);
        out |= v << (8 * c);
    }
    return out;
}

void Rasteriser::Draw(const Video &v, const uint8_t *spriteRam, const uint8_t *ss, std::vector<uint32_t> &pixels, int &width, int &height) {
    m_blendKnown = {};
    BuildFrame(v, spriteRam, margin, shift, m_frame, keep, keepContext, shiftY);
    const Frame &f = m_frame;
    const Screen &screen = f.screen;
    width = f.width;
    height = f.height;
    const bool over = overlay && pixels.size() == static_cast<size_t>(width) * height;
    if (!over) {
        pixels.assign(static_cast<size_t>(width) * height, 0);
    }
    // (the buffer: only its rows this frame uses cleared, all of them the first time)
    if (m_buffer.size() != static_cast<size_t>(kBufferWidth) * kBufferHeight) {
        m_buffer.assign(static_cast<size_t>(kBufferWidth) * kBufferHeight, 0);
    } else {
        std::fill_n(m_buffer.begin(), static_cast<size_t>(std::min(f.bufferHeight + 1, kBufferHeight)) * kBufferWidth, 0u);
    }
    const uint8_t *textures = v.textures.Pixels();
    { // (CPS3RECOMP_NATIVE_TILE_STATS=1: the most tile rows a frame has, which the GPU's first pass loops over per pixel)
        static const bool stats = getenv("CPS3RECOMP_NATIVE_TILE_STATS") != nullptr;
        static uint64_t most = 0;
        if (stats) {
            uint64_t rows = 0;
            for (const Tile &t : f.tiles) {
                rows += static_cast<uint64_t>(std::max(t.ey - t.sy, 0));
            }
            if (rows > most) {
                most = rows;
                fprintf(stderr, "native tiles: %zu tiles, %llu tile rows\n", f.tiles.size(), static_cast<unsigned long long>(rows));
            }
        }
    }
    for (const Tile &t : f.tiles) {
        int yIndex = t.yIndex;
        // (each row's texels: stepped by dx, or (a tile drawn at its size, as every tilemap's is) one by one)
        const bool unscaled = t.dx == 0x10000 || t.dx == -0x10000;
        const int step = t.dx < 0 ? -1 : 1;
        for (int y = t.sy; y < t.ey; y++, yIndex += t.dy) {
            const uint8_t *row = textures + t.source + (yIndex >> 16) * 16;
            uint32_t *dest = &m_buffer[static_cast<size_t>(y) * kBufferWidth];
            const uint32_t base = static_cast<uint32_t>(t.base);
            if (unscaled) {
                const uint8_t *texel = row + (t.xBase >> 16);
                switch (t.mode) {
                case 0:
                    for (int x = t.sx; x < t.ex; x++, texel += step) {
                        if (const uint8_t c = *texel) {
                            dest[x] = c | base;
                        }
                    }
                    break;
                case 1:
                    for (int x = t.sx; x < t.ex; x++, texel += step) {
                        if (const uint8_t c = *texel) {
                            dest[x] |= static_cast<uint32_t>(c & 0xF) << 13;
                        }
                    }
                    break;
                default:
                    for (int x = t.sx; x < t.ex; x++, texel += step) {
                        if (const uint8_t c = *texel) {
                            dest[x] |= (static_cast<uint32_t>(c & 1) << 15) | base;
                        }
                    }
                    break;
                }
                continue;
            }
            int xIndex = t.xBase;
            switch (t.mode) {
            case 0:
                for (int x = t.sx; x < t.ex; x++, xIndex += t.dx) {
                    if (const uint8_t c = row[xIndex >> 16]) {
                        dest[x] = c | base;
                    }
                }
                break;
            case 1:
                for (int x = t.sx; x < t.ex; x++, xIndex += t.dx) {
                    if (const uint8_t c = row[xIndex >> 16]) {
                        dest[x] |= static_cast<uint32_t>(c & 0xF) << 13;
                    }
                }
                break;
            default:
                for (int x = t.sx; x < t.ex; x++, xIndex += t.dx) {
                    if (const uint8_t c = row[xIndex >> 16]) {
                        dest[x] |= (static_cast<uint32_t>(c & 1) << 15) | base;
                    }
                }
                break;
            }
        }
    }

    // The buffer to the screen through the palette, zoomed if the picture is
    const uint16_t *colours = v.palette.Colours();
    const uint32_t fx = (screen.zoomX << 16) / 0x40, fy = (screen.zoomY << 16) / 0x40;
    uint32_t sy = 0;
    for (int row = 0; row < height; row++, sy += fy) {
        const uint32_t *source = &m_buffer[static_cast<size_t>(std::min<uint32_t>(sy >> 16, kBufferHeight - 1)) * kBufferWidth];
        uint32_t *dest = &pixels[static_cast<size_t>(row) * width];
        if (fx == 0x10000 && !over && !backdrop) {
            for (int col = 0; col < width; col++) {
                dest[col] = Rgb(colours[source[col] & 0x1FFFF]);
            }
            continue;
        }
        uint32_t sx = 0;
        for (int col = 0; col < width; col++, sx += fx) {
            const uint32_t colour = source[std::min<uint32_t>(sx >> 16, kBufferWidth - 1)];
            if (!over || (colour & 0x1FFF)) {
                dest[col] = colour == 0 && backdrop ? backdrop & 0xFFFFFF : Rgb(colours[colour & 0x1FFFF]);
            } else if (colour) {
                dest[col] = Blended(colours, colour >> 13, dest[col]);
            }
        }
    }

    // The text layer: 64 x 32 characters of 8x8, 4 bits a pixel, over everything
    if (!text) {
        return;
    }
    const int scrollY = f.textScrollY;
    const uint32_t paletteBase = f.textPalette;
    for (int line = 0; line < height; line++) {
        const int sub = line - line / 8 * 8;
        int offset = ((line + scrollY) / 8 * 128) & 0x1FFF;
        const int rowScroll = ss[((line + scrollY - 1) & 0x1FF) * 2 + 0x2000];
        uint32_t *dest = pixels.data() + static_cast<size_t>(line) * width;
        for (int x = 0; x < 64; x++, offset += 2) {
            const uint16_t data = static_cast<uint16_t>(ss[offset] | (ss[offset + 1] << 8));
            const uint32_t tile = data & 0x1FF;
            const uint32_t colour = ((data >> 9) & 0x1F) + (paletteBase << 5);
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
                    const uint8_t c = (chr[p >> 1] >> ((p & 1) * 4)) & 0xF;
                    if (c) {
                        dest[dx] = Rgb(colours[(base + c) & 0x1FFFF]);
                    }
                }
            }
        }
    }
}

} // namespace cps3recomp::native
