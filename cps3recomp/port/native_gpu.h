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
    ~NativeGpu();
    // On a device of the caller's (the presenter's, or one of its own for the check); false with the reason
    bool Open(SDL_GPUDevice *device, std::string &error);
    void Close();
    bool IsOpen() const {
        return m_tiles != nullptr;
    }
    // The frame drawn (its picture in Picture(), `width` x `height` of it): the commands go in `cmd`
    void Draw(SDL_GPUCommandBuffer *cmd, const cps3recomp::native::Frame &frame, const cps3recomp::native::Video &video, const uint8_t *ssRam);
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

private:
    SDL_GPUDevice *m_device = nullptr;
    SDL_GPUComputePipeline *m_tilesPass = nullptr, *m_resolvePass = nullptr;
    SDL_GPUBuffer *m_tilesParams = nullptr, *m_tiles = nullptr, *m_rowStarts = nullptr, *m_rowTiles = nullptr, *m_textures = nullptr;
    SDL_GPUBuffer *m_resolveParams = nullptr, *m_palette = nullptr, *m_text = nullptr;
    SDL_GPUTransferBuffer *m_upload = nullptr, *m_download = nullptr;
    SDL_GPUTexture *m_colours = nullptr, *m_picture = nullptr;
    uint32_t m_tilesCapacity = 0, m_rowTilesCapacity = 0, m_uploadCapacity = 0;
    std::array<uint64_t, 128> m_pagesSent{}; // (the textures' pages as last sent: their versions)
    bool m_texturesSent = false;
    int m_width = 0, m_height = 0;
    std::vector<uint32_t> m_rowStartsData, m_rowTilesData, m_rgb;
    bool Grow(SDL_GPUBuffer *&buffer, uint32_t &capacity, uint32_t bytes);
};

} // namespace port
