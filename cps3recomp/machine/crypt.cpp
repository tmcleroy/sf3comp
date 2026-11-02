// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "crypt.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>

namespace cps3recomp {

namespace {
    uint16_t RotateLeft(uint16_t v, int n) {
        return static_cast<uint16_t>((v << n) | (v >> (16 - n)));
    }
    // The cipher's one non-linear step, split as  RotXor(x, k) = Fixed(x) ^ (Sum(x) & k):  for a known x the key's
    // bits enter linearly, where Sum(x) has ones
    uint16_t Sum(uint16_t x) {
        return static_cast<uint16_t>(x + RotateLeft(x, 2));
    }
    uint16_t Fixed(uint16_t x) {
        const uint16_t s = Sum(x);
        return RotateLeft(s, 4) ^ (s & x);
    }
    uint16_t RotXor(uint16_t x, uint16_t k) {
        const uint16_t s = Sum(x);
        return RotateLeft(s, 4) ^ (s & (x ^ k));
    }
    uint16_t Mask16(uint32_t address, uint32_t key1, uint32_t key2) {
        address ^= key1;
        uint16_t v = static_cast<uint16_t>((address & 0xFFFF) ^ 0xFFFF);
        v = RotXor(v, static_cast<uint16_t>(key2));
        v ^= static_cast<uint16_t>((address >> 16) ^ 0xFFFF);
        v = RotXor(v, static_cast<uint16_t>(key2 >> 16));
        v ^= static_cast<uint16_t>((address & 0xFFFF) ^ (key2 & 0xFFFF));
        return v;
    }
} // namespace

uint32_t CipherMask(uint32_t address, const CipherKey &key) {
    if (key.none) {
        return 0;
    }
    const uint32_t v = Mask16(address, key.key1, key.key2);
    return v | (v << 16);
}

void Decipher(uint32_t *words, size_t count, uint32_t base, const CipherKey &key) {
    if (key.none) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        words[i] ^= CipherMask(base + static_cast<uint32_t>(i) * 4, key);
    }
}

// How the keys are found.
//
// Write a = address ^ key1 = (H << 16) | L'. The mask is
//     v1 = RotXor(L' ^ 0xFFFF, key2.lo)           (depends on the low half only)
//     m  = RotXor(v1 ^ H ^ 0xFFFF, key2.hi) ^ L' ^ key2.lo
// Words whose plaintext is a filler (zero) are stored as their mask, both halves alike, and come in long runs.
//
// 1. Take the words with one low address half L at several 64 KB blocks h. Then v1 ^ 0xFFFF ^ key1.hi is one
//    unknown Y, and  m_h = RotXor(Y ^ h, key2.hi) ^ C  with C = L ^ key1.lo ^ key2.lo. For each of the 65536 Y the
//    differences of the m_h give key2.hi's bits linearly (or show Y is wrong). That gives key2.hi, Y and C for L.
// 2. C ^ L is key1.lo ^ key2.lo for every L. Try the 65536 key1.lo: each gives key2.lo, and from each L's Y a key1.hi;
//    the right key1.lo makes them all agree.
// A filler of ones instead of zeros spoils some L of step 1; the most common answer over many L wins.
namespace {
bool RecoverKeysWith(const std::vector<CipherRegion> &regions, CipherKey &key, std::string &error, size_t runLeast, size_t samplesLeast) {
    // The samples: (block, mask) by low address half, from runs of 16 or more words whose halves are alike (and
    // which are not all one word: an unwritten flash reads as 0xFFFFFFFF, stored as it is)
    std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>> byLow;
    for (const CipherRegion &region : regions) {
        const uint32_t *w = region.words;
        for (size_t i = 0; i < region.count;) {
            if ((w[i] >> 16) != (w[i] & 0xFFFF)) {
                i++;
                continue;
            }
            size_t j = i;
            while (j < region.count && (w[j] >> 16) == (w[j] & 0xFFFF)) {
                j++;
            }
            if (j - i >= runLeast && w[i] != w[i + 1]) {
                for (size_t k = i; k < j; k++) {
                    const uint32_t address = region.base + static_cast<uint32_t>(k) * 4;
                    byLow[static_cast<uint16_t>(address)].push_back({static_cast<uint16_t>(address >> 16), static_cast<uint16_t>(w[k])});
                }
            }
            i = j;
        }
    }

    // Step 1
    std::map<std::pair<uint16_t, uint16_t>, int> votes; // (key2.hi, key1.lo ^ key2.lo)
    std::vector<std::tuple<uint16_t, uint16_t, uint16_t, uint16_t>> found; // (L, Y, key2.hi, key1.lo ^ key2.lo)
    int tried = 0;
    for (const auto &[low, samples] : byLow) {
        if (samples.size() < samplesLeast) {
            continue;
        }
        if (++tried > 1000) {
            break;
        }
        for (uint32_t y = 0; y < 0x10000; y++) {
            const uint16_t x0 = static_cast<uint16_t>(y ^ samples[0].first), s0 = Sum(x0), f0 = Fixed(x0);
            uint16_t known = 0, bits = 0;
            bool fits = true;
            for (size_t j = 1; j < samples.size() && fits; j++) {
                const uint16_t xj = static_cast<uint16_t>(y ^ samples[j].first);
                const uint16_t difference = samples[j].second ^ samples[0].second ^ Fixed(xj) ^ f0;
                const uint16_t which = Sum(xj) ^ s0;
                fits = !(difference & ~which) && !((bits ^ difference) & which & known);
                bits = static_cast<uint16_t>((bits & ~which) | (difference & which));
                known |= which;
            }
            if (!fits || known != 0xFFFF) {
                continue;
            }
            const uint16_t c = samples[0].second ^ f0 ^ (s0 & bits);
            const uint16_t d = c ^ low;
            votes[{bits, d}]++;
            found.push_back({low, static_cast<uint16_t>(y), bits, d});
        }
    }
    if (votes.empty()) {
        error = "the program has too little filler to find its cipher's keys (is it the right set?)";
        return false;
    }

    // Step 2, for the best few answers of step 1; the keys that fit the most samples win
    std::vector<std::pair<int, std::pair<uint16_t, uint16_t>>> ranked;
    for (const auto &[answer, count] : votes) {
        ranked.push_back({count, answer});
    }
    std::sort(ranked.rbegin(), ranked.rend());
    size_t total = 0;
    for (const auto &[low, samples] : byLow) {
        total += samples.size();
    }
    size_t best = 0;
    for (size_t r = 0; r < ranked.size() && r < 4; r++) {
        const auto [key2hi, d] = ranked[r].second;
        std::vector<std::pair<uint16_t, uint16_t>> lowY;
        for (const auto &[low, y, k, dd] : found) {
            if (k == key2hi && dd == d) {
                lowY.push_back({low, y});
            }
        }
        for (uint32_t key1lo = 0; key1lo < 0x10000; key1lo++) {
            const uint16_t key2lo = static_cast<uint16_t>(d ^ key1lo);
            std::map<uint16_t, int> agree;
            for (const auto &[low, y] : lowY) {
                agree[static_cast<uint16_t>(y ^ 0xFFFF ^ RotXor(static_cast<uint16_t>(low ^ key1lo ^ 0xFFFF), key2lo))]++;
            }
            for (const auto &[key1hi, count] : agree) {
                if (count * 10 < static_cast<int>(lowY.size()) * 9) {
                    continue;
                }
                const uint32_t key1 = (static_cast<uint32_t>(key1hi) << 16) | key1lo;
                const uint32_t key2 = (static_cast<uint32_t>(key2hi) << 16) | key2lo;
                size_t good = 0;
                for (const auto &[low, samples] : byLow) {
                    for (const auto &[block, mask] : samples) {
                        const uint16_t m = Mask16((static_cast<uint32_t>(block) << 16) | low, key1, key2);
                        good += m == mask || m == static_cast<uint16_t>(~mask);
                    }
                }
                if (good > best) {
                    best = good;
                    key.key1 = key1;
                    key.key2 = key2;
                    key.none = false;
                }
            }
        }
    }
    if (best * 10 < total * 9) {
        error = "no cipher keys fit the program (is it the right set?)";
        return false;
    }
    return true;
}

} // namespace

// The strictest test first (long runs of filler, many samples at each low address half); less where a program has
// little filler (New Generation's 8 MB; a cartridge's ROM alone)
bool RecoverKeys(const std::vector<CipherRegion> &regions, CipherKey &key, std::string &error) {
    static constexpr size_t kTries[][2] = {{16, 6}, {8, 4}, {4, 3}};
    for (const auto &[runLeast, samplesLeast] : kTries) {
        if (RecoverKeysWith(regions, key, error, runLeast, samplesLeast)) {
            error.clear();
            return true;
        }
    }
    // A cartridge whose CPU has lost its keys (2nd Impact runs from one: MAME's init_sfiii2) deciphers with keys of
    // nothing at all. Too little of its ROM is filler to solve them as above, so they are tried, and taken if the
    // filler there is fits them.
    size_t fits = 0, all = 0;
    for (const CipherRegion &region : regions) {
        const uint32_t *w = region.words;
        for (size_t i = 0; i < region.count;) {
            size_t j = i;
            while (j < region.count && (w[j] >> 16) == (w[j] & 0xFFFF)) {
                j++;
            }
            if (j - i >= 4 && w[i] != w[i + 1]) {
                for (size_t k = i; k < j; k++) {
                    const uint16_t m = Mask16(region.base + static_cast<uint32_t>(k) * 4, 0, 0);
                    fits += m == static_cast<uint16_t>(w[k]) || m == static_cast<uint16_t>(~w[k]);
                    all++;
                }
            }
            i = j > i ? j : i + 1;
        }
    }
    if (getenv("CPS3RECOMP_KEY_LOG")) {
        fprintf(stderr, "keys of nothing: %zu of %zu filler words fit\n", fits, all);
    }
    // (a wrong key fits about 2 in 65536 of them; fillers other than all zeros or ones do not fit either)
    if (all >= 64 && fits * 2 >= all) {
        key = {};
        error.clear();
        return true;
    }
    return false;
}

} // namespace cps3recomp
