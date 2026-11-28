// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// The in-game menu (menu.h): the shared menu told what a CPS-2 game's is made of.
#include "menu.h"

#include <cstring>

#include "../recomp-common/menu_layout.h"
#include "../recomp-common/menu_sdl.h"
#include "../recomp-common/settings.h"
#include "../runtime/system.h"
#include "../runtime/game.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace port {

namespace {
    // The first page's own rows
    enum { kCoin = 1, kStart2, kTest, kLeave };
    // The save states' slots (the Slot row, F6 / F7: 1 to 99, around; slots 1-9 the same files as before, Port::StatePath)
    constexpr int kSlots = 99;
} // namespace

void Menu::OpenOnline(Online online, bool keep) {
    m_menu.Close();
    recomp::MenuConfig config;
    config.groups = {{{recomp::MenuRow::Page, 0, kOnlinePage}},
                     {{recomp::MenuRow::Mute}, {recomp::MenuRow::Resume}},
                     {{recomp::MenuRow::Action, kLeave, online.leave, online.leaveDescription}}};
    config.pages.push_back({kOnlinePage, "", "The match's modes and your own picks for them.", false});
    config.features = std::move(online.features);
    config.commands = std::move(online.commands);
    config.value = std::move(online.value);
    config.setValue = std::move(online.setValue);
    config.muted = [this] { return muted; };
    config.setMuted = [this](bool to) { muted = to; };
    config.confirmName = "Enter or A";
    config.closeName = "Esc";
    if (keep && m_online && m_onlineMenu.IsOpen()) {
        m_onlineMenu.Configure(std::move(config));
        return;
    }
    m_onlineMenu = recomp::Menu();
    m_onlineMenu.SetCompact(true); // (drawn on the game's own picture, at the board's size)
    m_onlineMenu.Configure(std::move(config));
    m_onlineMenu.Open();
    m_onlineMenu.Buttons(recomp::kMenuConfirm); // (into the Online match page, its first row: the first page's first)
    m_online = true;
}

void Menu::Open(bool withStates) {
    m_online = false;
    m_onlineMenu.Close();
    if (onOpen) {
        onOpen();
    }
    Configure(withStates);
    m_menu.Open();
    if (!m_waiting.empty()) {
        m_menu.Note(m_waiting);
        m_waiting.clear();
    }
}

void Menu::Configure(bool withStates) {
    m_withStates = withStates;
    const cps3recomp::Game &game = cps3recomp::TheGame();
    recomp::MenuConfig config;
    // The standard layout (recomp-common/menu_layout.h, cps2recomp's), as tabs: the command row (Back, Save state, Load
    // state, Slot, Reset game, Quit game, Resume) and the tabs Game, Display, Sound, Online, Training, Modes, Stage, Cheats
    // and Capture. Under Game: the speed, Mute, the Board page and the board's own rows.
    // (the game's pages first, so that their words are what the rows say: Standard keeps them and places the pages)
    std::vector<std::string> modes;
    for (const cps3recomp::Page &page : game.pages) {
        // (such pages are ways of running the game: their rows do not count features that are on)
        config.pages.push_back({page.name, page.parent, page.description, false});
        const std::string name = page.name;
        if (page.top && !*page.parent && name != recomp::layout::kStage && name != recomp::layout::kModes) {
            modes.push_back(name); // (another of the game's ways to play: beside Alternative modes)
        }
    }
    recomp::layout::Rows rows;
    if (replaysPage) {
        rows.heads.push_back("Replays");
        config.pages.push_back({"Replays", "", "Your matches, kept as you play them, to watch again and to play on from.", false});
    }
    rows.heads.push_back(recomp::layout::kStage);
    rows.modes = std::move(modes);
    rows.states = withStates;
    config.pages.push_back({"Arcade monitor", recomp::layout::kDisplay, "A CRT at the board's own resolution: set on the launcher."});
    rows.actions = {
        {recomp::MenuRow::Action, kCoin, recomp::layout::kInsertCoin, "Puts a coin in the first slot."},
        {recomp::MenuRow::Action, kStart2, recomp::layout::kPlayer2Joins,
         "Presses player 2's Start: with a coin in (or free play), a second player to practise against."},
        {recomp::MenuRow::Action, kTest, recomp::layout::kTestMenu,
         "The game's own operator menu (the board's test switch, F2): its settings, such as difficulty and rounds. "
         "Its Exit row goes back."},
    };
    rows.resetOptionsDescription = "Puts every option back as it first was (the launcher's Enhanced preset), and the speed to full. The "
                                   "game's own settings (its Test menu) and the controls stay as they are.";
    rows.resetDescription = "Starts the board again, as if switched off and on.";
    recomp::layout::Tabbed(config, rows);
    for (const cps3recomp::Feature &feature : game.features) {
        config.features.push_back({feature.category, feature.title, feature.description, {feature.choices.begin(), feature.choices.end()}});
    }
    // (the port's own row under one of them, extraRow: the menu's features are then the game's with it after that one,
    // and `of` turns the menu's place into the game's)
    ExtraRow extra = extraRow ? extraRow() : ExtraRow{};
    size_t extraAt = SIZE_MAX;
    for (size_t i = 0; i < game.features.size() && !extra.after.empty(); i++) {
        if (extra.after == game.features[i].name) {
            extraAt = i + 1;
            config.features.insert(config.features.begin() + static_cast<std::ptrdiff_t>(extraAt),
                                   recomp::MenuFeature{game.features[i].category, extra.title, extra.description, extra.choices});
            break;
        }
    }
    auto of = [extraAt](size_t at) { return at < extraAt ? at : at - 1; };
    for (const cps3recomp::Command &command : game.commands) {
        if (hosting && !strcmp(command.name, "online_host")) {
            config.commands.push_back({command.category, "Stop hosting",
                                       "Others can no longer join: the match's code stops working. Your game goes on as it is."});
            continue;
        }
        if (recordingClip && !strcmp(command.name, "scene_clip")) {
            config.commands.push_back({command.category, "Stop recording", "Ends the clip being recorded here; its frames so far are kept."});
            continue;
        }
        if (recordingVideo && !strcmp(command.name, "video_record")) {
            config.commands.push_back({command.category, "Stop recording video", "Ends the video being recorded and saves it."});
            continue;
        }
        config.commands.push_back({command.category, command.title, command.description});
    }
    // (the replays' rows: commands after the game's, Press tells them by their place)
    for (const ReplayRow &row : replays) {
        config.commands.push_back({"Replays", row.title, row.detail});
    }
    config.value = [extraAt, of, get = extra.value](size_t at) {
        return at == extraAt ? (get ? get() : 0) : cps3recomp::FeatureSetting(cps3recomp::TheGame().features[of(at)].name);
    };
    config.setValue = [extraAt, of, set = extra.setValue](size_t at, int value) {
        if (at == extraAt) {
            if (set) {
                set(value);
            }
            return;
        }
        cps3recomp::SetFeatureValue(cps3recomp::TheGame().features[of(at)].name, value);
    };
    // (of a long choice, those the launcher leaves in the menu: settings.ini's [menu], recomp-common/settings.h)
    {
        recomp::Ini settings(cps3recomp::SettingsPath());
        settings.load();
        std::vector<std::vector<bool>> offered;
        for (const recomp::FeatureInfo &info : cps3recomp::FeatureList()) {
            offered.push_back(recomp::MenuChoices(settings, info));
            if (info.flags & cps3recomp::kFeatureLauncherOnly) {
                // (none but the one chosen and off, which the menu always offers: the row can only be put back to off)
                offered.back().assign(std::max<size_t>(info.choices.size(), 2), false);
            }
            if (choiceAvailable) {
                for (size_t choice = 1; choice < offered.back().size(); choice++) {
                    if (offered.back()[choice] && !choiceAvailable(info.name, static_cast<int>(choice))) {
                        offered.back()[choice] = false;
                    }
                }
            }
        }
        config.offered = [offered, extraAt, of](size_t at, int choice) {
            const size_t feature = of(at);
            return at == extraAt || feature >= offered.size() || choice < 0 || choice >= static_cast<int>(offered[feature].size()) ||
                   offered[feature][static_cast<size_t>(choice)];
        };
    }
    // (an option's default, which X puts back and against which a row shows as changed: as the launcher's Enhanced
    // preset has it, as Reset all options puts it)
    {
        const std::vector<recomp::FeatureInfo> list = cps3recomp::FeatureList();
        recomp::Ini preset("");
        recomp::SetPreset(preset, list, "enhanced");
        std::vector<int> defaults = recomp::FeatureValues(preset, list);
        config.defaultValue = [defaults = std::move(defaults)](size_t feature) { return feature < defaults.size() ? defaults[feature] : 0; };
    }
    config.tabsName = recomp::sdl::kTabsName;
    config.backName = recomp::sdl::kBackName;
    recomp::layout::Speeds(config);
    config.slots = kSlots;
    config.speedDescription = "Left/Right: slow motion, or faster than the board. While playing, P (or a click of the right stick) stops "
                              "the game and starts it again; O (or, once stopped, the left trigger) goes on by one frame; - and = change "
                              "the speed.";
    config.muted = [this] { return muted; };
    config.setMuted = [this](bool to) { muted = to; };
    config.confirmName = "Enter or A";
    config.closeName = "Esc";
    // (each option's hotkey, shown on its row: H or Y there gives it one; the ids are the menu features', the port's
    // own extra row having none)
    m_ids.clear();
    for (const cps3recomp::Feature &feature : game.features) {
        m_ids.push_back(feature.name);
    }
    if (extraAt != SIZE_MAX) {
        m_ids.insert(m_ids.begin() + static_cast<std::ptrdiff_t>(extraAt), "");
    }
    LoadHotkeys();
    config.hotkey = [this](size_t feature) { return feature < m_ids.size() && !m_ids[feature].empty() ? m_hotkeys.Of(m_ids[feature]).Name() : std::string(); };
    config.hotkeyName = recomp::sdl::kHotkeyName;
    m_menu.Configure(std::move(config));
    m_configured = true;
}

void Menu::LoadHotkeys() {
    recomp::Ini settings(cps3recomp::SettingsPath());
    settings.load();
    m_hotkeys.Load(settings.get("menu", "hotkeys"));
    m_hotkeysLoaded = true;
}

bool Menu::HotkeyListen(const SDL_Event &event, const std::function<std::string(recomp::Hotkey)> &taken) {
    if (m_online || !m_menu.IsOpen()) {
        return false;
    }
    bool changed = false;
    const bool answered = recomp::sdl::HotkeyListen(event, m_menu, m_hotkeys, m_ids, taken, changed);
    if (changed) {
        recomp::Ini settings(cps3recomp::SettingsPath());
        settings.load();
        settings.set("menu", "hotkeys", m_hotkeys.Save());
        settings.save();
    }
    return answered;
}

std::string Menu::HotkeyPress(const SDL_Event &event) {
    if (!m_hotkeysLoaded) {
        LoadHotkeys(); // (before the menu is first opened: those kept from before)
    }
    if (IsOpen() || m_hotkeys.Find(recomp::sdl::HotkeyOf(event)).empty()) {
        return "";
    }
    if (!m_configured) {
        Configure(m_withStates); // (the menu's options, for Step, before it is first opened)
    }
    return recomp::sdl::HotkeyPress(event, m_menu, m_hotkeys, m_ids);
}

Menu::Menu() {
    // (so that - and = work before the menu is first opened)
    recomp::MenuConfig config;
    recomp::layout::Speeds(config);
    config.slots = kSlots; // (and F6 / F7 go through them all)
    m_menu.Configure(std::move(config));
}

Menu::Rate Menu::GameRate() const {
    const recomp::layout::Speed speed = recomp::layout::SpeedOf(m_menu.Speed());
    // (the slower of the player's choice and what the game's sources ask for: runtime/game.h)
    const int slowBy = cps3recomp::SlowMotion();
    if (slowBy > 1 && speed.frames * slowBy > speed.ticks) {
        return {1, slowBy};
    }
    return {speed.frames, speed.ticks};
}

void Menu::SetFullSpeed() {
    m_menu.SetSpeed(recomp::layout::kFull);
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
    if (pressed && !buttons && IsOpen()) {
        Shown().Note("");
    }
    return Press(buttons);
}

Menu::Action Menu::Press(unsigned buttons) {
    return Take(Shown().Buttons(buttons));
}

Menu::Action Menu::Take(const recomp::Menu::Chosen &chosen) {
    switch (chosen.kind) {
    case recomp::Menu::Chosen::Save: return Action::Save;
    case recomp::Menu::Chosen::Load: return Action::Load;
    case recomp::Menu::Chosen::Quit: return Action::Quit;
    case recomp::Menu::Chosen::Reset: return Action::Reset;
    case recomp::Menu::Chosen::ResetOptions: return Action::ResetOptions;
    case recomp::Menu::Chosen::Command:
        m_command = static_cast<size_t>(chosen.id);
        if (m_online) {
            return Action::OnlineCommand;
        }
        if (m_command >= cps3recomp::TheGame().commands.size()) {
            m_replay = m_command - cps3recomp::TheGame().commands.size();
            return Action::Replay;
        }
        return Action::Command;
    case recomp::Menu::Chosen::Action:
        return chosen.id == kLeave ? Action::Leave : chosen.id == kCoin ? Action::Coin : chosen.id == kStart2 ? Action::Start2 : chosen.id == kTest ? Action::Test : Action::None;
    default: return Action::None;
    }
}

} // namespace port
