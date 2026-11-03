// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
// Save states of the CPU and the board (runtime/state.h). The SH-2's is satrecomp's.
#include "machine.h"
#include "sh2.h"

#include "state.h"

#include <algorithm>
#include <cstdio>

namespace cps3recomp::machine {

void SH2::Serialize(Archive &ar) {
    ar.Mark(0x53483220);
    ar.Pod(R);
    ar.Pod(PC);
    ar.Pod(PR);
    ar.Pod(GBR);
    ar.Pod(VBR);
    ar.Pod(MAC.u64);
    ar.Pod(SR.u32);
    ar.Pod(m_intrFlags);
    ar.Pod(m_delaySlot);
    ar.Pod(m_delaySlotTarget);
    ar.Pod(m_wbReg);
    ar.Pod(m_recompResume);
    ar.Pod(m_recompYield);
    ar.Pod(m_recompWaiting);
    ar.Pod(m_sleep);
    ar.Pod(m_yieldRequest);
    ar.Pod(INTC);
    ar.Pod(m_frt);
    ar.Pod(m_divu);
    ar.Pod(m_dma);
    ar.Pod(m_dmaOperation);
    ar.Pod(m_cacheControl);
    ar.Pod(m_cacheData);
    m_bus.Serialize(ar);
    ar.Pod(m_pipeMemory);
    ar.Pod(m_pipeLoads);
}

void Machine::Serialize(Archive &ar) {
    ar.Mark(0x43505342);
    m_cpu.Serialize(ar);
    ar.Vec(m_mainRam);
    mainRam = m_mainRam.data();
    ar.Vec(m_spriteRam);
    if (ar.inMemory) {
        // (the pages' stamps only; each page as it is now is kept beside, once)
        if (ar.Saving()) {
            for (int page = 0; page < kCharPages; page++) {
                std::deque<PageCopy> &kept = m_charKept[page];
                if (kept.empty() || kept.back().stamp != m_charStamp[page]) {
                    PageCopy copy;
                    if (kept.size() >= kCharPagesKept) {
                        copy = std::move(kept.front()); // (its buffer used again)
                        kept.pop_front();
                    }
                    copy.stamp = m_charStamp[page];
                    copy.data.assign(m_charRam.begin() + page * 0x10000, m_charRam.begin() + (page + 1) * 0x10000);
                    kept.push_back(std::move(copy));
                }
            }
        }
        std::array<uint64, kCharPages> stamps = m_charStamp;
        ar.Pod(stamps);
        if (!ar.Saving()) {
            for (int page = 0; page < kCharPages; page++) {
                if (stamps[page] == m_charStamp[page]) {
                    continue;
                }
                const std::deque<PageCopy> &kept = m_charKept[page];
                const auto found = std::find_if(kept.rbegin(), kept.rend(), [&](const PageCopy &c) { return c.stamp == stamps[page]; });
                if (found == kept.rend()) {
                    fprintf(stderr, "state: character RAM page %d is no longer kept\n", page);
                    continue;
                }
                std::copy(found->data.begin(), found->data.end(), m_charRam.begin() + page * 0x10000);
                m_charStamp[page] = stamps[page];
            }
        }
    } else {
        ar.Vec(m_charRam);
        if (!ar.Saving()) {
            for (uint64 &stamp : m_charStamp) {
                stamp = ++m_stampCounter; // (what a state from a file holds is new to the run)
            }
        }
    }
    ar.Vec(m_fram);
    ar.Vec(m_colourRam);
    if (!ar.Saving()) {
        MakeColours();
    }
    ar.Vec(m_ssRam);
    ar.Pod(m_ssRegs);
    ar.Pod(m_ssHScroll);
    ar.Pod(m_ssVScroll);
    ar.Pod(m_ssPaletteBase);
    ar.Pod(m_ppuRegs);
    ar.Pod(m_videoRegs);
    ar.Vec(m_spriteList);
    ar.Vec(m_spriteGroup);
    ar.Pod(m_spriteCount);
    ar.Pod(m_spriteDma);
    ar.Pod(m_dmaStatus);
    ar.Pod(m_charBank);
    ar.Pod(m_gfxFlashBank);
    ar.Pod(m_charDmaSource);
    ar.Pod(m_charDmaOther);
    ar.Pod(m_tableAddress);
    ar.Pod(m_palDmaSource);
    ar.Pod(m_palDmaDest);
    ar.Pod(m_palDmaFade);
    ar.Pod(m_palDmaOther);
    ar.Pod(m_palDmaLength);
    ar.Pod(m_lastNormalByte);
    ar.Pod(m_lastb);
    ar.Pod(m_lastb2);
    ar.Pod(m_eepromLatch);
    ar.Pod(m_irqLines);
    ar.Pod(m_dmaDoneAt);
    ar.Pod(m_spriteDmaDoneAt);
    ar.Pod(m_flash);
    UpdateFlashPlain(); // (what the translated code reads directly: runtime/block_native.h)
    ar.Pod(voices);
    ar.Pod(keyOn);
    ar.Pod(eeprom);
    ar.Pod(m_frame);
    ar.Pod(m_now);
    ar.Pod(m_frameStart);
    ar.Pod(m_spillover);
}

} // namespace cps3recomp::machine
