// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.

#include "present.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace port {

namespace {
#include "shaders.inc"

    SDL_GPUShader *MakeShader(SDL_GPUDevice *device, SDL_GPUShaderStage stage, const unsigned char *spirv, size_t spirvSize,
                              const char *msl, int samplers, int uniforms) {
        SDL_GPUShaderCreateInfo info = {};
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
        info.stage = stage;
        info.num_samplers = static_cast<Uint32>(samplers);
        info.num_uniform_buffers = static_cast<Uint32>(uniforms);
        return SDL_CreateGPUShader(device, &info);
    }

    struct Sizes {
        float size[2], scale[2], view[4], shown[2], spare[2];
    };

    // Zoomed (present.h, Zoomed: `pixels` two pictures, one above the other), what the camera shows, at the
    // picture's size: of the one above, the part `view` blended up to the whole (past its edges, black), and the
    // rectangles `keep` of the one below over it where they are
    void Compose(const uint32_t *pixels, int width, int height, const Zoomed &zoomed, std::vector<uint32_t> &out) {
        out.assign(static_cast<size_t>(width) * height, 0);
        auto at = [&](int x, int y) -> uint32_t {
            return x < 0 || y < 0 || x >= width || y >= height ? 0 : pixels[static_cast<size_t>(y) * width + x];
        };
        auto mix = [](uint32_t a, uint32_t b, float by) {
            uint32_t mixed = 0;
            for (int shift = 0; shift < 24; shift += 8) {
                const float from = static_cast<float>((a >> shift) & 0xFF), to = static_cast<float>((b >> shift) & 0xFF);
                mixed |= static_cast<uint32_t>(std::lround(from + (to - from) * by)) << shift;
            }
            return mixed;
        };
        const SDL_FRect &view = zoomed.view;
        for (int y = 0; y < height; y++) {
            const float down = view.y + (y + 0.5f) * view.h / height - 0.5f;
            const int row = static_cast<int>(std::floor(down));
            const float below = down - row;
            for (int x = 0; x < width; x++) {
                const float across = view.x + (x + 0.5f) * view.w / width - 0.5f;
                const int column = static_cast<int>(std::floor(across));
                const float right = across - column;
                out[static_cast<size_t>(y) * width + x] = mix(mix(at(column, row), at(column + 1, row), right),
                                                              mix(at(column, row + 1), at(column + 1, row + 1), right), below);
            }
        }
        for (const SDL_Rect &keep : zoomed.keep) {
            for (int y = std::max(keep.y, 0); y < std::min(keep.y + keep.h, height); y++) {
                for (int x = std::max(keep.x, 0); x < std::min(keep.x + keep.w, width); x++) {
                    out[static_cast<size_t>(y) * width + x] = pixels[static_cast<size_t>(y + height) * width + x];
                }
            }
        }
    }
} // namespace

float PictureShape(int columns, int rows, Shape shape, float window) {
    switch (shape) {
    case Shape::SquarePixels: return static_cast<float>(columns) / rows;
    case Shape::Fill: return window;
    case Shape::Arcade: break;
    }
    // (4:3 for the board's 384 columns, and as much wider as a wider picture has more of them: 16:9 for 512)
    return 4.0f / 3.0f * columns / 384.0f;
}

SDL_FRect FitPicture(int width, int height, int columns, int rows, Shape shape, Scaling scaling) {
    const float ratio = PictureShape(columns, rows, shape, height > 0 ? static_cast<float>(width) / height : 1.0f);
    SDL_FRect where;
    if (scaling == Scaling::WholePixels) {
        // As many of the window's rows to one of the picture's as fits with the picture's width too (a
        // picture as wide as the window across, if it fills it); with none that fits, as large as fits
        for (int by = height / rows; by >= 1; by--) {
            where.h = static_cast<float>(by * rows);
            where.w = shape == Shape::Fill ? static_cast<float>(width) : std::round(where.h * ratio);
            if (where.w <= width) {
                where.x = std::floor((width - where.w) / 2);
                where.y = std::floor((height - where.h) / 2);
                return where;
            }
        }
    }
    where.w = std::min<float>(static_cast<float>(width), height * ratio);
    where.h = shape == Shape::Fill ? static_cast<float>(height) : where.w / ratio;
    where.x = (width - where.w) / 2;
    where.y = (height - where.h) / 2;
    return where;
}

Presenter::~Presenter() {
    Close();
}

bool Presenter::MakePipeline(SDL_GPUTextureFormat target, std::string &error) {
    SDL_GPUShader *vertex = MakeShader(m_device, SDL_GPU_SHADERSTAGE_VERTEX, k_present_vert_spirv, sizeof(k_present_vert_spirv),
                                       k_present_vert_msl, 0, 0);
    SDL_GPUShader *fragment = MakeShader(m_device, SDL_GPU_SHADERSTAGE_FRAGMENT, k_present_frag_spirv, sizeof(k_present_frag_spirv),
                                         k_present_frag_msl, 1, 1);
    // (and the overlay's: its texture over the whole window, blended, its alpha premultiplied)
    SDL_GPUShader *overlay = MakeShader(m_device, SDL_GPU_SHADERSTAGE_FRAGMENT, k_overlay_frag_spirv, sizeof(k_overlay_frag_spirv),
                                        k_overlay_frag_msl, 1, 0);
    if (vertex && fragment && overlay) {
        SDL_GPUColorTargetDescription colour = {};
        colour.format = target;
        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = vertex;
        info.fragment_shader = fragment;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.target_info.num_color_targets = 1;
        info.target_info.color_target_descriptions = &colour;
        m_pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &info);
        info.fragment_shader = overlay;
        colour.blend_state.enable_blend = true;
        colour.blend_state.src_color_blendfactor = colour.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        colour.blend_state.dst_color_blendfactor = colour.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colour.blend_state.color_blend_op = colour.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        m_overlayPipeline = SDL_CreateGPUGraphicsPipeline(m_device, &info);
    }
    for (SDL_GPUShader *shader : {vertex, fragment, overlay}) {
        if (shader) {
            SDL_ReleaseGPUShader(m_device, shader);
        }
    }
    SDL_GPUSamplerCreateInfo sampler = {};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_sampler = SDL_CreateGPUSampler(m_device, &sampler);
    if (!m_pipeline || !m_overlayPipeline || !m_sampler) {
        error = SDL_GetError();
        return false;
    }
    return true;
}

bool Presenter::OpenGpu(SDL_Window *window, std::string &error) {
    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!m_device) {
        error = SDL_GetError();
        return false;
    }
    SDL_GPUTextureFormat target = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (window) {
        if (!SDL_ClaimWindowForGPUDevice(m_device, window)) {
            error = SDL_GetError();
            SDL_DestroyGPUDevice(m_device);
            m_device = nullptr;
            return false;
        }
        ApplyVsync();
        target = SDL_GetGPUSwapchainTextureFormat(m_device, window);
    }
    if (!MakePipeline(target, error)) {
        Close();
        return false;
    }
    return true;
}

void Presenter::SetVsync(bool on) {
    if (on != m_vsync) {
        m_vsync = on;
        ApplyVsync();
    }
}

void Presenter::ApplyVsync() {
    if (m_shading) {
        m_shading->SetVsync(m_vsync);
    } else if (m_device && m_window) {
        // Off: frames are paced by the port, to the board's rate, and the picture is shown as soon as it is made,
        // where the window lets it be (no waiting for the screen, which would hold the game's controls back too).
        // On: at the screen's refresh (VSYNC, which every window has)
        if (m_vsync) {
            SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
        } else {
            for (const SDL_GPUPresentMode mode : {SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX}) {
                if (SDL_WindowSupportsGPUPresentMode(m_device, m_window, mode)) {
                    SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
                    break;
                }
            }
        }
    } else if (m_renderer) {
        SDL_SetRenderVSync(m_renderer, m_vsync ? 1 : 0);
    }
}

bool Presenter::OpenShaded(SDL_Window *window, const std::string &preset, std::string &error) {
    m_window = window;
    m_shading = std::make_unique<Shading>();
    if (!m_shading->Open(window, error) || !m_shading->Load(preset, error)) {
        m_shading.reset();
        return false;
    }
    m_overlayChanged = !m_overlay.empty(); // (the overlay, to the new window's OpenGL)
    ApplyVsync();
    return true;
}

bool Presenter::Open(SDL_Window *window, bool gpu, std::string &error) {
    m_window = window;
    if (gpu && OpenGpu(window, error)) {
        return true;
    }
    m_renderer = SDL_CreateRenderer(window, nullptr);
    if (!m_renderer) {
        error = SDL_GetError();
        return false;
    }
    ApplyVsync(); // (frames are paced by the port, to the board's rate, unless vsync is on)
    return true;
}

void Presenter::Close() {
    m_shading.reset();
    if (m_device) {
        if (m_transfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_transfer);
        }
        if (m_picture) {
            SDL_ReleaseGPUTexture(m_device, m_picture);
        }
        if (m_sampler) {
            SDL_ReleaseGPUSampler(m_device, m_sampler);
        }
        if (m_pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
        }
        if (m_overlayPipeline) {
            SDL_ReleaseGPUGraphicsPipeline(m_device, m_overlayPipeline);
        }
        if (m_overlayTexture) {
            SDL_ReleaseGPUTexture(m_device, m_overlayTexture);
        }
        if (m_overlayTransfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_overlayTransfer);
        }
        if (m_window) {
            SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        }
        SDL_DestroyGPUDevice(m_device);
    }
    m_device = nullptr;
    m_pipeline = m_overlayPipeline = nullptr;
    m_overlayTexture = nullptr;
    m_overlayTransfer = nullptr;
    m_overlayTextureWidth = m_overlayTextureHeight = 0;
    m_overlayChanged = !m_overlay.empty(); // (sent again to whatever draws next)
    m_sampler = nullptr;
    m_picture = nullptr;
    m_transfer = nullptr;
    for (SDL_Texture *texture : {m_texture, m_enlarged, m_overlayRendered}) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    m_texture = m_enlarged = m_overlayRendered = nullptr;
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    m_renderer = nullptr;
    m_width = m_height = m_enlargedBy = 0;
}

void Presenter::Size(int &width, int &height) const {
    if (m_shading) {
        m_shading->Size(width, height);
    } else if (m_renderer) {
        SDL_GetRenderOutputSize(m_renderer, &width, &height);
    } else {
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
    }
}

bool Presenter::Upload(SDL_GPUCommandBuffer *commands, const uint32_t *pixels, int width, int height) {
    if (width != m_width || height != m_height) {
        if (m_picture) {
            SDL_ReleaseGPUTexture(m_device, m_picture);
        }
        if (m_transfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_transfer);
        }
        SDL_GPUTextureCreateInfo texture = {};
        texture.type = SDL_GPU_TEXTURETYPE_2D;
        texture.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture.width = static_cast<Uint32>(width);
        texture.height = static_cast<Uint32>(height);
        texture.layer_count_or_depth = 1;
        texture.num_levels = 1;
        m_picture = SDL_CreateGPUTexture(m_device, &texture);
        SDL_GPUTransferBufferCreateInfo transfer = {};
        transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer.size = static_cast<Uint32>(width * height * 4);
        m_transfer = SDL_CreateGPUTransferBuffer(m_device, &transfer);
        m_width = width;
        m_height = height;
    }
    if (!m_picture || !m_transfer) {
        return false;
    }
    void *to = SDL_MapGPUTransferBuffer(m_device, m_transfer, true);
    if (!to) {
        return false;
    }
    memcpy(to, pixels, static_cast<size_t>(width) * height * 4);
    SDL_UnmapGPUTransferBuffer(m_device, m_transfer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo from = {};
    from.transfer_buffer = m_transfer;
    from.pixels_per_row = static_cast<Uint32>(width);
    from.rows_per_layer = static_cast<Uint32>(height);
    SDL_GPUTextureRegion region = {};
    region.texture = m_picture;
    region.w = static_cast<Uint32>(width);
    region.h = static_cast<Uint32>(height);
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &from, &region, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

void Presenter::SetOverlay(const uint32_t *pixels, int width, int height) {
    if (!pixels) {
        m_overlayChanged = m_overlayChanged || !m_overlay.empty();
        m_overlay.clear();
        return;
    }
    m_overlay.assign(pixels, pixels + static_cast<size_t>(width) * height);
    m_overlayWidth = width;
    m_overlayHeight = height;
    m_overlayChanged = true;
}

bool Presenter::UploadOverlay(SDL_GPUCommandBuffer *commands) {
    if (m_overlay.empty()) {
        return false;
    }
    if (!m_overlayChanged) {
        return m_overlayTexture != nullptr;
    }
    m_overlayChanged = false;
    if (m_overlayWidth != m_overlayTextureWidth || m_overlayHeight != m_overlayTextureHeight) {
        if (m_overlayTexture) {
            SDL_ReleaseGPUTexture(m_device, m_overlayTexture);
        }
        if (m_overlayTransfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_overlayTransfer);
        }
        SDL_GPUTextureCreateInfo texture = {};
        texture.type = SDL_GPU_TEXTURETYPE_2D;
        texture.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture.width = static_cast<Uint32>(m_overlayWidth);
        texture.height = static_cast<Uint32>(m_overlayHeight);
        texture.layer_count_or_depth = 1;
        texture.num_levels = 1;
        m_overlayTexture = SDL_CreateGPUTexture(m_device, &texture);
        SDL_GPUTransferBufferCreateInfo transfer = {};
        transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer.size = static_cast<Uint32>(m_overlayWidth * m_overlayHeight * 4);
        m_overlayTransfer = SDL_CreateGPUTransferBuffer(m_device, &transfer);
        m_overlayTextureWidth = m_overlayWidth;
        m_overlayTextureHeight = m_overlayHeight;
    }
    if (!m_overlayTexture || !m_overlayTransfer) {
        return false;
    }
    void *to = SDL_MapGPUTransferBuffer(m_device, m_overlayTransfer, true);
    if (!to) {
        return false;
    }
    memcpy(to, m_overlay.data(), m_overlay.size() * 4);
    SDL_UnmapGPUTransferBuffer(m_device, m_overlayTransfer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo from = {};
    from.transfer_buffer = m_overlayTransfer;
    from.pixels_per_row = static_cast<Uint32>(m_overlayWidth);
    from.rows_per_layer = static_cast<Uint32>(m_overlayHeight);
    SDL_GPUTextureRegion region = {};
    region.texture = m_overlayTexture;
    region.w = static_cast<Uint32>(m_overlayWidth);
    region.h = static_cast<Uint32>(m_overlayHeight);
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &from, &region, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

void Presenter::Draw(SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, const SDL_FRect &where, const Zoomed *zoomed, Uint32 targetWidth,
                     Uint32 targetHeight) {
    const bool overlay = UploadOverlay(commands);
    SDL_GPUColorTargetInfo colour = {};
    colour.texture = target;
    colour.clear_color = {0, 0, 0, 1};
    colour.load_op = SDL_GPU_LOADOP_CLEAR;
    colour.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    const SDL_GPUViewport viewport = {where.x, where.y, where.w, where.h, 0, 1};
    SDL_SetGPUViewport(pass, &viewport);
    const SDL_GPUTextureSamplerBinding binding = {m_picture, m_sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    // (zoomed, the texture is two pictures, one above the other: present.h, Zoomed)
    const float width = static_cast<float>(m_width), height = static_cast<float>(zoomed ? m_height / 2 : m_height);
    const float tall = static_cast<float>(m_height);
    // The part `view` of the texture (its pixels) over the whole of `where`, drawn only inside `clip` (the window's pixels)
    auto part = [&](const SDL_FRect &view, const SDL_Rect &clip) {
        SDL_SetGPUScissor(pass, &clip);
        // (blended all over: as if each of the picture's pixels took one of the screen's, which the shader blends
        // from edge to edge)
        const bool smooth = m_scaling == Scaling::Smooth;
        const Sizes sizes = {{width, tall},
                             {smooth ? 1.0f : std::floor(where.w / view.w + 0.999f), smooth ? 1.0f : std::floor(where.h / view.h + 0.999f)},
                             {view.x / width, view.y / tall, view.w / width, view.h / tall},
                             {width, view.y >= height ? tall : height}, {0, 0}};
        SDL_PushGPUFragmentUniformData(commands, 0, &sizes, sizeof(sizes));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    };
    // (a rectangle of the picture's pixels, where it is in the window)
    auto inWindow = [&](float x, float y, float w, float h) {
        const int left = static_cast<int>(std::lround(where.x + x * where.w / width));
        const int top = static_cast<int>(std::lround(where.y + y * where.h / height));
        return SDL_Rect{left, top, static_cast<int>(std::lround(where.x + (x + w) * where.w / width)) - left,
                        static_cast<int>(std::lround(where.y + (y + h) * where.h / height)) - top};
    };
    part(zoomed ? zoomed->view : SDL_FRect{0, 0, width, height}, inWindow(0, 0, width, height));
    if (zoomed) {
        for (const SDL_Rect &keep : zoomed->keep) {
            part({0, height, width, height}, inWindow(static_cast<float>(keep.x), static_cast<float>(keep.y), static_cast<float>(keep.w), static_cast<float>(keep.h)));
        }
    }
    if (overlay && targetWidth && targetHeight) {
        const SDL_Rect all = {0, 0, static_cast<int>(targetWidth), static_cast<int>(targetHeight)};
        SDL_SetGPUScissor(pass, &all);
        const SDL_GPUViewport whole = {0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, 1};
        SDL_SetGPUViewport(pass, &whole);
        SDL_BindGPUGraphicsPipeline(pass, m_overlayPipeline);
        const SDL_GPUTextureSamplerBinding over = {m_overlayTexture, m_sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &over, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
}

void Presenter::Show(const uint32_t *pixels, int width, int height, const SDL_FRect &where, const Zoomed *zoomed, bool shade) {
    if (m_shading) {
        // Zoomed, the preset draws the whole picture and the camera closes in on what it drew, scanlines and all,
        // as on a monitor (shading.h); the menu, not through the preset, is given what the camera sees
        if (zoomed && !shade) {
            Compose(pixels, width, height, *zoomed, m_composed);
            pixels = m_composed.data();
        }
        if (m_overlayChanged) {
            m_overlayChanged = false;
            m_shading->SetOverlay(m_overlay.empty() ? nullptr : m_overlay.data(), m_overlayWidth, m_overlayHeight);
        }
        m_shading->Show(pixels, width, height, where, shade, shade ? zoomed : nullptr);
        return;
    }
    if (m_device) {
        SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(m_device);
        if (!commands) {
            return;
        }
        SDL_GPUTexture *screen = nullptr;
        Uint32 screenWidth = 0, screenHeight = 0;
        // (without vsync, not the call that waits for a free picture of the window's: a frame that cannot be shown
        // is left out; with it, the wait is the screen's refresh, and every frame is shown)
        if ((m_vsync ? SDL_WaitAndAcquireGPUSwapchainTexture(commands, m_window, &screen, &screenWidth, &screenHeight)
                     : SDL_AcquireGPUSwapchainTexture(commands, m_window, &screen, &screenWidth, &screenHeight)) &&
            screen &&
            Upload(commands, pixels, width, zoomed ? 2 * height : height)) {
            Draw(commands, screen, where, zoomed, screenWidth, screenHeight);
        }
        SDL_SubmitGPUCommandBuffer(commands);
        return;
    }
    // (zoomed, the texture is two pictures, one above the other: present.h, Zoomed)
    const int tall = zoomed ? 2 * height : height;
    if (!m_texture || width != m_width || tall != m_height) {
        if (m_texture) {
            SDL_DestroyTexture(m_texture);
        }
        m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, width, tall);
        SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
        m_width = width;
        m_height = tall;
        m_enlargedBy = 0;
    }
    SDL_UpdateTexture(m_texture, nullptr, pixels, width * static_cast<int>(sizeof(uint32_t)));
    // Zoomed: the part shown, then each rectangle kept as it is, from a texture `by` times the picture's size
    auto draw = [&](SDL_Texture *texture, float by) {
        const SDL_FRect whole = {0, 0, static_cast<float>(width), static_cast<float>(height)};
        const SDL_FRect &view = zoomed ? zoomed->view : whole;
        // (of the view, only what is on the picture: past its edges is black)
        const float left = std::max(view.x, 0.0f), top = std::max(view.y, 0.0f);
        const float right = std::min(view.x + view.w, whole.w), bottom = std::min(view.y + view.h, whole.h);
        if (right > left && bottom > top) {
            const SDL_FRect from = {left * by, top * by, (right - left) * by, (bottom - top) * by};
            const float across = where.w / view.w, down = where.h / view.h;
            const SDL_FRect to = {where.x + (left - view.x) * across, where.y + (top - view.y) * down, (right - left) * across, (bottom - top) * down};
            SDL_RenderTexture(m_renderer, texture, &from, &to);
        }
        if (zoomed) {
            const float across = where.w / width, down = where.h / height;
            for (const SDL_Rect &keep : zoomed->keep) {
                const SDL_FRect part = {keep.x * by, (keep.y + height) * by, keep.w * by, keep.h * by};
                const SDL_FRect to = {where.x + keep.x * across, where.y + keep.y * down, keep.w * across, keep.h * down};
                SDL_RenderTexture(m_renderer, texture, &part, &to);
            }
        }
    };
    // (the overlay over all of it: SetOverlay)
    auto overlay = [&] {
        if (m_overlay.empty()) {
            return;
        }
        if (m_overlayChanged || !m_overlayRendered) {
            m_overlayChanged = false;
            float w = 0, h = 0;
            if (!m_overlayRendered || !SDL_GetTextureSize(m_overlayRendered, &w, &h) || w != m_overlayWidth || h != m_overlayHeight) {
                if (m_overlayRendered) {
                    SDL_DestroyTexture(m_overlayRendered);
                }
                m_overlayRendered = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, m_overlayWidth, m_overlayHeight);
                if (!m_overlayRendered) {
                    return;
                }
                SDL_SetTextureBlendMode(m_overlayRendered, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
                SDL_SetTextureScaleMode(m_overlayRendered, SDL_SCALEMODE_LINEAR);
            }
            SDL_UpdateTexture(m_overlayRendered, nullptr, m_overlay.data(), m_overlayWidth * static_cast<int>(sizeof(uint32_t)));
        }
        SDL_RenderTexture(m_renderer, m_overlayRendered, nullptr, nullptr);
    };
    if (m_scaling == Scaling::Smooth) {
        // (blended all over: the picture as it is, fitted smoothly)
        SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_LINEAR);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
        SDL_RenderClear(m_renderer);
        draw(m_texture, 1);
        overlay();
        SDL_RenderPresent(m_renderer);
        return;
    }
    SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
    // The picture's pixels are not square, so it never fits the window a whole number of times across. To keep
    // the pixels crisp and even it is first enlarged a whole number of times (each pixel a sharp block), and
    // that is then fitted to the window smoothly.
    const int by = std::clamp(static_cast<int>(where.h / (zoomed ? zoomed->view.h : static_cast<float>(height)) + 0.999f), 1, 8);
    if (by != m_enlargedBy) {
        if (m_enlarged) {
            SDL_DestroyTexture(m_enlarged);
        }
        m_enlarged = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_TARGET, width * by, tall * by);
        if (m_enlarged) {
            SDL_SetTextureScaleMode(m_enlarged, SDL_SCALEMODE_LINEAR);
        }
        m_enlargedBy = by;
    }
    if (m_enlarged) {
        SDL_SetRenderTarget(m_renderer, m_enlarged);
        SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr);
        SDL_SetRenderTarget(m_renderer, nullptr);
    }
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
    draw(m_enlarged ? m_enlarged : m_texture, m_enlarged ? static_cast<float>(by) : 1.0f);
    overlay();
    SDL_RenderPresent(m_renderer);
}

bool Presenter::Render(const uint32_t *pixels, int width, int height, int outWidth, int outHeight, const SDL_FRect &where,
                       Scaling scaling, std::vector<uint32_t> &out, std::string &error, const Zoomed *zoomed) {
    Presenter p;
    p.m_scaling = scaling;
    if (!p.OpenGpu(nullptr, error)) {
        return false;
    }
    SDL_GPUDevice *device = p.m_device;
    SDL_GPUTextureCreateInfo texture = {};
    texture.type = SDL_GPU_TEXTURETYPE_2D;
    texture.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture.width = static_cast<Uint32>(outWidth);
    texture.height = static_cast<Uint32>(outHeight);
    texture.layer_count_or_depth = 1;
    texture.num_levels = 1;
    SDL_GPUTexture *target = SDL_CreateGPUTexture(device, &texture);
    SDL_GPUTransferBufferCreateInfo transfer = {};
    transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer.size = static_cast<Uint32>(outWidth * outHeight * 4);
    SDL_GPUTransferBuffer *download = SDL_CreateGPUTransferBuffer(device, &transfer);
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
    bool ok = target && download && commands && p.Upload(commands, pixels, width, zoomed ? 2 * height : height);
    if (ok) {
        p.Draw(commands, target, where, zoomed, static_cast<Uint32>(outWidth), static_cast<Uint32>(outHeight));
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureRegion region = {};
        region.texture = target;
        region.w = static_cast<Uint32>(outWidth);
        region.h = static_cast<Uint32>(outHeight);
        region.d = 1;
        SDL_GPUTextureTransferInfo to = {};
        to.transfer_buffer = download;
        to.pixels_per_row = static_cast<Uint32>(outWidth);
        to.rows_per_layer = static_cast<Uint32>(outHeight);
        SDL_DownloadFromGPUTexture(copy, &region, &to);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        ok = fence && SDL_WaitForGPUFences(device, true, &fence, 1);
        if (fence) {
            SDL_ReleaseGPUFence(device, fence);
        }
        if (ok) {
            const void *from = SDL_MapGPUTransferBuffer(device, download, false);
            ok = from != nullptr;
            if (ok) {
                out.resize(static_cast<size_t>(outWidth) * outHeight);
                memcpy(out.data(), from, out.size() * 4);
                SDL_UnmapGPUTransferBuffer(device, download);
            }
        }
    } else if (commands) {
        SDL_SubmitGPUCommandBuffer(commands);
    }
    if (!ok) {
        error = SDL_GetError();
    }
    if (download) {
        SDL_ReleaseGPUTransferBuffer(device, download);
    }
    if (target) {
        SDL_ReleaseGPUTexture(device, target);
    }
    return ok;
}

} // namespace port
