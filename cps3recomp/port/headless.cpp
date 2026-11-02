// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The game with no window, for checks and pictures:
//   <game>-headless ROMSET [--frames N] [--shot FRAME:file.png]... [--shots EVERY:prefix] [--hold FIRST-LAST:BUTTONS]...
//                   [--hash] [--cpu native|free|interp] [--record-blocks FILE] [--stats] [--check N]
// BUTTONS: a list of coin1 coin2 start1 start2 service test, p1up p1down p1left p1right p1lp p1mp p1hp p1lk p1mk p1hk,
// and p2 alike, joined with +.
#include "game_cfg.h"
#include "machine.h"
#include "png_write.h"
#include "renderer.h"
#include "romset.h"
#include "pcm.h"
#include "recomp.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <zlib.h>

using namespace cps3recomp;

namespace {
    // (which word and bit each button is: MAME's CPS-3 inputs; a pressed button reads 0)
    struct Button {
        bool extra;
        uint32_t bit;
    };
    const std::map<std::string, Button> kButtons = {
        {"p1up", {false, 0x1}}, {"p1down", {false, 0x2}}, {"p1left", {false, 0x4}}, {"p1right", {false, 0x8}},
        {"p1lp", {false, 0x10}}, {"p1mp", {false, 0x20}}, {"p1hp", {false, 0x40}},
        {"p2up", {false, 0x100}}, {"p2down", {false, 0x200}}, {"p2left", {false, 0x400}}, {"p2right", {false, 0x800}},
        {"p2lp", {false, 0x1000}}, {"p2mp", {false, 0x2000}}, {"p2hp", {false, 0x4000}},
        {"service", {false, 0x10000}}, {"test", {false, 0x20000}}, {"coin1", {false, 0x1000000}}, {"coin2", {false, 0x2000000}},
        {"p2hk", {false, 0x4000000}}, {"start1", {false, 0x10000000}}, {"start2", {false, 0x20000000}},
        {"p1hk", {true, 0x20000}}, {"p1mk", {true, 0x40000}}, {"p1lk", {true, 0x80000}}, {"p2lk", {true, 0x100000}},
        {"p2mk", {true, 0x200000}},
    };
    struct Hold {
        uint64_t first, last;
        uint32_t inputs = 0, extra = 0;
    };
} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROMSET [--frames N] [--shot FRAME:file.png] [--shots EVERY:prefix] [--hold FIRST-LAST:BUTTONS] [--hash]\n", argv[0]);
        return 1;
    }
    const char *romPath = argv[1];
    uint64_t frames = 600, shotsEvery = 0;
    std::string shotsPrefix, wavPath;
    std::map<uint64_t, std::string> shots;
    std::vector<Hold> holds;
    bool hash = false;
    std::string cpu = "native";
    Options recompOptions;
    std::string dumpDir;
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : "";
        if (a == "--frames") {
            frames = strtoull(v, nullptr, 10);
            i++;
        } else if (a == "--shot") {
            const char *colon = strchr(v, ':');
            if (colon) {
                shots[strtoull(v, nullptr, 10)] = colon + 1;
            }
            i++;
        } else if (a == "--shots") {
            const char *colon = strchr(v, ':');
            if (colon) {
                shotsEvery = strtoull(v, nullptr, 10);
                shotsPrefix = colon + 1;
            }
            i++;
        } else if (a == "--wav") {
            wavPath = v;
            i++;
        } else if (a == "--hold") {
            Hold h;
            char names[512] = {};
            unsigned long long first = 0, last = 0;
            if (sscanf(v, "%llu-%llu:%511s", &first, &last, names) == 3) {
                h.first = first;
                h.last = last;
                for (char *name = strtok(names, "+"); name; name = strtok(nullptr, "+")) {
                    const auto it = kButtons.find(name);
                    if (it == kButtons.end()) {
                        fprintf(stderr, "no button %s\n", name);
                        return 1;
                    }
                    (it->second.extra ? h.extra : h.inputs) |= it->second.bit;
                }
                holds.push_back(h);
            }
            i++;
        } else if (a == "--dump") {
            dumpDir = v;
            i++;
        } else if (a == "--hash") {
            hash = true;
        } else if (a == "--cpu") {
            cpu = v;
            i++;
        } else if (a == "--record-blocks") {
            recompOptions.recordPath = v;
            i++;
        } else if (a == "--stats") {
            recompOptions.stats = true;
        } else if (a == "--check") {
            recompOptions.checkEvery = static_cast<unsigned>(strtoul(v, nullptr, 10));
            i++;
        }
    }
    if (cpu != "native" && cpu != "free" && cpu != "interp") {
        fprintf(stderr, "--cpu: native (the translated code; default), free (the interpreter run as the translation runs: its "
                        "reference) or interp (the interpreter alone, no dispatcher)\n");
        return 1;
    }
    recompOptions.mode = cpu == "free" ? Mode::Free : Mode::Native;

    RomList list;
    list.set = GAME_ROM_SET;
    list.parent = GAME_ROM_PARENT;
    list.bios = ParseRomFiles(GAME_BIOS);
    const char *simms[7] = {"", GAME_SIMM1, GAME_SIMM2, GAME_SIMM3, GAME_SIMM4, GAME_SIMM5, GAME_SIMM6};
    for (int s = 1; s <= 6; s++) {
        list.simm[s] = ParseRomFiles(simms[s]);
    }
    list.programEnciphered = std::string(GAME_PROGRAM_ENCIPHERED) != "0";
    RomSet rom;
    std::string error;
    const auto loadStart = std::chrono::steady_clock::now();
    if (!LoadRomSet(romPath, list, rom, error)) {
        fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    fprintf(stderr, "ROM set read in %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - loadStart).count());

    if (!dumpDir.empty()) {
        // The deciphered program (bios.bin, program.bin: big-endian, as the CPU reads it), for disassembly. Derived
        // from the game: never committed.
        auto dump = [&](const char *name, const std::vector<uint32_t> &words) {
            FILE *f = fopen((dumpDir + "/" + name).c_str(), "wb");
            for (uint32_t w : words) {
                const uint8_t b[4] = {static_cast<uint8_t>(w >> 24), static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w)};
                fwrite(b, 1, 4, f);
            }
            fclose(f);
        };
        dump("bios.bin", rom.bios);
        dump("program.bin", rom.program);
    }
    machine::Machine m(rom);
    m.Reset();
    if (cpu != "interp") {
        Attach(m, recompOptions);
    }
    Renderer renderer;
    Pcm pcm(m);
    std::vector<int16_t> audio;
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t frame = 0; frame < frames; frame++) {
        uint32_t inputs = 0, extra = 0;
        for (const Hold &h : holds) {
            if (frame >= h.first && frame <= h.last) {
                inputs |= h.inputs;
                extra |= h.extra;
            }
        }
        m.inputs = ~inputs;
        m.extra = ~extra;
        m.RunFrame();
        if (getenv("CPS3RECOMP_TRACE_PC")) {
            fprintf(stderr, "frame %llu PC %08X PR %08X SR %03X pending %u/%u ext %u\n", static_cast<unsigned long long>(frame), m.Cpu().PC, m.Cpu().PR, m.Cpu().SR.u32,
                    m.Cpu().INTC.pending.level, m.Cpu().m_intrFlags.pending, m.Cpu().INTC.externalLevel);
        }
        pcm.EndFrame(wavPath.empty() ? nullptr : &audio);
        const bool shot = shots.count(frame) || (shotsEvery && frame % shotsEvery == 0);
        if (shot || hash) {
            renderer.Draw(m, pixels, width, height);
        }
        if (shot) {
            const std::string path = shots.count(frame) ? shots[frame] : shotsPrefix + std::to_string(frame) + ".png";
            WritePng(path.c_str(), pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
        if (hash) {
            const uint32_t crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef *>(pixels.data()), static_cast<uInt>(pixels.size() * 4)));
            const uint32_t ram = static_cast<uint32_t>(crc32(0, m.mainRam, 0x80000));
            printf("%llu %08x %08x\n", static_cast<unsigned long long>(frame), ram, crc);
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    fprintf(stderr, "%llu frames in %.2f s (%.1f fps), PC %08X\n", static_cast<unsigned long long>(frames), seconds, frames / seconds, m.Cpu().PC);
    if (!wavPath.empty()) {
        WriteWav(wavPath.c_str(), audio, Pcm::kRate);
    }
    if (!Finish()) {
        return 2; // (the check found differences)
    }
    return 0;
}
