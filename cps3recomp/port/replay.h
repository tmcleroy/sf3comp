// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// Match replays: every match is kept as a small file (the game as the match began, as a save state, and what each
// player held on every frame after), and played back from the in-game menu's Replays page: the game runs again from
// that state with those controls, so the replay is the match itself, with everything the port can do to a running
// game (stopping, a frame at a time, slow motion, the readouts and hit boxes). While one plays the player can go back
// and forward in it, and take over a side with Start: the game is theirs from that frame, against what the other side
// did in the match.
//
// A match (Game::fighting says when a round is fought) begins with its first round and ends a while after its last;
// offline, the frames are the port's own (Port::RunFrame), online the frames both sides have the real controls for
// (Netplay), from the state the match starts with on both. A replay is of the build that made it: a state is only
// this build's (the version and the state's version are kept, and another's is not played), and a hash of the game
// every few seconds says if a replay goes another way than the match did.
#pragma once

#include <array>
#include <cstdint>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace port {

struct Replay {
    using Pads = std::array<uint16_t, 4>;
    static constexpr int kMost = 40;            // files kept (the oldest go)
    static constexpr uint32_t kQuiet = 900;     // frames with no round fought after which a match is over
    static constexpr uint32_t kTail = 240;      // ... and how many of them are kept
    static constexpr uint32_t kLeast = 600;     // a match of fewer fought frames is not kept (nor one with next to no input)
    static constexpr uint32_t kLongest = 60 * 60 * 40;
    static constexpr uint32_t kHashEvery = 300;
    static constexpr uint32_t kKeepEvery = 600; // (playing: a state kept this often, to go back to)
    static constexpr int kJump = 300;           // frames a press of left or right goes
    static constexpr int kSkipATick = 30;       // frames run in a tick of the screen on the way to a place

    struct Data {
        std::string game, version, label;
        uint32_t stateVersion = 0;
        int64_t when = 0;
        uint32_t frames = 0, fought = 0;
        bool online = false;
        std::vector<std::pair<std::string, int>> features;
        std::vector<uint8_t> state;
        std::map<uint32_t, Pads> changes;    // what the players held from each frame it changed at
        std::map<uint32_t, uint32_t> hashes; // the game after so many frames
    };
    // A file, as the menu lists it
    struct Entry {
        std::string path, title, detail;
    };

    std::string folder; // where they are kept (the player's folder's replays/, as the port started; "": nowhere)

    // --- a match being kept ---
    bool recording = false, wasFighting = false;
    Data made;
    uint32_t lastFought = 0;
    Pads lastHeld{};

    // --- a replay being played ---
    bool playing = false;
    Data play;
    uint32_t at = 0; // frames of it run
    Pads now{};      // what its players hold at `at`
    bool taken[2] = {false, false};   // a side the player has taken over
    bool startHeld[2] = {false, false}; // ... whose Start is still held from taking it (not the game's)
    bool anyTaken() const {
        return taken[0] || taken[1];
    }
    std::vector<uint8_t> takenState; // the game as the first side was taken (the restart key goes back to it)
    uint32_t takenAt = 0;
    Pads takenNow{};
    int skip = 0;          // frames to run unseen, on the way to a place
    bool outOfStep = false; // a hash was not the match's
    bool over = false;      // its last frame has been played
    uint16_t lastPads[2] = {0xFFFF, 0xFFFF}; // (the controls' edges)
    uint64_t hintUntil = 0; // the tick the controls are said until
    // (the game and the features as they were before it, for when it is stopped)
    std::vector<uint8_t> ownState;
    std::map<std::string, int> ownFeatures;
    // (states kept on the way, to go back to: packed on another thread)
    struct Kept {
        std::vector<uint8_t> packed;
        size_t size = 0;
        Pads now{};
    };
    std::map<uint32_t, std::shared_future<Kept>> kept;

    static bool Write(const std::string &path, const Data &data);
    // (headOnly: all but the state and the controls, for the list)
    static bool Read(const std::string &path, Data &data, bool headOnly, std::string &error);
    static std::vector<Entry> List(const std::string &folder);
    static std::string Length(uint32_t frames); // "1:35"
};

} // namespace port
