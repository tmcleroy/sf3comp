// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's sound driver, the part that sets the sound chip's voices, written again as C++ (rewrites: runtime/lift.h):
// the native sound's first piece (cps3recomp/docs/NATIVE.md, games/sfiii3/NATIVE.md). Each routine writes the chip's
// registers as the game's code does (the driver reads them back, so they stay its own memory) and hands the same
// change to the native sound (sound/native_sound.h) as an event: what a voice plays, whether it loops on, its pitch,
// its volume, the keys. With CPS3RECOMP_NATIVE_CHECK=1 the board compares, every frame, the native mixer's samples
// with pcm.cpp's (games/sfiii3/checksound.sh).
//
// The driver runs at the end of the vertical blank (0x0613A408). It keeps two sets of sixteen channels of 116 bytes,
// the music's (0x02078D0C) and the effects' (0x0207944C), both on the chip's sixteen voices: an effect on a voice
// takes it from the music while it plays. For each channel 0x0613B3CC steps it on a frame: its timers, a new note
// (the voice keyed off, its sample set from the note's entry of three longs, start, loop and end, and keyed on again),
// its envelope (attack to a level, decay to a level, sustain, release: phases 1-4), a slide of its pitch, the
// vibrato and tremolo (two waves up and down), an effect's pan moving to where it is going; then its volume for each
// side (0x0613B226, from the envelope's level, the channel's volume and the pan) and its pitch (0x0613B1D8, from the
// note, the slide, the vibrato, the fine tune and the transpose) written to the voice. 0x06139316 sets the driver up
// (and silences the chip), 0x0613A334 stops every channel, 0x0613A272 lets the music's go on after a pause
// (0x0613A21C, which keeps the keys: neither is named anywhere in the program, so Resume is left unregistered).
//
// A channel's bytes, as far as these routines use them (offsets):
//   8 its instrument (a pointer: +1 the pan, 0xFF the channel's own at 109; +2 the volume; +11 the release's rate)
//   12 its note's sample (a pointer to three longs: start, loop, end, as the chip counts them, the bank added to the
//      high halves)
//   16, 20, 24 the pitch: the next note's, the note's, the pitch now (sliding to the note's at 70 a frame)
//   28, 32 the vibrato's and the tremolo's place on their waves; 36, 40 the note's two timers (counted down by the
//      frame's ticks)
//   60, 62, 64 the vibrato's and tremolo's depths and their speed; 66 the envelope's level; 70 the slide's speed;
//   74, 76 the attack's level and rate; 78, 80 the decay's level and rate; 82 the sustain's rate; 84 the release's
//   94 its flags (0x80 off, 0x40 releasing, 0x20 the first timer running, 0x02 sliding, 0x01 the waves restart)
//   95, 96 fine tune and transpose; 102, 105, 107 a new note (with a new sample: 102; the music's again: 107);
//   103, 104 the second timer on; 108 the envelope's phase; 109 the pan; 110 the waves' directions; 111 the effect's
#include "lift.h"
#include "machine.h"

#include <cstdlib>
#include <iterator>
#include <unordered_map>

namespace cps3recomp {

namespace {
    // --- the board's ---
    constexpr uint32_t kVoiceRegs = 0x240E0000; // 16 voices of 8 longs (words: +2 cleared, +4/+6 start, +10 loops,
                                                // +12 pitch, +14/+18 loop, +20/+22 end, +24/+26 end again, +28/+30 volumes)
    constexpr uint32_t kKeys = 0x240E0200;      // word: a bit a voice
    constexpr uint32_t kChipMode = 0x240E0210;  // long: set at start-up
    constexpr uint32_t kOtherChip = 0x25000C02; // word: a device the board model does not have, set at start-up

    // --- the game's ---
    constexpr uint32_t kMusic = 0x02078D0C;    // the music's channels, 116 bytes each
    constexpr uint32_t kEffects = 0x0207944C;  // the effects'
    constexpr uint32_t kChannel = 116;
    constexpr uint32_t kTick = 0x02007EE8;     // long: the music's tick (with kMusicTick)
    constexpr uint32_t kMusicTick = 0x02079C14;
    constexpr uint32_t kEffectTicks = 0x02079C18; // longs, by voice
    constexpr uint32_t kReleaseRates = 0x02078CEC; // long: the table of the release's rates (words)
    constexpr uint32_t kSampleBank = 0x02079C0E;   // word: added to the samples' high halves
    constexpr uint32_t kTranspose = 0x02079C0C;    // word: the music's
    constexpr uint32_t kMusicVolume = 0x02079C58;  // byte
    constexpr uint32_t kStereo = 0x02079C5B;       // byte
    constexpr uint32_t kPans = 0x02079B8C;         // the effects' pans, by voice: now, where to, the step, the state (words)
    constexpr uint32_t kDriverFlags = 0x02079C8C;  // byte: 1 on, 2 the music's, 4 paused
    constexpr uint32_t kKeptKeys = 0x02079C78;     // word: the keys as a pause found them
    constexpr uint32_t kKeptFlags = 0x02079C7C;    // bytes, by channel: the music's flags as a pause found them
    constexpr uint32_t kVolumeOf = 0x0613B226;     // r4 level, r5 the tremolo, r6 the music's volume, r7 the channel: r0
    constexpr uint32_t kStepOf = 0x0613B1D8;       // r4 pitch: r0 the chip's step
    constexpr uint32_t kSteps = 0x0660CFF8;        // the pitch's steps: words, 0xC00 of them (an octave), in the program
    constexpr uint32_t kSoundMode = 0x02079C10;    // long: the chip's mode word as set up (bit 16: the volumes curved)
    constexpr uint32_t kMasterFade = 0x02078D04;   // long: the music's fade (its bits 8-23)

    uint32_t Signed(int32_t v) {
        return static_cast<uint32_t>(v);
    }
    // 32-bit sums and differences as the CPU makes them (wrapping)
    int32_t Add(int32_t a, int32_t b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
    }
    int32_t Sub(int32_t a, int32_t b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
    }

    // The memory the driver's routines below work on, and where their events go. Live (a rewrite): the guest's, and the
    // events handed over now. Dry (a hook, with arcade timing, before the game's own code runs the routine): the
    // guest's read through a copy of its own that keeps every write (nothing changed, nothing charged), the writes to
    // the sound chip counted; each event then waits for the chip write it follows to be made by the game's code
    // (Machine::NativeSoundAfter), so it lands at that write's time, as the board's mixer takes it.
    class Driver {
    public:
        Driver(Guest &g, bool dry)
            : m_g(g)
            , m_dry(dry) {}
        uint8_t Read8(uint32_t a) {
            if (m_dry) {
                const auto it = m_kept.find(a & kMask);
                if (it != m_kept.end()) {
                    return it->second;
                }
            }
            return m_g.Read8(a);
        }
        uint16_t Read16(uint32_t a) {
            if (m_dry && (Kept(a) || Kept(a + 1))) {
                return static_cast<uint16_t>((Read8(a) << 8) | Read8(a + 1));
            }
            return m_g.Read16(a);
        }
        uint32_t Read32(uint32_t a) {
            if (m_dry && (Kept(a) || Kept(a + 1) || Kept(a + 2) || Kept(a + 3))) {
                return (static_cast<uint32_t>(Read16(a)) << 16) | Read16(a + 2);
            }
            return m_g.Read32(a);
        }
        void Write8(uint32_t a, uint8_t v) {
            if (!m_dry) {
                m_g.Write8(a, v);
                return;
            }
            Wrote(a);
            m_kept[a & kMask] = v;
        }
        void Write16(uint32_t a, uint16_t v) {
            if (!m_dry) {
                m_g.Write16(a, v);
                return;
            }
            Wrote(a);
            m_kept[a & kMask] = static_cast<uint8_t>(v >> 8);
            m_kept[(a + 1) & kMask] = static_cast<uint8_t>(v);
        }
        void Write32(uint32_t a, uint32_t v) {
            if (!m_dry) {
                m_g.Write32(a, v);
                return;
            }
            Wrote(a);
            for (uint32_t i = 0; i < 4; i++) {
                m_kept[(a + i) & kMask] = static_cast<uint8_t>(v >> (24 - 8 * i));
            }
        }
        // Live: the guest, for the game's own routines called as the driver calls them (their time and what they leave
        // on the stack and in the registers, as the game's code has them)
        bool Dry() const {
            return m_dry;
        }
        Guest &Live() {
            return m_g;
        }
        void Hand(const native::SoundEvent &e) {
            if (m_dry) {
                m_g.Board().NativeSoundAfter(e, m_chipWrites);
            } else {
                m_g.Board().NativeSound(e);
            }
        }

    private:
        static constexpr uint32_t kMask = 0x1FFFFFFF; // (the CPU's mirrors of the same memory)
        Guest &m_g;
        bool m_dry;
        std::unordered_map<uint32_t, uint8_t> m_kept;
        uint32_t m_chipWrites = 0;
        bool Kept(uint32_t a) const {
            return m_kept.count(a & kMask) != 0;
        }
        void Wrote(uint32_t a) {
            const uint32_t at = a & kMask;
            if (at >= 0x040E0000 && at < 0x040E0300) {
                m_chipWrites++;
            }
        }
    };

    void Hand(Driver &g, const native::SoundEvent &e) {
        g.Hand(e);
    }
    void HandKeys(Driver &g, native::SoundEvent::Kind kind, uint16_t keys) {
        native::SoundEvent e;
        e.kind = kind;
        e.keys = keys;
        Hand(g, e);
    }

    // A voice's register written, and written again until it reads back as written (0x0613BC9A)
    void Put(Driver &g, uint32_t at, uint32_t value) {
        g.Write16(at, static_cast<uint16_t>(value));
        while (g.Read16(at) != static_cast<uint16_t>(value)) {
            g.Write16(at, static_cast<uint16_t>(value));
        }
    }
    void KeyOff(Driver &g, uint16_t bit) {
        g.Write16(kKeys, static_cast<uint16_t>(g.Read16(kKeys) & ~bit));
        HandKeys(g, native::SoundEvent::kKeyOff, bit);
    }
    // A sample's place as the chip has it (the bank added to the high half, as the driver writes it) in the sample
    // stream's terms
    uint32_t Place(uint32_t value, uint32_t bank) {
        return ((value & 0xFFFF) | ((((value >> 16) + bank) & 0xFFFF) << 16)) - 0x400000;
    }

    // One of a set's channels
    struct Channel {
        Driver &g;
        uint32_t at;
        uint8_t B(uint32_t o) {
            return g.Read8(at + o);
        }
        int8_t SB(uint32_t o) {
            return static_cast<int8_t>(g.Read8(at + o));
        }
        uint16_t W(uint32_t o) {
            return g.Read16(at + o);
        }
        int32_t L(uint32_t o) {
            return static_cast<int32_t>(g.Read32(at + o));
        }
        void SetB(uint32_t o, uint32_t v) {
            g.Write8(at + o, static_cast<uint8_t>(v));
        }
        void SetW(uint32_t o, uint32_t v) {
            g.Write16(at + o, static_cast<uint16_t>(v));
        }
        void SetL(uint32_t o, int32_t v) {
            g.Write32(at + o, static_cast<uint32_t>(v));
        }
    };

    // A new note's sample set on a voice: keyed off, its registers written (twice over in part, as the driver does),
    // and whether it loops on (a loop point short of its end)
    void SetSample(Driver &g, Channel &c, uint32_t regs, uint8_t voice, uint16_t bit) {
        KeyOff(g, bit);
        Put(g, regs + 2, 0);
        const uint32_t bank = g.Read16(kSampleBank);
        const uint32_t entry = static_cast<uint32_t>(c.L(12));
        const uint32_t start = g.Read32(entry), loop = g.Read32(entry + 4), end = g.Read32(entry + 8);
        Put(g, regs + 4, start);
        Put(g, regs + 6, (start >> 16) + bank);
        Put(g, regs + 14, loop);
        Put(g, regs + 18, (loop >> 16) + bank);
        Put(g, regs + 20, end);
        Put(g, regs + 24, end);
        Put(g, regs + 22, (end >> 16) + bank);
        Put(g, regs + 26, (end >> 16) + bank);
        Put(g, regs + 2, 0);
        Put(g, regs + 4, start);
        Put(g, regs + 6, (start >> 16) + bank);
        const bool looping = end != loop;
        Put(g, regs + 10, looping ? 1 : 0);
        native::SoundEvent e;
        e.kind = native::SoundEvent::kSample;
        e.voice = voice;
        e.start = Place(start, bank);
        e.loop = Place(loop, bank);
        e.end = Place(end, bank);
        e.looping = looping;
        Hand(g, e);
    }
    void StopLooping(Driver &g, uint32_t regs, uint8_t voice) {
        Put(g, regs + 10, 0);
        native::SoundEvent e;
        e.kind = native::SoundEvent::kLooping;
        e.voice = voice;
        e.looping = false;
        Hand(g, e);
    }

    // 0613B226, the volume for a side, in native form (for a dry run, which calls no game code; checksound's arcade-timing
    // runs compare its events with the board's): from the level (r4: the envelope's, scaled by the pan), the tremolo
    // (r5), the music's volume (r6, a signed byte: -128 silent) and the channel (r7: its volume at 98 times 100's, 88's,
    // the instrument's and the driver's, and for an effect the effects' volume), clamped to 15 bits; then, with the
    // chip's mode bit 16 on, the music faded (a channel of the music's set: 111 is 0), else bent into the chip's curve
    // (four straight pieces up to 0x7FFF, doubled)
    int32_t Scaled(int32_t a, uint32_t b) { // (a byte of the game's, 0-127 about 64, times a value, over 64)
        return static_cast<int32_t>(static_cast<uint32_t>(a) * ((b + 64) & 127)) >> 6;
    }
    uint16_t NativeVolumeOf(Driver &g, uint32_t level, int32_t tremolo, int32_t musicVolume, uint32_t channel) {
        Channel c{g, channel};
        const uint32_t depth = c.B(100);
        int32_t v = static_cast<int32_t>((depth ? depth + 1 : 0) * c.B(98)) << 1;
        v = Scaled(v, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(c.W(88)))));
        v = Scaled(v, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(g.Read8(static_cast<uint32_t>(c.L(8)) + 2)))));
        v = Scaled(v, g.Read8(0x02079C5A));
        if (c.SB(111) == 0) {
            v = Scaled(v, g.Read8(0x02079C59));
        }
        v = Add(v, 1);
        const int32_t lowered = static_cast<int32_t>(static_cast<uint32_t>(v) * (level & 0xFFFF)) >> 15;
        int32_t out;
        if (static_cast<int8_t>(musicVolume) == -128) {
            out = 0;
        } else {
            const uint32_t l = static_cast<uint32_t>(lowered) & 0xFFFF;
            out = static_cast<int16_t>((static_cast<uint32_t>(tremolo) * l) >> 16);
            out = Add(out, static_cast<int8_t>(musicVolume) * 256);
            out = Add(out, static_cast<int32_t>(l));
        }
        const uint32_t clamped = out > 0x7FFF ? 0x7FFF : out < 0 ? 0 : static_cast<uint32_t>(out);
        if (g.Read8(kSoundMode + 1) & 1) {
            if (c.B(111)) {
                return static_cast<uint16_t>(clamped);
            }
            const uint32_t fade = (g.Read32(kMasterFade) >> 8) & 0xFFFF;
            return static_cast<uint16_t>(static_cast<int32_t>((clamped & 0xFFFF) * fade) >> 7);
        }
        uint32_t curved;
        if (clamped <= 0x1000) {
            curved = clamped << 2;
        } else {
            if (clamped <= 0x2000) {
                curved = (clamped << 1) + 0x2000;
            } else if (clamped <= 0x4000) {
                curved = (clamped >> 1) + 0x5000;
            } else {
                curved = (clamped >> 2) + 0x6000;
            }
        }
        if (static_cast<int32_t>(curved) >= 0x8000) {
            curved = 0x7FFF;
        }
        return static_cast<uint16_t>(curved << 1);
    }
    // ... as the driver calls it: live, the game's routine itself
    uint16_t VolumeOf(Driver &g, uint32_t level, int32_t tremolo, int32_t musicVolume, uint32_t channel) {
        if (g.Dry()) {
            return NativeVolumeOf(g, level, tremolo, musicVolume, channel);
        }
        Guest &live = g.Live();
        live.R(4) = level;
        live.R(5) = Signed(tremolo);
        live.R(6) = Signed(musicVolume);
        live.R(7) = channel;
        live.Call(kVolumeOf);
        return static_cast<uint16_t>(live.R(0));
    }

    // 0613B1D8, the chip's step for a pitch (in native form for a dry run; live, the game's routine): the pitch in 0xC00
    // steps an octave, the step of its place in the octave from the table (plus one) shifted by the octave, less one
    uint16_t StepOf(Driver &g, int32_t pitch) {
        if (!g.Dry()) {
            g.Live().R(4) = Signed(pitch);
            g.Live().Call(kStepOf);
            return static_cast<uint16_t>(g.Live().R(0));
        }
        int32_t octave = 0;
        while (pitch >= 0xC00) {
            pitch -= 0xC00;
            octave++;
        }
        while (pitch < 0) {
            pitch += 0xC00;
            octave--;
        }
        uint32_t step = Signed(static_cast<int16_t>(g.Read16(kSteps + static_cast<uint32_t>(pitch) * 2)) + 1);
        while (static_cast<int8_t>(octave) > 0) {
            step <<= 1;
            octave--;
        }
        while (static_cast<int8_t>(octave) < 0) {
            step = (step & 0xFFFF) >> 1;
            octave++;
        }
        return static_cast<uint16_t>(step - 1);
    }

    // 0613B3CC: a channel stepped on a frame and its voice set: r4 the channel, r5 its voice, r6 1 for the music's set
    // (0 the effects'). The music's channel has its voice unless an effect is playing on it. Gives the time it takes (live,
    // besides that of the game's routines it calls).
    uint64_t StepChannel(Driver &g, uint32_t channel, uint8_t voice, bool music) {
        Channel c{g, channel};
        const uint32_t regs = kVoiceRegs + (static_cast<uint32_t>(voice) << 5);
        const uint16_t bit = static_cast<uint16_t>(voice < 32 ? 1u << voice : 0);
        bool owns = true;
        if (music && !(g.Read8(kEffects + 94 + static_cast<uint32_t>(static_cast<int16_t>(voice * kChannel))) & 0x80)) {
            owns = false; // (an effect has the voice)
        }
        if (c.B(94) & 0x80) { // (the channel is off)
            if (music && owns) {
                KeyOff(g, bit);
            }
            return 40;
        }

        // The timers, counted down by the frame's ticks
        const int32_t tick = static_cast<int32_t>(music ? g.Read32(kTick) + g.Read32(kMusicTick) : g.Read32(kEffectTicks + voice * 4u));
        c.SetL(40, Sub(c.L(40), tick));
        if (c.B(103)) {
            c.SetL(36, Sub(c.L(36), tick));
        }
        if (c.L(40) <= 0) {
            c.SetB(94, c.B(94) & 0xDF);
        }
        if (c.L(36) <= 0 && c.B(103)) {
            // (the second timer out: the note let go)
            c.SetB(103, 0);
            c.SetB(104, 0);
            if (owns) {
                StopLooping(g, regs, voice);
            }
            if (c.B(108) != 4) {
                c.SetB(108, 4);
                const uint32_t rates = g.Read32(kReleaseRates);
                const uint32_t rate = g.Read8(static_cast<uint32_t>(c.L(8)) + 11);
                const uint32_t scaled = (static_cast<uint32_t>(c.W(66)) * (static_cast<uint32_t>(g.Read16(rates + rate * 2)) + 1)) >> 16;
                c.SetW(84, scaled ? scaled : 1);
            }
        }

        // A new note
        bool keyOn = false;
        if (c.B(105)) {
            c.SetB(105, 0);
            c.SetL(24, c.L(20));
            c.SetL(20, c.L(16));
            if (c.B(102)) {
                c.SetB(102, 0);
                c.SetB(104, 1);
                c.SetW(66, 0);
                if (owns) {
                    SetSample(g, c, regs, voice, bit);
                }
                if (c.W(70)) {
                    c.SetB(94, c.B(94) | 2);
                } else {
                    c.SetB(94, c.B(94) & 0xFD);
                    c.SetL(24, c.L(20));
                }
                c.SetB(108, 1);
                if (c.B(94) & 1) {
                    c.SetB(110, c.B(110) & 0xFC);
                    c.SetL(28, 0);
                    c.SetL(32, 0);
                }
            } else {
                if (c.W(70)) {
                    c.SetB(94, c.B(94) | 2);
                } else {
                    c.SetB(94, c.B(94) & 0xFD);
                    c.SetL(24, c.L(20));
                }
                c.SetB(108, 3);
            }
            keyOn = true;
        }
        if (music && c.B(107) && owns) {
            // (the music's voice given back after an effect: its sample again)
            c.SetB(107, 0);
            SetSample(g, c, regs, voice, bit);
            keyOn = true;
        }

        // Releasing: off once the envelope is down
        if (c.B(94) & 0x40) {
            if (c.W(66) == 0) {
                c.SetB(94, c.B(94) | 0x80);
                c.SetB(111, 0);
                return 120;
            }
            c.SetB(108, 4);
            if (owns) {
                StopLooping(g, regs, voice);
            }
        }

        // The pitch sliding to the note's
        if ((c.B(94) & 2) && c.L(20) != c.L(24)) {
            const int32_t target = c.L(20);
            const int32_t speed = c.W(70);
            if (c.L(24) > target) {
                const int32_t now = Sub(c.L(24), speed);
                c.SetL(24, now > target ? now : target);
            } else {
                const int32_t now = Add(c.L(24), speed);
                c.SetL(24, now >= target ? target : now);
            }
        }

        // The envelope
        switch (c.B(108)) {
        case 1: { // attack
            const uint32_t level = static_cast<uint32_t>(c.W(66)) + c.W(76), top = c.W(74);
            if (level >= top) {
                c.SetW(66, top);
                c.SetB(108, 2);
            } else {
                c.SetW(66, level);
            }
            break;
        }
        case 2: { // decay
            const uint32_t floor = c.W(78), rate = c.W(80);
            if (floor + rate >= c.W(66)) {
                c.SetW(66, floor);
                c.SetB(108, 3);
            } else {
                c.SetW(66, c.W(66) - rate);
            }
            break;
        }
        case 3: { // sustain
            const uint32_t rate = c.W(82), level = c.W(66);
            if (level > rate) {
                c.SetW(66, level - rate);
            } else {
                c.SetW(66, 0);
                c.SetB(108, 0);
            }
            break;
        }
        case 4: { // release
            const uint32_t rate = c.W(84), level = c.W(66);
            if (level > rate) {
                c.SetW(66, level - rate);
            } else {
                c.SetW(66, 0);
                c.SetB(108, 0);
                if (music && owns) {
                    KeyOff(g, bit);
                }
            }
            break;
        }
        default:
            break;
        }

        // The vibrato (28) and the tremolo (32): each up and down a wave of its depth, at the speed
        if (c.W(64)) {
            if (c.W(60)) {
                const int32_t step = static_cast<int32_t>(static_cast<uint32_t>(c.W(64)) * c.W(60));
                const int32_t depth = static_cast<int32_t>(static_cast<uint32_t>(c.W(60)) << 16);
                if (c.B(110) & 1) {
                    if (Sub(step, depth) >= c.L(28)) {
                        c.SetL(28, Sub(0, depth));
                        c.SetB(110, c.B(110) & 0xFE);
                    } else {
                        c.SetL(28, Sub(c.L(28), step));
                    }
                } else if (Sub(depth, step) > c.L(28)) {
                    c.SetL(28, Add(c.L(28), step));
                } else {
                    c.SetL(28, depth);
                    c.SetB(110, c.B(110) | 1);
                }
            }
            if (c.W(62)) {
                const int32_t depth = c.W(62);
                const int32_t step = static_cast<int16_t>((static_cast<uint32_t>(c.W(62)) * c.W(64)) >> 16);
                if (c.B(110) & 2) {
                    if (step - depth >= c.L(32)) {
                        c.SetL(32, -depth);
                        c.SetB(110, c.B(110) & 0xFD);
                    } else {
                        c.SetL(32, Sub(c.L(32), step));
                    }
                } else if (Sub(depth, step) > c.L(32)) {
                    c.SetL(32, Add(c.L(32), step));
                } else {
                    c.SetL(32, depth);
                    c.SetB(110, c.B(110) | 2);
                }
            }
        } else {
            c.SetL(28, 0);
            c.SetL(32, 0);
        }

        // An effect's pan moving to where it is going (at its end: the effect told, if it asked)
        const uint32_t pan = kPans + voice * 8u;
        if (!music) {
            const int16_t state = static_cast<int16_t>(g.Read16(pan + 6));
            if (state != -1 && state != 0) {
                const int32_t moved = static_cast<int16_t>(g.Read16(pan)) + static_cast<int16_t>(g.Read16(pan + 4));
                const int32_t to = static_cast<int16_t>(g.Read16(pan + 2));
                if (!g.Dry()) {
                    g.Live().R(7) = kEffects; // (as the routine leaves it)
                }
                const bool there = static_cast<int16_t>(g.Read16(pan + 4)) >= 0 ? moved >= to : to >= moved;
                if (there) {
                    g.Write16(pan, g.Read16(pan + 2));
                    if (static_cast<int16_t>(g.Read16(pan + 6)) == 1) {
                        const uint32_t flags = kEffects + static_cast<uint32_t>(static_cast<int16_t>(voice * kChannel)) + 94;
                        g.Write8(flags, g.Read8(flags) | 0x40);
                    }
                    g.Write16(pan + 6, 0);
                } else {
                    g.Write16(pan, static_cast<uint16_t>(moved));
                }
            }
        }
        if (!owns) {
            return 160;
        }

        // The volumes: from the envelope's level, the tremolo and (the music's) the music's volume; with stereo on,
        // the side away from the pan turned down by it. (Worked out both, then written in the driver's order.)
        const uint32_t ch = c.at;
        const int32_t musicVolume = music ? static_cast<int8_t>(g.Read8(kMusicVolume)) : 0;
        const uint32_t level = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(c.W(66))));
        uint16_t left, right;
        bool rightFirst;
        if (g.Read8(kStereo)) {
            uint32_t at;
            if (!music && static_cast<int16_t>(g.Read16(pan + 6)) != -1) {
                at = Signed(static_cast<int16_t>(g.Read16(pan)) >> 8);
            } else {
                const uint8_t own = g.Read8(static_cast<uint32_t>(c.L(8)) + 1);
                at = own == 0xFF ? c.B(109) : own;
            }
            at &= 0xFF;
            if (at < 64) { // (to the left: the right turned down)
                right = VolumeOf(g, (static_cast<uint32_t>(c.W(66)) * at) >> 6, c.L(32), musicVolume, ch);
                left = VolumeOf(g, level, c.L(32), musicVolume, ch);
                rightFirst = true;
            } else {
                const uint32_t away = (127 - at) & 0xFFFF;
                left = VolumeOf(g, Signed(static_cast<int32_t>(static_cast<uint32_t>(c.W(66)) * away) >> 6), c.L(32), musicVolume, ch);
                right = VolumeOf(g, level, c.L(32), musicVolume, ch);
                rightFirst = false;
            }
        } else if (music) {
            right = VolumeOf(g, level, c.L(32), musicVolume, ch);
            left = VolumeOf(g, level, c.L(32), musicVolume, ch);
            rightFirst = true;
        } else {
            left = VolumeOf(g, level, c.L(32), musicVolume, ch);
            right = VolumeOf(g, level, c.L(32), musicVolume, ch);
            rightFirst = false;
        }
        const auto side = [&](bool isLeft) {
            const uint16_t v = isLeft ? left : right;
            g.Write16(regs + (isLeft ? 28 : 30), v);
            native::SoundEvent e;
            e.kind = native::SoundEvent::kVolume;
            e.voice = voice;
            e.sides = isLeft ? 1 : 2;
            (isLeft ? e.left : e.right) = static_cast<int16_t>(v);
            Hand(g, e);
        };
        side(!rightFirst);
        side(rightFirst);
        native::SoundEvent e;

        // The pitch: the note's (sliding) with the vibrato, the fine tune (95, an eighth of a semitone's 192 steps?)
        // and the transpose (96, from 64), and the music's transpose
        int32_t pitch = Add(static_cast<int16_t>(static_cast<uint32_t>(c.L(28)) >> 16), c.L(24));
        pitch = Add(pitch, (c.SB(95) * 3 * 1024) >> 7);
        pitch = Add(pitch, ((c.SB(96) - 64) * 256) >> 6);
        if (music) {
            pitch = Add(pitch, static_cast<int16_t>(g.Read16(kTranspose)));
        }
        const uint16_t step = StepOf(g, pitch);
        g.Write16(regs + 12, step);
        e = {};
        e.kind = native::SoundEvent::kPitch;
        e.voice = voice;
        e.step = step;
        Hand(g, e);

        if (keyOn) {
            g.Write16(kKeys, static_cast<uint16_t>(g.Read16(kKeys) | bit));
            HandKeys(g, native::SoundEvent::kKeyOn, bit);
        }
        return 300;
    }

    // 06139316: the driver set up: r4 its table of settings, r5 and r6 two more tables, r7 bit 0 a mode bit of the
    // chip; on the stack, the chip's other mode byte (long 0's low byte) and whether it is stereo (long 1's). The chip
    // silenced: every voice's registers cleared and the keys off.
    void SetUp(Driver &g, uint32_t sp, uint32_t table, uint32_t second, uint32_t third, uint32_t mode) {
        g.Write32(0x02078CE8, 0x0660CBF8); // (the driver's tables in the program: the release's rates and four more)
        g.Write32(kReleaseRates, 0x0660CC78);
        g.Write32(0x02078CF0, 0x0660CCF8);
        g.Write32(0x02078CF4, 0x0660CDF8);
        g.Write32(0x02078CF8, 0x0660CEF8);
        g.Write32(0x02078CFC, second);
        g.Write32(0x02078D00, third);
        g.Write8(kStereo, g.Read8(sp + 7));
        // (21 words of settings for the other device, each given time to take)
        for (uint32_t i = 0; i < 42; i += 2) {
            g.Write16(kOtherChip, g.Read16(0x0660C758 + i));
        }
        for (uint8_t v = 0; v < 16; v++) {
            for (uint32_t i = 0; i < 32; i += 2) {
                g.Write16(kVoiceRegs + v * 32u + i, 0);
            }
            native::SoundEvent e;
            e.kind = native::SoundEvent::kVoice; // (every register 0)
            e.voice = v;
            e.start = e.loop = e.end = native::kCleared;
            Hand(g, e);
        }
        g.Write16(0x02079C10, static_cast<uint16_t>((static_cast<uint32_t>(g.Read8(sp + 3)) << 8) | (mode & 1) | 0x22));
        g.Write32(kChipMode, g.Read32(0x02079C10));
        g.Write16(kKeys, 0);
        HandKeys(g, native::SoundEvent::kKeys, 0);
        g.Write32(0x02079C6C, table);
        g.Write16(0x02079C7A, static_cast<uint16_t>((static_cast<uint32_t>(g.Read8(table)) << 8) + g.Read8(table + 1)));
        const uint8_t volume = g.Read8(table + 2);
        g.Write8(0x02079C59, volume);
        g.Write8(0x02079C8E, volume);
        g.Write8(0x02079C5A, g.Read8(table + 3));
        g.Write8(kDriverFlags, 1);
        g.Write16(kTranspose, 0);
        g.Write8(kMusicVolume, 0);
        g.Write32(0x02078D04, 0);
        g.Write32(0x02078D08, 0);
        g.Write32(kTick, 0);
        for (uint32_t i = 0; i < 16; i++) {
            g.Write8(kMusic + i * kChannel + 94, 0xC0);
            g.Write8(kKeptFlags + i, 0xC0);
        }
        for (uint32_t i = 0; i < 16; i++) {
            g.Write8(kEffects + i * kChannel + 94, 0xC0);
        }
        for (uint32_t i = 0; i < 16; i++) {
            g.Write8(0x02079C5C + i, 0);
        }
    }

    // 0613A334: every channel of both sets stopped, the keys off
    void StopAll(Driver &g) {
        for (uint32_t i = 0; i < 16; i++) {
            g.Write8(kMusic + i * kChannel + 94, 0xC0);
            g.Write8(kEffects + i * kChannel + 94, 0xC0);
            g.Write8(kEffects + i * kChannel + 111, 0);
        }
        g.Write8(kDriverFlags, 1);
        g.Write16(kKeys, 0);
        HandKeys(g, native::SoundEvent::kKeys, 0);
        g.Write16(kKeptKeys, 0);
    }

    // 0613A272: after a pause of the music, its channels as the pause found them and their voices keyed on again
    [[maybe_unused]] void Resume(Driver &g) {
        const uint8_t flags = g.Read8(kDriverFlags);
        if ((flags & 2) && (flags & 4)) {
            for (uint32_t i = 0; i < 16; i++) {
                g.Write8(kMusic + i * kChannel + 94, g.Read8(kKeptFlags + i));
            }
            g.Write8(kDriverFlags, g.Read8(kDriverFlags) & 0xFB);
            const uint16_t kept = g.Read16(kKeptKeys);
            g.Write16(kKeys, static_cast<uint16_t>(g.Read16(kKeys) | kept));
            HandKeys(g, native::SoundEvent::kKeyOn, kept);
        }
    }

    // The rewrites (live), and the hooks that work out the same events when the game's own code runs (dry: with arcade
    // timing; nothing changed, nothing charged)
    bool g_channelRewritten = false, g_setUpRewritten = false, g_stopRewritten = false;
    bool ChannelRewrite(Guest &g) {
        Driver d(g, false);
        const uint32_t sp = g.R(15);
        g.R(15) = sp - 44; // (the routine's frame: seven registers, pr, macl and 8 bytes; what it calls goes below)
        const uint64_t cycles = StepChannel(d, g.R(4), static_cast<uint8_t>(g.R(5)), (g.R(6) & 0xFF) != 0);
        g.R(15) = sp;
        g.Return(cycles);
        return true;
    }
    void ChannelHook(Guest &g) {
        if (g_channelRewritten && g.RewritesRun()) {
            return;
        }
        Driver d(g, true);
        StepChannel(d, g.R(4), static_cast<uint8_t>(g.R(5)), (g.R(6) & 0xFF) != 0);
    }
    bool SetUpRewrite(Guest &g) {
        Driver d(g, false);
        SetUp(d, g.R(15), g.R(4), g.R(5), g.R(6), g.R(7));
        g.Return(17000); // (the other device's waits: 21 of 257 turns of a loop)
        return true;
    }
    void SetUpHook(Guest &g) {
        if (g_setUpRewritten && g.RewritesRun()) {
            return;
        }
        Driver d(g, true);
        SetUp(d, g.R(15), g.R(4), g.R(5), g.R(6), g.R(7));
    }
    bool StopAllRewrite(Guest &g) {
        Driver d(g, false);
        StopAll(d);
        g.Return(250);
        return true;
    }
    void StopAllHook(Guest &g) {
        if (g_stopRewritten && g.RewritesRun()) {
            return;
        }
        Driver d(g, true);
        StopAll(d);
    }

    const Lift kLifts[] = {
        {"SoundSetUp", 0x06139316, 0x22E, 0x90E9CE5D71582AD6ull, SetUpRewrite, true, 0x100FF},
        {"SoundStopAll", 0x0613A334, 0xD4, 0x5F8C7600B189AA26ull, StopAllRewrite, true, 0x100FF},
        // {"SoundResume", 0x0613A272, 0x86, 0xC2CCA2D3E2FE96B4ull, Resume, true, 0x100FF}, (not reached by any check yet: left to the game\'s code; nothing in the program names it or its pause, 0x0613A21C)
        {"SoundChannel", 0x0613B3CC, 0x8E0, 0x7781CBAA0B34ED7Dull, ChannelRewrite, true, 0x1007F},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp
bool GameLiftWanted(const char *name);                    // lifts.cpp

void RegisterSoundDriver() {
    RegisterGameLifts(kLifts, std::size(kLifts));
    g_setUpRewritten = GameLiftWanted("SoundSetUp");
    g_stopRewritten = GameLiftWanted("SoundStopAll");
    g_channelRewritten = GameLiftWanted("SoundChannel");
    // (the hooks, by name as the lifts: SFIII3_LIFTS_ONLY leaves out those it does not name; SFIII3_SOUND_NO_HOOKS
    // leaves them all out, for checksound.sh to see that they change nothing)
    if (getenv("SFIII3_SOUND_NO_HOOKS")) {
        return;
    }
    if (GameLiftWanted("SoundSetUpEvents")) {
        AddHook("SoundSetUpEvents", 0x06139316, 0x22E, 0x90E9CE5D71582AD6ull, SetUpHook);
    }
    if (GameLiftWanted("SoundStopAllEvents")) {
        AddHook("SoundStopAllEvents", 0x0613A334, 0xD4, 0x5F8C7600B189AA26ull, StopAllHook);
    }
    if (GameLiftWanted("SoundChannelEvents")) {
        AddHook("SoundChannelEvents", 0x0613B3CC, 0x8E0, 0x7781CBAA0B34ED7Dull, ChannelHook);
    }
}

} // namespace cps3recomp
