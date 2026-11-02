// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
// Building blocks of translated code (tools/sh2emit.py).
//
// A translated block is plain C++ working on the registers, with main RAM, the cartridge's ROM and the program
// SIMMs read directly (deciphered, as the CPU reads them). Anything else (the board's hardware, the SH-2's on-chip
// peripherals, the cache RAM) goes through the CPU's memory functions. Time is counted in nominal cycles per
// instruction, the interpreter's own, and the translation is checked against the interpreter run block by block
// the same way (`--cpu free`, tools/check.sh).
//
// A translated block runs from its first instruction to its end and leaves early only when
//  - a conditional branch is taken, or
//  - an interrupt became pending (after a hardware access or a write to SR).
#pragma once

#include "blocks.h"

#include <cstring>

namespace cps3recomp {

extern uint8_t *g_mainRam;       // 512 KiB at 0x02000000
extern const uint8_t *g_bios;    // the cartridge's ROM, 512 KiB at 0x00000000, deciphered
extern const uint8_t *g_program; // the program SIMMs, 16 MiB at 0x06000000, deciphered
extern const bool *g_flashPlain; // the program SIMMs' flash chips are all reading their data (not their IDs)

// Direct memory access (BlockSet<-1> only so that it is a friend of SH2, like the generated code)
template <>
struct BlockSet<-1> {
    // Main RAM, the ROM or the program SIMMs behind an address of the cached (000) or cache-through (001) space
    FORCE_INLINE static const uint8_t *ReadPtr(uint32_t a) {
        if ((a & 0xDFF80000u) == 0x02000000u) [[likely]] {
            return g_mainRam + (a & 0x7FFFFu);
        }
        if ((a & 0xDF000000u) == 0x06000000u && *g_flashPlain) {
            return g_program + (a & 0xFFFFFFu);
        }
        if ((a & 0xDFF80000u) == 0) {
            return g_bios + (a & 0x7FFFFu);
        }
        return nullptr;
    }
    FORCE_INLINE static uint8_t *WritePtr(uint32_t a) {
        if ((a & 0xDFF80000u) == 0x02000000u) [[likely]] {
            return g_mainRam + (a & 0x7FFFFu);
        }
        return nullptr;
    }
    // A write went to `p`: if translated code lies there, it has to be compared with memory again
    FORCE_INLINE static void Wrote(const uint8_t *p) {
        CPS3RECOMP_NOTE_WRITE(p);
    }
    FORCE_INLINE static uint16_t Get16(const uint8_t *p) {
        uint16_t v;
        memcpy(&v, p, 2);
        return __builtin_bswap16(v);
    }
    FORCE_INLINE static uint32_t Get32(const uint8_t *p) {
        uint32_t v;
        memcpy(&v, p, 4);
        return __builtin_bswap32(v);
    }
    FORCE_INLINE static void Put16(uint8_t *p, uint16_t v) {
        v = __builtin_bswap16(v);
        memcpy(p, &v, 2);
    }
    FORCE_INLINE static void Put32(uint8_t *p, uint32_t v) {
        v = __builtin_bswap32(v);
        memcpy(p, &v, 4);
    }
};
using Native = BlockSet<-1>;

} // namespace cps3recomp

// ENTRY: the address of the function's first block (the PC is at the block it starts at)
#define SN_BEGIN(ENTRY)                                  \
    CPS3RECOMP_USING_TYPES                          \
    auto &R = c.R;                                  \
    const uint32 entry = (ENTRY);                  \
    uint32 pc = entry;                              \
    uint64 base = c.m_cyclesExecuted;               \
    bool hw = false;                                \
    (void)hw;                                       \
    (void)target;

// The start of a block, OFF bytes from the function's entry
#define SN_BLOCK(LABEL, OFF)    \
LABEL:                          \
    pc = entry + (OFF);         \
    c.m_intrFlags.allow = true;

// A branch to another block of the function (or back to the start of this one), CYC cycles into the current
// block. The CPU leaves the function instead if an interrupt is pending or, on a backward branch, if its time
// slice is over; K is the number of the block it was going to, where the function is taken up again.
#define SN_GOTO(LABEL, OFF, CYC, BACK, K)                                                       \
    {                                                                                           \
        base += (CYC);                                                                          \
        if (c.m_intrFlags.pending || ((BACK) && base >= target)) [[unlikely]] {                 \
            c.m_cyclesExecuted = base;                                                          \
            c.PC = entry + (OFF);                                                               \
            c.m_recompResume = (K) + 1;                                                         \
            return;                                                                             \
        }                                                                                       \
        goto LABEL;                                                                             \
    }

// A call (jsr, bsr, bsrf), after its delay slot instruction: the callee's translation is called here and now
// (recomp.cpp: Call). ONBACK: what to do when it has come back to the instruction after the call and that is a
// block of this function (a goto to it), nothing otherwise.
#define SN_CALL(TARGET, CYC, RETOFF, RETK, ONBACK)                                              \
    {                                                                                           \
        c.PC = (TARGET);                                                                        \
        c.m_cyclesExecuted = base + (CYC);                                                      \
        if (cps3recomp::Call(c, target, (RETK), entry + (RETOFF))) {                            \
            base = c.m_cyclesExecuted;                                                          \
            ONBACK                                                                              \
        }                                                                                       \
        return;                                                                                 \
    }

// The same for a loop (a block branching back to its own start) that only waits: every pass reads the same
// memory and leaves the same registers, until something outside (an interrupt) changes what it reads. Nothing
// does within this time slice, so the rest of the slice is spent here at once: as many passes as fit, counted
// but not run. (Not when a pass read hardware: that may change by itself.)
#define SN_GOTO_IDLE(LABEL, OFF, CYC, K)                                              \
    {                                                                               \
        base += (CYC);                                                              \
        if (!hw && !c.m_intrFlags.pending && base < target) {                       \
            base += (target - base + (CYC) - 1) / (CYC) * (CYC);                    \
        }                                                                           \
        if (c.m_intrFlags.pending || base >= target) [[likely]] {                   \
            c.m_cyclesExecuted = base;                                              \
            c.PC = entry + (OFF);                                                   \
            c.m_recompResume = (K) + 1;                                             \
            return;                                                                 \
        }                                                                           \
        hw = false;                                                                 \
        goto LABEL;                                                                 \
    }

// A block that starts with a countdown (dt Rn; bf to itself): the passes that neither finish the count nor
// end the time slice are counted at once. The last one runs as ordinary code after this.
#define SN_COUNTDOWN(N, CYC)                                                        \
    {                                                                               \
        const uint64 untilZero = static_cast<uint32>(R[N] - 1u);                    \
        const uint64 untilTarget = base < target ? (target - base - 1) / (CYC) : 0; \
        const uint64 skip = untilZero < untilTarget ? untilZero : untilTarget;      \
        R[N] -= static_cast<uint32>(skip);                                          \
        base += skip * (CYC);                                                       \
    }

// At the start of each instruction: its address and the time (in cycles since the block began) before it
#define SN_I(PCOFF, CYC)                 \
    const uint32 here = pc + (PCOFF);    \
    const uint64 now = base + (CYC);     \
    (void)here;                          \
    (void)now;

// Leave the block: at an instruction of the block, or to another address
#define SN_LEAVE(PCOFF, CYC)                   \
    {                                          \
        c.PC = pc + (PCOFF);                   \
        c.m_cyclesExecuted = base + (CYC);     \
        return;                                \
    }
#define SN_JUMP(TARGET, CYC)                   \
    {                                          \
        c.PC = (TARGET);                       \
        c.m_cyclesExecuted = base + (CYC);     \
        return;                                \
    }

// Before the hardware is reached it must see the current time
#define SN_HW_BEGIN()           \
    c.m_cyclesExecuted = now;   \
    hw = true;

// Memory reads. V is declared by the macro. ONWAIT: statement that leaves the block when the bus makes the
// access wait (never, on this board: kept for the generator's shape); the N forms never wait.
#define SN_RD8N(V, ADDR)                                        \
    uint32 V;                                                   \
    {                                                           \
        const uint32 a_ = (ADDR);                               \
        if (const uint8_t *p_ = Native::ReadPtr(a_)) [[likely]] { \
            V = *p_;                                            \
        } else {                                                \
            SN_HW_BEGIN()                                       \
            V = c.MemReadByte<false>(a_);                       \
        }                                                       \
    }
#define SN_RD16X(V, ADDR, ONWAIT)                                                    \
    uint32 V;                                                                        \
    {                                                                                \
        const uint32 a_ = (ADDR);                                                    \
        if (const uint8_t *p_ = Native::ReadPtr(a_ & ~1u)) [[likely]] {              \
            V = Native::Get16(p_);                                                   \
        } else {                                                                     \
            SN_HW_BEGIN()                                                            \
            ONWAIT                                                                   \
            V = c.MemReadWord<false>(a_);                                            \
        }                                                                            \
    }
#define SN_RD32X(V, ADDR, ONWAIT)                                                    \
    uint32 V;                                                                        \
    {                                                                                \
        const uint32 a_ = (ADDR);                                                    \
        if (const uint8_t *p_ = Native::ReadPtr(a_ & ~3u)) [[likely]] {              \
            V = Native::Get32(p_);                                                   \
        } else {                                                                     \
            SN_HW_BEGIN()                                                            \
            ONWAIT                                                                   \
            V = c.MemReadLong<false>(a_);                                            \
        }                                                                            \
    }
#define SN_RD16(V, ADDR, ONWAIT) SN_RD16X(V, ADDR, )
#define SN_RD32(V, ADDR, ONWAIT) SN_RD32X(V, ADDR, )
#define SN_RD16N(V, ADDR) SN_RD16X(V, ADDR, )
#define SN_RD32N(V, ADDR) SN_RD32X(V, ADDR, )

// Memory writes (each counts as activity)
#define SN_WR8N(ADDR, VAL)                                      \
    ++CPS3RECOMP_ACTIVITY;                                      \
    {                                                           \
        const uint32 a_ = (ADDR);                               \
        const uint8 v_ = static_cast<uint8>(VAL);               \
        if (uint8_t *p_ = Native::WritePtr(a_)) [[likely]] {    \
            *p_ = v_;                                           \
            Native::Wrote(p_);                                  \
        } else {                                                \
            SN_HW_BEGIN()                                       \
            c.MemWriteByte<false, false>(a_, v_);               \
        }                                                       \
    }
#define SN_WR16X(ADDR, VAL, ONWAIT)                                     \
    ++CPS3RECOMP_ACTIVITY;                                              \
    {                                                                   \
        const uint32 a_ = (ADDR);                                       \
        const uint16 v_ = static_cast<uint16>(VAL);                     \
        if (uint8_t *p_ = Native::WritePtr(a_ & ~1u)) [[likely]] {      \
            Native::Put16(p_, v_);                                      \
            Native::Wrote(p_);                                          \
        } else {                                                        \
            SN_HW_BEGIN()                                               \
            ONWAIT                                                      \
            c.MemWriteWord<false, false>(a_, v_);                       \
        }                                                               \
    }
#define SN_WR32X(ADDR, VAL, ONWAIT)                                     \
    ++CPS3RECOMP_ACTIVITY;                                              \
    {                                                                   \
        const uint32 a_ = (ADDR);                                       \
        const uint32 v_ = (VAL);                                        \
        if (uint8_t *p_ = Native::WritePtr(a_ & ~3u)) [[likely]] {      \
            Native::Put32(p_, v_);                                      \
            Native::Wrote(p_);                                          \
        } else {                                                        \
            SN_HW_BEGIN()                                               \
            ONWAIT                                                      \
            c.MemWriteLong<false, false>(a_, v_);                       \
        }                                                               \
    }
#define SN_WR16(ADDR, VAL, ONWAIT) SN_WR16X(ADDR, VAL, )
#define SN_WR32(ADDR, VAL, ONWAIT) SN_WR32X(ADDR, VAL, )
#define SN_WR16N(ADDR, VAL) SN_WR16X(ADDR, VAL, )
#define SN_WR32N(ADDR, VAL) SN_WR32X(ADDR, VAL, )

// A branch with a delay slot: before and after its slot instruction
#define SN_SLOT_BEGIN(TARGET) c.SetupDelaySlot(TARGET);
#define SN_SLOT_END()           \
    c.m_delaySlot = false;      \
    c.m_intrFlags.pending = c.INTC.pending.level > c.SR.ILevel || c.m_yieldRequest;
