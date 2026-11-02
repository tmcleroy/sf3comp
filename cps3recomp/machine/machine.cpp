// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cps3recomp::machine {

namespace {
    uint32 Get32(const uint8 *p) {
        return (static_cast<uint32>(p[0]) << 24) | (static_cast<uint32>(p[1]) << 16) | (static_cast<uint32>(p[2]) << 8) | p[3];
    }
    uint16 Get16(const uint8 *p) {
        return static_cast<uint16>((p[0] << 8) | p[1]);
    }
    void Put32(uint8 *p, uint32 v) {
        p[0] = static_cast<uint8>(v >> 24);
        p[1] = static_cast<uint8>(v >> 16);
        p[2] = static_cast<uint8>(v >> 8);
        p[3] = static_cast<uint8>(v);
    }
    void Put16(uint8 *p, uint16 v) {
        p[0] = static_cast<uint8>(v >> 8);
        p[1] = static_cast<uint8>(v);
    }
    void Combine(uint32 &target, uint32 value, uint32 mask) {
        target = (target & ~mask) | (value & mask);
    }
    // A colour component faded (bit 6 on; bit 5: towards white instead of black; bits 0-4 the amount)
    int Fade(int c, int f) {
        if (f & 0x40) {
            c = (f & 0x20) ? ((((c ^ 0x1F) * (~f & 0x1F)) >> 5) ^ 0x1F) : (c * (f & 0x1F) >> 5);
        }
        return c;
    }
    const bool g_trace = getenv("CPS3RECOMP_TRACE_IO") != nullptr;
    // (debugging aid, CPS3RECOMP_TRACE_DMA=1: the video DMAs of each frame, counted)
    const bool g_traceDma = getenv("CPS3RECOMP_TRACE_DMA") != nullptr;
    unsigned g_charDmas = 0, g_palDmas = 0, g_spriteDmas = 0, g_charBytes = 0;
} // namespace

Machine::Machine(const RomSet &rom)
    : m_mainRam(0x80000)
    , mainRam(m_mainRam.data())
    , m_rom(rom)
    , m_cpu(*this, true) {
    m_spriteRam.assign(0x80000, 0);
    m_charRam.assign(0x800000, 0);
    m_fram.assign(0x400, 0);
    m_colourRam.assign(0x20000, 0);
    m_rgb.assign(0x20000, 0);
    m_ssRam.assign(0x8000, 0);
    m_spriteList.assign(0x2000 / 4, 0);
    m_spriteGroup.assign(0x2000 / 16, 0);
    auto bytes = [](const std::vector<uint32> &words, std::vector<uint8> &out) {
        out.resize(words.size() * 4);
        for (size_t i = 0; i < words.size(); i++) {
            Put32(&out[i * 4], words[i]);
        }
    };
    bytes(m_rom.bios, m_biosBytes);
    bytes(m_rom.Opcodes(), m_programBytes);
    if (!m_rom.opcodes.empty()) {
        bytes(m_rom.program, m_programDataBytes);
    }
}

bool g_flashPlain = true;

void Machine::UpdateFlashPlain() {
    g_flashPlain = true;
    for (int simm = 1; simm <= 2; simm++) {
        for (int chip = 0; chip < 4; chip++) {
            g_flashPlain = g_flashPlain && m_flash[simm][chip].mode == FlashChip::Normal;
        }
    }
}

void Machine::Reset() {
    std::fill(m_mainRam.begin(), m_mainRam.end(), 0);
    m_tableAddress = 0xFFFFFFFF;
    m_dmaStatus = 0;
    m_spriteDma = 0;
    m_spriteCount = 0;
    m_spriteList[0] = 0x80000000;
    m_irqLines = 0;
    m_dmaDoneAt = m_spriteDmaDoneAt = 0;
    for (auto &simm : m_flash) {
        for (FlashChip &chip : simm) {
            chip = {};
        }
    }
    UpdateFlashPlain();
    m_now = m_frameStart = m_spillover = 0;
    m_frame = 0;
    m_cpu.Reset(true);
    UpdateInterrupts();
}

double Machine::FramePosition() const {
    return static_cast<double>(Now() - m_frameStart) / (kLines * kCyclesPerLine);
}

void Machine::UpdateInterrupts() {
    for (int level = 15; level > 0; level--) {
        if (m_irqLines & (1u << level)) {
            m_cpu.SetExternalInterrupt(static_cast<uint8>(level), static_cast<uint8>(64 + level / 2)); // (auto vectors)
            return;
        }
    }
    m_cpu.SetExternalInterrupt(0, 0);
}

void Machine::AcknowledgeExternalInterrupt(SH2 &) {
    // (the lines are held until the program acknowledges them at 05100000 and 05110000)
}

// The frame starts at the vertical blank: its interrupt, 40 lines of blank, then the 224 lines shown. When it
// returns, the picture is what the board would show next.
void Machine::RunFrame() {
    m_frameStart = m_now;
    m_irqLines |= 1u << 12;
    UpdateInterrupts();
    RunUntil(m_frameStart + static_cast<uint64>(kLines) * kCyclesPerLine);
    if (g_traceDma) {
        fprintf(stderr, "frame %llu: %u character DMAs (%u bytes), %u palette, %u sprite lists (%u objects)\n", static_cast<unsigned long long>(m_frame),
                g_charDmas, g_charBytes, g_palDmas, g_spriteDmas, m_spriteCount);
        g_charDmas = g_palDmas = g_spriteDmas = g_charBytes = 0;
    }
    m_frame++;
}

void Machine::RunUntil(uint64 target) {
    while (m_now < target) {
        uint64 next = target;
        if (m_dmaDoneAt && m_dmaDoneAt < next) {
            next = m_dmaDoneAt;
        }
        if (m_spriteDmaDoneAt && m_spriteDmaDoneAt < next) {
            next = m_spriteDmaDoneAt;
        }
        if (next > m_now) {
            const uint64 slice = next - m_now;
            m_sliceBase = m_now; // (the CPU starts the slice m_spillover cycles in: its count starts there too)
            m_inSlice = true;
            const uint64 ran = m_cpu.Advance(slice, m_spillover);
            m_inSlice = false;
            if (ran >= slice) {
                m_spillover = ran - slice;
                m_now = next;
            } else {
                m_spillover = 0;
                m_now += std::max<uint64>(ran, 1);
            }
        }
        if (m_dmaDoneAt && m_now >= m_dmaDoneAt) {
            m_dmaDoneAt = 0;
            if (g_traceDma) {
                fprintf(stderr, "  DMA done (frame %llu, cycle %llu)\n", static_cast<unsigned long long>(m_frame), static_cast<unsigned long long>(m_now - m_frameStart));
            }
            m_dmaStatus &= ~6;
            m_irqLines |= 1u << 10;
            UpdateInterrupts();
        }
        if (m_spriteDmaDoneAt && m_now >= m_spriteDmaDoneAt) {
            m_spriteDmaDoneAt = 0;
            m_dmaStatus &= ~1;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// The bus. Plain memories are read and written directly; everything else goes through ReadMasked / WriteMasked,
// which see the aligned 32-bit word and which of its bytes are meant (big-endian: the byte at address 0 is bits
// 24-31).

uint8 Machine::BusRead8(uint32 a) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        return m_spriteRam[a & 0x7FFFF];
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        return m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFF)];
    }
    if (a < 0x80000) {
        return static_cast<uint8>(m_rom.bios[a >> 2] >> ((3 - (a & 3)) * 8));
    }
    const uint32 shift = (3 - (a & 3)) * 8;
    return static_cast<uint8>(ReadMasked(a & ~3u, 0xFFu << shift) >> shift);
}
uint16 Machine::BusRead16(uint32 a) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        return Get16(&m_spriteRam[a & 0x7FFFE]);
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        return Get16(&m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFE)]);
    }
    if (a < 0x80000) {
        return static_cast<uint16>(m_rom.bios[a >> 2] >> ((2 - (a & 2)) * 8));
    }
    const uint32 shift = (2 - (a & 2)) * 8;
    return static_cast<uint16>(ReadMasked(a & ~3u, 0xFFFFu << shift) >> shift);
}
uint32 Machine::BusRead32(uint32 a) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        return Get32(&m_spriteRam[a & 0x7FFFC]);
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        return Get32(&m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFC)]);
    }
    if (a < 0x80000) {
        return m_rom.bios[a >> 2];
    }
    return ReadMasked(a, 0xFFFFFFFFu);
}
void Machine::BusWrite8(uint32 a, uint8 v) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        m_spriteRam[a & 0x7FFFF] = v;
        return;
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        TouchChar(((m_charBank & 7) << 20) | (a & 0xFFFFF));
        m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFF)] = v;
        return;
    }
    const uint32 shift = (3 - (a & 3)) * 8;
    WriteMasked(a & ~3u, static_cast<uint32>(v) << shift, 0xFFu << shift);
}
void Machine::BusWrite16(uint32 a, uint16 v) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        Put16(&m_spriteRam[a & 0x7FFFE], v);
        return;
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        TouchChar(((m_charBank & 7) << 20) | (a & 0xFFFFF));
        Put16(&m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFE)], v);
        return;
    }
    const uint32 shift = (2 - (a & 2)) * 8;
    WriteMasked(a & ~3u, static_cast<uint32>(v) << shift, 0xFFFFu << shift);
}
void Machine::BusWrite32(uint32 a, uint32 v) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        Put32(&m_spriteRam[a & 0x7FFFC], v);
        return;
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        TouchChar(((m_charBank & 7) << 20) | (a & 0xFFFFF));
        Put32(&m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFC)], v);
        return;
    }
    WriteMasked(a, v, 0xFFFFFFFFu);
}

uint32 Machine::SpriteRam(uint32 word) const {
    return Get32(&m_spriteRam[(word * 4) & 0x7FFFC]);
}

uint32 Machine::ReadMasked(uint32 a, uint32 mask) {
    if (a >= 0x03000000u && a < 0x03000400u) {
        return Get32(&m_fram[a & 0x3FC]);
    }
    if (a >= 0x04080000u && a < 0x040C0000u) {
        const uint32 index = (a - 0x04080000u) / 2;
        return (static_cast<uint32>(m_colourRam[index]) << 16) | m_colourRam[index + 1];
    }
    if (a >= 0x040C0000u && a < 0x040C0100u) {
        if (a == 0x040C000Cu) {
            return static_cast<uint32>(m_dmaStatus) << 16;
        }
        return 0;
    }
    if (a >= 0x040E0000u && a < 0x040E0300u) {
        return ReadSound((a - 0x040E0000u) / 4);
    }
    if (a >= 0x04200000u && a < 0x04400000u) {
        return ReadGfxFlash(a, mask);
    }
    if (a == 0x05000000u) {
        return inputs;
    }
    if (a == 0x05000004u) {
        return extra;
    }
    if (a >= 0x05000A00u && a < 0x05000A20u) {
        return 0xFFFFFFFF;
    }
    if (a >= 0x05001000u && a < 0x05001204u) {
        const uint32 offset = a - 0x05001000u;
        if (offset >= 0x100 && offset < 0x180) {
            const uint32 word = eeprom[(offset - 0x100) / 4];
            m_eepromLatch = static_cast<uint16>((mask & 0xFF000000u) ? word >> 16 : word);
            return 0;
        }
        if (offset == 0x200) {
            return (mask & 0xFF000000u) ? 0 : m_eepromLatch;
        }
        return 0;
    }
    if (a >= 0x05040000u && a < 0x05050000u) {
        const uint32 word = (a - 0x05040000u) / 4;
        return (static_cast<uint32>(m_ssRam[word * 2]) << 16) | m_ssRam[word * 2 + 1];
    }
    if (a >= 0x06000000u && a < 0x07000000u) {
        return ReadProgramFlash(a, mask, true);
    }
    if (g_trace) {
        fprintf(stderr, "read %08X & %08X at PC %08X\n", a, mask, m_cpu.PC);
    }
    return 0;
}

void Machine::WriteMasked(uint32 a, uint32 v, uint32 mask) {
    if (a >= 0x03000000u && a < 0x03000400u) {
        uint32 word = Get32(&m_fram[a & 0x3FC]);
        Combine(word, v, mask);
        Put32(&m_fram[a & 0x3FC], word);
        return;
    }
    if (a >= 0x04080000u && a < 0x040C0000u) {
        const uint32 index = (a - 0x04080000u) / 2;
        if (mask & 0xFFFF0000u) {
            uint32 c = m_colourRam[index];
            Combine(c, v >> 16, mask >> 16);
            SetColour(index, static_cast<uint16>(c), 0);
        }
        if (mask & 0xFFFFu) {
            uint32 c = m_colourRam[index + 1];
            Combine(c, v, mask & 0xFFFF);
            SetColour(index + 1, static_cast<uint16>(c), 0);
        }
        return;
    }
    if (a >= 0x040C0000u && a < 0x040C0100u) {
        WritePpu(a - 0x040C0000u, v, mask);
        return;
    }
    if (a >= 0x040E0000u && a < 0x040E0300u) {
        WriteSound((a - 0x040E0000u) / 4, v, mask);
        return;
    }
    if (a == 0x05000008u) {
        return; // (coin lockouts and counters)
    }
    if (a >= 0x05001000u && a < 0x05001204u) {
        const uint32 offset = a - 0x05001000u;
        if (offset >= 0x80 && offset < 0x100) {
            Combine(eeprom[(offset - 0x80) / 4], v, mask);
        }
        return;
    }
    if (a >= 0x05040000u && a < 0x05050000u) {
        const uint32 word = (a - 0x05040000u) / 4;
        if (mask & 0x00FF0000u) {
            m_ssRam[word * 2] = static_cast<uint8>(v >> 16);
        }
        if (mask & 0x000000FFu) {
            m_ssRam[word * 2 + 1] = static_cast<uint8>(v);
        }
        return;
    }
    if (a >= 0x05050000u && a < 0x0505002Cu) {
        const uint32 word = (a - 0x05050000u) / 4;
        for (int lane = 0; lane < 2; lane++) {
            if (!(mask & (lane ? 0x000000FFu : 0x00FF0000u))) {
                continue;
            }
            const uint32 reg = word * 2 + lane;
            const uint8 data = static_cast<uint8>(lane ? v : v >> 16);
            if (reg < m_ssRegs.size()) {
                m_ssRegs[reg] = data;
            }
            switch (reg) {
            case 0x07: m_ssHScroll = static_cast<uint16>((m_ssHScroll & 0xFF00) | data); break;
            case 0x08: m_ssHScroll = static_cast<uint16>((m_ssHScroll & 0xFF) | (data << 8)); break;
            case 0x10: m_ssVScroll = static_cast<uint16>((m_ssVScroll & 0xFF00) | data); break;
            case 0x11: m_ssVScroll = static_cast<uint16>((m_ssVScroll & 0xFF) | (data << 8)); break;
            case 0x12: m_ssPaletteBase = data; break;
            default: break;
            }
        }
        return;
    }
    if (a == 0x05100000u || a == 0x05110000u || a == 0x05120000u || a == 0x05130000u) {
        static constexpr int kLevel[4] = {12, 10, 14, 6};
        if (g_trace) {
            fprintf(stderr, "acknowledge %d at PC %08X (frame %llu)\n", kLevel[(a >> 16) & 3], m_cpu.PC, static_cast<unsigned long long>(m_frame));
        }
        m_irqLines &= ~(1u << kLevel[(a >> 16) & 3]);
        UpdateInterrupts();
        return;
    }
    if (a >= 0x06000000u && a < 0x07000000u) {
        WriteProgramFlash(a, v, mask);
        return;
    }
    if (a >= 0x04200000u && a < 0x04400000u) {
        WriteGfxFlash(a, v, mask);
        return;
    }
    if (g_trace) {
        fprintf(stderr, "write %08X = %08X & %08X at PC %08X\n", a, v, mask, m_cpu.PC);
    }
}

// --- the video chip's registers ---
void Machine::WritePpu(uint32 offset, uint32 v, uint32 mask) {
    const uint32 word = offset & ~3u;
    {
        uint32 stored = Get32(&m_ppuRegs[word]);
        Combine(stored, v, mask);
        Put32(&m_ppuRegs[word], stored);
    }
    if (word < 0x20) {
        Combine(m_videoRegs.globalScroll[word / 4], v, mask);
    } else if (word < 0x60) {
        Combine(m_videoRegs.tilemap[(word - 0x20) / 4], v, mask);
    } else if (word < 0x80) {
        Combine(m_videoRegs.crtc[(word - 0x60) / 4], v, mask);
    } else if (word == 0x80) {
        if (mask & 0xFFFF) {
            const uint16 previous = m_spriteDma;
            m_spriteDma = static_cast<uint16>((m_spriteDma & ~mask) | (v & mask));
            // (the games write 8, 9, 8, 9: the list is taken at a 9 followed by an 8)
            if (!(m_dmaStatus & 1) && (m_spriteDma & 9) == 8 && (previous & 9) == 9) {
                SpriteDma();
                m_dmaStatus |= 1;
                m_spriteDmaDoneAt = Now() + 100; // (4 microseconds)
                m_cpu.RequestYield();
            }
        }
    } else if (word == 0x84) {
        if (mask & 0xFF) {
            m_charBank = v;
        }
    } else if (word == 0x88) {
        if (mask & 0xFF000000u) {
            m_gfxFlashBank = static_cast<sint32>(v >> 16) - 2; // (the first 4 MB are not the graphics')
        }
    } else if (word == 0x94) {
        if (mask & 0xFF) {
            m_charDmaSource = v & 0xFFFF;
        }
    } else if (word == 0x98) {
        Combine(m_charDmaOther, v, mask);
        if ((mask & 0xFF000000u) && ((v >> 16) & 0x40)) {
            if (g_traceDma) {
                fprintf(stderr, "  character DMA list %06X at PC %08X PR %08X (frame %llu, cycle %llu, status %X)\n", m_charDmaSource | (m_charDmaOther & 0x003F0000u),
                        m_cpu.PC, m_cpu.PR, static_cast<unsigned long long>(m_frame), static_cast<unsigned long long>(Now() - m_frameStart), m_dmaStatus);
            }
            CharacterDma(m_charDmaSource | (m_charDmaOther & 0x003F0000u));
        }
    } else if (word == 0xA0) {
        Combine(m_palDmaSource, v, mask);
    } else if (word == 0xA4) {
        Combine(m_palDmaDest, v, mask);
    } else if (word == 0xA8) {
        Combine(m_palDmaFade, v, mask);
    } else if (word == 0xAC) {
        Combine(m_palDmaOther, v, mask);
        if (mask & 0xFF000000u) {
            m_palDmaLength = v >> 16;
        }
        if ((mask & 0xFF) && (v & 2)) {
            PaletteDma(v);
        }
    }
}

void Machine::MakeColours() {
    for (uint32 i = 0; i < m_colourRam.size(); i++) {
        const uint16 value = m_colourRam[i];
        const uint32 r = value & 0x1F, g = (value >> 5) & 0x1F, b = (value >> 10) & 0x1F;
        m_rgb[i] = (r << 3) | ((g << 3) << 8) | ((b << 3) << 16);
    }
}

void Machine::SetColour(uint32 index, uint16 value, uint32 fade) {
    int r = value & 0x1F, g = (value >> 5) & 0x1F, b = (value >> 10) & 0x1F;
    if (fade & 0x40400040u) {
        r = Fade(r, (fade >> 24) & 0x7F);
        g = Fade(g, (fade >> 16) & 0x7F);
        b = Fade(b, fade & 0x7F);
        value = static_cast<uint16>((value & 0x8000) | r | (g << 5) | (b << 10));
    }
    index &= 0x1FFFF;
    m_colourRam[index] = value;
    // (the video DAC's low three bits of each component are grounded)
    m_rgb[index] = static_cast<uint32>(r << 3) | (static_cast<uint32>(g << 3) << 8) | (static_cast<uint32>(b << 3) << 16);
}

// The object lists: sprite RAM's first 0x2000 bytes list them, four words each (where its objects are, how many,
// a position, a global scroll, and settings that apply to all of them). The DMA flattens them into one list of
// objects as the renderer draws them, with the list's settings applied.
void Machine::SpriteDma() {
    g_spriteDmas++;
    static constexpr uint32 kWords = 0x2000 / 4, kStop = 0x80000000u;
    uint32 out = 0;
    m_spriteCount = 0;
    m_spriteList[0] = kStop;
    for (uint32 i = 0; i < kWords; i += 4) {
        const uint32 head = SpriteRam(i);
        if (head & kStop) {
            break;
        }
        const uint32 at = (head & 0x00007FF0u) << 2;
        const uint32 length = (head & 0x01FF0000u) >> 16;
        const uint32 position = SpriteRam(i + 1), settings = SpriteRam(i + 2);
        const int x = (position >> 16) & 0x3FF, y = position & 0x3FF;
        const bool listBpp = (settings >> 30) & 1, listPalette = (settings >> 29) & 1;
        const uint32 flipX = (settings >> 28) & 1, flipY = (settings >> 27) & 1;
        const bool alpha = (settings >> 26) & 1, bpp = (settings >> 25) & 1;
        const uint32 palette = (settings >> 16) & 0x1FF;
        const uint32 scroll = m_videoRegs.globalScroll[(head >> 28) & 7];
        const int scrollX = (scroll >> 16) & 0x3FF, scrollY = scroll & 0x3FF;
        for (uint32 j = 0; j < length * 4; j += 4) {
            if (out >= kWords - 4) {
                break;
            }
            const uint32 v1 = SpriteRam(at + j), v2 = SpriteRam(at + j + 1);
            uint32 e1 = v1 & ~0x1FFFu;
            e1 |= listPalette ? palette : (v1 & 0x1FF);
            e1 |= (listBpp ? bpp : ((v1 >> 9) & 1)) ? 0x200 : 0;
            e1 |= (alpha || ((v1 >> 10) & 1)) ? 0x400 : 0;
            e1 |= (((v1 >> 12) & 1) ^ flipX) ? 0x1000 : 0;
            e1 |= (((v1 >> 11) & 1) ^ flipY) ? 0x800 : 0;
            const int mx = x + static_cast<int>((v2 >> 16) & 0x3FF) + scrollX + 1;
            const int my = y + static_cast<int>(v2 & 0x3FF) + scrollY;
            const uint32 e2 = (v2 & ~0x03FF03FFu) | ((static_cast<uint32>(mx) & 0x3FF) << 16) | (static_cast<uint32>(my) & 0x3FF);
            m_spriteList[out] = e1;
            m_spriteList[out + 1] = e2;
            m_spriteList[out + 2] = SpriteRam(at + j + 2);
            m_spriteList[out + 3] = SpriteRam(at + j + 3);
            m_spriteGroup[out / 4] = static_cast<uint16>(i / 4);
            out += 4;
            m_spriteCount++;
        }
        if (out >= kWords - 4) {
            break;
        }
    }
    m_spriteList[out] = kStop;
    for (uint32 k = 1; k < 4 && out + k < kWords; k++) {
        m_spriteList[out + k] = 0;
    }
}

// The palette DMA: colours from the graphics SIMMs into colour RAM, faded as they go
void Machine::PaletteDma(uint32 control) {
    g_palDmas++;
    const uint32 source = (m_palDmaSource << 1) - 0x400000;
    const uint32 length = m_palDmaLength | ((control & 1) << 16);
    const std::vector<uint8> &g = m_rom.graphics;
    for (uint32 i = 0; i < length; i++) {
        // (in 32-bit words the two colours of each word are swapped on both sides, as MAME has it)
        const size_t at = static_cast<size_t>(((source >> 1) + i) ^ 1) * 2;
        const uint16 colour = at + 1 < g.size() ? static_cast<uint16>((g[at] << 8) | g[at + 1]) : 0;
        SetColour((m_palDmaDest + i) ^ 1, colour, m_palDmaFade);
    }
    m_dmaStatus |= 4;
    m_dmaDoneAt = Now() + 2500; // (100 microseconds: the DMA's real speed is not known)
    m_cpu.RequestYield();
}

// The character DMA: a list in character RAM of transfers from the graphics SIMMs, most of them compressed
uint32 Machine::CharDmaByte(uint8 value, uint32 destination) {
    destination &= 0x7FFFFF;
    if (value & 0x40) {
        // (a run: the last plain byte again, (value & 0x3F) + 1 times)
        uint32 count = (value & 0x3F) + 1, done = 0;
        while (count--) {
            m_charRam[(destination + done) & 0x7FFFFF] = m_lastNormalByte & 0x3F;
            TouchChar((destination + done) & 0x7FFFFF);
            done++;
            if (destination + done > 0x7FFFFF) {
                break;
            }
        }
        return done;
    }
    m_charRam[destination] = value;
    TouchChar(destination);
    m_lastNormalByte = value;
    return 1;
}

uint32 Machine::CharDmaByte8(uint8 value, uint32 destination) {
    if (m_lastb == m_lastb2) {
        // (two bytes alike: this one counts the run)
        const int run = (value + 1) & 0xFF;
        for (int i = 0; i < run; i++) {
            m_charRam[(destination + i) & 0x7FFFFF] = static_cast<uint8>(m_lastb);
            TouchChar((destination + i) & 0x7FFFFF);
        }
        m_lastb2 = 0xFFFF;
        return static_cast<uint32>(run);
    }
    m_lastb2 = m_lastb;
    m_lastb = value;
    m_charRam[destination & 0x7FFFFF] = value;
    TouchChar(destination & 0x7FFFFF);
    return 1;
}

void Machine::CharacterDma(uint32 listAddress) {
    const std::vector<uint8> &g = m_rom.graphics;
    auto source = [&](uint32 at) -> uint8 { return at < g.size() ? g[at] : 0; };
    for (uint32 i = 0; i < 0x1000; i += 3) {
        const uint32 base = (listAddress + i) * 4;
        const uint32 d1 = Get32(&m_charRam[base & 0x7FFFFC]), d2 = Get32(&m_charRam[(base + 4) & 0x7FFFFC]),
                     d3 = Get32(&m_charRam[(base + 8) & 0x7FFFFC]);
        if (d1 & 0x01000000u) {
            break; // (the end of the list)
        }
        const uint32 command = (d1 >> 21) & 7;
        uint32 length = ((d1 & 0x001FFFFFu) + 1) << 3;
        g_charDmas++;
        g_charBytes += command == 4 ? 0 : length;
        uint32 destination = d2 << 3;
        uint32 from = (d3 << 1) - 0x400000;
        switch (command) {
        case 4: // the table of byte pairs the compressed transfers use
            m_tableAddress = from;
            break;
        case 2: { // 6 bits a pixel, compressed: most objects and backgrounds
            m_lastNormalByte = 0;
            int64_t remaining = length;
            auto put = [&](uint8 value) {
                const uint32 done = CharDmaByte(value, destination);
                remaining -= done;
                destination += done;
                return destination <= 0x7FFFFF && remaining > 0;
            };
            while (remaining > 0) {
                uint8 byte = source(from++);
                if (byte & 0x80) {
                    byte &= 0x7F;
                    if (!put(source(m_tableAddress + byte * 2)) || !put(source(m_tableAddress + byte * 2 + 1))) {
                        break;
                    }
                } else if (!put(byte)) {
                    break;
                }
            }
            break;
        }
        case 3: { // 8 bits a pixel, compressed otherwise (New Generation's Sean stage)
            const uint32 start = destination;
            m_lastb = 0xFFFE;
            m_lastb2 = 0xFFFF;
            bool done = false;
            while (!done) {
                uint8 control = source(from++);
                for (int bit = 0; bit < 8 && !done; bit++) {
                    const uint8 p = source(from);
                    if (control & 0x80) {
                        destination += CharDmaByte8(source(m_tableAddress + (p & 0x7F) * 2), destination);
                        destination += CharDmaByte8(source(m_tableAddress + (p & 0x7F) * 2 + 1), destination);
                    } else {
                        destination += CharDmaByte8(p, destination);
                    }
                    from++;
                    control = static_cast<uint8>(control << 1);
                    done = destination - start >= length;
                }
            }
            break;
        }
        case 0: // not compressed
            while (length--) {
                m_charRam[destination & 0x7FFFFF] = source(from++);
                TouchChar(destination & 0x7FFFFF);
                destination++;
            }
            break;
        default:
            if (g_trace) {
                fprintf(stderr, "character DMA: command %u\n", command);
            }
            break;
        }
    }
    m_dmaStatus |= 2;
    m_dmaDoneAt = Now() + 2500;
    m_cpu.RequestYield();
}

// --- the sound chip's registers (the mixing is sound/pcm.cpp) ---
uint32 Machine::ReadSound(uint32 offset) {
    if (offset < 0x80) {
        return voices[offset / 8].regs[offset & 7];
    }
    if (offset == 0x80) {
        return static_cast<uint32>(keyOn) << 16;
    }
    return 0;
}

void Machine::WriteSound(uint32 offset, uint32 v, uint32 mask) {
    if (beforeSoundWrite) {
        beforeSoundWrite(soundContext);
    }
    if (offset < 0x80) {
        Combine(voices[offset / 8].regs[offset & 7], v, mask);
    } else if (offset == 0x80) {
        const uint16 key = static_cast<uint16>(v >> 16);
        // (debugging aid, CPS3RECOMP_TRACE_VOICES=1: each voice keyed on, and the sample it starts at)
        static const bool traceVoices = getenv("CPS3RECOMP_TRACE_VOICES") != nullptr;
        if (traceVoices) {
            for (int i = 0; i < 16; i++) {
                if ((key & (1 << i)) && !(keyOn & (1 << i))) {
                    fprintf(stderr, "voice %d on, sample %08X, frame %llu\n", i, voices[i].regs[1], static_cast<unsigned long long>(m_frame));
                }
            }
        }
        for (int i = 0; i < 16; i++) {
            if ((key & (1 << i)) && !(keyOn & (1 << i))) {
                voices[i].position = voices[i].fraction = 0; // (key on)
            }
        }
        keyOn = key;
    }
}

// --- the flash SIMMs ---
// The program SIMMs: four chips side by side, chip 0 the word's high byte; a chip's address is the word's
uint32 Machine::ReadProgramFlash(uint32 a, uint32 mask, bool deciphered) {
    const uint32 simm = 1 + ((a >> 23) & 1), offset = (a & 0x7FFFFF) >> 2;
    FlashChip *chips = m_flash[simm];
    if (chips[0].mode == FlashChip::Normal && chips[1].mode == FlashChip::Normal && chips[2].mode == FlashChip::Normal &&
        chips[3].mode == FlashChip::Normal) [[likely]] {
        return deciphered ? m_rom.program[(a & 0xFFFFFF) >> 2] : m_rom.programRaw[(a & 0xFFFFFF) >> 2];
    }
    const uint32 raw = m_rom.programRaw[(a & 0xFFFFFF) >> 2];
    uint32 value = 0;
    for (int chip = 0; chip < 4; chip++) {
        const int shift = (3 - chip) * 8;
        value |= static_cast<uint32>(chips[chip].Read(offset, static_cast<uint8>(raw >> shift))) << shift;
    }
    return value ^ (deciphered && IsEnciphered(a) ? CipherMask(a & ~3u, m_rom.key) : 0);
}

void Machine::WriteProgramFlash(uint32 a, uint32 v, uint32 mask) {
    if (g_trace) {
        fprintf(stderr, "flash write %08X = %08X & %08X at PC %08X\n", a, v, mask, m_cpu.PC);
    }
    const uint32 simm = 1 + ((a >> 23) & 1), offset = (a & 0x7FFFFF) >> 2;
    for (int chip = 0; chip < 4; chip++) {
        const int shift = (3 - chip) * 8;
        if (mask & (0xFFu << shift)) {
            m_flash[simm][chip].Write(offset, static_cast<uint8>(v >> shift));
        }
    }
    UpdateFlashPlain();
}

// The graphics SIMMs, through a bank of 4 MB: a pair of chips, the first giving bits 24-31 and 8-15, the second
// 16-23 and 0-7, each at two addresses in turn
uint32 Machine::ReadGfxFlash(uint32 a, uint32 mask) {
    if (m_gfxFlashBank < 0) {
        return 0xFFFFFFFF;
    }
    uint32 offset = (a - 0x04200000u) >> 2;
    if (m_gfxFlashBank & 1) {
        offset += 0x200000 / 4;
    }
    const int simm = 3 + m_gfxFlashBank / 8, chip = (m_gfxFlashBank % 8) & ~1;
    if (simm > 6 || m_rom.chips[simm][chip].empty() || m_rom.chips[simm][chip + 1].empty()) {
        return 0xFFFFFFFF;
    }
    const std::vector<uint8> &c0 = m_rom.chips[simm][chip], &c1 = m_rom.chips[simm][chip + 1];
    const uint32 at = offset * 2;
    auto byte = [&](int n, uint32 address) {
        const std::vector<uint8> &data = n ? c1 : c0;
        return m_flash[simm][chip + n].Read(address, address < data.size() ? data[address] : 0xFF);
    };
    return (static_cast<uint32>(byte(0, at)) << 24) | (static_cast<uint32>(byte(1, at)) << 16) | (static_cast<uint32>(byte(0, at + 1)) << 8) |
           byte(1, at + 1);
}

void Machine::WriteGfxFlash(uint32 a, uint32 v, uint32 mask) {
    if (g_trace) {
        fprintf(stderr, "gfx flash write %08X = %08X & %08X bank %d at PC %08X\n", a, v, mask, m_gfxFlashBank, m_cpu.PC);
    }
    if (m_gfxFlashBank < 0) {
        return;
    }
    uint32 offset = (a - 0x04200000u) >> 2;
    if (m_gfxFlashBank & 1) {
        offset += 0x200000 / 4;
    }
    const int simm = 3 + m_gfxFlashBank / 8, chip = (m_gfxFlashBank % 8) & ~1;
    if (simm > 6) {
        return;
    }
    const uint32 at = offset * 2;
    if (mask & 0xFF000000u) {
        m_flash[simm][chip].Write(at, static_cast<uint8>(v >> 24));
    }
    if (mask & 0x00FF0000u) {
        m_flash[simm][chip + 1].Write(at, static_cast<uint8>(v >> 16));
    }
    if (mask & 0x0000FF00u) {
        m_flash[simm][chip].Write(at + 1, static_cast<uint8>(v >> 8));
    }
    if (mask & 0x000000FFu) {
        m_flash[simm][chip + 1].Write(at + 1, static_cast<uint8>(v));
    }
}

// --- the cipher, as the CPU's instruction reads and its DMA see it ---
uint16 Machine::FetchOpcode(uint32 a) {
    if (a < 0x80000) {
        return static_cast<uint16>(m_rom.bios[a >> 2] >> ((2 - (a & 2)) * 8));
    }
    if (a >= 0x06000000u && a < 0x07000000u) {
        return static_cast<uint16>(m_rom.Opcodes()[(a & 0xFFFFFF) >> 2] >> ((2 - (a & 2)) * 8));
    }
    return BusRead16(a);
}

uint16 Machine::CacheOpcode(const uint8 *cache, uint32 offset) {
    const uint32 word = Get32(cache + (offset & ~3u)) ^ CipherMask(0xC0000000u + (offset & ~3u), m_rom.key);
    return static_cast<uint16>(word >> ((2 - (offset & 2)) * 8));
}

bool Machine::IsEnciphered(uint32 a) const {
    return a < 0x80000 || (a >= 0x06000000u && a < 0x07000000u && !m_rom.key.none && m_rom.programEnciphered);
}
uint32 Machine::RawRead32(uint32 a) {
    if (a < 0x80000) {
        return m_rom.biosRaw[a >> 2];
    }
    return ReadProgramFlash(a & ~3u, 0xFFFFFFFF, false);
}
uint16 Machine::RawRead16(uint32 a) {
    return static_cast<uint16>(RawRead32(a & ~3u) >> ((2 - (a & 2)) * 8));
}
uint8 Machine::RawRead8(uint32 a) {
    return static_cast<uint8>(RawRead32(a & ~3u) >> ((3 - (a & 3)) * 8));
}

} // namespace cps3recomp::machine
