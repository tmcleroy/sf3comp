// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "system.h"
#include "game.h"

#include "state.h"
#include "recomp.h"

#include <algorithm>

#include <cstdio>
#include <cstring>
#include <string>
#include "png_write.h"
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
    board.nativeCheck = native::On(); // (the game's board: its rewrites hand the native interface what they draw)
    eeprom.words = board.eeprom.data();
    m_picture.assign(static_cast<size_t>(Video::kWidth) * Video::kHeight, 0);
}

void Machine::Reset() {
    board.Reset();
    frame = 0;
}

// The board's controls (MAME's CPS-3 inputs; a pressed button reads 0): the first word has both joysticks, the
// punches, Start, the coins, service and test and player 2's hard kick; the second the other kicks
extern bool (*g_rewritesNeededNow)(); // (recomp.cpp)

// The pixel check (docs/NATIVE.md, level b): the native interface's picture, drawn by the native rasteriser from what
// the game handed over, against renderer.cpp's from the board, every frame drawn the plain way (no widescreen, no
// second board, nothing left out). Reported at the end.
namespace {
    struct PixelTotals {
        uint64_t frames = 0, different = 0;
        unsigned reports = 6;
        ~PixelTotals() {
            if (frames) {
                fprintf(stderr, "cps3recomp: native pixels: %llu frames compared, %llu different\n", static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(different));
            }
        }
    } g_pixelTotals;
} // namespace

void Machine::NativePixels() {
    int width = 0, height = 0;
    m_nativeRaster.Draw(board.native, board.SpriteRamBytes(), board.SsRam(), m_nativePicture, width, height);
    g_pixelTotals.frames++;
    if (width != m_width || height != m_height || m_nativePicture.size() < static_cast<size_t>(width) * height ||
        m_picture.size() < static_cast<size_t>(width) * height ||
        !std::equal(m_nativePicture.begin(), m_nativePicture.begin() + static_cast<size_t>(width) * height, m_picture.begin())) {
        g_pixelTotals.different++;
        if (g_pixelTotals.reports) {
            g_pixelTotals.reports--;
            size_t at = 0, n = std::min(m_nativePicture.size(), m_picture.size());
            while (at < n && m_nativePicture[at] == m_picture[at]) {
                at++;
            }
            fprintf(stderr, "native pixels, frame %llu: %dx%d (board) / %dx%d (native), the first to differ at %d,%d: %06X / %06X\n",
                    static_cast<unsigned long long>(frame), m_width, m_height, width, height, width ? static_cast<int>(at % width) : 0,
                    width ? static_cast<int>(at / width) : 0, at < n ? m_picture[at] : 0, at < n ? m_nativePicture[at] : 0);
        }
    }
}

void Machine::RunFrame() {
    // (the bus's timing: the arcade_timing feature, or CPS3RECOMP_BUS_TIMING=1 for the timing harness)
    static const bool forced = getenv("CPS3RECOMP_BUS_TIMING") != nullptr;
    board.Cpu().m_busTiming = forced || FeatureOn("arcade_timing");
    g_rewritesNeededNow = TheGame().rewritesNeeded;
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
    const bool drawing = this->drawing && !holdPicture;
    m_renderer.text = !hidePanel;
    m_renderer.backdrop = backdrop;
    m_renderer.shift = worldShiftNow && m_renderer.margin ? std::clamp(worldShiftNow(), -m_renderer.margin, m_renderer.margin) : 0;
    if (drawing && dropRaise >= 0 && ownStageObject && dropOld.size() == static_cast<size_t>(PictureWidth()) * Video::kHeight) {
        // (the ring out's way down: the old stage's bottom rows at the top, this stage under them, the fighters over both)
        std::vector<uint32_t> stage;
        DrawStageOnly(stage);
        const int raise = std::clamp(dropRaise, 0, m_height);
        if (stage.size() == static_cast<size_t>(m_width) * m_height && dropOld.size() == stage.size()) {
            m_picture.resize(stage.size());
            for (int y = 0; y < m_height; y++) {
                const uint32_t *from = y < raise ? &dropOld[static_cast<size_t>(m_height - raise + y) * m_width] : &stage[static_cast<size_t>(y - raise) * m_width];
                memcpy(&m_picture[static_cast<size_t>(y) * m_width], from, static_cast<size_t>(m_width) * sizeof(uint32_t));
            }
            m_renderer.overlay = true;
            m_renderer.shiftY = raise;
            m_renderer.drawGroupContext = this;
            m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return !me.ownStageObject(me.board, object) && !(me.dropLifted && me.dropLifted(me.board, object));
            };
            m_renderer.Draw(board, m_picture, m_width, m_height);
            if (dropLifted) {
                const bool text = m_renderer.text;
                m_renderer.text = false;
                m_renderer.shiftY = raise - dropLift;
                m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                    const Machine &me = *static_cast<Machine *>(self);
                    return !me.ownStageObject(me.board, object) && me.dropLifted(me.board, object);
                };
                m_renderer.Draw(board, m_picture, m_width, m_height);
                m_renderer.text = text;
            }
            m_renderer.overlay = false;
            m_renderer.shiftY = 0;
            m_renderer.drawGroup = nullptr;
        }
    } else if (drawing && m_stage && stageObject && ownStageObject && (!composeNow || composeNow())) {
        m_stageRenderer.margin = m_renderer.margin;
        m_stageRenderer.text = false;
        m_stageRenderer.drawGroupContext = this;
        m_stageRenderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return me.stageObject(*me.m_stage, object);
        };
        m_stageRenderer.Draw(*m_stage, m_picture, m_width, m_height);
        if (m_stage2 && stageBlend > 0) {
            m_stage2Renderer.margin = m_renderer.margin;
            m_stage2Renderer.text = false;
            m_stage2Renderer.drawGroupContext = this;
            m_stage2Renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return me.stageObject(*me.m_stage2, object);
            };
            int w2 = 0, h2 = 0;
            m_stage2Renderer.Draw(*m_stage2, m_stage2Picture, w2, h2);
            if (w2 == m_width && h2 == m_height && m_stage2Picture.size() == m_picture.size()) {
                const uint32_t put = static_cast<uint32_t>(std::clamp(stageBlend, 0, 256)), keep = 256 - put;
                for (size_t i = 0; i < m_picture.size(); i++) {
                    const uint32_t a = m_picture[i], b = m_stage2Picture[i];
                    m_picture[i] = ((((a & 0xFF00FF) * keep + (b & 0xFF00FF) * put) >> 8) & 0xFF00FF) |
                                   ((((a & 0x00FF00) * keep + (b & 0x00FF00) * put) >> 8) & 0x00FF00);
                }
            }
        } else if (m_stage2 && stageSlide > 0) {
            // (the next stage coming in: the first's picture moved left, the second's left edge in the columns freed)
            m_stage2Renderer.margin = m_renderer.margin;
            m_stage2Renderer.text = false;
            m_stage2Renderer.drawGroupContext = this;
            m_stage2Renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return me.stageObject(*me.m_stage2, object);
            };
            int w2 = 0, h2 = 0;
            m_stage2Renderer.Draw(*m_stage2, m_stage2Picture, w2, h2);
            if (w2 == m_width && h2 == m_height) {
                const int slide = std::min(stageSlide, m_width);
                for (int y = 0; y < m_height; y++) {
                    uint32_t *row = &m_picture[static_cast<size_t>(y) * m_width];
                    memmove(row, row + slide, static_cast<size_t>(m_width - slide) * sizeof(uint32_t));
                    memcpy(row + m_width - slide, &m_stage2Picture[static_cast<size_t>(y) * m_width], static_cast<size_t>(slide) * sizeof(uint32_t));
                }
            }
        }
        if (stageDarken > 0) {
            const uint32_t keep = static_cast<uint32_t>(256 - std::min(stageDarken, 256));
            for (uint32_t &p : m_picture) {
                p = ((((p & 0xFF00FF) * keep) >> 8) & 0xFF00FF) | ((((p & 0x00FF00) * keep) >> 8) & 0x00FF00);
            }
        }
        m_renderer.overlay = true;
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return !me.ownStageObject(me.board, object);
        };
        if (!onlyStage) {
            m_renderer.Draw(board, m_picture, m_width, m_height);
        }
        m_renderer.overlay = false;
        m_renderer.drawGroup = nullptr;
    } else if (drawing && onlyStage && ownStageObject) {
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return me.ownStageObject(me.board, object);
        };
        m_renderer.text = false;
        m_renderer.Draw(board, m_picture, m_width, m_height);
        m_renderer.text = !hidePanel;
        m_renderer.drawGroup = nullptr;
    } else if (drawing && ownStageObject && getenv("CPS3RECOMP_OWN_ONLY")) {
        // (debugging aid: only what this board keeps over another's stage, on black)
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return !me.ownStageObject(me.board, object);
        };
        m_renderer.Draw(board, m_picture, m_width, m_height);
        m_renderer.drawGroup = nullptr;
    } else if (drawing && hideObject) {
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return !me.hideObject(me.board, object) && !(me.frontObject && me.frontObject(me.board, object));
        };
        m_renderer.Draw(board, m_picture, m_width, m_height);
        if (frontObject) {
            // (the second pass: the objects in front, over the rest)
            const bool text = m_renderer.text;
            m_renderer.text = false;
            m_renderer.overlay = true;
            m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return !me.hideObject(me.board, object) && me.frontObject(me.board, object);
            };
            m_renderer.Draw(board, m_picture, m_width, m_height);
            m_renderer.overlay = false;
            m_renderer.text = text;
        }
        m_renderer.drawGroup = nullptr;
    } else if (drawing) {
        m_renderer.Draw(board, m_picture, m_width, m_height);
        if (board.nativeCheck && m_renderer.margin == 0) {
            NativePixels();
        }
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

void Machine::SetStageBoard(std::unique_ptr<RomSet> set) {
    SetSecondStageBoard(false); // (of the set that goes)
    m_stagePcm.reset();
    if (m_stage) {
        Detach(*m_stage);
    }
    m_stage.reset();
    m_stageSet = std::move(set);
    if (m_stageSet) {
        // (the board's flash state is global, for the translated code: this board's is kept as it was)
        const bool plain = machine::g_flashPlain;
        m_stage = std::make_unique<machine::Machine>(*m_stageSet);
        m_stage->Reset();
        machine::g_flashPlain = plain;
        // (its own translated code, if the program has it: tools/translate.sh with GAME=its id)
        AttachSecond(*m_stage);
        m_stagePcm = std::make_unique<Pcm>(*m_stage);
    }
}

void Machine::DrawStageOnly(std::vector<uint32_t> &pixels) {
    if (!ownStageObject) {
        pixels.clear();
        return;
    }
    Renderer &r = m_renderer;
    const bool text = r.text;
    r.text = false;
    r.overlay = false;
    r.drawGroupContext = this;
    r.drawGroup = [](void *self, uint16_t, uint32_t object) {
        const Machine &me = *static_cast<Machine *>(self);
        return me.ownStageObject(me.board, object);
    };
    int w = m_width, h = m_height;
    r.Draw(board, pixels, w, h);
    r.drawGroup = nullptr;
    r.text = text;
}

void Machine::SetSecondStageBoard(bool on) {
    if (!on || !m_stageSet) {
        if (m_stage2) {
            Detach(*m_stage2);
        }
        m_stage2Pcm.reset();
        m_stage2.reset();
        stageSlide = 0;
        stageBlend = 0;
        secondStageGain = 0;
        return;
    }
    if (m_stage2) {
        return;
    }
    const bool plain = machine::g_flashPlain;
    m_stage2 = std::make_unique<machine::Machine>(*m_stageSet);
    m_stage2->Reset();
    machine::g_flashPlain = plain;
    AttachSecond(*m_stage2);
    m_stage2Pcm = std::make_unique<Pcm>(*m_stage2);
}

void Machine::KeepSecondStage(std::vector<uint8_t> &data) {
    data.clear();
    if (!m_stage2) {
        return;
    }
    Archive ar(data, true);
    m_stage2->Serialize(ar);
    m_stage2Pcm->Serialize(ar);
}

void Machine::RestoreSecondStage(const std::vector<uint8_t> &data) {
    if (!m_stage2 || data.empty()) {
        return;
    }
    std::vector<uint8_t> copy = data;
    Archive ar(copy, false);
    const bool plain = machine::g_flashPlain;
    Activate(*m_stage2);
    m_stage2->Serialize(ar);
    m_stage2Pcm->Serialize(ar);
    StateLoaded();
    Activate(board);
    machine::g_flashPlain = plain;
}

void Machine::SwapStageBoards() {
    if (!m_stage || !m_stage2) {
        return;
    }
    std::swap(m_stage, m_stage2);
    std::swap(m_stagePcm, m_stage2Pcm);
    std::swap(m_stageAudio, m_stage2Audio);
    stageSlide = 0;
    stageBlend = 0;
    secondStageGain = 0;
}

void Machine::RunStageQuiet(int frames) {
    if (!m_stage) {
        return;
    }
    for (int n = 0; n < frames; n++) {
        if (beforeStage) {
            beforeStage(*this, *m_stage);
        }
        const bool plain = machine::g_flashPlain;
        Activate(*m_stage);
        m_stage->RunFrame();
        Activate(board);
        machine::g_flashPlain = plain;
        m_stageAudio.clear();
        m_stagePcm->EndFrame(&m_stageAudio);
    }
}

void Machine::KeepStage(std::vector<uint8_t> &data) {
    data.clear();
    if (!m_stage) {
        return;
    }
    Archive ar(data, true);
    m_stage->Serialize(ar);
    m_stagePcm->Serialize(ar);
}

void Machine::RestoreStage(const std::vector<uint8_t> &data) {
    if (!m_stage || data.empty()) {
        return;
    }
    std::vector<uint8_t> copy = data;
    Archive ar(copy, false);
    const bool plain = machine::g_flashPlain;
    Activate(*m_stage);
    m_stage->Serialize(ar);
    m_stagePcm->Serialize(ar);
    StateLoaded(); // (its translated code's dispatcher: where its CPU was is forgotten)
    Activate(board);
    machine::g_flashPlain = plain;
}

void Machine::StepStage() {
    if (!m_stage) {
        return;
    }
    const int frames = stageFramesNow ? std::max(1, stageFramesNow()) : 1;
    // (the runtime runs the stage board's own code while it runs: its memory and its code's map; and the flash's
    // state, a global the translated code reads, is this board's again after it)
    m_stageAudio.clear();
    for (int n = 0; n < frames; n++) {
        if (beforeStage) {
            beforeStage(*this, *m_stage);
        }
        const bool plain = machine::g_flashPlain;
        Activate(*m_stage);
        m_stage->RunFrame();
        Activate(board);
        machine::g_flashPlain = plain;
        // Its sound: made every frame (so that it keeps time); the last frame's added to this board's when wanted
        m_stageAudio.clear();
        m_stagePcm->EndFrame(&m_stageAudio);
    }
    if (m_stage2) {
        m_stage2Audio.clear();
        const int frames2 = secondStageFramesNow ? std::max(1, secondStageFramesNow()) : 1;
        for (int n = 0; n < frames2; n++) {
            if (beforeSecondStage) {
                beforeSecondStage(*this, *m_stage2);
            }
            const bool plain = machine::g_flashPlain;
            Activate(*m_stage2);
            m_stage2->RunFrame();
            Activate(board);
            machine::g_flashPlain = plain;
            m_stage2Audio.clear();
            m_stage2Pcm->EndFrame(&m_stage2Audio);
        }
        // (the two stages' sound, the second's share of it as it comes in)
        const int gain = std::clamp(secondStageGain, 0, 256);
        const size_t n = std::min(m_stageAudio.size(), m_stage2Audio.size());
        for (size_t i = 0; i < n; i++) {
            m_stageAudio[i] = static_cast<int16_t>((m_stageAudio[i] * (256 - gain) + m_stage2Audio[i] * gain) / 256);
        }
    }
    if (stageSound) {
        const size_t n = std::min(m_stageAudio.size(), audio.size());
        int16_t *tail = audio.data() + (audio.size() - n);
        for (size_t i = 0; i < n; i++) {
            tail[i] = static_cast<int16_t>(std::clamp(tail[i] + m_stageAudio[i], -32768, 32767));
        }
    }
    // (debugging aid, CPS3RECOMP_STAGE_SHOTS=EVERY:prefix: the stage board's own whole picture)
    static const char *shots = getenv("CPS3RECOMP_STAGE_SHOTS");
    if (shots && frame % strtoull(shots, nullptr, 10) == 0) {
        Renderer whole;
        std::vector<uint32_t> pixels;
        int w = 0, h = 0;
        whole.Draw(*m_stage, pixels, w, h);
        const std::string path = std::string(strchr(shots, ':') + 1) + std::to_string(frame) + ".png";
        WritePng(path.c_str(), pixels.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }
}

void Machine::Serialize(Archive &ar) {
    ar.Mark(0x43505333);
    board.Serialize(ar);
    m_pcm.Serialize(ar);
    ar.Pod(frame);
    soundtrack.Serialize(ar);
}

} // namespace cps3recomp
