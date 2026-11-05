// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    native.textures.Reset();
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
// (debugging aid, CPS3RECOMP_VBLANK_LOG=FILE: at each vertical blank's start, the frame and a CRC-32 of main RAM,
// zlib's, to compare with a reference's RAM at its vertical blanks)
namespace {
    uint32 Crc32(const uint8 *data, size_t size) {
        static uint32 table[256];
        if (!table[1]) {
            for (uint32 n = 0; n < 256; n++) {
                uint32 c = n;
                for (int k = 0; k < 8; k++) {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                table[n] = c;
            }
        }
        uint32 crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < size; i++) {
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }
} // namespace

void Machine::RunFrame() {
    static FILE *vblankLog = getenv("CPS3RECOMP_VBLANK_LOG") ? fopen(getenv("CPS3RECOMP_VBLANK_LOG"), "w") : nullptr;
    if (vblankLog) {
        // (and CPS3RECOMP_VBLANK_WATCH=ADDRESS: the long there)
        static const uint32 watch = getenv("CPS3RECOMP_VBLANK_WATCH") ? static_cast<uint32>(strtoul(getenv("CPS3RECOMP_VBLANK_WATCH"), nullptr, 16)) & 0x7FFFC : 0;
        const uint8 *w = mainRam + watch;
        // (CPS3RECOMP_VBLANK_SKIP=START-END,...: those offsets of main RAM left out of the CRC, as zeros)
        static std::vector<std::pair<uint32, uint32>> skips = [] {
            std::vector<std::pair<uint32, uint32>> v;
            for (const char *e = getenv("CPS3RECOMP_VBLANK_SKIP"); e && *e;) {
                char *end;
                const uint32 lo = static_cast<uint32>(strtoul(e, &end, 16));
                const uint32 hi = *end == '-' ? static_cast<uint32>(strtoul(end + 1, &end, 16)) : lo + 1;
                v.push_back({lo, hi});
                e = *end == ',' ? end + 1 : nullptr;
            }
            return v;
        }();
        static std::vector<uint8> copy;
        copy.assign(mainRam, mainRam + 0x80000);
        for (const auto &[lo, hi] : skips) {
            for (uint32 i = lo; i < hi && i < 0x80000; i++) {
                copy[i] = 0;
            }
        }
        fprintf(vblankLog, "%llu %08x %08X %08x %llu %llu\n", static_cast<unsigned long long>(m_frame), Crc32(copy.data(), 0x80000), m_cpu.PC,
                (uint32(w[0]) << 24) | (uint32(w[1]) << 16) | (uint32(w[2]) << 8) | w[3], static_cast<unsigned long long>(m_cpu.m_bus.charged),
                static_cast<unsigned long long>(m_cpu.m_bus.busCycles));
        fflush(vblankLog);
        // (and CPS3RECOMP_VBLANK_DUMP=FRAME:FILE: main RAM at that frame's vertical blank, as it is)
        static const char *dump = getenv("CPS3RECOMP_VBLANK_DUMP");
        if (dump && strtoull(dump, nullptr, 10) == m_frame) {
            if (const char *colon = strchr(dump, ':')) {
                if (FILE *f = fopen(colon + 1, "wb")) {
                    fwrite(mainRam, 1, 0x80000, f);
                    fclose(f);
                }
            }
        }
    }
    // (the bus's timing forced on for the timing harness, CPS3RECOMP_BUS_TIMING=1; otherwise the arcade_timing
    // feature's, set by the port before the frame)
    static const bool busTiming = getenv("CPS3RECOMP_BUS_TIMING") != nullptr;
    if (busTiming) {
        m_cpu.m_busTiming = true;
    }
    // (debugging aid, CPS3RECOMP_BENCH=FILE: the timing harness's bench program put in main RAM at 0x02070000 and run
    // from 0x02070010 with the interrupts masked, at the first frame; its status writes (0x0207FFF0) are logged with
    // the time by the CPU: sh2.cpp)
    if (m_frame == 0) {
        if (const char *bench = getenv("CPS3RECOMP_BENCH")) {
            if (FILE *f = fopen(bench, "rb")) {
                const size_t got = fread(mainRam + 0x70000, 1, 0xF000, f);
                fclose(f);
                fprintf(stderr, "bench: %zu bytes\n", got);
                m_cpu.PC = 0x02070010;
                m_cpu.R[15] = 0x0207F000;
                m_cpu.SR.u32 |= 0xF0;
            }
        }
    }
    // (debugging aid, CPS3RECOMP_POKE=FRAME:ADDRESS:WORD,...: a word of main RAM set at the start of those frames, to
    // keep a run in step with the timing harness's reference past a value the timing decides)
    if (const char *pokes = getenv("CPS3RECOMP_POKE")) {
        for (const char *e = pokes; e && *e;) {
            char *end;
            const uint64_t frame = strtoull(e, &end, 10);
            const uint32 address = static_cast<uint32>(strtoul(end + 1, &end, 16)) & 0x7FFFE;
            const uint32 value = static_cast<uint32>(strtoul(end + 1, &end, 16));
            if (frame == m_frame) {
                mainRam[address] = static_cast<uint8>(value >> 8);
                mainRam[address + 1] = static_cast<uint8>(value);
            }
            e = *end == ',' ? end + 1 : nullptr;
        }
    }
    m_frameStart = m_now;
    m_irqLines |= 1u << 12;
    UpdateInterrupts();
    RunUntil(m_frameStart + static_cast<uint64>(kLines) * kCyclesPerLine);
    if (nativeCheck) {
        NativeCompare();
    }
    if (native::On()) {
        NativeFrameEnd();
    }
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
        NoteListWrite(a);
        return;
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        TouchChar(((m_charBank & 7) << 20) | (a & 0xFFFFF));
        m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFF)] = v;
        NoteCharWrite(((m_charBank & 7) << 20) | (a & 0xFFFFF), v, 1);
        return;
    }
    const uint32 shift = (3 - (a & 3)) * 8;
    WriteMasked(a & ~3u, static_cast<uint32>(v) << shift, 0xFFu << shift);
}
void Machine::BusWrite16(uint32 a, uint16 v) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        Put16(&m_spriteRam[a & 0x7FFFE], v);
        NoteListWrite(a);
        return;
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        TouchChar(((m_charBank & 7) << 20) | (a & 0xFFFFF));
        Put16(&m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFE)], v);
        NoteCharWrite(((m_charBank & 7) << 20) | (a & 0xFFFFE), v, 2);
        return;
    }
    const uint32 shift = (2 - (a & 2)) * 8;
    WriteMasked(a & ~3u, static_cast<uint32>(v) << shift, 0xFFFFu << shift);
}
void Machine::BusWrite32(uint32 a, uint32 v) {
    if ((a & 0xFFF80000u) == 0x04000000u) {
        Put32(&m_spriteRam[a & 0x7FFFC], v);
        NoteListWrite(a);
        return;
    }
    if ((a & 0xFFF00000u) == 0x04100000u) {
        TouchChar(((m_charBank & 7) << 20) | (a & 0xFFFFF));
        Put32(&m_charRam[((m_charBank & 7) << 20) | (a & 0xFFFFC)], v);
        NoteCharWrite(((m_charBank & 7) << 20) | (a & 0xFFFFC), v, 4);
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
        // (debugging aid, CPS3RECOMP_COLOUR_WATCH=index: the CPU's writes of that colour, with who made them)
        static const long watchColour = getenv("CPS3RECOMP_COLOUR_WATCH") ? strtol(getenv("CPS3RECOMP_COLOUR_WATCH"), nullptr, 16) : -1;
        if (static_cast<long>(index) == watchColour || static_cast<long>(index + 1) == watchColour) {
            fprintf(stderr, "colour %05lX = %08X mask %08X at PC %08X PR %08X frame %llu\n", watchColour, v, mask, m_cpu.PC, m_cpu.PR,
                    static_cast<unsigned long long>(m_frame));
        }
        if (mask & 0xFFFF0000u) {
            uint32 c = m_colourRam[index];
            Combine(c, v >> 16, mask >> 16);
            SetColour(index, static_cast<uint16>(c), 0);
            if (nativeCheck) {
                NoteColourWrite(index);
            }
        }
        if (mask & 0xFFFFu) {
            uint32 c = m_colourRam[index + 1];
            Combine(c, v, mask & 0xFFFF);
            SetColour(index + 1, static_cast<uint16>(c), 0);
            if (nativeCheck) {
                NoteColourWrite(index + 1);
            }
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
            if (nativeCheck) {
                NoteRegisterWrite();
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
    }
    if (word < 0x80 && nativeCheck) {
        NoteRegisterWrite();
    } else if (word == 0x80) {
        if (mask & 0xFFFF) {
            const uint16 previous = m_spriteDma;
            m_spriteDma = static_cast<uint16>((m_spriteDma & ~mask) | (v & mask));
            // (the games write 8, 9, 8, 9: the list is taken at a 9 followed by an 8)
            if (!(m_dmaStatus & 1) && (m_spriteDma & 9) == 8 && (previous & 9) == 9) {
                const bool compare = nativeCheck && m_cpu.PC >= 0x06000000u;
                if (compare) {
                    NativeCompareLists();
                }
                SpriteDma();
                if (compare) {
                    NativeCompareObjects();
                } else if (NativeTaking() && !nativeCheck) {
                    // (the frame's objects made from what was handed over, as the check makes them)
                    native::FlattenObjects(native.lists, native.registers, m_spriteRam.data(), native.objects);
                }
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
    // (debugging aid, CPS3RECOMP_PALETTE_LOG=1: each palette DMA's source in the graphics, destination and length)
    static const bool palLog = getenv("CPS3RECOMP_PALETTE_LOG") != nullptr;
    if (palLog) {
        fprintf(stderr, "palette dma frame %llu: from %06X to %05X, %u colours\n", static_cast<unsigned long long>(m_frame), source, m_palDmaDest, length);
    }
    for (uint32 i = 0; i < length; i++) {
        // (in 32-bit words the two colours of each word are swapped on both sides, as MAME has it)
        const size_t at = static_cast<size_t>(((source >> 1) + i) ^ 1) * 2;
        uint16 colour = at + 1 < g.size() ? static_cast<uint16>((g[at] << 8) | g[at + 1]) : 0;
        if (paletteColour) {
            colour = paletteColour(static_cast<uint32>((source >> 1) + i), (m_palDmaDest + i) ^ 1, colour);
        }
        SetColour((m_palDmaDest + i) ^ 1, colour, m_palDmaFade);
    }
    if (nativeCheck) {
        // (the transfer as the board's registers have it, in the native interface's terms)
        native::PaletteEvent e{native::PaletteEvent::kTransfer, {source >> 1, m_palDmaDest, length, m_palDmaFade}};
        m_boardPaletteEvents.push_back(e);
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

// --- the native interface's check (video/native.h) ---
namespace {
    struct NativeTotals {
        uint64 frames = 0, different = 0, events = 0, eventsDifferent = 0, transfers = 0, colours = 0, registersDifferent = 0;
        uint64 lists = 0, listsDifferent = 0, listsBoot = 0, heads = 0, texturesDifferent = 0, objectDmas = 0, objects = 0, objectsDifferent = 0;
        unsigned reports = 8;
        bool used = false;
        ~NativeTotals() {
            if (used) {
                fprintf(stderr,
                        "cps3recomp: native palette: %llu frames compared, %llu different; %llu events (%llu transfers, %llu colours written), "
                        "%llu frames with the events different\n",
                        static_cast<unsigned long long>(frames), static_cast<unsigned long long>(different), static_cast<unsigned long long>(events),
                        static_cast<unsigned long long>(transfers), static_cast<unsigned long long>(colours),
                        static_cast<unsigned long long>(eventsDifferent));
                fprintf(stderr, "cps3recomp: native frame registers: %llu frames compared, %llu different\n", static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(registersDifferent));
                fprintf(stderr, "cps3recomp: native object lists: %llu object DMAs compared (%llu heads), %llu different, %llu of the boot ROM's passed on\n",
                        static_cast<unsigned long long>(lists), static_cast<unsigned long long>(heads), static_cast<unsigned long long>(listsDifferent),
                        static_cast<unsigned long long>(listsBoot));
                fprintf(stderr, "cps3recomp: native objects: %llu object DMAs compared (%llu objects), %llu different\n",
                        static_cast<unsigned long long>(objectDmas), static_cast<unsigned long long>(objects), static_cast<unsigned long long>(objectsDifferent));
                fprintf(stderr, "cps3recomp: native textures: %llu frames compared, %llu different\n", static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(texturesDifferent));
            }
        }
    } g_nativeTotals;
} // namespace

void Machine::NativePaletteTransfer(const native::PaletteTransfer &t) {
    if (!NativeTaking()) {
        return;
    }
    native.palette.Transfer(t, m_rom.graphics.data(), m_rom.graphics.size(), paletteColour);
    if (nativeCheck) {
        native.paletteEvents.push_back({native::PaletteEvent::kTransfer, t});
    }
}

void Machine::NativeColour(uint32 index, uint16 value) {
    if (!NativeTaking()) {
        return;
    }
    native.palette.Set(index, value);
    if (nativeCheck) {
        native.paletteEvents.push_back({native::PaletteEvent::kColour, {value, index & 0x1FFFF, 1, 0}});
    }
}

// A colour the CPU wrote to colour RAM, for the check: the boot ROM's own passed on to the native palette (it is not the
// game's code); the game's noted, to be met by what the rewrites hand over
void Machine::NoteColourWrite(uint32 index) {
    const uint16 value = m_colourRam[index & 0x1FFFF];
    if (m_cpu.PC < 0x80000) {
        native.palette.Set(index, value);
        return;
    }
    m_boardPaletteEvents.push_back({native::PaletteEvent::kColour, {value, index & 0x1FFFF, 1, 0}});
}

native::FrameRegisters Machine::BoardRegisters() const {
    native::FrameRegisters r;
    for (int i = 0; i < 8; i++) {
        r.scroll[i][0] = static_cast<uint16>(m_videoRegs.globalScroll[i] >> 16);
        r.scroll[i][1] = static_cast<uint16>(m_videoRegs.globalScroll[i]);
    }
    for (int n = 0; n < 4; n++) {
        for (int k = 0; k < 8; k++) {
            const uint32 word = m_videoRegs.tilemap[n * 4 + k / 2];
            r.tilemap[n][k] = static_cast<uint16>(k & 1 ? word : word >> 16);
        }
    }
    for (int h = 0; h < 16; h++) {
        r.screen[h] = static_cast<uint16>(h & 1 ? m_videoRegs.crtc[h / 2] : m_videoRegs.crtc[h / 2] >> 16);
    }
    std::copy(m_ssRegs.begin(), m_ssRegs.end(), r.text);
    return r;
}

// The frame's end: whether the interface holds the frame as the board does (machine.h, NativeInSync). In the checks it
// is never made again from the board (the check compares the two instead), and holds the frame once the game's code
// hands it over.
bool Machine::SyncChecked() {
    static const bool on = getenv("CPS3RECOMP_NATIVE_SYNC_CHECK") != nullptr;
    return on;
}

void Machine::NativeFrameEnd() {
    const bool handed = m_nativeHanded == 3 && NativeTaking();
    m_nativeHanded = 0;
    if (!handed) {
        m_nativeInSync = false;
        return;
    }
    if (!m_nativeInSync) {
        // (made again only for a frame a GPU may draw: nativeWanted; run-ahead's frames, say, are not)
        if (!nativeCheck) {
            SyncNative();
        }
        m_nativeInSync = true;
    }
    m_nativeStamps = m_charStamp;
    m_nativeStampsKnown = true;
    // (debugging aid, CPS3RECOMP_NATIVE_SYNC_CHECK=1 outside the checks: the interface, in step, against the board, every
    // frame: the colours, the textures, the registers and the frame's objects)
    static const bool syncCheck = getenv("CPS3RECOMP_NATIVE_SYNC_CHECK") || getenv("CPS3RECOMP_NATIVE_SYNC_VERIFY"); // (VERIFY: compared only)
    if (syncCheck && !nativeCheck) {
        static struct Totals {
            uint64_t frames = 0, different = 0;
            ~Totals() {
                fprintf(stderr, "cps3recomp: native in step: %llu frames compared, %llu different\n", static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(different));
            }
        } totals;
        totals.frames++;
        bool same = memcmp(native.palette.Colours(), m_colourRam.data(), native::Palette::kColours * 2) == 0 &&
                    memcmp(native.textures.Pixels(), m_charRam.data(), native::Textures::kSize) == 0 && native.registers == BoardRegisters() &&
                    native.objects.size() == m_spriteCount;
        for (uint32 i = 0; same && i < m_spriteCount; i++) {
            const native::Object &o = native.objects[i];
            same = o.group == m_spriteGroup[i] && std::equal(o.words, o.words + 4, &m_spriteList[static_cast<size_t>(i) * 4]);
        }
        if (!same && totals.different++ < 6) {
            fprintf(stderr, "native in step, frame %llu: different from the board\n", static_cast<unsigned long long>(m_frame));
        }
    }
}

// The interface made from the board's memories and registers: the colours (colour RAM), the textures (character RAM's
// pages changed since the two last held the same), the frame's registers, the lists' heads and the frame's objects
void Machine::SyncNative() {
    native.palette.SetAll(m_colourRam.data());
    for (int page = 0; page < kCharPages; page++) {
        if (!m_nativeStampsKnown || m_nativeStamps[page] != m_charStamp[page]) {
            native.textures.PutPage(page, m_charRam.data() + static_cast<size_t>(page) * 0x10000);
        }
    }
    native.textures.SetTable(m_tableAddress);
    native.registers = BoardRegisters();
    native.lists = BoardLists();
    native.objects.resize(m_spriteCount);
    for (uint32 i = 0; i < m_spriteCount; i++) {
        native::Object &o = native.objects[i];
        std::copy_n(&m_spriteList[static_cast<size_t>(i) * 4], 4, o.words);
        o.group = m_spriteGroup[i];
    }
}

// A frame register written by the boot ROM (not the game's code): passed on to the interface as the board has it now.
// (Only the boot ROM writes these before the game's code starts; after, the rewrites alone.)
void Machine::NoteRegisterWrite() {
    if (m_cpu.PC < 0x80000) {
        native.registers = BoardRegisters();
    }
}

std::vector<native::ListHead> Machine::BoardLists() const {
    std::vector<native::ListHead> heads;
    for (uint32 i = 0; i < 0x2000 / 4; i += 4) {
        const uint32 head = SpriteRam(i);
        if (head & 0x80000000u) {
            break;
        }
        heads.push_back({head, SpriteRam(i + 1), SpriteRam(i + 2)});
    }
    return heads;
}

// An object DMA the game starts: the heads it takes against those the game handed over (the boot ROM's, written since
// the game last handed some over, passed on)
void Machine::NativeCompareLists() {
    NativeTotals &t = g_nativeTotals;
    t.used = true;
    std::vector<native::ListHead> board = BoardLists();
    if (m_bootLists) {
        native.lists = board;
        m_bootLists = false;
        t.listsBoot++;
        return;
    }
    t.lists++;
    t.heads += board.size();
    if (board != native.lists) {
        t.listsDifferent++;
        size_t i = 0;
        while (i < board.size() && i < native.lists.size() && board[i] == native.lists[i]) {
            i++;
        }
        if (t.reports) {
            t.reports--;
            const native::ListHead none{}, &a = i < board.size() ? board[i] : none, &b = i < native.lists.size() ? native.lists[i] : none;
            fprintf(stderr, "native lists, frame %llu: %zu heads (board) / %zu (native), the first to differ no. %zu: %08X %08X %08X / %08X %08X %08X\n",
                    static_cast<unsigned long long>(m_frame), board.size(), native.lists.size(), i, a.head, a.position, a.settings, b.head, b.position,
                    b.settings);
        }
    }
}

// ... and the objects the board's DMA made against those the native interface makes from what was handed over
void Machine::NativeCompareObjects() {
    NativeTotals &t = g_nativeTotals;
    native::FlattenObjects(native.lists, native.registers, m_spriteRam.data(), native.objects);
    t.objectDmas++;
    t.objects += m_spriteCount;
    bool same = native.objects.size() == m_spriteCount;
    size_t i = 0;
    for (; same && i < m_spriteCount; i++) {
        const native::Object &o = native.objects[i];
        same = o.group == m_spriteGroup[i] && o.words[0] == m_spriteList[i * 4] && o.words[1] == m_spriteList[i * 4 + 1] &&
               o.words[2] == m_spriteList[i * 4 + 2] && o.words[3] == m_spriteList[i * 4 + 3];
    }
    if (!same) {
        t.objectsDifferent++;
        if (t.reports) {
            t.reports--;
            fprintf(stderr, "native objects, frame %llu: %u objects (board) / %zu (native), the first to differ about no. %zu\n",
                    static_cast<unsigned long long>(m_frame), m_spriteCount, native.objects.size(), i ? i - 1 : 0);
        }
    }
}

void Machine::NativeCompare() {
    NativeTotals &t = g_nativeTotals;
    t.used = true;
    t.frames++;
    for (const native::PaletteEvent &e : m_boardPaletteEvents) {
        t.events++;
        (e.kind == native::PaletteEvent::kTransfer ? t.transfers : t.colours)++;
    }
    const auto say = [&](const char *what) {
        if (t.reports) {
            t.reports--;
            fprintf(stderr, "native palette, frame %llu: %s\n", static_cast<unsigned long long>(m_frame), what);
        }
    };
    char text[200];
    if (m_boardPaletteEvents != native.paletteEvents) {
        t.eventsDifferent++;
        size_t i = 0;
        while (i < m_boardPaletteEvents.size() && i < native.paletteEvents.size() && m_boardPaletteEvents[i] == native.paletteEvents[i]) {
            i++;
        }
        const auto show = [](const std::vector<native::PaletteEvent> &v, size_t at) {
            static char out[96];
            if (at >= v.size()) {
                snprintf(out, sizeof(out), "none");
            } else {
                const native::PaletteEvent &e = v[at];
                snprintf(out, sizeof(out), "%s %X %X %X %X", e.kind == native::PaletteEvent::kTransfer ? "transfer" : "colour", e.transfer.source,
                         e.transfer.dest, e.transfer.length, e.transfer.fade);
            }
            return std::string(out);
        };
        snprintf(text, sizeof(text), "events %zu (board) / %zu (native), the first to differ no. %zu: %s / %s", m_boardPaletteEvents.size(),
                 native.paletteEvents.size(), i, show(m_boardPaletteEvents, i).c_str(), show(native.paletteEvents, i).c_str());
        say(text);
    }
    m_boardPaletteEvents.clear();
    native.paletteEvents.clear();
    {
        const native::FrameRegisters board = BoardRegisters();
        if (!(board == native.registers)) {
            t.registersDifferent++;
            const uint8 *a = reinterpret_cast<const uint8 *>(&board), *b = reinterpret_cast<const uint8 *>(&native.registers);
            size_t at = 0;
            while (at < sizeof(board) && a[at] == b[at]) {
                at++;
            }
            snprintf(text, sizeof(text), "frame registers different, the first at byte %zu of FrameRegisters: %02X (board) / %02X (native)", at,
                     a[at], b[at]);
            say(text);
        }
    }
    if (memcmp(native.textures.Pixels(), m_charRam.data(), native::Textures::kSize) != 0) {
        t.texturesDifferent++;
        uint32 at = 0;
        while (native.textures.Pixels()[at] == m_charRam[at]) {
            at++;
        }
        snprintf(text, sizeof(text), "textures different, the first at %06X: %02X (board) / %02X (native)", at, m_charRam[at], native.textures.Pixels()[at]);
        say(text);
    }
    const uint16 *colours = native.palette.Colours();
    uint32 first = 0, count = 0;
    for (uint32 i = 0; i < native::Palette::kColours; i++) {
        if (colours[i] != m_colourRam[i]) {
            first = count++ ? first : i;
        }
    }
    if (count) {
        t.different++;
        snprintf(text, sizeof(text), "%u colours different, the first %05X: %04X (board) / %04X (native)", count, first, m_colourRam[first], colours[first]);
        say(text);
    }
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
