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
// A rewrite that hands the native interface what it draws (video/native.h: Lift::handsOver) is run dry where it does
// not run (CPS3RECOMP_NO_REWRITES, or the arcade timing without Game::rewritesNeeded: the game's own code runs, for
// its exact cycles): before the game's code, charged nothing, as a hook is, on a copy of the registers and an overlay
// of memory that keeps its writes to itself and drops those to the chips (DryRun), what it calls by native forms of
// those routines (AddNativeForm). What it hands over is handed over; nothing else is changed. So the interface gets
// the same with the board's timing as with the rewrites (docs/NATIVE.md, "Arcade timing").
// CPS3RECOMP_NO_DRY_RUNS=1 leaves the dry runs out (to see that the native check finds that).
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
#include <unordered_map>

namespace cps3recomp {

// A rewrite's dry run (Lift::handsOver): the registers it works on, and the bytes it has written (main RAM's and the
// stack's, by address; what it writes to the chips is dropped), read back by it before the guest's memory
struct DryRun {
    uint32_t r[16] = {};
    uint32_t pc = 0, pr = 0, gbr = 0;
    bool t = false;
    std::unordered_map<uint32_t, uint8_t> bytes;
    uint64_t cycles = 0; // (what it said it took: not charged)
};

// The CPU as a lifted routine sees it: the guest's own registers and memory, or a dry run's (DryRun)
class Guest {
public:
    explicit Guest(machine::SH2 &cpu)
        : m_cpu(cpu) {}
    Guest(machine::SH2 &cpu, DryRun &dry)
        : m_cpu(cpu), m_dry(&dry) {}
    // Whether this is a dry run (nothing it writes stays; its calls are to native forms)
    bool Dry() const {
        return m_dry != nullptr;
    }
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
    // waits for something cannot be called. For rewrites that call routines not rewritten yet. (In a dry run: the
    // routine's native form, AddNativeForm.)
    void Call(uint32_t address);
    // Leaves the routine as rts does; `cycles` is the time it is said to have taken
    void Return(uint64_t cycles = 100);
    // The board this CPU is on: for rewrites that hand the native interface what they draw (video/native.h)
    machine::Machine &Board();

private:
    machine::SH2 &m_cpu;
    DryRun *m_dry = nullptr;
    uint32_t DryRead(uint32_t address, int bytes);
    void DryWrite(uint32_t address, uint32_t value, int bytes);
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
    // A rewrite that hands the native interface what it draws: run dry where it does not run (above)
    bool handsOver = false;
    // The boards it is attached to: the game's (the first), and (kStageBoards) the second boards a game's sources run
    // beside it (runtime/system.h's stage boards), where it is attached (kept or not, so the stage game runs the same
    // either way) only if its code is there (its hash, when the board starts), and only run dry (a rewrite) or as it is
    // (a hook), the dry runs only while the interface takes what is handed over: what
    // the stage game draws handed to that board's own interface, nothing of that game changed but its calls to it
    // (called through the dispatcher, as any lift's)
    uint8_t boards = kFirstBoard;
    static constexpr uint8_t kFirstBoard = 1, kStageBoards = 2;
};
constexpr bool kHandsOver = true; // (Lift::handsOver, for the tables)

// FNV-1a, 64 bits
uint64_t LiftHash(const uint8_t *data, size_t size);

// Called by the game's sources (RegisterGame), before the dispatcher is attached
void RegisterLifts(const Lift *lifts, size_t count);
// A hook: `fn` runs whenever the CPU arrives at `address` with this code there, then the game's code goes on
void AddHook(const char *name, uint32_t address, uint32_t size, uint64_t hash, void (*fn)(Guest &guest));
// A routine's native form for the dry runs: what Guest::Call runs in a dry run in place of the routine at `address`
// (on the dry run's registers and memory: it does what the routine does to them)
void AddNativeForm(uint32_t address, void (*fn)(Guest &guest));

} // namespace cps3recomp
