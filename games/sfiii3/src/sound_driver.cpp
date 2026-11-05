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
// (0x0613A21C, which keeps the keys).
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

#include <iterator>

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

    void Hand(Guest &g, const native::SoundEvent &e) {
        g.Board().NativeSound(e);
    }
    void HandKeys(Guest &g, native::SoundEvent::Kind kind, uint16_t keys) {
        native::SoundEvent e;
        e.kind = kind;
        e.keys = keys;
        Hand(g, e);
    }

    // A voice's register written, and written again until it reads back as written (0x0613BC9A)
    void Put(Guest &g, uint32_t at, uint32_t value) {
        g.Write16(at, static_cast<uint16_t>(value));
        while (g.Read16(at) != static_cast<uint16_t>(value)) {
            g.Write16(at, static_cast<uint16_t>(value));
        }
    }
    void KeyOff(Guest &g, uint16_t bit) {
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
        Guest &g;
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
    void SetSample(Guest &g, Channel &c, uint32_t regs, uint8_t voice, uint16_t bit) {
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
    void StopLooping(Guest &g, uint32_t regs, uint8_t voice) {
        Put(g, regs + 10, 0);
        native::SoundEvent e;
        e.kind = native::SoundEvent::kLooping;
        e.voice = voice;
        e.looping = false;
        Hand(g, e);
    }

    // The game's volume for a side (0x0613B226), as the driver calls it
    uint16_t VolumeOf(Guest &g, uint32_t level, int32_t tremolo, int32_t musicVolume, uint32_t channel) {
        g.R(4) = level;
        g.R(5) = Signed(tremolo);
        g.R(6) = Signed(musicVolume);
        g.R(7) = channel;
        g.Call(kVolumeOf);
        return static_cast<uint16_t>(g.R(0));
    }

    // 0613B3CC: a channel stepped on a frame and its voice set: r4 the channel, r5 its voice, r6 1 for the music's set
    // (0 the effects'). The music's channel has its voice unless an effect is playing on it.
    bool StepChannel(Guest &g) {
        Channel c{g, g.R(4)};
        const uint8_t voice = static_cast<uint8_t>(g.R(5));
        const bool music = (g.R(6) & 0xFF) != 0;
        const uint32_t sp = g.R(15);
        g.R(15) = sp - 44; // (the routine's frame: seven registers, pr, macl and 8 bytes; what it calls goes below)
        const uint32_t regs = kVoiceRegs + (static_cast<uint32_t>(voice) << 5);
        const uint16_t bit = static_cast<uint16_t>(voice < 32 ? 1u << voice : 0);
        bool owns = true;
        if (music && !(g.Read8(kEffects + 94 + static_cast<uint32_t>(static_cast<int16_t>(voice * kChannel))) & 0x80)) {
            owns = false; // (an effect has the voice)
        }
        const auto done = [&](uint64_t cycles) {
            g.R(15) = sp;
            g.Return(cycles);
            return true;
        };
        if (c.B(94) & 0x80) { // (the channel is off)
            if (music && owns) {
                KeyOff(g, bit);
            }
            return done(40);
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
                return done(120);
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
                g.R(7) = kEffects;
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
            return done(160);
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
        if (rightFirst) {
            g.Write16(regs + 30, right);
            g.Write16(regs + 28, left);
        } else {
            g.Write16(regs + 28, left);
            g.Write16(regs + 30, right);
        }
        native::SoundEvent e;
        e.kind = native::SoundEvent::kVolume;
        e.voice = voice;
        e.left = static_cast<int16_t>(left);
        e.right = static_cast<int16_t>(right);
        Hand(g, e);

        // The pitch: the note's (sliding) with the vibrato, the fine tune (95, an eighth of a semitone's 192 steps?)
        // and the transpose (96, from 64), and the music's transpose
        int32_t pitch = Add(static_cast<int16_t>(static_cast<uint32_t>(c.L(28)) >> 16), c.L(24));
        pitch = Add(pitch, (c.SB(95) * 3 * 1024) >> 7);
        pitch = Add(pitch, ((c.SB(96) - 64) * 256) >> 6);
        if (music) {
            pitch = Add(pitch, static_cast<int16_t>(g.Read16(kTranspose)));
        }
        g.R(4) = Signed(pitch);
        g.Call(kStepOf);
        const uint16_t step = static_cast<uint16_t>(g.R(0));
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
        return done(300);
    }

    // 06139316: the driver set up: r4 its table of settings, r5 and r6 two more tables, r7 bit 0 a mode bit of the
    // chip; on the stack, the chip's other mode byte (long 0's low byte) and whether it is stereo (long 1's). The chip
    // silenced: every voice's registers cleared and the keys off.
    bool SetUp(Guest &g) {
        const uint32_t sp = g.R(15), table = g.R(4), second = g.R(5), third = g.R(6), mode = g.R(7);
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
        for (uint32_t i = 0; i < 0x200; i += 2) {
            g.Write16(kVoiceRegs + i, 0);
        }
        g.Write16(0x02079C10, static_cast<uint16_t>((static_cast<uint32_t>(g.Read8(sp + 3)) << 8) | (mode & 1) | 0x22));
        g.Write32(kChipMode, g.Read32(0x02079C10));
        g.Write16(kKeys, 0);
        for (uint8_t v = 0; v < 16; v++) {
            native::SoundEvent e;
            e.kind = native::SoundEvent::kVoice; // (every register 0)
            e.voice = v;
            e.start = e.loop = e.end = native::kCleared;
            Hand(g, e);
        }
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
        g.Return(17000); // (the other device's waits: 21 of 257 turns of a loop)
        return true;
    }

    // 0613A334: every channel of both sets stopped, the keys off
    bool StopAll(Guest &g) {
        for (uint32_t i = 0; i < 16; i++) {
            g.Write8(kMusic + i * kChannel + 94, 0xC0);
            g.Write8(kEffects + i * kChannel + 94, 0xC0);
            g.Write8(kEffects + i * kChannel + 111, 0);
        }
        g.Write8(kDriverFlags, 1);
        g.Write16(kKeys, 0);
        HandKeys(g, native::SoundEvent::kKeys, 0);
        g.Write16(kKeptKeys, 0);
        g.Return(250);
        return true;
    }

    // 0613A272: after a pause of the music, its channels as the pause found them and their voices keyed on again
    bool Resume(Guest &g) {
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
        g.Return(160);
        return true;
    }

    const Lift kLifts[] = {
        {"SoundSetUp", 0x06139316, 0x22E, 0x90E9CE5D71582AD6ull, SetUp, true, 0x100FF},
        {"SoundStopAll", 0x0613A334, 0xD4, 0x5F8C7600B189AA26ull, StopAll, true, 0x100FF},
        {"SoundResume", 0x0613A272, 0x86, 0xC2CCA2D3E2FE96B4ull, Resume, true, 0x100FF},
        {"SoundChannel", 0x0613B3CC, 0x8E0, 0x7781CBAA0B34ED7Dull, StepChannel, true, 0x1007F},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterSoundDriver() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
