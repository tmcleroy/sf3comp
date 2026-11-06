// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's music as its sound driver plays it, for the custom soundtrack (runtime/soundtrack.h) and the
// definitive version's stages (their music in place of 3rd Strike's):
//
// - The songs: the driver's sound data starts at the address kept at 0x02079C6C (0x0678F000), a table of longs
//   from its fourth byte: song n's data at that many bytes on (n from 1; 0 for none). 49 songs.
// - The song playing: the driver keeps a record for each of the music's 16 tracks (0x02078D0C on, 0x74 each): +0 the
//   place in the track's data, +4 where the track starts (inside its song's data), byte +0x5E its state (0x80 set:
//   stopped). Track 0's start says which song is playing; its place at its start, that it has just started (the
//   song again from the beginning, as the round's music after a continue). Found by watching the records' writes
//   (the driver's routine that starts a song, 0x06139544 on, takes its number in r4 and reads the table above).
// - Which of the sound chip's voices are music now: the driver's records for the music's 16 channels and the effects'
//   16 (0x0207944C on; the effects use 8-15), channel n on voice n, an effect's taking its voice from the music while
//   it plays (MusicVoice).
#include "game.h"
#include "lift.h"
#include "system.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cps3recomp {

namespace {
    constexpr uint32_t kSoundData = 0x02079C6C;   // (a long: where the sound data is)
    constexpr uint32_t kTracks = 0x02078D0C;      // the music's 16 track records
    constexpr uint32_t kTrackSize = 0x74;
    constexpr uint32_t kTrackState = 0x5E;        // (a byte: 0x80 set, stopped)
    constexpr uint32_t kMostSongs = 64;

    constexpr uint32_t kEffectChannels = kTracks + 16 * kTrackSize; // the effects' 16 channel records (0x0207944C)
    int32_t g_songNow = -1; // (the song playing, at the last frame: SongNow)

    // Where each song's data starts, from the table (read once the driver has set it up)
    uint32_t g_tableAt = 0;
    std::vector<uint32_t> g_songStarts; // (by song number; 0: none)
    void ReadSongTable(uint32_t base) {
        g_tableAt = base;
        g_songStarts.assign(1, 0);
        for (uint32_t n = 1; n < kMostSongs; n++) {
            const uint32_t at = Peek32(base + 4 + 4 * n);
            if (at == 0) {
                break;
            }
            g_songStarts.push_back(base + at);
        }
    }

    // The song whose data holds `place` (-1: none)
    int32_t SongAt(uint32_t place) {
        int32_t song = -1;
        for (uint32_t n = 1; n < g_songStarts.size(); n++) {
            if (g_songStarts[n] <= place && (song < 0 || g_songStarts[n] >= g_songStarts[song])) {
                song = static_cast<int32_t>(n);
            }
        }
        return song;
    }
} // namespace

// The song the driver is playing (-1: none), as of the last frame
int SongNow() {
    return g_songNow;
}

// Whether a voice is the music's now: the sound driver gives voice n to its music's channel n unless its effects' channel n
// is playing (sound_driver.cpp, StepChannel: the effect's flags, +94, without 0x80). (Until 2026-11-06 this was noted by
// a hook on the note's pitch routine, 0x0613B1D8, from the registers the driver's code had then; the driver's channel
// step rewritten (sound_driver.cpp) calls that routine with registers of its own, so no voice was ever the music's, and
// on another game's stage 3rd Strike's music played on under the stage's.)
bool MusicVoice(int voice) {
    return voice >= 0 && voice < 16 && (Ram8(kEffectChannels + static_cast<uint32_t>(voice) * kTrackSize + kTrackState) & 0x80) != 0;
}

// Before each frame: the song the driver plays, to the custom soundtrack
void MusicBeforeFrame(Machine &machine) {
    const uint32_t base = Ram32(kSoundData);
    if (base == 0) {
        machine.soundtrack.Follow(-1, false, machine.frame);
        return;
    }
    if (base != g_tableAt) {
        ReadSongTable(base);
    }
    const bool playing = (Ram8(kTracks + kTrackState) & 0x80) == 0;
    const uint32_t place = Ram32(kTracks), start = Ram32(kTracks + 4);
    const int32_t song = playing && start ? SongAt(start) : -1;
    machine.soundtrack.Follow(song, song >= 0 && place == start, machine.frame);
    g_songNow = song;
}

namespace {
    // Fight music: the stage's own, or another stage's. The game asks its driver for a song with 0x061394D0 (r4 the
    // song or sound effect: called from 16 places in its code); a stage's music is the songs 10-41, two to a stage (the
    // second later in the match), so a stage song asked for is changed for the chosen stage's, first or second as asked.
    void StartSongHook(Guest &g) {
        const int choice = FeatureValue("fight_music");
        const uint32_t song = g.R(4) & 0xFFFF;
        if (choice > 0 && song >= 10 && song <= 41) {
            g.R(4) = static_cast<uint32_t>(10 + (choice - 1) * 2 + (song & 1));
        }
    }
} // namespace

void RegisterMusic(Game &game) {
    game.songCount = kMostSongs; // (the custom soundtrack offered; the songs are the table's: MusicBeforeFrame)
    // The songs and where they are heard: the screens' from made-up play (a picture at each song's start), the stages'
    // from a training session on each of the 21 stages (sfiii3.h, kStage). A stage's second song follows its first later in
    // the match (seen for most of them; taken to be so for the rest). The others are not placed yet (the endings, say).
    game.songs = {
        {48, "opening", "Capcom's logo and the opening"},
        {1, "title", "The attract mode's drawings and the title"},
        {2, "character_select", "Character select"},
        {3, "versus", "The versus screen"},
        {4, "winner", "The winner's screen"},
        {7, "challenger", "A challenger comes in"},
        {10, "stage_nyc", "New York: Alex's and Ken's stages"},
        {11, "stage_nyc_2", "... the same, later in the match"},
        {12, "stage_russia", "Russia: Necro's and Twelve's stages"},
        {13, "stage_russia_2", "... the same, later in the match"},
        {14, "stage_hugo", "Hugo's stage"},
        {15, "stage_hugo_2", "... the same, later in the match"},
        {16, "stage_chun_li", "Chun-Li's stage"},
        {17, "stage_chun_li_2", "... the same, later in the match"},
        {18, "stage_ryu", "Ryu's stage"},
        {19, "stage_ryu_2", "... the same, later in the match"},
        {20, "stage_ibuki", "Ibuki's stage"},
        {21, "stage_ibuki_2", "... the same, later in the match"},
        {22, "stage_makoto", "Makoto's stage"},
        {23, "stage_makoto_2", "... the same, later in the match"},
        {24, "stage_akuma", "Akuma's stages"},
        {25, "stage_akuma_2", "... the same, later in the match"},
        {26, "stage_elena", "Elena's stage"},
        {27, "stage_elena_2", "... the same, later in the match"},
        {28, "stage_brazil", "Brazil: Oro's and Sean's stages"},
        {29, "stage_brazil_2", "... the same, later in the match"},
        {30, "stage_dudley", "Dudley's stage"},
        {31, "stage_dudley_2", "... the same, later in the match"},
        {32, "stage_hong_kong", "Hong Kong: Yun's and Yang's stages"},
        {33, "stage_hong_kong_2", "... the same, later in the match"},
        {34, "stage_remy", "Remy's stage"},
        {35, "stage_remy_2", "... the same, later in the match"},
        {36, "stage_urien", "Urien's stage"},
        {37, "stage_urien_2", "... the same, later in the match"},
        {38, "stage_gill", "Gill's stage"},
        {39, "stage_gill_2", "... the same, later in the match"},
        {40, "stage_q", "Q's stage"},
        {41, "stage_q_2", "... the same, later in the match"},
    };
    std::vector<uint16_t> songs;
    for (uint16_t n = 1; n <= 49; n++) {
        songs.push_back(n);
    }
    if (Machine *machine = TheMachine()) {
        machine->soundtrack.SetMusic(std::move(songs));
    }
    AddHook("Song asked for", 0x061394D0, 8, 0x1BF90A4B22ACB049ull, StartSongHook);
    game.features.push_back({"fight_music", "Game", "Fight music",
                             "The music of a fight: its stage's own, or another stage's (both its halves, as the match goes on).",
                             kFeatureMenuChoices,
                             {"The stage's own", "New York (Alex, Ken)", "Russia (Necro, Twelve)", "Hugo's", "Chun-Li's", "Ryu's", "Ibuki's",
                              "Makoto's", "Akuma's", "Elena's", "Brazil (Oro, Sean)", "Dudley's", "Hong Kong (Yun, Yang)", "Remy's",
                              "Urien's", "Gill's", "Q's"}});
}

} // namespace cps3recomp
