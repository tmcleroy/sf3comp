#version 450
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
// The picture fitted to the viewport with its pixels kept crisp: each pixel is a sharp block as wide as a
// whole number of the screen's pixels would make it, and only the edges between blocks are blended (the
// picture's pixels are not square, so it never fits a whole number of times across). Of the picture, the part
// `view` says (all of it unless zoomed: the port's camera, port/zoom.h).
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D picture;
layout(set = 3, binding = 0) uniform Sizes {
    vec2 size;  // the picture, in its own pixels
    vec2 scale; // screen pixels to one of the picture's, across and down
    vec4 view;  // the part of the picture shown: its corner, then its size, as parts of the whole
    vec2 shown; // the picture's own pixels, from the corner (past them: black)
};
void main() {
    vec2 at = (view.xy + uv * view.zw) * size;
    vec2 pixel = floor(at);
    vec2 within = at - pixel - 0.5;
    vec2 flat_part = 0.5 - 0.5 / max(scale, vec2(1.0));
    vec2 blend = (within - clamp(within, -flat_part, flat_part)) * max(scale, vec2(1.0));
    bool inside = all(greaterThanEqual(at, vec2(0.0))) && all(lessThan(at, shown));
    colour = inside ? vec4(texture(picture, (pixel + 0.5 + blend) / size).rgb, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
