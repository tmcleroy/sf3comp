// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "native.h"

#include "state.h"

#include <algorithm>
#include <cstdlib>

namespace cps3recomp::native {

namespace {
    bool g_keep = false;
    // (CPS3RECOMP_NATIVE: 1 kept, 0 not, whatever the port chose; unset: as it chose)
    int Forced() {
        static const int forced = [] {
            const char *v = getenv("CPS3RECOMP_NATIVE");
            return v && *v ? (*v != '0' ? 1 : 0) : -1;
        }();
        return forced;
    }
} // namespace

bool Checking() {
    static const bool on = [] {
        const char *v = getenv("CPS3RECOMP_NATIVE_CHECK");
        return v && *v && *v != '0';
    }();
    return on;
}

bool On() {
    return Checking() || (Forced() < 0 ? g_keep : Forced() == 1);
}

void Keep(bool on) {
    g_keep = on;
}

void Palette::SetAll(const uint16_t *colours) {
    std::copy(colours, colours + kColours, m_colours.begin());
    m_version++;
}

void Textures::PutPage(int page, const uint8_t *from) {
    const uint32_t at = static_cast<uint32_t>(page) * 0x10000;
    std::copy(from, from + 0x10000, m_pixels.begin() + at);
    Touch(at, at + 0x10000);
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
    m_version++;
}

void FlattenObjects(const std::vector<ListHead> &heads, const FrameRegisters &registers, const uint8_t *ram, std::vector<Object> &out) {
    static constexpr size_t kMost = 0x2000 / 16 - 1;
    auto word = [&](uint32_t n) {
        const uint8_t *p = ram + ((n * 4) & 0x7FFFC);
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    };
    out.clear();
    for (size_t h = 0; h < heads.size() && h < 0x2000 / 16 && out.size() < kMost; h++) {
        const ListHead &l = heads[h];
        const uint32_t at = (l.head & 0x00007FF0u) << 2, count = (l.head & 0x01FF0000u) >> 16;
        const int x = (l.position >> 16) & 0x3FF, y = l.position & 0x3FF;
        const uint32_t s = l.settings;
        const bool listBpp = (s >> 30) & 1, listPalette = (s >> 29) & 1, alpha = (s >> 26) & 1, bpp = (s >> 25) & 1;
        const uint32_t flipX = (s >> 28) & 1, flipY = (s >> 27) & 1, palette = (s >> 16) & 0x1FF;
        const uint16_t *scroll = registers.scroll[(l.head >> 28) & 7];
        const int scrollX = scroll[0] & 0x3FF, scrollY = scroll[1] & 0x3FF;
        for (uint32_t j = 0; j < count * 4 && out.size() < kMost; j += 4) {
            const uint32_t v1 = word(at + j), v2 = word(at + j + 1);
            uint32_t e1 = v1 & ~0x1FFFu;
            e1 |= listPalette ? palette : (v1 & 0x1FF);
            e1 |= (listBpp ? bpp : ((v1 >> 9) & 1)) ? 0x200 : 0;
            e1 |= (alpha || ((v1 >> 10) & 1)) ? 0x400 : 0;
            e1 |= (((v1 >> 12) & 1) ^ flipX) ? 0x1000 : 0;
            e1 |= (((v1 >> 11) & 1) ^ flipY) ? 0x800 : 0;
            const int mx = x + static_cast<int>((v2 >> 16) & 0x3FF) + scrollX + 1, my = y + static_cast<int>(v2 & 0x3FF) + scrollY;
            Object o;
            o.words[0] = e1;
            o.words[1] = (v2 & ~0x03FF03FFu) | ((static_cast<uint32_t>(mx) & 0x3FF) << 16) | (static_cast<uint32_t>(my) & 0x3FF);
            o.words[2] = word(at + j + 2);
            o.words[3] = word(at + j + 3);
            o.group = static_cast<uint16_t>(h);
            o.priority = l.priority;
            out.push_back(o);
        }
    }
}

Textures::Textures()
    : m_pixels(kSize, 0) {}

void Textures::Write(uint32_t at, uint32_t value, int bytes) {
    Touch(at & (kSize - 1), (at & (kSize - 1)) + static_cast<uint32_t>(bytes));
    for (int i = 0; i < bytes; i++) {
        m_pixels[(at + i) & (kSize - 1)] = static_cast<uint8_t>(value >> (8 * (bytes - 1 - i)));
    }
}

// The character DMA's transfers, as the board makes them (MAME's rules: machine.cpp's CharacterDma, in the interface's
// terms): kind 0 a plain copy; 2, 6 bits a pixel, a byte below 0x40 a pixel, 0x40 and up a run of the last pixel, 0x80 and
// up two bytes from the table of pairs; 3, 8 bits a pixel, a control byte for each eight, a pair from the table where its
// bit is set, two alike making the next a run's length; 4 sets the table
void Textures::Load(const uint32_t *list, size_t longs, const uint8_t *g, size_t size) {
    auto source = [&](uint32_t at) -> uint8_t { return at < size ? g[at] : 0; };
    uint8_t lastNormal = 0;
    uint16_t lastb = 0, lastb2 = 0;
    // (6 bits a pixel: a byte written, a run where it says so)
    auto byte6 = [&](uint8_t value, uint32_t to) -> uint32_t {
        to &= kSize - 1;
        if (value & 0x40) {
            uint32_t count = (value & 0x3F) + 1, done = 0;
            while (count--) {
                m_pixels[(to + done) & (kSize - 1)] = lastNormal & 0x3F;
                done++;
                if (to + done > kSize - 1) {
                    break;
                }
            }
            return done;
        }
        m_pixels[to] = value;
        lastNormal = value;
        return 1;
    };
    auto byte8 = [&](uint8_t value, uint32_t to) -> uint32_t {
        if (lastb == lastb2) {
            const int run = (value + 1) & 0xFF;
            for (int i = 0; i < run; i++) {
                m_pixels[(to + i) & (kSize - 1)] = static_cast<uint8_t>(lastb);
            }
            lastb2 = 0xFFFF;
            return static_cast<uint32_t>(run);
        }
        lastb2 = lastb;
        lastb = value;
        m_pixels[to & (kSize - 1)] = value;
        return 1;
    };
    for (size_t i = 0; i + 2 < longs; i += 3) {
        const uint32_t d1 = list[i], d2 = list[i + 1], d3 = list[i + 2];
        if (d1 & 0x01000000u) {
            break;
        }
        const uint32_t kind = (d1 >> 21) & 7;
        uint32_t length = ((d1 & 0x001FFFFFu) + 1) << 3;
        uint32_t to = d2 << 3;
        uint32_t from = (d3 << 1) - 0x400000;
        const uint32_t first = to & (kSize - 1);
        switch (kind) {
        case 4:
            m_table = from;
            break;
        case 2: {
            lastNormal = 0;
            int64_t remaining = length;
            auto put = [&](uint8_t value) {
                const uint32_t done = byte6(value, to);
                remaining -= done;
                to += done;
                return to <= kSize - 1 && remaining > 0;
            };
            while (remaining > 0) {
                uint8_t b = source(from++);
                if (b & 0x80) {
                    b &= 0x7F;
                    if (!put(source(m_table + b * 2)) || !put(source(m_table + b * 2 + 1))) {
                        break;
                    }
                } else if (!put(b)) {
                    break;
                }
            }
            runs.six++;
            runs.lastNormal = lastNormal;
            break;
        }
        case 3: {
            const uint32_t start = to;
            lastb = 0xFFFE;
            lastb2 = 0xFFFF;
            bool done = false;
            while (!done) {
                uint8_t control = source(from++);
                for (int bit = 0; bit < 8 && !done; bit++) {
                    const uint8_t p = source(from);
                    if (control & 0x80) {
                        to += byte8(source(m_table + (p & 0x7F) * 2), to);
                        to += byte8(source(m_table + (p & 0x7F) * 2 + 1), to);
                    } else {
                        to += byte8(p, to);
                    }
                    from++;
                    control = static_cast<uint8_t>(control << 1);
                    done = to - start >= length;
                }
            }
            runs.eight++;
            runs.lastb = lastb;
            runs.lastb2 = lastb2;
            break;
        }
        case 0:
            while (length--) {
                m_pixels[to & (kSize - 1)] = source(from++);
                to++;
            }
            break;
        default:
            break;
        }
        if (kind != 4) {
            Touch(first, std::min<uint32_t>(first + (to - (d2 << 3)) + 0x200, kSize)); // (runs may go a little past)
            if (first + (to - (d2 << 3)) > kSize) {
                Touch(0, kSize); // (wrapped)
            }
        }
    }
}

void Textures::Serialize(Archive &ar) {
    ar.Vec(m_pixels);
    ar.Pod(m_table);
    if (!ar.Saving()) {
        Touch(0, kSize); // (all of it may have changed)
    }
}

void Video::Serialize(Archive &ar) {
    ar.Mark(0x4E415456); // NATV
    textures.Serialize(ar);
    palette.Serialize(ar);
    ar.Pod(registers);
    ar.Vec(lists);
    ar.Vec(objects);
}

} // namespace cps3recomp::native
