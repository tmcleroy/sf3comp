// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// The in-game menu (menu.h): the shared menu told what a CPS-2 game's is made of.
#include "menu.h"

#include <cstring>

#include "../runtime/system.h"
#include "../runtime/game.h"

#include <algorithm>
#include <iterator>

namespace port {

namespace {
    // The first page's own rows
    enum { kCoin = 1, kStart2, kReset, kTest, kResetOptions };

    // The speeds, slowest first, each about 1.4 times the one before: the game runs `frames` frames in `ticks` of
    // the screen's
    struct Speed {
        const char *name;
        int frames, ticks;
    };
    const Speed kSpeeds[] = {{"1/8", 1, 8}, {"1/6", 1, 6}, {"1/4", 1, 4}, {"1/3", 1, 3}, {"1/2", 1, 2}, {"3/4", 3, 4}, {"Full", 1, 1},
                             {"1.5x", 3, 2}, {"2x", 2, 1}, {"3x", 3, 1}, {"4x", 4, 1}, {"6x", 6, 1}, {"8x", 8, 1}};
    constexpr int kFull = 6;
} // namespace

void Menu::Open(bool withStates) {
    if (onOpen) {
        onOpen();
    }
    const cps3recomp::Game &game = cps3recomp::TheGame();
    recomp::MenuConfig config;
    // Online, Display and Game first (issue #196), then the game's pages that belong at the head; then three groups
    // with a line between them: the save states, what the game offers, and leaving. Under Game: the speed, Mute, and
    // the Board, Cheats and Sound pages (a first page of fewer rows).
    config.groups.push_back({{recomp::MenuRow::Page, 0, "Online"}, {recomp::MenuRow::Page, 0, "Display"}, {recomp::MenuRow::Page, 0, "Game"}});
    if (replaysPage) {
        config.groups.back().push_back({recomp::MenuRow::Page, 0, "Replays"});
        config.pages.push_back({"Replays", "", "Your matches, kept as you play them, to watch again and to play on from.", false});
    }
    config.pages.push_back({"Game", "", "", true, {recomp::MenuRow::Speed, recomp::MenuRow::Mute}});
    for (const char *under : {"Board", "Cheats", "Sound"}) {
        config.pages.push_back({under, "Game"});
    }
    std::vector<recomp::MenuRow> top;
    for (const cps3recomp::Page &page : game.pages) {
        // (such pages are ways of running the game: their rows do not count features that are on)
        config.pages.push_back({page.name, page.parent, page.description, false});
        if (page.top && !*page.parent) {
            top.push_back({recomp::MenuRow::Page, 0, page.name});
        }
    }
    if (!top.empty()) {
        config.groups.push_back(std::move(top));
    }
    if (withStates) {
        config.groups.push_back({{recomp::MenuRow::Save}, {recomp::MenuRow::Load}, {recomp::MenuRow::Slot}});
    }
    config.groups.push_back({
        {recomp::MenuRow::Pages},
        {recomp::MenuRow::Action, kCoin, "Insert coin", "Puts a coin in the first slot."},
        {recomp::MenuRow::Action, kStart2, "Player 2 joins",
         "Presses player 2's Start: with a coin in (or free play), a second player to practise against."},
        {recomp::MenuRow::Action, kTest, "Test menu",
         "The game's own operator menu (the board's test switch, F2): its settings, such as difficulty and rounds. "
         "Its Exit row goes back."},
    });
    config.groups.push_back({
        {recomp::MenuRow::Action, kResetOptions, "Reset all options",
         "Puts every option back as it first was (the launcher's Enhanced preset), and the speed to full. The game's own "
         "settings (its Test menu) and the controls stay as they are."},
        {recomp::MenuRow::Action, kReset, "Reset game", "Starts the board again, as if switched off and on."},
        {recomp::MenuRow::Quit},
        {recomp::MenuRow::Resume},
    });
    for (const cps3recomp::Feature &feature : game.features) {
        config.features.push_back({feature.category, feature.title, feature.description, {feature.choices.begin(), feature.choices.end()}});
    }
    for (const cps3recomp::Command &command : game.commands) {
        if (hosting && !strcmp(command.name, "online_host")) {
            config.commands.push_back({command.category, "Stop hosting",
                                       "Others can no longer join: the match's code stops working. Your game goes on as it is."});
            continue;
        }
        config.commands.push_back({command.category, command.title, command.description});
    }
    // (the replays' rows: commands after the game's, Press tells them by their place)
    for (const ReplayRow &row : replays) {
        config.commands.push_back({"Replays", row.title, row.detail});
    }
    config.value = [](size_t feature) { return cps3recomp::FeatureSetting(cps3recomp::TheGame().features[feature].name); };
    config.setValue = [](size_t feature, int value) { cps3recomp::SetFeatureValue(cps3recomp::TheGame().features[feature].name, value); };
    // (of a long choice, those the launcher leaves in the menu: settings.ini's [menu], recomp-common/settings.h)
    {
        recomp::Ini settings(cps3recomp::SettingsPath());
        settings.load();
        std::vector<std::vector<bool>> offered;
        for (const recomp::FeatureInfo &info : cps3recomp::FeatureList()) {
            offered.push_back(recomp::MenuChoices(settings, info));
        }
        config.offered = [offered](size_t feature, int choice) {
            return feature >= offered.size() || choice < 0 || choice >= static_cast<int>(offered[feature].size()) || offered[feature][static_cast<size_t>(choice)];
        };
    }
    Speeds(config);
    config.speedDescription = "Left/Right: slow motion, or faster than the board. While playing, P (or a click of the right stick) stops "
                              "the game and starts it again; O (or, once stopped, the left trigger) goes on by one frame; - and = change "
                              "the speed.";
    config.muted = [this] { return muted; };
    config.setMuted = [this](bool to) { muted = to; };
    config.confirmName = "Enter or A";
    config.closeName = "Esc";
    m_menu.Configure(std::move(config));
    m_menu.Open();
}

Menu::Menu() {
    // (so that - and = work before the menu is first opened)
    recomp::MenuConfig config;
    Speeds(config);
    m_menu.Configure(std::move(config));
}

void Menu::Speeds(recomp::MenuConfig &config) {
    for (const Speed &speed : kSpeeds) {
        config.speeds.push_back(speed.name);
    }
    config.normalSpeed = kFull;
}

Menu::Rate Menu::GameRate() const {
    const Speed &speed = kSpeeds[std::clamp(m_menu.Speed(), 0, static_cast<int>(std::size(kSpeeds)) - 1)];
    // (the slower of the player's choice and what the game's sources ask for: runtime/game.h)
    const int slowBy = cps3recomp::SlowMotion();
    if (slowBy > 1 && speed.frames * slowBy > speed.ticks) {
        return {1, slowBy};
    }
    return {speed.frames, speed.ticks};
}

void Menu::SetFullSpeed() {
    m_menu.SetSpeed(kFull);
}

void Menu::Faster(int by) {
    m_menu.SetSpeed(m_menu.Speed() + by);
}

Menu::Action Menu::Buttons(uint16_t pressed) {
    unsigned buttons = 0;
    buttons |= (pressed & cps3recomp::kUp) ? recomp::kMenuUp : 0;
    buttons |= (pressed & cps3recomp::kDown) ? recomp::kMenuDown : 0;
    buttons |= (pressed & cps3recomp::kLeft) ? recomp::kMenuLeft : 0;
    buttons |= (pressed & cps3recomp::kRight) ? recomp::kMenuRight : 0;
    if (pressed & (cps3recomp::kPunch1 | cps3recomp::kPunch2 | cps3recomp::kPunch3 | cps3recomp::kKick1 | cps3recomp::kKick2 |
                   cps3recomp::kKick3 | cps3recomp::kStart)) {
        buttons |= recomp::kMenuConfirm;
    }
    // (a press of something else, a coin button say, still takes the note away, as it always did)
    if (pressed && !buttons && m_menu.IsOpen()) {
        m_menu.Note("");
    }
    return Press(buttons);
}

Menu::Action Menu::Press(unsigned buttons) {
    const recomp::Menu::Chosen chosen = m_menu.Buttons(buttons);
    switch (chosen.kind) {
    case recomp::Menu::Chosen::Save: return Action::Save;
    case recomp::Menu::Chosen::Load: return Action::Load;
    case recomp::Menu::Chosen::Quit: return Action::Quit;
    case recomp::Menu::Chosen::Command:
        m_command = static_cast<size_t>(chosen.id);
        if (m_command >= cps3recomp::TheGame().commands.size()) {
            m_replay = m_command - cps3recomp::TheGame().commands.size();
            return Action::Replay;
        }
        return Action::Command;
    case recomp::Menu::Chosen::Action:
        return chosen.id == kCoin           ? Action::Coin
               : chosen.id == kStart2       ? Action::Start2
               : chosen.id == kTest         ? Action::Test
               : chosen.id == kResetOptions ? Action::ResetOptions
                                            : Action::Reset;
    default: return Action::None;
    }
}

} // namespace port
