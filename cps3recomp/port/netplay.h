// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// Online play (issue #70): two copies of the game, one on each player's machine, each running the whole game with
// both players' controls. Each side sends the other its own player's controls, frame by frame, over UDP. A frame
// does not wait for the other side's: until they come they are guessed (as they last were), and when they come and
// a guess was wrong, the game is put back to that frame and the frames since are run again with the real ones
// (rollback, as --rollback-test tries it: port.h, RollbackTest). Neither side is ahead of the other's controls by
// more than kMostAhead frames: past that, it waits.
//
// --host [PORT] waits for the other player; --join ADDRESS[:PORT] goes to them. The host is player 1. Before the
// first frame the two make sure they are the same game, and the joining side takes the host's board settings
// (the settings memory) and the features that change play (Game and Board, and widescreen); the cheats and the rest of
// the alternative modes, training, run-ahead and the like stay off. Every so often each sends the other a hash of
// the game as it was at a frame both have the real controls for; if the two differ, the games have come apart
// (a desync), and the match stops, saying so.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace port {

struct Port;

// A UDP socket, IPv6 and IPv4 both; the other side's address
struct NetAddress {
    std::array<uint8_t, 128> storage{}; // (a sockaddr_storage)
    int length = 0;
    // (an IPv4 address: as IPv4 too, for a socket that has no IPv6)
    std::array<uint8_t, 16> four{};
    bool hasFour = false;
    bool operator==(const NetAddress &other) const {
        return length == other.length && storage == other.storage;
    }
};
class UdpSocket {
public:
    ~UdpSocket();
    bool Open(int port, std::string &error); // (0: any port)
    int LocalPort() const; // (the port it was given)
    static bool Resolve(const std::string &host, int port, NetAddress &address, std::string &error, bool fourOnly = false);
    // "ADDRESS", "ADDRESS:PORT", an IPv6 address, or "[IPv6]:PORT": the address and the port (`port` unless said)
    static void Split(const std::string &text, std::string &host, int &port);
    void Send(const NetAddress &to, const uint8_t *data, size_t size);
    // A packet that has come (its size), or -1 for none yet
    int Receive(uint8_t *data, size_t size, NetAddress &from);

private:
    intptr_t handle = -1;
    bool six = false; // (IPv6, and IPv4 through it)
};

class Netplay {
public:
    static constexpr int kDefaultPort = 7845;
    // The packets' layout (2: the host's state sent at the start); two copies must have the same to play, and the
    // lobby shows only matches of this one (the launcher asks with it too)
    static constexpr int kProtocol = 6; // (3: the version said in the hello and to the service; 4: the welcome's flags;
                                        // 5: the welcome's program words, so two regions' sets play together;
                                        // 6: each player's name in the hello and the welcome, and the chat)
    // The number said to the introduction service for kProtocol, which lists a lobby's matches and a waiting room's
    // players by it: this toolkit's own (3000 and up), so that its lobby and waiting room are its games' alone and
    // not cps2recomp's (until 2026-11-12 the two shared cps2recomp's service; now this collection has its own, online/)
    static constexpr int kLobbyProtocol = 3000 + kProtocol;
    static constexpr int kMostAhead = 8;   // frames run on guessed controls at most
    static constexpr int kKept = 16;       // states kept (more than the most a rollback goes back)
    static constexpr int kInputs = 128;    // controls kept, each side's
    static constexpr int kHashEvery = 60;  // frames between the hashes compared
    static constexpr int kDefaultDelay = 2; // frames a player's own controls are held back (fewer rollbacks)
    // The introduction service (online/): a host gets a code from it, and a player joining by the code is told where
    // the host is, and the host where they are; the two then reach each other directly
    static constexpr const char *kService = "https://sf3comp-online.tmcleroy.workers.dev";

    // What --host, --join and the --net-* options asked for
    struct Settings {
        bool hosting = false;
        std::string address; // (joining: the host's)
        int port = kDefaultPort;
        int delay = kDefaultDelay;
        std::string service = kService; // (--net-service URL; "off": none, the host's address only)
        std::string renewCode, renewSecret; // (hosting again after the other player left: the code they know, kept)
        bool listed = false;  // (--net-list) the match in the lobby: on the Online card's list of open matches
        std::string name;     // (--net-name) ... under this name
        // (--net-invite TO:FROM, --net-invite-name NAME) hosting: a player in the launcher's waiting room (online/)
        // invited to the match, by their id, from this player's (named `name`); the invited player's name, as said
        std::string invite, from, inviteName;
        // (--net-arcade) hosting: the game played meanwhile, alone, as offline (with the match's features); the match
        // begins from the game as it is when the other player comes, who presses Start to challenge
        bool arcade = false;
        // (the in-game menu's Host a match) hosting while the game is the player's own, offline, features and all;
        // when someone comes the match begins from the board's reset with the match's (Port::MatchComing)
        bool solo = false;
        // (tests: each packet sent late by this many ms, give or take `jitter`, and this many in a hundred lost)
        int fakeLag = 0, fakeJitter = 0, fakeLoss = 0;
    };
    explicit Netplay(const Settings &settings) : settings(settings) {}
    // The socket, the other side found and agreed with (blocks until then; false and `error` if it did not go)
    bool Connect(Port &port, std::string &error);
    // ... or a step at a time, for a window that shows the wait: Begin, then Waiting each tick until it is not 0 (1:
    // the match is on; -1: it did not go, as `error` says)
    bool Begin(Port &port, std::string &error);
    int Waiting(Port &port, std::string &error);
    std::string waiting; // what is being waited for ("Waiting for the other player...")
    std::string code;    // (hosting) the match's code, once the service has given one, as said ("K7Q-M2X")
    const std::string &Secret() const {
        return secret;
    }
    bool Hosting() const {
        return settings.hosting;
    }
    bool Listed() const {
        return settings.listed;
    }
    // (hosting, --net-invite) the invited player, by name, and what came of it: "" (waiting for them), "declined", or
    // "away" (they couldn't be given it: gone from the waiting room)
    const std::string &Invited() const {
        return settings.inviteName;
    }
    const std::string &Invitation() const {
        return invitation;
    }
    bool Arcade() const {
        return settings.hosting && settings.arcade;
    }
    // (hosting) someone has come, and is being sent the game: the game waits for them now
    bool Found() const {
        return found;
    }
    // (joining) the host's player has already pressed Start: the host was playing while it waited (--net-arcade)
    bool HostIn() const {
        return hostIn;
    }
    // The time a packet takes there and back (ms; the frame's wait for it to be read taken off), and how long since the
    // other side was last heard
    int Ping() const;
    int Delay() const {
        return delay;
    }
    uint64_t Silent() const;
    // This side's player (0: player 1)
    int Local() const {
        return local;
    }
    // Each tick: what has come in, sent again what has not been taken; false if the next frame must wait (the
    // other side's controls are too far behind, or this side is ahead of the other in time)
    bool Ready(uint64_t frame);
    // The frame, with this side's player's controls: rollback first if a guess was wrong, then the frame itself
    void Frame(Port &port, uint16_t controls);
    // Why the match is over ("" while it goes on); and whether that is a failure (a desync, the other side lost)
    std::string ended;
    bool failed = false;
    bool left = false, lost = false; // (... because the other side left, or went silent: a window waits for them again)
    // The match is over on this side: the other side is told
    void Leave();
    // A line on how it is going (ping, rollbacks), every few seconds, to stderr
    void Report(bool last = false);
    // The two players' names (--net-name; the other's from the hello or the welcome), as the screen shows them: at
    // most kNameMost letters ("" for none given)
    static constexpr size_t kNameMost = 16;
    std::string Name(int player) const;
    // The chat (issue #196): a line said to the other player, straight to them (never through the service), sent
    // again until they have it; and the lines that have come from them since this was last asked, in order
    static constexpr size_t kChatMost = 60;
    void Say(const std::string &text);
    std::vector<std::string> TakeHeard();

private:
    Settings settings;
    UdpSocket socket;
    // (the handshake: the hello or the welcome, sent again until answered)
    std::vector<uint8_t> greeting;
    uint32_t build = 0;
    uint64_t began = 0, sent = 0;
    bool found = false; // (hosting: the other player has said hello)
    bool hostIn = false;
    void Welcome(Port &port); // (hosting: the welcome, and the game as it is, for the one who has come)
    int joinPort = 0;
    void StartAt(int frames);
    // The game as the match begins: the host's, sent to the joining side in pieces (compressed), which it takes as
    // its own; so the two begin exactly alike, the first time and after a reconnect
    static constexpr size_t kChunk = 1024;
    std::vector<uint8_t> state;   // (compressed)
    size_t stateSize = 0;         // (as it was)
    uint32_t chunks = 0, chunksHad = 0; // its pieces, and how many the other side has (hosting) or this side has (joining)
    uint64_t chunkSent = 0;
    bool welcomed = false;        // (joining: the welcome has come, the state is coming)
    std::vector<bool> had;        // (joining: the pieces that have come)
    // The introduction (online/): this side's address as the internet sees it (asked of STUN servers, from the
    // socket the match uses), then the service asked for a code (hosting) or the host's addresses (joining by code),
    // then (hosting) asked until someone has joined; then each side sends to the other's addresses (a host's
    // packets open the way through its router for the other's hello)
    struct HttpAnswer {
        int status = 0; // (0: no answer)
        std::string body;
    };
    enum class Intro { Off, Stun, Asking, Polling, Done } intro = Intro::Off;
    std::string joinCode; // (joining by a code: it, as typed)
    std::future<std::vector<NetAddress>> stunLookup;
    std::vector<NetAddress> stunServers;
    std::array<uint8_t, 12> stunId{};
    uint64_t stunBegan = 0, stunSent = 0;
    std::string seenAs; // ("IP:PORT")
    std::future<HttpAnswer> asking;
    uint64_t askedAt = 0;
    uint64_t codeAt = 0; // (hosting: when the code was had, for how often it is asked about: Introduce)
    std::string secret;
    std::string invitation; // (hosting, --net-invite) what the service said came of it: Invitation()
    std::vector<NetAddress> candidates; // the other side's addresses
    uint64_t punchedAt = 0;
    bool Introduce(uint64_t now, std::string &error);
    bool TakeStun(const uint8_t *data, int size);
    std::string Addresses() const; // (this side's, as JSON)
    NetAddress peer;
    int local = 0, remote = 1;
    int delay = kDefaultDelay;
    std::array<uint16_t, kInputs> localInput{}, remoteInput{};
    uint64_t localUpTo = 0;  // this side's controls are known for the frames before this
    uint64_t remoteUpTo = 0; // ... and the other side's (all of them before it)
    uint64_t remoteAck = 0;  // the other side has this side's controls for the frames before this
    uint64_t remoteFrame = 0; // the frame the other side was on, last heard
    struct Kept {
        std::vector<uint8_t> state; // the game before the frame
        uint16_t used = 0;          // the other side's controls it was run with
        uint32_t hash = 0;          // the game after it, last run (when hashed)
    };
    std::array<Kept, kKept> kept;
    uint64_t confirmed = 0;  // the frames before this were run with the other side's real controls
    uint64_t current = 0;    // the frame running now
    std::map<uint64_t, uint32_t> ourHashes, theirHashes;
    uint64_t lastHashFrame = 0; uint32_t lastHash = 0; // (ours, sent with every packet)
    // Keeping time with the other side: how far ahead of it this side is (frames, averaged), and its own idea
    double ahead = 0, theirAhead = 0;
    int waitTicks = 0;
    uint64_t waitedAt = UINT64_MAX;
    // (ping)
    uint32_t echo = 0;       // the other side's time on its last packet
    uint64_t echoAt = 0;     // ... and when it came, ours
    double ping = 0;
    uint64_t lastHeard = 0;  // (ms)
    // (for the report)
    uint64_t rollbacks = 0, rerun = 0, waits = 0, lastReport = 0, reportedRollbacks = 0;
    int mostRerun = 0;
    double worst = 0;  // the longest a frame took, its rollback with it (seconds)
    uint64_t over = 0; // ... and how many took longer than a frame's time

    // (tests: packets held back, and when they go)
    struct Late { uint64_t due; NetAddress to; std::vector<uint8_t> data; };
    std::deque<Late> late;
    uint64_t random = 0x2545F4914F6CDD1Dull;

    std::string theirName; // (the other player's, as it came)
    // (the chat: the lines said that the other side hasn't said it has, by number, and when they were last sent; the
    // number of lines heard, all before it, and those not taken yet)
    std::deque<std::pair<uint32_t, std::string>> unheard;
    uint32_t said = 0, heardUpTo = 0;
    uint64_t saidAt = 0;
    std::vector<std::string> heard;
    void SendChat(uint64_t now);

    void Send(const std::vector<uint8_t> &packet);
    void SendTo(const NetAddress &to, const std::vector<uint8_t> &packet);
    void SendInputs();
    void Pump();
    void Take(const uint8_t *data, size_t size);
    void Run(Port &port, uint64_t at, bool drawing);
    void Compare();
};

} // namespace port
