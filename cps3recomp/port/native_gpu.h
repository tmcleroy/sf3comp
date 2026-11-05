// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// The native video drawn by the GPU (docs/NATIVE.md): SDL_GPU's compute, from the interface's frame (video/raster.h:
// its tiles in the board's order, the palette, the textures, the text layer's memory), into a texture the presenter
// shows. Two passes: the buffer of colour numbers (each pixel from the tiles over its row), then the picture (through
// the palette, the whole picture's zoom, the text layer over it). The same integer steps as the CPU's rasteriser, so the
// two make the same pixels; the pixel check (CPS3RECOMP_NATIVE_GPU=1 with CPS3RECOMP_NATIVE_CHECK=1) reads the GPU's
// back and compares them.
#pragma once

#include "raster.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace port {

class NativeGpu {
public:
    // The picture's texture (Picture): the frame's in its top left, Width() x Height()
    static constexpr int kPictureWidth = 1024, kPictureHeight = 256;
    ~NativeGpu();
    // On a device of the caller's (the presenter's, or one of its own for the check); false with the reason
    bool Open(SDL_GPUDevice *device, std::string &error);
    void Close();
    bool IsOpen() const {
        return m_tiles != nullptr;
    }
    // The frame drawn (its picture in Picture(), `width` x `height` of it): the commands go in `cmd` (or in the one
    // returned, which the caller submits)
    // What the port puts over the picture (the readout, the lines at the bottom, the menu), `width` x `height` of it,
    // a pixel each (red in the low byte): its colour over black and, in the high byte, how much of what is under it it
    // covers (port/main.cpp, Probe). The picture under it is the frame's, its pixels at x * frame width / `width`,
    // y * frame height / `height` (the menu's is three times as tall). `rawAbove`: the frame's picture as it is first and
    // the one with the overlay under it, twice as tall (the camera's zoom). `version`: changes when the pixels do (sent
    // again only then)
    struct Overlay {
        const uint32_t *pixels = nullptr;
        int width = 0, height = 0;
        bool rawAbove = false;
        uint64_t version = 0;
    };
    static constexpr int kOverlayWidth = 2048, kOverlayHeight = 1024; // (the most it can be: the menu's picture)
    SDL_GPUCommandBuffer *Draw(SDL_GPUCommandBuffer *cmd, const cps3recomp::native::Frame &frame, const cps3recomp::native::Video &video, const uint8_t *ssRam,
                               const Overlay *overlay = nullptr);
    // What the presenter samples after Draw: the picture, or with an overlay the picture with it (`width` x `height` of
    // a texture `textureWidth` x `textureHeight`; twice as tall with rawAbove)
    struct Shown {
        SDL_GPUTexture *texture = nullptr;
        int textureWidth = 0, textureHeight = 0, width = 0, height = 0;
    };
    Shown LastShown() const {
        return m_lastShown;
    }
    SDL_GPUTexture *Picture() const {
        return m_picture;
    }
    int Width() const {
        return m_width;
    }
    int Height() const {
        return m_height;
    }
    // (the check) the frame drawn and read back, 0x00BBGGRR as the CPU's; false if the GPU failed
    bool DrawAndRead(const cps3recomp::native::Frame &frame, const cps3recomp::native::Video &video, const uint8_t *ssRam, std::vector<uint32_t> &pixels);
    // Every texture page sent again with the next frame
    void ResendTextures() {
        m_texturesSent = false;
    }

private:
    SDL_GPUDevice *m_device = nullptr;
    SDL_GPUComputePipeline *m_tilesPass = nullptr, *m_resolvePass = nullptr, *m_composePass = nullptr;
    SDL_GPUBuffer *m_composeParams = nullptr;
    SDL_GPUTexture *m_overlay = nullptr, *m_composed = nullptr;
    uint64_t m_overlaySent = 0;
    bool m_overlayKnown = false;
    Shown m_lastShown;
    SDL_GPUBuffer *m_tilesParams = nullptr, *m_tiles = nullptr, *m_rowStarts = nullptr, *m_rowTiles = nullptr, *m_textures = nullptr;
    SDL_GPUBuffer *m_resolveParams = nullptr, *m_palette = nullptr, *m_text = nullptr;
    SDL_GPUTransferBuffer *m_upload = nullptr, *m_download = nullptr;
    SDL_GPUTexture *m_colours = nullptr, *m_picture = nullptr;
    uint32_t m_tilesCapacity = 0, m_rowTilesCapacity = 0, m_uploadCapacity = 0;
    std::array<uint64_t, 128> m_pagesSent{}; // (the textures' pages as last sent: their versions)
    bool m_texturesSent = false; // (and the palette: false, all of both sent with the next frame)
    uint64_t m_paletteSent = 0;
    int m_width = 0, m_height = 0;
    std::vector<uint32_t> m_rowStartsData, m_rowTilesData, m_rgb;
    bool Grow(SDL_GPUBuffer *&buffer, uint32_t &capacity, uint32_t bytes);
};

} // namespace port
