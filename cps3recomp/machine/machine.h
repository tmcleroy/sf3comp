// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The CPS-3 board: its memory map, the video chip's memories, registers and DMAs, the settings EEPROM, the
// controls, the interrupts and the frame's timing. The SH-2 (sh2.h) runs the game on it. Facts about the board are
// from MAME's CPS-3 driver (David Haywood, Andreas Naive, Tomasz Slanina, ElSemi; BSD-3-Clause) and its notes on the
// registers; the code is this project's.
//
// Memory map (as the CPU addresses it):
//   00000000-0007FFFF  the cartridge's program ROM (deciphered)
//   02000000-0207FFFF  main RAM (sh2.cpp reads and writes it directly)
//   03000000-030003FF  "FRAM" (memory test only)
//   04000000-0407FFFF  sprite RAM: the object lists and the tilemaps
//   04080000-040BFFFF  colour RAM, 0x20000 colours of 15 bits
//   040C0000-040C00AF  the video chip's registers (scrolls, tilemaps, the screen's timing, the DMAs)
//   040E0000-040E02FF  the sound chip's registers
//   04100000-041FFFFF  character RAM, a window of 1 MB on 8 MB (bank in 040C0084)
//   04200000-043FFFFF  the graphics SIMMs' flash (bank in 040C0088)
//   05000000-0500000B  controls, coin counters
//   05001000-05001203  the settings EEPROM
//   05040000-0504FFFF  "SS" RAM: the text layer's map, line scroll and 8x8 characters (odd bytes only)
//   05050000-0505002B  the text layer's registers
//   05100000, 05110000 acknowledge the interrupts at levels 12 (vertical blank) and 10 (DMA done)
//   06000000-06FFFFFF  the program SIMMs' flash (deciphered)
//   C0000000-C00003FF  the CPU's cache used as RAM (sh2.cpp; instructions read from it are deciphered)
#pragma once

#include "flash.h"
#include "native.h"
#include "romset.h"
#include "sh2.h"

#include <array>
#include <deque>
#include <cstdint>
#include <vector>

namespace cps3recomp {
class Archive;
}

namespace cps3recomp::machine {

// The video chip's registers, as the renderer reads them (all big-endian 32-bit words, as written)
struct VideoRegisters {
    uint32 globalScroll[8] = {};  // 040C0000: the object lists' scroll pairs
    uint32 tilemap[16] = {};      // 040C0020: four tilemaps of four words
    uint32 crtc[8] = {};          // 040C0060: the screen's timing and the whole picture's zoom
};

// The program SIMMs' flash chips all read their data (none is giving its IDs): their reads are the program's bytes
extern bool g_flashPlain;

class Machine {
public:
    static constexpr uint32 kCpuClock = 25000000;            // 6.25 MHz times 4
    static constexpr uint32 kLines = 264, kVisibleLines = 224;
    // The frame: 42.954545 MHz / 6 per line, 455 clocks a line, 264 lines (59.59 Hz)
    static constexpr double kFrameRate = 42954545.0 / 6.0 / 455.0 / 264.0;
    static constexpr uint32 kCyclesPerLine = 1589; // (25 MHz / 15734 Hz)

    explicit Machine(const RomSet &rom);

    void Reset();
    // Runs one frame; the picture is ready to draw (Renderer) when it returns
    void RunFrame();

    // Controls, as the board reads them (a pressed button is 0)
    uint32 inputs = 0xFFFFFFFF, extra = 0xFFFFFFFF;

    // --- what the CPU needs (sh2.cpp) ---
    std::vector<uint8> m_mainRam;
    uint8 *mainRam;
    uint8 BusRead8(uint32 address);
    uint16 BusRead16(uint32 address);
    uint32 BusRead32(uint32 address);
    // (the timing harness's bench, CPS3RECOMP_BENCH: the time now)
    uint64 TimeNow() const {
        return Now();
    }
    void BusWrite8(uint32 address, uint8 value);
    void BusWrite16(uint32 address, uint16 value);
    void BusWrite32(uint32 address, uint32 value);
    uint16 FetchOpcode(uint32 address);
    uint16 CacheOpcode(const uint8 *cache, uint32 offset);
    bool IsEnciphered(uint32 address) const;
    uint8 RawRead8(uint32 address);
    uint16 RawRead16(uint32 address);
    uint32 RawRead32(uint32 address);
    void AcknowledgeExternalInterrupt(SH2 &cpu);
    // The cartridge's ROM and the program SIMMs as the CPU reads them (deciphered), as big-endian bytes: what the
    // translated code reads directly and is made from (runtime/block_native.h)
    const uint8 *BiosBytes() const {
        return m_biosBytes.data();
    }
    const uint8 *ProgramBytes() const {
        return m_programBytes.data();
    }
    // ... and the program SIMMs as its data reads see them (2nd Impact's are not deciphered; elsewhere the same)
    const uint8 *ProgramDataBytes() const {
        return m_programDataBytes.empty() ? m_programBytes.data() : m_programDataBytes.data();
    }

    SH2 &Cpu() {
        return m_cpu;
    }
    uint64 Frame() const {
        return m_frame;
    }

    // --- what the renderer reads ---
    const VideoRegisters &Video() const {
        return m_videoRegs;
    }
    // The object list as the sprite DMA left it: four words per object, ended by 0x80000000; the count
    const uint32 *SpriteList() const {
        return m_spriteList.data();
    }
    uint32 SpriteCount() const {
        return m_spriteCount;
    }
    // Which entry of the main list (0-511) each object of SpriteList came from: its group, for drawing some of them
    const uint16 *SpriteGroups() const {
        return m_spriteGroup.data();
    }
    uint32 SpriteRam(uint32 word) const; // 32-bit word `word` of sprite RAM
    const uint8 *SpriteRamBytes() const {
        return m_spriteRam.data(); // (512 KB, big-endian)
    }
    const uint8 *CharRam() const {
        return m_charRam.data(); // 8 MB, big-endian bytes; a 16x16 tile is 256 bytes, one per pixel
    }
    const uint8 *SsRam() const {
        return m_ssRam.data();
    }
    // The game's sources' say over the colours a palette DMA brings in (custom colours): given where in the graphics a
    // colour is read from (its word) and where it goes, the colour to use (none: as read). And a colour written as the
    // CPU would (both its copies are the game's to keep: this writes one)
    uint16 (*paletteColour)(uint32 word, uint32 index, uint16 colour) = nullptr;
    void WriteColour(uint32 index, uint16 value) {
        SetColour(index, value, 0);
        // (whenever the interface is kept: written between frames (the colour editor), whatever the last frame took)
        if (native::On()) {
            native.palette.Set(index, value);
        }
    }
    const uint16 *ColourRam() const {
        return m_colourRam.data();
    }
    const uint32 *Palette() const {
        return m_rgb.data(); // 0x20000 colours, 0x00BBGGRR
    }
    uint16 SsPaletteBase() const {
        return m_ssPaletteBase;
    }
    uint16 SsVerticalScroll() const {
        return m_ssVScroll;
    }
    // The sound chip's registers (sound/pcm.h reads them)
    struct Voice {
        uint32 regs[8] = {};
        uint32 position = 0, fraction = 0;
    };
    std::array<Voice, 16> voices{};
    // Voices not to be heard (the mixer leaves them out; what they play goes on): for a game's sources that take one
    // part of the sound away (the definitive version's stages: 3rd Strike's music). Not part of the save state.
    std::array<bool, 16> silenced{};
    uint16 keyOn = 0;
    const uint8 *Samples() const {
        return m_rom.graphics.data();
    }
    size_t SamplesSize() const {
        return m_rom.graphics.size();
    }
    // Called before the sound chip's registers change, so that what was playing is mixed up to now
    void (*beforeSoundWrite)(void *context) = nullptr;
    void *soundContext = nullptr;
    // How far into the frame the CPU is (0..1), for the sound's timing
    double FramePosition() const;

    // The settings EEPROM (128 bytes)
    std::array<uint32, 32> eeprom{};

    // --- the native interface (video/native.h, docs/NATIVE.md) ---
    // What the game's rewritten code has handed over (kept when native::On(), and then in save states)
    native::Video native;
    // The rewrites hand it a palette transfer, or a colour the game's code writes (as they write the board's registers
    // and colour RAM: the board model is still what is drawn)
    void NativePaletteTransfer(const native::PaletteTransfer &t);
    void NativeColour(uint32 index, uint16 value);
    // ... and the frame's registers, as the game sets them (null when the interface is not kept: nothing to hand over)
    native::FrameRegisters *NativeRegisters() {
        if (!NativeTaking()) {
            return nullptr;
        }
        m_nativeHanded |= 2;
        return &native.registers;
    }
    // The frame's registers as the board holds them, in the interface's terms
    native::FrameRegisters BoardRegisters() const;
    // ... the object lists' heads the game made for the next object DMA
    void NativeLists(std::vector<native::ListHead> heads) {
        if (NativeTaking()) {
            native.lists = std::move(heads);
            m_bootLists = false;
            m_nativeHanded |= 1;
        }
    }
    // ... a character DMA's list (its transfers made into the native textures), and bytes the game's code writes to
    // character RAM, at their place in its 8 MB (the CPU's window's bank added)
    void NativeCharacterList(const uint32 *list, size_t longs) {
        if (NativeTaking()) {
            native.textures.Load(list, longs, m_rom.graphics.data(), m_rom.graphics.size());
        }
    }
    void NativeCharacterWrite(uint32 at, uint32 value, int bytes) {
        if (NativeTaking()) {
            native.textures.Write(at, value, bytes);
        }
    }
    // The object lists' heads as sprite RAM holds them (as the object DMA reads them: up to the end)
    std::vector<native::ListHead> BoardLists() const;
    // The check (CPS3RECOMP_NATIVE_CHECK=1, for the machine that runs the game whose code is rewritten): after every
    // frame, the palette events the board saw (its palette DMAs as its registers had them, the colours the game's code
    // wrote to colour RAM) against those the rewrites handed over, and colour RAM against the native palette. The boot
    // ROM's own colours (its memory test and warning screen: not the game's code) are passed on as they are written.
    // And the frame's registers as the board latched them against those handed over (the boot ROM's passed on too), and
    // at every object DMA the game starts, the lists' heads it takes against those handed over; and character RAM against
    // the native textures, every frame (the boot ROM's writes to it passed on).
    // Reported at the end (NativeCheckReport)
    bool nativeCheck = false;
    // Whether the interface holds the frame as the board does (a GPU may draw the picture from it): the game's code
    // handed over this frame's lists' heads and registers (the rewrites ran: not with arcade timing's own code, nor
    // CPS3RECOMP_NO_REWRITES), and nothing since put a state back that it was not made again from. Outside the checks,
    // a frame that hands over after one that did not (or after a state put back) makes the interface again from the
    // board's memories first (SyncNative)
    bool NativeInSync() const {
        return m_nativeInSync;
    }
    // Whether this frame the board's video memories are made from the native interface instead of by its video model
    // (the default build: the interface holds the frame and takes what is handed over): the object DMA's list from the
    // interface's objects, the palette DMA's colours from its palette, the character DMA's bytes from its textures (at
    // the frame's end: TakeFromNative), the same bytes as the model's (checknative.sh), so the save state is the same.
    // The model itself runs with --board, in the checks, for frames the interface does not hold (after a state put back,
    // run-ahead's, the boot ROM's), and with CPS3RECOMP_BOARD_MODEL=1
    bool NativeMakes() const;
    // The interface made to hold this frame for its picture if it does not (the frames the game's code hands nothing
    // over, the boot ROM's: made again from the board's memories, SyncNative); false when it cannot be (not kept, or in
    // the checks, where it is compared and never made again)
    bool NativeForPicture() {
        if (m_nativeInSync || m_nativeSyncedAt == m_frame) {
            return true;
        }
        if (!native::On() || nativeCheck) {
            return false;
        }
        SyncNative();
        m_nativeStamps = m_charStamp;
        m_nativeStampsKnown = true;
        m_nativeSyncedAt = m_frame;
        return true;
    }
    // Whether this frame's picture may be drawn from the interface (set by the port's machine before each frame): only
    // then is the interface made again from the board when it must be
    bool nativeWanted = false;
    // Whether what the game's code hands over is taken this frame: in the checks, and for a frame a GPU may draw (not
    // run-ahead's, nor a stage board's; CPS3RECOMP_NATIVE_SYNC_CHECK too). A frame not taken leaves the interface out of
    // step, to be made again from the board (SyncNative) when one is wanted
    bool NativeTaking() const {
        return native::On() && (nativeWanted || native::Checking() || SyncChecked());
    }
    static bool SyncChecked();

    void Serialize(Archive &ar);

private:
    const RomSet &m_rom;
    std::vector<uint8> m_biosBytes, m_programBytes, m_programDataBytes;
    void UpdateFlashPlain();
    SH2 m_cpu;
    uint64 m_frame = 0;
    uint64 m_now = 0;       // CPU cycles since reset
    uint64 m_frameStart = 0;
    uint64 m_spillover = 0;
    uint64 m_sliceBase = 0; // (while the CPU runs a slice: the time its count of cycles starts from)
    bool m_inSlice = false;
    // The time now, in CPU cycles since reset: inside a slice, as far as the CPU has got
    uint64 Now() const {
        return m_inSlice ? m_sliceBase + m_cpu.m_cyclesExecuted : m_now;
    }

    std::vector<uint8> m_spriteRam, m_charRam, m_fram;
    // Character RAM by pages of 64 KB, each with a stamp that changes at every write to it (unique over the run), and
    // the last copies of each page kept by states kept in memory (Archive::inMemory): such a state holds only the
    // stamps, and putting it back copies only the pages that differ
    static constexpr int kCharPages = 128;
    static constexpr size_t kCharPagesKept = 48;
    std::array<uint64, kCharPages> m_charStamp{};
    uint64 m_stampCounter = 0;
    struct PageCopy {
        uint64 stamp = 0;
        std::vector<uint8> data;
    };
    std::array<std::deque<PageCopy>, kCharPages> m_charKept;
    void TouchChar(uint32 address) {
        m_charStamp[(address >> 16) & (kCharPages - 1)] = ++m_stampCounter;
    }
    void MakeColours(); // (the colours as drawn, made again from colour RAM after a state is put back)
    std::vector<uint16> m_colourRam;
    std::vector<uint32> m_rgb;
    std::vector<uint8> m_ssRam;
    std::array<uint8, 0x30> m_ssRegs{};
    uint16 m_ssHScroll = 0, m_ssVScroll = 0, m_ssPaletteBase = 0;
    std::array<uint8, 0x100> m_ppuRegs{};
    VideoRegisters m_videoRegs;
    std::vector<uint32> m_spriteList;
    std::vector<uint16> m_spriteGroup;
    uint32 m_spriteCount = 0;
    uint16 m_spriteDma = 0, m_dmaStatus = 0;
    uint32 m_charBank = 0;
    sint32 m_gfxFlashBank = 0;
    uint32 m_charDmaSource = 0, m_charDmaOther = 0, m_tableAddress = 0;
    uint32 m_palDmaSource = 0, m_palDmaDest = 0, m_palDmaFade = 0, m_palDmaOther = 0, m_palDmaLength = 0;
    uint8 m_lastNormalByte = 0;
    uint16 m_lastb = 0, m_lastb2 = 0;
    uint16 m_eepromLatch = 0;
    uint16 m_irqLines = 0; // bit n: interrupt level n held
    std::vector<native::PaletteEvent> m_boardPaletteEvents; // (the check: this frame's, as the board saw them)
    void NativeCompare();
    void NoteColourWrite(uint32 index);
    void NoteRegisterWrite(); // (the check: a register written by the boot ROM passed on)
    bool m_bootLists = false;  // (the check: the boot ROM has written list heads since the game last handed some over)
    void NoteListWrite(uint32 address) {
        if (nativeCheck && (address & 0x7FFFF) < 0x2000 && m_cpu.PC < 0x80000) {
            m_bootLists = true;
        }
    }
    void NativeCompareLists();
    void NativeCompareObjects();
    // (outside the checks: whether the interface holds what the board does; SyncNative makes it so)
    uint8 m_nativeHanded = 0; // (this frame: 1 the lists' heads, 2 the registers)
    bool m_nativeInSync = false, m_nativeStampsKnown = false;
    uint64 m_nativeSyncedAt = ~0ull; // (the frame NativeForPicture made it again for)
    // (NativeMakes: what this frame's DMAs owe the board's memories, taken from the interface at the frame's end)
    std::vector<std::pair<uint32, uint32>> m_coloursOwed; // (destination, colours)
    bool m_charsOwed = false;
    std::array<uint64, kCharPages> m_pagesTaken{}; // (the interface's textures' pages' versions last taken)
    uint64 m_sixTaken = 0, m_eightTaken = 0;       // (its compressed transfers' counts last taken)
    void ObjectsFromNative();
    void TakeFromNative();
    std::array<uint64, 128> m_nativeStamps{}; // (character RAM's pages' stamps when the textures last held the same)
    void NativeFrameEnd();
    void SyncNative();
    void NoteCharWrite(uint32 at, uint32 value, int bytes) {
        if (nativeCheck && m_cpu.PC < 0x80000) {
            native.textures.Write(at, value, bytes);
        }
    }
    uint64 m_dmaDoneAt = 0, m_spriteDmaDoneAt = 0; // 0: none pending
    FlashChip m_flash[7][8]; // [SIMM][chip]

    uint32 ReadMasked(uint32 address, uint32 mask);
    void WriteMasked(uint32 address, uint32 value, uint32 mask);
    void WritePpu(uint32 offset, uint32 value, uint32 mask);
    void WriteSound(uint32 offset, uint32 value, uint32 mask);
    uint32 ReadSound(uint32 offset);
    uint32 ReadProgramFlash(uint32 address, uint32 mask, bool deciphered);
    void WriteProgramFlash(uint32 address, uint32 value, uint32 mask);
    uint32 ReadGfxFlash(uint32 address, uint32 mask);
    void WriteGfxFlash(uint32 address, uint32 value, uint32 mask);
    void SetColour(uint32 index, uint16 value, uint32 fade);
    void SpriteDma();
    void CharacterDma(uint32 listAddress);
    void PaletteDma(uint32 control);
    uint32 CharDmaByte(uint8 value, uint32 destination);
    uint32 CharDmaByte8(uint8 value, uint32 destination);
    void UpdateInterrupts();
    void RunUntil(uint64 cycle);
};

} // namespace cps3recomp::machine
