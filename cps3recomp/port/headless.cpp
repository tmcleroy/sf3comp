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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
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

namespace {
    std::map<std::string, std::string> ReadCfg(const std::string &path) {
        std::map<std::string, std::string> cfg;
        FILE *f = fopen(path.c_str(), "r");
        char line[4096];
        while (f && fgets(line, sizeof(line), f)) {
            std::string text = line;
            if (text.empty() || text[0] == '#' || text.find('=') == std::string::npos) {
                continue;
            }
            auto trim = [](std::string t) {
                t.erase(0, t.find_first_not_of(" \t\r\n"));
                t.erase(t.find_last_not_of(" \t\r\n") + 1);
                return t;
            };
            const size_t eq = text.find('=');
            cfg[trim(text.substr(0, eq))] = trim(text.substr(eq + 1));
        }
        if (f) {
            fclose(f);
        }
        return cfg;
    }
    // An object of the stage: a tilemap's lines, or an object with a stage's colours (palettes 0x80 and up: seen in 3rd
    // Strike and 2nd Impact; the fighters' are below, their shadows 0 with blending)
    bool StageObject(const machine::Machine &m, uint32_t object) {
        const uint32_t *list = m.SpriteList();
        return (list[object * 4 + 2] & 3) == 0 || (list[object * 4] & 0x1FF) >= 0x80;
    }
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
    // (--groups FRAME:prefix: at that frame, a picture of each group of the object list alone, for finding which
    // groups are the stage and which the fighters)
    uint64_t groupsAt = ~0ull, ramAt = ~0ull;
    // (--stage-cfg games/<id>/game.cfg --stage-rom SET: an experiment for the definitive version: that game run beside
    // this one, its camera following this one's, and the pictures made of its stage with this game's fighters over it)
    std::string stageCfg, stageRom;
    std::string ramPath; // (--dump-ram FRAME:file: main RAM after that frame)
    std::string groupsPrefix;
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
        } else if (a == "--groups") {
            const char *colon = strchr(v, ':');
            if (colon) {
                groupsAt = strtoull(v, nullptr, 10);
                groupsPrefix = colon + 1;
            }
            i++;
        } else if (a == "--dump-ram") {
            const char *colon = strchr(v, ':');
            if (colon) {
                ramAt = strtoull(v, nullptr, 10);
                ramPath = colon + 1;
            }
            i++;
        } else if (a == "--stage-cfg") {
            stageCfg = v;
            i++;
        } else if (a == "--stage-rom") {
            stageRom = v;
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
    std::unique_ptr<RomSet> stageSet;
    std::unique_ptr<machine::Machine> stage;
    Renderer stageRenderer;
    if (!stageCfg.empty()) {
        const auto cfg = ReadCfg(stageCfg);
        RomList stageList;
        stageList.set = cfg.count("rom_set") ? cfg.at("rom_set") : "";
        stageList.parent = cfg.count("rom_parent") ? cfg.at("rom_parent") : "";
        stageList.bios = ParseRomFiles(cfg.count("bios") ? cfg.at("bios").c_str() : "");
        for (int n = 1; n <= 6; n++) {
            const std::string key = "simm" + std::to_string(n);
            stageList.simm[n] = ParseRomFiles(cfg.count(key) ? cfg.at(key).c_str() : "");
        }
        stageList.programEnciphered = !cfg.count("program_enciphered") || cfg.at("program_enciphered") != "0";
        stageSet = std::make_unique<RomSet>();
        if (!LoadRomSet(stageRom, stageList, *stageSet, error)) {
            fprintf(stderr, "the stage's set: %s\n", error.c_str());
            return 1;
        }
        stage = std::make_unique<machine::Machine>(*stageSet);
        stage->Reset();
    }
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
        if (stage) {
            // (the same made-up play takes it into a fight; its camera then follows this game's, from one stage's ends to
            // the other's: words at 0x020221CC/D4/E4 and ends at 0x02022218 in 2nd Impact, 0x02026CA8/B0/C0 and 0x02026CF4
            // in 3rd Strike)
            stage->inputs = ~inputs;
            stage->extra = ~extra;
            auto word = [](uint8_t *ram, uint32_t at) { return static_cast<int16_t>((ram[at & 0x7FFFF] << 8) | ram[(at + 1) & 0x7FFFF]); };
            auto setWord = [](uint8_t *ram, uint32_t at, int v) {
                ram[at & 0x7FFFF] = static_cast<uint8_t>(v >> 8);
                ram[(at + 1) & 0x7FFFF] = static_cast<uint8_t>(v);
            };
            const int camera = word(m.mainRam, 0x02026CB0), right = word(m.mainRam, 0x02026CF4), left = word(m.mainRam, 0x02026CF8);
            const int theirRight = word(stage->mainRam, 0x02022218), theirLeft = word(stage->mainRam, 0x0202221C);
            if (right > left && theirRight > theirLeft) {
                const int theirs = theirLeft + (camera - left) * (theirRight - theirLeft) / (right - left);
                for (const uint32_t at : {0x020221CCu, 0x020221D4u, 0x020221E4u}) {
                    setWord(stage->mainRam, at, theirs);
                }
            }
            stage->RunFrame();
        }
        if (getenv("CPS3RECOMP_TRACE_PC")) {
            fprintf(stderr, "frame %llu PC %08X PR %08X SR %03X pending %u/%u ext %u\n", static_cast<unsigned long long>(frame), m.Cpu().PC, m.Cpu().PR, m.Cpu().SR.u32,
                    m.Cpu().INTC.pending.level, m.Cpu().m_intrFlags.pending, m.Cpu().INTC.externalLevel);
        }
        pcm.EndFrame(wavPath.empty() ? nullptr : &audio);
        const bool shot = shots.count(frame) || (shotsEvery && frame % shotsEvery == 0);
        if ((shot || hash) && stage) {
            stageRenderer.drawGroup = [](void *b, uint16_t, uint32_t object) { return StageObject(*static_cast<machine::Machine *>(b), object); };
            stageRenderer.drawGroupContext = stage.get();
            stageRenderer.text = false;
            stageRenderer.Draw(*stage, pixels, width, height);
            renderer.drawGroup = [](void *b, uint16_t, uint32_t object) { return !StageObject(*static_cast<machine::Machine *>(b), object); };
            renderer.drawGroupContext = &m;
            renderer.overlay = true;
            renderer.Draw(m, pixels, width, height);
            renderer.overlay = false;
            renderer.drawGroup = nullptr;
        } else if (shot || hash) {
            renderer.Draw(m, pixels, width, height);
        }
        if (shot) {
            const std::string path = shots.count(frame) ? shots[frame] : shotsPrefix + std::to_string(frame) + ".png";
            WritePng(path.c_str(), pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
        if (frame == ramAt) {
            if (FILE *f = fopen(ramPath.c_str(), "wb")) {
                fwrite(m.mainRam, 1, 0x80000, f);
                fclose(f);
            }
        }
        if (frame == groupsAt) {
            std::vector<uint16_t> groups(m.SpriteGroups(), m.SpriteGroups() + m.SpriteCount());
            std::sort(groups.begin(), groups.end());
            groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
            static uint16_t only = 0;
            for (const uint16_t group : groups) {
                only = group;
                renderer.drawGroup = [](void *, uint16_t g, uint32_t) { return g == only; };
                renderer.text = false;
                std::vector<uint32_t> one;
                int w = 0, h = 0;
                renderer.Draw(m, one, w, h);
                const std::string path = groupsPrefix + std::to_string(group) + ".png";
                WritePng(path.c_str(), one.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
            renderer.drawGroup = nullptr;
            renderer.text = true;
            fprintf(stderr, "groups at frame %llu: %zu\n", static_cast<unsigned long long>(frame), groups.size());
            for (const uint16_t group : groups) {
                const uint32_t head = m.SpriteRam(group * 4), position = m.SpriteRam(group * 4 + 1), settings = m.SpriteRam(group * 4 + 2);
                uint32_t count = 0, tilemaps = 0;
                for (uint32_t n = 0; n < m.SpriteCount(); n++) {
                    if (m.SpriteGroups()[n] == group) {
                        count++;
                        tilemaps += (m.SpriteList()[n * 4 + 2] & 3) == 0;
                    }
                }
                fprintf(stderr, "group %3u: head %08X position %08X settings %08X scroll %u, %u objects (%u tilemap lines)\n", group, head,
                        position, settings, head >> 28, count, tilemaps);
            }
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
