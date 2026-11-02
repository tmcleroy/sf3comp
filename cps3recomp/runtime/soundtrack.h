// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); its shape is cps2recomp's machine/soundtrack.h.
//
// A custom soundtrack: the game's music played from the player's own files (an arranged soundtrack, say) in place
// of the board's. This is the game's side of it; port/soundtrack.h plays the files.
//
// A CPS-3 game's music is played by its own program (a sound driver on the SH-2 feeding the sound chip's voices),
// not by a sound CPU asked through shared memory as on the CPS-2: so the game's sources say, before each frame, which
// piece of music the driver is playing and whether it has just started it (Follow), read from the driver's own
// records. When the player has a file for it, the game's sources leave the music's voices out of the mix
// (machine::Machine::silenced): they play on unheard, so the game's state is the same as without a soundtrack. What
// is playing (the song, how long since it started) is counted in the game's frames and kept with save states, so a
// state loaded, run-ahead's frames taken back and rollback's frames all leave it where the game is.
#pragma once

#include <cstdint>
#include <vector>

namespace cps3recomp {

class Archive;

class Soundtrack {
public:
    // The numbers of the game's music (the game's sources set them; not kept with save states)
    void SetMusic(std::vector<uint16_t> songs) {
        m_songs = std::move(songs);
    }
    const std::vector<uint16_t> &MusicSongs() const {
        return m_songs;
    }
    // The songs the player's files are for (the port sets it; empty: the board plays all its music)
    void Replace(std::vector<bool> songs) {
        m_replaced = std::move(songs);
    }
    bool Replaced(uint16_t song) const {
        return song < m_replaced.size() && m_replaced[song];
    }
    bool Any() const {
        return !m_replaced.empty();
    }

    static constexpr uint32_t kFull = 0x10000;
    struct State {
        int32_t song = -1;      // the song a file is playing for (-1: none; the board's music, if any, is heard)
        uint32_t serial = 0;    // counts the starts: the same song asked for again starts again
        uint32_t frames = 0;    // frames since it started
        uint32_t level = kFull; // its volume (the game's music fades, where a game's sources follow them)
        int32_t step = 0;
        uint32_t target = kFull;
        uint32_t allLevel = kFull;
        int32_t allStep = 0;
        int32_t board = -1;     // the song the board's driver is playing (-1: none), as last followed
        uint32_t boardFrames = 0; // ... and frames since it started it
    };
    State state;

    // Before each frame, from the game's sources: the song the game's music driver is playing (-1: none), and whether
    // it started it again since the last frame (the same song from its beginning; a start seen again within a few
    // frames of the last is the same one)
    // (CPS3RECOMP_SOUNDTRACK_LOG: each start and stop, to stderr with the frame)
    void Follow(int32_t song, bool started, uint64_t frame);
    // A file plays now in place of the board's music: the game's sources leave its music's voices out
    bool Playing() const {
        return state.song >= 0;
    }
    void Reset() {
        state = State{};
    }
    void Serialize(Archive &ar);

private:
    std::vector<bool> m_replaced;
    std::vector<uint16_t> m_songs;
};

} // namespace cps3recomp
