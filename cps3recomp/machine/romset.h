// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The game's ROM set (a "NO CD" set: the cartridge's program ROM and the SIMMs' flash chips as the CD would have
// written them), read from a zip or a folder, checked against game.cfg's checksums, and laid out as the board sees
// it. The set is only ever read.
#pragma once

#include "crypt.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cps3recomp {

struct RomFile {
    std::string name;
    uint32_t crc = 0;
};

struct RomList {
    std::string set, parent;
    std::vector<RomFile> bios;    // the cartridge's program ROM (one 512 KB file)
    std::vector<RomFile> simm[7]; // SIMMs 1-6 (index 0 unused): 2 MB flash chips, in slot order
    bool programEnciphered = true; // the SIMMs' program is read through the cipher (all but 2nd Impact)
};

// "name:crc name:crc ..."
std::vector<RomFile> ParseRomFiles(const char *text);

struct RomSet {
    // The cartridge's program ROM (0x00000000-0x0007FFFF) as 32-bit words: as stored, and deciphered
    std::vector<uint32_t> biosRaw, bios;
    // The program SIMMs 1 and 2 (0x06000000-0x06FFFFFF): 4 M words as stored (what the SH-2's DMA reads) and
    // deciphered (what the CPU reads); a SIMM the game does not have reads as 0xFFFFFFFF
    std::vector<uint32_t> programRaw, program;
    // ... and as the CPU's instruction reads see it, where that is not `program` (2nd Impact: its instructions are
    // deciphered, its data is not); empty: `program`
    std::vector<uint32_t> opcodes;
    const std::vector<uint32_t> &Opcodes() const {
        return opcodes.empty() ? program : opcodes;
    }
    // The graphics SIMMs 3-7 as the video's DMAs see them: a stream of bytes, two chips of a pair alternating, the
    // first byte at the video's address 0x400000 (MAME's "user5" region). The sound chip's samples are here too.
    std::vector<uint8_t> graphics;
    // The flash chips themselves, [simm][chip], for the CPU's reads of them as flash
    std::vector<uint8_t> chips[7][8];
    CipherKey key;
    bool programEnciphered = true; // (RomList's: the SIMMs' program is read through the cipher)
};

bool LoadRomSet(const std::string &path, const RomList &list, RomSet &set, std::string &error);

} // namespace cps3recomp
