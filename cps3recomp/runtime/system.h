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
#include <memory>
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
    static constexpr int kWidth = 384, kHeight = 224, kMaxWidth = 624; // (496, the board's wide mode, with widescreen's columns)
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
    // Widescreen: columns the picture gets on each side (0: the board's)
    void SetWideMargin(int margin) {
        m_renderer.margin = margin;
    }
    int WideMargin() const {
        return m_renderer.margin;
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

    // A second board whose picture is the stage (the definitive version's stages from the other games): its picture is
    // drawn with only its objects that `stageObject` says are the stage, and this board's other objects over it. It is
    // stepped by StepStage, once a shown frame (not run-ahead's or rollback's frames: it is scenery, outside the save
    // state), with `beforeStage` from the game's sources first (its camera, keeping it in its fight)
    void SetStageBoard(std::unique_ptr<RomSet> set);
    machine::Machine *StageBoard() {
        return m_stage.get();
    }
    void StepStage();
    // Which objects make the picture: of the stage board, those that are its stage; of this board, those that are not
    // its own stage
    bool (*stageObject)(const machine::Machine &stage, uint32_t object) = nullptr;
    bool (*ownStageObject)(const machine::Machine &own, uint32_t object) = nullptr;
    bool (*composeNow)() = nullptr; // (none: whenever there is a stage board)
    void (*beforeStage)(Machine &machine, machine::Machine &stage) = nullptr;

private:
    Renderer m_renderer;
    Pcm m_pcm;
    std::vector<uint32_t> m_picture;
    std::unique_ptr<RomSet> m_stageSet;
    std::unique_ptr<machine::Machine> m_stage;
    Renderer m_stageRenderer;
    int m_width = Video::kWidth, m_height = Video::kHeight;
};

// A long or a word as the CPU would read it, for a game's sources reading tables in the program SIMMs or the
// cartridge's ROM as well as main RAM (reads with no side effects only: memory, not the board's registers)
uint32_t Peek32(uint32_t address);
uint16_t Peek16(uint32_t address);

} // namespace cps3recomp
