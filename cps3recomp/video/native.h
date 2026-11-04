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

// Whether the native interface is kept this run (CPS3RECOMP_NATIVE_CHECK; fixed at start): when not, handing it things
// does nothing, and it is not in save states
bool On();

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
    }
    const uint16_t *Colours() const {
        return m_colours.data();
    }
    void Serialize(Archive &ar);

private:
    std::vector<uint16_t> m_colours;
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

// What the game has handed over
struct Video {
    Palette palette;
    // (the check: this frame's palette events as the rewrites made them)
    std::vector<PaletteEvent> paletteEvents;
    void Serialize(Archive &ar);
};

} // namespace cps3recomp::native
