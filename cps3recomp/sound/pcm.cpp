// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "pcm.h"

#include "machine.h"
#include "state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cps3recomp {

Pcm::Pcm(machine::Machine &m)
    : m_machine(m)
    , m_perFrame(kRate / machine::Machine::kFrameRate) {
    m.beforeSoundWrite = &Pcm::BeforeWrite;
    m.soundContext = this;
    m_carry = m_perFrame;
    m_frameSamples = static_cast<size_t>(m_carry);
    m_carry -= static_cast<double>(m_frameSamples);
}

void Pcm::BeforeWrite(void *self) {
    Pcm &pcm = *static_cast<Pcm *>(self);
    pcm.MixTo(static_cast<size_t>(pcm.m_machine.FramePosition() * static_cast<double>(pcm.m_frameSamples)));
}

// Mixes the voices from where the frame's sound is up to `samples` into it
void Pcm::MixTo(size_t samples) {
    samples = std::min(samples, m_frameSamples);
    const size_t from = m_mix.size() / 2;
    if (samples <= from) {
        return;
    }
    m_mix.resize(samples * 2, 0);
    const uint8_t *base = m_machine.Samples();
    const size_t size = m_machine.SamplesSize();
    for (int i = 0; i < 16; i++) {
        if (!(m_machine.keyOn & (1 << i))) {
            continue;
        }
        if (m_machine.silenced[i]) {
            // (not heard, but it plays on: its place in the sample moves as it would)
            machine::Machine::Voice &v = m_machine.voices[i];
            const uint32_t step = (v.regs[3] >> 16) & 0xFFFF;
            const uint32_t start = (((v.regs[1] >> 16) & 0xFFFF) | ((v.regs[1] << 16) & 0xFFFF0000u)) - 0x400000;
            const uint32_t end = (((v.regs[5] >> 16) & 0xFFFF) | ((v.regs[5] << 16) & 0xFFFF0000u)) - 0x400000;
            const uint32_t loop = ((v.regs[3] & 0xFFFF) | ((v.regs[4] << 16) & 0xFFFF0000u)) - 0x400000;
            for (size_t j = from; j < samples; j++) {
                v.position += v.fraction >> 12;
                v.fraction &= 0xFFF;
                if (start + v.position >= end) {
                    if (v.regs[2] & 1) {
                        v.position = v.position + loop - end;
                    } else {
                        break;
                    }
                }
                v.fraction += step;
            }
            continue;
        }
        machine::Machine::Voice &v = m_machine.voices[i];
        const uint32_t start = (((v.regs[1] >> 16) & 0xFFFF) | ((v.regs[1] << 16) & 0xFFFF0000u)) - 0x400000;
        const uint32_t end = (((v.regs[5] >> 16) & 0xFFFF) | ((v.regs[5] << 16) & 0xFFFF0000u)) - 0x400000;
        const uint32_t loop = ((v.regs[3] & 0xFFFF) | ((v.regs[4] << 16) & 0xFFFF0000u)) - 0x400000;
        const bool looping = v.regs[2] & 1;
        const uint32_t step = (v.regs[3] >> 16) & 0xFFFF;
        const int32_t right = static_cast<int16_t>(v.regs[7] & 0xFFFF), left = static_cast<int16_t>(v.regs[7] >> 16);
        uint32_t pos = v.position, frac = v.fraction;
        for (size_t j = from; j < samples; j++) {
            pos += frac >> 12;
            frac &= 0xFFF;
            if (start + pos >= end) {
                if (looping) {
                    pos = pos + loop - end;
                } else {
                    break; // (the sample's end: the voice stays keyed on, silent)
                }
            }
            // (the samples' bytes are in the graphics stream with each pair's two bytes swapped)
            const size_t at = (start + pos) ^ 1;
            const int32_t sample = at < size ? static_cast<int8_t>(base[at]) : 0;
            frac += step;
            m_mix[j * 2] += sample * left / 256;
            m_mix[j * 2 + 1] += sample * right / 256;
        }
        v.position = pos;
        v.fraction = frac;
    }
}

void Pcm::EndFrame(std::vector<int16_t> *out) {
    MixTo(m_frameSamples);
    if (out) {
        for (int32_t s : m_mix) {
            out->push_back(static_cast<int16_t>(std::clamp(static_cast<int32_t>(std::lround(s * gain)), -32768, 32767)));
        }
    }
    m_mix.clear();
    // (the next frame's length in samples: the rate is not a whole number of samples a frame)
    m_carry += m_perFrame;
    m_frameSamples = static_cast<size_t>(m_carry);
    m_carry -= static_cast<double>(m_frameSamples);
}

void Pcm::Serialize(Archive &ar) {
    ar.Pod(m_carry);
    ar.Pod(m_frameSamples);
}

void WriteWav(const char *path, const std::vector<int16_t> &samples, double rate) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; // (little-endian hosts)
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate) * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(bytes);
    fwrite(samples.data(), 2, samples.size(), f);
    fclose(f);
}

} // namespace cps3recomp
