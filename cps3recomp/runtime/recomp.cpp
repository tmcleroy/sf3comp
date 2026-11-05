// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
// The runtime: block lookup and dispatch for the game's translated SH-2 code. (satrecomp's dispatcher, for one
// CPU and the CPS-3's memory: the program SIMMs, main RAM and the cartridge's ROM.)
//
// SH-2 code runs as blocks: straight runs of instructions that end at an unconditional branch. A block is
// identified by its address and its exact bytes, so code that changes in main RAM can never run a stale
// translation. For each block reached, the dispatcher runs its translation if the build has one; otherwise the
// interpreter runs the same instructions the same way (InterpretFunctionFree), which is what the translation is
// checked against: a run with translations must leave the machine exactly as a run without them (tools/check.sh).
//
// Free running: a block runs to its end whatever the time slice (and a function of several blocks on to its next
// backward branch), and every instruction costs a fixed number of cycles by its kind (machine/sh2.cpp's table).
// Code in the CPU's cache RAM (0xC0000000) and anywhere else outside the three regions is left to the interpreter.
#include "recomp.h"

#include "lift.h"
#include "state.h"

#include "block_native.h"
#include "blocks.h"

#include "../machine/machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cps3recomp {
// The game's sources need the readable rewrites now, with the bus's timing on (set by system.cpp from
// Game::rewritesNeeded; none: never)
bool (*g_rewritesNeededNow)() = nullptr;
} // namespace cps3recomp

namespace cps3recomp {

uint8_t *g_mainRam = nullptr;
const uint8_t *g_bios = nullptr;
const uint8_t *g_program = nullptr;
const uint8_t *g_programData = nullptr;
const bool *g_flashPlain = nullptr;

using OpcodeType = machine::Op;

// The runtime's window into the CPU (a friend of SH2)
struct Access {
    struct Registers {
        std::array<uint32_t, 16> R;
        uint32_t PC, PR, SR, GBR, VBR;
        uint64_t MAC, cycles;
        bool pending, allow, delaySlot, sleep;
        uint32_t delayTarget, resume;
        machine::Bus bus; // (the bus's timing: its cache's state, and the pipeline's)
        bool operator==(const Registers &) const = default;
    };
    static machine::Machine &Board(SH2 &c) {
        return c.m_machine;
    }
    static Registers Get(SH2 &c) {
        return {c.R, c.PC, c.PR, c.SR.u32, c.GBR, c.VBR, c.MAC.u64, c.m_cyclesExecuted, c.m_intrFlags.pending, c.m_intrFlags.allow,
                c.m_delaySlot, c.m_sleep, c.m_delaySlotTarget, c.m_recompResume, c.m_bus};
    }
    static void Set(SH2 &c, const Registers &r) {
        c.R = r.R;
        c.PC = r.PC;
        c.PR = r.PR;
        c.SR.u32 = r.SR;
        c.GBR = r.GBR;
        c.VBR = r.VBR;
        c.MAC.u64 = r.MAC;
        c.m_cyclesExecuted = r.cycles;
        c.m_intrFlags.pending = r.pending;
        c.m_intrFlags.allow = r.allow;
        c.m_delaySlot = r.delaySlot;
        c.m_sleep = r.sleep;
        c.m_delaySlotTarget = r.delayTarget;
        c.m_recompResume = r.resume;
        c.m_bus = r.bus;
    }
    static std::array<uint8_t, 0x1000> &Cache(SH2 &c) {
        return c.m_cacheData;
    }
};

namespace {

    inline OpcodeType Kind(uint16_t opcode) {
        return machine::g_opKinds[opcode];
    }
    inline bool BadInSlot(uint16_t opcode) {
        return machine::IllegalInSlot(machine::g_opKinds[opcode]);
    }
    uint16_t Be16(const uint8_t *p) {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    // A place execution can enter a block: its first instruction (free running enters no other)
    struct Entry {
        const uint8_t *code; // big-endian instruction bytes from here to the end of the block
        NativeFn native;     // its translation (entry at the block's first instruction only)
        const FunctionDesc *function; // set if this is the entry of a function of several blocks
        Entry *head;         // set if this is another block of such a function: the entry of its first
        uint16_t block;      // ... and which block (the function is entered there)
        Entry *next;         // other code seen at the same address
        uint64_t runs;
        uint64_t checked;    // the code epoch at which `matches` was worked out (see Matches)
        bool matches;        // memory held this code then
        uint16_t size;       // bytes from here to the end of the block
        uint8_t start;       // instruction index within the block
        bool single;         // cannot be part of a block (illegal, sleep, trapa): one interpreter step
        bool fresh;          // found during this run (not in the build)
        const Lift *lift;    // a lifted routine or a hook at this address (lift.h): asked before any code is run
    };

    struct Region {
        uint32_t base;
        uint32_t size;
        const uint8_t *mem;
        bool rom;                   // never written: code is compared with memory once
        std::vector<Entry *> table; // by (offset / 2)
    };

    struct Runtime {
        Options opt;
        Region regions[3]; // the program SIMMs, main RAM, the cartridge's ROM
        std::deque<Entry> entries;
        std::deque<std::vector<uint8_t>> codeStore;
        // One byte per 64 bytes of main RAM: translated code lies there. A write to such a place moves the code
        // epoch on (machine::NoteWrite), and only then is an entry's code compared with memory again.
        std::vector<uint8_t> codeMap;
        uint64_t freshBlocks = 0, builtBlocks = 0, builtFunctions = 0, compares = 0;
        std::set<std::string> explored; // static discovery: address (text) + code bytes of the blocks found
        bool forceFree = false;          // (the check: the interpreter now, whatever the mode)

        // Calls made as calls (Call). A function that called another and did not get it back (the time slice
        // ended inside, an interrupt came up) is kept as a frame: when control arrives at the return address, the
        // function goes on from its block there. A function that stopped at one of its blocks (slice over,
        // interrupt pending) is kept as `stopped` and goes on from that block if that is where the CPU next is.
        struct Frame {
            Entry *entry;
            uint32_t block;
            uint32_t pc;
        };
        struct Linking {
            std::vector<Frame> frames;
            Frame stopped{nullptr, 0, 0};
            Entry *current = nullptr;
            unsigned depth = 0;
            bool operator==(const Linking &o) const {
                if (frames.size() != o.frames.size() || stopped.entry != o.stopped.entry || stopped.block != o.stopped.block ||
                    stopped.pc != o.stopped.pc) {
                    return false;
                }
                for (size_t i = 0; i < frames.size(); i++) {
                    if (frames[i].entry != o.frames[i].entry || frames[i].block != o.frames[i].block || frames[i].pc != o.frames[i].pc) {
                        return false;
                    }
                }
                return true;
            }
        } linking;
        bool link = false;
        uint64_t calls = 0, callsReturned = 0, framesResumed = 0, stopsResumed = 0, callsNotLinked = 0;

        // The check (Options::checkEvery)
        Machine *machine = nullptr;
        uint64_t dispatches = 0, checked = 0, notComparable = 0, different = 0;
        unsigned reportsLeft = 8;

        // Lifted routines (lift.h): CPS3RECOMP_NO_REWRITES=1 runs the game's own code for the rewrites;
        // CPS3RECOMP_LIFT_CHECK=n runs both for every nth call of each and compares what they leave
        struct LiftCheck {
            uint64_t calls = 0, checked = 0, different = 0, scratchDifferent = 0, notComparable = 0, hardwareWrites = 0;
            unsigned reportsLeft = 6;
        };
        bool noRewrites = false;
        bool noDryRuns = false; // CPS3RECOMP_NO_DRY_RUNS=1: the hand-overs' dry runs left out (lift.h)
        uint64_t dryRuns = 0;
        unsigned liftCheckEvery = 0;
        std::map<const Lift *, LiftCheck> liftChecks;
        bool inCheck = false; // a rewrite is being run for a check (its calls run in the interpreter)
        unsigned inCall = 0;  // Guest::Call is running a routine
    };

    Runtime *g_rt = nullptr; // the board being run now (Activate): its runtime, owned by its Board below
    std::deque<Lift> g_lifts; // (a deque: entries point at them)

    bool IsUnconditionalDelayed(OpcodeType t) {
        switch (t) {
        case OpcodeType::BRA:
        case OpcodeType::BRAF:
        case OpcodeType::BSR:
        case OpcodeType::BSRF:
        case OpcodeType::JMP:
        case OpcodeType::JSR:
        case OpcodeType::RTS:
        case OpcodeType::RTE: return true;
        default: return false;
        }
    }
    bool IsConditionalDelayed(OpcodeType t) {
        return t == OpcodeType::BTS || t == OpcodeType::BFS;
    }
    bool EndsBlock(OpcodeType t) {
        return t == OpcodeType::Illegal || t == OpcodeType::SLEEP || t == OpcodeType::TRAPA;
    }

    // Finds the block that starts at `code`: returns its instruction count (0 if the first instruction cannot
    // start a block). `slots` gets a bit for every instruction that only runs as a delay slot.
    unsigned ScanBlock(const uint8_t *code, uint32_t avail, uint64_t &slots) {
        unsigned n = 0;
        slots = 0;
        while (n < kMaxBlockInstrs && (n + 1) * 2 <= avail) {
            const OpcodeType t = Kind(Be16(code + n * 2));
            if (EndsBlock(t)) {
                break;
            }
            const bool uncond = IsUnconditionalDelayed(t);
            if (uncond || IsConditionalDelayed(t)) {
                if (n + 2 > kMaxBlockInstrs || (n + 2) * 2 > avail) {
                    break;
                }
                const uint16_t slot = Be16(code + (n + 1) * 2);
                if (BadInSlot(slot) || EndsBlock(Kind(slot))) {
                    break;
                }
                n += 2;
                if (uncond) {
                    slots |= 1ull << (n - 1);
                    break;
                }
                continue;
            }
            n++;
        }
        return n;
    }

    Region *RegionFor(Runtime &rt, uint32_t pc, uint32_t &offset) {
        // Cached (000) and cache-through (001) address spaces only
        if (pc >> 30) {
            return nullptr;
        }
        const uint32_t a = pc & 0x1FFFFFFF;
        if (a >= 0x06000000 && a < 0x07000000) {
            offset = a - 0x06000000;
            return &rt.regions[0];
        }
        if (a >= 0x02000000 && a < 0x02080000) {
            offset = a - 0x02000000;
            return &rt.regions[1];
        }
        if (a < 0x00080000) {
            offset = a;
            return &rt.regions[2];
        }
        return nullptr;
    }

    // --- static discovery ------------------------------------------------------------------------------
    //
    // While recording, the code a recorded block leads to is followed through memory as it is at that moment:
    // branches with fixed targets, the instruction after a call, and calls and jumps through a register loaded
    // from the literal pool. What is found goes into the block database with the blocks that ran, so that code no
    // recording run reached is translated too. A block found this way only runs if the CPU gets to its address and
    // finds the same bytes, like any other.

    // True if code at `code` reads like a routine's: the block there ends in a branch (or is cut at the length
    // limit), not because the next thing is no instruction. Keeps data out of the database.
    bool PlausibleBlock(const uint8_t *code, unsigned n, uint64_t slots) {
        if (n == 0) {
            return false;
        }
        bool allSame = true;
        for (unsigned i = 1; i < n; i++) {
            allSame = allSame && Be16(code + i * 2) == Be16(code);
        }
        if (allSame && n > 2) {
            return false; // (filler)
        }
        return n == kMaxBlockInstrs || ((slots >> (n - 1)) & 1);
    }

    void Explore(Runtime &rt, uint32_t startPc) {
        std::vector<uint32_t> todo{startPc};
        while (!todo.empty()) {
            const uint32_t at = todo.back();
            todo.pop_back();
            uint32_t offset;
            Region *r = RegionFor(rt, at, offset);
            if (!r || !r->rom || offset + 2 > r->size || (offset & 1)) {
                continue; // (only the ROMs: what is in main RAM may be anything by the time it runs)
            }
            uint64_t slots;
            const uint8_t *code = r->mem + offset;
            const unsigned n = ScanBlock(code, r->size - offset, slots);
            if (!PlausibleBlock(code, n, slots)) {
                continue;
            }
            char addr[16];
            snprintf(addr, sizeof(addr), "%08X ", r->base + offset);
            if (!rt.explored.insert(addr + std::string(reinterpret_cast<const char *>(code), n * 2)).second) {
                continue;
            }
            const uint32_t pc = r->base + offset;
            bool fallsThrough = true;
            for (unsigned i = 0; i < n; i++) {
                const uint16_t op = Be16(code + i * 2);
                const OpcodeType t = Kind(op);
                const uint32_t here = pc + i * 2;
                switch (t) {
                case OpcodeType::BT:
                case OpcodeType::BF:
                case OpcodeType::BTS:
                case OpcodeType::BFS: todo.push_back(here + 4 + static_cast<int32_t>(static_cast<int8_t>(op & 0xFF)) * 2); break;
                case OpcodeType::BRA:
                case OpcodeType::BSR:
                    todo.push_back(here + 4 + ((static_cast<int32_t>(op & 0xFFF) << 20) >> 19));
                    fallsThrough = t == OpcodeType::BSR; // (a call comes back to the instruction after its slot)
                    break;
                case OpcodeType::JSR:
                case OpcodeType::JMP: {
                    // The register's value, if a literal was loaded into it just before
                    const unsigned reg = (op >> 8) & 15;
                    for (unsigned k = i; k-- > 0;) {
                        const uint16_t prev = Be16(code + k * 2);
                        if ((prev >> 12) == 0xD && ((prev >> 8) & 15) == reg) {
                            const uint32_t lit = ((offset + k * 2) & ~3u) + 4 + (prev & 0xFF) * 4;
                            if (lit + 4 <= r->size) {
                                todo.push_back((static_cast<uint32_t>(Be16(r->mem + lit)) << 16) | Be16(r->mem + lit + 2));
                            }
                            break;
                        }
                        if (((prev >> 8) & 15) == reg) {
                            break; // (something else may have set the register)
                        }
                    }
                    fallsThrough = t == OpcodeType::JSR;
                    break;
                }
                case OpcodeType::BSRF: fallsThrough = true; break;
                case OpcodeType::BRAF:
                case OpcodeType::RTS:
                case OpcodeType::RTE: fallsThrough = false; break;
                default: break;
                }
            }
            if (fallsThrough) {
                todo.push_back(pc + n * 2);
            }
        }
    }

    void MarkCode(Runtime &rt, const Region &r, int64_t offset, unsigned size) {
        if (r.rom || offset < 0 || offset + size > r.size) {
            return;
        }
        for (size_t g = static_cast<size_t>(offset) >> 6; g <= (static_cast<size_t>(offset) + size - 1) >> 6 && g < rt.codeMap.size(); g++) {
            rt.codeMap[g] = 1;
        }
    }

    // Adds a block and an entry at its first instruction. Returns the entry.
    Entry *AddBlock(Runtime &rt, Region &r, uint32_t offset, const uint8_t *code, unsigned numInstrs, NativeFn native, bool single,
                    bool fresh) {
        Entry &e = rt.entries.emplace_back();
        e = {};
        e.code = code;
        e.native = native;
        e.size = static_cast<uint16_t>(numInstrs * 2);
        e.single = single;
        e.fresh = fresh;
        MarkCode(rt, r, offset, numInstrs * 2);
        e.next = r.table[offset >> 1];
        r.table[offset >> 1] = &e;
        return &e;
    }

    // A block the build doesn't know: take it from memory as it is now
    Entry *Discover(Runtime &rt, Region &r, uint32_t offset) {
        uint64_t slots;
        unsigned n = ScanBlock(r.mem + offset, r.size - offset, slots);
        const bool single = n == 0;
        if (single) {
            n = 1;
            if (offset + 2 > r.size) {
                return nullptr;
            }
        }
        auto &copy = rt.codeStore.emplace_back(r.mem + offset, r.mem + offset + n * 2);
        rt.freshBlocks++;
        return AddBlock(rt, r, offset, copy.data(), n, nullptr, single, true);
    }

    // How the interpreter left a block
    enum class BlockEnd {
        Stalled,  // an interrupt came up
        Fixed,    // went on to a fixed address: a branch with a fixed target, or off the end of the block
        Call,     // a call (jsr, bsr, bsrf) ending the block: the PC is the callee
        Computed, // return or computed jump
    };

    // The interpreter running a block the free-running way: to its end, charging nominal cycles. It leaves the
    // block where translated code does (block_native.h), so the two can be compared exactly.
    BlockEnd InterpretBlockFree(SH2 &cpu, const uint8_t *code, unsigned size) {
        const uint32_t pc0 = cpu.PC;
        const unsigned count = size / 2;
        for (unsigned i = 0; i < count; i++) {
            const uint16_t op = Be16(code + i * 2);
            const OpcodeType type = Kind(op);
            cpu.m_cyclesExecuted += cpu.RecompInterpretNext();
            if (cpu.m_delaySlot) {
                // A branch: its delay slot instruction belongs to it
                i++;
                cpu.m_cyclesExecuted += cpu.RecompInterpretNext();
                if (cpu.m_delaySlot) {
                    return BlockEnd::Stalled;
                }
                if (type == OpcodeType::JSR || type == OpcodeType::BSR || type == OpcodeType::BSRF) {
                    return i + 1 == count ? BlockEnd::Call : BlockEnd::Computed;
                }
                return type == OpcodeType::BRA || type == OpcodeType::BTS || type == OpcodeType::BFS ? BlockEnd::Fixed : BlockEnd::Computed;
            }
            if (cpu.m_intrFlags.pending) {
                return BlockEnd::Stalled;
            }
            if (cpu.PC != pc0 + (i + 1) * 2) {
                return type == OpcodeType::BT || type == OpcodeType::BF ? BlockEnd::Fixed : BlockEnd::Stalled;
            }
        }
        return BlockEnd::Fixed;
    }

    // The interpreter running a function of several blocks the way its translation does: from block `start` on,
    // from block to block along branches with fixed targets, calls made as calls (Call), leaving at anything
    // else; at a pending interrupt and, on a backward branch, when the time slice is over, it stops at a block
    // and says which (m_recompResume). `f` null: a block on its own, which may branch back to its own start.
    void InterpretFunctionFree(SH2 &cpu, const FunctionDesc *f, const uint8_t *code, unsigned size, unsigned start, uint64_t target) {
        FunctionBlock single{0, static_cast<uint16_t>(size), code};
        const FunctionBlock *blocks = f ? f->blocks : &single;
        const unsigned numBlocks = f ? f->numBlocks : 1;
        const FunctionBlock *block = &blocks[start < numBlocks ? start : 0];
        const uint32_t entry = cpu.PC - static_cast<uint32_t>(block->offset);
        auto find = [&](int32_t offset) -> const FunctionBlock * {
            if (!f) {
                return offset == 0 ? &single : nullptr;
            }
            for (unsigned i = 0; i < numBlocks; i++) {
                if (blocks[i].offset == offset) {
                    return &blocks[i];
                }
            }
            return nullptr;
        };
        for (;;) {
            const BlockEnd end = InterpretBlockFree(cpu, block->code, block->size);
            if (end == BlockEnd::Call) {
                const int32_t after = block->offset + block->size;
                const FunctionBlock *back = find(after);
                if (Call(cpu, target, back ? static_cast<int>(back - blocks) : -1, entry + static_cast<uint32_t>(after)) && back) {
                    block = back;
                    continue;
                }
                return;
            }
            if (end != BlockEnd::Fixed) {
                return;
            }
            const int32_t offset = static_cast<int32_t>(cpu.PC - entry);
            const FunctionBlock *next = find(offset);
            if (!next) {
                return;
            }
            if (cpu.m_intrFlags.pending || (offset <= block->offset && cpu.m_cyclesExecuted >= target)) {
                cpu.m_recompResume = static_cast<uint32_t>(next - blocks) + 1;
                return;
            }
            block = next;
        }
    }

    // Block code is compared against memory on every dispatch into main RAM
    inline bool SameCode(const uint8_t *a, const uint8_t *b, unsigned size) {
        return memcmp(a, b, size) == 0;
    }

    inline bool SameFunction(const Region &r, uint32_t offset, const FunctionDesc &f) {
        for (unsigned i = 1; i < f.numBlocks; i++) {
            const FunctionBlock &b = f.blocks[i];
            const int64_t at = static_cast<int64_t>(offset) + b.offset;
            if (at < 0 || at + b.size > r.size || !SameCode(r.mem + at, b.code, b.size)) {
                return false;
            }
        }
        return true;
    }

    // Is the entry's code (all of its function's) what is in memory now? Worked out by comparing; again only
    // after a write to main RAM that holds translated code (never for the ROMs)
    inline bool Matches(Runtime &rt, const Region &r, uint32_t offset, Entry &e) {
        const uint64_t epoch = r.rom ? 1 : CPS3RECOMP_CODE_EPOCH;
        if (e.checked == epoch) [[likely]] {
            return e.matches;
        }
        const bool matches = offset + e.size <= r.size && SameCode(r.mem + offset, e.code, e.size) &&
                             (!e.function || SameFunction(r, offset, *e.function));
        rt.compares++;
        e.checked = epoch;
        e.matches = matches;
        return matches;
    }

    inline uint32_t EntryStart(const Entry &e, uint32_t block, uint32_t pc) {
        return e.function && block < e.function->numBlocks ? pc - static_cast<uint32_t>(e.function->blocks[block].offset) : pc;
    }

    // Runs an entry's code from one of its blocks: its translation, or the interpreter doing the same
    void Run(Runtime &rt, SH2 &cpu, Entry &e, unsigned block, uint64_t target) {
        Runtime::Linking &lk = rt.linking;
        Entry *outer = lk.current;
        lk.current = &e;
        cpu.m_recompResume = 0;
        if (e.native && rt.opt.mode == Mode::Native && !rt.forceFree) {
            e.native(cpu, target, block);
        } else {
            InterpretFunctionFree(cpu, e.function, e.code, e.size, block, target);
        }
        lk.current = outer;
        if (const uint32_t resume = cpu.m_recompResume) {
            lk.stopped = {&e, resume - 1, cpu.PC};
            cpu.m_recompResume = 0;
        }
    }

    bool FrameValid(Runtime &rt, const Runtime::Frame &frame) {
        uint32_t offset;
        Region *r = RegionFor(rt, EntryStart(*frame.entry, frame.block, frame.pc), offset);
        return r && Matches(rt, *r, offset, *frame.entry);
    }

    // The check: the interpreter and the translation run from the same state; what they leave compared. Main RAM is
    // kept in a shadow copy brought up to date page by page (the pages written since: machine::g_dirtyMap), so that
    // a check costs what the block writes, not the whole of main RAM.
    constexpr size_t kPage = 256, kPages = 0x80000 / kPage;
    std::vector<uint8_t> g_shadow;
    std::vector<uint8_t> g_dirty;

    void CheckedRun(Runtime &rt, SH2 &cpu, Entry &e, unsigned block, uint64_t target, uint32_t pc) {
        static std::array<uint8_t, 0x1000> cacheBefore, cacheInterp;
        static std::vector<uint8_t> interpPages(kPages);
        static std::vector<uint8_t> interpRam(0x80000);
        for (size_t p = 0; p < kPages; p++) {
            if (g_dirty[p]) {
                memcpy(&g_shadow[p * kPage], &g_mainRam[p * kPage], kPage);
                g_dirty[p] = 0;
            }
        }
        const Access::Registers before = Access::Get(cpu);
        const Runtime::Linking linkBefore = rt.linking;
        const uint64_t hardware = machine::g_hardware;
        cacheBefore = Access::Cache(cpu);

        rt.forceFree = true;
        Run(rt, cpu, e, block, target);
        rt.forceFree = false;
        if (machine::g_hardware != hardware) {
            rt.notComparable++; // (the interpreter's run stands)
            return;
        }
        const Access::Registers interp = Access::Get(cpu);
        const Runtime::Linking linkInterp = rt.linking;
        cacheInterp = Access::Cache(cpu);
        for (size_t p = 0; p < kPages; p++) {
            interpPages[p] = g_dirty[p];
            if (g_dirty[p]) {
                memcpy(&interpRam[p * kPage], &g_mainRam[p * kPage], kPage);
                memcpy(&g_mainRam[p * kPage], &g_shadow[p * kPage], kPage);
                g_dirty[p] = 0;
            }
        }

        Access::Set(cpu, before);
        rt.linking = linkBefore;
        Access::Cache(cpu) = cacheBefore;
        CPS3RECOMP_CODE_EPOCH++;
        Run(rt, cpu, e, block, target);
        rt.checked++;
        const Access::Registers native = Access::Get(cpu);
        // (what main RAM must be: the interpreter's pages where it wrote, the shadow elsewhere)
        bool ramSame = true;
        size_t firstDiff = 0;
        for (size_t p = 0; p < kPages; p++) {
            if (!interpPages[p] && !g_dirty[p]) {
                continue;
            }
            const uint8_t *expected = interpPages[p] ? &interpRam[p * kPage] : &g_shadow[p * kPage];
            if (ramSame && memcmp(&g_mainRam[p * kPage], expected, kPage) != 0) {
                ramSame = false;
                for (size_t i = 0; i < kPage; i++) {
                    if (g_mainRam[p * kPage + i] != expected[i]) {
                        firstDiff = p * kPage + i;
                        break;
                    }
                }
            }
        }
        auto expectedByte = [&](size_t i) { return interpPages[i / kPage] ? interpRam[i] : g_shadow[i]; };
        const bool cacheSame = Access::Cache(cpu) == cacheInterp;
        const bool linkSame = rt.linking == linkInterp;
        if (native == interp && ramSame && cacheSame && linkSame) {
            return;
        }
        rt.different++;
        if (rt.reportsLeft) {
            rt.reportsLeft--;
            fprintf(stderr, "check: block %08X (entered at its block %u) differs:", pc, block);
            if (native.PC != interp.PC) {
                fprintf(stderr, " PC %08X/%08X", native.PC, interp.PC);
            }
            if (native.cycles != interp.cycles) {
                fprintf(stderr, " cycles +%llu/+%llu", (unsigned long long)(native.cycles - before.cycles),
                        (unsigned long long)(interp.cycles - before.cycles));
            }
            if (native.SR != interp.SR) {
                fprintf(stderr, " SR %03X/%03X", native.SR, interp.SR);
            }
            for (int i = 0; i < 16; i++) {
                if (native.R[i] != interp.R[i]) {
                    fprintf(stderr, " R%d %08X/%08X", i, native.R[i], interp.R[i]);
                }
            }
            if (native.PR != interp.PR || native.GBR != interp.GBR || native.MAC != interp.MAC || native.VBR != interp.VBR) {
                fprintf(stderr, " PR/GBR/MAC/VBR");
            }
            if (native.pending != interp.pending || native.allow != interp.allow || native.delaySlot != interp.delaySlot ||
                native.resume != interp.resume || native.sleep != interp.sleep) {
                fprintf(stderr, " flags");
            }
            if (!ramSame) {
                fprintf(stderr, " RAM %08zX %02X/%02X", 0x02000000 + firstDiff, g_mainRam[firstDiff], expectedByte(firstDiff));
            }
            if (!cacheSame) {
                fprintf(stderr, " cache RAM");
            }
            if (!linkSame) {
                fprintf(stderr, " call frames");
            }
            fprintf(stderr, " (translation/interpreter)\n");
        }
        // (the interpreter's result goes on)
        Access::Set(cpu, interp);
        rt.linking = linkInterp;
        for (size_t p = 0; p < kPages; p++) {
            if (interpPages[p] || g_dirty[p]) {
                memcpy(&g_mainRam[p * kPage], interpPages[p] ? &interpRam[p * kPage] : &g_shadow[p * kPage], kPage);
                g_dirty[p] = 1;
            }
        }
        Access::Cache(cpu) = cacheInterp;
        CPS3RECOMP_CODE_EPOCH++;
    }

    // Finds the code at `offset` of `r` as the dispatcher runs it: an entry, and which of its function's blocks
    Entry *Lookup(Runtime &rt, Region &r, uint32_t offset, unsigned &block) {
        block = 0;
        for (Entry *e = r.table[offset >> 1]; e; e = e->next) {
            if (e->lift) {
                continue; // (the dispatcher's business, before this)
            }
            if (e->head) {
                // A block of a function other than its first: the function is entered there
                const int64_t start = static_cast<int64_t>(offset) - e->function->blocks[e->block].offset;
                if (start >= 0 && Matches(rt, r, static_cast<uint32_t>(start), *e->head)) {
                    block = e->block;
                    return e->head;
                }
            } else if (Matches(rt, r, offset, *e)) {
                return e;
            }
        }
        return nullptr;
    }

    // --- lifted routines and hooks (lift.h) ---

    // Is the code at `offset` the routine a lift was written from? Its bytes are not in the build: they are taken from
    // memory the first time the hash matches, and compared with memory after that.
    bool LiftHere(Runtime &rt, const Region &r, uint32_t offset, Entry &e) {
        if (offset + e.size > r.size) {
            return false;
        }
        if (!e.code) {
            if (LiftHash(r.mem + offset, e.size) != e.lift->hash) {
                return false;
            }
            e.code = rt.codeStore.emplace_back(r.mem + offset, r.mem + offset + e.size).data();
        }
        return SameCode(r.mem + offset, e.code, e.size);
    }

    using LiftCheck = Runtime::LiftCheck;

    // A rewritten routine against the game's own code, for one call: the rewrite runs first (and to the routine's
    // return, if it called on); then the whole machine is put back and the game's code is run by the interpreter
    // until it returns; what the two left is compared. The game goes on from what its own code left. False if the
    // rewrite declined the call.
    //
    // Not compared: the registers the lift names as free, MACH and MACL, and the stack below where the routine found
    // it. A call where the game's code did not come back cannot be compared, and is counted.
    bool CheckRewrite(Runtime &rt, SH2 &cpu, const Lift &lift, LiftCheck &check) {
        static std::vector<uint8_t> machineBefore;
        machineBefore.clear();
        {
            Archive ar(machineBefore, true);
            ar.inMemory = true; // (character RAM by its changed pages only)
            rt.machine->Serialize(ar);
        }
        const Access::Registers before = Access::Get(cpu);
        const uint64_t cycles = before.cycles;
        static std::vector<machine::HardwareWrite> rewriteWrote, gameWrote;
        rewriteWrote.clear();
        gameWrote.clear();
        // Runs the interpreter from where the CPU is to the routine's return: the address in PR at the call, with the
        // stack as it was (or near it)
        const uint32_t returnTo = before.PR, stack = before.R[15];
        auto toReturn = [&]() {
            for (uint32_t steps = 0; steps < 20'000'000; steps++) {
                if (cpu.PC == returnTo && cpu.R[15] - stack + 0x1000 <= 0x2000 && !cpu.m_delaySlot) {
                    return true;
                }
                cpu.m_intrFlags.pending = false; // (no interrupt in the middle: it would not be part of the routine)
                (void)cpu.RecompInterpretNext();
            }
            return false;
        };
        machine::g_hardwareWrites = &rewriteWrote;
        rt.inCheck = true;
        Guest guest(cpu);
        const bool taken = lift.fn(guest);
        const bool rewriteReturned = taken && toReturn();
        rt.inCheck = false;
        machine::g_hardwareWrites = nullptr;
        if (!taken) {
            Archive ar(machineBefore, false);
            ar.inMemory = true; // (character RAM by its changed pages only)
            rt.machine->Serialize(ar);
            Access::Set(cpu, before);
            return false;
        }
        const Access::Registers rewritten = Access::Get(cpu);
        static std::vector<uint8_t> rewrittenRam;
        rewrittenRam.assign(g_mainRam, g_mainRam + 0x80000);
        const std::array<uint8_t, 0x1000> rewrittenCache = Access::Cache(cpu);

        // The game's own code, from the same before
        {
            Archive ar(machineBefore, false);
            ar.inMemory = true; // (character RAM by its changed pages only)
            rt.machine->Serialize(ar);
        }
        Access::Set(cpu, before);
        machine::g_hardwareWrites = &gameWrote;
        const bool returned = toReturn() && rewriteReturned;
        machine::g_hardwareWrites = nullptr;
        cpu.RecalcInterrupts();
        cpu.m_cyclesExecuted = cycles + 100;
        ++CPS3RECOMP_CODE_EPOCH; // (memory was put back and run over: translations are compared again)
        if (!g_dirty.empty()) {
            std::fill(g_dirty.begin(), g_dirty.end(), 1); // (the block check's shadow of main RAM: all of it again)
        }
        if (!returned) {
            check.notComparable++;
            if (check.reportsLeft) {
                check.reportsLeft--;
                fprintf(stderr, "rewrite %s: a call cannot be compared (one side did not come back)\n", lift.name);
            }
            return true;
        }
        check.checked++;
        const Access::Registers now = Access::Get(cpu);
        std::string what;
        char text[200];
        if (gameWrote != rewriteWrote) {
            size_t i = 0;
            while (i < gameWrote.size() && i < rewriteWrote.size() && gameWrote[i] == rewriteWrote[i]) {
                i++;
            }
            snprintf(text, sizeof(text), " hardware writes %zu/%zu, the first to differ: no. %zu", gameWrote.size(), rewriteWrote.size(), i);
            what += text;
        }
        check.hardwareWrites += gameWrote.size();
        bool freeDiffer = false;
        for (unsigned r = 0; r < 16; r++) {
            if (now.R[r] != rewritten.R[r]) {
                if ((lift.free >> r) & 1) {
                    freeDiffer = true;
                } else {
                    snprintf(text, sizeof(text), " r%u %08X/%08X", r, now.R[r], rewritten.R[r]);
                    what += text;
                }
            }
        }
        if (now.PC != rewritten.PC || now.PR != rewritten.PR || now.GBR != rewritten.GBR) {
            snprintf(text, sizeof(text), " pc %08X/%08X pr %08X/%08X gbr %08X/%08X", now.PC, rewritten.PC, now.PR, rewritten.PR, now.GBR,
                     rewritten.GBR);
            what += text;
        }
        if ((now.SR ^ rewritten.SR) & 1) {
            if ((lift.free >> 16) & 1) {
                freeDiffer = true;
            } else {
                what += now.SR & 1 ? " T 1/0" : " T 0/1";
            }
        }
        if ((now.SR ^ rewritten.SR) & 0x3F2) {
            what += " SR";
        }
        unsigned shown = 0;
        for (uint32_t i = 0; i < 0x80000; i++) {
            if (g_mainRam[i] == rewrittenRam[i]) {
                continue;
            }
            const uint32_t address = 0x02000000u + i, sp = stack & 0x1FFFFFFFu;
            if (address < sp && sp - address <= 0x4000) {
                continue; // (the stack below the routine's)
            }
            if (shown++ < 4) {
                snprintf(text, sizeof(text), " [%08X] %02X/%02X", address, g_mainRam[i], rewrittenRam[i]);
                what += text;
            }
        }
        if (shown > 4) {
            snprintf(text, sizeof(text), " (and %u more bytes)", shown - 4);
            what += text;
        }
        if (Access::Cache(cpu) != rewrittenCache) {
            what += " cache RAM";
        }
        if (!what.empty()) {
            check.different++;
            if (check.reportsLeft) {
                check.reportsLeft--;
                fprintf(stderr, "rewrite %s differs (the game's code/the rewrite):%s; called with r4-r7 %08X %08X %08X %08X from %08X\n", lift.name,
                        what.c_str(), before.R[4], before.R[5], before.R[6], before.R[7], before.PR);
            }
        } else if (freeDiffer) {
            check.scratchDifferent++;
        }
        return true;
    }

    // The lifts and hooks at `offset`: hooks run (and the game's code then does); a lift that takes the call runs in
    // the routine's place (true: the CPU is past it)
    bool RunLifts(Runtime &rt, SH2 &cpu, Region &r, uint32_t offset) {
        for (Entry *e = r.table[offset >> 1]; e; e = e->next) {
            if (!e->lift || !LiftHere(rt, r, offset, *e)) {
                continue;
            }
            const Lift &lift = *e->lift;
            Guest guest(cpu);
            // (with the bus's timing on, the port's own code takes none of the board's time for what it looks at:
            // a hook and a lift are charged nothing, a rewrite what it says; and the game's own code runs in place
            // of the rewrites unless the game's sources need them now: Game::rewritesNeeded)
            struct BusOff {
                SH2 &cpu;
                const bool was;
                explicit BusOff(SH2 &c) : cpu(c), was(c.m_busTiming) {
                    cpu.m_busTiming = false;
                }
                ~BusOff() {
                    cpu.m_busTiming = was;
                    cpu.m_busExtra = 0;
                }
            };
            const bool timed = cpu.m_busTiming;
            if (lift.hookFn) {
                e->runs++;
                BusOff off(cpu);
                lift.hookFn(guest);
                continue;
            }
            if (lift.rewrite) {
                if (rt.noRewrites || (timed && !(g_rewritesNeededNow && g_rewritesNeededNow()))) {
                    // (one that hands the native interface what it draws: run dry, charged nothing, before the game's
                    // code runs: lift.h)
                    if (lift.handsOver && !rt.noDryRuns && guest.Board().NativeTaking()) { // (machine.h: not for frames no GPU draws)
                        BusOff off(cpu);
                        DryRun dry;
                        std::copy(cpu.R.begin(), cpu.R.end(), dry.r);
                        dry.pc = cpu.PC;
                        dry.pr = cpu.PR;
                        dry.gbr = cpu.GBR;
                        dry.t = cpu.SR.T;
                        Guest dryGuest(cpu, dry);
                        if (lift.fn(dryGuest)) {
                            rt.dryRuns++;
                        }
                    }
                    continue;
                }
                LiftCheck &check = rt.liftChecks[&lift];
                if (rt.liftCheckEvery && check.calls++ % rt.liftCheckEvery == 0) {
                    if (!CheckRewrite(rt, cpu, lift, check)) {
                        continue;
                    }
                    e->runs++;
                    return true;
                }
            }
            BusOff off(cpu);
            if (lift.fn && lift.fn(guest)) {
                e->runs++;
                return true;
            }
        }
        return false;
    }

    bool Dispatch(SH2 &cpu, uint64_t target, void *ctx) {
        Runtime &rt = *static_cast<Runtime *>(ctx);
        bool ran = false;
        for (;;) {
            const uint32_t pc = cpu.PC;
            uint32_t offset;
            Region *r;
            // An interrupt is serviced by the interpreter, and code outside the regions is run by it
            if (cpu.m_intrFlags.pending || (pc & 1) || !(r = RegionFor(rt, pc, offset))) {
                break;
            }
            // A function that stopped here, or one that is waiting for a call to come back here, goes on
            unsigned block = 0;
            Entry *e = nullptr;
            Runtime::Linking &lk = rt.linking;
            if (lk.stopped.entry) {
                if (lk.stopped.pc == pc && FrameValid(rt, lk.stopped)) {
                    e = lk.stopped.entry;
                    block = lk.stopped.block;
                    rt.stopsResumed++;
                }
                lk.stopped.entry = nullptr;
            }
            if (!e) {
                for (size_t n = lk.frames.size(); n-- > 0;) {
                    if (lk.frames[n].pc == pc) {
                        if (FrameValid(rt, lk.frames[n])) {
                            e = lk.frames[n].entry;
                            block = lk.frames[n].block;
                            rt.framesResumed++;
                        }
                        lk.frames.resize(n); // (the calls made after it never came back, or have)
                        break;
                    }
                }
            }
            if (!e) {
                // Lifted routines and hooks first (lift.h)
                if (RunLifts(rt, cpu, *r, offset)) {
                    ran = true;
                    ++CPS3RECOMP_ACTIVITY;
                    if (cpu.m_delaySlot || cpu.m_cyclesExecuted >= target) {
                        return true;
                    }
                    continue;
                }
                e = Lookup(rt, *r, offset, block);
            }
            if (!e && !(e = Discover(rt, *r, offset))) {
                break;
            }
            e->runs++;
            if (e->runs == 1 && !rt.opt.recordPath.empty() && block == 0) [[unlikely]] {
                Explore(rt, pc); // (recording: follow where this code leads)
            }
            if (e->single) {
                break;
            }
            rt.dispatches++;
            if (rt.opt.checkEvery && e->native && lk.depth == 0 && rt.dispatches % rt.opt.checkEvery == 0) {
                CheckedRun(rt, cpu, *e, block, target, pc);
            } else {
                Run(rt, cpu, *e, block, target);
            }
            ran = true;
            if (cpu.m_delaySlot || cpu.m_cyclesExecuted >= target) {
                return true; // (a delay slot left to run: the interpreter does it)
            }
        }
        ++CPS3RECOMP_ACTIVITY;
        return ran;
    }

    std::string HexBytes(const uint8_t *p, size_t n) {
        static const char kHex[] = "0123456789ABCDEF";
        std::string s;
        for (size_t i = 0; i < n; i++) {
            s += kHex[p[i] >> 4];
            s += kHex[p[i] & 15];
        }
        return s;
    }

    // The block database: one block per line, address and code bytes in hex
    void SaveBlocks(Runtime &rt) {
        std::set<std::string> lines;
        if (FILE *f = fopen(rt.opt.recordPath.c_str(), "r")) {
            char buf[512];
            while (fgets(buf, sizeof(buf), f)) {
                std::string s = buf;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
                    s.pop_back();
                }
                if (!s.empty() && s[0] != '#') {
                    lines.insert(s);
                }
            }
            fclose(f);
        }
        const size_t before = lines.size();
        for (auto &r : rt.regions) {
            for (size_t i = 0; i < r.table.size(); i++) {
                for (Entry *e = r.table[i]; e; e = e->next) {
                    if (!e->single && e->runs && !e->function && !e->head && !e->lift) {
                        char addr[16];
                        snprintf(addr, sizeof(addr), "%08X ", r.base + static_cast<uint32_t>(i * 2));
                        lines.insert(addr + HexBytes(e->code, e->size));
                    }
                }
            }
        }
        size_t found = 0;
        for (const std::string &b : rt.explored) {
            found += lines.insert(b.substr(0, 9) + HexBytes(reinterpret_cast<const uint8_t *>(b.data()) + 9, b.size() - 9)).second;
        }
        FILE *f = fopen(rt.opt.recordPath.c_str(), "w");
        if (!f) {
            fprintf(stderr, "cps3recomp: cannot write %s\n", rt.opt.recordPath.c_str());
            return;
        }
        fprintf(f, "# cps3recomp block database: address, code bytes (derived from the game: never commit)\n");
        for (auto &l : lines) {
            fprintf(f, "%s\n", l.c_str());
        }
        fclose(f);
        fprintf(stderr, "cps3recomp: block database %s: %zu blocks (%zu new, %zu of them found by following code)\n", rt.opt.recordPath.c_str(),
                lines.size(), lines.size() - before, found);
    }

} // namespace

bool Call(SH2 &cpu, uint64_t target, int retBlock, uint32_t ret) {
    constexpr unsigned kMaxDepth = 128;
    constexpr size_t kMaxFrames = 256;
    Runtime &rt = *g_rt;
    Runtime::Linking &lk = rt.linking;
    const uint32_t pc = cpu.PC;
    uint32_t offset;
    Region *r;
    if (!rt.link || cpu.m_intrFlags.pending || cpu.m_cyclesExecuted >= target || lk.depth >= kMaxDepth || (pc & 1) ||
        !(r = RegionFor(rt, pc, offset))) {
        return false;
    }
    // (not where a lifted routine or a hook is: those are the dispatcher's)
    for (Entry *l = r->table[offset >> 1]; l; l = l->next) {
        if (l->lift) {
            rt.callsNotLinked++;
            return false;
        }
    }
    unsigned block;
    Entry *e = Lookup(rt, *r, offset, block);
    // (only where there is a translation, in either mode: the interpreter calls where the translation would)
    if (!e || !e->native || e->single) {
        rt.callsNotLinked++;
        return false;
    }
    Entry *caller = lk.current;
    const bool comesBack = retBlock >= 0 && caller != nullptr;
    if (comesBack) {
        if (lk.frames.size() >= kMaxFrames) {
            lk.frames.erase(lk.frames.begin());
        }
        lk.frames.push_back({caller, static_cast<uint32_t>(retBlock), ret});
    }
    const size_t mark = lk.frames.size();
    e->runs++;
    rt.calls++;
    lk.depth++;
    Run(rt, cpu, *e, block, target);
    lk.depth--;
    if (!comesBack || lk.stopped.entry || cpu.m_delaySlot || cpu.m_intrFlags.pending || cpu.m_cyclesExecuted >= target || cpu.PC != ret) {
        return false; // (the caller's frame stays: it goes on when control arrives at `ret`)
    }
    if (mark == 0 || lk.frames.size() < mark) {
        return false;
    }
    const Runtime::Frame frame = lk.frames[mark - 1];
    if (frame.entry != caller || frame.block != static_cast<uint32_t>(retBlock) || frame.pc != ret || !FrameValid(rt, frame)) {
        return false;
    }
    lk.frames.resize(mark - 1);
    rt.callsReturned++;
    return true;
}

namespace {
    // A board run by its own runtime: the globals the translated code and the CPU read (its memory, the map of its
    // main RAM's code, the code epoch, its flash's state) are this board's while it is the one being run
    struct Board {
        Machine *machine = nullptr;
        std::unique_ptr<Runtime> rt;
        uint8_t *mainRam = nullptr;
        const uint8_t *bios = nullptr, *program = nullptr, *programData = nullptr;
        const uint8_t *codeBase = nullptr, *codeMap = nullptr;
        size_t codeSize = 0;
        uint64_t codeEpoch = 1;
        uint8_t *dirtyMap = nullptr;
        bool flashPlain = true;
    };
    std::deque<Board> g_boards;
    Board *g_active = nullptr;

    void SaveGlobals(Board &b) {
        b.mainRam = g_mainRam;
        b.bios = g_bios;
        b.program = g_program;
        b.programData = g_programData;
        b.codeBase = CPS3RECOMP_CODE_BASE;
        b.codeMap = CPS3RECOMP_CODE_MAP;
        b.codeSize = CPS3RECOMP_CODE_SIZE;
        b.codeEpoch = CPS3RECOMP_CODE_EPOCH;
        b.dirtyMap = machine::g_dirtyMap;
        b.flashPlain = machine::g_flashPlain;
    }
    void LoadGlobals(Board &b) {
        g_rt = b.rt.get();
        g_mainRam = b.mainRam;
        g_bios = b.bios;
        g_program = b.program;
        g_programData = b.programData;
        CPS3RECOMP_CODE_BASE = b.codeBase;
        CPS3RECOMP_CODE_MAP = b.codeMap;
        CPS3RECOMP_CODE_SIZE = b.codeSize;
        CPS3RECOMP_CODE_EPOCH = b.codeEpoch;
        machine::g_dirtyMap = b.dirtyMap;
        machine::g_flashPlain = b.flashPlain;
        g_active = &b;
    }

    struct RegisteredSet {
        const char *name;
        const BlockTable *tables;
        size_t count;
    };
    std::vector<RegisteredSet> &Registered() {
        static std::vector<RegisteredSet> sets;
        return sets;
    }

    // A board's runtime made with these tables (and the lifts and the check, for the first board only), and made the
    // board being run
    void AttachTables(Machine &m, const Options &options, const BlockTable *tables, size_t tableCount, bool first);
} // namespace

bool RegisterBlockTables(const char *name, const BlockTable *tables, size_t count) {
    Registered().push_back({name, tables, count});
    return true;
}

void Attach(Machine &m, const Options &options) {
    const BlockTable *tables = g_blockTables;
    size_t count = g_blockTableCount;
    // (a game with no code of its own but another's registered that is its program: a stage game's own build)
    if (count == 0) {
        for (const RegisteredSet &set : Registered()) {
            if (set.count && set.tables[0].count) {
                tables = set.tables;
                count = set.count;
            }
        }
    }
    AttachTables(m, options, tables, count, true);
}

namespace {
void AttachTables(Machine &m, const Options &options, const BlockTable *tables, size_t tableCount, bool first) {
    if (g_active) {
        SaveGlobals(*g_active);
    }
    Board *board = nullptr;
    for (Board &b : g_boards) {
        if (b.machine == &m) {
            board = &b;
        }
    }
    if (!board) {
        board = &g_boards.emplace_back();
        board->machine = &m;
    }
    board->rt = std::make_unique<Runtime>();
    g_rt = board->rt.get();
    g_active = board;
    machine::g_dirtyMap = nullptr;
    Runtime &rt = *g_rt;
    rt.opt = options;
    rt.machine = &m;
    g_mainRam = m.mainRam;
    g_bios = m.BiosBytes();
    g_program = m.ProgramBytes();
    g_programData = m.ProgramDataBytes();
    g_flashPlain = &machine::g_flashPlain;
    rt.regions[0] = {0x06000000, 0x1000000, g_program, true, {}};
    rt.regions[1] = {0x02000000, 0x80000, g_mainRam, false, {}};
    rt.regions[2] = {0x00000000, 0x80000, g_bios, true, {}};
    for (auto &r : rt.regions) {
        r.table.assign(r.size / 2, nullptr);
    }
    // Writes to main RAM that holds translated code are noticed from here on
    rt.codeMap.assign(0x80000 >> 6, 0);
    CPS3RECOMP_CODE_BASE = g_mainRam;
    CPS3RECOMP_CODE_MAP = rt.codeMap.data();
    CPS3RECOMP_CODE_SIZE = 0x80000;
    if (rt.opt.checkEvery && first) {
        g_shadow.assign(g_mainRam, g_mainRam + 0x80000);
        g_dirty.assign(kPages, 0);
        machine::g_dirtyMap = g_dirty.data();
    }

    // (CPS3RECOMP_NO_BLOCKS=1: the build's translations left out, everything run by the interpreter)
    const bool noBlocks = getenv("CPS3RECOMP_NO_BLOCKS") != nullptr;
    for (size_t t = 0; t < tableCount && !noBlocks; t++) {
        for (size_t i = 0; i < tables[t].count; i++) {
            const BlockDesc &d = tables[t].blocks[i];
            uint32_t offset;
            Region *r = RegionFor(rt, d.pc, offset);
            uint64_t slots;
            if (!r || ScanBlock(d.code, d.numInstrs * 2u, slots) != d.numInstrs) {
                fprintf(stderr, "cps3recomp: generated block %08X doesn't match this runtime's block rules\n", d.pc);
                continue;
            }
            AddBlock(rt, *r, offset, d.code, d.numInstrs, d.native, false, false);
            rt.builtBlocks++;
        }
    }

    // Calls made as calls, unless something wants to see every block as it is entered
    rt.link = rt.opt.recordPath.empty() && !getenv("CPS3RECOMP_NO_CALLS");

    // Functions of several blocks: an entry of their own at their first block, found before the block's
    if (!getenv("CPS3RECOMP_NO_FUNCTIONS") && !noBlocks) {
        for (size_t t = 0; t < tableCount; t++) {
            for (size_t i = 0; i < tables[t].functionCount; i++) {
                const FunctionDesc &f = tables[t].functions[i];
                uint32_t offset;
                Region *r = RegionFor(rt, f.pc, offset);
                uint64_t slots;
                if (!r || ScanBlock(f.blocks[0].code, f.blocks[0].size, slots) != f.blocks[0].size / 2u) {
                    continue;
                }
                Entry &e = rt.entries.emplace_back();
                e = {};
                e.code = f.blocks[0].code;
                e.size = f.blocks[0].size;
                e.native = f.fn;
                e.function = &f;
                for (unsigned b = 0; b < f.numBlocks; b++) {
                    MarkCode(rt, *r, static_cast<int64_t>(offset) + f.blocks[b].offset, f.blocks[b].size);
                }
                e.next = r->table[offset >> 1];
                r->table[offset >> 1] = &e;
                rt.builtFunctions++;
                // The function can be entered at any of its blocks (not while recording: blocks are then followed
                // one by one)
                for (unsigned b = 1; b < f.numBlocks && rt.opt.recordPath.empty(); b++) {
                    const int64_t at = static_cast<int64_t>(offset) + f.blocks[b].offset;
                    if (at < 0 || at + f.blocks[b].size > r->size) {
                        continue;
                    }
                    Entry &mbr = rt.entries.emplace_back();
                    mbr = {};
                    mbr.code = f.blocks[b].code;
                    mbr.size = f.blocks[b].size;
                    mbr.native = f.fn;
                    mbr.function = &f;
                    mbr.head = &e;
                    mbr.block = static_cast<uint16_t>(b);
                    mbr.next = r->table[at >> 1];
                    r->table[at >> 1] = &mbr;
                }
            }
        }
    }
    // Lifted routines and hooks: an entry each at their address, asked before any code
    rt.noRewrites = getenv("CPS3RECOMP_NO_REWRITES") != nullptr;
    rt.noDryRuns = getenv("CPS3RECOMP_NO_DRY_RUNS") != nullptr;
    if (const char *v = getenv("CPS3RECOMP_LIFT_CHECK")) {
        rt.liftCheckEvery = static_cast<unsigned>(std::max(1, atoi(v)));
    }
    if (first && !getenv("CPS3RECOMP_NO_LIFTS")) {
        for (const Lift &lift : g_lifts) {
            uint32_t offset;
            if (Region *r = RegionFor(rt, lift.address, offset)) {
                Entry &e = rt.entries.emplace_back();
                e = {};
                e.size = static_cast<uint16_t>(lift.size);
                e.lift = &lift;
                e.next = r->table[offset >> 1];
                r->table[offset >> 1] = &e;
            }
        }
    }
    m.Cpu().SetRecompDispatcher(Dispatch, &rt);
    SaveGlobals(*board);
}
} // namespace

// A second board (a stage board) run by translated code of its own: the registered set whose blocks its program holds
// (most of a sample of them, read where they would be), in the first board's mode; none: it stays with the
// interpreter. The first board is the one being run again afterwards.
bool AttachSecond(Machine &m) {
    // (CPS3RECOMP_NO_STAGE_BLOCKS=1: it stays with the interpreter, to compare)
    if (g_boards.empty() || g_boards.front().machine == &m || getenv("CPS3RECOMP_NO_STAGE_BLOCKS")) {
        return false;
    }
    Board &first = g_boards.front();
    const RegisteredSet *best = nullptr;
    size_t bestScore = 0;
    const uint8_t *program = m.ProgramBytes(), *bios = m.BiosBytes();
    // (the registered sets, and the program's own: a second board of the same game, the modes' stage behind)
    std::vector<RegisteredSet> sets = Registered();
    if (g_blockTableCount) {
        sets.push_back({"its own game", g_blockTables, g_blockTableCount});
    }
    for (const RegisteredSet &set : sets) {
        size_t tried = 0, same = 0;
        for (size_t t = 0; t < set.count && tried < 400; t++) {
            for (size_t i = 0; i < set.tables[t].count && tried < 400; i += 7) {
                const BlockDesc &d = set.tables[t].blocks[i];
                const uint8_t *at = d.pc >= 0x06000000u && d.pc < 0x07000000u ? program + (d.pc - 0x06000000u)
                                    : d.pc < 0x80000u                          ? bios + d.pc
                                                                               : nullptr;
                if (!at) {
                    continue;
                }
                tried++;
                same += memcmp(at, d.code, d.numInstrs * 2u) == 0;
            }
        }
        if (tried && same * 10 >= tried * 9 && same > bestScore) {
            best = &set;
            bestScore = same;
        }
    }
    if (!best) {
        return false;
    }
    static RegisteredSet chosen;
    chosen = *best;
    best = &chosen; // (the list above goes)
    Options options;
    options.mode = first.rt->opt.mode;
    AttachTables(m, options, best->tables, best->count, false);
    if (getenv("CPS3RECOMP_STAGE_LOG")) {
        fprintf(stderr, "cps3recomp: a second board runs %s's translated code\n", best->name);
    }
    g_active->flashPlain = true; // (it has just started: its flash reads its data)
    LoadGlobals(g_boards.front());
    return true;
}

void Detach(Machine &m) {
    for (auto it = g_boards.begin(); it != g_boards.end(); ++it) {
        if (it->machine == &m && it != g_boards.begin()) {
            if (g_active == &*it) {
                LoadGlobals(g_boards.front());
            }
            m.Cpu().SetRecompDispatcher(nullptr, nullptr);
            g_boards.erase(it);
            return;
        }
    }
}

void Activate(Machine &m) {
    if (g_active && g_active->machine == &m) {
        return;
    }
    for (Board &b : g_boards) {
        if (b.machine == &m) {
            if (g_active) {
                SaveGlobals(*g_active);
            }
            LoadGlobals(b);
            return;
        }
    }
}


void StateLoaded() {
    if (g_rt) {
        g_rt->linking = {};
        ++CPS3RECOMP_CODE_EPOCH;
    }
}

namespace {
    Runtime::Linking g_keptLinking;
} // namespace

void KeepDispatcher() {
    if (g_rt) {
        g_keptLinking = g_rt->linking;
    }
}

void PutDispatcherBack() {
    if (g_rt) {
        g_rt->linking = g_keptLinking;
        ++CPS3RECOMP_CODE_EPOCH; // (memory was put back without its writes being seen)
    }
}

bool Finish() {
    if (!g_rt) {
        return true;
    }
    if (g_active != &g_boards.front()) {
        SaveGlobals(*g_active);
        LoadGlobals(g_boards.front()); // (the first board's report)
    }
    Runtime &rt = *g_rt;
    if (!rt.opt.recordPath.empty()) {
        SaveBlocks(rt);
    }
    if (rt.opt.stats) {
        uint64_t translated = 0, interpreted = 0, single = 0;
        std::vector<std::pair<uint64_t, uint32_t>> top;
        for (auto &r : rt.regions) {
            for (size_t i = 0; i < r.table.size(); i++) {
                for (Entry *e = r.table[i]; e; e = e->next) {
                    if (e->lift) {
                        continue;
                    }
                    if (e->single) {
                        single += e->runs;
                    } else if (e->native && rt.opt.mode == Mode::Native) {
                        translated += e->runs;
                    } else {
                        interpreted += e->runs;
                        if (e->runs) {
                            top.emplace_back(e->runs, r.base + static_cast<uint32_t>(i * 2));
                        }
                    }
                }
            }
        }
        std::sort(top.rbegin(), top.rend());
        for (size_t i = 0; i < top.size() && i < 8; i++) {
            fprintf(stderr, "cps3recomp: interpreted: %08X x %llu\n", top[i].second, (unsigned long long)top[i].first);
        }
        const uint64_t total = translated + interpreted + single;
        fprintf(stderr,
                "cps3recomp: blocks in build %llu (and %llu functions of several blocks), found this run %llu\n"
                "cps3recomp: block runs: translated %llu (%.2f%%), interpreted %llu (%.2f%%), single-stepped %llu\n"
                "cps3recomp: calls made as calls %llu (%llu came straight back, %llu callers taken up later), %llu not; "
                "functions taken up where they stopped %llu; code compared with memory %llu times\n",
                (unsigned long long)rt.builtBlocks, (unsigned long long)rt.builtFunctions, (unsigned long long)rt.freshBlocks,
                (unsigned long long)translated, total ? 100.0 * translated / total : 0.0, (unsigned long long)interpreted,
                total ? 100.0 * interpreted / total : 0.0, (unsigned long long)single, (unsigned long long)rt.calls,
                (unsigned long long)rt.callsReturned, (unsigned long long)rt.framesResumed, (unsigned long long)rt.callsNotLinked,
                (unsigned long long)rt.stopsResumed, (unsigned long long)rt.compares);
    }
    const bool ok = rt.different == 0;
    if (rt.opt.checkEvery) {
        fprintf(stderr, "cps3recomp: check: %llu dispatches compared, %llu different, %llu not comparable (touched hardware)\n",
                (unsigned long long)rt.checked, (unsigned long long)rt.different, (unsigned long long)rt.notComparable);
    }
    if (rt.dryRuns) {
        fprintf(stderr, "cps3recomp: hand-overs made by dry runs of their rewrites: %llu\n", (unsigned long long)rt.dryRuns);
    }
    bool liftsOk = true;
    for (const auto &[lift, check] : rt.liftChecks) {
        if (rt.liftCheckEvery) {
            fprintf(stderr,
                    "cps3recomp: rewrite %s: %llu calls, %llu compared, %llu different, %llu not comparable, %llu with free registers "
                    "different, %llu hardware writes compared\n",
                    lift->name, (unsigned long long)check.calls, (unsigned long long)check.checked, (unsigned long long)check.different,
                    (unsigned long long)check.notComparable, (unsigned long long)check.scratchDifferent,
                    (unsigned long long)check.hardwareWrites);
            liftsOk = liftsOk && check.different == 0;
        }
    }
    machine::g_dirtyMap = nullptr;
    for (Board &b : g_boards) {
        b.machine->Cpu().SetRecompDispatcher(nullptr, nullptr);
    }
    g_boards.clear();
    g_active = nullptr;
    g_rt = nullptr;
    return ok && liftsOk;
}

// --- lifted routines and hooks (lift.h) ---

uint32_t &Guest::R(unsigned n) {
    return m_dry ? m_dry->r[n & 15] : m_cpu.R[n & 15];
}
uint32_t Guest::PR() const {
    return m_dry ? m_dry->pr : m_cpu.PR;
}
uint32_t Guest::PC() const {
    return m_dry ? m_dry->pc : m_cpu.PC;
}
uint32_t Guest::GBR() const {
    return m_dry ? m_dry->gbr : m_cpu.GBR;
}
bool Guest::T() const {
    return m_dry ? m_dry->t : m_cpu.SR.T;
}
void Guest::SetT(bool t) {
    if (m_dry) {
        m_dry->t = t;
        return;
    }
    m_cpu.SR.T = t ? 1 : 0;
}

namespace {
    // A dry run's byte's key: main RAM and the program by their place (the cache-through mirror the same), the rest
    // (the CPU's cache RAM) as it is
    uint32_t DryKey(uint32_t address) {
        return (address >> 29) <= 1 ? (address & 0x1FFFFFFFu) : address;
    }
    // The chips (video, sound, I/O: 04000000-05FFFFFF) and the CPU's own registers: a dry run's writes to them are
    // dropped
    bool DryChip(uint32_t address) {
        const uint32_t a = address & 0x1FFFFFFFu;
        return (address >> 29) == 7 || ((address >> 29) <= 1 && a >= 0x04000000u && a < 0x06000000u);
    }
    std::map<uint32_t, void (*)(Guest &)> g_nativeForms;
} // namespace

// (a dry run: the guest's memory, with what the run wrote over it)
uint32_t Guest::DryRead(uint32_t address, int bytes) {
    address &= ~static_cast<uint32_t>(bytes - 1);
    uint32_t value = bytes == 1 ? m_cpu.Read8(address) : bytes == 2 ? m_cpu.Read16(address) : m_cpu.Read32(address);
    if (m_dry->bytes.empty()) {
        return value;
    }
    for (int i = 0; i < bytes; i++) {
        const auto it = m_dry->bytes.find(DryKey(address + static_cast<uint32_t>(i)));
        if (it != m_dry->bytes.end()) {
            const int shift = 8 * (bytes - 1 - i);
            value = (value & ~(0xFFu << shift)) | (static_cast<uint32_t>(it->second) << shift);
        }
    }
    return value;
}
void Guest::DryWrite(uint32_t address, uint32_t value, int bytes) {
    address &= ~static_cast<uint32_t>(bytes - 1);
    if (DryChip(address)) {
        return;
    }
    for (int i = 0; i < bytes; i++) {
        m_dry->bytes[DryKey(address + static_cast<uint32_t>(i))] = static_cast<uint8_t>(value >> (8 * (bytes - 1 - i)));
    }
}
uint8_t Guest::Read8(uint32_t address) {
    return m_dry ? static_cast<uint8_t>(DryRead(address, 1)) : m_cpu.Read8(address);
}
uint16_t Guest::Read16(uint32_t address) {
    return m_dry ? static_cast<uint16_t>(DryRead(address, 2)) : m_cpu.Read16(address);
}
uint32_t Guest::Read32(uint32_t address) {
    return m_dry ? DryRead(address, 4) : m_cpu.Read32(address);
}
void Guest::Write8(uint32_t address, uint8_t value) {
    if (m_dry) {
        DryWrite(address, value, 1);
        return;
    }
    m_cpu.Write8(address, value);
}
void Guest::Write16(uint32_t address, uint16_t value) {
    if (m_dry) {
        DryWrite(address, value, 2);
        return;
    }
    m_cpu.Write16(address, value);
}
void Guest::Write32(uint32_t address, uint32_t value) {
    if (m_dry) {
        DryWrite(address, value, 4);
        return;
    }
    m_cpu.Write32(address, value);
}
void Guest::Call(uint32_t address) {
    if (m_dry) {
        const auto it = g_nativeForms.find(address);
        if (it != g_nativeForms.end()) {
            it->second(*this);
            return;
        }
        static std::set<uint32_t> told;
        if (told.insert(address).second) {
            fprintf(stderr, "cps3recomp: a dry run calls %08X, which has no native form (AddNativeForm): not called\n", address);
        }
        return;
    }
    // (an address no code is at stands for "back in the caller")
    constexpr uint32_t kBack = 0x0FFFFFF0;
    Runtime &rt = *g_rt;
    const uint32_t pr = m_cpu.PR, pc = m_cpu.PC;
    m_cpu.PR = kBack;
    m_cpu.PC = address;
    rt.inCall++;
    const uint64_t cyclesAtCall = m_cpu.m_cyclesExecuted;
    for (uint32_t steps = 0;; steps++) {
        m_cpu.m_intrFlags.pending = false;
        if (m_cpu.PC == kBack && !m_cpu.m_delaySlot) {
            break;
        }
        // The routine runs as the rest of the game does: translated code and the lifts of what it calls (while a
        // rewrite is being checked: all by the interpreter, as the game's side of the check is)
        if (rt.inCheck || m_cpu.m_delaySlot || !Dispatch(m_cpu, m_cpu.m_cyclesExecuted + (1ull << 40), &rt)) {
            m_cpu.m_cyclesExecuted += m_cpu.RecompInterpretNext();
        }
        if (m_cpu.m_cyclesExecuted - cyclesAtCall > 100'000'000 || steps == 50'000'000) {
            fprintf(stderr, "Guest::Call(%08X): the routine did not come back (does it wait for something?)\n", address);
            abort();
        }
    }
    rt.inCall--;
    m_cpu.RecalcInterrupts();
    m_cpu.PR = pr;
    m_cpu.PC = pc;
}
machine::Machine &Guest::Board() {
    return Access::Board(m_cpu);
}
void Guest::Return(uint64_t cycles) {
    if (m_dry) {
        m_dry->pc = m_dry->pr;
        m_dry->cycles += cycles;
        return;
    }
    m_cpu.PC = m_cpu.PR;
    m_cpu.m_cyclesExecuted += cycles;
}

uint64_t LiftHash(const uint8_t *data, size_t size) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ data[i]) * 0x100000001B3ull;
    }
    return h;
}

void RegisterLifts(const Lift *lifts, size_t count) {
    g_lifts.insert(g_lifts.end(), lifts, lifts + count);
}

void AddHook(const char *name, uint32_t address, uint32_t size, uint64_t hash, void (*fn)(Guest &guest)) {
    Lift lift{name, address, size, hash, nullptr};
    lift.hookFn = fn;
    g_lifts.push_back(lift);
}

void AddNativeForm(uint32_t address, void (*fn)(Guest &guest)) {
    g_nativeForms[address] = fn;
}

} // namespace cps3recomp
