// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// One of the SIMMs' flash chips (Fujitsu MBM29F016A, 2 MB) as the program sees it: it reads its data, or, after the
// unlock and ID commands, its maker and device codes, which the board's start-up checks to see the SIMMs are there.
// Writing and erasing (only the CD sets' loading does) are not done: the set is only read. The commands are the
// AMD-style ones MAME's flash model (intelfsh.cpp, BSD-3-Clause) follows.
#pragma once

#include <cstdint>

namespace cps3recomp::machine {

struct FlashChip {
    enum Mode : uint8_t { Normal, Unlock1, Unlock2, Id, Status };
    uint8_t mode = Normal;

    // What a read at `address` gives, `data` being the chip's byte there
    uint8_t Read(uint32_t address, uint8_t data) const {
        if (mode == Id) {
            switch (address & 0xFF) {
            case 0: return 0x04; // Fujitsu
            case 1: return 0xAD; // MBM29F016A
            default: return 0;
            }
        }
        if (mode == Status) {
            return 0x80; // (ready)
        }
        return data;
    }
    void Write(uint32_t address, uint8_t value) {
        switch (mode) {
        case Unlock1:
            mode = ((address & 0xFFFF) == 0x2AA && value == 0x55) ? Unlock2 : Normal;
            break;
        case Unlock2:
            if ((address & 0xFFFF) == 0x555 && value == 0x90) {
                mode = Id;
            } else {
                mode = Normal; // (program and erase: not done)
            }
            break;
        default:
            if (value == 0xF0 || value == 0xFF) {
                mode = Normal;
            } else if (value == 0x90) {
                mode = Id;
            } else if (value == 0x70) {
                mode = Status;
            } else if (value == 0xAA && (address & 0xFFF) == 0x555) {
                mode = Unlock1;
            }
            break;
        }
    }
};

} // namespace cps3recomp::machine
