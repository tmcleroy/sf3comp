// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
//
// Lifted routines and hooks: game code replaced by, or run after, hand-written C++ (docs/READABLE.md).
//
// A game's own sources (games/<id>/src) register them by address (RegisterLifts, from their RegisterGame). When the
// CPU, run by the dispatcher (--cpu native or free), arrives at such an address and the code there is the routine
// that was studied (the same hash), the C++ runs: a lift in place of the routine (it leaves with Return() as the
// original would with rts; returning false runs the original after all), a hook before it (the original then
// runs, with whatever the hook changed). Calls made by translated code to such an address come back to the
// dispatcher first, so a lift is met however its routine is called. --cpu interp, with no dispatcher, runs none.
//
// CPS3RECOMP_NO_LIFTS=1 runs the game's own code everywhere; CPS3RECOMP_NO_REWRITES=1 leaves out the rewrites only;
// CPS3RECOMP_LIFT_CHECK=n runs every nth call of each rewrite and the game's own code both, and compares what they
// leave (registers, main RAM, the CPU's cache RAM, the writes to hardware, in order); reported at the end.
//
// A lift or a hook keeps nothing of its own between calls: what it needs to carry goes in the game's save state
// (runtime/game.h, Game::state), so that rollback and run-ahead run it as the first time.
#pragma once

namespace cps3recomp::machine {
class SH2;
class Machine;
}

#include <cstddef>
#include <cstdint>
#include <string>

namespace cps3recomp {

// The CPU as a lifted routine sees it
class Guest {
public:
    explicit Guest(machine::SH2 &cpu)
        : m_cpu(cpu) {}
    uint32_t &R(unsigned n);
    uint32_t PR() const;
    uint32_t PC() const; // (in a lift or hook, before it has done anything: the routine's own address)
    uint32_t GBR() const;
    bool T() const;
    void SetT(bool t);
    uint8_t Read8(uint32_t address);
    uint16_t Read16(uint32_t address);
    uint32_t Read32(uint32_t address);
    void Write8(uint32_t address, uint8_t value);
    void Write16(uint32_t address, uint16_t value);
    void Write32(uint32_t address, uint32_t value);
    // Calls a routine of the game's as jsr does (arguments and results in the registers): it runs as the rest of the
    // game does, but at once, no interrupt taken and no time passing for the rest of the machine, so a routine that
    // waits for something cannot be called. For rewrites that call routines not rewritten yet.
    void Call(uint32_t address);
    // Leaves the routine as rts does; `cycles` is the time it is said to have taken
    void Return(uint64_t cycles = 100);
    // The board this CPU is on: for rewrites that hand the native interface what they draw (video/native.h)
    machine::Machine &Board();

private:
    machine::SH2 &m_cpu;
};

struct Lift {
    const char *name;
    uint32_t address; // where the routine starts
    uint32_t size;    // bytes of code checked (from the start)
    uint64_t hash;    // LiftHash of those bytes (tools/lifthash.py)
    // A lift: false to do nothing and run the original
    bool (*fn)(Guest &guest);
    // A rewrite: readable code that does exactly what the routine does (the same memory, registers and writes to
    // hardware, given the same before), checked by CPS3RECOMP_LIFT_CHECK
    bool rewrite = false;
    // Registers no caller reads after it (bit n: rn; bit 16: T): not compared by the check (tools/liveout.py)
    uint32_t free = 0;
    // A hook (AddHook): runs, then the original does
    void (*hookFn)(Guest &guest) = nullptr;
};

// FNV-1a, 64 bits
uint64_t LiftHash(const uint8_t *data, size_t size);

// Called by the game's sources (RegisterGame), before the dispatcher is attached
void RegisterLifts(const Lift *lifts, size_t count);
// A hook: `fn` runs whenever the CPU arrives at `address` with this code there, then the game's code goes on
void AddHook(const char *name, uint32_t address, uint32_t size, uint64_t hash, void (*fn)(Guest &guest));

} // namespace cps3recomp
