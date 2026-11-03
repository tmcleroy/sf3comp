// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The SH-2 pipeline's stalls between instructions (the arcade_timing feature, with machine/bus.h), as jtcores' SH7604
// model makes them (SH_core.sv): one cycle when the instruction before
//  - accessed memory (or the MAC registers) and this one is at a long-aligned address, so that its fetch is due in
//    the same cycle (not for a bra or bsr with a 12-bit displacement); or
//  - loaded a register from memory that this one reads.
// What each kind of instruction reads and loads is the model's decoder's (SH_pkg.sv's Decode), for its first state and
// its last; the table below was made from it for every opcode (the timing harness's decode/kinds.py) and is the same
// for all the opcodes of each kind. Registers: the n field (bits 11-8), the m field (7-4), or a number (16: PR).
#pragma once

#include "sh2_ops.h"

#include <cstdint>

namespace cps3recomp::machine {

struct PipeKind {
    Op op;
    int8_t reads[3];
    uint8_t memory;
    int8_t loads;
    uint8_t bra12;
};
inline constexpr int8_t kRn = -2, kRm = -3, kNone = -1;
inline constexpr PipeKind kPipeKinds[] = {
    {Op::NOP, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::SLEEP, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::MOV_R, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::MOVB_L, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVW_L, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVL_L, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVB_L0, {kRm, 0, kNone}, 1, kRn, 0},
    {Op::MOVW_L0, {kRm, 0, kNone}, 1, kRn, 0},
    {Op::MOVL_L0, {kRm, 0, kNone}, 1, kRn, 0},
    {Op::MOVB_L4, {kRm, kNone, kNone}, 1, 0, 0},
    {Op::MOVW_L4, {kRm, kNone, kNone}, 1, 0, 0},
    {Op::MOVL_L4, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVB_LG, {kNone, kNone, kNone}, 1, 0, 0},
    {Op::MOVW_LG, {kNone, kNone, kNone}, 1, 0, 0},
    {Op::MOVL_LG, {kNone, kNone, kNone}, 1, 0, 0},
    {Op::MOVB_M, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVW_M, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVL_M, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVB_P, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVW_P, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVL_P, {kRm, kNone, kNone}, 1, kRn, 0},
    {Op::MOVB_S, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVW_S, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVL_S, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVB_S0, {kRn, kRm, 0}, 1, kNone, 0},
    {Op::MOVW_S0, {kRn, kRm, 0}, 1, kNone, 0},
    {Op::MOVL_S0, {kRn, kRm, 0}, 1, kNone, 0},
    {Op::MOVB_S4, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVW_S4, {0, kRm, kNone}, 1, kNone, 0},
    {Op::MOVL_S4, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MOVB_SG, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::MOVW_SG, {0, kNone, kNone}, 1, kNone, 0},
    {Op::MOVL_SG, {0, kNone, kNone}, 1, kNone, 0},
    {Op::MOV_I, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::MOVW_I, {kNone, kNone, kNone}, 1, kRn, 0},
    {Op::MOVL_I, {kNone, kNone, kNone}, 1, kRn, 0},
    {Op::MOVA, {0, kNone, kNone}, 0, kNone, 0},
    {Op::MOVT, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::CLRT, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::SETT, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::EXTUB, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::EXTUW, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::EXTSB, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::EXTSW, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::SWAPB, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::SWAPW, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::XTRCT, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::LDC_GBR_R, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::LDC_SR_R, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::LDC_VBR_R, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::LDS_MACH_R, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::LDS_MACL_R, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::LDS_PR_R, {kRn, 16, kNone}, 0, kNone, 0},
    {Op::STC_GBR_R, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::STC_SR_R, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::STC_VBR_R, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::STS_MACH_R, {kNone, kNone, kNone}, 1, kRn, 0},
    {Op::STS_MACL_R, {kNone, kNone, kNone}, 1, kRn, 0},
    {Op::STS_PR_R, {kRn, 16, kNone}, 0, kNone, 0},
    {Op::LDC_GBR_M, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::LDC_SR_M, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::LDC_VBR_M, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::LDS_MACH_M, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::LDS_MACL_M, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::LDS_PR_M, {kRn, kNone, kNone}, 1, 16, 0},
    {Op::STC_GBR_M, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::STC_SR_M, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::STC_VBR_M, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::STS_MACH_M, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::STS_MACL_M, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::STS_PR_M, {kRn, 16, kNone}, 1, kNone, 0},
    {Op::ADD, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::ADD_I, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::ADDC, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::ADDV, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::AND_R, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::AND_I, {0, kNone, kNone}, 0, kNone, 0},
    {Op::AND_M, {0, kNone, kNone}, 1, kNone, 0},
    {Op::NEG, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::NEGC, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::NOT, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::OR_R, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::OR_I, {0, kNone, kNone}, 0, kNone, 0},
    {Op::OR_M, {0, kNone, kNone}, 1, kNone, 0},
    {Op::ROTCL, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::ROTCR, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::ROTL, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::ROTR, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHAL, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHAR, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLL, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLL2, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLL8, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLL16, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLR, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLR2, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLR8, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SHLR16, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::SUB, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::SUBC, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::SUBV, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::XOR_R, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::XOR_I, {0, kNone, kNone}, 0, kNone, 0},
    {Op::XOR_M, {0, kNone, kNone}, 1, kNone, 0},
    {Op::DT, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::CLRMAC, {kNone, kNone, kNone}, 1, kNone, 0},
    {Op::MACW, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::MACL, {kRn, kNone, kNone}, 1, kNone, 0},
    {Op::MUL, {kRm, kNone, kNone}, 1, kNone, 0},
    {Op::MULS, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::MULU, {kRn, kRm, kNone}, 1, kNone, 0},
    {Op::DMULS, {kRm, kNone, kNone}, 1, kNone, 0},
    {Op::DMULU, {kRm, kNone, kNone}, 1, kNone, 0},
    {Op::DIV0S, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::DIV0U, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::DIV1, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::CMP_EQ_I, {0, kNone, kNone}, 0, kNone, 0},
    {Op::CMP_EQ_R, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::CMP_GE, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::CMP_GT, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::CMP_HI, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::CMP_HS, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::CMP_PL, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::CMP_PZ, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::CMP_STR, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::TAS, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::TST_R, {kRn, kRm, kNone}, 0, kNone, 0},
    {Op::TST_I, {0, kNone, kNone}, 0, kNone, 0},
    {Op::TST_M, {0, kNone, kNone}, 0, kNone, 0},
    {Op::BF, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::BFS, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::BT, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::BTS, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::BRA, {kNone, kNone, kNone}, 0, kNone, 1},
    {Op::BRAF, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::BSR, {16, kNone, kNone}, 0, kNone, 1},
    {Op::BSRF, {kRn, 16, kNone}, 0, kNone, 0},
    {Op::JMP, {kRn, kNone, kNone}, 0, kNone, 0},
    {Op::JSR, {kRn, 16, kNone}, 0, kNone, 0},
    {Op::TRAPA, {kNone, kNone, kNone}, 0, kNone, 0},
    {Op::RTE, {15, kNone, kNone}, 0, kNone, 0},
    {Op::RTS, {16, kNone, kNone}, 0, kNone, 0},
    {Op::Illegal, {kNone, kNone, kNone}, 0, kNone, 0},
};

// An opcode's: the registers it reads (a mask, bit 16 PR), whether it accesses memory, the register it loads (-1 none),
// and whether it is the bra or bsr that has no fetch stall
struct PipeInfo {
    uint32_t reads = 0;
    bool memory = false, bra12 = false;
    int8_t loads = -1;
};
const PipeInfo &PipeOf(uint16_t opcode);

} // namespace cps3recomp::machine
