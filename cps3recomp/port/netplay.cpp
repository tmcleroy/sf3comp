// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// Online play: the socket, the handshake, and rollback over the network (netplay.h)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "netplay.h"

#include "game_info.h"
#include "../recomp-common/launcher/src/online.h"
#include "../recomp-common/launcher/src/run_program.h"
#include "../runtime/game.h"
#include "port.h"

#include <SDL3/SDL.h>

#include <zlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <thread>

using namespace cps3recomp;

namespace port {

namespace {
    uint64_t NowMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // The packets: "C2NP", what kind, then the kind's fields, little-endian
    constexpr uint8_t kMagic[4] = {'C', '2', 'N', 'P'};
    constexpr uint16_t kProtocol = Netplay::kProtocol;
    enum Kind : uint8_t {
        kHello = 1,   // joining -> host: the protocol, the game and its build
        kWelcome = 2, // host -> joining: the delay, the board settings, the features that change play
        kRefuse = 3,  // either: why not (and the match is over)
        kControls = 4, // either: controls, and the rest of what each frame says (Netplay::SendInputs)
        kBye = 5,     // either: this side has left
        kPunch = 6,   // host -> joining, before the hello: nothing, but it opens the host's router to them
        kPiece = 7,   // host -> joining: a piece of the game's state as the match begins (Netplay::state)
        kPieceAck = 8, // joining -> host: how many pieces have come, all before that one
        kChat = 9,    // either: a line of the chat, by its number
        kChatAck = 10, // either: how many lines of the chat have come, all before that one
    };

    // A name or a line of the chat as the screen's font has it (ASCII's letters, digits and marks), at most `most` of
    // them, spaces at its ends taken off
    std::string Printable(const std::string &text, size_t most) {
        std::string out;
        for (const char c : text) {
            if (out.size() < most && c >= 32 && c < 127) {
                out += c;
            }
        }
        const size_t start = out.find_first_not_of(' ');
        return start == std::string::npos ? std::string() : out.substr(start, out.find_last_not_of(' ') - start + 1);
    }

    struct Writer {
        std::vector<uint8_t> out;
        explicit Writer(Kind kind) {
            out.assign(kMagic, kMagic + 4);
            out.push_back(kind);
        }
        void U8(uint8_t v) { out.push_back(v); }
        void U16(uint16_t v) { U8(static_cast<uint8_t>(v)); U8(static_cast<uint8_t>(v >> 8)); }
        void U32(uint32_t v) { U16(static_cast<uint16_t>(v)); U16(static_cast<uint16_t>(v >> 16)); }
        void Text(const std::string &s) {
            U16(static_cast<uint16_t>(s.size()));
            out.insert(out.end(), s.begin(), s.end());
        }
        void Bytes(const void *p, size_t n) {
            U16(static_cast<uint16_t>(n));
            out.insert(out.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
        }
    };
    struct Reader {
        const uint8_t *at, *end;
        bool ok = true;
        Reader(const uint8_t *data, size_t size, size_t from = 5) : at(data + from), end(data + size) {} // (after the magic and kind)
        bool Has(size_t n) {
            ok = ok && static_cast<size_t>(end - at) >= n;
            return ok;
        }
        uint8_t U8() { return Has(1) ? *at++ : 0; }
        uint16_t U16() { const uint16_t lo = U8(); return static_cast<uint16_t>(lo | U8() << 8); }
        uint32_t U32() { const uint32_t lo = U16(); return lo | static_cast<uint32_t>(U16()) << 16; }
        std::string Text() {
            const size_t n = U16();
            if (!Has(n)) {
                return "";
            }
            std::string s(reinterpret_cast<const char *>(at), n);
            at += n;
            return s;
        }
    };

    // Which features the joining side takes from the host: those that change how the game plays. Each player's
    // own: how the picture is shown and the sound. Off for both: the cheats (a fair match), and the rest (the
    // alternative modes, training, the viewers: kept apart from the save state, or not worked out for two machines
    // yet), and run-ahead (rollback does what it does).
    enum class Share { Host, Own, Off, Keep };
    Share Shared(const Feature &feature) {
        const std::string category = feature.category, name = feature.name;
        if (name == "run_ahead" || name == "custom_colours") {
            return Share::Off; // (custom colours: each player's own library, which the other side has not)
        }
        if (name.rfind("widescreen", 0) == 0 || category == "Game" || category == "Board") {
            return Share::Host;
        }
        if (category == "Cheats") {
            return Share::Off; // (a fair match: no damage for the host's player, say, is not)
        }
        if (category == "Display" || category == "Sound" || category == "Online") {
            return Share::Own; // (how each player sees and hears it, and what their screen says of the match)
        }
        if (feature.flags & kFeatureOwn) {
            return Share::Keep; // (a record or a choice kept for a mode: left as it is, and the mode is off)
        }
        return Share::Off;
    }

    // What both sides must have the same of: the game as this build is made (the set its code was translated from)
    // and the sound program. The program itself may be another region's set's (game.cfg's other_set), which differs
    // only in its online_words: the host's are sent in the welcome, so the match is played as the host's region
    uint32_t BuildHash(const Port &port) {
        uLong h = crc32(0, nullptr, 0);
        h = crc32(h, reinterpret_cast<const Bytef *>(GAME_BIOS), static_cast<uInt>(strlen(GAME_BIOS)));
        h = crc32(h, reinterpret_cast<const Bytef *>(GAME_SIMM1), static_cast<uInt>(strlen(GAME_SIMM1)));
        h = crc32(h, reinterpret_cast<const Bytef *>(GAME_SIMM2), static_cast<uInt>(strlen(GAME_SIMM2)));
        return static_cast<uint32_t>(h);
    }
#ifndef GAME_ONLINE_WORDS
#define GAME_ONLINE_WORDS ""
#endif
    // The program's words two sets of the game have differently (game.cfg's online_words: the region, the checksum
    // and the like), in both views (the rest of a set's program differs only where the game never reads it: the
    // cipher's other view)
    std::vector<uint32_t> OnlineWords() {
        std::vector<uint32_t> words;
        for (const char *at = GAME_ONLINE_WORDS; *at;) {
            char *end;
            const unsigned long word = strtoul(at, &end, 16);
            if (end == at) {
                at++; // (" || " between the lines of them)
                continue;
            }
            words.push_back(static_cast<uint32_t>(word));
            at = end;
        }
        return words;
    }
    std::string SetName(const Port &port) {
        (void)port;
        return GAME_ROM_SET;
    }
    // A match's code as the service keeps it ("K7QM2X"), from what was typed ("k7q-m2x"), or "" if it is not one
    // (an address has a dot or a colon)
    std::string CodeOf(const std::string &text) {
        static const std::string kAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"; // (online/src/index.js)
        if (text.find_first_of(".:") != std::string::npos) {
            return "";
        }
        std::string code;
        for (const char c : text) {
            if (c == '-' || c == ' ') {
                continue;
            }
            const char upper = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            if (kAlphabet.find(upper) == std::string::npos) {
                return "";
            }
            code += upper;
        }
        return code.size() == 6 ? code : "";
    }
    // ... as said to the player: "K7Q-M2X"
    std::string Spaced(const std::string &code) {
        return code.size() == 6 ? code.substr(0, 3) + "-" + code.substr(3) : code;
    }

    // The service is asked with the system's curl (as the launcher's updates are fetched: macOS, Windows 10 and
    // later, and Linux have it; Android, through the game's activity), on another thread; the answer's last line is
    // its HTTP status
    std::string CurlProgram() {
#ifdef _WIN32
        char system[MAX_PATH];
        const UINT n = GetSystemDirectoryA(system, MAX_PATH);
        return n > 0 && n < MAX_PATH ? std::string(system) + "\\curl.exe" : "curl.exe";
#else
        std::error_code error;
        return std::filesystem::exists("/usr/bin/curl", error) ? "/usr/bin/curl" : "curl";
#endif
    }

    // A text field of a JSON answer ("" if none), a list of texts, and whether a field is true (the service's
    // answers are small and of known shapes: online/src/index.js)
    std::string JsonText(const std::string &json, const std::string &key) {
        const size_t at = json.find("\"" + key + "\":\"");
        if (at == std::string::npos) {
            return "";
        }
        std::string out;
        for (size_t i = at + key.size() + 4; i < json.size() && json[i] != '"'; i++) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                i++;
            }
            out += json[i];
        }
        return out;
    }
    std::vector<std::string> JsonList(const std::string &json, const std::string &key) {
        std::vector<std::string> out;
        const size_t at = json.find("\"" + key + "\":[");
        if (at == std::string::npos) {
            return out;
        }
        const size_t end = json.find(']', at);
        for (size_t i = json.find('"', at + key.size() + 3); i < end && i != std::string::npos;) {
            const size_t close = json.find('"', i + 1);
            if (close == std::string::npos || close > end) {
                break;
            }
            out.push_back(json.substr(i + 1, close - i - 1));
            i = json.find('"', close + 1);
        }
        return out;
    }
    bool JsonTrue(const std::string &json, const std::string &key) {
        return json.find("\"" + key + "\":true") != std::string::npos;
    }

    // The STUN binding request (RFC 5389): what address and port a server sees this socket's packets come from
    constexpr uint32_t kStunCookie = 0x2112A442;
} // namespace

// The socket

#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#define closesocket close
#endif

UdpSocket::~UdpSocket() {
    if (handle != -1) {
        closesocket(static_cast<SocketHandle>(handle));
    }
}

bool UdpSocket::Open(int port, std::string &error) {
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "cannot start Windows sockets";
            return false;
        }
        started = true;
    }
#endif
    // (IPv6 with IPv4 too, where the system has it; or IPv4)
    SocketHandle s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    six = true;
    if (s != static_cast<SocketHandle>(-1)) {
        int off = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&off), sizeof(off));
        sockaddr_in6 any{};
        any.sin6_family = AF_INET6;
        any.sin6_port = htons(static_cast<uint16_t>(port));
        any.sin6_addr = in6addr_any;
        if (bind(s, reinterpret_cast<sockaddr *>(&any), sizeof(any)) != 0) {
            closesocket(s);
            s = static_cast<SocketHandle>(-1);
        }
    }
    if (s == static_cast<SocketHandle>(-1)) {
        six = false;
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in any{};
        any.sin_family = AF_INET;
        any.sin_port = htons(static_cast<uint16_t>(port));
        any.sin_addr.s_addr = htonl(INADDR_ANY);
        if (s == static_cast<SocketHandle>(-1) || bind(s, reinterpret_cast<sockaddr *>(&any), sizeof(any)) != 0) {
            error = "cannot use UDP port " + std::to_string(port) + " (is another copy of the game using it?)";
            if (s != static_cast<SocketHandle>(-1)) {
                closesocket(s);
            }
            return false;
        }
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    handle = static_cast<intptr_t>(s);
    return true;
}

bool UdpSocket::Resolve(const std::string &host, int port, NetAddress &address, std::string &error, bool fourOnly) {
    addrinfo hints{}, *found = nullptr;
    hints.ai_family = fourOnly ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "cannot find " + host;
        return false;
    }
    // (an IPv4 address as IPv6 sees it, for the socket that is both)
    const addrinfo *use = found;
    if (use->ai_family == AF_INET) {
        sockaddr_in6 mapped{};
        mapped.sin6_family = AF_INET6;
        mapped.sin6_port = reinterpret_cast<const sockaddr_in *>(use->ai_addr)->sin_port;
        mapped.sin6_addr.s6_addr[10] = 0xFF;
        mapped.sin6_addr.s6_addr[11] = 0xFF;
        memcpy(&mapped.sin6_addr.s6_addr[12], &reinterpret_cast<const sockaddr_in *>(use->ai_addr)->sin_addr, 4);
        memcpy(address.storage.data(), &mapped, sizeof(mapped));
        address.length = sizeof(mapped);
        memcpy(address.four.data(), use->ai_addr, sizeof(sockaddr_in));
        address.hasFour = true;
    } else {
        memcpy(address.storage.data(), use->ai_addr, use->ai_addrlen);
        address.length = static_cast<int>(use->ai_addrlen);
    }
    freeaddrinfo(found);
    return true;
}

void UdpSocket::Split(const std::string &text, std::string &host, int &port) {
    host = text;
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon) { // (ADDRESS:PORT; an IPv6 address alone has several)
        port = atoi(host.c_str() + colon + 1);
        host = host.substr(0, colon);
    } else if (host.size() > 2 && host[0] == '[') { // ([IPv6]:PORT)
        const size_t close = host.find(']');
        if (close != std::string::npos && close + 1 < host.size() && host[close + 1] == ':') {
            port = atoi(host.c_str() + close + 2);
        }
        host = host.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    }
}

int UdpSocket::LocalPort() const {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (getsockname(static_cast<SocketHandle>(handle), reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return 0;
    }
    return ntohs(address.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6 *>(&address)->sin6_port
                                               : reinterpret_cast<sockaddr_in *>(&address)->sin_port);
}

void UdpSocket::Send(const NetAddress &to, const uint8_t *data, size_t size) {
    const bool asFour = !six && to.hasFour;
    sendto(static_cast<SocketHandle>(handle), reinterpret_cast<const char *>(data), static_cast<int>(size), 0,
           reinterpret_cast<const sockaddr *>(asFour ? to.four.data() : to.storage.data()),
           static_cast<socklen_t>(asFour ? sizeof(sockaddr_in) : to.length));
}

int UdpSocket::Receive(uint8_t *data, size_t size, NetAddress &from) {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    const int got = static_cast<int>(recvfrom(static_cast<SocketHandle>(handle), reinterpret_cast<char *>(data), static_cast<int>(size), 0,
                                              reinterpret_cast<sockaddr *>(&address), &length));
    if (got < 0) {
        return -1;
    }
    from = NetAddress{};
    memcpy(from.storage.data(), &address, std::min<size_t>(length, sizeof(from.storage)));
    from.length = static_cast<int>(length);
    return got;
}

// The session

void Netplay::Send(const std::vector<uint8_t> &packet) {
    SendTo(peer, packet);
}

void Netplay::SendTo(const NetAddress &to, const std::vector<uint8_t> &packet) {
    if (settings.fakeLoss && static_cast<int>((random = random * 6364136223846793005ull + 1442695040888963407ull) >> 33) % 100 < settings.fakeLoss) {
        return;
    }
    if (settings.fakeLag || settings.fakeJitter) {
        random = random * 6364136223846793005ull + 1442695040888963407ull;
        const int jitter = settings.fakeJitter ? static_cast<int>((random >> 33) % (2 * settings.fakeJitter + 1)) - settings.fakeJitter : 0;
        late.push_back({NowMs() + static_cast<uint64_t>(std::max(0, settings.fakeLag + jitter)), to, packet});
        return;
    }
    socket.Send(to, packet.data(), packet.size());
}

namespace {
    // The features as the match has them: the joining side takes the host's (Share::Host); both turn off the rest
    void Settle(bool hosting, const std::map<std::string, int> &host) {
        for (const Feature &feature : TheGame().features) {
            switch (Shared(feature)) {
            case Share::Host:
                if (!hosting) {
                    const auto it = host.find(feature.name);
                    SetFeatureForNow(feature.name, it == host.end() ? 0 : it->second);
                }
                break;
            case Share::Off: SetFeatureForNow(feature.name, 0); break;
            default: break;
            }
        }
    }
} // namespace

void Netplay::StartAt(int frames) {
    delay = std::clamp(frames, 0, kMostAhead); // (the first `delay` frames: nobody's controls)
    localUpTo = remoteUpTo = static_cast<uint64_t>(delay);
}

// The welcome for the one who has come: the delay, the board settings, the features that change play, and the game
// as it is now (the board's reset, or, with --net-arcade, wherever the host's play has got to)
void Netplay::Welcome(Port &port) {
    Writer welcome(kWelcome);
    welcome.U16(kProtocol);
    welcome.U8(static_cast<uint8_t>(delay));
    welcome.Bytes(port.machine->eeprom.words, sizeof(port.machine->eeprom.words));
    std::string features;
    for (const Feature &feature : TheGame().features) {
        if (Shared(feature) == Share::Host && FeatureSetting(feature.name)) {
            features += std::string(feature.name) + "=" + std::to_string(FeatureSetting(feature.name)) + ",";
        }
    }
    welcome.Text(features);
    // (the game as it is now: the state the match begins from, sent in pieces, after this side's program's words
    // where another set's may differ, which the other side takes: their count, then each one's address and both views)
    std::vector<uint8_t> raw;
    port.KeepState(raw);
    const std::vector<uint32_t> words = OnlineWords();
    Writer program(kWelcome);
    program.U16(static_cast<uint16_t>(words.size()));
    for (const uint32_t word : words) {
        // (the SIMMs' program as the CPU reads it: data and instructions alike)
        const size_t at = std::min<size_t>(word, port.roms.program.size() * 4 - 2);
        program.U32(word);
        const uint8_t b0 = static_cast<uint8_t>(port.roms.program[at / 4] >> ((3 - at % 4) * 8));
        const uint8_t b1 = static_cast<uint8_t>(port.roms.program[(at + 1) / 4] >> ((3 - (at + 1) % 4) * 8));
        for (const uint8_t b : {b0, b1, b0, b1}) {
            program.U8(b);
        }
    }
    raw.insert(raw.begin(), program.out.begin() + 5, program.out.end());
    uLongf packed = compressBound(static_cast<uLong>(raw.size()));
    state.resize(packed);
    compress2(state.data(), &packed, raw.data(), static_cast<uLong>(raw.size()), 6);
    state.resize(packed);
    stateSize = raw.size();
    chunks = static_cast<uint32_t>((state.size() + kChunk - 1) / kChunk);
    welcome.U32(static_cast<uint32_t>(stateSize));
    welcome.U32(static_cast<uint32_t>(state.size()));
    // (flags: 1, this side's player is already in the game, which was played while it waited)
    welcome.U8(port.cameIn ? 1 : 0);
    // (this side's set: the other side says so when it has another region's)
    welcome.Text(SetName(port));
    welcome.Text(settings.name);
    greeting = welcome.out;
}

bool Netplay::Begin(Port &port, std::string &error) {
    if (!socket.Open(settings.hosting ? settings.port : 0, error)) {
        return false;
    }
    local = settings.hosting ? 0 : 1;
    remote = 1 - local;
    build = BuildHash(port);
    began = NowMs();
    if (settings.hosting) {
        StartAt(settings.delay);
        if (!settings.solo) {
            Settle(true, {}); // (solo: when someone comes)
        }
        waiting = "Waiting for the other player (UDP port " + std::to_string(settings.port) + ")";
    } else {
        Writer hello(kHello);
        hello.U16(kProtocol);
        hello.Text(GAME_ID);
        hello.U32(build);
        hello.Text(RECOMP_VERSION);
        hello.Text(settings.name);
        greeting = hello.out;
        // A match's code (K7Q-M2X: six letters and digits), or the host's address
        joinCode = CodeOf(settings.address);
        if (!joinCode.empty()) {
            if (settings.service == "off" || settings.service.empty()) {
                error = "Joining by a code needs the online service, which --net-service off turned off.";
                return false;
            }
            intro = Intro::Stun;
            waiting = "Joining match " + Spaced(joinCode);
        } else {
            std::string host;
            int hostPort = settings.port;
            UdpSocket::Split(settings.address, host, hostPort);
            if (!UdpSocket::Resolve(host, hostPort, peer, error)) {
                return false;
            }
            candidates = {peer};
            joinPort = hostPort;
            waiting = "Joining " + host + " (UDP port " + std::to_string(hostPort) + ")";
        }
    }
    if (settings.hosting && settings.service != "off" && !settings.service.empty()) {
        intro = Intro::Stun;
    }
    if (intro == Intro::Stun) {
        // (the STUN servers' addresses looked up on another thread: a name can take a while, or fail, offline)
        stunLookup = std::async(std::launch::async, [] {
            std::vector<NetAddress> found;
            for (const auto &[host, port] : {std::pair<const char *, int>{"stun.cloudflare.com", 3478}, {"stun.l.google.com", 19302}}) {
                NetAddress address;
                std::string ignored;
                // (IPv4: the address a router changes, which the other side must be told)
                if (UdpSocket::Resolve(host, port, address, ignored, true)) {
                    found.push_back(address);
                }
            }
            return found;
        });
        stunBegan = NowMs();
    }
    fprintf(stderr, "online: %s\n", waiting.c_str());
    return true;
}

namespace {
    // The service asked (method, its path after the service's address, a JSON body or none): the status (0: no
    // answer) and the body, with curl, which is started with no window of its own
    std::pair<int, std::string> Http(const std::string &method, const std::string &url, const std::string &body) {
        std::vector<std::string> args = {CurlProgram(), "-sS", "--max-time", "10", "-X", method, "-H", "content-type: application/json",
                                         "-w", "\n%{http_code}"};
        if (!body.empty()) {
            args.push_back("--data-binary");
            args.push_back(body);
        }
        args.push_back(url);
#if defined(__ANDROID__)
        // (Android has no curl to start: the game's activity makes the request, as the launcher's are made there:
        // recomp-common's run_program, GameActivity.curl)
        std::string text;
        const int exitCode = launcher::run_program(launcher::curl_program(), std::vector<std::string>(args.begin() + 1, args.end()), &text);
#elif defined(_WIN32)
        // (Windows: curl, a console program, started with no console window; its output through a pipe)
        std::string command;
        for (const std::string &arg : args) {
            std::string quoted = "\"";
            for (size_t i = 0; i < arg.size(); i++) {
                size_t slashes = 0;
                while (i < arg.size() && arg[i] == '\\') {
                    slashes++, i++;
                }
                if (i == arg.size()) {
                    quoted.append(slashes * 2, '\\');
                    break;
                }
                quoted.append(arg[i] == '"' ? slashes * 2 + 1 : slashes, '\\');
                quoted += arg[i];
            }
            command += (command.empty() ? "" : " ") + quoted + "\"";
        }
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE readEnd = nullptr, writeEnd = nullptr;
        if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
            return {0, ""};
        }
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writeEnd;
        startup.hStdError = nullptr;
        startup.hStdInput = nullptr;
        PROCESS_INFORMATION info{};
        std::vector<char> commandLine(command.begin(), command.end());
        commandLine.push_back('\0');
        const bool started = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
        CloseHandle(writeEnd);
        std::string text;
        int exitCode = -1;
        if (started) {
            char buffer[4096];
            DWORD got = 0;
            while (ReadFile(readEnd, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
                text.append(buffer, got);
            }
            WaitForSingleObject(info.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(info.hProcess, &code);
            exitCode = static_cast<int>(code);
            CloseHandle(info.hProcess);
            CloseHandle(info.hThread);
        }
        CloseHandle(readEnd);
#else
        std::vector<const char *> argv;
        for (const std::string &arg : args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_Process *process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!process) {
            return {0, ""};
        }
        size_t size = 0;
        int exitCode = 0;
        char *out = static_cast<char *>(SDL_ReadProcess(process, &size, &exitCode));
        SDL_DestroyProcess(process);
        std::string text = out ? std::string(out, size) : "";
        SDL_free(out);
#endif
        if (getenv("CPS3RECOMP_NET_DEBUG")) {
            fprintf(stderr, "online: %s %s: curl %d: %s\n", method.c_str(), url.c_str(), exitCode, text.c_str());
        }
        const size_t line = text.rfind('\n');
        if (exitCode != 0 || line == std::string::npos) {
            return {0, ""};
        }
        return {atoi(text.c_str() + line + 1), text.substr(0, line)};
    }
} // namespace

std::string Netplay::Addresses() const {
    // (where the internet saw this socket, and its address on each of this machine's networks)
    std::vector<std::string> list;
    if (!seenAs.empty()) {
        list.push_back(seenAs);
    }
    for (const launcher::LocalAddress &here : launcher::local_addresses()) {
        list.push_back(here.address + ":" + std::to_string(socket.LocalPort()));
    }
    std::string json = "[";
    for (size_t i = 0; i < list.size() && i < 8; i++) {
        json += (i ? ",\"" : "\"") + list[i] + "\"";
    }
    return json + "]";
}

bool Netplay::TakeStun(const uint8_t *data, int size) {
    // A binding success (0x0101) for our request: its XOR-MAPPED-ADDRESS (0x0020), IPv4
    if (size < 20 || data[0] != 0x01 || data[1] != 0x01 || memcmp(data + 8, stunId.data(), 12) != 0) {
        return false;
    }
    for (int at = 20; at + 4 <= size;) {
        const int type = data[at] << 8 | data[at + 1], length = data[at + 2] << 8 | data[at + 3];
        if (type == 0x0020 && length >= 8 && at + 12 <= size && data[at + 5] == 0x01) {
            const int port = (data[at + 6] << 8 | data[at + 7]) ^ (kStunCookie >> 16);
            const uint32_t ip = (static_cast<uint32_t>(data[at + 8]) << 24 | data[at + 9] << 16 | data[at + 10] << 8 | data[at + 11]) ^ kStunCookie;
            seenAs = std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." + std::to_string((ip >> 8) & 0xFF) + "." +
                     std::to_string(ip & 0xFF) + ":" + std::to_string(port);
            return true;
        }
        at += 4 + ((length + 3) & ~3);
    }
    return true;
}

// A step of the introduction (see Intro); false and `error` when joining by a code cannot go on
bool Netplay::Introduce(uint64_t now, std::string &error) {
    const std::string service = settings.service;
    const auto ask = [&](const std::string &method, const std::string &path, const std::string &body) {
        askedAt = now;
        asking = std::async(std::launch::async, [method, url = service + path, body] {
            const auto [status, text] = Http(method, url, body);
            return HttpAnswer{status, text};
        });
    };
    const auto answered = [&] { return asking.valid() && asking.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    const std::string about = "\"game\":\"" GAME_ID "\",\"title\":\"" GAME_NAME "\",\"version\":\"" RECOMP_VERSION "\",\"protocol\":" +
                              std::to_string(kProtocol) +
                              ",\"build\":\"" + std::to_string(build) + "\"";
    if (getenv("CPS3RECOMP_NET_DEBUG")) {
        static int last = -1;
        if (last != static_cast<int>(intro)) {
            fprintf(stderr, "online: intro %d, stun servers %zu, seen as '%s'\n", static_cast<int>(intro), stunServers.size(), seenAs.c_str());
            last = static_cast<int>(intro);
        }
    }
    switch (intro) {
    case Intro::Off:
    case Intro::Done: return true;
    case Intro::Stun:
        if (stunServers.empty() && stunLookup.valid() && stunLookup.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            stunServers = stunLookup.get();
        }
        if (seenAs.empty() && now - stunBegan < 3000) {
            if (!stunServers.empty() && now - stunSent > 400) {
                // (a binding request: a type, no length, the cookie and a transaction id of our own)
                for (uint8_t &b : stunId) {
                    b = static_cast<uint8_t>((random = random * 6364136223846793005ull + 1442695040888963407ull) >> 56);
                }
                std::vector<uint8_t> request = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
                request.insert(request.end(), stunId.begin(), stunId.end());
                for (const NetAddress &server : stunServers) {
                    socket.Send(server, request.data(), request.size());
                }
                stunSent = now;
            }
            return true;
        }
        // (with where the internet sees us, or without: then only the addresses of this machine's networks)
        if (settings.hosting) {
            const std::string again = settings.renewCode.empty() ? ""
                                      : "\"code\":\"" + settings.renewCode + "\",\"secret\":\"" + settings.renewSecret + "\",";
            std::string listed;
            if (settings.listed || !settings.invite.empty()) {
                // (the name, as JSON text: quotes and backslashes escaped, control characters left out)
                std::string name;
                for (const char c : settings.name) {
                    if (c == '"' || c == '\\') {
                        name += '\\';
                    }
                    if (static_cast<unsigned char>(c) >= 0x20) {
                        name += c;
                    }
                }
                listed = std::string(settings.listed ? ",\"listed\":true" : "") + ",\"name\":\"" + name + "\"";
            }
            // (a player invited from the waiting room: ids, hex digits)
            if (!settings.invite.empty()) {
                listed += ",\"invite\":\"" + settings.invite + "\",\"from\":\"" + settings.from + "\"";
            }
            ask("POST", "/v1/host", "{" + again + about + listed + ",\"addresses\":" + Addresses() + "}");
        } else {
            ask("POST", "/v1/join/" + joinCode, "{" + about + ",\"addresses\":" + Addresses() + "}");
        }
        intro = Intro::Asking;
        return true;
    case Intro::Asking: {
        if (!answered()) {
            return true;
        }
        const HttpAnswer answer = asking.get();
        if (settings.hosting) {
            if (answer.status == 200 && !JsonText(answer.body, "code").empty()) {
                code = Spaced(JsonText(answer.body, "code"));
                secret = JsonText(answer.body, "secret");
                waiting = "Waiting for the other player"; // (the code said under it, and the lobby: Port::Tick)
                fprintf(stderr, "online: the match's code is %s%s\n", code.c_str(), seenAs.empty() ? "" : (" (seen at " + seenAs + ")").c_str());
                codeAt = now;
                intro = Intro::Polling;
            } else {
                fprintf(stderr, "online: no code: the online service did not answer (%d)\n", answer.status);
                intro = Intro::Off; // (the host's address still works)
            }
            return true;
        }
        if (answer.status == 200) {
            for (const std::string &text : JsonList(answer.body, "addresses")) {
                std::string host;
                int port = 0;
                NetAddress address;
                std::string ignored;
                UdpSocket::Split(text, host, port);
                if (port && UdpSocket::Resolve(host, port, address, ignored)) {
                    candidates.push_back(address);
                }
            }
            if (candidates.empty()) {
                error = "The host of that match can't be reached.";
                return false;
            }
            fprintf(stderr, "online: match %s: trying %zu of the host's addresses\n", Spaced(joinCode).c_str(), candidates.size());
            began = now; // (30 seconds from here to hear from the host)
            intro = Intro::Done;
            return true;
        }
        error = answer.status == 404   ? "There's no match with the code " + Spaced(joinCode) + " (codes last 30 minutes)."
                : answer.status == 409 ? JsonText(answer.body, "error")
                                       : "Couldn't reach the online service (is this machine on the internet?).";
        return false;
    }
    case Intro::Polling: {
        // (hosting: asked until someone has joined, every 2 seconds, then 5 after two minutes and 10 after ten, the
        // most the joiner's 30 seconds of waiting allow; then their addresses are sent to)
        if (asking.valid()) {
            if (!answered()) {
                return true;
            }
            const HttpAnswer answer = asking.get();
            if (answer.status == 200 && JsonTrue(answer.body, "joined")) {
                for (const std::string &text : JsonList(answer.body, "addresses")) {
                    std::string host;
                    int port = 0;
                    NetAddress address;
                    std::string ignored;
                    UdpSocket::Split(text, host, port);
                    if (port && UdpSocket::Resolve(host, port, address, ignored)) {
                        candidates.push_back(address);
                    }
                }
                intro = Intro::Done;
                return true;
            }
            if (answer.status == 200 && invitation.empty()) {
                invitation = JsonText(answer.body, "invite");
            }
            if (answer.status == 404) {
                // (the code has run out (30 minutes): the same one had again, for whoever was given it, and asked on)
                fprintf(stderr, "online: the code ran out: asking for it again\n");
                settings.renewCode = code.substr(0, 3) + code.substr(4);
                settings.renewSecret = secret;
                intro = Intro::Stun;
                return true;
            }
        }
        const uint64_t waited = now - codeAt;
        if (now - askedAt > (waited < 120000 ? 2000u : waited < 600000 ? 5000u : 10000u)) {
            ask("GET", "/v1/host/" + code.substr(0, 3) + code.substr(4) + "?secret=" + secret, "");
        }
        return true;
    }
    }
    return true;
}

int Netplay::Waiting(Port &port, std::string &error) {
    Pump();
    uint8_t packet[2048];
    NetAddress from;
    const uint64_t now = NowMs();
    if (!Introduce(now, error)) {
        return -1;
    }
    if (settings.hosting) {
        int got;
        while ((got = socket.Receive(packet, sizeof(packet), from)) >= 0) {
            if (TakeStun(packet, got) || got < 5 || memcmp(packet, kMagic, 4)) {
                continue;
            }
            Reader in(packet, static_cast<size_t>(got));
            if (packet[4] == kHello && (!found || from == peer)) {
                const uint16_t protocol = in.U16();
                const std::string game = in.Text();
                const uint32_t theirs = in.U32();
                const std::string version = in.Text();
                const std::string name = in.Text();
                std::string why;
                if (protocol == kProtocol && version != RECOMP_VERSION) {
                    why = "The other player has version " + version + " and this one " RECOMP_VERSION ": both need the same version.";
                } else if (protocol != kProtocol) {
                    why = "The two copies of the game are different versions: update both.";
                } else if (game != GAME_ID) {
                    why = "The other player started " + game + ", this side " GAME_ID ".";
                } else if (theirs != build) {
                    why = "The two copies of the game are different builds: use the same release on both.";
                }
                if (!why.empty()) {
                    Writer refuse(kRefuse);
                    refuse.Text(why);
                    socket.Send(from, refuse.out.data(), refuse.out.size());
                    fprintf(stderr, "online: turned away a player: %s\n", why.c_str());
                    continue;
                }
                if (!found) {
                    if (settings.solo) {
                        // (the player's own game until now: the match from the board's reset, with the match's features)
                        port.MatchComing();
                        Settle(true, {});
                    }
                    peer = from;
                    found = true;
                    theirName = name;
                    chunksHad = 0;
                    Welcome(port);
                    fprintf(stderr, "online: the other player is here\n");
                }
                lastHeard = now;
                Send(greeting);
                sent = now;
            } else if (found && from == peer && packet[4] == kControls) {
                Take(packet, static_cast<size_t>(got)); // (they have the welcome: the match is on)
                break;
            } else if (found && from == peer && packet[4] == kPieceAck) {
                chunksHad = std::max(chunksHad, in.U32());
                lastHeard = now;
            } else if (found && from == peer && packet[4] == kBye) {
                found = false; // (they left before it began)
                chunksHad = 0;
            }
        }
        if (got >= 0) {
            lastHeard = now;
            fprintf(stderr, "online: the match is on: you are player 1 (%d frame%s of delay)\n", delay, delay == 1 ? "" : "s");
            return 1;
        }
        if (found && now - lastHeard > 5000) {
            // (they went quiet before it began: waited for again, and the game, if it is played meanwhile, goes on)
            fprintf(stderr, "online: the other player went quiet before the match began\n");
            found = false;
            chunksHad = 0;
        }
        if (found && now - sent > 250 && chunksHad == 0) {
            Send(greeting);
            sent = now;
        }
        // (the state, in pieces: those the other side hasn't said it has, a window at a time, again until it has them)
        if (found && chunksHad < chunks && now - chunkSent > 40) {
            for (uint32_t n = chunksHad; n < chunks && n < chunksHad + 32; n++) {
                Writer piece(kPiece);
                piece.U32(n);
                const size_t at = static_cast<size_t>(n) * kChunk;
                piece.Bytes(state.data() + at, std::min(kChunk, state.size() - at));
                Send(piece.out);
            }
            chunkSent = now;
        }
        // (someone joined by the code: packets their way, which open this side's router to their hello)
        if (!found && !candidates.empty() && now - punchedAt > 250) {
            Writer punch(kPunch);
            for (const NetAddress &to : candidates) {
                SendTo(to, punch.out);
            }
            punchedAt = now;
        }
        return 0;
    }
    // Joining: the hello every quarter second to each of the host's addresses until the welcome comes (for 30 seconds)
    if (intro != Intro::Off && intro != Intro::Done) {
        int got;
        while ((got = socket.Receive(packet, sizeof(packet), from)) >= 0) {
            TakeStun(packet, got);
        }
        return 0;
    }
    if (now - began > 30000) {
        error = joinCode.empty() ? "The host did not answer. Is it waiting, and can this machine reach it on UDP port " + std::to_string(joinPort) + "?"
                                 : "The host of match " + Spaced(joinCode) + " couldn't be reached through the two networks' routers.";
        return -1;
    }
    if (now - sent > (welcomed ? 100 : 250)) {
        if (welcomed) {
            Writer ack(kPieceAck);
            ack.U32(chunksHad);
            Send(ack.out);
        } else {
            for (const NetAddress &to : candidates) {
                SendTo(to, greeting);
            }
        }
        sent = now;
    }
    int got;
    while ((got = socket.Receive(packet, sizeof(packet), from)) >= 0) {
        if (TakeStun(packet, got) || got < 5 || memcmp(packet, kMagic, 4)) {
            continue;
        }
        Reader in(packet, static_cast<size_t>(got));
        if (packet[4] == kRefuse) {
            error = in.Text();
            return -1;
        }
        if (packet[4] == kPiece && welcomed && from == peer) {
            const uint32_t n = in.U32();
            const size_t size = in.U16();
            const size_t at = static_cast<size_t>(n) * kChunk;
            if (n < chunks && in.Has(size) && at + size <= state.size()) {
                memcpy(state.data() + at, in.at, size);
                had[n] = true;
                while (chunksHad < chunks && had[chunksHad]) {
                    chunksHad++;
                }
            }
            if (chunksHad == chunks) {
                // (all here: the host's game, as this side's)
                std::vector<uint8_t> raw(stateSize);
                uLongf size = static_cast<uLongf>(stateSize);
                if (uncompress(raw.data(), &size, state.data(), static_cast<uLong>(state.size())) != Z_OK || size != stateSize) {
                    error = "The host's game did not come whole.";
                    return -1;
                }
                // (the host's program's words first: where this side's set differs, the host's are taken in)
                Reader program(raw.data(), raw.size(), 0);
                std::vector<std::pair<uint32_t, std::array<uint8_t, 4>>> words(program.U16());
                for (auto &[word, bytes] : words) {
                    word = program.U32();
                    for (uint8_t &b : bytes) {
                        b = program.U8();
                    }
                }
                if (!program.ok) {
                    error = "The host's game did not come whole.";
                    return -1;
                }
                for (const auto &[word, bytes] : words) {
                    if (word + 2 <= port.roms.program.size() * 4) {
                        for (int n = 0; n < 2; n++) {
                            uint32_t &w = port.roms.program[(word + n) / 4];
                            const int shift = (3 - (word + n) % 4) * 8;
                            w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(bytes[n]) << shift);
                        }
                    }
                }
                raw.erase(raw.begin(), raw.begin() + (program.at - raw.data()));
                port.PutBackState(raw);
                lastHeard = now;
                fprintf(stderr, "online: the match is on: you are player 2 (%d frame%s of delay)\n", delay, delay == 1 ? "" : "s");
                return 1;
            }
            continue;
        }
        if (packet[4] != kWelcome || welcomed) {
            continue;
        }
        peer = from;
        if (in.U16() != kProtocol) {
            error = "The two copies of the game are different versions: update both.";
            return -1;
        }
        StartAt(in.U8());
        const size_t words = in.U16();
        if (!in.Has(words) || words != sizeof(port.machine->eeprom.words)) {
            error = "The host's welcome did not make sense.";
            return -1;
        }
        memcpy(port.machine->eeprom.words, in.at, words); // (the host's board settings, for this match only)
        in.at += words;
        std::map<std::string, int> host;
        const std::string features = in.Text();
        for (size_t at = 0; at < features.size();) {
            const size_t comma = std::min(features.find(',', at), features.size());
            const std::string item = features.substr(at, comma - at);
            const size_t eq = item.find('=');
            if (eq != std::string::npos) {
                host[item.substr(0, eq)] = atoi(item.c_str() + eq + 1);
            }
            at = comma + 1;
        }
        Settle(false, host);
        stateSize = in.U32();
        state.assign(in.U32(), 0);
        if (!in.ok || stateSize == 0 || state.empty() || state.size() > (16u << 20)) {
            error = "The host's welcome did not make sense.";
            return -1;
        }
        hostIn = (in.U8() & 1) != 0;
        // (the host's set: where this side's is another region's, the match is played as the host's)
        const std::string hostSet = in.Text();
        theirName = in.Text();
        if (!in.ok) {
            error = "The host's welcome did not make sense.";
            return -1;
        }
        if (hostSet != SetName(port)) {
            fprintf(stderr, "online: playing the host's set, %s\n", hostSet.c_str());
        }
        chunks = static_cast<uint32_t>((state.size() + kChunk - 1) / kChunk);
        had.assign(chunks, false);
        welcomed = true;
        sent = 0; // (the first ack at once)
    }
    return 0;
}

std::string Netplay::Name(int player) const {
    return Printable(player == local ? settings.name : theirName, kNameMost);
}

void Netplay::Say(const std::string &text) {
    const std::string line = Printable(text, kChatMost);
    if (!line.empty()) {
        unheard.push_back({said++, line});
        SendChat(0);
    }
}

std::vector<std::string> Netplay::TakeHeard() {
    std::vector<std::string> taken;
    taken.swap(heard);
    return taken;
}

// The lines the other side hasn't said it has, in order, now and then until it has (a line is a packet: what the
// chat sends is small beside the controls, sent every frame)
void Netplay::SendChat(uint64_t now) {
    if (unheard.empty() || now - saidAt < 250) {
        return;
    }
    for (const auto &[n, text] : unheard) {
        Writer line(kChat);
        line.U32(n);
        line.Text(text);
        Send(line.out);
    }
    saidAt = NowMs();
}

int Netplay::Ping() const {
    return std::max(0, static_cast<int>(std::lround(ping - 1000.0 / Machine::kFramesPerSecond)));
}

uint64_t Netplay::Silent() const {
    return NowMs() - lastHeard;
}

bool Netplay::Connect(Port &port, std::string &error) {
    if (!Begin(port, error)) {
        return false;
    }
    int state;
    while ((state = Waiting(port, error)) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return state > 0;
}

void Netplay::SendInputs() {
    Writer out(kControls);
    out.U32(static_cast<uint32_t>(current));
    out.U32(static_cast<uint32_t>(remoteUpTo));
    const uint64_t first = std::max<uint64_t>(remoteAck, localUpTo > 64 ? localUpTo - 64 : 0);
    out.U32(static_cast<uint32_t>(first));
    out.U8(static_cast<uint8_t>(localUpTo - first));
    for (uint64_t at = first; at < localUpTo; at++) {
        out.U16(localInput[at % kInputs]);
    }
    out.U16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(ahead, -100.0, 100.0) * 100))));
    out.U32(static_cast<uint32_t>(lastHashFrame));
    out.U32(lastHash);
    const uint64_t now = NowMs();
    out.U32(static_cast<uint32_t>(now));
    out.U32(echo);
    out.U32(static_cast<uint32_t>(echoAt ? now - echoAt : 0));
    Send(out.out);
}

void Netplay::Take(const uint8_t *data, size_t size) {
    Reader in(data, size);
    if (data[4] == kBye || data[4] == kRefuse) {
        if (ended.empty()) { // (the first reason is the one: a desync is followed by a goodbye)
            ended = data[4] == kBye ? "The other player left." : in.Text();
            failed = data[4] == kRefuse;
            left = data[4] == kBye;
        }
        return;
    }
    if (data[4] == kChat) {
        // (the next line, or one again that is already here: either way, how many have come is said back)
        const uint32_t n = in.U32();
        const std::string text = Printable(in.Text(), kChatMost);
        if (in.ok && n == heardUpTo) {
            heardUpTo++;
            if (!text.empty()) {
                heard.push_back(text);
            }
        }
        Writer ack(kChatAck);
        ack.U32(heardUpTo);
        Send(ack.out);
        return;
    }
    if (data[4] == kChatAck) {
        const uint32_t upTo = in.U32();
        while (in.ok && !unheard.empty() && unheard.front().first < upTo) {
            unheard.pop_front();
        }
        return;
    }
    if (data[4] != kControls) {
        return;
    }
    const uint64_t frame = in.U32(), ack = in.U32(), first = in.U32();
    const unsigned count = in.U8();
    uint16_t inputs[256];
    for (unsigned n = 0; n < count; n++) {
        inputs[n] = in.U16();
    }
    const double theirs = static_cast<int16_t>(in.U16()) / 100.0;
    const uint64_t hashFrame = in.U32();
    const uint32_t hash = in.U32();
    const uint32_t sentAt = in.U32(), echoed = in.U32(), held = in.U32();
    if (!in.ok) {
        return;
    }
    lastHeard = NowMs();
    remoteFrame = std::max(remoteFrame, frame);
    remoteAck = std::max(remoteAck, ack);
    theirAhead = theirs;
    for (uint64_t at = remoteUpTo; at < first + count; at++) {
        if (at >= first && at < confirmed + kInputs) {
            remoteInput[at % kInputs] = inputs[at - first];
            remoteUpTo = at + 1;
        }
    }
    if (hashFrame) {
        theirHashes[hashFrame] = hash;
    }
    echo = sentAt;
    echoAt = NowMs();
    if (echoed) {
        const double rtt = static_cast<double>(static_cast<uint32_t>(static_cast<uint32_t>(NowMs()) - echoed - held));
        if (rtt < 10000) {
            ping = ping ? ping * 0.9 + rtt * 0.1 : rtt;
        }
    }
}

void Netplay::Pump() {
    const uint64_t now = NowMs();
    while (!late.empty() && late.front().due <= now) {
        socket.Send(late.front().to, late.front().data.data(), late.front().data.size());
        late.pop_front();
    }
}

bool Netplay::Ready(uint64_t frame) {
    Pump();
    uint8_t packet[2048];
    NetAddress from;
    int got;
    while ((got = socket.Receive(packet, sizeof(packet), from)) >= 0) {
        if (got >= 5 && !memcmp(packet, kMagic, 4) && from == peer) {
            if (packet[4] == kWelcome && local == 1) {
                continue; // (the host had not heard yet: our controls tell it)
            }
            Take(packet, static_cast<size_t>(got));
        }
    }
    Compare();
    SendChat(NowMs());
    if (ended.empty() && NowMs() - lastHeard > 5000) {
        ended = "Lost the connection to the other player.";
        failed = lost = true;
    }
    if (!ended.empty()) {
        return false;
    }
    current = frame;
    // Too far ahead of the other side's controls: wait for them
    if (frame >= remoteUpTo + kMostAhead) {
        waits++;
        SendInputs();
        return false;
    }
    // Ahead of the other side in time (each side's own idea of how far ahead it is, compared): a tick waited now
    // and then, until even
    if (frame % 60 == 0 && frame != waitedAt && waitTicks == 0) {
        const double by = (ahead - theirAhead) / 2;
        waitTicks = by >= 1 ? std::min(static_cast<int>(by), 8) : 0;
    }
    if (waitTicks > 0 && frame != waitedAt) {
        waitedAt = frame;
        waitTicks--;
        waits++;
        SendInputs();
        return false;
    }
    return true;
}

void Netplay::Run(Port &port, uint64_t at, bool drawing) {
    Kept &k = kept[at % kKept];
    uint16_t players[Machine::kMaxPlayers] = {};
    players[local] = localInput[at % kInputs];
    // (the other side's, or guessed as the last that came)
    k.used = at < remoteUpTo ? remoteInput[at % kInputs] : remoteUpTo ? remoteInput[(remoteUpTo - 1) % kInputs] : 0;
    players[remote] = k.used;
    port.machine->drawing = drawing;
    port.Step(players);
    port.machine->drawing = true;
    if ((at + 1) % kHashEvery == 0 || port.hashFile) {
        k.hash = port.StateHash();
    }
}

void Netplay::Frame(Port &port, uint16_t controls) {
    const auto began = std::chrono::steady_clock::now();
    current = port.frame;
    const uint64_t frame = port.frame;
    localInput[(frame + delay) % kInputs] = controls;
    localUpTo = frame + delay + 1;
    // How far ahead of the other side this side is: the frame it is on, over what the other side said last
    ahead = ahead * 0.95 + (static_cast<double>(frame) - static_cast<double>(remoteFrame)) * 0.05;
    SendInputs();
    // A wrong guess since the last frame that had the real controls: back to it, and on to here again, unseen and
    // unheard (the sound of those frames has been heard)
    const uint64_t upTo = std::min(remoteUpTo, frame);
    uint64_t wrong = upTo;
    for (uint64_t at = confirmed; at < upTo; at++) {
        if (kept[at % kKept].used != remoteInput[at % kInputs]) {
            wrong = at;
            break;
        }
    }
    if (wrong < upTo) {
        const size_t sound = port.machine->audio.size();
        port.PutBackState(kept[wrong % kKept].state, true);
        for (uint64_t at = wrong; at < frame; at++) {
            if (at != wrong) {
                port.KeepState(kept[at % kKept].state, true);
            }
            Run(port, at, false);
        }
        port.machine->audio.resize(sound);
        rollbacks++;
        rerun += frame - wrong;
        mostRerun = std::max(mostRerun, static_cast<int>(frame - wrong));
    }
    // The frames that now have the real controls: their hashes, to compare (and to the hash log)
    for (uint64_t at = confirmed; at < upTo; at++) {
        const Kept &k = kept[at % kKept];
        if (!port.opt.movieRecord.empty()) {
            // (--record: what both players held, as the frame had them, for the frames that have the real ones)
            Movie::Pads pads{};
            pads[local] = localInput[at % kInputs];
            pads[remote] = remoteInput[at % kInputs];
            port.recMovie.changes[at] = pads;
        }
        if (port.hashFile) {
            fprintf(port.hashFile, "%llu %08x\n", static_cast<unsigned long long>(at + 1), k.hash);
        }
        if ((at + 1) % kHashEvery == 0) {
            ourHashes[at + 1] = k.hash;
            lastHashFrame = at + 1;
            lastHash = k.hash;
        }
    }
    confirmed = upTo;
    Compare();
    // The frame itself
    port.KeepState(kept[frame % kKept].state, true);
    Run(port, frame, true);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    worst = std::max(worst, took);
    over += took > 1 / Machine::kFramesPerSecond;
    Report();
}

void Netplay::Compare() {
    for (auto it = ourHashes.begin(); it != ourHashes.end();) {
        const auto theirs = theirHashes.find(it->first);
        if (theirs != theirHashes.end()) {
            if (theirs->second != it->second && ended.empty()) {
                ended = "The two games came apart (a desync at frame " + std::to_string(it->first) + "): the match is stopped.";
                failed = true;
                Writer refuse(kRefuse);
                refuse.Text(ended);
                Send(refuse.out);
                Pump();
            }
            it = ourHashes.erase(it);
        } else if (it->first + kHashEvery * 20 < confirmed) {
            it = ourHashes.erase(it); // (theirs never came: lost)
        } else {
            ++it;
        }
    }
    while (!theirHashes.empty() && theirHashes.begin()->first + kHashEvery * 20 < confirmed) {
        theirHashes.erase(theirHashes.begin());
    }
}

void Netplay::Leave() {
    Writer bye(kBye);
    for (int n = 0; n < 3; n++) {
        socket.Send(peer, bye.out.data(), bye.out.size());
    }
}

void Netplay::Report(bool last) {
    const uint64_t now = NowMs();
    if (!last && now - lastReport < 10000) {
        return;
    }
    if (!lastReport && !last) {
        lastReport = now;
        return;
    }
    lastReport = now;
    fprintf(stderr, "online: frame %llu, ping %.0f ms, %llu rollbacks (%llu since the last report), %llu frames run again, at most %d at once; "
                    "%llu ticks waited; a frame at most %.1f ms, %llu over a frame's time\n",
            static_cast<unsigned long long>(current), ping, static_cast<unsigned long long>(rollbacks),
            static_cast<unsigned long long>(rollbacks - reportedRollbacks), static_cast<unsigned long long>(rerun), mostRerun,
            static_cast<unsigned long long>(waits), worst * 1000, static_cast<unsigned long long>(over));
    reportedRollbacks = rollbacks;
}

} // namespace port
