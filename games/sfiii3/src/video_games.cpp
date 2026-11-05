// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The video hand-offs registered, for 3rd Strike and for the stage games (video.h): each game's places and routines,
// and the rewrites made for them. 3rd Strike's run on its own board (rewrites, checked against the game's code by
// CPS3RECOMP_LIFT_CHECK; run dry with the arcade timing) and on a stage board of 3rd Strike's (only dry); New
// Generation's and 2nd Impact's only on the stage board, only dry: they hand the stage board's native interface the
// guest stage's palette, textures, frame registers and object lists, and change nothing of that game.
//
// The stage games' places and routines were found from 3rd Strike's: the vertical blank's handler (0x06000528) is the
// same code in the three, and its calls name the frame's routines; those are the same code but for their literals
// (the routines' places), save 2nd Impact's screen registers and zoom and New Generation's text colours and their
// place, compiled otherwise and read to be the same steps.
#include "video.h"

#include <deque>
#include <string>
#include <vector>

namespace cps3recomp {

// 3rd Strike's (the names as before: the checks' lists name them)
const VideoPlaces kThirdStrikeVideo = {
    .dmaState = 0x0207603C, .fillSet = 0x02078A38, .drainSet = 0x02078A3C, .paletteQueue = 0x0207804C, .paletteCount = 0x0207844C,
    .paletteNext = 0x02078454, .charQueue = 0x02076044, .charCount = 0x0207803C, .charNext = 0x02078044, .kindLog = 0x02078464,
    .kindPlace = 0x0207845C, .charListAt = 0x02007EE0, .charListBank = 0x02007EE4, .textColourBase = 0x02078CE4, .charBank = 0x02079C96,
    .charBankBefore = 0x02079C94,
    .shakeX = 0x0200DCD2, .shakeY = 0x0200DCD4, .objectOffsetX = 0x0200DCD6, .objectOffsetY = 0x0200DCD8, .objectLayers = 0x0206A084,
    .tilemapLayers = 0x0206A104, .tilemapWork = 0x0206A154, .tilemapOffsetX = 0x0200DCDE, .tilemapOffsetY = 0x0200DCE0,
    .tilemapSettings = 0x0206A1BC, .tilemapFlips = 0x0206A1CC, .tilemapBases = 0x0206A17C, .tileBanksAsked = 0x0206A1E0,
    .tileBankSettings = 0x0206A1DE, .tileBankNext = 0x0206A1DC, .tileMasks = 0x06605548, .textLayer = 0x0206A144,
    .textOffsetX = 0x0200DCC6, .textShakeX = 0x0200DCDA, .textOffsetY = 0x0200DCC8, .textShakeY = 0x0200DCDC, .turnSet = 0x02000660,
    .screenAsked = 0x0200DCD1, .mode = 0x0200DCA4, .sprites = 0x0200DCC4, .textKind = 0x0200DCCC, .textKinds = 0x0613E442,
    .turnedOffsets = 0x0613E482, .zoomAsked = 0x0200DCD0, .zoom = 0x0200DCB4, .centreX = 0x0200DCE2, .centreY = 0x0200DCE4,
    .headsAsked = 0x020700A9, .prioritySet = 0x020700A8, .priorities = 0x0206FC94, .priorities2 = 0x0206FE94, .screenPlace = 0x02007ED0,
    .freePages = 0x02074FB8, .takenPages = 0x02072FB4, .ownerPages = 0x02075FBC, .divideError = 0x02079C90,
    .setCharBank = 0x0613AA5E, .restoreCharBank = 0x0613AAB0, .setTileBanks = 0x0612E14C, .modeChanged = 0x06005AFC, .divide = 0x0612D428,
    .takeArea = 0x061379EC, .areaPlace = 0x06137980,
};
const VideoRoutines kThirdStrikeRoutines = {
    .askPalette = {0x06137DF0, 0x1B8, 0xC2DBCF8B57239313ull}, .startNext = {0x06138168, 0x230, 0xD09F1A5FC6A6A73Full},
    .textColours = {0x061388F0, 0x28, 0x93363361932173FCull}, .objectScrolls = {0x0612DDCC, 0x9C, 0xBDC47072323497EDull},
    .tilemapRegisters = {0x0612E610, 0x404, 0x78CCA79ED47596D7ull}, .screenRegisters = {0x06005C00, 0x11E, 0x90CC17B7165E52F1ull},
    .pictureZoom = {0x06005FB8, 0x58, 0xFC8223620916E67Full}, .textPalette = {0x061388DA, 0x16, 0xFDC3A59BA456D6DEull},
    .listHeads = {0x06136F90, 0x284, 0x06D76DCD2C3326A5ull}, .clearListArea = {0x06136782, 0x6A, 0x64F2ABB38AEAC94Full},
    .emptyFrame = {0x061367EE, 0xC0, 0x2A10159006482AFEull},
};

// New Generation's: the frame's places 0x50480 on from 3rd Strike's, the rest in blocks of their own
const VideoPlaces kNewGenerationVideo = {
    .dmaState = 0x0206A120, .fillSet = 0x0206CB1C, .drainSet = 0x0206CB20, .paletteQueue = 0x0206C130, .paletteCount = 0x0206C530,
    .paletteNext = 0x0206C538, .charQueue = 0x0206A128, .charCount = 0x0206C120, .charNext = 0x0206C128, .kindLog = 0x0206C548,
    .kindPlace = 0x0206C540, .charListAt = 0x02007390, .charListBank = 0x02007394, .textColourBase = 0x0206CDC8, .charBank = 0x0206CDF6,
    .charBankBefore = 0x0206CDF4,
    .shakeX = 0x0205E152, .shakeY = 0x0205E154, .objectOffsetX = 0x0205E156, .objectOffsetY = 0x0205E158, .objectLayers = 0x0205E168,
    .tilemapLayers = 0x0205E1E8, .tilemapWork = 0x0205E238, .tilemapOffsetX = 0x0205E15E, .tilemapOffsetY = 0x0205E160,
    .tilemapSettings = 0x0205E2A0, .tilemapFlips = 0x0205E2B0, .tilemapBases = 0x0205E260, .tileBanksAsked = 0x0205E2C4,
    .tileBankSettings = 0x0205E2C2, .tileBankNext = 0x0205E2C0, .tileMasks = 0x0643A0C8, .textLayer = 0x0205E228,
    .textOffsetX = 0x0205E146, .textShakeX = 0x0205E15A, .textOffsetY = 0x0205E148, .textShakeY = 0x0205E15C, .turnSet = 0x02007134,
    .screenAsked = 0x0205E151, .mode = 0x0205E124, .sprites = 0x0205E144, .textKind = 0x0205E14C, .textKinds = 0x0643A008,
    .turnedOffsets = 0x0643A048, .zoomAsked = 0x0205E150, .zoom = 0x0205E134, .centreX = 0x0205E162, .centreY = 0x0205E164,
    .headsAsked = 0x0206418D, .prioritySet = 0x0206418C, .priorities = 0x02063D78, .priorities2 = 0x02063F78, .screenPlace = 0x02007380,
    .freePages = 0x0206909C, .takenPages = 0x02067098, .ownerPages = 0x0206A0A0, .divideError = 0x0206CDF0,
    .setCharBank = 0x063CE056, .restoreCharBank = 0x063CE0A8, .setTileBanks = 0x063BC598, .modeChanged = 0x063BBBF4, .divide = 0x063BB44C,
    .takeArea = 0x063CB540, .areaPlace = 0x063CB4D4,
};
const VideoRoutines kNewGenerationRoutines = {
    .askPalette = {0x063CB944, 0x1B8, 0xA6B2DCC1911778FFull}, .startNext = {0x063CBCBC, 0x230, 0xC391C27BDDC83CD1ull},
    .textColours = {0x063CC478, 0x44, 0xBC32AC5BBA04A655ull}, .objectScrolls = {0x063BC218, 0x9C, 0xBDC47072323497EDull},
    .tilemapRegisters = {0x063BCA5C, 0x404, 0x3C04513F1F2F7EF6ull}, .screenRegisters = {0x063BBCC8, 0x11E, 0x910FCA0D1FDDA71Full},
    .pictureZoom = {0x063BC068, 0x58, 0xFC8223620916E67Full}, .textPalette = {0x063CC44A, 0x20, 0xD89C2B296BE6C5E0ull},
    .listHeads = {0x063CAAE4, 0x284, 0x42F9BD4F2A89B674ull}, .clearListArea = {0x063CA2D6, 0x6A, 0xDDDFF99BB41BEFF5ull},
    .emptyFrame = {0x063CA342, 0xC0, 0x43D15113EAFC57FBull},
};

// 2nd Impact's: the frame's places 0x56840 on from 3rd Strike's (the layers' 0x5B5C back), the rest in blocks of their own
const VideoPlaces kSecondImpactVideo = {
    .dmaState = 0x020704E0, .fillSet = 0x02072EDC, .drainSet = 0x02072EE0, .paletteQueue = 0x020724F0, .paletteCount = 0x020728F0,
    .paletteNext = 0x020728F8, .charQueue = 0x020704E8, .charCount = 0x020724E0, .charNext = 0x020724E8, .kindLog = 0x02072908,
    .kindPlace = 0x02072900, .charListAt = 0x02008734, .charListBank = 0x02008738, .textColourBase = 0x02073188, .charBank = 0x020731B6,
    .charBankBefore = 0x020731B4,
    .shakeX = 0x02064512, .shakeY = 0x02064514, .objectOffsetX = 0x02064516, .objectOffsetY = 0x02064518, .objectLayers = 0x02064528,
    .tilemapLayers = 0x020645A8, .tilemapWork = 0x020645F8, .tilemapOffsetX = 0x0206451E, .tilemapOffsetY = 0x02064520,
    .tilemapSettings = 0x02064660, .tilemapFlips = 0x02064670, .tilemapBases = 0x02064620, .tileBanksAsked = 0x02064684,
    .tileBankSettings = 0x02064682, .tileBankNext = 0x02064680, .tileMasks = 0x0656E080, .textLayer = 0x020645E8,
    .textOffsetX = 0x02064506, .textShakeX = 0x0206451A, .textOffsetY = 0x02064508, .textShakeY = 0x0206451C, .turnSet = 0x020084D4,
    .screenAsked = 0x02064511, .mode = 0x020644E4, .sprites = 0x02064504, .textKind = 0x0206450C, .textKinds = 0x0656DFBE,
    .turnedOffsets = 0x0656DFFE, .zoomAsked = 0x02064510, .zoom = 0x020644F4, .centreX = 0x02064522, .centreY = 0x02064524,
    .headsAsked = 0x0206A54D, .prioritySet = 0x0206A54C, .priorities = 0x0206A138, .priorities2 = 0x0206A338, .screenPlace = 0x02008724,
    .freePages = 0x0206F45C, .takenPages = 0x0206D458, .ownerPages = 0x02070460, .divideError = 0x020731B0,
    .setCharBank = 0x060FB246, .restoreCharBank = 0x060FB298, .setTileBanks = 0x060EE6C0, .modeChanged = 0x060ED978, .divide = 0x060ED204,
    .takeArea = 0x060F8268, .areaPlace = 0x060F81FC,
};
const VideoRoutines kSecondImpactRoutines = {
    .askPalette = {0x060F866C, 0x1B8, 0x69E37069F884C10Eull}, .startNext = {0x060F89E4, 0x230, 0x80DEFCCCA63BCDA0ull},
    .textColours = {0x060F916C, 0x28, 0x93363361932173FCull}, .objectScrolls = {0x060EE340, 0x9C, 0xBDC47072323497EDull},
    .tilemapRegisters = {0x060EEB84, 0x404, 0x752569524A1E335Aull}, .screenRegisters = {0x060EDC00, 0x40, 0x8B0D45F2E6F19C7Cull},
    .pictureZoom = {0x060EE184, 0x5C, 0xEC609FFD98A91743ull}, .textPalette = {0x060F9156, 0x16, 0xFDC3A59BA456D6DEull},
    .listHeads = {0x060F780C, 0x284, 0xBFEB088705315A1Dull}, .clearListArea = {0x060F6FFE, 0x6A, 0x107E682B7C4591E9ull},
    .emptyFrame = {0x060F706A, 0xC0, 0x5A1B25EACEB653BBull},
};

namespace {
    // A game's table: its rewrites (the registers no caller reads: tools/liveout.py on 3rd Strike's) and the empty
    // frame's hook. `boards`: 3rd Strike's on its board and a stage board; the stage games' on a stage board only.
    template <const VideoPlaces &P>
    std::vector<Lift> Table(const VideoRoutines &r, const char *game, uint8_t boards) {
        static std::deque<std::string> names; // (kept where they are: the lifts point at them)
        const auto name = [&](const char *n) {
            names.push_back(*game ? std::string(n) + " (" + game + ")" : std::string(n));
            return names.back().c_str();
        };
        const auto rewrite = [&](const char *n, const VideoRoutine &at, bool (*fn)(Guest &), uint32_t free) {
            Lift l{name(n), at.address, at.size, at.hash, fn, true, free, nullptr, kHandsOver};
            l.boards = boards;
            return l;
        };
        std::vector<Lift> t = {
            rewrite("AskPalette", r.askPalette, AskPalette<P>, 0x100FE),
            rewrite("StartNextDma", r.startNext, StartNextDma<P>, 0x100FE),
            rewrite("TextColours", r.textColours, TextColours<P>, 0x10B7F),
            rewrite("ObjectScrolls", r.objectScrolls, ObjectScrolls<P>, 0x1003F),
            rewrite("TilemapRegisters", r.tilemapRegisters, TilemapRegisters<P>, 0x1001F),
            rewrite("ScreenRegisters", r.screenRegisters, ScreenRegisters<P>, 0x1001F),
            rewrite("PictureZoom", r.pictureZoom, PictureZoom<P>, 0x1001F),
            rewrite("TextPalette", r.textPalette, TextPalette<P>, 0x100F9),
            rewrite("ListHeads", r.listHeads, ListHeads<P>, 0x1007F),
            rewrite("ClearListArea", r.clearListArea, ClearListArea<P>, 0x100FF),
        };
        // 061367EE (hooked: it ends by waiting for the object DMA it starts, which a rewrite cannot)
        Lift hook{name("EmptyFrame"), r.emptyFrame.address, r.emptyFrame.size, r.emptyFrame.hash, nullptr};
        hook.hookFn = EmptyFrame<P>;
        hook.handsOver = true;
        hook.boards = boards;
        t.push_back(hook);
        return t;
    }
} // namespace

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterVideo() {
    static const std::vector<Lift> own = Table<kThirdStrikeVideo>(kThirdStrikeRoutines, "", Lift::kFirstBoard | Lift::kStageBoards);
    static const std::vector<Lift> newGeneration = Table<kNewGenerationVideo>(kNewGenerationRoutines, "New Generation", Lift::kStageBoards);
    static const std::vector<Lift> secondImpact = Table<kSecondImpactVideo>(kSecondImpactRoutines, "2nd Impact", Lift::kStageBoards);
    // (3rd Strike's rewrites through RegisterGameLifts, SFIII3_LIFTS_ONLY's choice; the hook as hooks are, always)
    RegisterGameLifts(own.data(), own.size() - 1);
    RegisterLifts(&own.back(), 1);
    RegisterLifts(newGeneration.data(), newGeneration.size());
    RegisterLifts(secondImpact.data(), secondImpact.size());
    RegisterVideoForms<kThirdStrikeVideo>();
    RegisterVideoForms<kNewGenerationVideo>();
    RegisterVideoForms<kSecondImpactVideo>();
}

} // namespace cps3recomp
