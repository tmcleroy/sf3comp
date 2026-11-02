// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's training mode: the toolkit's session (fight/session.h) told where 3rd Strike keeps things, and what each
// player does each frame, read from the players' records after the frame: a move or not, reeling, thrown, in the air;
// hits and guards as the records count them; damage as the health goes down. Hit boxes through the game's camera, and
// the super art meter kept full.
//
// Read once a frame, not at the game's own steps: a move's numbers are counted in frames, which is what 3rd Strike's
// published frame data counts too. Not compared with that data yet.
#include "fight.h"
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace cps3recomp {

namespace {
    struct State {
        uint16_t moveAnim[2] = {};  // (the frame data log: each move's animation, from its first step)
        uint32_t loggedAt[2] = {};
        uint16_t hits[2] = {}, marker[2] = {};
        uint8_t health[2] = {}, blocking[2] = {}, freeze[2] = {};
        bool reeling[2] = {}, guarding[2] = {};
        // (the parry readout: each player's last window opened, and what came of it)
        struct Parry {
            uint8_t window[4] = {};
            bool armed = false;
            int8_t kind = -1;        // which window
            int16_t result = 0;      // 1 parried, 2 the wrong window, 3 too early, 4 too late, 0 none yet
            int16_t frames = 0;      // ... how many frames into the window, or early or late by
            uint32_t opened = 0, closed = 0, lastHit = 0, shownUntil = 0;
            uint16_t connection = 0;
        } parry[2];
        uint32_t frame = 0;
        uint32_t parryTap = 0; // (the dummy's parry: the frame it last tapped)
        // (... and for each of player 1's animations (by character), its frames until its attack box comes out, so that the
        // next time it taps just before it can hit; and when player 1's move now started)
        struct Learned {
            uint16_t anim = 0;
            uint8_t who = 0, startup = 0;
        } learned[256];
        uint32_t moveFrom[2] = {};
        bool moveLow[2] = {}; // (made crouching: down held on the floor as it started)
        // (by animation: player 1's now, since when, and whether its attack box has come out in it yet)
        uint16_t anim = 0;
        uint32_t animFrom = 0;
        bool animBox = false;
        uint32_t learnedAt = 0;
    } g;

    // A move's startup as learned (0: not seen yet)
    int LearnedStartup(uint8_t who, uint16_t anim) {
        for (uint32_t n = 0, at = anim % std::size(g.learned); n < std::size(g.learned); n++, at = (at + 1) % std::size(g.learned)) {
            const auto &one = g.learned[at];
            if (one.startup == 0) {
                return 0;
            }
            if (one.anim == anim && one.who == who) {
                return one.startup;
            }
        }
        return 0;
    }
    void Learn(uint8_t who, uint16_t anim, int startup) {
        if (startup <= 0 || startup > 255) {
            return;
        }
        for (uint32_t n = 0, at = anim % std::size(g.learned); n < std::size(g.learned); n++, at = (at + 1) % std::size(g.learned)) {
            auto &one = g.learned[at];
            if (one.startup == 0 || (one.anim == anim && one.who == who)) {
                one = {anim, who, static_cast<uint8_t>(startup)};
                return;
            }
        }
    }

    int16_t Signed16(uint32_t address) {
        return static_cast<int16_t>(Ram16(address));
    }
    uint32_t Player(int p) {
        return sfiii3::kPlayer[p];
    }
    bool InMatch() {
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2;
    }
    bool Guarding(int p) {
        const uint8_t b = Ram8(Player(p) + sfiii3::kBlocking);
        return b > 0 && b < 5;
    }
    // Free to act: not attacking, guarding, thrown, busy or frozen, and taking input (the training script's "idle")
    bool Idle(int p) {
        const uint32_t base = Player(p);
        return Ram8(base + sfiii3::kAttacking) == 0 && !Guarding(p) && Ram8(base + sfiii3::kThrown) == 0 &&
               (Ram16(base + sfiii3::kBusy) & 0xFF) == 0 && Ram8(base + sfiii3::kFreeze) == 0 && Ram16(base + sfiii3::kInputCapacity) > 0;
    }
    // A box list's first box that is not empty
    bool AnyBox(int p, uint32_t offset, int count) {
        const uint32_t at = Ram32(Player(p) + offset);
        if (!at) {
            return false;
        }
        for (int n = 0; n < count; n++) {
            if (Peek32(at + n * 8) || Peek32(at + n * 8 + 4)) {
                return true;
            }
        }
        return false;
    }

    void Boxes(std::vector<HudBox> &boxes) {
        const int cameraX = Signed16(sfiii3::kCameraX), cameraY = Signed16(sfiii3::kCameraY);
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            const int px = Signed16(base + sfiii3::kX) - cameraX + Video::kWidth / 2;
            const int py = Video::kHeight - (Signed16(base + sfiii3::kY) - cameraY) - 23;
            const bool flipped = Ram8(base + sfiii3::kFacing) != 0;
            for (const sfiii3::BoxList &list : sfiii3::kBoxes) {
                const uint32_t at = Ram32(base + list.offset);
                if (!at) {
                    continue; // (no list)
                }
                for (int n = 0; n < list.count; n++) {
                    const uint32_t box = at + n * 8;
                    // (the lists are in main RAM or the program's tables)
                    const int left = static_cast<int16_t>(Peek16(box)), width = static_cast<int16_t>(Peek16(box + 2));
                    const int bottom = static_cast<int16_t>(Peek16(box + 4)), height = static_cast<int16_t>(Peek16(box + 6));
                    if (!left && !width && !bottom && !height) {
                        continue;
                    }
                    const int x = flipped ? px - left - width : px + left;
                    static constexpr uint8_t kColour[5][3] = {{60, 120, 255}, {255, 40, 40}, {60, 220, 60}, {255, 230, 40}, {255, 60, 255}};
                    const uint8_t *c = kColour[list.kind];
                    boxes.push_back({x, py - bottom - height, width, height, c[0], c[1], c[2]});
                }
            }
        }
    }

    // The parry readout. A parry in a window says which of its frames it came on; a hit or guard arriving while a window
    // is open but not parried, the wrong window; a hit within 20 frames after a window closed, too early by so many; a
    // window opened within 20 frames after a hit, too late by so many.
    void ReadParries(bool hit[2], bool guarded[2]) {
        static const char *const kKinds[4] = {"forward", "down", "air", "anti-air"};
        static const bool log = getenv("SFIII3_PARRIES") != nullptr;
        for (int p = 0; p < 2; p++) {
            State::Parry &parry = g.parry[p];
            const uint16_t connection = Ram16(Player(p) + sfiii3::kConnection);
            const bool parried = parry.connection != 0xFFF1 && connection == 0xFFF1 && !hit[p];
            parry.connection = connection;
            const bool struck = hit[p] || guarded[p];
            auto say = [&](int result, int frames) {
                parry.result = static_cast<int16_t>(result);
                parry.frames = static_cast<int16_t>(frames);
                parry.armed = false;
                parry.shownUntil = g.frame + 120;
                if (log) {
                    static const char *const kResults[5] = {"", "parried", "wrong window", "too early", "too late"};
                    fprintf(stderr, "parry: player %d %s %s, %d frames\n", p + 1, kKinds[parry.kind], kResults[result], frames);
                }
            };
            bool open = false;
            for (int k = 0; k < 4; k++) {
                const uint8_t now = Ram8(sfiii3::kParryWindow[k] + p * sfiii3::kParryWindowPlayer2);
                if (parry.window[k] == 0 && now != 0) {
                    parry.kind = static_cast<int8_t>(k);
                    parry.opened = g.frame;
                    parry.armed = true;
                    if (parry.lastHit && g.frame - parry.lastHit <= 20 && !parried) {
                        say(4, static_cast<int>(g.frame - parry.lastHit));
                    }
                }
                parry.window[k] = now;
                open = open || now != 0;
            }
            if (struck) {
                parry.lastHit = g.frame;
            }
            if (parried && parry.kind >= 0) {
                say(1, static_cast<int>(g.frame - parry.opened));
                parry.closed = 0;
                continue;
            }
            if (!parry.armed) {
                continue;
            }
            if (open) {
                parry.closed = 0;
                if (struck) {
                    say(2, static_cast<int>(g.frame - parry.opened));
                }
            } else {
                if (!parry.closed) {
                    parry.closed = g.frame;
                }
                if (struck) {
                    say(3, static_cast<int>(g.frame - parry.closed));
                } else if (g.frame - parry.closed > 20) {
                    parry.armed = false; // (nothing came: forgotten)
                }
            }
        }
    }

    // After every frame: what each player did, told to the session
    void ReadSteps() {
        g.frame++;
        if (!InMatch()) {
            for (int p = 0; p < 2; p++) {
                g.hits[p] = Ram16(Player(p) + sfiii3::kHitCount);
                g.health[p] = Ram8(Player(p) + sfiii3::kLife);
                g.reeling[p] = g.guarding[p] = false;
            }
            return;
        }
        // First what landed this frame on each player (the attacker's move connected this step, whatever its boxes say:
        // a hit can take the box away the frame it lands)
        bool landed[2] = {}, wasHit[2] = {}, wasGuarded[2] = {};
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            const uint16_t hits = Ram16(base + sfiii3::kHitCount);
            const bool hit = hits != g.hits[p];
            const bool guardStarted = Guarding(p) && !g.guarding[p];
            g.hits[p] = hits;
            if (hit) {
                if (getenv("SFIII3_STEPS")) {
                    fprintf(stderr, "contact: player %d hit\n", p + 1);
                }
                g.reeling[p] = true;
                session::Contact(p, false);
            } else if (guardStarted) {
                session::Contact(p, true);
            }
            landed[1 - p] = hit || guardStarted;
            wasHit[p] = hit;
            wasGuarded[p] = guardStarted;
            g.guarding[p] = Guarding(p);
            const uint8_t health = Ram8(base + sfiii3::kLife);
            if (health < g.health[p]) {
                session::Hit(p, g.health[p] - health, g.guarding[p] && !hit);
                // (debugging aid, SFIII3_PARRY_MISSES=1: each hit player 2 takes, with what it knew of player 1's move)
                static const bool misses = getenv("SFIII3_PARRY_MISSES") != nullptr;
                if (misses && p == 1) {
                    fprintf(stderr, "miss: anim %04X learned %d into %u low %d thrown %d tapped %u ago\n", g.anim,
                            LearnedStartup(static_cast<uint8_t>(Ram16(Player(0) + 0x3C0)), g.anim), g.frame - g.animFrom, g.moveLow[0],
                            Ram8(Player(1) + sfiii3::kThrown), g.frame - g.parryTap);
                }
            }
            g.health[p] = health;
        }
        ReadParries(wasHit, wasGuarded);
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            const bool idle = Idle(p);
            if (idle && !wasHit[p]) { // (the frame a hit lands the record still reads as it was: reeling from the next)
                g.reeling[p] = false;
            }
            fight::StepView view;
            const bool thrown = Ram8(base + sfiii3::kThrown) != 0;
            view.control = thrown ? fight::StepView::Held : (g.reeling[p] || g.guarding[p]) ? fight::StepView::Reeling : fight::StepView::Own;
            view.canAct = idle;
            view.frozen = Ram8(base + sfiii3::kFreeze) != 0;
            view.attacking = Ram8(base + sfiii3::kAttacking) != 0;
            view.jumping = Signed16(base + sfiii3::kY) > 0;
            view.attackOut = AnyBox(p, 0x2C8, 4);
            (void)landed;
            // (debugging aid, SFIII3_STEPS=1: what is read of each player, each frame)
            static const bool printSteps = getenv("SFIII3_STEPS") != nullptr;
            static const int stepsOf = printSteps ? atoi(getenv("SFIII3_STEPS")) - 1 : 0; // (1: player 1's, 2: player 2's)
            if (printSteps && p == std::max(stepsOf, 0)) {
                fprintf(stderr, "step: control %d canAct %d frozen %d attacking %d jumping %d attackOut %d anim %04X -> %d\n", view.control,
                        view.canAct, view.frozen, view.attacking, view.jumping, view.attackOut, Ram16(base + 0x202), static_cast<int>(session::Now(p)));
            }
            const fight::Doing before = session::Now(p);
            session::Step(p, view);
            // Each move's animation as it starts, and its startup once measured (the dummy's parry learns them; and a
            // debugging aid, SFIII3_MOVES=1, prints each move's character, animation, startup, active and recovery as
            // measured, for tools/framecheck.py to compare with the training script's recorded animations)
            if (before == fight::Doing::Free && session::Now(p) != fight::Doing::Free) {
                g.moveAnim[p] = Ram16(base + 0x202);
                g.moveFrom[p] = g.frame;
                const Machine *m = TheMachine();
                g.moveLow[p] = m && (m->players[p] & kDown) && Signed16(base + sfiii3::kY) <= 0;
            }
            // (by animation: each of player 1's animations' frames until its attack box comes out, learned the first
            // time it does; so chained and cancelled moves are known too)
            if (p == 0) {
                const uint16_t anim = Ram16(base + 0x202);
                if (anim != g.anim) {
                    g.anim = anim;
                    g.animFrom = g.frame;
                    g.animBox = view.attackOut;
                }
                if (view.attackOut && !g.animBox) {
                    g.animBox = true;
                    Learn(static_cast<uint8_t>(Ram16(base + 0x3C0)), anim, static_cast<int>(g.frame - g.animFrom));
                }
            }
            const fight::Frames::Move move = session::LastMove(p);
            if (move.at && move.at != g.loggedAt[p]) {
                g.loggedAt[p] = move.at;
                static const bool logMoves = getenv("SFIII3_MOVES") != nullptr;
                if (logMoves) {
                    printf("move %d %u %04x %u %u %u\n", p, Ram16(base + 0x3C0), g.moveAnim[p], move.startup, move.active, move.recovery);
                }
            }
        }
    }

    bool FullMeter(int p) {
        return FeatureOn(p == 0 ? "full_meter" : "full_meter_2");
    }
} // namespace

void WidescreenState(Archive &ar);
void SurvivalState(Archive &ar);
void SurvivalHud(std::vector<HudLine> &lines);

void TrainingBeforeFrame(Machine &m) {
    if (InMatch()) {
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            const uint8_t most = Ram8(sfiii3::kMaxMeterCount[p]);
            if (FullMeter(p) && most && Ram8(sfiii3::kMeterCount[p]) != most && Ram8(sfiii3::kMeterCountShown[p]) != most) {
                SetRam8(sfiii3::kGauge[p], 0);
                SetRam8(sfiii3::kMeterCount[p], most);
                SetRam8(sfiii3::kMeterUpdate[p], 1);
            }
            // (no stun: what it has taken kept at none; one hit: health below full in a round is none)
            if (FeatureOn(p == 0 ? "no_stun" : "no_stun_2")) {
                SetRam32(sfiii3::kStunMost[p] + 6, 0);
            }
            if (FeatureOn("one_hit") && Ram8(base + sfiii3::kLife) < sfiii3::kFullLife && Ram8(base + sfiii3::kLife) > 0) {
                SetRam8(base + sfiii3::kLife, 0);
            }
            if (FeatureOn(p == 0 ? "no_damage" : "no_damage_2")) {
                SetRam8(base + sfiii3::kLife, sfiii3::kFullLife);
                g.health[p] = sfiii3::kFullLife;
            }
            // (nobody knocked out in a session: health back to full below a quarter, until the game's knock-out has a
            // hook)
            if (session::NoKnockOut() && Ram8(base + sfiii3::kLife) < sfiii3::kFullLife / 4) {
                SetRam8(base + sfiii3::kLife, sfiii3::kFullLife);
                g.health[p] = sfiii3::kFullLife;
            }
        }
        if (session::NoClock()) {
            SetRam8(sfiii3::kClock, 99);
        }
    }
    // (a session's stage: set while no round is on, so the next fight's is the one chosen)
    if (session::InSession() && !InMatch()) {
        const int stage = FeatureValue("training_stage");
        if (stage > 0) {
            SetRam8(sfiii3::kStage, static_cast<uint8_t>(stage - 1));
        }
    }
    session::BeforeFrame(m);
    // Played by the game: player 2's record says the computer works it (byte 3: 0 the computer, 1 a player; seen in
    // RAM compared between one- and two-player fights), set while a round is on
    // (and player 1's too, to watch the game play itself)
    if (session::InSession() && InMatch()) {
        const bool watch = FeatureOn("training_watch");
        for (int p = 0; p < 2; p++) {
            const bool computer = watch || (p == 1 && FeatureOn("dummy_computer"));
            const uint8_t now = Ram8(Player(p) + 3);
            if (computer && now != 0) {
                SetRam8(Player(p) + 3, 0);
            } else if (!computer && now == 0) {
                SetRam8(Player(p) + 3, 1);
            }
        }
    }
    // Stunned (the stun timer, two bytes on from the most it takes, counts down from 130 or so), the dummy mashes: the
    // stick round and the buttons, a different one each frame
    if (session::InSession() && FeatureOn("dummy_stun_mash") && InMatch() && Ram8(sfiii3::kStunMost[1] + 2) != 0) {
        static const uint16_t kMash[] = {kLeft | kPunch1, kDown | kKick1, kRight | kPunch2, kUp | kKick2, kLeft | kPunch3, kDown | kKick3};
        m.players[1] = (g.frame & 1) ? 0 : kMash[(g.frame / 2) % std::size(kMash)];
    }
    // The dummy parries: as player 1's attack starts, a tap towards player 1 (down for an attack made crouching) opens
    // a parry window of about 10 frames; again no sooner than 18 frames on (the best found against made-up play: 24 missed more)
    if (session::InSession() && FeatureOn("dummy_parry") && InMatch()) {
        const bool onRight = static_cast<int16_t>(Ram16(Player(1) + sfiii3::kX)) >= static_cast<int16_t>(Ram16(Player(0) + sfiii3::kX));
        // (an animation seen to hit before: the tap as it is about to, `lead` frames before; a move not seen yet: as it starts)
        static const int lead = getenv("SFIII3_PARRY_LEAD") ? atoi(getenv("SFIII3_PARRY_LEAD")) : 2;
        const int startup = LearnedStartup(static_cast<uint8_t>(Ram16(Player(0) + 0x3C0)), g.anim);
        const int into = static_cast<int>(g.frame - g.animFrom);
        const bool due = startup ? into >= startup - lead && !g.animBox : session::Now(0) == fight::Doing::Startup && into == 0;
        static const uint32_t again = getenv("SFIII3_PARRY_AGAIN") ? static_cast<uint32_t>(atoi(getenv("SFIII3_PARRY_AGAIN"))) : 18;
        if (due && (g.parryTap == 0 || g.frame - g.parryTap > again)) {
            m.players[1] = g.moveLow[0] ? kDown : (onRight ? kLeft : kRight);
            if (!RunningAhead()) {
                g.parryTap = g.frame;
            }
        } else if (g.parryTap && g.frame - g.parryTap < 2) {
            m.players[1] = 0; // (let go, so the next tap is a fresh one)
        }
    }
}

void TrainingAfterFrame(Machine &m) {
    ReadSteps();
    session::Frame(m);
}

void RegisterTraining(Game &game) {
    session::Description d;
    d.player[0] = sfiii3::kPlayer[0];
    d.player[1] = sfiii3::kPlayer[1];
    d.x = sfiii3::kX;
    d.health = sfiii3::kLife;
    d.healthFull = sfiii3::kFullLife;
    d.credits = sfiii3::kCredits;
    d.fighting = InMatch;
    // (character select: 0 no player, 1 coming in, 2 choosing a character, 3-4 the super art, 5 chosen)
    d.joined = [](int p) { return Ram8(sfiii3::kSelectState[p]) >= 2; };
    d.chosen = [](int p) { return Ram8(sfiii3::kSelectState[p]) >= 3; };
    d.choosing = [] { return !InMatch() && (Ram8(sfiii3::kSelectState[0]) >= 1 || Ram8(sfiii3::kSelectState[1]) >= 1) &&
                             (Ram8(sfiii3::kSelectState[0]) < 5 || Ram8(sfiii3::kSelectState[1]) < 5); };
    d.facingLeft = [](int p) { return Ram8(sfiii3::kPlayer[p] + sfiii3::kFacing) == 0; };
    d.hud = [](Machine &, std::vector<HudLine> &lines, std::vector<HudBox> &boxes) {
        SurvivalHud(lines);
        if (FeatureOn("hit_boxes")) {
            Boxes(boxes);
        }
        if (FeatureOn("parry_readout")) {
            static const char *const kKinds[4] = {"FORWARD", "DOWN", "AIR", "ANTI-AIR"};
            for (int p = 0; p < 2; p++) {
                const State::Parry &parry = g.parry[p];
                char text[64] = "";
                if (parry.result && g.frame < parry.shownUntil && parry.kind >= 0) {
                    if (parry.result == 1) {
                        snprintf(text, sizeof(text), "PARRIED (%s, frame %d of the window)", kKinds[parry.kind], parry.frames + 1);
                    } else if (parry.result == 2) {
                        snprintf(text, sizeof(text), "PARRY: WRONG WINDOW (%s)", kKinds[parry.kind]);
                    } else {
                        snprintf(text, sizeof(text), "PARRY: TOO %s BY %d", parry.result == 3 ? "EARLY" : "LATE", parry.frames);
                    }
                }
                if (*text) {
                    lines.push_back({p == 0 ? 0.02f : 0.98f, 0.26f, p == 0 ? -1 : 1, text, 120, 200, 255});
                }
            }
        }
        if (FeatureOn("stun_readout")) {
            // (under each health bar, at its outer end: taken / the most before a stun)
            for (int p = 0; p < 2; p++) {
                const uint32_t most = Ram8(sfiii3::kStunMost[p]), taken = Ram32(sfiii3::kStunMost[p] + 6) >> 24;
                char text[32];
                snprintf(text, sizeof(text), "STUN %u/%u", taken, most);
                lines.push_back({p == 0 ? 0.10f : 0.90f, 0.115f, p == 0 ? -1 : 1, text, 255, 210, 80});
            }
        }
    };
    d.features = {
        {"hit_boxes", session::kPage, "Hit boxes",
         "Each fighter's boxes over the picture: blue where it can be hit, red where it hits, yellow its throw, pink where it can be "
         "thrown, green the body that pushes."},
        {"training_stage", session::kPage, "Stage", "The stage a training session is fought on.", kFeatureMenuChoices,
         {"The game's choice", "Gill's", "Alex's", "Ryu's", "Yun's", "Dudley's", "Necro's", "Hugo's", "Ibuki's", "Elena's", "Oro's",
          "Yang's", "Ken's", "Sean's", "Urien's", "Akuma's", "Akuma's (red)", "Chun-Li's", "Makoto's", "Q's", "Twelve's", "Remy's"}},
        {"training_watch", session::kPage, "Watch the game play itself",
         "In a training session, both players are the game's computer player: a fight to watch, as long as it is on (health "
         "comes back below a quarter, as in any session)."},
        {"dummy_computer", "Dummy", "Played by the game",
         "Player 2 is the game's own computer player, as in a one-player game (at the board's difficulty setting).", kFeatureExclusive},
        {"dummy_stun_mash", "Dummy", "Mashes out of stun",
         "Stunned, the dummy works the stick and the buttons as fast as it can, as a player does to get out sooner."},
        {"dummy_parry", "Dummy", "Parries",
         "Player 2 tries to parry player 1's attacks: it taps towards player 1 (or down for an attack made crouching). A move it has "
         "seen before it parries just before it can hit (it learns how long each animation takes to bring out its attack, chained "
         "and cancelled moves too); one it has not, as it starts. In tests it parried about three in four of made-up play's "
         "attacks (a third without the learning)."},
        {"parry_readout", session::kPage, "Parry readout",
         "When a parry window opens (the stick forward, down, in the air, or up against a jump-in): whether the parry came, on which "
         "frame of the window, or how long after the window opened the hit came instead."},
        {"stun_readout", session::kPage, "Stun readout",
         "Under each health bar: how much stun the player has taken, and how much stuns them (it drains when they are left alone)."},
        {"full_meter", session::kPage, "Full meter", "Player 1's super art meter is always full."},
        {"full_meter_2", session::kPage, "Full meter, player 2", "Player 2's super art meter is always full."},
    };
    // (the corners: the players' walls are 164 from the camera's limits, which the camera's middle keeps between; seen
    // in made-up play on Ken's stage, 108 and 916 with limits 272 and 752)
    d.corners = [](int &left, int &right) {
        left = Signed16(sfiii3::kCameraLeft[0]) - 164;
        right = Signed16(sfiii3::kCameraRight[0]) + 164;
        return right - left > 200;
    };
    d.cornerApart = 90;
    // (the frame data log: the character by its number in the record, 0x3C0; the move by its animation, 0x202)
    d.moveStart = [](int p) {
        static const char *const kNames[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                             "Ken", "Sean", "Urien", "Akuma", "Gill", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
        const uint32_t base = Player(p);
        const uint16_t who = Ram16(base + 0x3C0);
        fight::MoveLog::Start start;
        start.character = who < std::size(kNames) ? kNames[who] : "?";
        const Machine *m = TheMachine();
        start.input = fight::Notation(m ? m->players[p] : 0, Ram8(base + sfiii3::kFacing) == 0);
        start.air = Signed16(base + sfiii3::kY) > 0;
        start.id = Ram16(base + 0x202);
        return start;
    };
    d.ownAids = {"hit_boxes", "dummy_parry", "dummy_stun_mash", "dummy_computer", "training_watch", "parry_readout", "stun_readout", "full_meter", "full_meter_2"};
    // The dummy's guard: player 1's move starting or able to hit, near enough (280 across: the distance a move can hit
    // from is not known; nearer missed more, farther no fewer); standing against an attack from the air (made-up play,
    // three seeds: 25 hits taken to 43 with the toolkit's own, which crouches always)
    d.guardView = [](Machine &, bool &inRange, bool &standing) {
        const fight::Doing one = session::Now(0);
        const int dx = std::abs(Signed16(Player(0) + sfiii3::kX) - Signed16(Player(1) + sfiii3::kX));
        inRange = (one == fight::Doing::Startup || one == fight::Doing::Active) && dx < 280;
        standing = Signed16(Player(0) + sfiii3::kY) > 0;
    };
    // Being thrown: the throw escape (light punch and light kick, tapped, as the throw takes hold: kThrown is set two
    // frames after the thrower's buttons)
    d.dummy.heldAction = [](int frame, uint16_t, uint16_t) -> uint16_t {
        return static_cast<uint16_t>((frame & 1) ? 0 : (kPunch1 | kKick1));
    };
    d.dummy.heldActionTitle = "Escapes throws";
    d.dummy.heldActionWords = "Player 2 escapes throws (light punch and light kick as the throw takes hold): always, or one in two at random.";
    // Knocked down: the quick stand (down tapped as it lands)
    d.dummy.getUp.push_back({"Quick stand", [](int frame, uint16_t, uint16_t) -> uint16_t {
                                 return static_cast<uint16_t>(((frame / 2) & 1) ? 0 : kDown);
                             }});
    d.dummy.downAfter = 12;
    d.dummy.getUpWords = "How player 2 gets up after being knocked down: where it fell, a quick stand (down as it lands), or either at random.";
    session::Register(d);
    game.state = [](Archive &ar) {
        WidescreenState(ar);
        SurvivalState(ar);
        ar.Pod(g);
        session::State(ar);
    };
}

} // namespace cps3recomp
