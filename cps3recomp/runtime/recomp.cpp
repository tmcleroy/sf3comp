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

#include "block_native.h"
#include "blocks.h"

#include "../machine/machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cps3recomp {

uint8_t *g_mainRam = nullptr;
const uint8_t *g_bios = nullptr;
const uint8_t *g_program = nullptr;
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
        bool operator==(const Registers &) const = default;
    };
    static Registers Get(SH2 &c) {
        return {c.R, c.PC, c.PR, c.SR.u32, c.GBR, c.VBR, c.MAC.u64, c.m_cyclesExecuted, c.m_intrFlags.pending, c.m_intrFlags.allow,
                c.m_delaySlot, c.m_sleep, c.m_delaySlotTarget, c.m_recompResume};
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
    };

    std::unique_ptr<Runtime> g_rt;

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

    // The check: the interpreter and the translation run from the same state; what they leave compared
    void CheckedRun(Runtime &rt, SH2 &cpu, Entry &e, unsigned block, uint64_t target, uint32_t pc) {
        static std::vector<uint8_t> ramBefore, ramInterp;
        static std::array<uint8_t, 0x1000> cacheBefore, cacheInterp;
        const Access::Registers before = Access::Get(cpu);
        const Runtime::Linking linkBefore = rt.linking;
        const uint64_t hardware = machine::g_hardware;
        ramBefore.assign(g_mainRam, g_mainRam + 0x80000);
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
        ramInterp.assign(g_mainRam, g_mainRam + 0x80000);
        cacheInterp = Access::Cache(cpu);

        Access::Set(cpu, before);
        rt.linking = linkBefore;
        memcpy(g_mainRam, ramBefore.data(), 0x80000);
        Access::Cache(cpu) = cacheBefore;
        CPS3RECOMP_CODE_EPOCH++;
        Run(rt, cpu, e, block, target);
        rt.checked++;
        const Access::Registers native = Access::Get(cpu);
        const bool ramSame = memcmp(g_mainRam, ramInterp.data(), 0x80000) == 0;
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
                for (size_t i = 0; i < 0x80000; i++) {
                    if (g_mainRam[i] != ramInterp[i]) {
                        fprintf(stderr, " RAM %08zX %02X/%02X", 0x02000000 + i, g_mainRam[i], ramInterp[i]);
                        break;
                    }
                }
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
        memcpy(g_mainRam, ramInterp.data(), 0x80000);
        Access::Cache(cpu) = cacheInterp;
        CPS3RECOMP_CODE_EPOCH++;
    }

    // Finds the code at `offset` of `r` as the dispatcher runs it: an entry, and which of its function's blocks
    Entry *Lookup(Runtime &rt, Region &r, uint32_t offset, unsigned &block) {
        block = 0;
        for (Entry *e = r.table[offset >> 1]; e; e = e->next) {
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
                    if (!e->single && e->runs && !e->function && !e->head) {
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

void Attach(Machine &m, const Options &options) {
    g_rt = std::make_unique<Runtime>();
    Runtime &rt = *g_rt;
    rt.opt = options;
    rt.machine = &m;
    g_mainRam = m.mainRam;
    g_bios = m.BiosBytes();
    g_program = m.ProgramBytes();
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

    // (CPS3RECOMP_NO_BLOCKS=1: the build's translations left out, everything run by the interpreter)
    const bool noBlocks = getenv("CPS3RECOMP_NO_BLOCKS") != nullptr;
    for (size_t t = 0; t < g_blockTableCount && !noBlocks; t++) {
        for (size_t i = 0; i < g_blockTables[t].count; i++) {
            const BlockDesc &d = g_blockTables[t].blocks[i];
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
        for (size_t t = 0; t < g_blockTableCount; t++) {
            for (size_t i = 0; i < g_blockTables[t].functionCount; i++) {
                const FunctionDesc &f = g_blockTables[t].functions[i];
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
    m.Cpu().SetRecompDispatcher(Dispatch, &rt);
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
    g_rt.reset();
    return ok;
}

} // namespace cps3recomp
