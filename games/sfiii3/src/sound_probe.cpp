// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// A debugging aid for finding 3rd Strike's sound requests (the definitive version's music: DEFINITIVE.md):
// SFIII3_SOUND_PROBE=1 prints each call of the routines the game's code calls in its sound driver (found from a call
// graph), with their arguments, the caller and the frame.
#include "game.h"
#include "lift.h"
#include "system.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace cps3recomp {

namespace {
    void Probe(const char *name, Guest &g) {
        const Machine *m = TheMachine();
        fprintf(stderr, "sound %s r4 %08X r5 %08X r6 %08X r7 %08X from %08X frame %llu\n", name, g.R(4), g.R(5), g.R(6), g.R(7), g.PR(),
                static_cast<unsigned long long>(m ? m->frame : 0));
    }
} // namespace

namespace {
    bool Skip(Guest &g) {
        g.Return(4); // (the experiment: the routine does nothing)
        return true;
    }
    Lift g_skip[1];
} // namespace

namespace {
    // (SFIII3_SOUND_RECORDS=1: which voice records (r14) drive which of the chip's voices (r13), counted before and
    // after frame 2400, written at exit)
    struct Records {
        std::map<std::pair<uint32_t, uint32_t>, std::pair<uint64_t, uint64_t>> counts;
        ~Records() {
            for (const auto &[key, n] : counts) {
                fprintf(stderr, "record %08X voice %2u: %llu before, %llu after\n", key.first, (key.second - 0x040E0000u) / 0x20,
                        static_cast<unsigned long long>(n.first), static_cast<unsigned long long>(n.second));
            }
        }
    } g_records;
} // namespace

void RegisterSoundProbe() {
    if (getenv("SFIII3_SOUND_RECORDS")) {
        AddHook("Sound pitch", 0x0613B1D8, 8, 0xBA5502CCC81A1B0Eull, [](Guest &g) {
            const Machine *m = TheMachine();
            auto &n = g_records.counts[{g.R(14), g.R(13) & 0x1FFFFFFFu}];
            (m && m->frame >= 2400 ? n.second : n.first)++;
        });
    }
    // (SFIII3_SOUND_SKIP=ADDRESS:HASH: that routine skipped, to see what goes quiet)
    if (const char *skip = getenv("SFIII3_SOUND_SKIP")) {
        g_skip[0] = {"Skipped", static_cast<uint32_t>(strtoul(skip, nullptr, 16)), 8, strtoull(strchr(skip, ':') + 1, nullptr, 16), Skip, false, 0};
        RegisterLifts(g_skip, 1);
    }
    // (SFIII3_SONG_PROBE=1: each start of a song asked of the driver, 0x061394D0 (r4 the song), with its number and who called)
    if (getenv("SFIII3_SONG_PROBE")) {
        AddHook("Song start", 0x061394D0, 8, 0x1BF90A4B22ACB049ull, [](Guest &g) { Probe("song", g); });
    }
    if (!getenv("SFIII3_SOUND_PROBE")) {
        return;
    }
    AddHook("Sound BA0", 0x06138BA0, 8, 0x9FE4A81365EB2C03ull, [](Guest &g) { Probe("BA0", g); });
    AddHook("Sound A9B0", 0x0613A9B0, 8, 0x67CD4951B783CE6Cull, [](Guest &g) { Probe("A9B0", g); });
    AddHook("Sound A182", 0x0613A182, 8, 0x3B8920AB130A91CBull, [](Guest &g) { Probe("A182", g); });
    AddHook("Sound 87DE", 0x061387DE, 8, 0x6A53100EFB6B5CCCull, [](Guest &g) {
        Probe("87DE", g);
        if (getenv("SFIII3_SOUND_MUTE_87DE")) {
            g.R(5) = 0; // (the experiment: its tracks given nothing to play)
        }
    });
}

} // namespace cps3recomp
