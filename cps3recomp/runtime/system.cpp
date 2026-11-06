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
#include "native_gpu.h"
#include "present.h"
#include <chrono>
#include <ctime>
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
    board.nativeCheck = native::Checking(); // (the game's board, in the checks: its rewrites hand the native interface what they draw)
    SetBoardSound(false);
    eeprom.words = board.eeprom.data();
    m_picture.assign(static_cast<size_t>(Video::kWidth) * Video::kHeight, 0);
}

void Machine::SetBoardSound(bool on) {
    m_boardSound = on;
    board.mixNative = !on || board.nativeCheck;
    // (pcm.cpp mixes as the chip's registers are written: only when it is heard or checked)
    const bool pcm = on || board.nativeCheck;
    board.beforeSoundWrite = pcm ? m_pcm.beforeWrite : nullptr;
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
        const char *label = "native pixels";
        ~PixelTotals() {
            if (frames) {
                fprintf(stderr, "cps3recomp: %s: %llu frames compared, %llu different\n", label, static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(different));
            }
        }
    } g_pixelTotals, g_stagePixelTotals{0, 0, 6, "native pixels with a stage"}, g_otherPixelTotals{0, 0, 6, "native pixels drawn otherwise"};
} // namespace

// (and with CPS3RECOMP_NATIVE_GPU=1: the GPU's picture of the same frame, read back, against the CPU rasteriser's)
namespace {
    struct GpuCheck {
        SDL_GPUDevice *device = nullptr;
        port::NativeGpu gpu;
        // (CPS3RECOMP_NATIVE_GPU_PRESENT=1: the frame as the window's presenter shows it, Presenter::ShowNative: the
        // compute passes and the render pass that samples the picture in one command buffer, here into a texture of the
        // presenter's own at 1:1 with the nearest pixel sampled; read back after its fence, in a command buffer of its own)
        port::Presenter presenter;
        bool present = false;
        std::vector<uint32_t> target;
        bool tried = false;
        uint64_t frames = 0, different = 0;
        unsigned reports = 6;
        std::vector<uint32_t> pixels;
        const char *label = "native GPU pixels";
        explicit GpuCheck(const char *l) : label(l) {}
        ~GpuCheck() {
            End();
        }
        // (the GPU's things let go of while the program still runs: from a static destructor, at exit, the Vulkan driver
        // had already gone and destroying the device crashed in it; EndNativeGpu)
        bool ended = false;
        void End() {
            if (ended) {
                return;
            }
            ended = true;
            if (frames || tried) {
                fprintf(stderr, "cps3recomp: %s: %llu frames compared, %llu different%s\n", label, static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(different), device ? "" : " (no GPU device)");
            }
            gpu.Close();
            presenter.Close();
            if (device && !present) { // (the presenter's own goes with it)
                SDL_DestroyGPUDevice(device);
            }
            device = nullptr;
        }
    } g_gpuCheck{"native GPU pixels"}, g_stageGpuCheck{"native GPU pixels with a stage"};
    void GpuCompare(GpuCheck &c, const native::Frame &f, const native::Video &video, const uint8_t *ss, const std::vector<uint32_t> &cpu, int width,
                    uint64_t frame, const char *whose);
} // namespace

// How the pictures shown were drawn: from the native interfaces, or by the board's renderer (with --board, in the checks,
// and when an interface did not hold the frame). Said at the end with CPS3RECOMP_STATS=1.
namespace {
    // (CPS3RECOMP_PICTURE_TIMES=1: how long each way took to draw a picture, mean and 99th percentile)
    struct ShownTotals {
        uint64_t native = 0, board = 0;
        std::vector<double> nativeTimes, boardTimes, abNative, abBoard;
        ~ShownTotals() {
            for (auto [name, times] : {std::pair<const char *, std::vector<double> *>{"native rasteriser", &abNative}, {"board's renderer", &abBoard}}) {
                if (times->empty()) {
                    continue;
                }
                double sum = 0;
                for (double t : *times) {
                    sum += t;
                }
                std::sort(times->begin(), times->end());
                const double p99 = (*times)[std::min(times->size() - 1, times->size() * 99 / 100)];
                fprintf(stderr, "cps3recomp: picture A/B, the %s: %zu frames, %.3f ms CPU a frame (mean), %.3f ms (99th percentile)\n", name,
                        times->size(), 1000 * sum / static_cast<double>(times->size()), 1000 * p99);
            }
            if (getenv("CPS3RECOMP_STATS") && (native || board)) {
                fprintf(stderr, "cps3recomp: pictures shown: %llu from the native interface, %llu by the board's renderer\n",
                        static_cast<unsigned long long>(native), static_cast<unsigned long long>(board));
            }
            for (auto [name, times] : {std::pair<const char *, std::vector<double> *>{"native rasteriser", &nativeTimes}, {"board's renderer", &boardTimes}}) {
                if (times->empty()) {
                    continue;
                }
                double sum = 0;
                for (double t : *times) {
                    sum += t;
                }
                std::sort(times->begin(), times->end());
                const double p99 = (*times)[std::min(times->size() - 1, times->size() * 99 / 100)];
                fprintf(stderr, "cps3recomp: picture drawn by the %s: %zu frames, %.3f ms a frame (mean), %.3f ms (99th percentile)\n", name,
                        times->size(), 1000 * sum / static_cast<double>(times->size()), 1000 * p99);
            }
        }
    } g_shownTotals;
} // namespace

// Whether this frame's picture is drawn from the native interfaces: always but with --board (boardPicture) and in the
// checks (the board's picture is the one the native one is compared with). Each board's interface (and the stage
// boards' when they are drawn) holds the frame: handed over by the game's code (machine.h, NativeInSync), or made
// again from the board's memories first when it was not (NativeForPicture: after a state put back, run-ahead's or
// rollback's frames, and the boot ROM's, which hand nothing over)
bool Machine::ShownNative() {
    if (boardPicture || board.nativeCheck || !native::On() || !board.NativeForPicture()) {
        return false;
    }
    if (m_stage && stageObject && (!composeNow || composeNow()) && !m_stage->NativeForPicture()) {
        return false;
    }
    return !(m_stage2 && (stageBlend > 0 || stageSlide > 0) && !m_stage2->NativeForPicture());
}

// One board's layers into `pixels` with the renderer's settings `r` (its margin, world shift, rows down, text layer,
// overlay, backdrop and choice of objects): by `r`, from the board's chips, or (native) by the native rasteriser from
// that board's native interface, the same pixels (checknative.sh)
void Machine::DrawLayers(bool native, Renderer &r, const machine::Machine &m, std::vector<uint32_t> &pixels, int &width, int &height) {
    if (!native) {
        r.Draw(m, pixels, width, height);
        return;
    }
    native::Rasteriser &n = m_shownRaster;
    n.margin = r.margin;
    n.shift = r.shift;
    n.shiftY = r.shiftY;
    n.text = r.text;
    n.overlay = r.overlay;
    n.backdrop = r.backdrop;
    m_shownKeep = {r.drawGroup, r.drawGroupContext};
    n.keep = r.drawGroup ? +[](const void *context, const native::Object &o, uint32_t index) {
        const ShownKeep &k = *static_cast<const ShownKeep *>(context);
        return k.fn(k.context, o.group, index);
    } : nullptr;
    n.keepContext = &m_shownKeep;
    n.Draw(m.native, m.SpriteRamBytes(), m.SsRam(), pixels, width, height);
}

// The frontmost priority of this board's fighters, for the plain composition of a stage board's stage under this
// board's layers (the stage's pieces in front of them drawn over them: native::Frame::front); 0 for the others (a second
// stage fading or scrolling in, the stage darkened or alone: the stage all under, as before)
int Machine::FrontNow() {
    if (!frontPriority || onlyStage || stageDarken > 0 || (m_stage2 && (stageBlend > 0 || stageSlide > 0))) {
        return 0;
    }
    return frontPriority(board);
}
bool Machine::ObjectInFront(const machine::Machine &m, uint32_t object, int front) {
    return object < m.native.objects.size() && native::InFront(m.native.objects[object].priority, front);
}

// Whether a frame with the stage board's stage is drawn as one frame of the two producers (DrawWithStage): the plain
// composition, the stage under this board's layers (not a second stage sliding or fading in, nor the stage darkened or
// alone: those are the modes' own stages', drawn as two pictures)
bool Machine::SingleFrameWithStage() const {
    return m_stage && nativeStageObject && nativeOwnStageObject && !(m_stage2 && (stageBlend > 0 || stageSlide > 0)) && stageDarken <= 0 && !onlyStage &&
           (!composeNow || composeNow());
}

// The stage board's stage layers (its objects nativeStageObject says are its stage, with its own palette and textures)
// and this board's layers but its own stage (nativeOwnStageObject) made into one frame and drawn by one rasteriser
// (native::Rasteriser::DrawWithGuest): the picture the two board models compose (checknative.sh, "native pixels with a
// stage")
void Machine::DrawWithStage(std::vector<uint32_t> &pixels, int &width, int &height) {
    native::Rasteriser &n = m_shownRaster;
    n.front = FrontNow();
    n.margin = m_renderer.margin;
    n.shift = m_renderer.shift;
    n.shiftY = 0;
    n.text = m_renderer.text;
    n.overlay = false;
    n.backdrop = m_renderer.backdrop;
    n.keepContext = this;
    n.keep = [](const void *self, const native::Object &o, uint32_t) { return !static_cast<const Machine *>(self)->nativeOwnStageObject(o); };
    n.DrawWithGuest(&m_stage->native, m_stage->SpriteRamBytes(),
                    [](const void *self, const native::Object &o, uint32_t) { return static_cast<const Machine *>(self)->nativeStageObject(o); }, this,
                    board.native, board.SpriteRamBytes(), board.SsRam(), pixels, width, height);
}

// The pixel check (docs/NATIVE.md, level b), every frame drawn in the checks: the frame's picture composed again from
// the native interfaces (Compose, as the board's was: the plain way, a stage board's stage under this board's layers,
// every other way the game's sources choose) against the board's. Frames drawn the plain way counted as "native pixels",
// those with a stage board's stage as "native pixels with a stage", the others as "native pixels drawn otherwise". And
// with CPS3RECOMP_NATIVE_GPU=1 the GPU's picture of a frame of one producer against the CPU's (the plain frame; with a
// stage board, each producer's part alone, the stage board's with no text layer, given an empty SS RAM as the CPU draws
// it)
void Machine::ComparePixels(int kind) {
    int width = m_width, height = m_height;
    Compose(true, m_nativePicture, width, height);
    PixelTotals &t = kind == 1 ? g_pixelTotals : kind == 2 ? g_stagePixelTotals : g_otherPixelTotals;
    t.frames++;
    if (width != m_width || height != m_height || m_nativePicture.size() < static_cast<size_t>(width) * height ||
        m_picture.size() < static_cast<size_t>(width) * height ||
        !std::equal(m_nativePicture.begin(), m_nativePicture.begin() + static_cast<size_t>(width) * height, m_picture.begin())) {
        t.different++;
        if (t.reports) {
            t.reports--;
            size_t at = 0, n = std::min(m_nativePicture.size(), m_picture.size());
            while (at < n && m_nativePicture[at] == m_picture[at]) {
                at++;
            }
            fprintf(stderr, "%s, frame %llu: %dx%d (board) / %dx%d (native), the first to differ at %d,%d: %06X / %06X\n", t.label,
                    static_cast<unsigned long long>(frame), m_width, m_height, width, height, width ? static_cast<int>(at % width) : 0,
                    width ? static_cast<int>(at / width) : 0, at < n ? m_picture[at] : 0, at < n ? m_nativePicture[at] : 0);
        }
    }
    static const bool gpu = getenv("CPS3RECOMP_NATIVE_GPU") != nullptr;
    if (!gpu) {
        return;
    }
    if (kind == 1) {
        GpuCompare(g_gpuCheck, m_shownRaster.LastFrame(), board.native, board.SsRam(), m_nativePicture, width, frame, "");
        return;
    }
    if (kind == 2 && m_shownRaster.LastFrame().guest) {
        // (the one frame of the two producers, DrawWithStage, as the CPU drew it just now)
        GpuCompare(g_stageGpuCheck, m_shownRaster.LastFrame(), board.native, board.SsRam(), m_nativePicture, width, frame, " with a stage");
    }
}

namespace {
// The GPU's picture of a frame (CPS3RECOMP_NATIVE_GPU=1), read back, against the CPU rasteriser's (`cpu`, `width` wide)
void GpuCompare(GpuCheck &c, const native::Frame &f, const native::Video &video, const uint8_t *ss, const std::vector<uint32_t> &cpu, int width,
                uint64_t frame, const char *whose) {
    static const bool gpu = getenv("CPS3RECOMP_NATIVE_GPU") != nullptr;
    if (!gpu) {
        return;
    }
    if (!c.tried) {
        c.tried = true;
        // (no window: SDL's offscreen video driver, unless another is asked for)
        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            SDL_InitSubSystem(SDL_INIT_VIDEO);
        }
        std::string error;
        c.present = getenv("CPS3RECOMP_NATIVE_GPU_PRESENT") != nullptr;
        if (c.present) {
            if (c.presenter.OpenOffscreen(port::NativeGpu::kPictureWidth, port::NativeGpu::kPictureHeight, true, error) &&
                c.presenter.CanShowNative()) {
                c.device = c.presenter.Device();
            } else {
                fprintf(stderr, "native GPU check (presented): %s\n", error.c_str());
            }
        } else {
            c.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, getenv("CPS3RECOMP_GPU_DEBUG") != nullptr, nullptr);
            if (!c.device || !c.gpu.Open(c.device, error)) {
                fprintf(stderr, "native GPU check: %s\n", c.device ? error.c_str() : SDL_GetError());
            }
        }
    }
    // (presented: as the window would, its picture's part read back)
    const int height = width ? static_cast<int>(cpu.size() / static_cast<size_t>(width)) : 0;
    auto presented = [&](std::vector<uint32_t> &out) {
        const SDL_FRect where = {0, 0, static_cast<float>(width), static_cast<float>(height)};
        if (!c.presenter.ShowNative(f, video, ss, where) || !c.presenter.ReadOffscreen(c.target)) {
            return false;
        }
        out.resize(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; y++) {
            std::copy_n(&c.target[static_cast<size_t>(y) * port::NativeGpu::kPictureWidth], width, &out[static_cast<size_t>(y) * width]);
        }
        return true;
    };
    if (c.present ? !c.presenter.CanShowNative() : !c.gpu.IsOpen()) {
        return;
    }
    if (c.present) {
        c.frames++;
        if (!presented(c.pixels) || c.pixels != cpu) {
            c.different++;
            std::vector<uint32_t> again;
            const bool sameAgain = presented(again) && again == cpu;
            fprintf(stderr, "native GPU pixels%s (presented), frame %llu: shown again: %s\n", whose, static_cast<unsigned long long>(frame),
                    sameAgain ? "the same as the CPU's" : "different");
            if (c.reports) {
                c.reports--;
                size_t at = 0, n = std::min(c.pixels.size(), cpu.size());
                while (at < n && c.pixels[at] == cpu[at]) {
                    at++;
                }
                fprintf(stderr, "native GPU pixels%s (presented), frame %llu: %zu / %zu pixels, the first to differ no. %zu (%d,%d): %06X (CPU) / %06X (GPU)\n",
                        whose, static_cast<unsigned long long>(frame), cpu.size(), c.pixels.size(), at, width ? static_cast<int>(at % width) : 0,
                        width ? static_cast<int>(at / width) : 0, at < n ? cpu[at] : 0, at < n ? c.pixels[at] : 0);
            }
        }
        return;
    }
    c.frames++;
    if (!c.gpu.DrawAndRead(f, video, ss, c.pixels) || c.pixels != cpu) {
        c.different++;
        // (for looking into it: the same frame drawn again by the GPU at once, as it was, and with every texture page sent
        // again; whether each comes out as the CPU's)
        std::vector<uint32_t> again;
        const bool sameAgain = c.gpu.DrawAndRead(f, video, ss, again) && again == cpu;
        c.gpu.ResendTextures();
        const bool sameResent = c.gpu.DrawAndRead(f, video, ss, again) && again == cpu;
        fprintf(stderr, "native GPU pixels%s, frame %llu: drawn again: %s; with the textures sent again: %s\n", whose,
                static_cast<unsigned long long>(frame), sameAgain ? "the same as the CPU's" : "different", sameResent ? "the same as the CPU's" : "different");
        if (c.reports) {
            c.reports--;
            size_t at = 0, n = std::min(c.pixels.size(), cpu.size());
            while (at < n && c.pixels[at] == cpu[at]) {
                at++;
            }
            fprintf(stderr, "native GPU pixels%s, frame %llu: %zu / %zu pixels, the first to differ no. %zu (%d,%d): %06X (CPU) / %06X (GPU)\n", whose,
                    static_cast<unsigned long long>(frame), cpu.size(), c.pixels.size(), at, width ? static_cast<int>(at % width) : 0,
                    width ? static_cast<int>(at / width) : 0, at < n ? cpu[at] : 0, at < n ? c.pixels[at] : 0);
        }
    }
}
} // namespace

// The bench (CPS3RECOMP_NATIVE_BENCH=WIDTHxHEIGHT, with the native interface kept, CPS3RECOMP_NATIVE=1, and no check):
// every frame drawn the plain way is shown both ways by a presenter of its own into a texture of that size, as the window
// would (FitPicture, the Display page's scaling): the CPU's picture (renderer.cpp, then Show: its upload and the render
// pass) and the GPU's (BuildFrame, then ShowNative: the uploads, the compute passes and the render pass). The CPU's time
// for each, and how long each command buffer then took to finish on the GPU (from its submit to its fence: the GPU's
// time, with the wait's own), are reported at the end. The two take turns as to which goes first.
namespace {
    struct Bench {
        port::Presenter presenter;
        bool tried = false, open = false;
        int width = 0, height = 0;
        uint64_t frames = 0;
        double draw = 0, show = 0, showGpu = 0, build = 0, showNative = 0, nativeGpu = 0;
        std::vector<double> drawEach, nativeEach, showGpuEach, nativeGpuEach;
        ~Bench() {
            End();
        }
        bool ended = false;
        void End() {
            if (ended) {
                return;
            }
            ended = true;
            presenter.Close();
            if (!frames) {
                return;
            }
            const auto ms = [&](double t) { return 1000 * t / static_cast<double>(frames); };
            const auto p99 = [](std::vector<double> v) {
                std::sort(v.begin(), v.end());
                return v.empty() ? 0.0 : 1000 * v[std::min(v.size() - 1, v.size() * 99 / 100)];
            };
            fprintf(stderr,
                    "cps3recomp: native bench (%dx%d), %llu frames, ms a frame (mean; 99th percentile):\n"
                    "  CPU path: renderer.cpp %.3f + Show %.3f = %.3f (%.3f); GPU after submit %.3f (%.3f)\n"
                    "  GPU path: BuildFrame %.3f + ShowNative %.3f = %.3f (%.3f); GPU after submit %.3f (%.3f)\n",
                    width, height, static_cast<unsigned long long>(frames), ms(draw), ms(show), ms(draw + show), p99(drawEach), ms(showGpu),
                    p99(showGpuEach), ms(build), ms(showNative), ms(build + showNative), p99(nativeEach), ms(nativeGpu), p99(nativeGpuEach));
        }
    } g_bench;
    double Seconds(std::chrono::steady_clock::time_point since) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
    }
} // namespace

void EndNativeGpu() {
    g_gpuCheck.End();
    g_stageGpuCheck.End();
    g_bench.End();
}

void Machine::BenchFrame() {
    Bench &b = g_bench;
    if (!b.tried) {
        b.tried = true;
        const char *size = getenv("CPS3RECOMP_NATIVE_BENCH");
        if (sscanf(size, "%dx%d", &b.width, &b.height) != 2 || b.width <= 0 || b.height <= 0) {
            b.width = 1920;
            b.height = 1080;
        }
        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            SDL_InitSubSystem(SDL_INIT_VIDEO);
        }
        std::string error;
        b.open = b.presenter.OpenOffscreen(b.width, b.height, false, error) && b.presenter.CanShowNative();
        if (!b.open) {
            fprintf(stderr, "native bench: %s\n", error.c_str());
        }
    }
    if (!b.open || !board.NativeInSync()) {
        return;
    }
    using Clock = std::chrono::steady_clock;
    const SDL_FRect where = port::FitPicture(b.width, b.height, m_width, m_height, port::Shape::Arcade, port::Scaling::Sharp);
    auto cpu = [&] {
        Clock::time_point at = Clock::now();
        m_renderer.Draw(board, m_picture, m_width, m_height);
        const double draw = Seconds(at);
        at = Clock::now();
        b.presenter.Show(m_picture.data(), m_width, m_height, where);
        const double gpu = b.presenter.WaitShown();
        const double show = Seconds(at) - gpu;
        b.draw += draw;
        b.show += show;
        b.drawEach.push_back(draw + show);
        b.showGpu += gpu;
        b.showGpuEach.push_back(gpu);
    };
    auto gpu = [&] {
        Clock::time_point at = Clock::now();
        native::BuildFrame(board.native, board.SpriteRamBytes(), m_renderer.margin, m_renderer.shift, m_gpuFrame);
        const double build = Seconds(at);
        at = Clock::now();
        b.presenter.ShowNative(m_gpuFrame, board.native, board.SsRam(), where);
        const double wait = b.presenter.WaitShown();
        const double show = Seconds(at) - wait;
        b.build += build;
        b.showNative += show;
        b.nativeEach.push_back(build + show);
        b.nativeGpu += wait;
        b.nativeGpuEach.push_back(wait);
    };
    if (b.frames++ % 2) {
        cpu();
        gpu();
    } else {
        gpu();
        cpu();
    }
}

void Machine::KeepGpuFrame() {
    const native::Video &v = board.native;
    for (int page = 0; page < native::Textures::kPages; page++) {
        const uint64_t version = v.textures.PageVersion(page);
        if (version != m_gpuPages[static_cast<size_t>(page)]) {
            m_gpuVideo.textures.PutPage(page, v.textures.Pixels() + static_cast<size_t>(page) * 0x10000);
            m_gpuPages[static_cast<size_t>(page)] = version;
        }
    }
    if (v.palette.Version() != m_gpuColours) {
        m_gpuVideo.palette.SetAll(v.palette.Colours());
        m_gpuColours = v.palette.Version();
    }
    m_gpuText.assign(board.SsRam(), board.SsRam() + 0x8000);
}

void Machine::KeepGpuGuest() {
    const native::Video &v = m_stage->native;
    for (int page = 0; page < native::Textures::kPages; page++) {
        const uint64_t version = v.textures.PageVersion(page);
        if (m_gpuGuest != m_stageMade || version != m_gpuGuestPages[static_cast<size_t>(page)]) {
            m_gpuGuestVideo.textures.PutPage(page, v.textures.Pixels() + static_cast<size_t>(page) * 0x10000);
            m_gpuGuestPages[static_cast<size_t>(page)] = version;
        }
    }
    if (m_gpuGuest != m_stageMade || v.palette.Version() != m_gpuGuestColours) {
        m_gpuGuestVideo.palette.SetAll(v.palette.Colours());
        m_gpuGuestColours = v.palette.Version();
    }
    m_gpuGuestVideo.registers = v.registers;
    m_gpuGuestVideo.objects = v.objects;
    m_gpuGuest = m_stageMade;
}

void Machine::MakePicture() const {
    if (!m_pictureOwed) {
        return;
    }
    m_pictureOwed = false;
    Machine &self = const_cast<Machine &>(*this);
    if (m_gpuFrame.guest) {
        // (a frame with the stage board's stage: drawn from the frame made for the GPU, and what it was made from as it
        // was then, the two producers' colours and textures and this board's text layer)
        native::Rasteriser &n = self.m_shownRaster;
        n.text = true; // (a frame made for the GPU has its text layer and no backdrop)
        n.overlay = false;
        n.backdrop = 0;
        n.DrawFrame(m_gpuFrame, m_gpuVideo, m_gpuText.data(), self.m_picture, self.m_width, self.m_height);
        return;
    }
    // (as the frame's plain picture is drawn: the renderer's settings are still the frame's)
    self.DrawLayers(native::On() && board.NativeInSync(), self.m_renderer, board, self.m_picture, self.m_width, self.m_height);
}

// The frame's picture into `pixels`, by the board's renderer or (native) the native rasteriser from the boards' native
// interfaces, the same steps either way (the game's sources' choices: the ring out's way down, a stage board's stage
// under this board's layers, the stage alone, objects hidden or put in front). 1: drawn the plain way; 2: a stage
// board's stage with this board's layers over it; 3: another way
int Machine::Compose(bool native, std::vector<uint32_t> &pixels, int &width, int &height) {
    if (dropRaise >= 0 && ownStageObject && dropOld.size() == static_cast<size_t>(PictureWidth()) * Video::kHeight) {
        // (the ring out's way down: the old stage's bottom rows at the top, this stage under them, the fighters over both)
        std::vector<uint32_t> stage;
        DrawStageOnly(native, stage);
        const int raise = std::clamp(dropRaise, 0, height);
        if (stage.size() == static_cast<size_t>(width) * height && dropOld.size() == stage.size()) {
            pixels.resize(stage.size());
            for (int y = 0; y < height; y++) {
                const uint32_t *from = y < raise ? &dropOld[static_cast<size_t>(height - raise + y) * width] : &stage[static_cast<size_t>(y - raise) * width];
                memcpy(&pixels[static_cast<size_t>(y) * width], from, static_cast<size_t>(width) * sizeof(uint32_t));
            }
            m_renderer.overlay = true;
            m_renderer.shiftY = raise;
            m_renderer.drawGroupContext = this;
            m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return !me.ownStageObject(me.board, object) && !(me.dropLifted && me.dropLifted(me.board, object));
            };
            DrawLayers(native, m_renderer, board, pixels, width, height);
            if (dropLifted) {
                const bool text = m_renderer.text;
                m_renderer.text = false;
                m_renderer.shiftY = raise - dropLift;
                m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                    const Machine &me = *static_cast<Machine *>(self);
                    return !me.ownStageObject(me.board, object) && me.dropLifted(me.board, object);
                };
                DrawLayers(native, m_renderer, board, pixels, width, height);
                m_renderer.text = text;
            }
            m_renderer.overlay = false;
            m_renderer.shiftY = 0;
            m_renderer.drawGroup = nullptr;
        }
    } else if (m_stage && stageObject && ownStageObject && (!composeNow || composeNow())) {
        if (native && SingleFrameWithStage()) {
            // (one frame of the two producers: the stage board's stage layers under this board's, one rasteriser)
            DrawWithStage(pixels, width, height);
            return 2;
        }
        // (two pictures: the stage board's stage, then this board's layers over it; in the plain composition, the stage's
        // pieces in front of this board's fighters left out of the first and drawn over the second, and this board's
        // things in front of those over them: as the one frame draws its four buffers, Rasteriser::Resolve)
        m_front = FrontNow();
        m_bands = false;
        if (m_front) {
            // (any of its stage's objects, on the picture or not: as the one frame asks, native::AddGuest)
            m_frontPiece = 0x100;
            for (const native::Object &o : m_stage->native.objects) {
                if (nativeStageObject && nativeStageObject(o) && native::InFront(o.priority, m_front)) {
                    m_bands = true;
                    m_frontPiece = std::min<int>(m_frontPiece, o.priority);
                }
            }
        }
        m_stageRenderer.margin = m_renderer.margin;
        m_stageRenderer.text = false;
        m_stageRenderer.drawGroupContext = this;
        m_stageRenderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return me.stageObject(*me.m_stage, object) && !(me.m_bands && ObjectInFront(*me.m_stage, object, me.m_front));
        };
        DrawLayers(native, m_stageRenderer, *m_stage, pixels, width, height);
        if (m_stage2 && stageBlend > 0) {
            m_stage2Renderer.margin = m_renderer.margin;
            m_stage2Renderer.text = false;
            m_stage2Renderer.drawGroupContext = this;
            m_stage2Renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return me.stageObject(*me.m_stage2, object);
            };
            int w2 = 0, h2 = 0;
            DrawLayers(native, m_stage2Renderer, *m_stage2, m_stage2Picture, w2, h2);
            if (w2 == width && h2 == height && m_stage2Picture.size() == pixels.size()) {
                const uint32_t put = static_cast<uint32_t>(std::clamp(stageBlend, 0, 256)), keep = 256 - put;
                for (size_t i = 0; i < pixels.size(); i++) {
                    const uint32_t a = pixels[i], b = m_stage2Picture[i];
                    pixels[i] = ((((a & 0xFF00FF) * keep + (b & 0xFF00FF) * put) >> 8) & 0xFF00FF) |
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
            DrawLayers(native, m_stage2Renderer, *m_stage2, m_stage2Picture, w2, h2);
            if (w2 == width && h2 == height) {
                const int slide = std::min(stageSlide, width);
                for (int y = 0; y < height; y++) {
                    uint32_t *row = &pixels[static_cast<size_t>(y) * width];
                    memmove(row, row + slide, static_cast<size_t>(width - slide) * sizeof(uint32_t));
                    memcpy(row + width - slide, &m_stage2Picture[static_cast<size_t>(y) * width], static_cast<size_t>(slide) * sizeof(uint32_t));
                }
            }
        }
        if (stageDarken > 0) {
            const uint32_t keep = static_cast<uint32_t>(256 - std::min(stageDarken, 256));
            for (uint32_t &p : pixels) {
                p = ((((p & 0xFF00FF) * keep) >> 8) & 0xFF00FF) | ((((p & 0x00FF00) * keep) >> 8) & 0x00FF00);
            }
        }
        m_renderer.overlay = true;
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return !me.ownStageObject(me.board, object) && !(me.m_bands && ObjectInFront(me.board, object, me.m_frontPiece));
        };
        const bool text = m_renderer.text;
        if (!onlyStage) {
            m_renderer.text = text && !m_bands;
            DrawLayers(native, m_renderer, board, pixels, width, height);
        }
        if (m_bands) {
            // (the stage's pieces in front, then this board's things in front of them, and its text layer)
            m_stageRenderer.overlay = true;
            m_stageRenderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return me.stageObject(*me.m_stage, object) && ObjectInFront(*me.m_stage, object, me.m_front);
            };
            DrawLayers(native, m_stageRenderer, *m_stage, pixels, width, height);
            m_stageRenderer.overlay = false;
            m_renderer.text = text;
            m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return !me.ownStageObject(me.board, object) && ObjectInFront(me.board, object, me.m_frontPiece);
            };
            DrawLayers(native, m_renderer, board, pixels, width, height);
        }
        m_renderer.text = text;
        m_renderer.overlay = false;
        m_renderer.drawGroup = nullptr;
        // (the plain composition: no second stage, fade or stage alone)
        return !onlyStage && stageDarken <= 0 && !(m_stage2 && (stageBlend > 0 || stageSlide > 0)) ? 2 : 3;
    } else if (onlyStage && ownStageObject) {
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return me.ownStageObject(me.board, object);
        };
        m_renderer.text = false;
        DrawLayers(native, m_renderer, board, pixels, width, height);
        m_renderer.text = !hidePanel;
        m_renderer.drawGroup = nullptr;
    } else if (ownStageObject && getenv("CPS3RECOMP_OWN_ONLY")) {
        // (debugging aid: only what this board keeps over another's stage, on black)
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return !me.ownStageObject(me.board, object);
        };
        DrawLayers(native, m_renderer, board, pixels, width, height);
        m_renderer.drawGroup = nullptr;
    } else if (hideObject) {
        m_renderer.drawGroupContext = this;
        m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
            const Machine &me = *static_cast<Machine *>(self);
            return !me.hideObject(me.board, object) && !(me.frontObject && me.frontObject(me.board, object));
        };
        DrawLayers(native, m_renderer, board, pixels, width, height);
        if (frontObject) {
            // (the second pass: the objects in front, over the rest)
            const bool text = m_renderer.text;
            m_renderer.text = false;
            m_renderer.overlay = true;
            m_renderer.drawGroup = [](void *self, uint16_t, uint32_t object) {
                const Machine &me = *static_cast<Machine *>(self);
                return !me.hideObject(me.board, object) && me.frontObject(me.board, object);
            };
            DrawLayers(native, m_renderer, board, pixels, width, height);
            m_renderer.overlay = false;
            m_renderer.text = text;
        }
        m_renderer.drawGroup = nullptr;
    } else {
        DrawLayers(native, m_renderer, board, pixels, width, height);
        return 1;
    }
    return 3;
}

void Machine::RunFrame() {
    // (a frame not drawn keeps the last picture: the one owed, if it was made for the GPU)
    if (m_pictureOwed && (!this->drawing || holdPicture)) {
        MakePicture();
    }
    m_pictureOwed = false; // (a frame drawn draws all of the picture, whichever way)
    m_gpuReady = false;
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
    static const bool benching = getenv("CPS3RECOMP_NATIVE_BENCH") != nullptr; // (Machine::BenchFrame)
    // (taken for every frame whose picture may be drawn from it: the GPU's, or the CPU's from the native interface)
    board.nativeWanted = gpuPicture || benching || (this->drawing && !holdPicture && !boardPicture && native::On());
    board.RunFrame();
    // (the native mixer's frame first: the check, in pcm.cpp's, compares the board's with it)
    if (board.mixNative) {
        board.nativeSound.EndFrame(board.Samples(), board.SamplesSize(), board.silenced, board.nativeCheck);
        if (!m_boardSound) {
            board.nativeSound.Output(audio, m_pcm.gain);
        }
    }
    if (m_boardSound || board.nativeCheck) {
        m_pcm.EndFrame(m_boardSound ? &audio : nullptr);
    } else {
        m_pcm.SkipFrame();
    }
    // (the mixer not heard kept where the heard one is, so that a save state has both as they stand: a state saved
    // with either loads with the other)
    if (!board.mixNative) {
        board.NativeSoundFromBoard(m_pcm.Carry(), m_pcm.FrameSamples());
    } else if (!m_boardSound && !board.nativeCheck) {
        for (int v = 0; v < 16; v++) {
            board.voices[v].position = board.nativeSound.Position(v);
            board.voices[v].fraction = board.nativeSound.Fraction(v);
        }
    }
    const bool drawing = this->drawing && !holdPicture;
    m_renderer.text = !hidePanel;
    m_renderer.backdrop = backdrop;
    m_renderer.shift = worldShiftNow && m_renderer.margin ? std::clamp(worldShiftNow(), -m_renderer.margin, m_renderer.margin) : 0;
    const bool dropping = dropRaise >= 0 && ownStageObject && dropOld.size() == static_cast<size_t>(PictureWidth()) * Video::kHeight;
    const bool stageComposed = m_stage && stageObject && ownStageObject && (!composeNow || composeNow());
    const bool plainFrame = !dropping && !stageComposed && !(onlyStage && ownStageObject) && !(ownStageObject && getenv("CPS3RECOMP_OWN_ONLY")) && !hideObject;
    // (a frame of the stage board's stage under this board's layers: one frame of the two producers, the GPU's too)
    const bool stageFrame = !dropping && stageComposed && SingleFrameWithStage();
    if (drawing && (plainFrame || stageFrame) && gpuPicture && !board.nativeCheck && board.NativeInSync() && (!stageFrame || m_stage->NativeForPicture()) &&
        m_renderer.text && !m_renderer.backdrop) {
        // (for the GPU: the frame made ready, the picture drawn on the CPU only if asked for)
        if (stageFrame) {
            KeepGpuGuest();
            native::BuildFrame(board.native, board.SpriteRamBytes(), m_renderer.margin, m_renderer.shift, m_gpuFrame,
                               [](const void *self, const native::Object &o, uint32_t) { return !static_cast<const Machine *>(self)->nativeOwnStageObject(o); }, this,
                               0, FrontNow());
            native::AddGuest(m_gpuGuestVideo, m_stage->SpriteRamBytes(), m_gpuFrame,
                             [](const void *self, const native::Object &o, uint32_t) { return static_cast<const Machine *>(self)->nativeStageObject(o); }, this);
        } else {
            native::BuildFrame(board.native, board.SpriteRamBytes(), m_renderer.margin, m_renderer.shift, m_gpuFrame);
        }
        KeepGpuFrame();
        m_width = m_gpuFrame.width;
        m_height = m_gpuFrame.height;
        m_pictureOwed = true;
        m_gpuReady = true;
    } else if (drawing) {
        // The picture: from the native interfaces (the native rasteriser) when they hold the frame, else (and with
        // --board, and in the checks, where the native one is compared with it) by the board's renderer
        const bool native = ShownNative();
        (native ? g_shownTotals.native : g_shownTotals.board)++;
        static const bool timed = getenv("CPS3RECOMP_PICTURE_TIMES") != nullptr;
        const auto at = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const int kind = Compose(native, m_picture, m_width, m_height);
        if (timed) {
            (native ? g_shownTotals.nativeTimes : g_shownTotals.boardTimes).push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - at).count());
        }
        if (board.nativeCheck && kind) {
            ComparePixels(kind);
        }
        static const bool bench = getenv("CPS3RECOMP_NATIVE_BENCH") != nullptr;
        if (bench && !board.nativeCheck && kind == 1) {
            BenchFrame();
        }
        // (CPS3RECOMP_PICTURE_AB=1: the frame drawn both ways again, the order alternating, each timed by the thread's
        // own CPU time (the machine's other load left out), mean and 99th percentile at the end: renderer.cpp against the
        // native rasteriser, the same picture)
        static const bool ab = getenv("CPS3RECOMP_PICTURE_AB") != nullptr;
        if (ab && native) {
            static std::vector<uint32_t> scratch;
            const auto cpu = [] {
                timespec t;
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
                return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
            };
            for (int turn = 0; turn < 2; turn++) {
                const bool which = (turn == 0) == (frame % 2 == 0);
                int w = m_width, h = m_height;
                const double at = cpu();
                Compose(which, scratch, w, h);
                (which ? g_shownTotals.abNative : g_shownTotals.abBoard).push_back(cpu() - at);
            }
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
    m_stageMade++;
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
        m_stage->nativeCheck = native::Checking(); // (in the checks: its hand-offs to its own interface checked as the game board's are)
        machine::g_flashPlain = plain;
        // (its own translated code, if the program has it: tools/translate.sh with GAME=its id)
        AttachSecond(*m_stage);
        m_stagePcm = std::make_unique<Pcm>(*m_stage);
    }
}

void Machine::DrawStageOnly(std::vector<uint32_t> &pixels) {
    DrawStageOnly(ShownNative(), pixels);
}

void Machine::DrawStageOnly(bool native, std::vector<uint32_t> &pixels) {
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
    DrawLayers(native, r, board, pixels, w, h);
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
    m_stage2->nativeCheck = native::Checking();
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
    m_stageMade++;
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
        m_stage->nativeWanted = false; // (unseen: its interface made again from its memories when next wanted)
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

namespace {
    // (CPS3RECOMP_STAGE_TIMES=FROM: the stage boards' frames' CPU time (the thread's), each shown frame from FROM on,
    // mean and 99th percentile at the end)
    struct StageTimes {
        std::vector<double> times;
        ~StageTimes() {
            if (times.empty()) {
                return;
            }
            double sum = 0;
            for (double t : times) {
                sum += t;
            }
            std::sort(times.begin(), times.end());
            fprintf(stderr, "cps3recomp: the stage boards' frames: %zu shown frames, %.3f ms CPU a frame (mean), %.3f ms (99th percentile)\n",
                    times.size(), 1000 * sum / static_cast<double>(times.size()), 1000 * times[std::min(times.size() - 1, times.size() * 99 / 100)]);
        }
    } g_stageTimes;
    double ThreadSeconds() {
        timespec t;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
        return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
    }
} // namespace

void Machine::StepStage() {
    if (!m_stage) {
        return;
    }
    static const long long timesFrom = getenv("CPS3RECOMP_STAGE_TIMES") ? atoll(getenv("CPS3RECOMP_STAGE_TIMES")) : -1;
    const double timedFrom = timesFrom >= 0 && static_cast<long long>(frame) >= timesFrom ? ThreadSeconds() : 0;
    struct Timed {
        double from;
        ~Timed() {
            if (from) {
                g_stageTimes.times.push_back(ThreadSeconds() - from);
            }
        }
    } timed{timedFrom};
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
        // (its interface taken when this board's is: its stage game's hand-offs run dry on it, video_games.cpp, so it
        // stays in step for a GPU to draw its layers; machine.h, NativeInSync)
        m_stage->nativeWanted = board.nativeWanted;
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
            m_stage2->nativeWanted = board.nativeWanted;
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
    if (!ar.Saving() && !ar.inMemory) {
        MakePicture(); // (the picture stays the one shown until a frame is drawn: of what was there)
        m_gpuReady = false;
    }
    // (put back from memory, by run-ahead or rollback: the frame shown stays the last one run, the GPU's from what was
    // kept of it, GpuVideo; its CPU picture, if asked for before the next frame, is of what was put back)
    ar.Mark(0x43505333);
    board.Serialize(ar);
    m_pcm.Serialize(ar);
    if (!ar.Saving() && ar.version != 0 && ar.version < 4) {
        // (a state from before the native mixer was kept: it plays on from the chip as the state has it)
        board.NativeSoundFromBoard(m_pcm.Carry(), m_pcm.FrameSamples());
    }
    ar.Pod(frame);
    soundtrack.Serialize(ar);
}

} // namespace cps3recomp
