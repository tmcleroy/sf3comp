// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "native_sound.h"

#include "state.h"

#include <algorithm>
#include <cmath>

namespace cps3recomp::native {

Mixer::Mixer(double frameRate)
    : m_perFrame(kRate / frameRate) {
    m_carry = m_perFrame;
    NextFrame();
}

// The next frame's length in samples (the rate is not a whole number of samples a frame: what is left over is carried)
void Mixer::NextFrame() {
    m_frameSamples = static_cast<size_t>(m_carry);
    m_carry -= static_cast<double>(m_frameSamples);
}

void Mixer::Apply(const SoundEvent &e) {
    Playing &p = m_voices[e.voice & 15];
    switch (e.kind) {
    case SoundEvent::kKeyOn:
    case SoundEvent::kKeyOff:
    case SoundEvent::kKeys: {
        const uint16_t keys = e.kind == SoundEvent::kKeyOn ? (m_keys | e.keys) : e.kind == SoundEvent::kKeyOff ? (m_keys & ~e.keys) : e.keys;
        for (int i = 0; i < 16; i++) {
            if ((keys & (1 << i)) && !(m_keys & (1 << i))) {
                m_voices[i].position = m_voices[i].fraction = 0; // (keyed on: from its start)
            }
        }
        m_keys = keys;
        break;
    }
    case SoundEvent::kSample:
        p.settings.start = e.start;
        p.settings.loop = e.loop;
        p.settings.end = e.end;
        p.settings.looping = e.looping;
        break;
    case SoundEvent::kLooping:
        p.settings.looping = e.looping;
        break;
    case SoundEvent::kPitch:
        p.settings.step = e.step;
        break;
    case SoundEvent::kVolume:
        p.settings.left = e.left;
        p.settings.right = e.right;
        break;
    case SoundEvent::kVoice:
        p.settings = {e.start, e.loop, e.end, e.looping, e.step, e.left, e.right};
        break;
    }
}

// The voices keyed on played from where the frame's sound is up to `samples`
void Mixer::MixTo(size_t samples, const uint8_t *data, size_t size, const std::array<bool, 16> &silenced, bool byVoice) {
    samples = std::min(samples, m_frameSamples);
    const size_t from = m_mix.size() / 2;
    if (samples <= from) {
        return;
    }
    m_mix.resize(samples * 2, 0);
    for (auto &v : m_voiceMix) {
        if (byVoice) {
            v.resize(samples * 2, 0);
        }
    }
    for (int i = 0; i < 16; i++) {
        if (!(m_keys & (1 << i))) {
            continue;
        }
        Playing &p = m_voices[i];
        const Voice &s = p.settings;
        const bool heard = !silenced[i];
        int32_t *own = byVoice ? m_voiceMix[i].data() : nullptr;
        uint32_t pos = p.position, frac = p.fraction;
        for (size_t j = from; j < samples; j++) {
            pos += frac >> 12;
            frac &= 0xFFF;
            if (s.start + pos >= s.end) {
                if (s.looping) {
                    pos = pos + s.loop - s.end;
                } else {
                    break; // (the sample's end: the voice stays keyed on, silent)
                }
            }
            if (heard) {
                // (the stream has each pair's two bytes swapped)
                const size_t at = (s.start + pos) ^ 1;
                const int32_t sample = at < size ? static_cast<int8_t>(data[at]) : 0;
                const int32_t left = sample * s.left / 256, right = sample * s.right / 256;
                m_mix[j * 2] += left;
                m_mix[j * 2 + 1] += right;
                if (own) {
                    own[j * 2] += left;
                    own[j * 2 + 1] += right;
                }
            }
            frac += s.step;
        }
        p.position = pos;
        p.fraction = frac;
    }
}

void Mixer::EndFrame(const uint8_t *samples, size_t size, const std::array<bool, 16> &silenced, bool byVoice) {
    m_mix.clear();
    for (auto &v : m_voiceMix) {
        v.clear();
    }
    for (const SoundEvent &e : m_events) {
        MixTo(static_cast<size_t>(e.at * static_cast<double>(m_frameSamples)), samples, size, silenced, byVoice);
        Apply(e);
    }
    m_events.clear();
    MixTo(m_frameSamples, samples, size, silenced, byVoice);
    m_carry += m_perFrame;
    NextFrame();
}

void Mixer::Output(std::vector<int16_t> &out, float gain) const {
    for (int32_t s : m_mix) {
        out.push_back(static_cast<int16_t>(std::clamp(static_cast<int32_t>(std::lround(s * gain)), -32768, 32767)));
    }
}

void Mixer::Serialize(Archive &ar) {
    ar.Mark(0x4E534E44); // NSND
    ar.Pod(m_voices);
    ar.Pod(m_keys);
    ar.Vec(m_events);
    ar.Pod(m_carry);
    ar.Pod(m_frameSamples);
}

} // namespace cps3recomp::native
