// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The native video interface (docs/NATIVE.md): what the game's rewritten drawing code hands over, in place of what it
// writes to the video chip's registers and memories. The board model (machine/, renderer.cpp) stays as the reference:
// with CPS3RECOMP_NATIVE_CHECK=1 the board keeps this beside its own memories, and every frame what the board's
// memories say is compared with what the rewrites handed over (Machine::nativeCheck).
//
// The first piece is the palette: the colours that the palette DMA brings in from the graphics SIMMs, faded as they go,
// and the colours the CPU writes itself (the text layer's).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cps3recomp {
class Archive;
}

namespace cps3recomp::native {

// Whether the native interface is kept this run (fixed at start): in the checks (CPS3RECOMP_NATIVE_CHECK), and wherever
// the GPU draws the picture from it (Keep, before the first machine is made; CPS3RECOMP_NATIVE=1 or 0 says so whatever
// the port chose). When not, handing it things does nothing
bool On();
// Whether this is a check run (CPS3RECOMP_NATIVE_CHECK): only then is the interface in save states. Elsewhere it is made
// again from the board's memories after a state is put back (Machine::SyncNative): until the board model leaves the
// default build, the two hold the same, as the checks show every frame, and save states stay as they were
bool Checking();
// The port's choice (before the first machine is made: native::On is fixed from then on)
void Keep(bool on);

// A palette transfer: `length` colours from the graphics, from word `source` of the graphics SIMMs' stream (the samples'
// and the characters' stream: Machine::Samples), to colours `dest` on, faded by `fade`: three fade bytes, red in bits
// 24-30, green 16-22, blue 0-6, each with bit 6 on to fade, bit 5 to fade towards white instead of black and bits 0-4 the
// amount (32ths). As the board's 32-bit bus has it, the two colours of each pair are swapped on both sides (an even
// start and length, as the game's always are, is a plain copy).
struct PaletteTransfer {
    uint32_t source = 0, dest = 0, length = 0, fade = 0;
    bool operator==(const PaletteTransfer &) const = default;
};

// For the check: what happened to the palette, in order (a transfer, or a colour written by the game's code)
struct PaletteEvent {
    enum Kind : uint8_t { kTransfer, kColour } kind;
    PaletteTransfer transfer; // (kColour: dest the colour's number, source its value)
    bool operator==(const PaletteEvent &) const = default;
};

// The game's sources' say over the colours a transfer brings in (custom colours): given the graphics word a colour is
// read from and the colour it goes to, the colour to use
using ColourHook = uint16_t (*)(uint32_t word, uint32_t index, uint16_t colour);

// 0x20000 colours of 15 bits (red in bits 0-4, green 5-9, blue 10-14) and a 16th the blend objects read (bit 15)
class Palette {
public:
    static constexpr uint32_t kColours = 0x20000;
    Palette();
    // A transfer made: the colours read from `graphics` (big-endian words, `size` bytes) and faded
    void Transfer(const PaletteTransfer &t, const uint8_t *graphics, size_t size, ColourHook hook);
    // A colour set as it is (the game's code writing one)
    void Set(uint32_t index, uint16_t colour) {
        m_colours[index & (kColours - 1)] = colour;
        m_version++;
    }
    const uint16_t *Colours() const {
        return m_colours.data();
    }
    // (made again from the board's colour RAM: all of it)
    void SetAll(const uint16_t *colours);
    // Changes whenever a colour may have changed (a GPU's copy sends the colours again only then)
    uint64_t Version() const {
        return m_version;
    }
    void Serialize(Archive &ar);

private:
    std::vector<uint16_t> m_colours;
    uint64_t m_version = 1;
};

// A colour component (5 bits) faded by a fade byte (as PaletteTransfer::fade's)
inline int Fade(int c, int f) {
    if (f & 0x40) {
        c = (f & 0x20) ? ((((c ^ 0x1F) * (~f & 0x1F)) >> 5) ^ 0x1F) : (c * (f & 0x1F) >> 5);
    }
    return c;
}
// A colour faded by a transfer's three fade bytes (none of them set: as it is)
inline uint16_t FadeColour(uint16_t value, uint32_t fade) {
    if (!(fade & 0x40400040u)) {
        return value;
    }
    const int r = Fade(value & 0x1F, (fade >> 24) & 0x7F), g = Fade((value >> 5) & 0x1F, (fade >> 16) & 0x7F),
              b = Fade((value >> 10) & 0x1F, fade & 0x7F);
    return static_cast<uint16_t>((value & 0x8000) | r | (g << 5) | (b << 10));
}

// The registers the video chip takes once a frame, at the vertical blank (the game writes them all in its first 45
// lines: no raster effects), as the game sets them, in 16-bit halves: what the native renderer reads as its frame's
// settings (a GPU's uniforms)
struct FrameRegisters {
    // The object lists' eight global scrolls: x (10 bits) and y, by the list's scroll number
    uint16_t scroll[8][2] = {};
    // The four tilemaps: scroll x and y; the settings (high half: on 0x8000, line scroll 0x4000, flips 0x0800/0x0400;
    // low half), the bases (high half: the line scroll table's and the map's places in sprite RAM, in KB) and a fourth
    // the game leaves as it is
    uint16_t tilemap[4][8] = {};
    // The screen's timing and the whole picture's zoom (the board's eight words 040C0060-7F, in halves: the picture's
    // edges in halves 0-3 and 8-11, the zooms in the low bytes of halves 7 and 15)
    uint16_t screen[16] = {};
    // The text layer's registers, a byte each (scrolls, palette base, its settings)
    uint8_t text[0x30] = {};
    bool operator==(const FrameRegisters &) const = default;
};

// An object list's head, as the game makes the frame's lists: where its objects are in sprite RAM and how many (and which
// global scroll), its position, the settings for all its objects. The board's object DMA takes the heads at the next
// vertical blank and flattens them with their objects into the frame's objects.
struct ListHead {
    uint32_t head = 0;     // bit 31 the end; 28-30 the global scroll; 16-24 the count; 4-14 the objects' place (16 bytes)
    uint32_t position = 0; // x in bits 16-25, y in 0-9
    uint32_t settings = 0; // bit 30: the list's bits a pixel for all, 29 its palette, 28/27 flips, 26 blend, 25 bits a
                           // pixel, 16-24 the palette
    // The game's priority the head was written for (it writes them from its highest number, 127, to 0: the board draws
    // a later head's objects over an earlier's, so a lower number is nearer the front); kNoPriority where the game's
    // code said none (an empty head, the pads) or it is not known (heads made again from sprite RAM, which does not
    // keep it). Not the board's: not compared with the board's heads
    uint8_t priority = 0xFF;
    bool operator==(const ListHead &o) const {
        return head == o.head && position == o.position && settings == o.settings;
    }
};
constexpr uint8_t kNoPriority = 0xFF;

// An object of the frame, its list's settings applied (what the board's object DMA makes of the heads and sprite RAM's
// object words, and what a GPU makes quads of: a tile, or lines of a tilemap when it has no width): the tile and its
// colour, flips, blend and bits a pixel (word 0), its place with the list's position and global scroll added (word 1:
// x in 16-25, y in 0-9), its size and zoom (word 2), word 3 as the game left it; and the main list's head it came from
struct Object {
    uint32_t words[4] = {};
    uint16_t group = 0;
    // (its head's priority, ListHead::priority: not the board's, not compared)
    uint8_t priority = kNoPriority;
    bool operator==(const Object &o) const {
        return words[0] == o.words[0] && words[1] == o.words[1] && words[2] == o.words[2] && words[3] == o.words[3] && group == o.group;
    }
};
// The frame's objects from the lists' heads, the frame's registers (the global scrolls) and sprite RAM's object words
// (512 KB, big-endian: the game's own memory, read as it stands at the vertical blank), as the board's DMA has them
// (MAME's rules: machine.cpp's SpriteDma, in the interface's terms): at most 511
void FlattenObjects(const std::vector<ListHead> &heads, const FrameRegisters &registers, const uint8_t *spriteRam, std::vector<Object> &out);

// The tiles the objects and tilemaps are drawn from: 8 MB of pixels, a byte each (16x16 tiles of 256 bytes), as the
// character DMA's transfers bring them in from the graphics SIMMs (most compressed: 6 bits a pixel with runs, or 8 bits
// with pairs from a table), and as the game's code writes a few itself (the DMA's own lists, a cleared area). What a
// GPU keeps as its tile texture.
class Textures {
public:
    static constexpr uint32_t kSize = 0x800000;
    Textures();
    // A character DMA's list (three longs a transfer, as the chip's list in character RAM has them, ended by a long with
    // bit 24 set), its transfers made
    void Load(const uint32_t *list, size_t longs, const uint8_t *graphics, size_t size);
    // Bytes written by the game's code at a place in the 8 MB (big-endian, `bytes` of them from `value`'s low end)
    void Write(uint32_t at, uint32_t value, int bytes);
    const uint8_t *Pixels() const {
        return m_pixels.data();
    }
    // Each 64 KB page's version: it changes whenever the page is written (a GPU's copy sends only the pages changed)
    static constexpr int kPages = 128;
    uint64_t PageVersion(int page) const {
        return m_versions[page];
    }
    void Reset() {
        m_table = 0xFFFFFFFF;
    }
    // (made again from the board's character RAM: a 64 KB page as it is there, and the compressed transfers' table)
    void PutPage(int page, const uint8_t *from);
    void SetTable(uint32_t table) {
        m_table = table;
    }
    uint32_t Table() const {
        return m_table;
    }
    // What the compressed transfers leave of their runs, as the board's character DMA keeps it (its save state holds
    // it): the last plain byte of the last 6-bit transfer, the last two bytes of the last 8-bit one; and how many of each
    // there have been (the board takes these from here when the interface makes its character RAM)
    struct Runs {
        uint64_t six = 0, eight = 0;
        uint8_t lastNormal = 0;
        uint16_t lastb = 0, lastb2 = 0;
    } runs;
    void Serialize(Archive &ar);

private:
    std::vector<uint8_t> m_pixels;
    uint32_t m_table = 0xFFFFFFFF; // where in the graphics the compressed transfers' table of pairs is (a transfer sets it)
    std::vector<uint64_t> m_versions = std::vector<uint64_t>(kPages, 0);
    uint64_t m_counter = 0;
    void Touch(uint32_t from, uint32_t to) { // (bytes from..to-1 written, within the 8 MB or past its end)
        for (uint32_t page = from >> 16; page <= ((to - 1) >> 16) && page < static_cast<uint32_t>(kPages) && to > from; page++) {
            m_versions[page] = ++m_counter;
        }
    }
};

// What the game has handed over
struct Video {
    Textures textures;
    Palette palette;
    FrameRegisters registers;
    // The object lists' heads for the next object DMA (the last the game made), ended where the game ends them
    std::vector<ListHead> lists;
    // The frame's objects, made from the heads at the object DMA (FlattenObjects): what is drawn until the next
    std::vector<Object> objects;
    // (the check: this frame's palette events as the rewrites made them)
    std::vector<PaletteEvent> paletteEvents;
    void Serialize(Archive &ar);
};

} // namespace cps3recomp::native
