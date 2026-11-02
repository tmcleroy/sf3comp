#version 450
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
// What is drawn over the whole window (the touch controls: port/touch.h), its alpha premultiplied, blended over the picture
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D overlay;
void main() {
    colour = texture(overlay, uv);
}
