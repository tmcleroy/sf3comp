// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
// Block tables: the interface between generated code (tools/recomp.py) and the runtime (recomp.cpp).
#pragma once

#include "backend.h"

#include <cstddef>
#include <cstdint>

namespace cps3recomp {

// Translated code (tools/sh2emit.py, block_native.h): a block, or a function of several blocks. It counts
// nominal cycles, and runs until control leaves it, an interrupt is pending or, at a backward branch, the time
// slice is over (`target` cycles). `start` is the block of the function to start at: 0, its entry, or the block
// it stopped at before (which it says in the CPU's m_recompResume) or was to come back to from a call.
using NativeFn = void (*)(SH2 &cpu, uint64_t target, unsigned start);

// A call made by translated code (the CPU's PC is the callee, PR the return address): runs the callee's
// translation right away, if there is one. True if it came back to `ret` and the caller, a function that has a
// block there (number `retBlock`), can go on with it. retBlock < 0: the caller has no block there.
bool Call(SH2 &cpu, uint64_t target, int retBlock, uint32_t ret);

// A straight run of SH-2 instructions, identified by its address and its exact code bytes (deciphered)
struct BlockDesc {
    uint32_t pc;         // 0x06xxxxxx program SIMMs, 0x020xxxxx main RAM, 0x000xxxxx the cartridge's ROM
    uint16_t numInstrs;  // code size is numInstrs * 2 bytes
    const uint8_t *code; // the instructions, big-endian
    NativeFn native;
};

// A native function made of several blocks joined by their branches
struct FunctionBlock {
    int32_t offset; // from the function's entry address
    uint16_t size;  // bytes
    const uint8_t *code;
};

struct FunctionDesc {
    uint32_t pc;
    NativeFn fn;
    const FunctionBlock *blocks; // blocks[0] is the entry
    uint16_t numBlocks;
};

struct BlockTable {
    const BlockDesc *blocks;
    size_t count;
    const FunctionDesc *functions;
    size_t functionCount;
};

// Defined by the generated blocks_index.cpp (or, empty, by no_blocks.cpp when nothing was generated for the game)
extern const BlockTable g_blockTables[];
extern const size_t g_blockTableCount;

// Another game's code linked into the same program (tools/recomp.py --prefix: a stage board's), registered by name as
// the program starts; a board takes the set whose blocks its program holds (runtime/recomp.cpp)
bool RegisterBlockTables(const char *name, const BlockTable *tables, size_t count);

// Longest block the recorder makes
inline constexpr unsigned kMaxBlockInstrs = 48;

} // namespace cps3recomp
