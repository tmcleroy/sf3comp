// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The model viewer (the Alternative modes page, as the CPS-2 ports have it): a character alone on a stage, or on plain
// white or black, drawn at a size chosen, moved with player 1's controls. It is a training session (fight/session.h)
// with its character written through the choice (kChosen), the stage chosen (kStage), the panel (the text layer) not
// drawn, player 2's fighter not drawn (its objects' palettes 0x10-0x17; player 1's are 0x08-0x0F: seen by leaving each
// range out), and the picture zoomed about the character's feet.
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace cps3recomp {

namespace {
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                       "Ken", "Sean", "Urien", "Akuma", "Akuma (red)", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
    constexpr int kCount = static_cast<int>(std::size(kCharacters));
    const double kSizes[] = {1, 1.5, 2, 3, 4, 6, 8, 0.5, 0.75};
    struct State {
        bool on = false;
    } v;

    int16_t Signed16(uint32_t at) {
        return static_cast<int16_t>(Ram16(at));
    }
    bool Plain() {
        return FeatureValue("model_background") >= kCount;
    }
    // Player 2's fighter (and, on plain white or black, the stage) not drawn
    bool Hidden(const machine::Machine &board, uint32_t object) {
        const uint32_t *list = board.SpriteList();
        const bool tilemap = (list[object * 4 + 2] & 3) == 0;
        const uint32_t palette = list[object * 4] & 0x1FF;
        if (Plain() && (tilemap || palette >= 0x80)) {
            return true;
        }
        return !tilemap && palette >= 0x10 && palette <= 0x17;
    }
    void Start(Machine &machine) {
        session::StartSession(machine);
        v.on = true;
    }
} // namespace

bool ModelViewerOn() {
    return v.on && session::InSession();
}

void ModelBeforeFrame(Machine &m) {
    if (v.on && !session::InSession()) {
        v.on = false;
    }
    const bool on = ModelViewerOn();
    m.hidePanel = m.hidePanel || on;
    m.hideObject = on ? Hidden : nullptr;
    const int background = FeatureValue("model_background");
    m.backdrop = on && Plain() ? (background == kCount ? 0x01FFFFFFu : 0x01000000u) : 0;
    if (!on) {
        return;
    }
    // (the character and the stage, written through the choice; player 2 whatever, it is not seen)
    const bool locked = Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    if (!locked) {
        SetRam8(sfiii3::kChosen[0], static_cast<uint8_t>(std::clamp(FeatureValue("model_character"), 0, kCount - 1)));
        SetRam8(sfiii3::kStage, static_cast<uint8_t>(std::min(background, kCount - 1)));
    }
    m.players[1] = 0;
}

// After the frame: the picture zoomed about the character's feet
void ModelAfterFrame(Machine &m) {
    if (!ModelViewerOn()) {
        return;
    }
    const double size = kSizes[std::clamp(FeatureValue("model_size"), 0, static_cast<int>(std::size(kSizes)) - 1)];
    if (size == 1) {
        return;
    }
    const int width = m.PictureWidth(), height = Video::kHeight;
    const uint32_t base = sfiii3::kPlayer[0];
    const int cx = Signed16(base + sfiii3::kX) - Signed16(sfiii3::kCameraX) + width / 2;
    const int feet = height - (Signed16(base + sfiii3::kY) - Signed16(sfiii3::kCameraY)) - 23;
    // (the character in the middle; up to 1.5 times, its feet kept a little above the bottom, larger its body's middle,
    // about 55 above its feet, at the picture's)
    const double anchorX = width / 2.0, anchorY = size <= 1.5 ? height - 16.0 : height / 2.0;
    const int point = size <= 1.5 ? feet : feet - 55;
    static std::vector<uint32_t> from;
    from.assign(m.Picture(), m.Picture() + static_cast<size_t>(width) * height);
    const uint32_t fill = m.backdrop ? m.backdrop & 0xFFFFFF : 0;
    uint32_t *to = m.Picture();
    for (int y = 0; y < height; y++) {
        const int sy = static_cast<int>(point + (y - anchorY) / size);
        for (int x = 0; x < width; x++) {
            const int sx = static_cast<int>(cx + (x - anchorX) / size);
            to[static_cast<size_t>(y) * width + x] = sx >= 0 && sx < width && sy >= 0 && sy < height ? from[static_cast<size_t>(sy) * width + sx] : fill;
        }
    }
}

void RegisterModel(Game &game) {
    static const char *const kPage = "Model viewer";
    game.commands.push_back({"model_start", kPage, "Start",
                             "Shows the character chosen below alone, moved with player 1's controls (every move works), on a stage or "
                             "plain white or black, at the size chosen. Reset ends it.",
                             Start});
    game.features.push_back({"model_character", kPage, "Character", "Who is shown (taken when the viewer starts).", kFeatureMenuChoices,
                             {std::begin(kCharacters), std::end(kCharacters)}});
    static std::vector<std::string> names;
    if (names.empty()) {
        for (const char *name : kCharacters) {
            names.push_back(std::string(name) + "'s stage");
        }
    }
    std::vector<const char *> backgrounds;
    for (const std::string &name : names) {
        backgrounds.push_back(name.c_str());
    }
    backgrounds.push_back("White");
    backgrounds.push_back("Black");
    game.features.push_back({"model_background", kPage, "Background",
                             "What is behind the character: one of the stages (taken when the viewer starts), or plain white or black.",
                             kFeatureMenuChoices, backgrounds});
    game.features.push_back({"model_size", kPage, "Size", "How large the character is drawn.", 0,
                             {"1x", "1.5x", "2x", "3x", "4x", "6x", "8x", "0.5x", "0.75x"}});
}

} // namespace cps3recomp
