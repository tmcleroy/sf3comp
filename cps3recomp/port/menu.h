// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// The in-game menu: save states (save, load, slot), a coin, the speed the game runs at, the game's optional
// features and commands (a page for each of their categories), and leaving the game, while playing.
// The frontend opens it, stops the game while it is open and gives it the player's presses; it draws itself
// over a copy of the game's picture.
//
// The menu itself is the one the recomp ports share (recomp-common/menu.h, a copy of recomp-common's: see
// tools/sync-common.sh). This is what is CPS-2's and this toolkit's about it: the rows of the first page,
// the buttons, the speeds, and the games' features and commands handed over.
#pragma once

#include "../recomp-common/menu.h"
#include "../recomp-common/hotkey.h"

#include <cstdint>
#include <functional>
#include <vector>
#include <string>

namespace port {

class Menu {
public:
    enum class Action { None, Save, Load, Coin, Start2, Test, Reset, ResetOptions, Command, Replay, Quit, OnlineCommand, Leave };

    Menu();
    // withStates: show the save state rows
    void Open(bool withStates);
    // Hotkeys (recomp-common/hotkey.h; settings.ini's [menu] hotkeys): H or Y on an option's row asks for a key or
    // button. HotkeyListen: an event while it asks, which answers it (true: the event was that), `taken` saying why
    // one plays the game. HotkeyPress: in the game, a hotkey's event steps its option; what it is now ("" none).
    bool HotkeyListen(const SDL_Event &event, const std::function<std::string(recomp::Hotkey)> &taken);
    std::string HotkeyPress(const SDL_Event &event);
    // During an online match (Port::OpenOnlineMenu): the match's own menu, drawn small over the game, which goes on for
    // both players. It opens on its Online match page (the host's modes to start, as commands; this player's own picks,
    // as features), and its first page has that page, Mute, Resume and leaving the match. A command chosen is
    // Action::OnlineCommand (Command(): its place in `commands`), leaving Action::Leave.
    struct Online {
        std::vector<recomp::MenuFeature> features;
        std::vector<recomp::MenuCommand> commands;
        std::function<int(size_t feature)> value;
        std::function<void(size_t feature, int value)> setValue;
        std::string leave, leaveDescription;
    };
    static constexpr const char *kOnlinePage = "Online match";
    void OpenOnline(Online online, bool keep = false);
    bool OnlineOpen() const {
        return m_online && m_onlineMenu.IsOpen();
    }
    void Close() {
        m_menu.Close();
        m_onlineMenu.Close();
    }
    bool IsOpen() const {
        return m_menu.IsOpen() || m_onlineMenu.IsOpen();
    }
    // Buttons newly pressed (cps3recomp::Button bits): Up/Down choose, any punch, kick or Start changes or
    // goes into a page, Left/Right change a row with several values.
    // Returns what the frontend is to do (with Slot()); it closes the menu if that went well.
    // (The tests' way in. A player's presses come through Press.)
    Action Buttons(uint16_t pressed);
    // The menu's own buttons newly pressed (recomp::kMenu... bits), which the frontend makes from keys and
    // gamepad buttons that are the same whatever the game's are set to: the arrows, the d-pad and the left
    // stick; Space, Enter and A choose; Backspace and B go back.
    Action Press(unsigned buttons);
    // What the shared menu returned (its mouse's, say: recomp-common/menu_sdl.h, MouseEvent), as Press returns it
    Action Take(const recomp::Menu::Chosen &chosen);
    // The shared menu itself, for the mouse's events
    recomp::Menu &Shared() {
        return Shown();
    }
    // The "Sound" row: the frontend plays nothing while this is set (--mute starts with it set)
    bool muted = false;
    // The Replays page's rows after the game's commands' (the port's match replays): a title and what it says for
    // each, set before the menu is opened; Action::Replay with Replay(): which. `replayPlaying`: the first is the
    // row that stops the one being played
    struct ReplayRow {
        std::string title, detail;
    };
    std::vector<ReplayRow> replays;
    bool replaysPage = false;      // (the game keeps replays: the page is there)
    std::function<void()> onOpen;  // (before the menu is made, each time it opens: the port lists the replays)
    // (whether a choice can be had now, asked each time the menu opens: one that is not is passed over by Left, Right and
    // Confirm, as one the launcher leaves out of the menu; the Dreamcast music without its disc. Not given: all can)
    std::function<bool(const std::string &feature, int choice)> choiceAvailable;
    // (a row of the port's own on the page of one of the game's features, under it, made anew each time the menu opens:
    // the Display page's Your own preset under the Shader, Port::Init's. Not given, or `after` none of the features: none)
    struct ExtraRow {
        std::string after; // (the feature's name)
        std::string title, description;
        std::vector<std::string> choices;
        std::function<int()> value;
        std::function<void(int)> setValue;
    };
    std::function<ExtraRow()> extraRow;
    size_t Replay() const {
        return m_replay;
    }
    bool hosting = false; // (a match hosted from the Online page, waited for: its row is Stop hosting)
    bool recordingClip = false;  // (a scene clip being recorded: the Scene export page's Record clip row is Stop recording)
    bool recordingVideo = false; // (a video being recorded: the Video page's Record video row is Stop recording video)
    int Slot() const {
        return m_menu.Slot();
    }
    void SetSlot(int slot) {
        m_menu.SetSlot(slot);
    }
    // After Action::Command: which of the game's commands (TheGame().commands)
    size_t Command() const {
        return m_command;
    }
    // The speed the game runs at (the player's choice): `frames` frames in `ticks` of the screen's
    struct Rate {
        int frames, ticks;
    };
    Rate GameRate() const;
    bool FullSpeed() const {
        const Rate rate = GameRate();
        return rate.frames == rate.ticks;
    }
    // The next speed up (by 1) or down (by -1)
    void Faster(int by);
    void SetFullSpeed();
    // A line to tell the player (why not, say), until the next press; given while the menu is closed (a choice the
    // launcher made, taking effect at the start), it waits and is shown when the menu next opens
    void Note(const std::string &text) {
        if (IsOpen()) {
            Shown().Note(text);
        } else {
            m_waiting = text;
        }
    }
    // Over a picture of 32-bit pixels (red in the low byte)
    void Draw(uint32_t *pixels, int width, int height) const {
        Shown().Draw(pixels, width, height);
    }
    // The rows in small letters, close together, for a screen of few lines (the Arcade monitor page's Compact menu)
    void SetCompact(bool compact) {
        m_menu.SetCompact(compact);
    }
    bool Compact() const {
        return m_menu.Compact();
    }

private:
    void Configure(bool withStates); // (the menu as Open shows it, without opening it)
    void LoadHotkeys();
    // (the menu open, or last open: the online match's while it is)
    recomp::Menu &Shown() {
        return m_online ? m_onlineMenu : m_menu;
    }
    const recomp::Menu &Shown() const {
        return m_online ? m_onlineMenu : m_menu;
    }

    recomp::Menu m_menu;
    recomp::Menu m_onlineMenu; // (made afresh at each OpenOnline: on its page, its first row)
    bool m_online = false;
    recomp::Hotkeys m_hotkeys;
    std::vector<std::string> m_ids; // (each menu feature's id, for the hotkeys)
    bool m_configured = false;      // (Configure has given m_menu the game's features)
    bool m_hotkeysLoaded = false;
    bool m_withStates = true;
    size_t m_command = 0, m_replay = 0;
    std::string m_waiting; // (a note given while the menu was closed: Note)
};

} // namespace port
