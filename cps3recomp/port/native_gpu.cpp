// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#include "native_gpu.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace port {

namespace {
    // (debugging aid, CPS3RECOMP_NATIVE_GPU_TIMING=1: each pass submitted alone and waited for, its time on the GPU, the wait's
    // own included, reported at the end: the most and the mean)
    struct PassTimes {
        const char *names[4] = {"uploads", "tiles", "resolve", "compose"};
        double most[4] = {}, total[4] = {};
        uint64_t count[4] = {};
        ~PassTimes() {
            if (!count[0]) {
                return;
            }
            for (int i = 0; i < 4; i++) {
                fprintf(stderr, "cps3recomp: native GPU pass %s: %llu, mean %.3f ms, most %.3f ms\n", names[i], static_cast<unsigned long long>(count[i]),
                        count[i] ? 1000 * total[i] / static_cast<double>(count[i]) : 0.0, 1000 * most[i]);
            }
        }
    } g_passTimes;
    bool Timing() {
        static const bool on = getenv("CPS3RECOMP_NATIVE_GPU_TIMING") != nullptr;
        return on;
    }
    // (the pass just recorded submitted and waited for, timed; recording goes on in a new command buffer)
    SDL_GPUCommandBuffer *TimePass(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, int pass) {
        const Uint64 from = SDL_GetTicksNS();
        if (SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
            SDL_WaitForGPUFences(device, true, &fence, 1);
            SDL_ReleaseGPUFence(device, fence);
        }
        const double t = static_cast<double>(SDL_GetTicksNS() - from) / 1e9;
        g_passTimes.total[pass] += t;
        g_passTimes.most[pass] = std::max(g_passTimes.most[pass], t);
        g_passTimes.count[pass]++;
        return SDL_AcquireGPUCommandBuffer(device);
    }
#include "native_shaders.inc"

    constexpr int kBufferWidth = 1024, kBufferHeight = 448;
    constexpr uint32_t kTexturesSize = cps3recomp::native::Textures::kSize, kPage = 0x10000;
    constexpr uint32_t kPaletteSize = cps3recomp::native::Palette::kColours * 4, kTextSize = 0x8000;
    // (the game's buffer's rows and a guest's; the palettes: the game's, a guest's, the blends' 48 scales)
    constexpr int kRows = 2 * kBufferHeight;
    constexpr uint32_t kPalettesSize = 2 * kPaletteSize + 64 * 4;

    SDL_GPUComputePipeline *MakePass(SDL_GPUDevice *device, const unsigned char *spirv, size_t spirvSize, const char *msl, uint32_t buffers,
                                     uint32_t textures) {
        SDL_GPUComputePipelineCreateInfo info = {};
        if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) {
            info.format = SDL_GPU_SHADERFORMAT_MSL;
            info.code = reinterpret_cast<const Uint8 *>(msl);
            info.code_size = strlen(msl);
            info.entrypoint = "main0";
        } else {
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.code = spirv;
            info.code_size = spirvSize;
            info.entrypoint = "main";
        }
        info.num_readonly_storage_buffers = buffers;
        info.num_readwrite_storage_textures = textures;
        info.threadcount_x = 8;
        info.threadcount_y = 8;
        info.threadcount_z = 1;
        return SDL_CreateGPUComputePipeline(device, &info);
    }
    SDL_GPUBuffer *MakeBuffer(SDL_GPUDevice *device, uint32_t size) {
        SDL_GPUBufferCreateInfo info = {};
        info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        info.size = size;
        return SDL_CreateGPUBuffer(device, &info);
    }
    SDL_GPUTexture *MakeTexture(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = static_cast<Uint32>(width);
        info.height = static_cast<Uint32>(height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        return SDL_CreateGPUTexture(device, &info);
    }
} // namespace

NativeGpu::~NativeGpu() {
    Close();
}

bool NativeGpu::Open(SDL_GPUDevice *device, std::string &error) {
    m_device = device;
    m_tilesPass = MakePass(device, k_native_tiles_comp_spirv, sizeof(k_native_tiles_comp_spirv), k_native_tiles_comp_msl, 5, 1);
    m_resolvePass = MakePass(device, k_native_resolve_comp_spirv, sizeof(k_native_resolve_comp_spirv), k_native_resolve_comp_msl, 3, 2);
    m_composePass = MakePass(device, k_native_compose_comp_spirv, sizeof(k_native_compose_comp_spirv), k_native_compose_comp_msl, 1, 3);
    m_composeParams = MakeBuffer(device, 32);
    m_tilesParams = MakeBuffer(device, 32);
    m_resolveParams = MakeBuffer(device, 48);
    m_rowStarts = MakeBuffer(device, (kRows + 1) * 4);
    m_textures = MakeBuffer(device, 2 * kTexturesSize);
    m_palette = MakeBuffer(device, kPalettesSize);
    m_text = MakeBuffer(device, kTextSize);
    // (the game's buffer of colour numbers, and a guest's below it: Frame::guest)
    m_colours = MakeTexture(device, SDL_GPU_TEXTUREFORMAT_R32_UINT,
                            SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE, kBufferWidth,
                            kRows);
    m_picture = MakeTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
                                SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE,
                            kBufferWidth, 256);
    // (what is put over it, and the two together: NativeGpu::Overlay)
    m_overlay = MakeTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                            SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE, kOverlayWidth,
                            kOverlayHeight);
    m_composed = MakeTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                             kOverlayWidth, kOverlayHeight);
    if (!m_tilesPass || !m_resolvePass || !m_composePass || !m_composeParams || !m_overlay || !m_composed || !m_tilesParams || !m_resolveParams || !m_rowStarts || !m_textures || !m_palette || !m_text ||
        !m_colours || !m_picture || !Grow(m_tiles, m_tilesCapacity, 48 * 4096) || !Grow(m_rowTiles, m_rowTilesCapacity, 4 * 65536)) {
        error = SDL_GetError();
        Close();
        return false;
    }
    m_texturesSent = false;
    m_guestSent = nullptr;
    m_overlayKnown = false;
    return true;
}

void NativeGpu::Close() {
    if (!m_device) {
        return;
    }
    for (SDL_GPUComputePipeline *p : {m_tilesPass, m_resolvePass, m_composePass}) {
        if (p) {
            SDL_ReleaseGPUComputePipeline(m_device, p);
        }
    }
    for (SDL_GPUBuffer *b : {m_tilesParams, m_tiles, m_rowStarts, m_rowTiles, m_textures, m_resolveParams, m_palette, m_text, m_composeParams}) {
        if (b) {
            SDL_ReleaseGPUBuffer(m_device, b);
        }
    }
    for (SDL_GPUTransferBuffer *t : {m_upload, m_download}) {
        if (t) {
            SDL_ReleaseGPUTransferBuffer(m_device, t);
        }
    }
    for (SDL_GPUTexture *t : {m_colours, m_picture, m_overlay, m_composed}) {
        if (t) {
            SDL_ReleaseGPUTexture(m_device, t);
        }
    }
    m_tilesPass = m_resolvePass = m_composePass = nullptr;
    m_composeParams = nullptr;
    m_overlay = m_composed = nullptr;
    m_lastShown = {};
    m_tilesParams = m_tiles = m_rowStarts = m_rowTiles = m_textures = m_resolveParams = m_palette = m_text = nullptr;
    m_upload = m_download = nullptr;
    m_colours = m_picture = nullptr;
    m_tilesCapacity = m_rowTilesCapacity = m_uploadCapacity = 0;
    m_device = nullptr;
}

bool NativeGpu::Grow(SDL_GPUBuffer *&buffer, uint32_t &capacity, uint32_t bytes) {
    if (buffer && capacity >= bytes) {
        return true;
    }
    if (buffer) {
        SDL_ReleaseGPUBuffer(m_device, buffer);
    }
    capacity = std::max<uint32_t>(bytes, capacity * 2);
    buffer = MakeBuffer(m_device, capacity);
    return buffer != nullptr;
}

SDL_GPUCommandBuffer *NativeGpu::Draw(SDL_GPUCommandBuffer *cmd, const cps3recomp::native::Frame &f, const cps3recomp::native::Video &v, const uint8_t *ss,
                                      const Overlay *overlay) {
    using cps3recomp::native::Tile;
    m_width = f.width;
    m_height = f.height;
    // The tiles by row: each row's tiles in the board's order (a guest's in the rows from kBufferHeight on: its buffer)
    const cps3recomp::native::Video *guest = f.guest ? f.guestVideo : nullptr;
    const uint32_t rows = guest ? static_cast<uint32_t>(kBufferHeight + f.guestBufferHeight) : static_cast<uint32_t>(f.bufferHeight);
    const auto rowOf = [](const Tile &t, int y) { return t.producer ? kBufferHeight + y : y; };
    m_rowStartsData.assign(kRows + 1, 0);
    for (const Tile &t : f.tiles) {
        for (int y = t.sy; y < t.ey && y < kBufferHeight; y++) {
            m_rowStartsData[static_cast<size_t>(rowOf(t, y)) + 1]++;
        }
    }
    for (int y = 0; y < kRows; y++) {
        m_rowStartsData[static_cast<size_t>(y) + 1] += m_rowStartsData[static_cast<size_t>(y)];
    }
    m_rowTilesData.assign(std::max<uint32_t>(m_rowStartsData[kRows], 1), 0);
    {
        std::vector<uint32_t> at(m_rowStartsData.begin(), m_rowStartsData.end() - 1);
        for (uint32_t i = 0; i < f.tiles.size(); i++) {
            for (int y = f.tiles[i].sy; y < f.tiles[i].ey && y < kBufferHeight; y++) {
                m_rowTilesData[at[static_cast<size_t>(rowOf(f.tiles[i], y))]++] = i;
            }
        }
    }
    const uint32_t tileBytes = static_cast<uint32_t>(std::max<size_t>(f.tiles.size(), 1) * sizeof(Tile));
    const uint32_t rowTileBytes = static_cast<uint32_t>(m_rowTilesData.size() * 4);
    Grow(m_tiles, m_tilesCapacity, tileBytes);
    Grow(m_rowTiles, m_rowTilesCapacity, rowTileBytes);
    // The palette's colours as drawn (the video DAC's low three bits of each component grounded): sent when they may have
    // changed (Palette::Version)
    const auto toRgb = [](const uint16_t *from, std::vector<uint32_t> &rgb) {
        rgb.resize(cps3recomp::native::Palette::kColours);
        for (uint32_t i = 0; i < cps3recomp::native::Palette::kColours; i++) {
            const uint16_t c = from[i];
            rgb[i] = (static_cast<uint32_t>(c & 0x1F) << 3) | (static_cast<uint32_t>((c >> 5) & 0x1F) << 11) | (static_cast<uint32_t>((c >> 10) & 0x1F) << 19);
        }
    };
    const bool colours = !m_texturesSent || v.palette.Version() != m_paletteSent;
    if (colours) {
        m_paletteSent = v.palette.Version();
        toRgb(v.palette.Colours(), m_rgb);
    }
    // (a guest's: its colours, and the game's blends' scales, when either may have changed)
    if (guest && m_guestSent != guest) {
        m_guestSent = guest;
        m_guestPaletteSent = 0;
        for (int page = 0; page < cps3recomp::native::Textures::kPages; page++) {
            m_pagesSent[static_cast<size_t>(cps3recomp::native::Textures::kPages + page)] = ~0ull;
        }
    }
    const bool guestColours = guest && (!m_texturesSent || guest->palette.Version() != m_guestPaletteSent);
    if (guestColours) {
        m_guestPaletteSent = guest->palette.Version();
        toRgb(guest->palette.Colours(), m_guestRgb);
    }
    const bool scales = guest && (!m_texturesSent || v.palette.Version() != m_scalesFrom);
    if (scales) {
        m_scalesFrom = v.palette.Version();
        cps3recomp::native::BlendScales(v.palette.Colours(), m_scales);
    }
    // The textures' pages changed since last sent
    std::vector<int> pages; // (a guest's from 128)
    static const bool all = getenv("CPS3RECOMP_NATIVE_GPU_ALL_PAGES") != nullptr; // (debugging aid: every page sent every frame)
    for (int page = 0; page < cps3recomp::native::Textures::kPages; page++) {
        if (all || !m_texturesSent || v.textures.PageVersion(page) != m_pagesSent[static_cast<size_t>(page)]) {
            pages.push_back(page);
            m_pagesSent[static_cast<size_t>(page)] = v.textures.PageVersion(page);
        }
    }
    for (int page = 0; guest && page < cps3recomp::native::Textures::kPages; page++) {
        const size_t slot = static_cast<size_t>(cps3recomp::native::Textures::kPages + page);
        if (all || !m_texturesSent || guest->textures.PageVersion(page) != m_pagesSent[slot]) {
            pages.push_back(cps3recomp::native::Textures::kPages + page);
            m_pagesSent[slot] = guest->textures.PageVersion(page);
        }
    }
    m_texturesSent = true;
    // (debugging aid, CPS3RECOMP_NATIVE_GPU_SHADOW=1: the pages not sent compared with what was sent of them)
    static const bool shadowed = getenv("CPS3RECOMP_NATIVE_GPU_SHADOW") != nullptr;
    if (shadowed) {
        static std::vector<uint8_t> shadow(kTexturesSize, 0);
        static uint64_t frames = 0;
        frames++;
        std::vector<bool> sent(cps3recomp::native::Textures::kPages, false);
        for (const int page : pages) {
            if (page >= cps3recomp::native::Textures::kPages) {
                continue;
            }
            sent[static_cast<size_t>(page)] = true;
            memcpy(shadow.data() + static_cast<size_t>(page) * kPage, v.textures.Pixels() + static_cast<size_t>(page) * kPage, kPage);
        }
        for (int page = 0; page < cps3recomp::native::Textures::kPages; page++) {
            if (!sent[static_cast<size_t>(page)] && memcmp(shadow.data() + static_cast<size_t>(page) * kPage, v.textures.Pixels() + static_cast<size_t>(page) * kPage, kPage)) {
                fprintf(stderr, "native GPU: page %d changed unmarked (draw %llu)\n", page, static_cast<unsigned long long>(frames));
                memcpy(shadow.data() + static_cast<size_t>(page) * kPage, v.textures.Pixels() + static_cast<size_t>(page) * kPage, kPage);
            }
        }
    }
    // (a guest's buffer may be wider than the game's, with a zoom of its own)
    const uint32_t columns = static_cast<uint32_t>(std::max(f.bufferWidth, guest ? f.guestBufferWidth : 0));
    const uint32_t tilesParams[8] = {columns, static_cast<uint32_t>(f.bufferHeight), static_cast<uint32_t>(f.tiles.size()), m_rowStartsData[kRows],
                                     guest ? static_cast<uint32_t>(f.guestBufferHeight) : 0u, 0, 0, 0};
    const uint32_t fx = (f.screen.zoomX << 16) / 0x40, fy = (f.screen.zoomY << 16) / 0x40;
    const uint32_t gfx = guest ? (f.guestScreen.zoomX << 16) / 0x40 : 0, gfy = guest ? (f.guestScreen.zoomY << 16) / 0x40 : 0;
    const int32_t resolveParams[12] = {f.width, f.height, static_cast<int32_t>(fx), static_cast<int32_t>(fy), f.margin, f.textScrollY,
                                       static_cast<int32_t>(f.textPalette), guest ? 1 : 0, static_cast<int32_t>(gfx), static_cast<int32_t>(gfy), 0, 0};
    // One upload of everything
    // (the overlay: sent when it changed)
    // (one column and one row past it are written too: NativeGpu::Overlay)
    const bool over = overlay && overlay->pixels && overlay->width > 0 && overlay->height > 0 && overlay->width < kOverlayWidth &&
                      (overlay->rawAbove ? 2 * overlay->height : overlay->height) < kOverlayHeight;
    const bool sendOverlay = over && (!m_overlayKnown || overlay->version != m_overlaySent);
    const uint32_t overlayBytes = sendOverlay ? static_cast<uint32_t>(overlay->width * overlay->height * 4) : 0;
    // (the picture as shown: the frame's, or as large as the overlay, with it over it; twice as tall with rawAbove)
    const int shownWidth = over ? overlay->width : f.width, shownHeight = over ? overlay->height : f.height;
    const bool rawAbove = over && overlay->rawAbove;
    const int composedHeight = rawAbove ? 2 * shownHeight : shownHeight;
    const uint32_t composeParams[8] = {static_cast<uint32_t>(shownWidth), static_cast<uint32_t>(shownHeight), static_cast<uint32_t>(f.width),
                                       static_cast<uint32_t>(f.height), rawAbove ? 1u : 0u, over ? 1u : 0u, 0, 0};
    const uint32_t total = 32 + overlayBytes + 32 + 48 + tileBytes + (kRows + 1) * 4 + rowTileBytes + static_cast<uint32_t>(pages.size()) * kPage +
                           (colours ? kPaletteSize : 0) + (guestColours ? kPaletteSize : 0) + (scales ? 48 * 4 : 0) + kTextSize;
    if (!m_upload || m_uploadCapacity < total) {
        if (m_upload) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_upload);
        }
        m_uploadCapacity = std::max(total, m_uploadCapacity * 2);
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = m_uploadCapacity;
        m_upload = SDL_CreateGPUTransferBuffer(m_device, &info);
    }
    uint8_t *map = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(m_device, m_upload, true));
    if (!map) {
        fprintf(stderr, "native GPU: the upload (%u bytes) cannot be mapped: %s\n", total, SDL_GetError());
        m_texturesSent = false; // (nothing went: all of it again next time)
        return cmd;
    }
    struct Copy {
        SDL_GPUBuffer *buffer;
        uint32_t at, size, offset;
    };
    std::vector<Copy> copies;
    uint32_t at = 0;
    const auto put = [&](SDL_GPUBuffer *buffer, const void *data, uint32_t size, uint32_t offset = 0) {
        memcpy(map + at, data, size);
        copies.push_back({buffer, at, size, offset});
        at += size;
    };
    put(m_composeParams, composeParams, 32);
    const uint32_t overlayAt = at;
    if (sendOverlay) {
        memcpy(map + at, overlay->pixels, overlayBytes);
        at += overlayBytes;
        m_overlaySent = overlay->version;
        m_overlayKnown = true;
    }
    put(m_tilesParams, tilesParams, 32);
    put(m_resolveParams, resolveParams, 48);
    if (!f.tiles.empty()) {
        put(m_tiles, f.tiles.data(), tileBytes);
    }
    put(m_rowStarts, m_rowStartsData.data(), (kRows + 1) * 4);
    put(m_rowTiles, m_rowTilesData.data(), rowTileBytes);
    for (const int page : pages) {
        const bool theGuests = page >= cps3recomp::native::Textures::kPages;
        const uint8_t *from = (theGuests ? guest->textures : v.textures).Pixels() +
                              static_cast<size_t>(page % cps3recomp::native::Textures::kPages) * kPage;
        put(m_textures, from, kPage, static_cast<uint32_t>(page) * kPage);
    }
    if (colours) {
        put(m_palette, m_rgb.data(), kPaletteSize);
    }
    if (guestColours) {
        put(m_palette, m_guestRgb.data(), kPaletteSize, kPaletteSize);
    }
    if (scales) {
        put(m_palette, m_scales, 48 * 4, 2 * kPaletteSize);
    }
    put(m_text, ss, kTextSize);
    SDL_UnmapGPUTransferBuffer(m_device, m_upload);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    for (const Copy &c : copies) {
        SDL_GPUTransferBufferLocation from = {m_upload, c.at};
        SDL_GPUBufferRegion to = {c.buffer, c.offset, c.size};
        SDL_UploadToGPUBuffer(copy, &from, &to, false);
    }
    if (sendOverlay) {
        SDL_GPUTextureTransferInfo from = {};
        from.transfer_buffer = m_upload;
        from.offset = overlayAt;
        from.pixels_per_row = static_cast<Uint32>(overlay->width);
        from.rows_per_layer = static_cast<Uint32>(overlay->height);
        SDL_GPUTextureRegion to = {};
        to.texture = m_overlay;
        to.w = static_cast<Uint32>(overlay->width);
        to.h = static_cast<Uint32>(overlay->height);
        to.d = 1;
        SDL_UploadToGPUTexture(copy, &from, &to, false);
    }
    SDL_EndGPUCopyPass(copy);
    if (Timing()) {
        cmd = TimePass(m_device, cmd, 0);
    }
    // (debugging aid, CPS3RECOMP_NATIVE_GPU_SPLIT=1: the uploads submitted and waited for before the passes that read them)
    static const bool split = getenv("CPS3RECOMP_NATIVE_GPU_SPLIT") != nullptr;
    if (split) {
        if (SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
            SDL_WaitForGPUFences(m_device, true, &fence, 1);
            SDL_ReleaseGPUFence(m_device, fence);
        }
        cmd = SDL_AcquireGPUCommandBuffer(m_device);
    }

    // The buffer of colour numbers
    {
        SDL_GPUStorageTextureReadWriteBinding out = {};
        out.texture = m_colours;
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &out, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, m_tilesPass);
        SDL_GPUBuffer *buffers[5] = {m_tilesParams, m_tiles, m_rowStarts, m_rowTiles, m_textures};
        SDL_BindGPUComputeStorageBuffers(pass, 0, buffers, 5);
        SDL_DispatchGPUCompute(pass, (columns + 7) / 8, (rows + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        if (Timing()) {
            cmd = TimePass(m_device, cmd, 1);
        }
    }
    // The picture
    {
        SDL_GPUStorageTextureReadWriteBinding out[2] = {};
        out[0].texture = m_colours;
        out[1].texture = m_picture;
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, out, 2, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, m_resolvePass);
        SDL_GPUBuffer *buffers[3] = {m_resolveParams, m_palette, m_text};
        SDL_BindGPUComputeStorageBuffers(pass, 0, buffers, 3);
        SDL_DispatchGPUCompute(pass, static_cast<Uint32>((f.width + 7) / 8), static_cast<Uint32>((f.height + 7) / 8), 1);
        SDL_EndGPUComputePass(pass);
        if (Timing()) {
            cmd = TimePass(m_device, cmd, 2);
        }
    }
    // The picture as the presenter samples it: with what is put over it (NativeGpu::Overlay), and a column and a row
    // past it the same as its edges
    {
        SDL_GPUStorageTextureReadWriteBinding out[3] = {};
        out[0].texture = m_picture;
        out[1].texture = m_overlay;
        out[2].texture = m_composed;
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, out, 3, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, m_composePass);
        SDL_BindGPUComputeStorageBuffers(pass, 0, &m_composeParams, 1);
        SDL_DispatchGPUCompute(pass, static_cast<Uint32>((shownWidth + 8) / 8), static_cast<Uint32>((composedHeight + 8) / 8), 1);
        SDL_EndGPUComputePass(pass);
        if (Timing()) {
            cmd = TimePass(m_device, cmd, 3);
        }
        m_lastShown = {m_composed, kOverlayWidth, kOverlayHeight, shownWidth, shownHeight};
    }
    return cmd;
}

bool NativeGpu::DrawAndRead(const cps3recomp::native::Frame &f, const cps3recomp::native::Video &v, const uint8_t *ss, std::vector<uint32_t> &pixels) {
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) {
        return false;
    }
    cmd = Draw(cmd, f, v, ss);
    // (the drawing finished before the picture is read back: in one command buffer, the copy pass's read of the picture
    // was seen to overtake the compute pass's writes to it now and then, on Metal; the frame then read back as the one
    // before)
    static const bool together = getenv("CPS3RECOMP_NATIVE_GPU_ONE_BUFFER") != nullptr; // (debugging aid: as it was)
    if (!together) {
        SDL_GPUFence *drawn = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!drawn) {
            return false;
        }
        SDL_WaitForGPUFences(m_device, true, &drawn, 1);
        SDL_ReleaseGPUFence(m_device, drawn);
        cmd = SDL_AcquireGPUCommandBuffer(m_device);
        if (!cmd) {
            return false;
        }
    }
    const uint32_t bytes = static_cast<uint32_t>(m_width * m_height * 4);
    if (!m_download) {
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = kBufferWidth * 256 * 4;
        m_download = SDL_CreateGPUTransferBuffer(m_device, &info);
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion from = {};
    from.texture = m_picture;
    from.w = static_cast<Uint32>(m_width);
    from.h = static_cast<Uint32>(m_height);
    from.d = 1;
    SDL_GPUTextureTransferInfo to = {};
    to.transfer_buffer = m_download;
    to.pixels_per_row = static_cast<Uint32>(m_width);
    to.rows_per_layer = static_cast<Uint32>(m_height);
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return false;
    }
    SDL_WaitForGPUFences(m_device, true, &fence, 1);
    SDL_ReleaseGPUFence(m_device, fence);
    const uint8_t *map = static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(m_device, m_download, false));
    if (!map) {
        return false;
    }
    pixels.resize(static_cast<size_t>(m_width) * m_height);
    memcpy(pixels.data(), map, bytes);
    SDL_UnmapGPUTransferBuffer(m_device, m_download);
    for (uint32_t &p : pixels) {
        p &= 0x00FFFFFF; // (the spare byte: 0 on the CPU's)
    }
    return true;
}

} // namespace port
