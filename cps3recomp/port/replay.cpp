// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
//
// Match replays (replay.h): the files, and the port's part (keeping a match as it is played, playing one back).
#include "replay.h"

#include "game.h"
#include "game_info.h"
#include "port.h"

#include <SDL3/SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <ctime>

using namespace cps3recomp;

namespace port {

namespace {
    constexpr char kMagic[4] = {'S', '3', 'R', 'P'};
    constexpr uint32_t kFormat = 1;

    struct Out {
        std::vector<uint8_t> bytes;
        void Raw(const void *p, size_t size) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            bytes.insert(bytes.end(), b, b + size);
        }
        void U32(uint32_t v) {
            const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
            Raw(b, 4);
        }
        void U16(uint16_t v) {
            const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
            Raw(b, 2);
        }
        void Text(const std::string &text) {
            U32(static_cast<uint32_t>(text.size()));
            Raw(text.data(), text.size());
        }
    };
    struct In {
        const std::vector<uint8_t> &bytes;
        size_t at = 0;
        bool ok = true;
        bool Raw(void *p, size_t size) {
            if (!ok || size > bytes.size() - at) {
                ok = false;
                return false;
            }
            memcpy(p, bytes.data() + at, size);
            at += size;
            return true;
        }
        uint32_t U32() {
            uint8_t b[4] = {};
            Raw(b, 4);
            return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
        }
        uint16_t U16() {
            uint8_t b[2] = {};
            Raw(b, 2);
            return static_cast<uint16_t>(b[0] | (b[1] << 8));
        }
        std::string Text() {
            const uint32_t size = U32();
            if (!ok || size > bytes.size() - at) {
                ok = false;
                return {};
            }
            std::string text(reinterpret_cast<const char *>(bytes.data() + at), size);
            at += size;
            return text;
        }
    };

    bool Pack(const std::vector<uint8_t> &raw, std::vector<uint8_t> &packed, int level) {
        uLongf size = compressBound(static_cast<uLong>(raw.size()));
        packed.resize(size);
        if (compress2(packed.data(), &size, raw.data(), static_cast<uLong>(raw.size()), level) != Z_OK) {
            return false;
        }
        packed.resize(size);
        return true;
    }
    bool Unpack(const uint8_t *packed, size_t packedSize, size_t rawSize, std::vector<uint8_t> &raw) {
        raw.resize(rawSize);
        uLongf size = static_cast<uLongf>(rawSize);
        return uncompress(raw.data(), &size, packed, static_cast<uLong>(packedSize)) == Z_OK && size == rawSize;
    }
    bool ReadAll(const std::string &path, std::vector<uint8_t> &data, size_t most = SIZE_MAX) {
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        fseek(f, 0, SEEK_END);
        const size_t size = std::min(static_cast<size_t>(ftell(f)), most);
        fseek(f, 0, SEEK_SET);
        data.resize(size);
        const bool ok = fread(data.data(), 1, size, f) == size;
        fclose(f);
        return ok;
    }
    // A feature of the match's that the replay is played with (what changes play); the rest are the player's own now
    bool Shared(const Feature &feature) {
        const std::string category = feature.category, name = feature.name;
        if (feature.flags & kFeatureTraining) {
            return false;
        }
        if (category == "Online" || category == "Replays" || (category == "Display" && name.rfind("widescreen", 0) != 0)) {
            return false;
        }
        return name != "run_ahead" && name != "rapid_fire" && name != "custom_soundtrack";
    }
    uint32_t GameHash(Port &port) {
        // (the game alone, not its picture: the picture is the viewer's to choose)
        const bool picture = port.opt.hashPicture, noPicture = port.opt.hashNoPicture;
        port.opt.hashPicture = false;
        port.opt.hashNoPicture = true;
        const uint32_t hash = port.StateHash();
        port.opt.hashPicture = picture;
        port.opt.hashNoPicture = noPicture;
        return hash;
    }
} // namespace

std::string Replay::Length(uint32_t frames) {
    const uint32_t seconds = frames / 60;
    char text[24];
    snprintf(text, sizeof(text), "%u:%02u", seconds / 60, seconds % 60);
    return text;
}

bool Replay::Write(const std::string &path, const Data &data) {
    Out head;
    head.Raw(kMagic, 4);
    head.U32(kFormat);
    head.Text(data.game);
    head.Text(data.version);
    head.U32(data.stateVersion);
    head.U32(static_cast<uint32_t>(data.when));
    head.U32(static_cast<uint32_t>(data.when >> 32));
    head.U32(data.frames);
    head.U32(data.fought);
    head.U32(data.online ? 1 : 0);
    head.Text(data.label);
    head.U32(static_cast<uint32_t>(data.features.size()));
    for (const auto &[name, value] : data.features) {
        head.Text(name);
        head.U32(static_cast<uint32_t>(value));
    }
    Out body;
    body.U32(static_cast<uint32_t>(data.state.size()));
    body.Raw(data.state.data(), data.state.size());
    body.U32(static_cast<uint32_t>(data.changes.size()));
    for (const auto &[frame, pads] : data.changes) {
        body.U32(frame);
        for (const uint16_t pad : pads) {
            body.U16(pad);
        }
    }
    body.U32(static_cast<uint32_t>(data.hashes.size()));
    for (const auto &[frame, hash] : data.hashes) {
        body.U32(frame);
        body.U32(hash);
    }
    std::vector<uint8_t> packed;
    if (!Pack(body.bytes, packed, 6)) {
        return false;
    }
    head.U32(static_cast<uint32_t>(body.bytes.size()));
    head.U32(static_cast<uint32_t>(packed.size()));
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = fwrite(head.bytes.data(), 1, head.bytes.size(), f) == head.bytes.size() && fwrite(packed.data(), 1, packed.size(), f) == packed.size();
    fclose(f);
    return ok;
}

bool Replay::Read(const std::string &path, Data &data, bool headOnly, std::string &error) {
    std::vector<uint8_t> bytes;
    if (!ReadAll(path, bytes, headOnly ? 16384 : SIZE_MAX)) {
        error = "cannot read " + path;
        return false;
    }
    In in{bytes};
    char magic[4] = {};
    in.Raw(magic, 4);
    if (!in.ok || memcmp(magic, kMagic, 4) != 0 || in.U32() != kFormat) {
        error = "not a replay this build reads";
        return false;
    }
    data = {};
    data.game = in.Text();
    data.version = in.Text();
    data.stateVersion = in.U32();
    const uint64_t low = in.U32(), high = in.U32();
    data.when = static_cast<int64_t>(low | (high << 32));
    data.frames = in.U32();
    data.fought = in.U32();
    data.online = in.U32() != 0;
    data.label = in.Text();
    const uint32_t features = in.U32();
    for (uint32_t n = 0; n < features && in.ok; n++) {
        std::string name = in.Text();
        data.features.push_back({std::move(name), static_cast<int>(in.U32())});
    }
    const uint32_t rawSize = in.U32(), packedSize = in.U32();
    if (!in.ok) {
        error = "the replay's file is cut short";
        return false;
    }
    if (headOnly) {
        return true;
    }
    std::vector<uint8_t> raw;
    if (packedSize > bytes.size() - in.at || !Unpack(bytes.data() + in.at, packedSize, rawSize, raw)) {
        error = "the replay's file is cut short";
        return false;
    }
    In body{raw};
    data.state.resize(body.U32());
    body.Raw(data.state.data(), data.state.size());
    const uint32_t changes = body.U32();
    for (uint32_t n = 0; n < changes && body.ok; n++) {
        const uint32_t frame = body.U32();
        Pads pads{};
        for (uint16_t &pad : pads) {
            pad = body.U16();
        }
        data.changes[frame] = pads;
    }
    const uint32_t hashes = body.U32();
    for (uint32_t n = 0; n < hashes && body.ok; n++) {
        const uint32_t frame = body.U32();
        data.hashes[frame] = body.U32();
    }
    if (!body.ok) {
        error = "the replay's file is cut short";
        return false;
    }
    return true;
}

std::vector<Replay::Entry> Replay::List(const std::string &folder) {
    struct Found {
        int64_t when;
        Entry entry;
    };
    std::vector<Found> found;
    std::error_code ignored;
    for (const auto &file : std::filesystem::directory_iterator(TextPath(folder), ignored)) {
        if (file.path().extension() != ".s3r") {
            continue;
        }
        Data data;
        std::string error;
        const std::string path = PathText(file.path());
        if (!Read(path, data, true, error)) {
            continue;
        }
        char stamp[32] = "";
        const std::time_t when = static_cast<std::time_t>(data.when);
        if (const std::tm *local = std::localtime(&when)) {
            std::strftime(stamp, sizeof(stamp), "%b %d %H:%M", local);
        }
        Entry entry;
        entry.path = path;
        entry.title = std::string(stamp) + "  " + Length(data.frames) + (data.label.empty() ? "" : "  " + data.label);
        if (data.game != GAME_ID || data.version != RECOMP_VERSION || data.stateVersion != Port::StateVersion()) {
            entry.detail = "Kept by version " + data.version + ": only the version that kept a match can play it again.";
        } else {
            entry.detail = "Plays this match again. While it plays: left and right go back and forward five seconds (a frame, while "
                           "stopped), down stops it and starts it, up starts it over. Start takes over that player's side from "
                           "there, and the restart key (Backspace, the left shoulder) then goes back to that moment to try again.";
        }
        found.push_back({data.when, std::move(entry)});
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.when > b.when; });
    std::vector<Entry> list;
    for (Found &one : found) {
        list.push_back(std::move(one.entry));
    }
    return list;
}

// ---------------------------------------------------------------------------------------------------------------
// The port's part

std::string Port::ReplayFolder() const {
    return replay.folder;
}

void Port::ReplayBegin(bool online) {
    Replay &r = replay;
    if (getenv("CPS3RECOMP_REPLAY_DEBUG")) {
        fprintf(stderr, "replay: a match begins (%s)\n", online ? "online" : "offline");
    }
    r.made = {};
    r.made.game = GAME_ID;
    r.made.version = RECOMP_VERSION;
    r.made.stateVersion = StateVersion();
    r.made.when = static_cast<int64_t>(std::time(nullptr));
    r.made.online = online;
    for (const Feature &feature : TheGame().features) {
        r.made.features.push_back({feature.name, FeatureSetting(feature.name)});
    }
    if (TheGame().matchLabel && !online) {
        r.made.label = TheGame().matchLabel();
    }
    KeepState(r.made.state);
    r.lastFought = 0;
    r.lastHeld = {};
    r.recording = true;
}

void Port::ReplayEnd(bool keep) {
    Replay &r = replay;
    if (!r.recording) {
        return;
    }
    r.recording = false;
    Replay::Data &made = r.made;
    if (!made.online) {
        // (the match, and a moment after its last round)
        const uint32_t frames = std::min(made.frames, r.lastFought + Replay::kTail);
        made.frames = frames;
        made.changes.erase(made.changes.lower_bound(frames), made.changes.end());
        made.hashes.erase(made.hashes.upper_bound(frames), made.hashes.end());
    }
    const std::string folder = ReplayFolder();
    // (a match somebody played: the attract mode's fights have next to no input)
    if (!keep || folder.empty() || made.fought < Replay::kLeast || made.changes.size() < 20) {
        if (getenv("CPS3RECOMP_REPLAY_DEBUG")) {
            fprintf(stderr, "replay: not kept (keep %d, folder '%s', %u frames fought, %zu changes)\n", keep, folder.c_str(), made.fought, made.changes.size());
        }
        made = {};
        return;
    }
    SDL_CreateDirectory(folder.c_str());
    char stamp[32] = "";
    const std::time_t when = static_cast<std::time_t>(made.when);
    if (const std::tm *local = std::localtime(&when)) {
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
    }
    const std::string path = folder + stamp + ".s3r";
    if (Replay::Write(path, made)) {
        fprintf(stderr, "replay: kept %s (%s, %zu changes of the controls)\n", path.c_str(), Replay::Length(made.frames).c_str(), made.changes.size());
    } else {
        fprintf(stderr, "replay: cannot write %s\n", path.c_str());
    }
    made = {};
    // (the oldest go)
    std::vector<std::string> files;
    std::error_code ignored;
    for (const auto &file : std::filesystem::directory_iterator(TextPath(folder), ignored)) {
        if (file.path().extension() == ".s3r") {
            files.push_back(PathText(file.path()));
        }
    }
    std::sort(files.begin(), files.end()); // (named by their time)
    for (size_t n = 0; n + Replay::kMost < files.size(); n++) {
        std::filesystem::remove(TextPath(files[n]), ignored);
    }
}

void Port::ReplayConfirmed(uint64_t at, const uint16_t *pads) {
    Replay &r = replay;
    if (!r.recording || !r.made.online) {
        return;
    }
    Replay::Pads held{};
    std::copy(pads, pads + held.size(), held.begin());
    if (at == 0 || held != r.lastHeld) {
        r.made.changes[static_cast<uint32_t>(at)] = held;
        r.lastHeld = held;
    }
    r.made.frames = r.made.fought = static_cast<uint32_t>(at + 1);
    if (r.made.frames >= Replay::kLongest) {
        ReplayEnd();
    }
}

void Port::ReplayBefore(uint16_t *pressed) {
    Replay &r = replay;
    if (r.playing) {
        // A state now and then, to go back to (packed on another thread: a state is some megabytes)
        if (r.at > 0 && r.at % Replay::kKeepEvery == 0 && !r.kept.count(r.at) && !r.anyTaken()) {
            auto raw = std::make_shared<std::vector<uint8_t>>();
            KeepState(*raw);
            const Replay::Pads now = r.now;
            r.kept[r.at] = std::async(std::launch::async, [raw, now] {
                                Replay::Kept kept;
                                kept.size = raw->size();
                                kept.now = now;
                                Pack(*raw, kept.packed, 1);
                                return kept;
                            }).share();
        }
        const auto change = r.play.changes.find(r.at);
        if (change != r.play.changes.end()) {
            r.now = change->second;
        }
        for (size_t p = 0; p < r.now.size(); p++) {
            if (p < 2 && r.taken[p]) {
                // (the side is the player's: all but the Start that took it, until it is let go)
                if (r.startHeld[p]) {
                    if (pressed[p] & kStart) {
                        pressed[p] &= static_cast<uint16_t>(~kStart);
                    } else {
                        r.startHeld[p] = false;
                    }
                }
            } else {
                pressed[p] = r.at < r.play.frames ? r.now[p] : 0;
            }
        }
        return;
    }
    const Game &game = TheGame();
    const bool on = FeatureOn("match_replays") && game.fighting && !(game.inTraining && game.inTraining()) && !FastForward();
    const bool fighting = on && game.fighting();
    if (!r.recording && fighting && !r.wasFighting) {
        ReplayBegin(false);
    }
    r.wasFighting = fighting;
    if (!r.recording || r.made.online) {
        return;
    }
    Replay::Pads held{};
    std::copy(pressed, pressed + held.size(), held.begin());
    if (r.made.frames == 0 || held != r.lastHeld) {
        r.made.changes[r.made.frames] = held;
        r.lastHeld = held;
    }
    if (fighting) {
        r.lastFought = r.made.frames;
        r.made.fought++;
    }
}

void Port::ReplayAfter() {
    Replay &r = replay;
    if (r.playing) {
        r.at++;
        if (!r.anyTaken()) {
            const auto hash = r.play.hashes.find(r.at);
            if (hash != r.play.hashes.end() && hash->second != GameHash(*this)) {
                if (!r.outOfStep) {
                    fprintf(stderr, "replay: out of step at frame %u\n", r.at);
                }
                r.outOfStep = true;
            }
            if (r.at == r.play.frames && !r.over) {
                // (its last frame: stopped there, to go back or to play on from)
                r.over = true;
                r.skip = 0;
                paused = !opt.headless; // (a test's run goes on, with nothing held)
                steps = 0;
                fprintf(stderr, "replay: over after %u frames%s\n", r.at, r.outOfStep ? ", out of step" : ", in step");
            }
        }
        return;
    }
    if (!r.recording || r.made.online) {
        return;
    }
    r.made.frames++;
    if (r.made.frames % Replay::kHashEvery == 0) {
        r.made.hashes[r.made.frames] = GameHash(*this);
    }
    const Game &game = TheGame();
    const bool on = FeatureOn("match_replays") && !(game.inTraining && game.inTraining());
    if (!on || r.made.frames - r.lastFought > Replay::kQuiet || r.made.frames >= Replay::kLongest) {
        ReplayEnd();
    }
}

bool Port::ReplayPlay(const std::string &path, std::string &error) {
    Replay &r = replay;
    if (net || waitingHost) {
        error = "Not while a match is hosted or played online.";
        return false;
    }
    Replay::Data data;
    if (!Replay::Read(path, data, false, error)) {
        return false;
    }
    if (data.game != GAME_ID || data.version != RECOMP_VERSION || data.stateVersion != StateVersion()) {
        error = "Kept by version " + data.version + ": only the version that kept a match can play it again.";
        return false;
    }
    ReplayEnd();
    if (r.playing) {
        // (another one: the player's own game and features stay the ones kept)
        for (const auto &[name, value] : r.ownFeatures) {
            SetFeatureForNow(name.c_str(), value);
        }
    } else {
        KeepState(r.ownState);
        r.ownFeatures.clear();
        for (const Feature &feature : TheGame().features) {
            r.ownFeatures[feature.name] = FeatureSetting(feature.name);
        }
    }
    r.play = std::move(data);
    for (const auto &[name, value] : r.play.features) {
        for (const Feature &feature : TheGame().features) {
            if (name == feature.name && Shared(feature)) {
                SetFeatureForNow(feature.name, value);
            }
        }
    }
    r.playing = true;
    r.kept.clear();
    SetWatching(true);
    ReplaySeek(0);
    paused = false;
    steps = 0;
    r.hintUntil = tick + 600;
    r.lastPads[0] = r.lastPads[1] = 0xFFFF; // (what is held now is not a press)
    fprintf(stderr, "replay: playing %s (%s)\n", path.c_str(), Replay::Length(r.play.frames).c_str());
    return true;
}

void Port::ReplayStop(bool putBack) {
    Replay &r = replay;
    if (!r.playing) {
        return;
    }
    r.playing = false;
    SetWatching(false);
    for (const auto &[name, value] : r.ownFeatures) {
        SetFeatureForNow(name.c_str(), value);
    }
    if (putBack && !r.ownState.empty()) {
        PutBackState(r.ownState);
    }
    machine->audio.clear();
    r.ownState.clear();
    r.ownFeatures.clear();
    r.kept.clear();
    r.play = {};
    r.takenState.clear();
    r.taken[0] = r.taken[1] = false;
    r.skip = 0;
    r.wasFighting = true; // (the player's own game, back in the middle of whatever it was: not a match's beginning)
    paused = false;
    steps = 0;
}

void Port::ReplaySeek(int64_t to) {
    Replay &r = replay;
    const uint32_t target = static_cast<uint32_t>(std::clamp<int64_t>(to, 0, r.play.frames));
    // (the nearest state kept at or before it; the match's own beginning if none)
    uint32_t from = 0;
    Replay::Pads now{};
    const std::vector<uint8_t> *state = &r.play.state;
    std::vector<uint8_t> raw;
    auto it = r.kept.upper_bound(target);
    if (it != r.kept.begin()) {
        --it;
        const Replay::Kept &kept = it->second.get();
        if (Unpack(kept.packed.data(), kept.packed.size(), kept.size, raw)) {
            from = it->first;
            now = kept.now;
            state = &raw;
        }
    }
    PutBackState(*state);
    machine->audio.clear();
    r.at = from;
    r.now = now;
    r.skip = static_cast<int>(target - from);
    r.taken[0] = r.taken[1] = r.startHeld[0] = r.startHeld[1] = false;
    r.takenState.clear();
    r.over = false;
    r.outOfStep = false;
}

bool Port::ReplayRetry() {
    Replay &r = replay;
    if (!r.playing || r.takenState.empty()) {
        return false;
    }
    PutBackState(r.takenState);
    machine->audio.clear();
    r.at = r.takenAt;
    r.now = r.takenNow;
    r.skip = 0;
    for (int p = 0; p < 2; p++) {
        r.startHeld[p] = r.taken[p] && (pad[p] & kStart);
    }
    return true;
}

void Port::ReplayControls() {
    Replay &r = replay;
    const uint16_t held[2] = {pad[0], pad[1]};
    uint16_t pressed[2] = {static_cast<uint16_t>(held[0] & ~r.lastPads[0]), static_cast<uint16_t>(held[1] & ~r.lastPads[1])};
    r.lastPads[0] = held[0];
    r.lastPads[1] = held[1];
    if (!r.playing || menu.IsOpen() || r.skip > 0) {
        return;
    }
    for (int p = 0; p < 2; p++) {
        if ((pressed[p] & kStart) && !r.taken[p]) {
            if (!r.anyTaken()) {
                KeepState(r.takenState);
                r.takenAt = r.at;
                r.takenNow = r.now;
            }
            r.taken[p] = r.startHeld[p] = true;
            r.over = false;
            paused = false;
            steps = 0;
            r.hintUntil = tick + 420;
        }
    }
    if (r.anyTaken()) {
        return; // (the controls are the game's now)
    }
    const uint16_t any = pressed[0] | pressed[1];
    if (any & kDown) {
        paused = !paused;
        steps = 0;
        r.hintUntil = tick + 300;
    } else if (any & kUp) {
        ReplaySeek(0);
        paused = false;
    } else if (any & kLeft) {
        ReplaySeek(static_cast<int64_t>(r.at) - (paused ? 1 : Replay::kJump));
    } else if (any & kRight) {
        if (paused) {
            steps++;
        } else {
            r.skip = static_cast<int>(std::min<uint32_t>(Replay::kJump, r.play.frames - std::min(r.at, r.play.frames)));
        }
    }
}

std::string Port::ReplayLine(int which) const {
    const Replay &r = replay;
    if (!r.playing) {
        return {};
    }
    if (which == 0) {
        if (r.anyTaken()) {
            const char *who = r.taken[0] && r.taken[1] ? "BOTH PLAYERS" : r.taken[0] ? "PLAYER 1" : "PLAYER 2";
            return tick < r.hintUntil ? std::string("PLAYING ON FROM THE REPLAY AS ") + who : std::string();
        }
        std::string line = "REPLAY " + Replay::Length(r.at) + " / " + Replay::Length(r.play.frames);
        if (r.over) {
            line += "  OVER";
        }
        if (r.outOfStep) {
            line += "  OUT OF STEP";
        }
        return line;
    }
    if (r.anyTaken()) {
        return tick < r.hintUntil ? "RESTART KEY: THIS MOMENT AGAIN  MENU: STOP" : "";
    }
    if (tick < r.hintUntil || paused) {
        return paused ? "LEFT RIGHT: A FRAME  DOWN: GO ON  START: PLAY" : "LEFT RIGHT: 5 S  DOWN: STOP  UP: OVER  START: PLAY";
    }
    return {};
}

} // namespace port
