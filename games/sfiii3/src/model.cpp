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
#include <cstdio>
#include <cstdlib>
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
        bool editing = false;        // the colour editor (the viewer with its panel)
        int row = 0, line = 0, colour = 1; // the panel's row; player 1's line (0-7: colour RAM line * 64) and colour (0-63)
        uint16_t held = 0;           // player 1's controls at the last frame (for presses)
        uint32_t heldFor = 0, frames = 0;
        uint32_t changedAt = 0;      // when the last change was made (written to the file a second after)
        bool unsaved = false;
        uint32_t blinkUntil = 0;     // the colour chosen blinks till then
        uint16_t blinkWas = 0;       // ... its own colour, put back
        bool blinking = false;
        uint32_t tick = 0;           // (slow motion: frames counted)
        // (the animation shown: the menu's choice it was made from, the animation, and when it last ended)
        int choice = 0;
        uint32_t target = 0, endedAt = 0;
        bool ended = false;
        uint16_t lastCell = 0;
    } v;
    // The animations: each character's record holds ten lists of them (+0x1AC on, a long each: the lists' addresses);
    // a list is longs, each an animation's address, as many as point near the list (found from 3rd Strike's records and
    // program: Alex's lists 0x061DE53C on). An animation is cells of 16 bytes: the first word's high byte the frames the
    // cell is shown for (below 0x100: a command instead; 0xFAFF holds it), the fourth the picture. The record's +0x200 is
    // the animation playing, +0x204 the cell (in fours of bytes), +0x214 the frames left of it (0x06089848 advances it).
    constexpr int kLists = 10, kMostInList = 160;
    constexpr uint32_t kAnimation = 0x200, kCell = 0x204, kCellLeft = 0x214;
    int ListCount(uint32_t list) {
        int n = 0;
        for (; n < kMostInList; n++) {
            const uint32_t at = Peek32(list + 4 * static_cast<uint32_t>(n));
            if (at < 0x06000000u || at >= 0x07000000u || at + 0x40000u < list || at > list + 0x40000u) {
                break;
            }
        }
        return n;
    }
    uint32_t AnimationOf(int choice) {
        if (choice <= 0) {
            return 0;
        }
        const int list = (choice - 1) / kMostInList, n = (choice - 1) % kMostInList;
        const uint32_t at = Ram32(sfiii3::kPlayer[0] + 0x1AC + 4 * static_cast<uint32_t>(list));
        if (list >= kLists || at < 0x06000000u || n >= ListCount(at)) {
            return 0;
        }
        return Peek32(at + 4 * static_cast<uint32_t>(n));
    }
    enum Row { kLine, kColour, kRed, kGreen, kBlue, kReset, kRows };

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
    // Player 1's fighter (its objects' palettes 0x08-0x0F), drawn over the stage's pieces in front of the fighters
    // ("Stage in front": Behind)
    bool Front(const machine::Machine &board, uint32_t object) {
        const uint32_t *list = board.SpriteList();
        const uint32_t palette = list[object * 4] & 0x1FF;
        return (list[object * 4 + 2] & 3) != 0 && palette >= 0x08 && palette <= 0x0F;
    }
    void Start(Machine &machine) {
        session::StartSession(machine);
        v = {};
        v.on = true;
    }
    void StartEditor(Machine &machine) {
        Start(machine);
        v.editing = true;
    }
} // namespace

// (colours.cpp)
std::string CharacterName(int number);
bool ColourWord(uint32_t index, uint32_t &word);
void SetCustomColour(Machine &m, const std::string &character, uint32_t index, uint16_t colour);
void SaveCustomColours(const std::string &character);
void ForgetCustomColours(const std::string &character);
void ForceCustomColours(bool on);

namespace {
    // (shown so only while the character is on its stage: the choice and the rest as they are)
    bool Showing() {
        const uint8_t state = Ram8(sfiii3::kMatchState);
        return v.on && session::InSession() && (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && state >= 1 &&
               state <= 8;
    }
    std::string Character() {
        return CharacterName(Ram16(sfiii3::kPlayer[0] + 0x3C0));
    }
    uint32_t Index() {
        return static_cast<uint32_t>(v.line * 64 + v.colour);
    }
    uint16_t ColourNow(Machine &m) {
        return v.blinking ? v.blinkWas : m.board.ColourRam()[Index()];
    }
    void StopBlink(Machine &m) {
        if (v.blinking) {
            m.board.WriteColour(Index(), v.blinkWas);
            m.board.WriteColour(Index() + 0x200, v.blinkWas);
            v.blinking = false;
        }
    }
    // The animation chosen played over and over (each time it ends, or holds, after the repeat's wait); the ones a
    // character has not got passed over in the direction the choice moved
    void Animate(Machine &m) {
        int choice = FeatureValue(v.editing ? "colours_animation" : "model_animation");
        const char *name = v.editing ? "colours_animation" : "model_animation";
        if (choice > 0 && !AnimationOf(choice)) {
            const int way = choice >= v.choice ? 1 : -1;
            int next = choice;
            for (int n = 0; n < kLists * kMostInList && next > 0 && !AnimationOf(next); n++) {
                next += way;
                if (next > kLists * kMostInList) {
                    next = 1;
                }
            }
            choice = AnimationOf(next) ? next : 0;
            SetFeatureValue(name, choice);
        }
        v.choice = choice;
        const uint32_t base = sfiii3::kPlayer[0];
        const uint32_t animation = AnimationOf(choice);
        if (!animation) {
            v.target = 0;
            return;
        }
        m.players[0] = 0; // (the animation plays; the controls are not the character's)
        const bool startNow = animation != v.target;
        // (over: the game has gone on to another, it holds (a cell of 0xFF frames), or it has come round to its start (one
        // that repeats by itself))
        const uint16_t cell = Ram16(base + kCell);
        const bool over = Ram32(base + kAnimation) != animation || Ram8(base + kCellLeft) >= 0xF0 || cell < v.lastCell;
        v.lastCell = cell;
        if (!startNow && over && !v.ended) {
            v.ended = true;
            v.endedAt = v.frames;
        }
        static const int kRepeat[] = {0, 60, 120, 180, 240, 300};
        const uint32_t wait = static_cast<uint32_t>(kRepeat[std::clamp(FeatureValue("model_repeat"), 0, 5)]);
        if (v.ended && v.frames - v.endedAt >= wait && FeatureOn("model_all") && !v.editing) {
            // (play all: on to the next the character has)
            int next = choice + 1;
            for (int n = 0; n < kLists * kMostInList && !AnimationOf(next); n++) {
                next = next >= kLists * kMostInList ? 1 : next + 1;
            }
            SetFeatureValue(name, next);
            v.choice = next;
            v.target = 0;
            v.ended = false;
            return;
        }
        if (getenv("SFIII3_MODEL_LOG") && (startNow || v.frames % 30 == 0)) { // (debugging aid)
            fprintf(stderr, "model: frame %u choice %d animation %08X (it plays %08X, cell %u, left %u)%s\n", v.frames, choice, animation,
                    Ram32(base + kAnimation), Ram16(base + kCell), Ram8(base + kCellLeft), startNow ? " new" : "");
        }
        if (startNow || (v.ended && v.frames - v.endedAt >= wait)) {
            SetRam32(base + kAnimation, animation);
            SetRam16(base + kCell, 0);
            SetRam8(base + kCellLeft, 1);
            v.target = animation;
            v.ended = false;
            v.lastCell = 0;
        }
    }

    // The panel worked with player 1's controls: up and down a row, left and right its value (held: again and again)
    void Editor(Machine &m) {
        const uint16_t now = m.players[0];
        const uint16_t pressed = static_cast<uint16_t>(now & ~v.held);
        v.heldFor = now && now == v.held ? v.heldFor + 1 : 0;
        const bool repeat = v.heldFor > 20 && v.heldFor % 3 == 0;
        const uint16_t act = repeat ? now : pressed;
        v.held = now;
        m.players[0] = 0; // (the character stands; the controls are the panel's)
        StopBlink(m);
        if (act & kUp) {
            v.row = (v.row + kRows - 1) % kRows;
        }
        if (act & kDown) {
            v.row = (v.row + 1) % kRows;
        }
        const int step = (act & kRight) ? 1 : (act & kLeft) ? -1 : 0;
        bool chosen = false;
        if (step) {
            uint16_t c = ColourNow(m);
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            switch (v.row) {
            case kLine: v.line = (v.line + 8 + step) % 8; chosen = true; break;
            case kColour: v.colour = (v.colour + 64 + step) % 64; chosen = true; break;
            case kRed: r = std::clamp(r + step, 0, 31); break;
            case kGreen: g = std::clamp(g + step, 0, 31); break;
            case kBlue: b = std::clamp(b + step, 0, 31); break;
            default: break;
            }
            if (v.row >= kRed && v.row <= kBlue) {
                c = static_cast<uint16_t>((c & 0x8000) | r | (g << 5) | (b << 10));
                SetCustomColour(m, Character(), Index(), c);
                v.changedAt = v.frames;
                v.unsaved = true;
            }
        }
        if (v.row == kReset && (pressed & (kPunch1 | kKick1))) {
            ForgetCustomColours(Character());
            v.unsaved = false;
            StartEditor(m); // (the game's colours again, from the start of a fight)
            return;
        }
        if (chosen) {
            v.blinkUntil = v.frames + 40;
        }
        // (the colour chosen blinks white and black for a moment, so that it is found on the character)
        if (v.frames < v.blinkUntil && (v.frames / 6) % 2 == 0) {
            v.blinkWas = m.board.ColourRam()[Index()];
            const uint16_t flash = ((v.frames / 12) % 2) ? 0x0000 : 0x7FFF;
            m.board.WriteColour(Index(), flash);
            m.board.WriteColour(Index() + 0x200, flash);
            v.blinking = true;
        }
        if (v.unsaved && v.frames - v.changedAt > 60) {
            SaveCustomColours(Character());
            v.unsaved = false;
        }
    }
} // namespace

bool ModelViewerOn() {
    return v.on && session::InSession();
}

void ModelBeforeFrame(Machine &m) {
    if (v.on && !session::InSession()) {
        v.on = false;
    }
    v.frames++;
    ForceCustomColours(v.on && v.editing);
    if (!v.on) {
        SetSlowMotion(1);
    }
    const bool on = Showing();
    m.hidePanel = m.hidePanel || on;
    m.hideObject = on ? Hidden : nullptr;
    m.frontObject = on && !v.editing && !Plain() && FeatureValue("model_foreground") == 1 ? Front : nullptr;
    const int background = FeatureValue("model_background");
    m.backdrop = on && Plain() ? (background == kCount ? 0x01FFFFFFu : 0x01000000u) : 0;
    if (!ModelViewerOn()) {
        return;
    }
    // (the character and the stage, written through the choice; player 2 whatever, it is not seen)
    const bool locked = Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    if (!locked) {
        SetRam8(sfiii3::kChosen[0], static_cast<uint8_t>(std::clamp(FeatureValue("model_character"), 0, kCount - 1)));
        SetRam8(sfiii3::kStage, static_cast<uint8_t>(std::min(background, kCount - 1)));
    }
    m.players[1] = 0;
    if (on && v.editing) {
        Editor(m);
    }
    if (on) {
        Animate(m);
    }
    // (slow motion: the port runs the game slower while the character is shown, as its own speed does)
    const int speed = FeatureValue("model_speed");
    SetSlowMotion(on && speed > 0 ? 1 << speed : 1);
}

// The editor's panel, at the right of the picture; the animation's name at the bottom
void ModelHud(std::vector<HudLine> &lines, std::vector<HudBox> &boxes) {
    if (Showing() && v.choice > 0) {
        char name[16];
        snprintf(name, sizeof(name), "%c%d", 'A' + (v.choice - 1) / kMostInList, (v.choice - 1) % kMostInList);
        lines.push_back({0.5f, 0.94f, 0, name, 255, 255, 255});
    }
    if (!Showing() || !v.editing) {
        return;
    }
    Machine *m = TheMachine();
    if (!m) {
        return;
    }
    const uint16_t c = ColourNow(*m);
    uint32_t word = 0;
    const bool known = ColourWord(Index(), word);
    char text[6][48];
    snprintf(text[0], sizeof(text[0]), "LINE %d", v.line + 1);
    snprintf(text[1], sizeof(text[1]), "COLOUR %d%s", v.colour, known ? "" : " (NOT THE CHARACTER'S)");
    snprintf(text[2], sizeof(text[2]), "RED %d", c & 31);
    snprintf(text[3], sizeof(text[3]), "GREEN %d", (c >> 5) & 31);
    snprintf(text[4], sizeof(text[4]), "BLUE %d", (c >> 10) & 31);
    snprintf(text[5], sizeof(text[5]), "THE GAME'S COLOURS AGAIN");
    std::string title = "COLOUR EDITOR: " + Character();
    for (char &ch : title) {
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    }
    lines.push_back({0.97f, 0.06f, 1, title, 255, 230, 120});
    for (int r = 0; r < kRows; r++) {
        const bool here = r == v.row;
        lines.push_back({0.97f, 0.13f + 0.055f * r, 1, std::string(here ? "> " : "") + text[r], here ? uint8_t(255) : uint8_t(200),
                         here ? uint8_t(255) : uint8_t(200), here ? uint8_t(120) : uint8_t(200)});
    }
    // (the colour itself, and the line's 64)
    const int width = m->PictureWidth();
    const auto rgb = [](uint16_t x, uint8_t &r, uint8_t &g, uint8_t &b) {
        r = static_cast<uint8_t>((x & 31) << 3);
        g = static_cast<uint8_t>(((x >> 5) & 31) << 3);
        b = static_cast<uint8_t>(((x >> 10) & 31) << 3);
    };
    uint8_t r, g, b;
    rgb(c, r, g, b);
    boxes.push_back({width - 46, 152, 40, 20, r, g, b, 255});
    for (int n = 0; n < 64; n++) {
        const uint16_t x = n == v.colour ? c : m->board.ColourRam()[v.line * 64 + n];
        rgb(x, r, g, b);
        boxes.push_back({width - 134 + (n % 16) * 8, 178 + (n / 16) * 8, n == v.colour ? 8 : 7, n == v.colour ? 8 : 7, r, g, b, 255});
    }
}

// After the frame: the picture zoomed about the character's feet
void ModelAfterFrame(Machine &m) {
    if (!Showing() || v.editing) {
        return;
    }
    const double size = kSizes[std::clamp(FeatureValue("model_size"), 0, static_cast<int>(std::size(kSizes)) - 1)];
    if (size == 1 && !Plain()) {
        return; // (on a stage, at its own size: as the game shows it; on plain white or black, moved to the middle)
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
    game.features.push_back({"model_foreground", kPage, "Stage in front",
                             "What the stage has in front of the fighters: in front of the character too, as in a fight, or behind it so "
                             "that all of it is seen. Not in the colour editor.",
                             0, {"In front", "Behind"}});
    game.features.push_back({"model_size", kPage, "Size", "How large the character is drawn (not in the colour editor).", 0,
                             {"1x", "1.5x", "2x", "3x", "4x", "6x", "8x", "0.5x", "0.75x"}});
    static std::vector<std::string> animations;
    if (animations.empty()) {
        animations.push_back("Standing (you control it)");
        for (int list = 0; list < kLists; list++) {
            for (int n = 0; n < kMostInList; n++) {
                animations.push_back(std::string(1, static_cast<char>('A' + list)) + std::to_string(n));
            }
        }
    }
    std::vector<const char *> choices;
    for (const std::string &one : animations) {
        choices.push_back(one.c_str());
    }
    game.features.push_back({"model_animation", kPage, "Animation",
                             "What the character does: stands as in a fight, moved with player 1's controls, or goes through one of its "
                             "animations over and over. They are named by the character's ten lists of them and a number (B12); the "
                             "ones a character has not got are passed over. Its name is shown under it.",
                             kFeatureMenuChoices, choices});
    game.features.push_back({"model_repeat", kPage, "Repeat after", "How long an animation's end is held before it starts again.", 0,
                             {"At once", "1 second", "2 seconds", "3 seconds", "4 seconds", "5 seconds"}});
    game.features.push_back({"model_all", kPage, "Play all",
                             "Goes through all of the character's animations, one after another: each shown once (and its end held for "
                             "\"Repeat after\"), then the next."});
    game.features.push_back({"colours_animation", "Colour editor", "Animation",
                             "What the character does in the editor: stands, or goes through one of its animations (as in the model "
                             "viewer).",
                             kFeatureMenuChoices, choices});
    game.features.push_back({"model_speed", kPage, "Speed", "How fast the character moves: slow motion, to look at a move frame by frame.",
                             0, {"Full", "1/2", "1/4", "1/8"}});
    game.commands.push_back({"colours_start", "Colour editor", "Start",
                             "The model viewer with a panel: player 1's controls choose a line of the character's colours, a colour of "
                             "it (it blinks on the character), and its red, green and blue. Changes are kept for the character (the "
                             "palettes folder) and used in every fight with the Display page's Custom colours on.",
                             StartEditor});
}

} // namespace cps3recomp
