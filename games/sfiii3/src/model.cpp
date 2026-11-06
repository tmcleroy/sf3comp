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

#include "../../../cps3recomp/recomp-common/text.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <random>
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
        bool fast = false;           // on its way to the round, at speed
        bool arrived = false;        // ... and there
        int named = -1;              // the animation whose name is shown, and since when
        uint32_t namedAt = 0;
        uint32_t cellSince = 0;      // when the animation's cell last changed (one held 3 seconds is its end)
        int character = 0, background = 0; // (the menu's when it started: another chosen while viewing goes to it)
        uint32_t session = 0;        // the session it runs in
        int16_t holdX = 0;           // (an animation shown: where the character is held, so that it plays in place)
        uint64_t zoomed = ~0ull;     // the machine's frame whose picture was last zoomed (zoomed once only)
        // (the colour editor: its row; the set (restarting the viewer with it chosen), player 1's line (0-7: colour RAM
        // line * 64) and colour (1-63))
        int row = 6, set = 0, line = 0, colour = 1; // (row 6: the Colour row)
        uint16_t before = 0, held = 0; // player 1's controls the frame before, and the direction held and for how long
        int heldFor = 0;
        uint32_t frames = 0;
        int unsaved = -1;            // frames since the last change not yet written (-1: none)
        int blink = 0;               // frames the colour chosen still blinks for
        uint16_t blinkWas = 0;       // ... its own colour, put back
        bool blinking = false;
        int copied = -1;             // a colour copied with LP (-1: none)
        // The character's saved sets, the one being tried on the Load row (0: none), the colours from before it
        std::vector<std::string> saved;
        int loading = 0;
        bool trying = false;
        std::map<uint32_t, uint16_t> kept;
        int deleteFor = 0, randomFor = 0;
        bool canUndo = false;        // (the colours from before the last random ones, for HK)
        std::map<uint32_t, uint16_t> undo;
        std::string said;            // a message, and frames it is still shown for
        int saidFor = 0;
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
    constexpr int kBeside = 120;           // how far right of the character the viewer keeps player 2 (not seen)
    constexpr uint32_t kNamedFor = 120;    // frames an animation's name is shown for after it changes (the CPS-2 ports' viewer's)
    constexpr uint32_t kLongestFrame = 180; // frames a frame is held before it counts as the animation's end
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
    // The colour editor's panel (as cps2recomp's fight/colour_editor.cpp has it)
    enum Row { kRowCharacter, kRowBackground, kRowSize, kRowAnimation, kRowSet, kRowLine, kRowColour, kRowRed, kRowGreen, kRowBlue,
               kRowRandom, kRowSave, kRowLoad, kRows };
    constexpr const char *kRowNames[kRows] = {"Character", "Background", "Size", "Animation", "Set", "Line", "Colour",
                                              "Red", "Green", "Blue", "Random colours", "Save set", "Load"};
    // 3rd Strike's seven sets of a character's colours, by the button that chooses them (character select's colour byte,
    // 0-6: LP to HK seen; the seventh the game's own other)
    constexpr const char *kSetNames[] = {"LP", "MP", "HP", "LK", "MK", "HK", "Extra"};
    constexpr int kSets = 7, kLines = 8, kInLine = 64; // (player 1's colours: eight lines of 64, colour 0 of each not seen)
    constexpr int kHeldFirst = 20, kHeldEvery = 8, kHeldEveryLevel = 3; // frames a direction is held before it repeats
    constexpr int kBlinkFor = 48, kBlinkEvery = 6; // frames the colour chosen blinks for, and how fast
    constexpr int kSaveAfter = 45;                 // frames after the last change that the file is written
    constexpr int kSaidFor = 120;                  // frames a message is shown for
    constexpr int kPanelWidth = 140, kShift = 72;  // the panel's width, and how far left the character goes

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
        if (!tilemap && palette >= 0x10 && palette <= 0x17) {
            return true;
        }
        // (player 2's shadow: the blended objects of palette 0 under its fighter, between its objects' ends)
        if (!tilemap && palette == 0 && ((list[object * 4] >> 10) & 1)) {
            int left = 0x7FFF, right = -0x7FFF;
            for (uint32_t n = 0; n < std::min<uint32_t>(board.SpriteCount(), 0x200); n++) {
                const uint32_t p = list[n * 4] & 0x1FF;
                if ((list[n * 4 + 2] & 3) != 0 && p >= 0x10 && p <= 0x17) {
                    const int x = static_cast<int>((list[n * 4 + 1] >> 16) & 0x3FF);
                    left = std::min(left, x);
                    right = std::max(right, x + static_cast<int>(((list[n * 4 + 2] >> 16) & 0x7F) + 1));
                }
            }
            const int x = static_cast<int>((list[object * 4 + 1] >> 16) & 0x3FF);
            return left <= right && x >= left - 48 && x <= right + 16;
        }
        return false;
    }
    // Nothing drawn: the way to the round (character select, the versus screen, the entrances and the round's banners,
    // run at speed) is not shown, the picture black until the character is on its stage, framed (Veiled)
    bool HideAll(const machine::Machine &, uint32_t) {
        return true;
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
        v.session = session::Starts(); // (another session started after it, training's: the viewer is over)
        v.character = FeatureValue("model_character");
        v.background = FeatureValue("model_background");
    }
    void StartEditor(Machine &machine) {
        Start(machine);
        v.editing = true;
    }
    // (the editor started again, as it is: another set or character)
    void RestartEditor(Machine &machine) {
        const int set = v.set, row = v.row, line = v.line, colour = v.colour, copied = v.copied;
        StartEditor(machine);
        v.set = set;
        v.row = row;
        v.line = line;
        v.colour = colour;
        v.copied = copied;
    }
} // namespace

// (colours.cpp)
std::string CharacterName(int number);
bool ColourWord(uint32_t index, uint32_t &word);
void SetCustomColour(Machine &m, const std::string &character, uint32_t index, uint16_t colour);
void SaveCustomColours(const std::string &character);
void ForgetCustomColours(const std::string &character);
void ForceCustomColours(bool on);
bool GameColourAt(uint32_t index, uint16_t &colour);
std::vector<std::string> SavedSets(const std::string &character);
bool SaveSet(const std::string &character, const std::map<uint32_t, uint16_t> &colours, std::string &saved);
bool ReadSavedSet(const std::string &character, const std::string &saved, std::map<uint32_t, uint16_t> &colours);
bool RemoveSavedSet(const std::string &character, const std::string &saved);

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
        // (in place, as the CPS-2 ports' viewer shows them: a dash or a jump does not take the character, or the camera, away)
        if (startNow && v.target == 0 && v.holdX == 0) {
            v.holdX = static_cast<int16_t>(Ram16(base + sfiii3::kX));
        }
        SetRam16(base + sfiii3::kX, static_cast<uint16_t>(v.holdX));
        SetRam16(base + sfiii3::kY, 0);
        // (over: the game has gone on to another, it holds (a cell of 0xFF frames), or it has come round to its start (one
        // that repeats by itself))
        const uint16_t cell = Ram16(base + kCell);
        if (cell != v.lastCell || startNow) {
            v.cellSince = v.frames;
        }
        // (a frame held longer than kLongestFrame is the animation's end too: a knock-out's last, say, which lies there)
        const bool over = Ram32(base + kAnimation) != animation || Ram8(base + kCellLeft) >= 0xF0 || cell < v.lastCell ||
                          v.frames - v.cellSince > kLongestFrame;
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

    // --- the colour editor (cps2recomp's fight/colour_editor.cpp, on 3rd Strike's colours) ---

    std::string Fit(std::string text, size_t most) {
        return text.size() <= most ? text : text.substr(0, most - 2) + "..";
    }
    constexpr size_t kValueMost = 15, kLineMost = 21; // characters: beside a row's name, and a whole line
    void Say(std::string text) {
        v.said = Fit(std::move(text), kLineMost);
        v.saidFor = kSaidFor;
    }
    std::string ShortName(const std::string &saved, size_t most) {
        const std::string prefix = Character() + " ";
        const bool own = saved.size() > prefix.size() && saved.compare(0, prefix.size(), prefix) == 0;
        return Fit(own ? saved.substr(prefix.size()) : saved, most);
    }
    // Player 1's colours that are the character's (their graphics word known): its set as it is now
    bool Known(uint32_t index) {
        uint32_t word;
        return ColourWord(index, word);
    }
    uint16_t ColourAt(Machine &m, uint32_t index) {
        return v.blinking && index == Index() ? v.blinkWas : static_cast<uint16_t>(m.board.ColourRam()[index] & 0x7FFF);
    }
    uint16_t GameColour(Machine &m, uint32_t index) {
        uint16_t colour;
        return GameColourAt(index, colour) ? colour : ColourAt(m, index);
    }
    void Put(Machine &m, uint32_t index, uint16_t colour) {
        if (Known(index)) {
            SetCustomColour(m, Character(), index, static_cast<uint16_t>(colour & 0x7FFF));
        }
    }
    void Change(Machine &m, uint16_t colour) {
        if (colour == ColourAt(m, Index())) {
            return;
        }
        StopBlink(m);
        Put(m, Index(), colour);
        v.unsaved = 0;
        v.canUndo = false;
    }
    void Save() {
        if (v.unsaved < 0) {
            return;
        }
        v.unsaved = -1;
        SaveCustomColours(Character());
        Say("Saved " + Character());
    }
    // The set's colours now (every one that is the character's), and put back
    std::map<uint32_t, uint16_t> SetNow(Machine &m) {
        std::map<uint32_t, uint16_t> colours;
        for (uint32_t index = 0; index < kLines * kInLine; index++) {
            if (index % kInLine && Known(index)) {
                colours[index] = ColourAt(m, index);
            }
        }
        return colours;
    }
    void PutSet(Machine &m, const std::map<uint32_t, uint16_t> &colours) {
        StopBlink(m);
        for (const auto &[index, colour] : colours) {
            Put(m, index, colour);
        }
    }
    void PutBack(Machine &m) {
        if (v.trying) {
            PutSet(m, v.kept);
            v.trying = false;
        }
        v.loading = 0;
    }
    void Try(Machine &m, int choice) {
        PutBack(m);
        v.loading = choice;
        if (!choice) {
            return;
        }
        v.kept = SetNow(m);
        std::map<uint32_t, uint16_t> colours;
        if (!ReadSavedSet(Character(), v.saved[static_cast<size_t>(choice - 1)], colours)) {
            Say("Cannot read it");
            return;
        }
        v.trying = true;
        PutSet(m, colours);
    }
    void Load() {
        if (!v.trying) {
            return;
        }
        v.trying = false; // (kept)
        v.unsaved = 0;
        Say("Loaded " + ShortName(v.saved[static_cast<size_t>(v.loading - 1)], kLineMost - 7));
    }
    void Delete(Machine &m) {
        if (!v.loading) {
            return;
        }
        if (v.deleteFor <= 0) {
            v.deleteFor = 90;
            Say("MK again: delete");
            return;
        }
        const std::string name = v.saved[static_cast<size_t>(v.loading - 1)];
        PutBack(m);
        v.deleteFor = 0;
        Say(RemoveSavedSet(Character(), name) ? "Deleted " + ShortName(name, kLineMost - 8) : std::string("Not deleted"));
        v.saved = SavedSets(Character());
    }
    // Every colour of the set that is seen at random (any of the board's 32 levels of each of red, green and blue)
    void Randomise(Machine &m) {
        PutBack(m);
        static std::mt19937 random(std::random_device{}());
        std::uniform_int_distribution<int> level(0, 0x7FFF);
        v.undo = SetNow(m);
        std::map<uint32_t, uint16_t> colours = v.undo;
        for (auto &[index, colour] : colours) {
            colour = static_cast<uint16_t>(level(random));
        }
        PutSet(m, colours);
        v.unsaved = 0;
        v.canUndo = true;
        Say("Random: HK undoes it");
    }
    void AskRandom(Machine &m) {
        if (v.randomFor > 0) {
            v.randomFor = 0;
            Randomise(m);
        } else {
            v.randomFor = 90;
            Say("Again: random colours");
        }
    }
    void Undo(Machine &m) {
        if (!v.canUndo) {
            return;
        }
        PutSet(m, v.undo);
        v.canUndo = false;
        v.unsaved = 0;
        Say("Random colours undone");
    }
    void ResetSet(Machine &m) {
        if (!v.editing || !Showing()) {
            return;
        }
        PutBack(m);
        std::map<uint32_t, uint16_t> colours = SetNow(m);
        for (auto &[index, colour] : colours) {
            colour = GameColour(m, index);
        }
        PutSet(m, colours);
        v.unsaved = 0;
    }
    void RandomCommand(Machine &m) {
        if (v.editing && Showing()) {
            Randomise(m);
        }
    }
    void ResetCharacter(Machine &m) {
        if (!v.editing || !Showing()) {
            return;
        }
        PutBack(m);
        ForgetCustomColours(Character()); // (every set's: the file removed)
        v.unsaved = -1;
        RestartEditor(m); // (the game's colours again, from the start of a fight)
    }
    int StepChoice(const char *name, int count, int by) {
        const int value = (std::clamp(FeatureValue(name), 0, count - 1) + by + count) % count;
        SetFeatureValue(name, value);
        return value;
    }
    int AnimationStep(int choice, int by) {
        int next = choice;
        for (int n = 0; n < kLists * kMostInList + 1; n++) {
            next += by;
            if (next > kLists * kMostInList) {
                next = 0;
            } else if (next < 0) {
                next = kLists * kMostInList;
            }
            if (next == 0 || AnimationOf(next)) {
                return next;
            }
        }
        return 0;
    }
    // Left or right on a row
    void Step(Machine &m, int by) {
        switch (v.row) {
        case kRowCharacter:
            Save();
            StepChoice("model_character", kCount, by); // (the viewer goes to the new one)
            break;
        case kRowBackground: StepChoice("model_background", kCount + 2, by); break;
        case kRowSize: StepChoice("model_size", static_cast<int>(std::size(kSizes)), by); break;
        case kRowAnimation: SetFeatureValue("colours_animation", AnimationStep(FeatureValue("colours_animation"), by)); break;
        case kRowSet:
            Save();
            v.set = (v.set + by + kSets) % kSets;
            RestartEditor(m); // (the character chosen again with that set)
            break;
        case kRowLine:
            StopBlink(m);
            v.line = (v.line + by + kLines) % kLines;
            v.blink = kBlinkFor;
            break;
        case kRowColour:
            StopBlink(m);
            v.colour = (v.colour - 1 + by + (kInLine - 1)) % (kInLine - 1) + 1;
            v.blink = kBlinkFor;
            break;
        case kRowRandom: AskRandom(m); break;
        case kRowSave: {
            std::string name;
            Say(SaveSet(Character(), SetNow(m), name) ? "Saved as " + ShortName(name, kLineMost - 9) : std::string("Not saved: no folder"));
            break;
        }
        case kRowLoad:
            if (v.saved.empty()) {
                Say("None saved yet");
            } else {
                Try(m, (v.loading + by + static_cast<int>(v.saved.size()) + 1) % (static_cast<int>(v.saved.size()) + 1));
            }
            break;
        default: {
            const int shift = 5 * (v.row - kRowRed);
            const uint16_t c = ColourAt(m, Index());
            const int level = std::clamp(((c >> shift) & 31) + by, 0, 31);
            Change(m, static_cast<uint16_t>((c & ~(31 << shift)) | (level << shift)));
            break;
        }
        }
    }

    // The panel worked with player 1's controls: up and down a row, left and right its value (held: again and again),
    // the buttons as cps2recomp's editor has them (LP copy, MP paste, HP the game's colour, HK undo random; on the Load
    // row LP loads and MK deletes)
    void Editor(Machine &m) {
        const uint16_t now = m.players[0];
        m.players[0] = 0; // (the character stands; the controls are the panel's)
        const uint16_t pressed = static_cast<uint16_t>(now & ~v.before);
        v.before = now;
        const uint16_t direction = now & (kUp | kDown | kLeft | kRight);
        v.heldFor = direction && direction == v.held ? v.heldFor + 1 : 0;
        v.held = direction;
        const int every = v.row >= kRowRed && v.row <= kRowBlue ? kHeldEveryLevel : kHeldEvery;
        const bool acts = v.heldFor == 0 || (v.heldFor >= kHeldFirst && (v.heldFor - kHeldFirst) % every == 0);
        if (direction && acts) {
            if (direction & (kUp | kDown)) {
                if (v.row == kRowLoad) {
                    if (v.trying) {
                        Say("Put back");
                    }
                    PutBack(m);
                }
                v.row = (v.row + (direction & kUp ? kRows - 1 : 1)) % kRows;
                if (v.row == kRowLoad) {
                    v.saved = SavedSets(Character());
                    v.loading = 0;
                }
            } else {
                Step(m, direction & kRight ? 1 : -1);
                if (!v.on || !v.arrived) {
                    return; // (started again)
                }
            }
        }
        v.deleteFor = std::max(v.deleteFor - 1, 0);
        v.randomFor = v.row == kRowRandom ? std::max(v.randomFor - 1, 0) : 0;
        if (v.row == kRowLoad) {
            if (pressed & kPunch1) {
                Load();
            } else if (pressed & kKick2) {
                Delete(m);
            }
        } else if (pressed & kPunch1) {
            v.copied = ColourAt(m, Index());
            Say("Copied");
        } else if ((pressed & kPunch2) && v.copied >= 0) {
            Change(m, static_cast<uint16_t>(v.copied));
        } else if (pressed & kPunch3) {
            Change(m, GameColour(m, Index()));
        } else if ((pressed & kKick1) && v.row == kRowRandom) {
            AskRandom(m);
        } else if (pressed & kKick3) {
            Undo(m);
        }
        if (v.unsaved >= 0 && ++v.unsaved >= kSaveAfter) {
            Save();
        }
        v.saidFor = std::max(v.saidFor - 1, 0);
        // (the colour chosen blinks white and black for a moment, so that it is found on the character)
        StopBlink(m);
        if (v.blink > 0) {
            v.blink--;
            if ((v.blink / kBlinkEvery) % 2 == 1) {
                v.blinkWas = static_cast<uint16_t>(m.board.ColourRam()[Index()]);
                const uint16_t flash = ((v.blink / (2 * kBlinkEvery)) % 2) ? 0x0000 : 0x7FFF;
                m.board.WriteColour(Index(), flash);
                m.board.WriteColour(Index() + 0x200, flash);
                v.blinking = true;
            }
        }
    }

    // The panel over the right of the picture (the board's colours: 15 bits, red the lowest five)
    recomp::text::Color Shown(uint16_t c) {
        return {static_cast<uint8_t>((c & 31) * 255 / 31), static_cast<uint8_t>(((c >> 5) & 31) * 255 / 31),
                static_cast<uint8_t>(((c >> 10) & 31) * 255 / 31), 255};
    }
    void DrawPanel(Machine &m) {
        using recomp::text::Color;
        const int width = m.PictureWidth(), height = Video::kHeight;
        const recomp::text::Canvas canvas{m.Picture(), width, height};
        const Color white{255, 255, 255, 255}, dim{150, 150, 170, 255}, black{0, 0, 0, 255};
        const int x = width - kPanelWidth - 4, right = width - 10;
        int y = 6;
        canvas.Fill(static_cast<float>(x), 4, kPanelWidth, static_cast<float>(height - 8), {18, 20, 30, 235});
        auto text = [&](int at, int row, const char *str, Color c, int align) {
            const float w = recomp::text::TextWidth(str, 1);
            canvas.Text(align < 0 ? static_cast<float>(at) : static_cast<float>(at) - w, static_cast<float>(row), 1, 0, c, str);
        };
        text(x + 6, y + 2, "COLOUR EDITOR", white, -1);
        y += 13;
        const uint16_t colour = ColourAt(m, Index());
        for (int row = 0; row < kRows; row++) {
            const bool chosen = row == v.row;
            if (chosen) {
                canvas.Fill(static_cast<float>(x + 2), static_cast<float>(y - 1), kPanelWidth - 4, 10, {64, 70, 110, 255});
            }
            text(x + 6, y, kRowNames[row], chosen ? white : dim, -1);
            std::string value;
            switch (row) {
            case kRowCharacter: value = Character(); break;
            case kRowBackground: {
                const int background = FeatureValue("model_background");
                value = background >= kCount ? (background == kCount ? "White" : "Black") : Fit(kCharacters[background] + std::string("'s"), kValueMost);
                break;
            }
            case kRowSize: {
                char size[16];
                snprintf(size, sizeof(size), "%gx", kSizes[std::clamp(FeatureValue("model_size"), 0, static_cast<int>(std::size(kSizes)) - 1)]);
                value = size;
                break;
            }
            case kRowAnimation: {
                const int choice = FeatureValue("colours_animation");
                char name[16];
                snprintf(name, sizeof(name), "%c%d", 'A' + (choice - 1) / kMostInList, (choice - 1) % kMostInList);
                value = choice <= 0 ? "Standing" : name;
                break;
            }
            case kRowSet: value = kSetNames[v.set]; break;
            case kRowLine: value = std::to_string(v.line + 1); break;
            case kRowColour: value = std::to_string(v.colour) + (Known(Index()) ? "" : " (not its)"); break;
            case kRowRandom:
            case kRowSave: value = chosen ? "< >" : ""; break;
            case kRowLoad:
                value = v.loading ? ShortName(v.saved[static_cast<size_t>(v.loading - 1)], kValueMost) : !chosen ? "" : v.saved.empty() ? "none saved" : "< >";
                break;
            default: {
                // a bar of 32 levels, and the level
                const int part = row - kRowRed, level = (colour >> (5 * part)) & 31;
                const Color fill = part == 0 ? Color{230, 60, 60, 255} : part == 1 ? Color{60, 200, 80, 255} : Color{70, 110, 240, 255};
                const int barX = x + 52;
                canvas.Fill(static_cast<float>(barX), static_cast<float>(y + 1), 64, 6, {50, 52, 64, 255});
                canvas.Fill(static_cast<float>(barX), static_cast<float>(y + 1), static_cast<float>(2 * (level + 1)), 6, fill);
                value = std::to_string(level);
                break;
            }
            }
            text(right, y, value.c_str(), chosen ? white : dim, 1);
            y += 10;
        }
        // The line's colours (two rows of 32), the one chosen marked
        y += 3;
        for (int n = 0; n < kInLine; n++) {
            const int sx = x + 6 + (n % 32) * 4, sy = y + (n / 32) * 8;
            const bool here = n == v.colour;
            canvas.Fill(static_cast<float>(sx), static_cast<float>(sy), 4, 7, Shown(ColourAt(m, static_cast<uint32_t>(v.line * kInLine + n))));
            if (here) {
                canvas.Fill(static_cast<float>(sx), static_cast<float>(sy + 7), 4, 1, white);
                canvas.Fill(static_cast<float>(sx), static_cast<float>(sy - 1), 4, 1, white);
            }
        }
        y += 18;
        // The colour chosen, large, with its value and the game's (and the one copied with LP)
        canvas.Fill(static_cast<float>(x + 6), static_cast<float>(y), 26, 18, Shown(colour));
        char hex[16];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", (colour & 31) * 255 / 31, ((colour >> 5) & 31) * 255 / 31, ((colour >> 10) & 31) * 255 / 31);
        text(x + 38, y + 1, hex, white, -1);
        text(x + 38, y + 11, "game", dim, -1);
        canvas.Fill(static_cast<float>(x + 64), static_cast<float>(y + 11), 10, 8, Shown(GameColour(m, Index())));
        if (v.copied >= 0) {
            text(x + 80, y + 11, "copy", dim, -1);
            canvas.Fill(static_cast<float>(x + 106), static_cast<float>(y + 11), 10, 8, Shown(static_cast<uint16_t>(v.copied)));
        }
        y += 22;
        const char *const help[] = {"Arrows: choose/change", "LP copy  MP paste",
                                    v.canUndo ? "HK undo random" : v.row == kRowRandom ? "LK twice: random" : "HP game's colour"};
        const char *const loadHelp[] = {"Left/Right: try one", "LP load it  MK delete", "Up/Down: put it back"};
        const char *const *lines = v.row == kRowLoad ? loadHelp : help;
        for (int n = 0; n < 2; n++) {
            text(x + 6, y + 10 * n, lines[n], dim, -1);
        }
        if (v.saidFor > 0) {
            text(x + 6, y + 20, v.said.c_str(), white, -1);
        } else if (v.unsaved >= 0) {
            text(x + 6, y + 20, "Saving...", dim, -1);
        } else {
            text(x + 6, y + 20, lines[2], dim, -1);
        }
    }
} // namespace

bool ModelViewerOn() {
    return v.on && session::InSession();
}

// The viewer (and the colour editor) on its way to the round: shown black, the intro of a versus fight not seen
bool ModelVeiled() {
    return ModelViewerOn() && !v.arrived;
}

void ModelBeforeFrame(Machine &m) {
    if (v.on && (!session::InSession() || session::Starts() != v.session)) {
        v.on = false; // (the board reset, or another session started: training's)
        v.editing = false;
    }
    v.frames++;
    ForceCustomColours(v.on && v.editing);
    if (!v.on) {
        SetSlowMotion(1);
        if (v.fast) {
            v.fast = false;
            SetFastForward(false);
        }
    }
    const bool on = Showing();
    m.hidePanel = m.hidePanel || on;
    m.hideObject = on ? Hidden : nullptr;
    m.frontObject = on && !v.editing && !Plain() && FeatureValue("model_foreground") == 1 ? Front : nullptr;
    const int background = FeatureValue("model_background");
    m.backdrop = on && Plain() ? (background == kCount ? 0x01FFFFFFu : 0x01000000u) : 0;
    if (ModelVeiled()) {
        m.hideObject = HideAll;
        m.frontObject = nullptr;
        m.hidePanel = true;
        m.backdrop = 0x01000000u;
    }
    if (!ModelViewerOn()) {
        return;
    }
    if (v.arrived && (FeatureValue("model_character") != v.character || FeatureValue("model_background") != v.background)) {
        const bool editing = v.editing;
        Start(m); // (another character or stage chosen while viewing: the viewer goes there)
        v.editing = editing;
        return;
    }
    // (the character and the stage, written through the choice; player 2 whatever, it is not seen)
    const bool locked = Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    if (!locked) {
        SetRam8(sfiii3::kChosen[0], static_cast<uint8_t>(std::clamp(FeatureValue("model_character"), 0, kCount - 1)));
        if (v.editing && Ram8(sfiii3::kSelectState[0]) >= 4) {
            SetRam8(sfiii3::kSelectColour[0], static_cast<uint8_t>(v.set)); // (the colour editor's set)
        }
        SetRam8(sfiii3::kStage, static_cast<uint8_t>(std::min(background, kCount - 1)));
    }
    if (on) {
        m.players[1] = 0; // (player 2's fighter is not seen: nobody plays it)
    }
    // (on its way to the round, as the CPS-2 ports' viewer goes: the character the menu's, chosen for both players, the
    // screens before the round pressed through, unseen and at speed)
    if (!on && !session::Booting()) {
        const bool press = v.frames % 12 < 3;
        for (int p = 0; p < 2; p++) {
            const uint8_t state = Ram8(sfiii3::kSelectState[p]);
            m.players[p] = state == 0 ? (press ? kStart : 0) : (press ? kPunch1 : 0);
        }
    }
    if (on && Ram8(sfiii3::kMatchState) == 2) {
        if (!v.arrived) {
            // (the character put in the middle of the stage, so that the camera has the stage all round it)
            const int middle = (static_cast<int16_t>(Ram16(sfiii3::kCameraLeft[0])) + static_cast<int16_t>(Ram16(sfiii3::kCameraRight[0]))) / 2;
            SetRam16(sfiii3::kPlayer[0] + sfiii3::kX, static_cast<uint16_t>(middle - kBeside / 2));
            v.holdX = static_cast<int16_t>(middle - kBeside / 2);
        }
        v.arrived = true; // (the round is on: the entrances and its banners were run at speed)
    }
    if (on) {
        // (player 2, not seen, kept a little to player 1's right: the camera, which keeps both in the picture, follows player
        // 1, and the two never overlap (overlapping, the game swaps their palettes, by which player 2's is left out))
        SetRam16(sfiii3::kPlayer[1] + sfiii3::kX, static_cast<uint16_t>(static_cast<int16_t>(Ram16(sfiii3::kPlayer[0] + sfiii3::kX)) + kBeside));
    }
    {
        const bool fast = !v.arrived && !session::Booting();
        if (fast != v.fast) {
            v.fast = fast;
            SetFastForward(fast);
        }
    }
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
    // (the animation's name at the top, for a while after it changes, as the CPS-2 ports' viewer has it)
    if (v.choice != v.named) {
        v.named = v.choice;
        v.namedAt = v.frames;
    }
    if (Showing() && v.choice > 0 && v.frames - v.namedAt < kNamedFor) {
        char name[16];
        snprintf(name, sizeof(name), "%c%d", 'A' + (v.choice - 1) / kMostInList, (v.choice - 1) % kMostInList);
        lines.push_back({0.5f, 0.04f, 0, name});
    }
}

namespace {
    void Zoom(Machine &m);
}

// After the frame: the picture zoomed about the character's feet
void ModelAfterFrame(Machine &m) {
    if (ModelVeiled() && m.drawing) {
        // (black whatever drew it: a stage board's stage composed under it too)
        uint32_t *picture = m.Picture();
        std::fill(picture, picture + static_cast<size_t>(m.PictureWidth()) * Video::kHeight, 0u);
        return;
    }
    if (!Showing() || !v.arrived) {
        return; // (not till the round is on: the entrances and banners run unseen before it)
    }
    if (v.editing) {
        Zoom(m);
        DrawPanel(m); // (the colour editor's panel over the right of the picture)
        return;
    }
    Zoom(m);
}

namespace {
    // The picture zoomed about the character's feet (the colour editor's: moved left, out from under its panel); once
    // for a picture the machine made (a frame not drawn, or run again in slow motion, leaves the zoomed one as it is)
    void Zoom(Machine &m) {
    if (!m.drawing || m.frame == v.zoomed) {
        return;
    }
    v.zoomed = m.frame;
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
    const double anchorX = width / 2.0 - (v.editing ? kShift : 0), anchorY = size <= 1.5 ? height - 16.0 : height / 2.0;
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
} // namespace

namespace {
    // The menu's "Reset options": every choice of the viewer's page back to what it is when nothing is chosen, the menu
    // left open on the page (Command::note). While viewing, the character and the stage are what is being viewed and
    // stay as they are (another of either would start the round again); the rest is applied as it is shown (the size,
    // standing, full speed, the stage in front).
    void ResetChoices(Machine &) {
        const bool viewing = ModelViewerOn();
        for (const char *choice : {"model_character", "model_background", "model_foreground", "model_size", "model_animation", "model_speed",
                                   "model_repeat", "model_all"}) {
            if (viewing && (!strcmp(choice, "model_character") || !strcmp(choice, "model_background"))) {
                continue;
            }
            SetFeatureValue(choice, 0);
        }
    }
} // namespace

void RegisterModel(Game &game) {
    static const char *const kPage = "Model viewer";
    game.commands.push_back({"model_start", kPage, "Start",
                             "Shows the character chosen below alone, moved with player 1's controls (every move works), on a stage or "
                             "plain white or black, at the size chosen. Reset ends it.",
                             Start});
    game.commands.push_back({"model_reset", kPage, "Reset options",
                             "Every choice on this page back to what it is when nothing is chosen (while viewing, the character and "
                             "stage stay), shown at once; the menu stays open.",
                             ResetChoices, kKeyNone, "The viewer's options are back as they first were."});
    game.features.push_back({"model_character", kPage, "Character", "Who is shown. Changing it while viewing goes to the new one.", kFeatureMenuChoices,
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
                             "What is behind the character: one of the stages, or plain white or black. Changing it while viewing goes to the new one.",
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
                             "ones a character has not got are passed over. Its name is shown at the top when it changes.",
                             kFeatureMenuChoices, choices});
    game.features.push_back({"model_speed", kPage, "Speed", "How fast the character moves: slow motion, to look at a move frame by frame.",
                             0, {"Full", "1/2", "1/4", "1/8"}});
    game.features.push_back({"model_repeat", kPage, "Repeat after", "How long an animation's end is held before it starts again.", 0,
                             {"At once", "1 second", "2 seconds", "3 seconds", "4 seconds", "5 seconds"}});
    game.features.push_back({"model_all", kPage, "Play all",
                             "Goes through all of the character's animations, one after another: each shown once (and its end held for "
                             "\"Repeat after\"), then the next."});

    game.commands.push_back({"colours_start", "Colour editor", "Start",
                             "The model viewer's character with a panel worked with player 1's controls. Changes are saved to the "
                             "palettes folder and used in every fight. Reset ends it.",
                             StartEditor});
    game.features.push_back({"colours_animation", "Colour editor", "Animation",
                             "What the character does in the editor: stands, or goes through one of its animations (as in the model "
                             "viewer). The panel has this row too.",
                             kFeatureMenuChoices, choices});
    game.commands.push_back({"colours_reset_set", "Colour editor", "Game's colours for this set",
                             "While editing: every colour of the set being edited back to the game's.", ResetSet});
    game.commands.push_back({"colours_random", "Colour editor", "Random colours for this set",
                             "While editing: every colour of the set being edited at random (the panel's Random row does it too, "
                             "pressed twice: LK, left or right). HK puts back the colours from before, until the next change; \"Game's "
                             "colours for this set\" puts back the game's.",
                             RandomCommand});
    game.commands.push_back({"colours_reset_character", "Colour editor", "Game's colours for this character",
                             "While editing: every set of the character being edited back to the game's colours.", ResetCharacter});
}

} // namespace cps3recomp
