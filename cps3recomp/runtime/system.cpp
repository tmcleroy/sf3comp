// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "system.h"

#include "state.h"

#include <cstdio>
#include <cstdlib>

namespace cps3recomp {

namespace {
    machine::Machine *g_board = nullptr;
    bool Plain(uint32_t a) {
        a &= 0x1FFFFFFFu;
        return (a & 0xFFF80000u) == 0x02000000u || a < 0x80000u || (a >= 0x06000000u && a < 0x07000000u);
    }
} // namespace

uint32_t Peek32(uint32_t a) {
    return g_board && Plain(a) ? g_board->BusRead32(a & 0x1FFFFFFCu) : 0;
}
uint16_t Peek16(uint32_t a) {
    return g_board && Plain(a) ? g_board->BusRead16(a & 0x1FFFFFFEu) : 0;
}

Machine::Machine(const RomSet &rom)
    : board(rom)
    , m_pcm(board) {
    g_board = &board;
    eeprom.words = board.eeprom.data();
    m_picture.assign(static_cast<size_t>(Video::kWidth) * Video::kHeight, 0);
}

void Machine::Reset() {
    board.Reset();
    frame = 0;
}

// The board's controls (MAME's CPS-3 inputs; a pressed button reads 0): the first word has both joysticks, the
// punches, Start, the coins, service and test and player 2's hard kick; the second the other kicks
void Machine::RunFrame() {
    uint32_t inputs = 0, extra = 0;
    for (int p = 0; p < 2; p++) {
        const uint16_t pad = players[p];
        const uint32_t shift = p * 8;
        inputs |= ((pad & kUp) ? 0x1u : 0) << shift;
        inputs |= ((pad & kDown) ? 0x2u : 0) << shift;
        inputs |= ((pad & kLeft) ? 0x4u : 0) << shift;
        inputs |= ((pad & kRight) ? 0x8u : 0) << shift;
        inputs |= ((pad & kPunch1) ? 0x10u : 0) << shift;
        inputs |= ((pad & kPunch2) ? 0x20u : 0) << shift;
        inputs |= ((pad & kPunch3) ? 0x40u : 0) << shift;
        inputs |= (pad & kStart) ? (0x10000000u << p) : 0;
        inputs |= (pad & kCoin) ? (0x01000000u << p) : 0;
    }
    extra |= (players[0] & kKick1) ? 0x80000u : 0;
    extra |= (players[0] & kKick2) ? 0x40000u : 0;
    extra |= (players[0] & kKick3) ? 0x20000u : 0;
    extra |= (players[1] & kKick1) ? 0x100000u : 0;
    extra |= (players[1] & kKick2) ? 0x200000u : 0;
    inputs |= (players[1] & kKick3) ? 0x4000000u : 0;
    inputs |= serviceButton ? 0x10000u : 0;
    inputs |= testSwitch ? 0x20000u : 0;
    board.inputs = ~inputs;
    board.extra = ~extra;
    board.RunFrame();
    m_pcm.EndFrame(&audio);
    if (drawing) {
        m_renderer.Draw(board, m_picture, m_width, m_height);
    }
    // (debugging aid, CPS3RECOMP_SPRITES=FRAME: the object list as drawn at that frame)
    static const long long spritesAt = getenv("CPS3RECOMP_SPRITES") ? atoll(getenv("CPS3RECOMP_SPRITES")) : -1;
    if (static_cast<long long>(frame) == spritesAt) {
        const uint32_t *list = board.SpriteList();
        for (uint32_t n = 0; n < board.SpriteCount(); n++) {
            fprintf(stderr, "object %3u: %08X %08X %08X %08X\n", n, list[n * 4], list[n * 4 + 1], list[n * 4 + 2], list[n * 4 + 3]);
        }
        for (uint32_t i = 0; i < 0x800; i += 4) {
            const uint32_t head = board.SpriteRam(i);
            fprintf(stderr, "list %3u: %08X %08X %08X %08X\n", i / 4, head, board.SpriteRam(i + 1), board.SpriteRam(i + 2), board.SpriteRam(i + 3));
            if (head & 0x80000000u) {
                break;
            }
        }
        for (int t = 0; t < 4; t++) {
            fprintf(stderr, "tilemap %d: %08X %08X %08X %08X\n", t, board.Video().tilemap[t * 4], board.Video().tilemap[t * 4 + 1],
                    board.Video().tilemap[t * 4 + 2], board.Video().tilemap[t * 4 + 3]);
        }
    }
    frame++;
}

void Machine::Serialize(Archive &ar) {
    ar.Mark(0x43505333);
    board.Serialize(ar);
    m_pcm.Serialize(ar);
    ar.Pod(frame);
}

} // namespace cps3recomp
