// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// Putting the picture on the screen. With SDL's GPU API (Metal, Vulkan, Direct3D 12) where there is a
// device for it; with SDL's renderer otherwise, or when asked (--renderer sdl); through a shader preset, with
// OpenGL, when one is chosen (shading.h).
#pragma once

#include "native_gpu.h"
#include "shading.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace port {

// How the picture is fitted to the window: the Display page's "Scaling" and "Picture shape" (the port's
// display_scaling and display_shape features, in the order of their choices)
enum class Scaling {
    Sharp,       // as large as fits; each pixel a sharp block, only the edges between blocks blended
    WholePixels, // as tall as a whole number of the picture's rows fits (a border round it); across, the shape
    Smooth,      // as large as fits, blended all over
};
enum class Shape {
    Arcade,       // 4:3 for the board's 384 columns, as its monitor showed them (the pixels narrower than tall)
    SquarePixels, // each pixel as wide as it is tall (384 x 224 is 12:7)
    Fill,         // the whole window
};
// Where in a `width` x `height` window (in its pixels) a picture `columns` across and `rows` down goes
SDL_FRect FitPicture(int width, int height, int columns, int rows, Shape shape, Scaling scaling);
// The picture's shape (width over height) for a window of the given shape
float PictureShape(int columns, int rows, Shape shape, float window);

// Of the picture, the part shown larger (the port's camera, zoom.h). The picture given is then twice as tall: the
// game's own above, and below it the same with the readout over it. `view`, in the pixels of the one above, fills
// the rectangle the whole picture would; and `keep`, rectangles of the one below (in the pixels of its own size,
// not counting the one above) are shown where they are and at their size all the same: the readout's panels,
// which are not part of the game's picture.
struct Zoomed {
    SDL_FRect view;
    std::vector<SDL_Rect> keep;
};

class Presenter {
public:
    ~Presenter();
    // False with the reason in `error` if the window cannot be drawn to either way
    bool Open(SDL_Window *window, bool gpu, std::string &error);
    // Through the shader preset `preset` (a .slangp), in an OpenGL window (shading.h); false with the reason, and
    // the window is then not drawn to (it is an OpenGL window, which Open does not draw to either)
    bool OpenShaded(SDL_Window *window, const std::string &preset, std::string &error);
    void Close();
    bool Shaded() const {
        return m_shading != nullptr;
    }
    Shading *Shader() {
        return m_shading.get();
    }
    // Another preset, while shaded; false with the reason, and the one before is kept
    bool SetPreset(const std::string &preset, std::string &error) {
        return m_shading && m_shading->Load(preset, error);
    }
    bool UsesGpu() const {
        return m_device != nullptr;
    }
    SDL_GPUDevice *Device() const {
        return m_device;
    }
    SDL_Renderer *Renderer() const {
        return m_renderer;
    }
    // The window's size in pixels
    void Size(int &width, int &height) const;
    // Shows a picture (`width` x `height` pixels, each red, green, blue and a spare byte in memory) in the
    // rectangle `where` of the window, the rest black (zoomed: part of it larger, as `zoomed` says). Shaded, through
    // the preset unless `shade` is false (the menu, drawn larger than the game's picture: only fitted to `where`)
    void Show(const uint32_t *pixels, int width, int height, const SDL_FRect &where, const Zoomed *zoomed = nullptr, bool shade = true);
    // The native video's frame (video/raster.h, made ready by Machine: GpuFrame) drawn by the GPU (native_gpu.h) and
    // shown as Show shows a picture, in one command buffer: the compute passes that draw the picture, then the render
    // pass that fits it to the window, submitted where Show submits, with no wait on the CPU. False (nothing done) where
    // it cannot be: not through SDL's GPU API, or shaded, or the GPU's renderer could not be made; Show then
    bool ShowNative(const cps3recomp::native::Frame &frame, const cps3recomp::native::Video &video, const uint8_t *ssRam, const SDL_FRect &where);
    bool CanShowNative() const {
        return m_device && !m_shading && m_native.IsOpen();
    }
    // Vsync (the Display page's VSync, issue #185): each picture shown when the screen next refreshes, so never
    // torn, and none left out for want of a free one of the window's. Off unless set (frames are paced by the port,
    // and shown as soon as made). The same after the presenter is opened again.
    void SetVsync(bool on);
    bool Vsync() const {
        return m_vsync;
    }
    // How the picture's pixels are drawn (FitPicture says where): Sharp unless set
    void SetScaling(Scaling scaling) {
        m_scaling = scaling;
    }
    // What is drawn over the whole window with each picture after this (the touch controls: touch.h): `width` x
    // `height` pixels, red in the low byte, each colour already multiplied by its alpha; stretched to the window.
    // None (nullptr) draws nothing over it. Not through a shader preset.
    void SetOverlay(const uint32_t *pixels, int width, int height);

    // For tests: the GPU path with no window. The picture is drawn into `outWidth` x `outHeight` at `where`
    // as Show would draw it, and read back. False (with the reason) where there is no GPU device.
    static bool Render(const uint32_t *pixels, int width, int height, int outWidth, int outHeight, const SDL_FRect &where,
                       Scaling scaling, std::vector<uint32_t> &out, std::string &error, const Zoomed *zoomed = nullptr);
    // For the checks and the bench: no window, the pictures drawn into a texture `width` x `height` of the presenter's
    // own (Show and ShowNative as for a window); `nearest`: sampled at the nearest pixel (at 1:1, each pixel as it is).
    // Each picture's command buffer is submitted with a fence: WaitShown waits for it (and says how long that took)
    bool OpenOffscreen(int width, int height, bool nearest, std::string &error);
    double WaitShown();
    // ... and what the last was read back (0x00BBGGRR), in a command buffer of its own after WaitShown
    bool ReadOffscreen(std::vector<uint32_t> &pixels);

private:
    bool OpenGpu(SDL_Window *window, std::string &error);
    bool MakePipeline(SDL_GPUTextureFormat target, std::string &error);
    bool Upload(SDL_GPUCommandBuffer *commands, const uint32_t *pixels, int width, int height);
    // `picture` (`textureWidth` x `textureHeight`), its top left `width` x `height` the picture (zoomed: two of them,
    // one above the other) into `target`
    void Draw(SDL_GPUCommandBuffer *commands, SDL_GPUTexture *target, const SDL_FRect &where, const Zoomed *zoomed, Uint32 targetWidth,
              Uint32 targetHeight, SDL_GPUTexture *picture, float textureWidth, float textureHeight, float width, float height);
    // The window's texture to draw into for this command buffer (or the offscreen one); false: none this time
    bool Screen(SDL_GPUCommandBuffer *commands, SDL_GPUTexture *&screen, Uint32 &width, Uint32 &height);
    void Submit(SDL_GPUCommandBuffer *commands);
    // The native video's renderer, on this device
    NativeGpu m_native;
    // (offscreen: OpenOffscreen)
    SDL_GPUTexture *m_offscreen = nullptr;
    SDL_GPUTransferBuffer *m_offscreenRead = nullptr;
    int m_offscreenWidth = 0, m_offscreenHeight = 0;
    SDL_GPUFence *m_shownFence = nullptr;
    SDL_GPUSampler *m_nearest = nullptr;
    bool m_useNearest = false, m_wantNative = true;

    SDL_Window *m_window = nullptr;
    Scaling m_scaling = Scaling::Sharp;
    bool m_vsync = false;
    void ApplyVsync();
    // GPU
    SDL_GPUDevice *m_device = nullptr;
    SDL_GPUGraphicsPipeline *m_pipeline = nullptr;
    SDL_GPUSampler *m_sampler = nullptr;
    SDL_GPUTexture *m_picture = nullptr;
    SDL_GPUTransferBuffer *m_transfer = nullptr;
    int m_width = 0, m_height = 0;
    // The overlay (SetOverlay): its pixels, waiting to be sent to the GPU texture or the renderer's when changed
    std::vector<uint32_t> m_overlay;
    int m_overlayWidth = 0, m_overlayHeight = 0;
    bool m_overlayChanged = false;
    SDL_GPUGraphicsPipeline *m_overlayPipeline = nullptr;
    SDL_GPUTexture *m_overlayTexture = nullptr;
    SDL_GPUTransferBuffer *m_overlayTransfer = nullptr;
    int m_overlayTextureWidth = 0, m_overlayTextureHeight = 0;
    SDL_Texture *m_overlayRendered = nullptr;
    bool UploadOverlay(SDL_GPUCommandBuffer *commands);
    // SDL's renderer
    SDL_Renderer *m_renderer = nullptr;
    SDL_Texture *m_texture = nullptr, *m_enlarged = nullptr;
    int m_enlargedBy = 0;
    // A shader preset's
    std::unique_ptr<Shading> m_shading;
    std::vector<uint32_t> m_composed;
};

} // namespace port
