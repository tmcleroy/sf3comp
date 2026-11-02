// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
// Save states of the CPU and the board (runtime/state.h). The SH-2's is satrecomp's.
#include "machine.h"
#include "sh2.h"

#include "state.h"

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
}

void Machine::Serialize(Archive &ar) {
    ar.Mark(0x43505342);
    m_cpu.Serialize(ar);
    ar.Vec(m_mainRam);
    mainRam = m_mainRam.data();
    ar.Vec(m_spriteRam);
    ar.Vec(m_charRam);
    ar.Vec(m_fram);
    ar.Vec(m_colourRam);
    ar.Vec(m_rgb);
    ar.Vec(m_ssRam);
    ar.Pod(m_ssRegs);
    ar.Pod(m_ssHScroll);
    ar.Pod(m_ssVScroll);
    ar.Pod(m_ssPaletteBase);
    ar.Pod(m_ppuRegs);
    ar.Pod(m_videoRegs);
    ar.Vec(m_spriteList);
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
    ar.Pod(voices);
    ar.Pod(keyOn);
    ar.Pod(eeprom);
    ar.Pod(m_frame);
    ar.Pod(m_now);
    ar.Pod(m_frameStart);
    ar.Pod(m_spillover);
}

} // namespace cps3recomp::machine
