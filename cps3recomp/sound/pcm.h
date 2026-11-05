// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The CPS-3's sound chip: sixteen voices playing 8-bit samples from the graphics SIMMs, each with a start, an end, a
// loop point, a pitch and a volume for each side. How a voice steps through its sample is MAME's (cps3_a.cpp,
// Philip Bennett, BSD-3-Clause), written again here. The chip's registers are in the machine (machine.h); this mixes
// them into 16-bit stereo at the chip's rate, as the frame goes (a register write mixes what was playing up to then).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace cps3recomp {
class Archive;
namespace machine {
    class Machine;
}

class Pcm {
public:
    // 42.954545 MHz / 3 / 384
    static constexpr double kRate = 42954545.0 / 3.0 / 384.0;

    explicit Pcm(machine::Machine &m);
    // The frame's sound, mixed to its end and added to `out` (left and right in turn), if given
    void EndFrame(std::vector<int16_t> *out);
    // A frame not mixed (the native mixer heard in its place): its length passed over, as EndFrame does
    void SkipFrame();
    // What the machine calls before a sound register is written (machine.h's beforeSoundWrite), for putting back
    void (*const beforeWrite)(void *) = &Pcm::BeforeWrite;
    // Volume as a fraction of the chip's
    float gain = 1.0f;
    void Serialize(Archive &ar);

private:
    machine::Machine &m_machine;
    std::vector<int32_t> m_mix; // the frame so far (left and right in turn)
    std::array<std::vector<int32_t>, 16> m_voiceMix; // (the native check's: the same voice by voice)
    double m_perFrame;
    double m_carry = 0; // samples owed from earlier frames
    size_t m_frameSamples = 0;
    void MixTo(size_t samples);
    static void BeforeWrite(void *self);
};

// A 16-bit stereo WAV file
void WriteWav(const char *path, const std::vector<int16_t> &samples, double rate);

} // namespace cps3recomp
