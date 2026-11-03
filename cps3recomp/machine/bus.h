// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The SH-2's bus timing (the arcade_timing feature): the cycles an access takes beyond the one its instruction is
// counted with, as the SH7604's bus state controller and cache make them. The rules are the SH7604 hardware
// manual's, as jtcores' SH7604 model applies them (its BSC and cache), which the port was compared with
// (docs/TIMING.md):
//
// - An external access is a bus cycle of 2 states and the area's waits: none, 1 or 2 by WCR's Wn, or with Wn 3 the
//   long wait (3 to 6 by BCR1's AnLW). An area narrower than the access (BCR2's AnSZ; area 0 is 32 bits wide on
//   the CPS-3) takes a bus cycle for each piece.
// - The cache (CCR's CE): 64 entries of 4 ways of 16 bytes, the entry by address bits 9-4, the tag bits 28-10.
//   A read of the cached space (addresses 00000000-1FFFFFFF) that hits costs nothing more; one that misses fills
//   the line, a long at a time (the long after the one asked for first, round to it: the CPU waits for the whole
//   line), the way to fill chosen by the entry's six LRU bits. Writes go through (a bus cycle each, the line
//   updated if it is there; no line is filled). The cache-through space (20000000-3FFFFFFF) is always the bus.
//   CCR's ID or OD leave instruction fetches or data out of the cache; TW makes it two ways and the cache RAM.
// - The cache's data array as RAM (C0000000) and the on-chip registers (FFFFFE00 on) take no bus cycle.
//
// Not modelled: the CPS-3 board's own WAIT (the SIMMs' and its chips' answer times beyond the programmed waits),
// the idle cycles between areas (WCR's IWn), an instruction fetch and a data access wanting the cache at once, and
// the board's DMAs taking the bus.
#pragma once

#include <array>
#include <cstdint>

namespace cps3recomp {
class Archive;
}

namespace cps3recomp::machine {

class Bus {
public:
    // The bus state controller's and the cache control register's values as the program writes them
    void SetBCR1(uint16_t v) { m_bcr1 = v; }
    void SetBCR2(uint16_t v) { m_bcr2 = v; }
    void SetWCR(uint16_t v) { m_wcr = v; }
    void SetCCR(uint8_t v);

    // The cycles beyond the one counted for an access: `size` 1, 2 or 4 bytes, `fetch` an instruction fetch
    unsigned Read(uint32_t address, unsigned size, bool fetch);
    unsigned Write(uint32_t address, unsigned size);

    // The bus never makes translated code leave its block to wait (the generator's question)
    bool IsBusWait(uint32_t, uint32_t, bool) const {
        return false;
    }

    bool operator==(const Bus &) const = default;

    // For save states: everything above (the cache's tags and LRU included)
    void Serialize(Archive &ar);

private:
    // One bus cycle's states in an area, and how many a `size`-byte access takes there
    unsigned CycleStates(unsigned area) const;
    unsigned Width(unsigned area) const; // (bytes)
    unsigned External(uint32_t address, unsigned size) const;

    uint16_t m_bcr1 = 0x03F0, m_bcr2 = 0x00FC, m_wcr = 0xAAFF; // (the SH7604's values at reset)
    uint8_t m_ccr = 0;
    // A line's tag (address bits 28-10) and a valid bit (bit 31), by entry and way
    std::array<std::array<uint32_t, 4>, 64> m_tags{};
    std::array<uint8_t, 64> m_lru{};
};

} // namespace cps3recomp::machine
