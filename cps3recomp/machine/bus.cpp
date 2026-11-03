// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
// The SH-2's bus timing (bus.h).
#include "bus.h"

#include "state.h"

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
    return pieces * (CycleStates(area) - 1); // (each bus cycle's states but one: that one overlaps the pipeline's)
}

unsigned Bus::Read(uint32_t address, unsigned size, bool fetch) {
    const unsigned cycles = ReadCycles(address, size, fetch);
    charged += cycles;
    return cycles;
}
unsigned Bus::Write(uint32_t address, unsigned size) {
    const unsigned cycles = WriteCycles(address, size);
    charged += cycles;
    return cycles;
}

unsigned Bus::ReadCycles(uint32_t address, unsigned size, bool fetch) {
    const unsigned space = address >> 29;
    if (space == 6 || space == 7) {
        return 0; // (the cache RAM, the on-chip registers)
    }
    const bool cached = space == 0 && (m_ccr & kCacheOn) && !(m_ccr & (fetch ? kNoFetches : kNoData));
    if (!cached) {
        return space <= 1 ? External(address, size) : 0;
    }
    const unsigned entry = (address >> 4) & 63;
    const uint32_t tag = ((address >> 10) & 0x7FFFF) | 0x80000000u;
    const bool twoWays = (m_ccr & kTwoWays) != 0;
    auto &ways = m_tags[entry];
    for (int way = twoWays ? 2 : 0; way < 4; way++) {
        if (ways[way] == tag) {
            m_lru[entry] = Used(way, m_lru[entry]);
            return 0;
        }
    }
    // (a miss: the line filled, four longs)
    const int way = WayToFill(m_lru[entry], twoWays);
    ways[way] = tag;
    m_lru[entry] = Used(way, m_lru[entry]);
    return 4 * External(address, 4);
}

unsigned Bus::WriteCycles(uint32_t address, unsigned size) {
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
        return 0;
    }
    if (space > 1) {
        return 0;
    }
    return External(address, size); // (a line there is updated: its tag stays as it is)
}

void Bus::Serialize(Archive &ar) {
    ar.Pod(m_bcr1);
    ar.Pod(m_bcr2);
    ar.Pod(m_wcr);
    ar.Pod(m_ccr);
    ar.Pod(m_tags);
    ar.Pod(m_lru);
}

} // namespace cps3recomp::machine

#include "sh2.h"
#include "sh2_pipeline.h"

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
