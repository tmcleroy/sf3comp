// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
// The SH-2's timing (bus.h).
#include "bus.h"

#include "sh2_pipeline.h"
#include "state.h"

#include <algorithm>

namespace cps3recomp::machine {

namespace {
    // CCR's bits
    constexpr uint8_t kCacheOn = 0x01, kNoFetches = 0x02, kNoData = 0x04, kTwoWays = 0x08, kPurge = 0x10;

    // The way the LRU bits say to fill (four ways; two ways: 2 or 3), and the bits after a way is used
    int WayToFill(uint8_t lru, bool twoWays) {
        if (twoWays) {
            return (lru & 1) ? 2 : 3;
        }
        if ((lru & 0x38) == 0x38) {
            return 0;
        }
        if ((lru & 0x26) == 0x06) {
            return 1;
        }
        if ((lru & 0x15) == 0x01) {
            return 2;
        }
        if ((lru & 0x0B) == 0x00) {
            return 3;
        }
        return 0; // (no pattern: way 0's, as the model leaves it)
    }
    uint8_t Used(int way, uint8_t lru) {
        switch (way) {
        case 0: return static_cast<uint8_t>(lru & 0x07);
        case 1: return static_cast<uint8_t>((lru & 0x19) | 0x20);
        case 2: return static_cast<uint8_t>((lru & 0x2A) | 0x14);
        default: return static_cast<uint8_t>(lru | 0x0B);
        }
    }
} // namespace

void Bus::SetCCR(uint8_t v) {
    if (v & kPurge) {
        for (auto &entry : m_tags) {
            entry.fill(0);
        }
        m_lru.fill(0);
    }
    m_ccr = static_cast<uint8_t>(v & ~kPurge);
}

// A bus cycle's states in an area: 2, and the waits
unsigned Bus::CycleStates(unsigned area) const {
    const unsigned w = (m_wcr >> (area * 2)) & 3;
    if (w < 3) {
        return 2 + w;
    }
    // (the long wait: area 0's AnLW, area 1's, or areas 2 and 3's AHLW)
    const unsigned shift = area == 0 ? 4 : area == 1 ? 6 : 8;
    return 2 + 3 + ((m_bcr1 >> shift) & 3);
}
unsigned Bus::Width(unsigned area) const {
    if (area == 0) {
        return 4; // (the CPS-3 wires area 0 at 32 bits: its MD pins)
    }
    const unsigned sz = (m_bcr2 >> (area * 2)) & 3;
    return sz == 1 ? 1 : sz == 2 ? 2 : 4;
}
unsigned Bus::External(uint32_t address, unsigned size) {
    const unsigned area = (address >> 25) & 3, width = Width(area);
    const unsigned pieces = size > width ? size / width : 1;
    busCycles += pieces;
    return pieces * CycleStates(area);
}

// Where an access is answered: the cache (a hit, or a line filled from the bus), the bus, or the chip itself
Bus::Place Bus::Look(uint32_t address, bool fetch) {
    const unsigned space = address >> 29;
    if (space >= 2) {
        return Place::None; // (the purge space, the cache's arrays, the cache RAM, the on-chip registers)
    }
    if (space == 1 || !(m_ccr & kCacheOn) || (m_ccr & (fetch ? kNoFetches : kNoData))) {
        return Place::Bus;
    }
    const unsigned entry = (address >> 4) & 63;
    const uint32_t tag = ((address >> 10) & 0x7FFFF) | 0x80000000u;
    const bool twoWays = (m_ccr & kTwoWays) != 0;
    auto &ways = m_tags[entry];
    for (int way = twoWays ? 2 : 0; way < 4; way++) {
        if (ways[way] == tag) {
            m_lru[entry] = Used(way, m_lru[entry]);
            return Place::Hit;
        }
    }
    const int way = WayToFill(m_lru[entry], twoWays);
    ways[way] = tag;
    m_lru[entry] = Used(way, m_lru[entry]);
    return Place::Filled;
}

unsigned Bus::Begin(uint16_t opcode, uint32_t pc, uint64_t now) {
    const int64_t t = static_cast<int64_t>(now);
    int64_t stall = 0;
    // The fetch: of a long, asked for two instructions ahead (one, just after a jump); the instruction can be decoded
    // the cycle after it has come. A hit waits only for a line being filled; a miss for the bus too.
    const bool next = pc == m_lastFetch + 2;
    m_lastFetch = pc;
    if ((pc & 2) == 0 || !next) {
        const int64_t asked = next ? t - 2 : t - 1;
        int64_t takes = 1;
        const Place place = Look(pc & ~3u, true);
        switch (place) {
        case Place::Filled: takes = 4 * External(pc & ~3u, 4); break;
        case Place::Bus: takes = External(pc & ~3u, 4); break;
        default: break;
        }
        const int64_t start = place == Place::Hit ? std::max(asked, m_cacheFree) : std::max({asked, m_cacheFree, m_busFree});
        const int64_t done = start + takes;
        if (place != Place::Hit) {
            m_cacheFree = m_busFree = done;
            m_lastWrite = false;
        }
        stall = std::max<int64_t>(0, done - t);
    }
    // The pipeline's own stalls (sh2_pipeline.h): the fetch clash not after a read that waited (the fetch was made
    // while it did)
    const PipeInfo &p = PipeOf(opcode);
    const bool fetchClash = m_prevMemory && !m_readWaited && !(pc & 2) && !p.bra12;
    const bool loadUse = m_prevLoads >= 0 && (p.reads >> m_prevLoads) & 1;
    if (fetchClash || loadUse) {
        stall += 1;
    }
    m_prevMemory = p.memory;
    m_prevLoads = p.loads;
    m_readWaited = 0;
    m_ma = t + stall + 1;
    charged += static_cast<uint64_t>(stall);
    return static_cast<unsigned>(stall);
}

unsigned Bus::Read(uint32_t address, unsigned size) {
    const Place place = Look(address, false);
    if (place == Place::None) {
        m_ma += 1;
        return 0;
    }
    int64_t start = std::max(m_ma, m_cacheFree), takes = 1;
    if (place != Place::Hit) {
        takes = External(address, place == Place::Filled ? 4 : size) * (place == Place::Filled ? 4 : 1);
        start = std::max(start, m_busFree);
        m_lastWrite = false;
    }
    const int64_t done = start + takes;
    if (place != Place::Hit) {
        m_cacheFree = m_busFree = done;
    }
    const int64_t stall = done - (m_ma + 1);
    if (stall > 0) {
        m_readWaited = 1;
    }
    m_ma = done;
    charged += static_cast<uint64_t>(stall);
    return static_cast<unsigned>(stall);
}

unsigned Bus::Write(uint32_t address, unsigned size) {
    const unsigned space = address >> 29;
    if (space == 2) {
        // (the purge space: the line that address would be in, invalid)
        const unsigned entry = (address >> 4) & 63;
        const uint32_t tag = ((address >> 10) & 0x7FFFF) | 0x80000000u;
        for (auto &way : m_tags[entry]) {
            if (way == tag) {
                way = 0;
            }
        }
    }
    if (space >= 2) {
        m_ma += 1;
        return 0;
    }
    // Passed on: the pipeline waits until its bus cycle begins, 2 cycles after it is asked for at the soonest, and
    // goes on (a line in the cache is updated, its tag as it is; the cache's hits go on meanwhile). One asked for in
    // the last state of a write the bus controller does twice (the model's: its bus cycle taken while the other ends)
    const uint64_t before = busCycles;
    const int64_t asked = std::max(m_ma, m_cacheFree), states = External(address, size);
    int64_t begins;
    if (m_lastWrite && asked == m_busFree - 1) {
        busCycles += busCycles - before;
        begins = m_busFree + states;
        m_busFree = begins + states;
    } else {
        begins = std::max(asked + 2, m_busFree);
        m_busFree = begins + states;
    }
    m_lastWrite = true;
    const int64_t stall = begins - m_ma;
    m_ma = begins + 1;
    charged += static_cast<uint64_t>(stall);
    return static_cast<unsigned>(stall);
}

void Bus::Exception(uint64_t now) {
    m_ma = static_cast<int64_t>(now) + 1;
    m_lastFetch = 0xFFFFFFFF;
    m_prevMemory = 0;
    m_prevLoads = -1;
}

void Bus::Rebase(int64_t by) {
    m_cacheFree += by;
    m_busFree += by;
    m_ma += by;
}

void Bus::Serialize(Archive &ar) {
    ar.Pod(m_bcr1);
    ar.Pod(m_bcr2);
    ar.Pod(m_wcr);
    ar.Pod(m_ccr);
    ar.Pod(m_tags);
    ar.Pod(m_lru);
    ar.Pod(m_cacheFree);
    ar.Pod(m_busFree);
    ar.Pod(m_ma);
    ar.Pod(m_lastWrite);
    ar.Pod(m_readWaited);
    ar.Pod(m_lastFetch);
    ar.Pod(m_prevMemory);
    ar.Pod(m_prevLoads);
}

} // namespace cps3recomp::machine

#include "sh2.h"

namespace cps3recomp::machine {

const PipeInfo &PipeOf(uint16_t opcode) {
    static const std::array<PipeInfo, 0x10000> table = [] {
        std::array<PipeInfo, 0x10000> t{};
        PipeKind byOp[256] = {};
        for (const PipeKind &k : kPipeKinds) {
            byOp[static_cast<unsigned>(k.op)] = k;
        }
        for (unsigned op = 0; op < 0x10000; op++) {
            const PipeKind &k = byOp[static_cast<unsigned>(g_opKinds[op])];
            const auto reg = [op](int8_t r) { return r == kRn ? static_cast<int>((op >> 8) & 15) : r == kRm ? static_cast<int>((op >> 4) & 15) : r; };
            PipeInfo &p = t[op];
            for (const int8_t r : k.reads) {
                if (r != kNone) {
                    p.reads |= 1u << reg(r);
                }
            }
            p.memory = k.memory != 0;
            p.bra12 = k.bra12 != 0;
            p.loads = k.loads == kNone ? -1 : static_cast<int8_t>(reg(k.loads));
        }
        return t;
    }();
    return table[opcode];
}

} // namespace cps3recomp::machine
