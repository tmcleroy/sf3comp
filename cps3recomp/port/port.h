// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// Shared between the port's entry point (main.cpp) and its window (frontend_sdl.cpp).
#pragma once

#include "system.h"
#include "menu.h"
#include "netplay.h"
#include "present.h"
#include "replay.h"
#include "soundtrack.h"
#include "zoom.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace port {

// A path as UTF-8 text (as the rest of the port keeps paths), and back
inline std::string PathText(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
inline std::filesystem::path TextPath(const std::string &text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

struct Options {
    std::string romPath = "rom";
    std::string extraRomPath;  // --rom2: the extra set (game.cfg's extra_set), if the game has one (default: beside --rom)
    std::string eepromPath;    // the board's settings memory (created when missing)
    std::string moviePlay;     // input movie to replay
    std::string movieRecord;   // input movie to write
    std::string replayPlay;    // --replay FILE: a match replay (replay.h) played from the first frame
    std::string shotPrefix;    // PNG prefix for --shot-every
    std::string hashLog;       // per-frame state hash log
    std::string recordEntries; // entries file to add to (tools/recomp.py reads it)
    std::string dumpProgram;   // write the deciphered program here and stop
    std::string audioDump;     // WAV file of the sound (headless)
    std::string soundtrack;    // --soundtrack DIR: the custom soundtrack's folder (soundtrack.h; default: beside settings.ini)
    std::string soundtrackDump; // --soundtrack-dump FILE: WAV file of what the custom soundtrack plays (headless)
    std::string settingsPath;  // settings.ini with the player's feature choices
    std::string features;      // --features A,B,-C
    std::string menuTest;      // --menu FRAME:KEYS[:shot.png]
    std::string keys;          // --keys TICK:KEY,TICK:KEY...
    std::vector<std::string> dumpRam; // --dump-ram FRAME:file (as many as given)
    std::string poke;                 // --poke ADDR=BYTE,...
    uint32_t watchLow = 1, watchHigh = 0; // --watch LOW[-HIGH]
    std::string trace;         // --trace LOW-HIGH:file
    bool checkRewrites = false, noRewrites = false;
    std::string cpu;           // native | blocks | interp | check (default: native when there is translated code)
    std::string sound;         // driver | z80 | check (default: driver when the game's sound program is a known one)
    std::string stateSave, stateLoad; // FRAME:path
    uint64_t frames = 0;       // stop after this many (0: run on)
    uint64_t shotEvery = 0;
    uint64_t autoSeed = 0;     // --auto: made-up input from this seed
    uint64_t soundTest = 0;    // --sound-test SEED[:EVERY]: every song and command sent to the sound side
    unsigned musicTest = 0;    // --music-test FRAMES: each piece of music on the board in turn
    unsigned soundTestEvery = 30;
    uint64_t autoFrom = 0;     // ... from this frame on
    int autoPlayers = 2;       // ... for this many players
    uint64_t autoUntil = ~0ull; // ... until this frame (then no buttons held: the players idle)
    uint64_t testFrom = 0, testTo = 0; // --test-switch FROM:TO: the operator's test switch held over these frames
    bool autoPlay = false;
    bool headless = false, mute = false, unthrottled = false, fullscreen = false, stats = false;
    bool hashPicture = false; // --hash-log hashes the picture only
    bool hashNoPicture = false; // --hash-no-picture: the state hash without the picture
    bool hashNoSound = false;   // --hash-no-sound: ... without the sound side (as online play's)
    bool hashGame = false;     // --hash-game: the state hash without the Z80 and its own memory (to compare --sound driver and z80)
    int cpuSpeed = 1;         // --cpu-speed
    std::string renderer;     // --renderer sdl|gpu (gpu unless said)
    bool board = false; // --board: the whole board model: the picture by its renderer (renderer.cpp), not from the native
                        // interface, and the sound by its mixer (pcm.cpp), not the native mixer
    std::string shader;       // --shader FILE.slangp: that shader preset, whatever the Display page's Shader says (shading.h)
    std::string presentTest;  // --present-test FILE.png: the last picture drawn by the GPU path with no window
    int presentWidth = 0, presentHeight = 0; // --present-size WxH: the window it is drawn in
    uint64_t presentEvery = 0;  // --present-every N:PREFIX: ... every N frames (the zoom followed every frame)
    std::string presentPrefix;
    int scale = 0;            // --scale: the window this many times the board's lines (0: the Display page's)
    int zoom = 0, zoomPlayer = 0; // --zoom SIZE:PLAYER: the camera's zoom from the start (zoom.h; SIZE 0-4, PLAYER 1 or 2, 3 both)
    uint64_t rollbackTest = 0;  // --rollback-test SEED: each frame run as online play's rollback would (RollbackTest)
    bool online = false;        // --host or --join: online play (netplay.h), as `net` says
    Netplay::Settings net;
    std::string netSay;         // --net-say [FRAME:]TEXT: (tests) the line said in the chat at FRAME of the match (120)
    uint64_t netSayAt = 120;
    std::string netTyping;      // --net-typing TEXT: (pictures) a line shown as being typed, all through the match
};

// Recorded input: what the players hold, at the frames where it changes
// (each line: the frame, then each player's; two players unless a movie has more)
struct Movie {
    using Pads = std::array<uint16_t, cps3recomp::Machine::kMaxPlayers>;
    std::map<uint64_t, Pads> changes;
    bool Load(const std::string &path);
    bool Save(const std::string &path) const;
};

// --rollback-test: what online play's rollback does to the game (issue #70), with no network. Player 1's controls
// are this side's; the others' come from the other side late, by a lag that changes at random (0 to kMostLag
// frames). Until they come they are guessed (as last known: what rollback sessions guess), and when they come
// and a guess was wrong, the machine and the game's sources are put back to that frame and the frames since are
// run again. A run with it must go exactly as one without (--hash-log the same on every frame): anything the
// game keeps outside its save state, or that a frame run twice does twice, shows up as a difference.
struct RollbackTest {
    static constexpr int kMostLag = 8;
    static constexpr int kKept = 16; // (frames kept: more than the most a rollback goes back)
    struct Kept {
        std::vector<uint8_t> state; // the machine and the game's sources before the frame
        size_t sound = 0;           // the machine's sound before it
        Movie::Pads used{}, real{}; // the controls it was run with, and the real ones
        uint32_t hash = 0;          // (--hash-log: the state after it, as last run)
    };
    std::array<Kept, kKept> kept;
    uint64_t random = 0;
    uint64_t confirmed = 0; // the frames before this have had the others' real controls
    int lag = 0;
    uint64_t rollbacks = 0, rerun = 0, most = 0; // (for the report at the end)
    // (and what it cost: the frames, the keeping and the putting back, in seconds and how many)
    double stepTime = 0, keepTime = 0, putBackTime = 0;
    std::vector<float> frameTimes; // (each frame's, its rollback's frames and keeping included)
    uint64_t steps = 0, keeps = 0, putBacks = 0;
    size_t stateSize = 0;
};

struct Port {
    Options opt;
    cps3recomp::RomSet roms;
    std::unique_ptr<cps3recomp::Machine> machine;
    uint64_t frame = 0;       // frames run
    uint16_t pad[cps3recomp::Machine::kMaxPlayers] = {}; // what the players hold (cps3recomp::Button bits), set by the window
    bool service = false, test = false;
    Movie playMovie, recMovie;
    uint16_t lastRecorded[cps3recomp::Machine::kMaxPlayers] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    FILE *hashFile = nullptr;
    uint64_t autoState = 0;
    uint16_t autoPad[cps3recomp::Machine::kMaxPlayers] = {};
    Menu menu;
    // Stopping, slowing and speeding up the game: a tick is one picture of the screen; the game runs a frame in
    // every tick, or so many frames in so many ticks (menu.GameRate()), or while stopped only when asked to step
    uint64_t tick = 0;
    int owed = 0;         // the game's frames due, in ticks of the rate (whole frames run at once)
    bool paused = false;
    int steps = 0;        // frames to go on by while stopped
    // In a Multi game run (runtime/game.h, MultiGame): held, the game runs kHurry frames a tick, unheard
    static constexpr int kHurry = 4;
    bool hurry = false;
    // The speed is said at the bottom for a while after it changes (as the zoom's label), not all the while it is not
    // full: the rate said last, and the screen's ticks it is said for yet
    static constexpr int kSpeedTicks = 150; // (two and a half seconds)
    Menu::Rate saidRate{1, 1};
    int speedTicks = 0;
    // F12 (issue #31): the picture as shown, readout and all (not the zoom's), saved as a PNG in the player's folder's
    // screenshots/; where it went is said at the bottom for a while
    bool Screenshot();
    std::string shotSaid;
    int shotTicks = 0;
    // One tick; true if the game ran (so that there is a new picture and sound)
    bool Tick();
    // The keys that work while playing: 'p' stops and starts, 'o' goes on by a frame, '-' and '=' change the
    // speed, 'k' does the game's command with the key `n` (runtime/game.h, kKeyRestart...), 'n' asks a
    // Multi game run for its next game ('h': its fast forward on and off, for tests; 'z' the zoom pressed for player
    // `n` + 1, 'm' its camera moved a step, `n` 1-4 left, right, up, down, for tests)
    void Key(char key, unsigned n = 0);
    void RunCommand(size_t index);
    // (rapid fire: how long each player has held each button)
    uint32_t rapidHeld[cps3recomp::Machine::kMaxPlayers][16] = {};
    void RapidFire();
    // The custom soundtrack (soundtrack.h): its files, played while the port's custom_soundtrack feature is on
    SoundtrackPlayer soundtrack;
    bool soundtrackOn = false;
    int soundtrackValue = 0; // (the feature's choice: 1 on, 2 on with the playlist shuffled)
    std::string SoundtrackFolder() const;
    void SoundtrackFollow(); // (each frame: the feature switched on or off in the menu)
    int coinFrames = 0;   // frames left of the menu's coin
    int start2Frames = 0; // ... and of its press of player 2's Start
    int testFrames = 0;   // ... and of its press of the test switch

    // What the menu asked for (it is closed if that went well)
    void MenuAct(Menu::Action action);
    bool quit = false; // the player chose the menu's "Quit game": the frontend stops
    // The game's picture at the size the menu is drawn at (4:3, three times the lines)
    void MenuPicture(std::vector<uint32_t> &pixels, uint32_t &width, uint32_t &height) const;
    // The picture with the readout over it (runtime/game.h, SetHud), as the window and screenshots show it; and
    // where on it the readout's panels went (none: not asked)
    void Shown(std::vector<uint32_t> &pixels, std::vector<SDL_Rect> *panels = nullptr) const;
    // The camera's zoom (zoom.h), the window's: a gamepad's left stick held and a shoulder pressed
    Zoom zoom;
    // The picture for the window, a step of the zoom on: as Shown, or zoomed (true), as `zoomed` says (present.h)
    bool Windowed(std::vector<uint32_t> &pixels, Zoomed &zoomed);
    // ... in two: the zoom's step and the readout's counts on (once a shown frame), then the picture made
    void WindowedStep();
    bool WindowedPicture(std::vector<uint32_t> &pixels, Zoomed &zoomed);
    // Whether Shown would add nothing to the machine's picture and the window would show it unzoomed: no readout, no
    // line at the bottom, nothing online. Then the GPU may draw the picture from the native interface
    // (Presenter::ShowNative) in place of the CPU's
    bool ShownPlain() const;
    // ... and what Shown would put over the picture, as an overlay the GPU puts over its own (NativeGpu::Overlay: empty
    // when nothing); zoomed (true), as Windowed zooms (the overlay rawAbove)
    bool WindowedOverlay(std::vector<uint32_t> &overlay, Zoomed &zoomed) const;
    // ... and the menu's picture's (MenuPicture with the menu drawn over it), the menu's picture's size
    void MenuOverlay(std::vector<uint32_t> &overlay, uint32_t &width, uint32_t &height) const;
    // What Shown puts over a picture (the readout, the lines at the bottom, online's names and chat)
    void ShownOver(std::vector<uint32_t> &pixels, std::vector<SDL_Rect> *panels = nullptr) const;
    mutable std::vector<uint32_t> m_probeBlack, m_probeWhite;
    // Set by the frontend: the picture goes to the window through SDL's GPU API with the native video's renderer
    // (Presenter::CanShowNative): each frame is then made for the GPU (Machine::gpuPicture) unless the menu is open
    bool gpuPresent = false;
    // How many pixels a line of it has (the machine's picture: wider when the game's sources ask)
    int ShownWidth() const {
        return machine->PictureWidth();
    }
    std::string StatePath(int slot) const;
    static uint32_t StateVersion(); // (a state's, as this build writes it)
    // Match replays (replay.h): a match kept as it is played (ReplayBefore and ReplayAfter around each of the port's
    // own frames; online, ReplayBegin as the match starts and ReplayConfirmed for each frame that has both sides' real
    // controls), ended and written (ReplayEnd); one played back (ReplayPlay; ReplayBefore then gives the frame its
    // controls), gone about in (ReplaySeek, ReplayControls each tick, ReplayRetry for the restart key) and stopped
    // (ReplayStop: the player's own game back as it was, or not). ReplayLine: what the bottom of the screen says.
    Replay replay;
    std::string ReplayFolder() const;
    void ReplayBegin(bool online);
    void ReplayEnd(bool keep = true);
    void ReplayConfirmed(uint64_t at, const uint16_t *pads);
    void ReplayBefore(uint16_t *pressed);
    void ReplayAfter();
    bool ReplayPlay(const std::string &path, std::string &error);
    void ReplayStop(bool putBack);
    void ReplaySeek(int64_t to);
    bool ReplayRetry();
    void ReplayControls();
    std::string ReplayLine(int which) const;
    std::vector<Replay::Entry> replayList; // (as the menu last listed them)
    std::string PlayerFolder() const; // (the one settings.ini and the saves are in, with a slash; "" if none)

    bool Init(std::string &error);
    // The shader preset to draw with (shading.h): --shader's, or the Display page's Shader's; "" for none
    std::string ShaderPreset() const;
    // One frame: input (window, movie or made up), the machine, the per-frame outputs
    void RunFrame();
    // The frame itself, with these controls: what makes the next frame from the last (all that online play runs
    // again in a rollback)
    void Step(const uint16_t *players);
    // The machine and the game's sources, kept in memory and put back (run-ahead's and rollback's)
    void KeepState(std::vector<uint8_t> &data, bool inMemory = false);
    void PutBackState(const std::vector<uint8_t> &data, bool inMemory = false);
    RollbackTest rollbackTest;
    void RollbackFrame(const uint16_t *real);
    // Online play (netplay.h): the other side found before the first frame, and each frame run through it
    std::unique_ptr<Netplay> net;
    bool connecting = false; // (the other side not found yet: a window shows the wait)
    bool leaving = false;    // (online: Esc asked whether to leave the match; said on the screen until answered)
    bool cameIn = false;
    // Online: each side's win streak (matches won in a row, the other player's against them), counted in the fights
    // both players have pressed Start for (fight/battle.h: the round's result, and the rounds a match is won with); said
    // under the life bars from two on. Each player's machine counts the same, both running the same game.
    bool otherIn = false;
    int streak[2] = {}, roundsWon[2] = {};
    bool roundCounted = false;
    int roundQuiet = 0;
    void Streaks();     // (this side's player has pressed Start in the match: "PRESS START TO JOIN" is said until then)
    // Online: the chat (issue #196). Each line is said on the picture for a while (`until`, a tick), the newest
    // lowest; and the line this side's player is typing, while they type one (the frontend's: Enter sends it, Esc
    // puts it away; the keyboard is the chat's meanwhile, not the game's)
    struct ChatLine {
        std::string text;
        uint64_t until;
        bool own;
    };
    std::deque<ChatLine> chat;
    bool typing = false;
    std::string typed;
    uint64_t chatHintUntil = 0; // (how to chat, said under the chat for a while as a match begins)
    void Say();       // (the typed line: sent, and said with the rest)
    void HearChat();  // (the other side's lines that have come)
    bool StartOnline(std::string &error);
    // The in-game menu's Online page (issue #196): a match hosted while the game goes on as the player's own, offline
    // (its modes, cheats, the menu, save states, pause: all as without it), `waitingHost` listening meanwhile (PumpHost:
    // every tick, and while the menu is open), listed in the lobby under the launcher's name if the player has one.
    // When someone comes (MatchComing, from Netplay::Waiting) the game starts from the board's reset with the match's
    // features, for both players alike. The menu's row again stops hosting (StopHosting). After the match the player's
    // own game and features are back (BackToSolo): still hosted with the same code when the other player left, not
    // when this one did.
    std::unique_ptr<Netplay> waitingHost;
    void HostFromMenu();
    void PumpHost();
    void MatchComing();
    void StopHosting();
    void BackToSolo(bool stillHosting);
    bool hostedFromMenu = false;
    std::map<std::string, int> ownFeatures; // (as they were before the match's: put back by BackToSolo)
    std::string hostNote;                   // (why the hosting stopped, said for a few seconds: until hostNoteUntil)
    uint64_t hostNoteUntil = 0;
    // Esc's question answered yes: the match left, and the game with it unless it was hosted from the menu
    void LeaveOnline();
    void Matched();
    void ArcadeFrame(); // (--net-arcade: the host's own play while it waits for the other player)
    // A window's match that ended with the other side gone: the host waits for them to come back (the same code, or
    // its address), the joining side goes to the host again; the game starts over from the board's reset, the host's
    // sent whole to the other (Netplay's state). `rejoin` says why, while it is waited for.
    void Again(const std::string &why);
    std::string rejoin;
    uint64_t againAt = 0; // (joining: the tick to try again at)
    void RunAhead();
    bool RunsAhead() const; // (whether RunAhead runs frames ahead now)
    void Finish();
    uint32_t StateHash() const;
    bool SaveState(const std::string &path, std::string &error);
    bool LoadState(const std::string &path, std::string &error);
};

int RunWindowed(Port &port);
// The collection: one launcher for every game (launcher/CMakeLists.txt, COLLECTION), its program beside the games'
// and the name of its folder (its launcher.ini; each game keeps its own folder, named by the game)
inline constexpr const char *kCollectionLauncher = "sf3comp-launcher";
inline constexpr const char *kCollectionName = "SF3comp";
// The game is one of the collection's (sf3comp-launcher is beside it)
bool InCollection();
// The optional features as the launcher lists them (--list-features)
std::string LauncherFeatureList();
// The launcher's own folder (launcher.ini's), the collection's or the game's; empty if there is none
std::filesystem::path LauncherFolder();
#ifdef __ANDROID__
// Android (port/android.cpp): the game's folder and log, which its activity says, before anything else
void StartOnAndroid();
#endif

// The Display page's choices (the port's display_* and window_size features): how the picture is fitted to
// the window, and how many times the board's lines the window opens
Scaling DisplayScaling();
Shape DisplayShape();
int WindowSize();

} // namespace port
