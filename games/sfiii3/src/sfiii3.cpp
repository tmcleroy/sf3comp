// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's own sources: its features (runtime/game.h).
#include "sfiii3.h"

#include "game.h"
#include "session.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace cps3recomp {

bool AttractOn(); // attract.cpp

void RegisterTraining(Game &game);
void RegisterLifts3rdStrike();
void RegisterStages(Game &game);
void RegisterSoundProbe();
void RegisterBoard(Game &game);
void BoardBeforeFrame(Machine &m);
void StagesBeforeFrame(Machine &machine);
void StageChecksBeforeFrame(Machine &machine);
void MusicBeforeFrame(Machine &machine);
void RingOutBeforeFrame(Machine &machine);
void BattleBeforeFrame(Machine &machine);
void ModelBeforeFrame(Machine &machine);
void RegisterModel(Game &game);
void RegisterColours(Game &game);
void RegisterBattle(Game &game);
bool BattleOn();
void RegisterRingOut(Game &game);
void RegisterMusic(Game &game);
void WidescreenBeforeFrame(Machine &m, bool fighting);
int WidestPicture(); // widescreen.cpp
void RegisterWidescreen(); // widescreen.cpp
void TrainingBeforeFrame(Machine &m);
void ScenarioBeforeFrame(Machine &m);
void RegisterScenarios(Game &game);
std::string MatchLabel();
void AttractBeforeFrame(Machine &m);
void RegisterAttract(Game &game);
void BonusBeforeFrame(Machine &m);
void RegisterBonus(Game &game);
bool BonusOn(); // bonus.cpp
void TrainingAfterFrame(Machine &m);
int WorldShift();     // widescreen.cpp
bool ModelViewerOn(); // model.cpp

namespace {
    // The board's start-up (its checks, the region's warning, Capcom's logo) runs unseen when skip_intro is on: the
    // game shows from its opening, which starts at frame 800 (seen in pictures)
    constexpr uint64_t kOpening = 760;
    bool g_skipping = false;

    void BeforeFrame(Machine &m) {
        BoardBeforeFrame(m);
        if (FeatureOn("skip_intro") && m.frame < kOpening && !session::Booting() && !AttractOn() && !BonusOn()) {
            if (!g_skipping) {
                g_skipping = true;
                SetFastForward(true);
            }
        } else if (g_skipping) {
            g_skipping = false;
            if (!session::Booting()) {
                SetFastForward(false);
            }
        }
        // (attract mode: the board's show, which it runs only with no coin in)
        if (AttractOn()) {
            SetRam8(sfiii3::kCredits, 0);
        } else if (FeatureOn("free_play") && Ram8(sfiii3::kCredits) < 2) {
            SetRam8(sfiii3::kCredits, 2); // (two: a second player can always join)
        }
        // (only while the byte holds a running clock: what it holds outside a fight is not known yet)
        const uint8_t clock = Ram8(sfiii3::kClock);
        if (FeatureOn("infinite_time") && clock > 0 && clock < 99) {
            SetRam8(sfiii3::kClock, 99);
        }
        // (Gill: the character chosen made his, until the match is on)
        const int gill = FeatureValue("play_as_gill");
        const bool locked = Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
        for (int p = 0; p < 2 && gill && !locked; p++) {
            if ((gill == 3 || gill == p + 1) && Ram8(sfiii3::kSelectState[p]) >= 2) {
                SetRam8(sfiii3::kChosen[p], 0);
            }
        }
        WidescreenBeforeFrame(m, (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2);
        MusicBeforeFrame(m);
        StagesBeforeFrame(m);
        TrainingBeforeFrame(m);
        ScenarioBeforeFrame(m); // (after the session's: its controls for player 2 are the last word)
        BattleBeforeFrame(m);
        BonusBeforeFrame(m);
        ModelBeforeFrame(m);
        AttractBeforeFrame(m);
        StageChecksBeforeFrame(m);
    }
} // namespace

void RegisterGame() {
    Game &game = TheGame();
    game.features.push_back({"widescreen", "Display", "Widescreen",
                             "16:9: 64 more columns of the stage on each side. In a fight the camera stays 64 columns further from "
                             "the stage's ends, so the corners come that much sooner than on the board.",
                             kFeatureFirstPage});
    // (the window's shape: the picture's, 16:9 with widescreen, followed when it is switched in the menu)
    game.widestPicture = WidestPicture;
    game.features.push_back({"widescreen_walls", "Display", "Invisible walls",
                             "With widescreen: fights are still shown in 16:9, but played exactly as on the board. The camera goes "
                             "as far as the board's and the corners are the board's; near a stage's end the view stays on the stage, "
                             "so the fight is then to one side of the picture. Nothing without widescreen (nor on another game's stage).",
                             kFeatureEnhanced});
    game.features.push_back({"skip_intro", "Game", "Straight to the opening",
                             "The board's start-up (its memory checks, the region's warning and Capcom's logo) runs unseen, so the game "
                             "shows from its opening in about two seconds.",
                             kFeatureEnhanced});
    game.features.push_back({"play_as_gill", "Game", "Play as Gill",
                             "The boss, Gill, for player 1, player 2 or both: whoever is chosen at character select is Gill (the super art "
                             "chosen is one of his).",
                             0, {"Off", "Player 1", "Player 2", "Both"}});
    game.features.push_back({"free_play", "Game", "Free play", "Start works without a coin.", kFeatureEnhanced});
    game.features.push_back({"infinite_time", "Cheats", "Infinite time", "The round's clock stands still."});
    game.features.push_back({"no_damage", "Cheats", "No damage", "Player 1's health does not go down."});
    game.features.push_back({"no_damage_2", "Cheats", "No damage, player 2", "Player 2's health does not go down (the opponent's, in a one-player game)."});
    game.features.push_back({"one_hit", "Cheats", "One-hit knockouts", "Any hit that does damage takes all the health that is left (both players)."});
    game.features.push_back({"full_gauge", "Cheats", "Full gauge", "Player 1's super art gauge is always full."});
    game.features.push_back({"full_gauge_2", "Cheats", "Full gauge, player 2", "Player 2's super art gauge is always full (the opponent's, in a one-player game)."});
    game.features.push_back({"no_stun", "Cheats", "No stun", "Player 1 is never stunned: the stun they take is put back to none."});
    game.features.push_back({"no_stun_2", "Cheats", "No stun, player 2", "Player 2 is never stunned (the opponent, in a one-player game)."});
    game.beforeFrame = BeforeFrame;
    game.frame = TrainingAfterFrame;
    RegisterTraining(game);
    RegisterScenarios(game);
    game.matchLabel = MatchLabel;
    RegisterLifts3rdStrike();
    RegisterStages(game);
    RegisterWidescreen();
    RegisterMusic(game);
    RegisterBattle(game);
    RegisterModel(game);
    // (the port's zoom on a fighter, port/zoom.h: where its feet are in the picture, while a round is fought; not in the
    // model viewer, which has sizes of its own)
    game.focus = [](int player, int &x, int &y) {
        Machine *m = TheMachine();
        const bool fought = (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2;
        if (!m || !fought || ModelViewerOn() || player < 0 || player > 1) {
            return false;
        }
        const uint32_t base = sfiii3::kPlayer[player];
        x = static_cast<int16_t>(Ram16(base + sfiii3::kX)) - static_cast<int16_t>(Ram16(sfiii3::kCameraX)) + m->PictureWidth() / 2 - WorldShift();
        y = Video::kHeight - (static_cast<int16_t>(Ram16(base + sfiii3::kY)) - static_cast<int16_t>(Ram16(sfiii3::kCameraY))) - 23 - 44;
        return true;
    };
    game.focusLift = 44;
    game.focusTall = 120;
    RegisterColours(game);
    game.noRunAhead = BattleOn; // (a mode with a spare machine: its frames are not run ahead)
    // (with arcade timing: the readable rewrites where the port's own features work inside them)
    game.rewritesNeeded = [] { return BattleOn() || session::InSession(); };
    // The menu's pages for the ways of playing beyond the board's own (as the CPS-2 ports have them)
    game.pages.push_back({"Stage", "", "Where the fights are: 3rd Strike's stages, and New Generation's and 2nd Impact's.", true});
    game.pages.push_back({"Alternative modes", "", "Ways to play beyond the arcade game: training, attract mode, viewers for the stages "
                                                   "and the characters, the colour editor, survival, endless and team battles, ring out, the bonus games.", true});
    game.pages.push_back({"Training mode", "Alternative modes", "A fight with no clock that nobody loses, and the aids for practising in it."});
    game.pages.push_back({"Dummy", "Training mode", "What player 2 does in a training session."});
    game.pages.push_back({"Attract mode", "Alternative modes", "The game's own show on a loop, as with nobody playing: the opening, "
                                                               "the title, demonstration fights."});
    game.pages.push_back({"Survival", "Alternative modes", "One opponent after another on the health you have left. How many can you beat?"});
    game.pages.push_back({"Ring out", "Alternative modes", "No health: knock your opponent over the end of the stage, and jump down to the next."});
    game.pages.push_back({"Endless battle", "Alternative modes", "Two fighters the game plays, for as long as you like. Touch the controls "
                                                                 "to take one over."});
    game.pages.push_back({"Stage viewer", "Alternative modes", "A stage with nobody on it, to look at and listen to."});
    game.pages.push_back({"Model viewer", "Alternative modes", "A character alone, to look at: on a stage or plain, at any size."});
    game.pages.push_back({"Colour editor", "Alternative modes", "The characters' colours changed while you watch: any set, any of its "
                                                                "lines of colours, 32 levels of red, green and blue."});
    game.pages.push_back({"Tag team battle", "Alternative modes", "Two teams: knock a fighter down and the next of that team comes on. The last team standing wins."});
    game.pages.push_back({"Bonus games", "Alternative modes", "The one-player game's bonus stages, played straight away: crush the car, "
                                                              "parry Sean's basketballs."});
    RegisterAttract(game);
    RegisterBonus(game);
    RegisterSoundProbe();
    RegisterBoard(game);
    // The menu's pages and rows in the CPS-2 ports' order (Street Fighter Alpha 2's), this game's own after them
    const auto order = [&](const char *page, std::initializer_list<const char *> ids) {
        std::vector<size_t> at;
        for (size_t n = 0; n < game.features.size(); n++) {
            if (std::string(game.features[n].category) == page) {
                at.push_back(n);
            }
        }
        std::vector<Feature> rows;
        for (const char *id : ids) {
            for (const size_t n : at) {
                if (std::string(game.features[n].name) == id) {
                    rows.push_back(game.features[n]);
                }
            }
        }
        for (const size_t n : at) {
            if (std::find_if(rows.begin(), rows.end(), [&](const Feature &f) { return std::string(f.name) == game.features[n].name; }) == rows.end()) {
                rows.push_back(game.features[n]);
            }
        }
        for (size_t k = 0; k < at.size(); k++) {
            game.features[at[k]] = rows[k];
        }
    };
    order("Game", {"play_as_gill", "free_play", "skip_intro"});
    order("Cheats", {"no_damage", "no_damage_2", "one_hit", "full_gauge", "full_gauge_2", "infinite_time", "no_stun", "no_stun_2"});
    order("Stage", {"stage", "stages_preload"});
    order("Training mode", {"restart_at", "damage_readout", "stun_readout", "parry_readout", "input_display", "hit_boxes",
                            "frame_data", "frame_log", "frame_meter", "health_refill", "gauge_refill"});
    order("Dummy", {"dummy_stand", "dummy_crouch", "dummy_jump", "dummy_record", "dummy_replay", "dummy_replay_any", "dummy_slot",
                    "dummy_guard", "dummy_guard_action", "dummy_parry", "dummy_reversal", "dummy_reversal_random", "dummy_tech",
                    "dummy_getup", "dummy_stun_mash", "dummy_computer"});
    {
        static const char *const kPages[] = {"Training mode", "Attract mode", "Stage viewer", "Survival", "Endless battle", "Ring out",
                                             "Tag team battle", "Bonus games", "Model viewer", "Colour editor"};
        std::stable_sort(game.pages.begin(), game.pages.end(), [](const Page &a, const Page &b) {
            const auto rank = [](const Page &p) {
                if (std::string(p.parent) != "Alternative modes") {
                    return -1;
                }
                for (size_t n = 0; n < std::size(kPages); n++) {
                    if (std::string(p.name) == kPages[n]) {
                        return static_cast<int>(n);
                    }
                }
                return 100;
            };
            return rank(a) < rank(b);
        });
    }
}

} // namespace cps3recomp
