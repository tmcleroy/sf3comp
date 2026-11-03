// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The SH-2's timing (the arcade_timing feature): when each instruction starts and how long its memory holds it, as
// the SH7604's pipeline, cache and bus state controller make it, beyond the nominal cycles each instruction is
// counted with. The rules are the SH7604's as jtcores' SH7604 model applies them, and its numbers were fitted to that
// model run on a bench of small loops (docs/TIMING.md):
//
// - The pipeline is in order and stops whole while its memory waits. Its fetches are of a long (two instructions) at a
//   time, asked for two instructions ahead (just after a jump: one); its data accesses are in the instruction's MA
//   stage, a cycle after it starts. Both go through the cache, which serves one at a time.
// - The cache (CCR's CE): 64 entries of 4 ways of 16 bytes, the entry by address bits 9-4, the tag bits 28-10, its
//   six LRU bits per entry choosing the way to fill. A hit is a cycle. A read miss fills the line from the bus, a long
//   at a time, and the pipeline waits for all of it. The cache-through space (20000000-3FFFFFFF) is always the bus.
//   CCR's ID or OD leave instruction fetches or data out of it; TW makes it two ways and the cache RAM.
// - The bus: a bus cycle is 2 states and its area's waits (WCR's Wn: none, 1 or 2, or with 3 the long wait, 3 to 6 by
//   BCR1's AnLW), one for each piece of an area narrower than the access (BCR2's AnSZ; area 0 is 32 bits wide on the
//   CPS-3). A read holds the pipeline for all of it, and a fill or a read through the cache waits for the bus to be
//   free. A write is passed on: the pipeline waits until its bus cycle begins (2 cycles after it is asked for, at the
//   soonest) and goes on, its cache hits too. One asked for in the last state of a write before it the model's bus
//   controller does twice.
// - Besides: a cycle when the instruction before accessed memory and this one's fetch falls due with it (not when
//   that access was a read that waited), or loaded a register this one reads (sh2_pipeline.h).
//
// Not modelled: the CPS-3 board's own WAIT (its chips' answer times beyond the programmed waits), the idle cycles
// between areas (WCR's IWn; the model has none either), and the board's DMAs taking the bus.
//
// Times are the CPU's cycle count within its time slice (SH2::m_cyclesExecuted); Rebase moves them to the next slice's.
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

    // The instruction `opcode` at `pc` about to start at `now`: the cycles it starts later (its fetch, the pipeline)
    unsigned Begin(uint16_t opcode, uint32_t pc, uint64_t now);
    // Its data accesses, in order (`size` 1, 2 or 4 bytes): the cycles each holds the pipeline beyond its own
    unsigned Read(uint32_t address, unsigned size);
    unsigned Write(uint32_t address, unsigned size);
    // An exception or interrupt taken at `now` (the pipeline emptied; its own accesses follow)
    void Exception(uint64_t now);
    // The CPU's time moved by `by` (a new time slice)
    void Rebase(int64_t by);

    // The bus never makes translated code leave its block to wait (the generator's question)
    bool IsBusWait(uint32_t, uint32_t, bool) const {
        return false;
    }

    bool operator==(const Bus &) const = default;
    // All the cycles charged so far, and the bus cycles (for the timing harness's log, CPS3RECOMP_VBLANK_LOG; not saved)
    uint64_t charged = 0;
    uint64_t busCycles = 0;

    // For save states: everything but the two counts above
    void Serialize(Archive &ar);

private:
    enum class Place { Hit, Filled, Bus, None }; // (the cache's answer, a line filled, the bus, neither: on the chip)
    Place Look(uint32_t address, bool fetch);
    unsigned CycleStates(unsigned area) const;
    unsigned Width(unsigned area) const; // (bytes)
    unsigned External(uint32_t address, unsigned size); // (the bus's states for an access)

    uint16_t m_bcr1 = 0x03F0, m_bcr2 = 0x00FC, m_wcr = 0xAAFF; // (the SH7604's values at reset)
    uint8_t m_ccr = 0;
    // A line's tag (address bits 28-10) and a valid bit (bit 31), by entry and way
    std::array<std::array<uint32_t, 4>, 64> m_tags{};
    std::array<uint8_t, 64> m_lru{};
    int64_t m_cacheFree = 0;           // when the cache is free (a line filled, a read through it done)
    int64_t m_busFree = 0;             // when the bus is (a write passed on done too)
    uint8_t m_lastWrite = 0;           // the bus's last cycle a write
    uint8_t m_readWaited = 0;          // the instruction's read waited
    int64_t m_ma = 0;                  // when the instruction's next data access is
    uint32_t m_lastFetch = 0xFFFFFFFF; // the instruction last begun (to tell a jump from the next one)
    uint8_t m_prevMemory = 0;          // the instruction before accessed memory
    int8_t m_prevLoads = -1;           // ... and the register it loaded (-1 none)
};

} // namespace cps3recomp::machine
