// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp's fight/session.cpp.
#include "session.h"

#include "recomp.h"
#include "state.h"

#include <algorithm>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cps3recomp::session {

namespace {
    Description d;

    constexpr int kRefillAfter = 90; // frames without a hit after which health comes back
    constexpr int kLogNoteFor = 150; // frames the frame data log's note stays up after it writes
    constexpr uint16_t kSticks = kLeft | kRight | kUp | kDown;
    constexpr uint16_t kAllButtons = kPunch1 | kPunch2 | kPunch3 | kKick1 | kKick2 | kKick3;

    fight::Frames g_frames;
    fight::MoveLog g_log;
    fight::InputHistory g_inputs;
    fight::Combos g_combos;
    fight::Dummy g_dummy;
    bool g_fighting = false;
    // Each player has been free (fight::Doing::Free) at a step since the hit before: the next hit starts a new combo. (Not
    // what the player is doing as the damage is taken: a game may take a hit's damage a step after its contact, when the
    // player is reeling from that hit already, and then every hit read as part of a combo.)
    bool g_freeSinceHit[2] = {true, true};
    // The hit confirm drill (hit_confirm_drill): the dummy guards at random, and each of player 1's attacks that
    // touches it first (nothing had for a moment) is judged by what player 1 did next: gone on to another move after a
    // hit, or not after a guarded one, is right. Judged when player 1 has been free a few steps (a move that links is
    // begun by then) or after a second and a half. Not kept in a save state: a state loaded in the middle of one
    // attack loses that attack's judgement, no more.
    struct Confirm {
        static constexpr int kQuiet = 40, kFree = 14, kLongest = 90, kSaid = 150;
        bool on = false, open = false, guarded = false, followed = false, past = false;
        int age = 0, free = 0, quiet = kQuiet, said = 0, result = 0; // result: 1 confirmed, 2 held back, 3 hit and no more, 4 guarded and went on
        int right = 0, tried = 0;
    } g_confirm;
    void ConfirmContact(bool guarded) {
        Confirm &c = g_confirm;
        if (c.on && !c.open && c.quiet >= Confirm::kQuiet) {
            c.open = true;
            c.guarded = guarded;
            c.followed = c.past = false;
            c.age = c.free = 0;
        }
        c.quiet = 0;
    }
    void ConfirmFrame(std::vector<HudLine> &lines) {
        Confirm &c = g_confirm;
        const bool on = FeatureOn("hit_confirm_drill");
        if (on != c.on) {
            c = {};
            c.on = on;
        }
        if (!on) {
            return;
        }
        c.quiet = std::min(c.quiet + 1, Confirm::kQuiet);
        if (c.open) {
            const fight::Doing one = g_frames.Now(0);
            c.age++;
            // (another move: one beginning, or one out again once the first was over: a move cancelled into has no
            // beginning to see)
            c.followed = c.followed || (c.age > 1 && one == fight::Doing::Startup) || (c.past && one == fight::Doing::Active);
            c.past = c.past || one == fight::Doing::Recovery || one == fight::Doing::Free;
            c.free = one == fight::Doing::Free ? c.free + 1 : 0;
            if (c.followed || c.free >= Confirm::kFree || c.age >= Confirm::kLongest) {
                c.open = false;
                c.result = c.guarded ? (c.followed ? 4 : 2) : (c.followed ? 1 : 3);
                c.tried++;
                c.right += c.result <= 2;
                c.said = Confirm::kSaid;
            }
        }
        char text[48];
        snprintf(text, sizeof(text), "HIT CONFIRM %d / %d", c.right, c.tried);
        lines.push_back({0.5f, 0.22f, 0, text});
        if (c.said > 0) {
            c.said--;
            static const char *const kResults[5] = {"", "CONFIRMED", "GUARDED: HELD BACK", "HIT: NO FOLLOW-UP", "GUARDED: WENT ON"};
            const bool right = c.result <= 2;
            lines.push_back({0.5f, 0.285f, 0, kResults[c.result], static_cast<uint8_t>(right ? 96 : 255), static_cast<uint8_t>(right ? 230 : 110),
                             static_cast<uint8_t>(right ? 120 : 96)});
        }
    }
    // Getting to a session from switching on: coins while there are fewer than two credits (and no free play), then
    // both Start buttons until both players are choosing (the game runs unseen meanwhile)
    bool g_boot = false;
    int g_bootFrames = 0;
    constexpr int kBootMost = 4000; // frames after which it gives up
    // The machine as it was when the round's fighting began, for starting over
    std::vector<uint8_t> g_roundStart;
    bool g_roundFought = false;
    bool g_choosingTwo = false; // player 1's controls are choosing player 2's character
    uint16_t g_heldOver = 0;    //   and what it held as it chose for player 1, not passed on until let go

    int X(int n) {
        return static_cast<int16_t>(Ram16(d.player[n] + d.x));
    }

    void Afresh(bool fighting) {
        g_fighting = fighting;
        g_inputs.Clear();
        g_combos.Clear();
        g_freeSinceHit[0] = g_freeSinceHit[1] = true;
        g_dummy.Afresh();
        g_frames.Reset();
    }

    bool Fighting() {
        return d.fighting && d.fighting();
    }

    // The frame data log's note, for a few seconds after each move it writes: where the file is (the log shows nothing
    // else on the screen)
    void LogNote(const Machine &machine, std::vector<HudLine> &lines) {
        static uint32_t seen = 0;
        static uint64_t until = 0;
        if (g_log.Written() != seen) {
            seen = g_log.Written();
            until = machine.frame + kLogNoteFor;
        }
        if (!FeatureOn("frame_log") || g_log.Path().empty() || machine.frame >= until) {
            return;
        }
        std::string path = g_log.Path();
        const char *home = getenv("HOME");
        if (home && *home && path.compare(0, strlen(home), home) == 0) {
            path = "~" + path.substr(strlen(home));
        }
        char count[32];
        snprintf(count, sizeof(count), " (%u moves)", seen);
        lines.push_back({0.5f, 0.95f, 0, "frame data saved to " + path + count, 150, 255, 150});
    }

    // (debugging aid, CPS3RECOMP_HUD_LOG=1: the HUD's lines each time they change, for the checks to read)
    void LogHud(const Machine &machine, const std::vector<HudLine> &lines) {
        static const bool log = getenv("CPS3RECOMP_HUD_LOG") != nullptr;
        static std::vector<HudLine> last;
        if (!log || RunningAhead() || lines == last) {
            return;
        }
        last = lines;
        fprintf(stderr, "hud: frame %llu:", static_cast<unsigned long long>(machine.frame));
        for (const HudLine &line : lines) {
            fprintf(stderr, " [%s]", line.text.c_str());
        }
        fprintf(stderr, "\n");
    }

    void Boot(Machine &machine) {
        uint16_t &one = machine.players[0], &two = machine.players[1];
        const bool press = g_bootFrames % 16 < 6;
        one = two = 0;
        if ((d.joined(0) && d.joined(1)) || ++g_bootFrames > kBootMost) {
            g_boot = false;
            SetFastForward(false);
        } else if (d.credits && Ram8(d.credits) < 2 && !FeatureOn("free_play")) {
            one = press ? kCoin : 0;
        } else if (!d.joined(0)) {
            one = press ? kStart : 0;
        } else {
            two = press ? kStart : 0;
        }
    }

    // "Start over": the round as it began, in the middle (where it began) or a corner, the sides as they began or
    // swapped. The stick held as it is asked for chooses (and it is remembered): left or right a corner, up the middle,
    // down swaps the sides.
    void StartOver(Machine &machine) {
        if (getenv("CPS3RECOMP_SESSION_LOG")) { // (debugging aid)
            fprintf(stderr, "session: start over asked at frame %llu (round kept: %s, in a session: %s)\n",
                    static_cast<unsigned long long>(machine.frame), g_roundStart.empty() ? "no" : "yes", InSession() ? "yes" : "no");
        }
        if (g_roundStart.empty() || !InSession()) {
            return;
        }
        int place = FeatureValue("restart_at"); // 0 middle, 1 swapped, 2 left corner, 3 right corner, +2: swapped
        int where = place <= 1 ? 0 : (place - 2) % 2 + 1; // 0 middle, 1 left, 2 right
        bool swapped = place == 1 || place >= 4;
        const uint16_t stick = machine.players[0] & kSticks;
        if (stick & kUp) {
            where = 0;
            swapped = false;
        }
        if (d.corners && (stick & kLeft)) {
            where = 1;
        } else if (d.corners && (stick & kRight)) {
            where = 2;
        }
        if (stick & kDown) {
            swapped = !swapped;
        }
        place = where == 0 ? (swapped ? 1 : 0) : 1 + where + (swapped ? 2 : 0);
        SetFeatureValue("restart_at", place);
        Archive ar(g_roundStart, false);
        machine.Serialize(ar);
        StateLoaded(); // (the translated code's dispatcher: where the CPU was is forgotten)
        int x[2] = {X(0), X(1)};
        int left = 0, right = 0;
        if (where && d.corners && d.corners(left, right)) {
            x[0] = where == 1 ? left : right;
            x[1] = where == 1 ? left + d.cornerApart : right - d.cornerApart;
        }
        if (swapped) {
            std::swap(x[0], x[1]);
        }
        SetRam16(d.player[0] + d.x, static_cast<uint16_t>(x[0]));
        SetRam16(d.player[1] + d.x, static_cast<uint16_t>(x[1]));
        Afresh(true);
    }

    void Dummy(Machine &machine) {
        fight::DummyView view;
        view.fighting = Fighting() && g_fighting;
        view.doing = g_frames.Now(1);
        view.onRight = X(1) >= X(0);
        view.sinceHit = g_combos.Since(1);
        if (view.fighting && (FeatureValue("dummy_guard") || FeatureOn("hit_confirm_drill"))) {
            if (d.guardView) {
                d.guardView(machine, view.attackInRange, view.guardStanding);
            } else {
                const fight::Doing one = g_frames.Now(0);
                view.attackInRange = one == fight::Doing::Startup || one == fight::Doing::Active;
            }
        }
        g_dummy.BeforeFrame(machine.players[0], machine.players[1], view);
    }

    void Start(Machine &machine) {
        StartSession(machine);
    }
} // namespace

namespace {
    uint32_t g_starts = 0;
}
uint32_t Starts() {
    return g_starts;
}

void StartSession(Machine &machine) {
    g_starts++;
    SetFeature("training_mode", true);
    machine.Reset();
    StateLoaded();
    g_roundStart.clear();
    g_roundFought = false;
    g_boot = true;
    g_bootFrames = 0;
    SetFastForward(true);
}

bool Booting() {
    return g_boot;
}

bool InSession() {
    return FeatureOn("training_mode");
}
bool NoClock() {
    return InSession();
}
bool NoKnockOut() {
    return InSession();
}

void Step(int player, const fight::StepView &view) {
    g_frames.Step(player, view);
    if (g_frames.Now(player) == fight::Doing::Free) {
        g_freeSinceHit[player] = true;
    }
    if (d.moveStart && FeatureOn("frame_log")) {
        g_log.Step(player, g_frames, d.moveStart);
    }
}
void Contact(int victim, bool guarded) {
    g_frames.Contact(victim, guarded);
    if (victim == 1) {
        ConfirmContact(guarded);
    }
}
void Hit(int player, unsigned damage, bool chip) {
    // A combo goes on while the player has not got free since the last hit (a guarded hit's chip ends one)
    g_combos.Hit(player, damage, 0, !chip && !g_freeSinceHit[player]);
    g_freeSinceHit[player] = chip;
}
bool Chance(int percent) {
    return g_dummy.Chance(percent);
}
fight::Doing Now(int player) {
    return g_frames.Now(player);
}
fight::Frames::Move LastMove(int player) {
    return g_frames.LastMove(player);
}

void BeforeFrame(Machine &machine) {
    if (g_boot) {
        Boot(machine);
        return;
    }
    if (InSession() && d.choosing && d.choosing() && d.chosen && d.chosen(0) && !d.chosen(1)) {
        // Player 1 has chosen: its controls choose for player 2. What was held as it chose is not passed on until let
        // go, or the button that chose for player 1 would choose for player 2 at once.
        const uint16_t held = machine.players[0] & (kSticks | kAllButtons);
        const uint16_t heldOver = g_choosingTwo ? g_heldOver & held : held;
        if (!RunningAhead()) {
            g_heldOver = heldOver;
            g_choosingTwo = true;
        }
        machine.players[1] |= held & ~heldOver;
        machine.players[0] &= kStart | kCoin;
        return;
    }
    if (!RunningAhead()) {
        g_choosingTwo = false;
    }
    if (InSession()) {
        Dummy(machine);
    }
}

void Frame(Machine &machine) {
    const bool fighting = Fighting();
    // The round's fighting begins: kept, for starting over
    if (fighting && !g_roundFought && InSession()) {
        g_roundStart.clear();
        Archive ar(g_roundStart, true);
        machine.Serialize(ar);
    }
    g_roundFought = fighting;
    if (!fighting || !g_fighting) {
        Afresh(fighting);
        SetHud({});
        SetBoxes({});
        return;
    }
    g_combos.Frame();
    std::vector<HudLine> lines;
    std::vector<HudBox> boxes;
    if (d.aidsOff && d.aidsOff()) {
        if (d.hud) {
            d.hud(machine, lines, boxes);
        }
        SetBoxes(std::move(boxes));
        SetHud(std::move(lines));
        return;
    }
    const bool readout = FeatureOn("damage_readout"), refill = FeatureOn("health_refill"), inputs = FeatureOn("input_display");
    for (int n = 0; n < 2; n++) {
        const uint8_t health = Ram8(d.player[n] + d.health);
        if (refill && g_combos.Since(n) > kRefillAfter && health < d.healthFull && g_frames.Now(n) == fight::Doing::Free) {
            SetRam8(d.player[n] + d.health, d.healthFull);
        }
        if (readout) {
            g_combos.Hud(n, lines, 0.17f, true, {}, false);
        }
        {
            g_inputs.Push(n, d.held ? d.held(n) : machine.players[n], d.facingLeft && d.facingLeft(n));
            if (inputs) {
                g_inputs.Hud(n, lines, d.inputY, d.inputSpacing);
            }
        }
    }
    g_frames.Hud(FeatureOn("frame_data"), FeatureOn("frame_meter"), lines, boxes, d.framesY, d.framesTop, machine.PictureWidth(), 0);
    LogNote(machine, lines);
    ConfirmFrame(lines);
    if (d.hud) {
        d.hud(machine, lines, boxes);
    }
    SetBoxes(std::move(boxes));
    g_dummy.Hud(lines, 0.17f);
    LogHud(machine, lines);
    SetHud(std::move(lines));
}

void State(Archive &ar) {
    ar.Mark(0x5452414E); // TRAN
    ar.Pod(g_fighting);
    ar.Pod(g_combos);
    ar.Pod(g_freeSinceHit);
    ar.Pod(g_inputs);
    g_dummy.Serialize(ar);
    ar.Vec(g_roundStart);
    ar.Pod(g_roundFought);
    g_frames.Serialize(ar);
    ar.Pod(g_boot);
    ar.Pod(g_bootFrames);
}

void Register(const Description &description) {
    d = description;
    g_frames.print = getenv("CPS3RECOMP_FRAMES") != nullptr;
    Game &game = TheGame();
    game.commands.push_back({"training_start", kPage, "Start",
                             "Starts a training session: the game starts over and goes to the character choice for two players. "
                             "Player 1's controls choose both characters; player 2 is the dummy.",
                             Start});
    game.commands.push_back({"training_restart", kPage, "Start over",
                             "In a training session: the round as it began. Backspace or a gamepad's left shoulder does this while "
                             "playing; the stick held as it is pressed chooses where: left or right a corner, up the middle, down "
                             "swaps the sides.",
                             StartOver, kKeyRestart});
    if (d.corners) {
        game.features.push_back({"restart_at", kPage, "Start over at",
                                 "Where \"Start over\" puts the players: where the round began or a corner, player 1 on its own side or "
                                 "swapped.",
                                 0, {"As the round began", "As it began, swapped", "Left corner", "Right corner", "Left corner, swapped",
                                     "Right corner, swapped"}});
    } else {
        game.features.push_back({"restart_at", kPage, "Start over with", "The players on their own sides, or swapped.", 0,
                                 {"Sides as they began", "Sides swapped"}});
    }
    game.features.push_back({"training_mode", kPage, "Training session",
                             "The fight has no clock and nobody is knocked out. (\"Start\" above switches this on and takes the game to "
                             "the character choice; Reset ends it.)",
                             kFeatureSession});
    game.features.push_back({"damage_readout", kPage, "Combo readout",
                             "Under the bar of the player hit: what the hit took off; for a combo (hits landing before the player is "
                             "free again) the hits and the damage; the round's best."});
    {
        game.features.push_back({"input_display", kPage, "Input display",
                                 "Shows what each player presses and for how many frames: the stick as a keypad number from the "
                                 "player's side (6 forward, 4 back, 2 down), then the buttons; nothing for the stick left in the "
                                 "middle."});
    }
    game.features.insert(game.features.end(), d.features.begin(), d.features.end());
    game.features.push_back({"frame_data", kPage, "Frame data",
                             "After a move: the step it can first hit on (startup), how many it can hit for (active) and how many follow "
                             "before the player can act (recovery). After a hit or a guarded one: who can act first, by how many steps "
                             "(+: the attacker)."});
    if (d.moveStart) {
        game.features.push_back({"frame_log", kPage, "Frame data log",
                                 "Writes every move's frame data to frame_data.csv, in the game's own folder beside settings.ini "
                                 "(~/Library/Application Support/<the game> on a Mac, ~/.local/share/<the game> on Linux and the Steam "
                                 "Deck, %APPDATA%\\<the game> on Windows, user/ beside the launcher when it is portable): the character, "
                                 "what was held as it began, startup, active, recovery and the advantage after a hit or a guarded one. "
                                 "Nothing is shown in the fight but a note at the bottom of the screen, saying where, as each move is "
                                 "written."});
    }
    game.features.push_back({"frame_meter", kPage, "Frame meter",
                             "A row of cells for each player, one for every step of the game: green before a move can hit, red while it "
                             "can, blue after; yellow reeling from a hit, light blue from a guarded one; grey in the air."});
    game.features.push_back({"health_refill", kPage, "Health refills", "A player's health comes back when they have not been hit for a moment."});
    game.features.push_back({"hit_confirm_drill", kPage, "Hit confirm drill",
                             "The dummy guards about half your attacks, at random. Each attack that touches it is judged by "
                             "what you did next: going on into another move after a hit, or holding back after a guarded one, "
                             "is right. The score is at the top of the screen; switching the drill off and on starts it again."});
    fight::RegisterDummy(d.dummy);
    for (const char *name : {"damage_readout", "input_display", "frame_data", "frame_log", "frame_meter", "health_refill", "hit_confirm_drill"}) {
        TrainingAids({name});
    }
    for (const char *name : d.ownAids) {
        TrainingAids({name});
    }
    game.inTraining = [] { return InSession(); };
    game.fighting = [] { return d.fighting && d.fighting(); };
}

} // namespace cps3recomp::session
