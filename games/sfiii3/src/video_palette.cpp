// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's palette, written again as C++ (rewrites: runtime/lift.h), the first piece of the native video
// (cps3recomp/docs/NATIVE.md, games/sfiii3/NATIVE.md): the routines that put colours in the board's colour RAM hand
// the same to the native interface (video/native.h) as they write the board's registers and memory. With
// CPS3RECOMP_NATIVE_CHECK=1 the board compares, every frame, what it saw with what these handed over
// (games/sfiii3/checknative.sh).
//
// How the colours get there. Almost all of them come by the video chip's palette DMA, from the graphics SIMMs, faded
// as they come (a screen's fade in and out is the same colours brought in again with another fade). The game asks for
// one with 0x06137DF0: started at once when the chip's DMAs are idle (the status word 0x040C000C, bits 1-2) and no
// transfer of the game's is under way (0x0207603C, the DMA queue's state: 0 none, 1 a character DMA, 2 a palette DMA
// from the queue, 3 one started at once), else put in the frame's queue (sixteen bytes each, up to 32 a frame: the
// registers' words as they will be written). The DMA-done interrupt (level 10: 0x060004D0, 0x061380E4) and the
// vertical blank (0x0613809A) start the next of the queue with 0x06138168 (character DMAs first: r4 1 a list of
// character transfers, 2 a palette transfer). The text layer's colours are the only ones the game writes itself
// (0x061388F0, sixteen a set, from a table, its base set by 0x061388DA). Nothing reads colour RAM back.
//
// The queues: two of each kind, by the set the game fills (0x02078A38) and the set the DMAs drain (0x02078A3C). For
// each: the palette requests (0x0207804C, 0x200 bytes a set), their count (0x0207844C) and the next to start
// (0x02078454); the character transfers (0x02076044, 0xFFC bytes a set, twelve bytes each: the chip's list entries),
// their count (0x0207803C) and the next (0x02078044); a log of the kinds asked for, in order (0x02078464, 0x2EA bytes
// a set, the place in it 0x0207845C).
#include "lift.h"
#include "machine.h"

#include <iterator>
#include <vector>

namespace cps3recomp {

namespace {
    using machine::Machine;

    // --- the board's ---
    constexpr uint32_t kDmaStatus = 0x240C000C; // word: bit 0 the object list's DMA under way, 1 a character DMA's, 2 a palette DMA's
    constexpr uint32_t kPaletteSource = 0x240C00A0; // words: the palette DMA's source (two), destination (two), fade (two),
                                                    // length (two, the last starting it with bit 1)
    constexpr uint32_t kCharListLow = 0x240C0096;   // word: the character DMA's list, in longs of character RAM (low 16 bits)
    constexpr uint32_t kCharListHigh = 0x240C0098;  // word: its bits 16-21; bit 6 starts it
    constexpr uint32_t kCharWindow = 0x24100000;    // the CPU's window on character RAM (its bank: kSetCharBank)
    constexpr uint32_t kColourRam = 0x24080000;

    // --- the game's ---
    constexpr uint32_t kDmaState = 0x0207603C;   // long: 0 idle, 1 a character DMA, 2 a palette DMA from the queue, 3 one started at once
    constexpr uint32_t kFillSet = 0x02078A38;    // long: the queues' set requests go to
    constexpr uint32_t kDrainSet = 0x02078A3C;   // long: the set the DMAs take from
    constexpr uint32_t kPaletteQueue = 0x0207804C; // 16 bytes a request, 0x200 a set
    constexpr uint32_t kPaletteCount = 0x0207844C; // longs, by set: requests waiting
    constexpr uint32_t kPaletteNext = 0x02078454;  // longs, by set: the next to start
    constexpr uint32_t kPaletteMost = 32;
    constexpr uint32_t kCharQueue = 0x02076044;   // 12 bytes a transfer, 0xFFC a set
    constexpr uint32_t kCharCount = 0x0207803C;   // longs, by set
    constexpr uint32_t kCharNext = 0x02078044;    // longs, by set
    constexpr uint32_t kCharMost = 0x155;         // transfers in one list
    constexpr uint32_t kKindLog = 0x02078464;     // words, 0x2EA bytes a set: 1 a character request, 2 a palette request
    constexpr uint32_t kKindPlace = 0x0207845C;   // longs, by set
    constexpr uint32_t kCharListAt = 0x02007EE0;  // long: where in character RAM the list is made (its low 20 bits)
    constexpr uint32_t kCharListBank = 0x02007EE4; // long: the bank it is in
    constexpr uint32_t kTextColourBase = 0x02078CE4; // long: where the text layer's colours go (in the CPU's window on colour RAM)
    constexpr uint32_t kSetCharBank = 0x0613AA5E;  // r4: the CPU's character RAM bank set (the last kept)
    constexpr uint32_t kRestoreCharBank = 0x0613AAB0; // ... and the one before put back (r0: the bank)
    constexpr uint32_t kCharBank = 0x02079C96;        // word: the CPU's character RAM bank now (its low 3 bits)

    uint32_t Signed(int32_t value) {
        return static_cast<uint32_t>(value);
    }
    uint32_t Long(Guest &g, uint32_t at) {
        return g.Read32(at);
    }
    // A set's long in a table of longs by set
    uint32_t BySet(uint32_t table, uint32_t set) {
        return table + set * 4;
    }
    // The palette registers' eight words as the game writes them (and its queue keeps them: source high, source low,
    // destination high, destination low, length low, length high, fade's red and green, fade's blue), in the order of
    // the queue's entry
    struct PaletteWords {
        uint16_t sourceLow, sourceHigh, destLow, destHigh, lengthLow, lengthHigh, redGreen, blue;
    };
    // ... from what the game asks for: the source and destination as byte addresses (of the graphics and of colour RAM),
    // the length in bytes, the fade's three bytes
    PaletteWords WordsOf(uint32_t source, uint32_t dest, uint32_t length, uint16_t red, uint16_t green, uint16_t blue) {
        PaletteWords w;
        w.sourceLow = static_cast<uint16_t>(source >> 1);
        w.sourceHigh = static_cast<uint16_t>(((source >> 1) & 0x07FF0000u) >> 16);
        w.destLow = static_cast<uint16_t>(dest >> 1);
        w.destHigh = static_cast<uint16_t>(((dest >> 1) & 0x00010000u) >> 16);
        w.lengthLow = static_cast<uint16_t>(length >> 1);
        w.lengthHigh = static_cast<uint16_t>(((length >> 1) & 0x00010000u) >> 16);
        w.redGreen = static_cast<uint16_t>(((red << 8) | green) & 0x7F7F);
        w.blue = static_cast<uint16_t>(blue & 0x7F);
        return w;
    }
    // The palette DMA started: its registers written (high word before low, the length's high word last with bit 1),
    // and the same handed to the native palette
    void StartPalette(Guest &g, const PaletteWords &w) {
        g.Write16(kPaletteSource + 0x0, w.sourceHigh);
        g.Write16(kPaletteSource + 0x2, w.sourceLow);
        g.Write16(kPaletteSource + 0x4, w.destHigh);
        g.Write16(kPaletteSource + 0x6, w.destLow);
        g.Write16(kPaletteSource + 0x8, w.redGreen);
        g.Write16(kPaletteSource + 0xA, w.blue);
        g.Write16(kPaletteSource + 0xC, w.lengthLow);
        g.Write16(kPaletteSource + 0xE, static_cast<uint16_t>(w.lengthHigh | 2));
        // (the transfer in the native interface's terms: the source a word of the graphics stream, which the chip's
        // address has 0x200000 words further on; the length's bit 16 from the last word)
        native::PaletteTransfer t;
        t.source = ((((static_cast<uint32_t>(w.sourceHigh) << 16) | w.sourceLow) << 1) - 0x400000u) >> 1;
        t.dest = (static_cast<uint32_t>(w.destHigh) << 16) | w.destLow;
        t.length = w.lengthLow | ((w.lengthHigh & 1u) << 16);
        t.fade = (static_cast<uint32_t>(w.redGreen) << 16) | w.blue;
        g.Board().NativePaletteTransfer(t);
    }

    // 06137DF0: a palette transfer asked for: r4 its source (a byte address in the graphics, as the chip counts it),
    // r5 its destination (a byte offset in colour RAM), r6 its length in bytes, r7 the fade's green byte and the two
    // longs on the stack its red and blue. Started at once when nothing is under way, else queued for the DMA-done
    // interrupt to start (r0 -1: the queue is full and it is dropped; 0 otherwise).
    bool AskPalette(Guest &g) {
        const uint32_t sp = g.R(15);
        const PaletteWords w = WordsOf(g.R(4), g.R(5), g.R(6), g.Read16(sp + 2), static_cast<uint16_t>(g.R(7)), g.Read16(sp + 6));
        const uint16_t status = g.Read16(kDmaStatus);
        if (!(status & 6) && Long(g, kDmaState) == 0) {
            g.Write32(kDmaState, 3);
            StartPalette(g, w);
            g.R(0) = 0;
            g.Return(90);
            return true;
        }
        const uint32_t set = Long(g, kFillSet);
        const uint32_t waiting = Long(g, BySet(kPaletteCount, set));
        if (waiting >= kPaletteMost) {
            g.R(0) = Signed(-1);
            g.Return(40);
            return true;
        }
        const uint32_t entry = kPaletteQueue + (set << 9) + (waiting << 4);
        g.Write16(entry + 0x0, w.sourceLow);
        g.Write16(entry + 0x2, w.sourceHigh);
        g.Write16(entry + 0x4, w.destLow);
        g.Write16(entry + 0x6, w.destHigh);
        g.Write16(entry + 0x8, w.lengthLow);
        g.Write16(entry + 0xA, w.lengthHigh);
        g.Write16(entry + 0xC, w.redGreen);
        g.Write16(entry + 0xE, w.blue);
        g.Write32(BySet(kPaletteCount, set), waiting + 1);
        // (the kind logged at the log's place, which this does not move on)
        const uint32_t place = Long(g, BySet(kKindPlace, set));
        const uint32_t log = kKindLog + Signed(static_cast<int16_t>(static_cast<int16_t>(set) * 0x2EA));
        g.Write16(log + place * 2, 2);
        g.R(0) = 0;
        g.Return(110);
        return true;
    }

    // 06138168: the next DMA of the draining set started, r4 its kind (1: the character transfers waiting, as many as a
    // list holds, made into the chip's list in character RAM; 2: the next palette transfer). r0 left as the game's code
    // leaves it (1: the bank put back; 2: the address of the next-palette table; other kinds: r4).
    bool StartNext(Guest &g) {
        const uint32_t kind = g.R(4);
        if (kind == 1) {
            const uint32_t set = Long(g, kDrainSet);
            const uint32_t waiting = Long(g, BySet(kCharCount, set));
            const int32_t count = static_cast<int32_t>(waiting > kCharMost ? kCharMost : waiting);
            const uint32_t list = (Long(g, kCharListAt) & 0x000FFFFF) + kCharWindow;
            g.R(15) -= 32; // (the routine's frame: pr, macl and 24 bytes)
            g.R(4) = Long(g, kCharListBank);
            g.Call(kSetCharBank);
            // (the list written through the CPU's window: its place in character RAM's 8 MB, for the native textures)
            Machine &board = g.Board();
            const uint32_t bank = (static_cast<uint32_t>(g.Read16(kCharBank)) & 15 & 7) << 20;
            std::vector<uint32_t> longs;
            const auto put = [&](uint32_t at, uint32_t value) {
                g.Write32(at, value);
                board.NativeCharacterWrite(bank | (at & 0xFFFFC), value, 4);
                longs.push_back(value);
            };
            int32_t i = 0;
            for (; i < count; i++) {
                const uint32_t drainSet = Long(g, kDrainSet);
                const int16_t slot = static_cast<int16_t>((Long(g, BySet(kCharNext, drainSet)) + Signed(i)) * 12);
                const int16_t setOffset = static_cast<int16_t>(static_cast<int16_t>(drainSet) * 0xFFC);
                const uint32_t from = kCharQueue + Signed(setOffset) + Signed(slot);
                const uint32_t to = list + Signed(i * 12);
                put(to, Long(g, from));
                put(to + 4, Long(g, from + 4));
                put(to + 8, Long(g, from + 8));
                g.Write32(BySet(kCharCount, drainSet), Long(g, BySet(kCharCount, drainSet)) - 1);
            }
            const uint32_t drainSet = Long(g, kDrainSet);
            g.Write32(BySet(kCharNext, drainSet), Long(g, BySet(kCharNext, drainSet)) + Signed(count));
            const uint32_t end = list + Signed(i * 12);
            put(end, 0x01000000); // (the list's end)
            put(end + 4, 0);
            put(end + 8, 0);
            const uint32_t offset = list + 0xDBF00000u; // (the list's place in character RAM)
            g.Write16(kCharListLow, static_cast<uint16_t>(offset >> 2));
            g.Write16(kCharListHigh, static_cast<uint16_t>(((offset >> 18) & 63) | 64));
            board.NativeCharacterList(longs.data(), longs.size()); // (the character DMA: its transfers made)
            g.Call(kRestoreCharBank);
            g.R(15) += 32;
            g.Return(80 + 40 * static_cast<uint64_t>(count));
            return true;
        }
        if (kind == 2) {
            const uint32_t set = Long(g, kDrainSet);
            const uint32_t next = Long(g, BySet(kPaletteNext, set));
            const uint32_t entry = kPaletteQueue + (set << 9) + (next << 4);
            g.Write32(kDmaState, 2);
            PaletteWords w;
            w.sourceLow = g.Read16(entry + 0x0);
            w.sourceHigh = g.Read16(entry + 0x2);
            w.destLow = g.Read16(entry + 0x4);
            w.destHigh = g.Read16(entry + 0x6);
            w.lengthLow = g.Read16(entry + 0x8);
            w.lengthHigh = g.Read16(entry + 0xA);
            w.redGreen = g.Read16(entry + 0xC);
            w.blue = g.Read16(entry + 0xE);
            StartPalette(g, w);
            const uint32_t drainSet = Long(g, kDrainSet);
            g.Write32(BySet(kPaletteCount, drainSet), Long(g, BySet(kPaletteCount, drainSet)) - 1);
            const uint32_t again = Long(g, kDrainSet);
            g.Write32(BySet(kPaletteNext, again), Long(g, BySet(kPaletteNext, again)) + 1);
            g.R(0) = kPaletteNext;
            g.Return(80);
            return true;
        }
        g.R(0) = kind;
        g.SetT(false);
        g.Return(20);
        return true;
    }

    // 061388F0: r6 of the text layer's colours (a table of words at r5; 0xFFFF: that colour left as it is) written from
    // colour r4 / 2 of the text layer's set (kTextColourBase) on. Leaves r7 the count written over (callers read it), r4
    // and r5 past the end.
    bool TextColours(Guest &g) {
        const int32_t count = static_cast<int32_t>(g.R(6));
        uint32_t to = g.R(4) + Long(g, kTextColourBase), from = g.R(5);
        int32_t n = 0;
        uint32_t last = g.R(3);
        g.R(0) = 0xFFFF;
        g.R(1) = kTextColourBase;
        g.SetT(count > 0);
        for (; n < count; n++, from += 2, to += 2) {
            const uint16_t colour = g.Read16(from);
            last = colour;
            if (colour != 0xFFFF) {
                last = Signed(static_cast<int16_t>(colour));
                g.Write16(to, colour);
                g.Board().NativeColour((to - kColourRam) >> 1, colour);
            }
        }
        if (count > 0) {
            g.R(3) = last;
            g.SetT(true);
        } else {
            g.R(3) = Long(g, kTextColourBase);
            g.SetT(false);
        }
        g.R(4) = to;
        g.R(5) = from;
        g.R(7) = Signed(n);
        g.Return(10 + 9 * static_cast<uint64_t>(n > 0 ? n : 0));
        return true;
    }

    const Lift kLifts[] = {
        {"AskPalette", 0x06137DF0, 0x1B8, 0xC2DBCF8B57239313ull, AskPalette, true, 0x100FE, nullptr, kHandsOver},
        {"StartNextDma", 0x06138168, 0x230, 0xD09F1A5FC6A6A73Full, StartNext, true, 0x100FE, nullptr, kHandsOver},
        {"TextColours", 0x061388F0, 0x28, 0x93363361932173FCull, TextColours, true, 0x10B7F, nullptr, kHandsOver},
    };
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterVideoPalette() {
    RegisterGameLifts(kLifts, std::size(kLifts));
}

} // namespace cps3recomp
