// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The whole game machine as the port sees it: the board (machine/machine.h) with its picture drawn and its sound
// mixed each frame, and the players' controls as the buttons the recomp ports share. The port's code (port/, taken
// from cps2recomp) works on this, as cps2recomp's does on its Machine, so that what is shared keeps one shape.
#pragma once

#include "machine.h"
#include "pcm.h"
#include "renderer.h"
#include "romset.h"

#include <cstdint>
#include <vector>

namespace cps3recomp {

class Archive;

// A player's controls, as bits that are 1 while pressed (cps2recomp's, the same bits)
enum Button : uint16_t {
    kRight = 1 << 0,
    kLeft = 1 << 1,
    kDown = 1 << 2,
    kUp = 1 << 3,
    kPunch1 = 1 << 4, // light, medium, hard
    kPunch2 = 1 << 5,
    kPunch3 = 1 << 6,
    kKick1 = 1 << 7,
    kKick2 = 1 << 8,
    kKick3 = 1 << 9,
    kStart = 1 << 10,
    kCoin = 1 << 11,
    kTag = 1 << 12, // (never on the board's lines: for a game's sources' own use)
};

// The picture's size (the board can also show 496 columns: PictureWidth)
struct Video {
    static constexpr int kWidth = 384, kHeight = 224, kMaxWidth = 496;
};

// The board's settings memory, as the port keeps it in a file
struct Eeprom {
    uint32_t *words = nullptr;
    static constexpr size_t kBytes = 128;
};

class Machine {
public:
    static constexpr int kMaxPlayers = 4; // (the board reads two; the port's tables are cps2recomp's)
    static constexpr double kFramesPerSecond = machine::Machine::kFrameRate;

    explicit Machine(const RomSet &rom);

    void Reset();
    // One frame: the controls given to the board, the board run, the picture drawn (unless `drawing` is off) and
    // the frame's sound added to `audio`
    void RunFrame();
    bool drawing = true;

    const uint32_t *Picture() const {
        return m_picture.data();
    }
    uint32_t *Picture() {
        return m_picture.data();
    }
    int PictureWidth() const {
        return m_width;
    }
    // The sound made since the port last took it: 16-bit stereo, left first, at Pcm::kRate
    std::vector<int16_t> audio;
    static constexpr double kSampleRate = Pcm::kRate;

    uint16_t players[kMaxPlayers] = {};
    bool serviceButton = false, testSwitch = false;
    Eeprom eeprom;
    uint64_t frame = 0;

    machine::Machine board;
    uint8_t *ram() {
        return board.mainRam;
    }

    void Serialize(Archive &ar);

private:
    Renderer m_renderer;
    Pcm m_pcm;
    std::vector<uint32_t> m_picture;
    int m_width = Video::kWidth, m_height = Video::kHeight;
};

// A long or a word as the CPU would read it, for a game's sources reading tables in the program SIMMs or the
// cartridge's ROM as well as main RAM (reads with no side effects only: memory, not the board's registers)
uint32_t Peek32(uint32_t address);
uint16_t Peek16(uint32_t address);

} // namespace cps3recomp
