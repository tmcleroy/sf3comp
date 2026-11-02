// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The CPS-3's program cipher. The board's CPU reads its program ROM (the cartridge's flash, at 0x00000000) and the
// program SIMMs (0x06000000-0x06FFFFFF) through a cipher: each 32-bit word is XORed with a 16-bit mask, repeated in
// both halves, worked out from the word's address and the cartridge's two 32-bit keys. (The function is Andreas
// Naive's, as MAME's CPS-3 driver has it, BSD-3-Clause: THIRD_PARTY_NOTICES.md.)
//
// No key is in this project. The keys are found from the player's own set when the game starts: a ROM's
// zero-filled stretches are stored as the mask itself, and the mask's form lets the two keys be solved from those
// stretches in two searches of 65536 tries each (RecoverKeys). The keys are kept only in memory.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cps3recomp {

struct CipherKey {
    uint32_t key1 = 0, key2 = 0;
    bool none = false; // the program is not enciphered (a game made for a cartridge with a dead or plain CPU)
};

// The mask for the 32-bit word at `address` (as the CPU addresses it)
uint32_t CipherMask(uint32_t address, const CipherKey &key);

// Finds the keys from enciphered words: `words` are 32-bit words as stored, the first at `base`. Several regions
// may be given (all enciphered with the same keys). False (and a reason) if no keys fit.
struct CipherRegion {
    const uint32_t *words;
    size_t count;
    uint32_t base;
};
bool RecoverKeys(const std::vector<CipherRegion> &regions, CipherKey &key, std::string &error);

// Deciphers `count` words in place (the first at `base`)
void Decipher(uint32_t *words, size_t count, uint32_t base, const CipherKey &key);

} // namespace cps3recomp
