// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "raster.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <tuple>

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
                    d.priority = o.priority;
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
                d.priority = o.priority;
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

void BuildFrame(const Video &v, const uint8_t *spriteRam, int margin, int shift, Frame &f, KeepObject keep, const void *keepContext, int shiftY, int front) {
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
    f.guest = false;
    f.guestVideo = nullptr;
    f.guestBufferWidth = f.guestBufferHeight = 0;
    f.front = front;
    f.bands = false;
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
        // (in front of the fighters: the game's things a guest's pieces in front go under, Frame::front)
        const int producer = InFront(d.priority, front) ? 3 : 0;
        if (d.kind == DrawItem::kQuad) {
            if (MakeTile(clip, d.tile, d.colour, d.granularity64 ? 64 : 256, d.flipX, d.flipY, d.x + margin - shift, d.y + shiftY, d.blend, d.scaleX, d.scaleY, t)) {
                t.producer = producer;
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
                t.producer = producer;
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
uint32_t Rasteriser::Blended(const uint16_t *colours, uint32_t bank, uint32_t pixel, int scales) {
    bank &= 0xF;
    std::array<bool, 16> &known = m_blendKnown[scales];
    uint32_t (&scale)[16][3] = m_blendScale[scales];
    if (!known[bank]) {
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
            scale[bank][c] = base[c] ? std::min<uint32_t>(static_cast<uint32_t>(made[c] * 256 / base[c]), 512) : 256;
        }
        known[bank] = true;
    }
    uint32_t out = 0;
    for (int c = 0; c < 3; c++) {
        const uint32_t v = std::min<uint32_t>((((pixel >> (8 * c)) & 0xFF) * scale[bank][c]) >> 8, 255);
        out |= v << (8 * c);
    }
    return out;
}

bool AddGuest(const Video &guest, const uint8_t *guestSpriteRam, Frame &f, KeepObject keep, const void *keepContext) {
    static thread_local Frame g;
    BuildFrame(guest, guestSpriteRam, f.margin, 0, g, keep, keepContext, 0, f.front);
    // (the game's things in front of the fighters back in its one buffer, unless the guest has pieces in front of them)
    const auto oneBuffer = [&f] {
        for (Tile &t : f.tiles) {
            if (t.producer == 3) {
                t.producer = 0;
            }
        }
    };
    if (g.width != f.width || g.height != f.height) {
        oneBuffer();
        return false;
    }
    // (its pieces in front of the game's fighters, if any of its objects drawn is one, on the picture or not: then the
    // game's things in front of those go in a buffer of their own, over them; else the game's are all in its one buffer.
    // Machine::Compose's two pictures ask the same of the objects)
    bool bands = false;
    for (uint32_t i = 0; i < guest.objects.size() && !bands && f.front; i++) {
        bands = (!keep || keep(keepContext, guest.objects[i], i)) && InFront(guest.objects[i].priority, f.front);
    }
    for (Tile t : g.tiles) {
        t.producer = t.producer == 3 ? 2 : 1;
        f.tiles.push_back(t);
    }
    if (!bands) {
        oneBuffer();
    }
    f.bands = bands;
    f.guest = true;
    f.guestScreen = g.screen;
    f.guestBufferWidth = g.bufferWidth;
    f.guestBufferHeight = g.bufferHeight;
    f.guestVideo = &guest;
    return true;
}

void BlendScales(const uint16_t *colours, uint32_t scales[16][3]) {
    for (uint32_t bank = 0; bank < 16; bank++) {
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
            scales[bank][c] = base[c] ? std::min<uint32_t>(static_cast<uint32_t>(made[c] * 256 / base[c]), 512) : 256;
        }
    }
}

// The frame's tiles into the buffers of colour numbers (the game's, and a guest's), in the board's order
void Rasteriser::Rasterise(const Frame &f, const uint8_t *ownTextures, const uint8_t *guestTextures) {
    // (the buffers: only their rows this frame uses cleared, all of them the first time)
    for (auto [buffer, rows, wanted] : {std::tuple<std::vector<uint32_t> *, int, bool>{&m_buffer, f.bufferHeight, true},
                                        {&m_guestBuffer, f.guestBufferHeight, f.guest},
                                        {&m_guestFrontBuffer, f.guestBufferHeight, f.guest && f.bands},
                                        {&m_frontBuffer, f.bufferHeight, f.guest && f.bands}}) {
        if (!wanted) {
            continue;
        }
        if (buffer->size() != static_cast<size_t>(kBufferWidth) * kBufferHeight) {
            buffer->assign(static_cast<size_t>(kBufferWidth) * kBufferHeight, 0);
        } else {
            std::fill_n(buffer->begin(), static_cast<size_t>(std::min(rows + 1, kBufferHeight)) * kBufferWidth, 0u);
        }
    }
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
    std::vector<uint32_t> *const buffers[4] = {&m_buffer, &m_guestBuffer, &m_guestFrontBuffer, &m_frontBuffer};
    for (const Tile &t : f.tiles) {
        const bool guests = t.producer == 1 || t.producer == 2;
        const uint8_t *textures = guests ? guestTextures : ownTextures;
        if (buffers[t.producer & 3]->empty()) {
            continue; // (a buffer this frame does not have: none of its tiles are made, AddGuest)
        }
        uint32_t *buffer = buffers[t.producer & 3]->data();
        int yIndex = t.yIndex;
        // (each row's texels: stepped by dx, or (a tile drawn at its size, as every tilemap's is) one by one)
        const bool unscaled = t.dx == 0x10000 || t.dx == -0x10000;
        const int step = t.dx < 0 ? -1 : 1;
        for (int y = t.sy; y < t.ey; y++, yIndex += t.dy) {
            const uint8_t *row = textures + t.source + (yIndex >> 16) * 16;
            uint32_t *dest = &buffer[static_cast<size_t>(y) * kBufferWidth];
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
}

// A buffer over the picture already in `pixels`, as Renderer's overlay: where it drew nothing the picture shows; a blend
// over nothing of its own makes the picture's colour as much darker or lighter as that blend makes its palette's colours
void Rasteriser::Over(const std::vector<uint32_t> &buffer, const Screen &screen, const uint16_t *colours, int scales, std::vector<uint32_t> &pixels,
                      int width, int height) {
    const uint32_t fx = (screen.zoomX << 16) / 0x40, fy = (screen.zoomY << 16) / 0x40;
    uint32_t sy = 0;
    for (int row = 0; row < height; row++, sy += fy) {
        const uint32_t *source = &buffer[static_cast<size_t>(std::min<uint32_t>(sy >> 16, kBufferHeight - 1)) * kBufferWidth];
        uint32_t *dest = &pixels[static_cast<size_t>(row) * width];
        uint32_t sx = 0;
        for (int col = 0; col < width; col++, sx += fx) {
            const uint32_t colour = source[std::min<uint32_t>(sx >> 16, kBufferWidth - 1)];
            if (colour & 0x1FFF) {
                dest[col] = Rgb(colours[colour & 0x1FFFF]);
            } else if (colour) {
                dest[col] = Blended(colours, colour >> 13, dest[col], scales);
            }
        }
    }
}

// The buffers to the screen through the palettes, each zoomed as its picture is: a guest's first (its own colours), then
// the game's over it (over: as over another picture already in `pixels`); with a guest's pieces in front of the game's
// fighters (Frame::bands), those over that, then the game's things in front of them
void Rasteriser::Resolve(const Frame &f, const uint16_t *colours, std::vector<uint32_t> &pixels, int width, int height, bool over) {
    const Screen &screen = f.screen;
    if (f.guest) {
        const uint16_t *guestColours = f.guestVideo->palette.Colours();
        const uint32_t gx = (f.guestScreen.zoomX << 16) / 0x40, gy = (f.guestScreen.zoomY << 16) / 0x40;
        uint32_t sy = 0;
        for (int row = 0; row < height; row++, sy += gy) {
            const uint32_t *source = &m_guestBuffer[static_cast<size_t>(std::min<uint32_t>(sy >> 16, kBufferHeight - 1)) * kBufferWidth];
            uint32_t *dest = &pixels[static_cast<size_t>(row) * width];
            uint32_t sx = 0;
            for (int col = 0; col < width; col++, sx += gx) {
                dest[col] = Rgb(guestColours[source[std::min<uint32_t>(sx >> 16, kBufferWidth - 1)] & 0x1FFFF]);
            }
        }
        over = true;
    }
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
    if (f.guest && f.bands) {
        Over(m_guestFrontBuffer, f.guestScreen, f.guestVideo->palette.Colours(), 1, pixels, width, height);
        Over(m_frontBuffer, screen, colours, 0, pixels, width, height);
    }
}

// The text layer: 64 x 32 characters of 8x8, 4 bits a pixel, over everything
void Rasteriser::Text(const Frame &f, const uint16_t *colours, const uint8_t *ss, std::vector<uint32_t> &pixels, int width, int height) {
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
                const int sx = copy * 512 + x * 8 - rowScroll + f.margin;
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

void Rasteriser::DrawFrame(const Frame &f, const Video &v, const uint8_t *ss, std::vector<uint32_t> &pixels, int &width, int &height) {
    m_blendKnown[0] = m_blendKnown[1] = {};
    width = f.width;
    height = f.height;
    const bool over = !f.guest && overlay && pixels.size() == static_cast<size_t>(width) * height;
    if (!over) {
        pixels.assign(static_cast<size_t>(width) * height, 0);
    }
    Rasterise(f, v.textures.Pixels(), f.guest ? f.guestVideo->textures.Pixels() : nullptr);
    Resolve(f, v.palette.Colours(), pixels, width, height, over);
    if (text) {
        Text(f, v.palette.Colours(), ss, pixels, width, height);
    }
}

void Rasteriser::Draw(const Video &v, const uint8_t *spriteRam, const uint8_t *ss, std::vector<uint32_t> &pixels, int &width, int &height) {
    DrawWithGuest(nullptr, nullptr, nullptr, nullptr, v, spriteRam, ss, pixels, width, height);
}

void Rasteriser::DrawWithGuest(const Video *guest, const uint8_t *guestSpriteRam, KeepObject guestKeep, const void *guestContext, const Video &v,
                               const uint8_t *spriteRam, const uint8_t *ss, std::vector<uint32_t> &pixels, int &width, int &height) {
    m_blendKnown[0] = m_blendKnown[1] = {};
    BuildFrame(v, spriteRam, margin, shift, m_frame, keep, keepContext, shiftY, guest ? front : 0);
    if (guest) {
        AddGuest(*guest, guestSpriteRam, m_frame, guestKeep, guestContext);
    }
    const Frame &f = m_frame;
    width = f.width;
    height = f.height;
    const bool over = !f.guest && overlay && pixels.size() == static_cast<size_t>(width) * height;
    if (!over) {
        pixels.assign(static_cast<size_t>(width) * height, 0);
    }
    Rasterise(f, v.textures.Pixels(), f.guest ? guest->textures.Pixels() : nullptr);
    Resolve(f, v.palette.Colours(), pixels, width, height, over);
    if (text) {
        Text(f, v.palette.Colours(), ss, pixels, width, height);
    }
}

} // namespace cps3recomp::native
