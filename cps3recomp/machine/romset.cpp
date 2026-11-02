// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp). The zip reader is cps2recomp's.
#include "romset.h"

#include <sys/stat.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace cps3recomp {

std::vector<RomFile> ParseRomFiles(const char *text) {
    std::vector<RomFile> files;
    std::istringstream in(text ? text : "");
    std::string item;
    while (in >> item) {
        const size_t colon = item.find(':');
        RomFile file;
        file.name = item.substr(0, colon);
        if (colon != std::string::npos) {
            file.crc = static_cast<uint32_t>(strtoul(item.c_str() + colon + 1, nullptr, 16));
        }
        files.push_back(file);
    }
    return files;
}

namespace {
    bool ReadWhole(const std::string &path, std::vector<uint8_t> &data) {
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data.resize(size > 0 ? static_cast<size_t>(size) : 0);
        const size_t got = fread(data.data(), 1, data.size(), f);
        fclose(f);
        return got == data.size();
    }

    // A zip file: its list of files, and one of them unpacked
    struct Zip {
        struct Entry {
            std::string name;
            uint32_t crc = 0, packed = 0, size = 0, offset = 0;
            uint16_t method = 0;
        };
        std::string path;
        std::vector<Entry> entries;

        static uint32_t Le32(const uint8_t *p) {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
        static uint16_t Le16(const uint8_t *p) {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }
        bool Open(const std::string &file) {
            path = file;
            FILE *f = fopen(file.c_str(), "rb");
            if (!f) {
                return false;
            }
            // The directory is at the end, after a record that says where
            fseek(f, 0, SEEK_END);
            const long size = ftell(f);
            const long tailSize = std::min<long>(size, 65536 + 22);
            std::vector<uint8_t> tail(static_cast<size_t>(tailSize));
            fseek(f, size - tailSize, SEEK_SET);
            bool ok = fread(tail.data(), 1, tail.size(), f) == tail.size();
            long end = -1;
            for (long at = tailSize - 22; ok && at >= 0; at--) {
                if (Le32(&tail[at]) == 0x06054B50) {
                    end = at;
                    break;
                }
            }
            if (end < 0) {
                fclose(f);
                return false;
            }
            const uint32_t count = Le16(&tail[end + 10]), dirSize = Le32(&tail[end + 12]), dirAt = Le32(&tail[end + 16]);
            std::vector<uint8_t> dir(dirSize);
            fseek(f, dirAt, SEEK_SET);
            ok = fread(dir.data(), 1, dir.size(), f) == dir.size();
            fclose(f);
            size_t at = 0;
            for (uint32_t n = 0; ok && n < count && at + 46 <= dir.size() && Le32(&dir[at]) == 0x02014B50; n++) {
                Entry e;
                e.method = Le16(&dir[at + 10]);
                e.crc = Le32(&dir[at + 16]);
                e.packed = Le32(&dir[at + 20]);
                e.size = Le32(&dir[at + 24]);
                e.offset = Le32(&dir[at + 42]);
                const size_t nameLen = Le16(&dir[at + 28]);
                if (at + 46 + nameLen > dir.size()) {
                    break;
                }
                e.name.assign(reinterpret_cast<const char *>(&dir[at + 46]), nameLen);
                entries.push_back(e);
                at += 46 + nameLen + Le16(&dir[at + 30]) + Le16(&dir[at + 32]);
            }
            return ok;
        }
        bool Read(const Entry &e, std::vector<uint8_t> &data) const {
            FILE *f = fopen(path.c_str(), "rb");
            if (!f) {
                return false;
            }
            uint8_t local[30];
            fseek(f, e.offset, SEEK_SET);
            bool ok = fread(local, 1, 30, f) == 30 && Le32(local) == 0x04034B50;
            std::vector<uint8_t> packed(e.packed);
            if (ok) {
                fseek(f, e.offset + 30 + Le16(&local[26]) + Le16(&local[28]), SEEK_SET);
                ok = fread(packed.data(), 1, packed.size(), f) == packed.size();
            }
            fclose(f);
            if (!ok) {
                return false;
            }
            if (e.method == 0) {
                data = std::move(packed);
                return data.size() == e.size;
            }
            if (e.method != 8) {
                return false;
            }
            data.resize(e.size);
            z_stream z = {};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
                return false;
            }
            z.next_in = packed.data();
            z.avail_in = static_cast<uInt>(packed.size());
            z.next_out = data.data();
            z.avail_out = static_cast<uInt>(data.size());
            const int result = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            return result == Z_STREAM_END && z.total_out == e.size;
        }
    };

    // Where the set's files are looked for: zips first (by checksum), then loose files in a folder (by name)
    struct Places {
        std::vector<Zip> zips;
        std::string folder;
    };

    bool ReadFile(const Places &places, const RomFile &file, std::vector<uint8_t> &data, std::string &error) {
        for (const Zip &zip : places.zips) {
            for (const Zip::Entry &e : zip.entries) {
                const size_t slash = e.name.find_last_of('/');
                const std::string base = slash == std::string::npos ? e.name : e.name.substr(slash + 1);
                if (file.crc ? e.crc == file.crc : base == file.name) {
                    if (zip.Read(e, data) &&
                        (!file.crc || static_cast<uint32_t>(crc32(0, data.data(), static_cast<uInt>(data.size()))) == file.crc)) {
                        return true;
                    }
                    error = "could not unpack " + e.name + " from " + zip.path;
                    return false;
                }
            }
        }
        if (places.folder.empty()) {
            error = "the set has no " + file.name;
            return false;
        }
        const std::string path = places.folder + "/" + file.name;
        if (!ReadWhole(path, data)) {
            error = "missing file: " + path;
            return false;
        }
        const uint32_t crc = static_cast<uint32_t>(crc32(0, data.data(), static_cast<uInt>(data.size())));
        if (file.crc && crc != file.crc) {
            char text[200];
            snprintf(text, sizeof(text), "%s is not the file this port is made from (CRC32 %08x, expected %08x)", file.name.c_str(), crc,
                     file.crc);
            error = text;
            return false;
        }
        return true;
    }
} // namespace

bool LoadRomSet(const std::string &path, const RomList &list, RomSet &set, std::string &error) {
    Places places;
    {
        struct stat info;
        if (stat(path.c_str(), &info) != 0) {
            error = "there is no " + path;
            return false;
        }
        std::vector<std::string> zips;
        std::string beside;
        if (S_ISDIR(info.st_mode)) {
            places.folder = path;
            beside = path;
        } else {
            zips.push_back(path);
            const size_t slash = path.find_last_of('/');
            beside = slash == std::string::npos ? "." : path.substr(0, slash);
        }
        for (const std::string &name : {list.set, list.parent}) {
            if (!name.empty() && beside + "/" + name + ".zip" != path) {
                zips.push_back(beside + "/" + name + ".zip");
            }
        }
        for (size_t n = 0; n < zips.size(); n++) {
            Zip zip;
            if (zip.Open(zips[n])) {
                places.zips.push_back(std::move(zip));
            } else if (n == 0 && places.folder.empty()) {
                error = path + " is not a zip file";
                return false;
            }
        }
    }

    std::vector<uint8_t> data;
    // The cartridge's ROM: 32-bit words, high byte first
    if (list.bios.empty() || !ReadFile(places, list.bios[0], data, error)) {
        return false;
    }
    set.biosRaw.assign(0x80000 / 4, 0xFFFFFFFF);
    for (size_t i = 0; i < set.biosRaw.size() && i * 4 + 3 < data.size(); i++) {
        set.biosRaw[i] = (static_cast<uint32_t>(data[i * 4]) << 24) | (data[i * 4 + 1] << 16) | (data[i * 4 + 2] << 8) | data[i * 4 + 3];
    }

    // The SIMMs' chips
    for (int simm = 1; simm <= 6; simm++) {
        for (size_t chip = 0; chip < list.simm[simm].size() && chip < 8; chip++) {
            if (!ReadFile(places, list.simm[simm][chip], set.chips[simm][chip], error)) {
                return false;
            }
        }
    }

    // The program SIMMs: four chips side by side make 32-bit words, the first chip's byte highest
    set.programRaw.assign(0x1000000 / 4, 0xFFFFFFFF);
    for (int simm = 1; simm <= 2; simm++) {
        if (list.simm[simm].size() < 4) {
            continue;
        }
        const size_t base = (simm - 1) * 0x800000 / 4;
        const size_t words = std::min<size_t>(set.chips[simm][0].size(), 0x800000 / 4);
        for (size_t i = 0; i < words; i++) {
            set.programRaw[base + i] = (static_cast<uint32_t>(set.chips[simm][0][i]) << 24) | (set.chips[simm][1][i] << 16) |
                                       (set.chips[simm][2][i] << 8) | set.chips[simm][3][i];
        }
    }

    // The keys, from the program itself (crypt.h); the cartridge's ROM has the same
    // (the cartridge's ROM has the same keys as the program: both are used, where both are enciphered)
    std::vector<CipherRegion> regions = {{set.biosRaw.data(), set.biosRaw.size(), 0x00000000}};
    if (list.programEnciphered) {
        regions.push_back({set.programRaw.data(), set.programRaw.size(), 0x06000000});
    }
    if (!RecoverKeys(regions, set.key, error)) {
        return false;
    }
    set.programEnciphered = list.programEnciphered;
    set.bios = set.biosRaw;
    Decipher(set.bios.data(), set.bios.size(), 0, set.key);
    set.program = set.programRaw;
    if (list.programEnciphered) {
        Decipher(set.program.data(), set.program.size(), 0x06000000, set.key);
        set.opcodes.clear();
    } else {
        set.opcodes = set.programRaw;
        Decipher(set.opcodes.data(), set.opcodes.size(), 0x06000000, set.key);
    }

    // The graphics SIMMs, pairs of chips one after the other; the video reads a pair's two chips byte by byte in turn.
    // A SIMM missing ends them (MAME's copy_from_nvram).
    set.graphics.assign(0x5000000, 0);
    size_t at = 0;
    for (int simm = 3; simm <= 6; simm++) {
        bool complete = true;
        for (size_t pair = 0; pair + 1 < 8; pair += 2) {
            if (pair + 1 >= list.simm[simm].size()) {
                complete = false;
                break;
            }
            const std::vector<uint8_t> &a = set.chips[simm][pair], &b = set.chips[simm][pair + 1];
            for (size_t i = 0; i < 0x200000 && i < a.size() && i < b.size(); i++) {
                set.graphics[at + i * 2] = a[i];
                set.graphics[at + i * 2 + 1] = b[i];
            }
            at += 0x400000;
        }
        if (!complete) {
            break;
        }
    }
    return true;
}

} // namespace cps3recomp
