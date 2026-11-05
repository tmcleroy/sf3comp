// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The native sound interface (docs/NATIVE.md, "Sound"): what the game's rewritten sound driver hands over, in place of
// what it writes to the sound chip's registers, and a mixer that plays it. The board model (machine.cpp's registers,
// pcm.cpp) stays as the reference: with CPS3RECOMP_NATIVE_CHECK=1 the board keeps this beside its own chip, and every
// frame the mixer's samples are compared with pcm.cpp's, voice by voice and sample by sample (level (c)).
//
// The chip has sixteen voices, each playing 8-bit samples from the graphics SIMMs' stream: from a start to an end, on
// from a loop point if it loops, at a pitch (a step through the sample of 4.12 fixed point a sample out), at a volume for
// each side; and a key word, a bit a voice, that starts and stops them (a voice keyed on starts at its start again). The
// driver sets a voice while it is keyed off and changes its pitch and volume while it plays, so its events are those:
// what a voice plays (kSample), whether it loops on (kLooping), its pitch, its volume, and the keys. Each carries when
// in the frame it was handed over (Machine::FramePosition, as pcm.cpp mixes up to each write), and the mixer plays the
// frame's events at their times.
//
// (Two of a voice's eight registers the driver writes have no effect the board model knows: the first's low half,
// written 0 as a voice is set, and the seventh, written the sample's end again; the chip's mode word at 040E0210, set
// at start-up. They are not part of the interface.)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cps3recomp {
class Archive;
}

namespace cps3recomp::native {

// What the native sound is handed: one change to the voices
struct SoundEvent {
    enum Kind : uint8_t {
        kKeyOn,   // the voices in `keys` keyed on (those already on go on as they are)
        kKeyOff,  // the voices in `keys` keyed off
        kKeys,    // the key word set as a whole to `keys`
        kSample,  // what `voice` plays: start, loop, end (byte places in the sample stream) and whether it loops on
        kLooping, // whether `voice` loops on at its end
        kPitch,   // `voice`'s step through its sample
        kVolume,  // `voice`'s volume, left and right (those of `sides`: 1 left, 2 right)
        kVoice,   // all of `voice` as given (the boot ROM's writes, passed on as the board has them)
    } kind = kKeys;
    uint8_t voice = 0;
    uint16_t keys = 0;
    uint32_t start = 0, loop = 0, end = 0;
    bool looping = false;
    uint16_t step = 0;
    int16_t left = 0, right = 0;
    uint8_t sides = 3;
    double at = 0; // when in the frame: 0 its start, 1 its end (Machine::FramePosition)
};

// The place in the sample stream of a chip's address 0 (its registers cleared, as at reset): the chip counts its
// addresses 0x400000 bytes further on
constexpr uint32_t kCleared = 0u - 0x400000u;

// A voice as the driver has set it (at first: its registers cleared)
struct Voice {
    uint32_t start = kCleared, loop = kCleared, end = kCleared;
    bool looping = false;
    uint16_t step = 0;
    int16_t left = 0, right = 0;
    bool operator==(const Voice &) const = default;
};

// The sixteen voices played from the events
class Mixer {
public:
    // 42.954545 MHz / 3 / 384, as the chip runs
    static constexpr double kRate = 42954545.0 / 3.0 / 384.0;
    explicit Mixer(double frameRate);
    void Add(const SoundEvent &e) {
        m_events.push_back(e);
    }
    // The frame mixed: the voices played with the frame's events at their times, into Mix() (left and right in turn, at
    // the chip's scale: 16 bits, not clamped), and into VoiceMix() voice by voice when `byVoice`. `silenced`: voices not
    // heard (they play on). The samples are the graphics SIMMs' stream (each pair's two bytes swapped, as the board has
    // them).
    void EndFrame(const uint8_t *samples, size_t size, const std::array<bool, 16> &silenced, bool byVoice);
    const std::vector<int32_t> &Mix() const {
        return m_mix;
    }
    const std::vector<int32_t> &VoiceMix(int v) const {
        return m_voiceMix[v];
    }
    // The frame's sound as 16-bit samples, added to `out`
    void Output(std::vector<int16_t> &out, float gain) const;
    const Voice &Settings(int v) const {
        return m_voices[v].settings;
    }
    uint16_t Keys() const {
        return m_keys;
    }
    // Where a voice is in its sample, and the fraction of a sample (12 bits)
    uint32_t Position(int v) const {
        return m_voices[v & 15].position;
    }
    uint32_t Fraction(int v) const {
        return m_voices[v & 15].fraction;
    }
    void Serialize(Archive &ar);
    // Put back as a voice and the keys stand (a save state from before the native mixer: from the board's chip), and
    // the frame's place in the sample rate (pcm.cpp's, which keeps the same count)
    void SetVoice(int v, const Voice &settings, uint32_t position, uint32_t fraction) {
        m_voices[v & 15] = {settings, position, fraction};
    }
    void SetKeys(uint16_t keys) {
        m_keys = keys;
    }
    void SetFrame(double carry, size_t frameSamples) {
        m_events.clear();
        m_carry = carry;
        m_frameSamples = frameSamples;
    }

private:
    struct Playing {
        Voice settings;
        uint32_t position = 0, fraction = 0; // where in its sample, and the fraction of a sample (12 bits)
    };
    std::array<Playing, 16> m_voices{};
    uint16_t m_keys = 0;
    std::vector<SoundEvent> m_events; // the frame's so far
    std::vector<int32_t> m_mix;
    std::array<std::vector<int32_t>, 16> m_voiceMix;
    double m_perFrame, m_carry = 0;
    size_t m_frameSamples = 0;
    void Apply(const SoundEvent &e);
    void MixTo(size_t samples, const uint8_t *data, size_t size, const std::array<bool, 16> &silenced, bool byVoice);
    void NextFrame();
};

} // namespace cps3recomp::native
