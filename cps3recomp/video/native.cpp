// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "native.h"

#include "state.h"

#include <cstdlib>

namespace cps3recomp::native {

bool On() {
    static const bool on = [] {
        const char *v = getenv("CPS3RECOMP_NATIVE_CHECK");
        return v && *v && *v != '0';
    }();
    return on;
}

Palette::Palette()
    : m_colours(kColours, 0) {}

void Palette::Transfer(const PaletteTransfer &t, const uint8_t *graphics, size_t size, ColourHook hook) {
    for (uint32_t i = 0; i < t.length; i++) {
        const uint32_t word = t.source + i;
        const size_t at = static_cast<size_t>(word ^ 1) * 2; // (the pair's two colours swapped)
        uint16_t colour = at + 1 < size ? static_cast<uint16_t>((graphics[at] << 8) | graphics[at + 1]) : 0;
        const uint32_t index = (t.dest + i) ^ 1;
        if (hook) {
            colour = hook(word, index, colour);
        }
        Set(index, FadeColour(colour, t.fade));
    }
}

void Palette::Serialize(Archive &ar) {
    ar.Vec(m_colours);
}

void Video::Serialize(Archive &ar) {
    ar.Mark(0x4E415456); // NATV
    palette.Serialize(ar);
}

} // namespace cps3recomp::native
