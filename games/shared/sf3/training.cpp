// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Training mode for New Generation and 2nd Impact: the toolkit's session (fight/session.h) told where the game keeps
// things (the game's facts, facts.h), and what each player does each frame, read from the players' records after the
// frame: a move or not, reeling, thrown, in the air; hits and guards as the records count them; damage as the health
// goes down. Hit boxes through the game's camera, the super art gauge kept full, the stun and parry readouts and the
// dummy's parries.
//
// The same shape as 3rd Strike's (games/sfiii3/src/training.cpp), which has its places as constants and some of its
// steps from its rewritten routines: a fix to one most likely belongs in the other. Everything here is read from RAM,
// once a frame; a game offers only what its facts give (an offset or address of 0: the game lacks it, or it is not
// known, and its feature is not registered).
#include "training.h"
#include "widescreen.h"

#include "facts.h"
#include "fight.h"
#include "game.h"
#include "session.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace cps3recomp {

namespace {
    using sf3::InRound;
    using sf3::TheFacts;

    struct State {
        uint16_t moveAnim[2] = {};  // (the frame data log: each move's animation, from its first step)
        uint32_t loggedAt[2] = {};
        uint16_t hits[2] = {};
        uint8_t health[2] = {};
        bool reeling[2] = {}, guarding[2] = {};
        bool metFlag[2] = {}; // (frame data: each player's attack's "met" flag, to see it set)
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
        uint32_t parryTap = 0; // (the dummy's parry: the frame it last tapped, or let an attack through)
        bool parryTried = false;
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
        int projectileDistance = 1 << 20; // (the dummy's parry: player 1's nearest projectile with its attack out, how far)
        // (the dummy's guard: moves (by their first animation) that hit it guarding the wrong way, the height to guard
        // them at next time: 1 standing, 2 crouching; and how it was told to guard last)
        struct Height {
            uint16_t anim = 0;
            uint8_t who = 0, height = 0;
        } heights[128];
        bool toldGuard = false, toldStanding = false;
    } g;

    int LearnedHeight(uint8_t who, uint16_t anim) {
        for (uint32_t n = 0, at = anim % std::size(g.heights); n < std::size(g.heights); n++, at = (at + 1) % std::size(g.heights)) {
            const auto &one = g.heights[at];
            if (one.height == 0) {
                return 0;
            }
            if (one.anim == anim && one.who == who) {
                return one.height;
            }
        }
        return 0;
    }
    void LearnHeight(uint8_t who, uint16_t anim, int height) {
        for (uint32_t n = 0, at = anim % std::size(g.heights); n < std::size(g.heights); n++, at = (at + 1) % std::size(g.heights)) {
            auto &one = g.heights[at];
            if (one.height == 0 || (one.anim == anim && one.who == who)) {
                one = {anim, who, static_cast<uint8_t>(height)};
                return;
            }
        }
    }

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

    constexpr uint16_t kDizzy = 25; // (a record's reaction, routine_no[2], while stunned)

    int16_t Signed16(uint32_t address) {
        return static_cast<int16_t>(Ram16(address));
    }
    uint32_t Player(int p) {
        return TheFacts().player[p];
    }
    const sf3::Facts::Record &R() {
        return TheFacts().record;
    }
    // A record a player's record points to (its commands', its super art's, its stun's): in main RAM, or none
    uint32_t Pointed(int p, uint32_t offset) {
        const uint32_t at = offset ? Ram32(Player(p) + offset) : 0;
        return at >= 0x02000000 && at < 0x02080000 ? at : 0;
    }
    uint8_t Character(int p) {
        return static_cast<uint8_t>(Ram16(Player(p) + R().character));
    }
    uint16_t Animation(uint32_t base) {
        return Ram16(base + R().animation + 2);
    }
    bool Guarding(int p) {
        const uint8_t b = Ram8(Player(p) + R().blocking);
        return b > 0 && b < 5;
    }
    // Free to act: in its normal routine (routine_no[1] 0: a move's last animation frame can read as free in all else a
    // frame before the move's routine is left, or not, and the same move then measured a frame shorter now and then),
    // not attacking, guarding, thrown, busy or frozen, and taking input
    bool Idle(int p) {
        const uint32_t base = Player(p);
        return Ram16(base + R().doing) == 0 && Ram8(base + R().attacking) == 0 && !Guarding(p) && Ram8(base + R().thrown) == 0 &&
               Ram8(base + R().busy) == 0 && Ram8(base + R().freeze) == 0 && Ram8(base + R().inputTaken) != 0;
    }
    // A box list's first box that is not empty
    bool AnyBox(uint32_t base, uint32_t offset, int count) {
        const uint32_t at = offset ? Ram32(base + offset) : 0;
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

    // The projectiles' records (facts.h: effectList); one with no hurt box list is not in use
    std::vector<uint32_t> Projectiles() {
        const sf3::Facts &f = TheFacts();
        std::vector<uint32_t> list;
        if (!f.effectList) {
            return list;
        }
        int16_t index = static_cast<int16_t>(Ram16(f.effectList + 3 * 2));
        for (int n = 0; n < 30 && index >= 0 && index < 128; n++) {
            const uint32_t base = f.effects + static_cast<uint32_t>(index) * f.effectSize;
            if (Ram32(base + R().hurtBoxes) != 0) {
                list.push_back(base);
            }
            index = static_cast<int16_t>(Ram16(base + 0x1C));
        }
        return list;
    }

    void Boxes(std::vector<HudBox> &boxes) {
        const sf3::Facts &f = TheFacts();
        const int cameraX = Signed16(f.cameraX), cameraY = Signed16(f.cameraY);
        const Machine *m = TheMachine();
        const int middle = (m ? m->PictureWidth() : Video::kWidth) / 2;
        // (the players' records, then the projectiles')
        std::vector<uint32_t> records = {Player(0), Player(1)};
        for (const uint32_t base : Projectiles()) {
            records.push_back(base);
        }
        for (const uint32_t base : records) {
            const int px = Signed16(base + R().x) - cameraX + middle - WorldShift(); // (widescreen.cpp: invisible walls)
            const int py = Video::kHeight - (Signed16(base + R().y) - cameraY) - 23;
            const bool flipped = Ram8(base + R().facing) != 0;
            for (const sf3::BoxList &list : f.boxes) {
                if (!list.offset) {
                    break;
                }
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

    bool HasParries() {
        return TheFacts().parryWindow[0] != 0 && R().commands != 0;
    }
    uint8_t ParryWindow(int p, int k) {
        const uint32_t commands = Pointed(p, R().commands), offset = TheFacts().parryWindow[k];
        return commands && offset ? Ram8(commands + offset) : 0;
    }

    // A parry just made by player p, as the game made it (set_paring_status keeps the reaction the hit judgement chose):
    // its reaction (routine_no[2]) 31 or 32 forward (through the anti-air window where the game has one, against an
    // attack from the air), 33 down, 34 or 35 in the air (0-3, parryWindow's order; -1 not a parry's reaction)
    int ParryKind(int p) {
        const uint32_t base = Player(p);
        const uint16_t how = Ram16(base + R().which);
        if (how == 33) {
            return 1;
        }
        if (how == 34 || how == 35) {
            return 2;
        }
        if (how != 31 && how != 32) {
            return -1;
        }
        const uint32_t by = Ram32(base + R().hitBy) & 0x0FFFFFFF; // (the record that hit it)
        const uint32_t attacker = by >= 0x02000000 && by < 0x02080000 ? by : Player(1 - p);
        // (the anti-air window: the attacker a player in the air, defense_ground's own test)
        return TheFacts().parryWindow[3] && attacker == Player(1 - p) && Signed16(attacker + R().y) > 1 ? 3 : 0;
    }

    // The parry readout. A parry in a window says which of its frames it came on; a hit or guard arriving while a window
    // is open but not parried, the wrong window; a hit within 20 frames after a window closed, too early by so many; a
    // window opened within 20 frames after a hit, too late by so many.
    void ReadParries(bool hit[2], bool guarded[2]) {
        static const char *const kKinds[4] = {"forward", "down", "air", "anti-air"};
        static const bool log = getenv("SF3_PARRIES") != nullptr;
        const uint16_t parryStop = static_cast<uint16_t>(TheFacts().parryStop);
        for (int p = 0; p < 2; p++) {
            State::Parry &parry = g.parry[p];
            const uint16_t connection = Ram16(Player(p) + R().hurtStop);
            const bool parried = parry.connection != parryStop && connection == parryStop && !hit[p];
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
            bool open = false, opened = false;
            for (int k = 0; k < 4; k++) {
                const uint8_t now = ParryWindow(p, k);
                // (a tap forward on the floor opens the forward window and the anti-air one together: named the first)
                if (parry.window[k] == 0 && now != 0 && !opened) {
                    opened = true;
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
                const int kind = ParryKind(p);
                if (kind >= 0) {
                    parry.kind = static_cast<int8_t>(kind);
                }
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

    uint32_t StunRecord(int p) {
        return TheFacts().stun.most ? Pointed(p, R().stun) : 0;
    }

    // After every frame: what each player did, told to the session
    void ReadSteps() {
        const sf3::Facts &f = TheFacts();
        g.frame++;
        if (!InRound()) {
            for (int p = 0; p < 2; p++) {
                g.hits[p] = Ram16(Player(p) + R().hitCount);
                g.health[p] = Ram8(Player(p) + f.life);
                g.reeling[p] = g.guarding[p] = false;
            }
            return;
        }
        // First what landed this frame on each player (the attacker's move connected this step, whatever its boxes say:
        // a hit can take the box away the frame it lands)
        bool landed[2] = {}, wasHit[2] = {}, wasGuarded[2] = {};
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            const uint16_t hits = Ram16(base + R().hitCount);
            const bool hit = hits != g.hits[p];
            const bool guardStarted = Guarding(p) && !g.guarding[p];
            g.hits[p] = hits;
            // (the dummy told to guard, hit while free (not already reeling: a combo's later hits cannot be guarded) by a
            // move made on the floor: that move is guarded the other way next time)
            if (p == 1 && hit && !g.reeling[1] && g.toldGuard && Signed16(Player(0) + R().y) <= 0) {
                LearnHeight(Character(0), g.moveAnim[0], g.toldStanding ? 2 : 1);
            }
            if (hit) {
                g.reeling[p] = true;
                session::Contact(p, false);
            } else if (guardStarted) {
                session::Contact(p, true);
            }
            // (by the attacker's own record, not a projectile of its: the record that hit)
            landed[1 - p] = (hit || guardStarted) && (Ram32(base + R().hitBy) & 0x0FFFFFFF) == Player(1 - p);
            wasHit[p] = hit;
            wasGuarded[p] = guardStarted;
            g.guarding[p] = Guarding(p);
            const uint8_t health = Ram8(base + f.life);
            if (health < g.health[p]) {
                session::Hit(p, g.health[p] - health, g.guarding[p] && !hit);
            }
            g.health[p] = health;
        }
        if (HasParries()) {
            ReadParries(wasHit, wasGuarded);
        }
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            const bool idle = Idle(p);
            if (idle && !wasHit[p]) { // (the frame a hit lands the record still reads as it was: reeling from the next)
                g.reeling[p] = false;
            }
            fight::StepView view;
            const bool thrown = Ram8(base + R().thrown) != 0;
            view.control = thrown ? fight::StepView::Held : (g.reeling[p] || g.guarding[p]) ? fight::StepView::Reeling : fight::StepView::Own;
            view.canAct = idle;
            view.frozen = Ram8(base + R().freeze) != 0;
            view.attacking = Ram8(base + R().attacking) != 0;
            view.jumping = Signed16(base + R().y) > 0;
            // The attack out: this frame of the animation has attack boxes (they stay for the whole of the frame, whatever
            // they met), or the move met the other this step: an animation frame can say that a hit moves the animation on
            // at once (hit_pattern_extdat_check), and then the frame that hit is never seen here, only the one after it.
            // (So a move that hits can be shorter than the same move missing: both are the game's own numbers.)
            const uint16_t animNow = Animation(base), cellNow = Ram16(base + R().animationFrame);
            const bool met = Ram8(base + R().cancelHit) != 0;
            landed[p] = landed[p] || (met && !g.metFlag[p]);
            g.metFlag[p] = met;
            view.attackOut = AnyBox(base, R().attackBoxes, R().attackBoxCount) || AnyBox(base, R().throwBox, 1) || landed[p];
            // (debugging aid, SF3_STEPS=1 or 2: what is read of that player, each frame)
            static const bool printSteps = getenv("SF3_STEPS") != nullptr;
            static const int stepsOf = printSteps ? atoi(getenv("SF3_STEPS")) - 1 : 0;
            if (printSteps && p == std::max(stepsOf, 0) && !RunningAhead()) {
                fprintf(stderr, "step: %u control %d canAct %d frozen %d attacking %d jumping %d attackOut %d anim %04X cell %u doing %u %u -> %d\n",
                        g.frame, view.control, view.canAct, view.frozen, view.attacking, view.jumping, view.attackOut, animNow, cellNow,
                        Ram16(base + R().doing), Ram16(base + R().which), static_cast<int>(session::Now(p)));
            }
            const fight::Doing before = session::Now(p);
            session::Step(p, view);
            // Each move's animation as it starts (the frame data log's and the dummy's guard's name for the move)
            if (before == fight::Doing::Free && session::Now(p) != fight::Doing::Free) {
                g.moveAnim[p] = animNow;
                g.moveFrom[p] = g.frame;
                const Machine *m = TheMachine();
                g.moveLow[p] = m && (m->players[p] & kDown) && Signed16(base + R().y) <= 0;
            }
            // (by animation: each of player 1's animations' frames until its attack box comes out, learned the first
            // time it does; so chained and cancelled moves are known too)
            if (p == 0) {
                if (animNow != g.anim) {
                    g.anim = animNow;
                    g.animFrom = g.frame;
                    g.animBox = view.attackOut;
                }
                if (view.attackOut && !g.animBox) {
                    g.animBox = true;
                    Learn(Character(0), animNow, static_cast<int>(g.frame - g.animFrom));
                }
            }
            // (debugging aid, SF3_MOVES=1: each move's player, character, animation, startup, active and recovery)
            const fight::Frames::Move move = session::LastMove(p);
            if (move.at && move.at != g.loggedAt[p]) {
                g.loggedAt[p] = move.at;
                static const bool logMoves = getenv("SF3_MOVES") != nullptr;
                if (logMoves && !RunningAhead()) {
                    fprintf(stderr, "move %d %u %04x %u %u %u\n", p, Character(p), g.moveAnim[p], move.startup, move.active, move.recovery);
                }
            }
        }
        // (debugging aid, SF3_TRAINING_LOG=1: each frame of a round, what checktraining.sh judges by: both players'
        // doings (fight::Doing), player 2's controls as the game was given them, thrown, health, stun (taken, the most, the
        // timer), places, the animations, player 2's parry windows, its routine and reaction, the gauges' stocks, the stage)
        static const bool trainingLog = getenv("SF3_TRAINING_LOG") != nullptr;
        if (trainingLog && !RunningAhead()) {
            const Machine *m = TheMachine();
            const uint32_t stun = StunRecord(1);
            const uint32_t gauge0 = f.gauge.stocks ? Pointed(0, R().gauge) : 0, gauge1 = f.gauge.stocks ? Pointed(1, R().gauge) : 0;
            fprintf(stderr, "tl %llu %u d %d %d held %04x %04x thrown %u life %u %u stun %u %u %u x %d %d y %d %d anim %04x %04x win %u%u%u%u how %u %u "
                            "clock %u gauge %u %u stage %u\n",
                    static_cast<unsigned long long>(m ? m->frame : 0), g.frame, static_cast<int>(session::Now(0)), static_cast<int>(session::Now(1)),
                    m ? m->players[0] : 0, m ? m->players[1] : 0, Ram8(Player(1) + R().thrown), Ram8(Player(0) + f.life), Ram8(Player(1) + f.life),
                    stun ? Ram16(stun + f.stun.taken) : 0, stun ? Ram16(stun + f.stun.most) : 0, stun ? Ram16(stun + f.stun.timer) : 0,
                    Signed16(Player(0) + R().x), Signed16(Player(1) + R().x), Signed16(Player(0) + R().y), Signed16(Player(1) + R().y),
                    Animation(Player(0)), Animation(Player(1)), ParryWindow(1, 0) != 0, ParryWindow(1, 1) != 0, ParryWindow(1, 2) != 0,
                    ParryWindow(1, 3) != 0, Ram16(Player(1) + R().doing), Ram16(Player(1) + R().which), Ram8(f.clock),
                    gauge0 ? Ram16(gauge0 + f.gauge.stocks) : 0, gauge1 ? Ram16(gauge1 + f.gauge.stocks) : 0, f.stage ? Ram8(f.stage) : 0);
        }
    }

    void Hud(Machine &machine, std::vector<HudLine> &lines, std::vector<HudBox> &boxes) {
        const sf3::Facts &f = TheFacts();
        if (f.boxes[0].offset && FeatureOn("hit_boxes")) {
            // The picture a frame shows is of the fighters as the frame before left them (these games draw a frame's
            // sprites from the records before they step them: an attack's box was there a frame before the limb was
            // out), so the boxes drawn are those of the frame before too, through that frame's camera. (Not a state's:
            // after a state is loaded the first frame has none.)
            static std::vector<HudBox> held;
            static uint64_t heldAt = 0;
            const uint64_t now = machine.frame;
            if (heldAt + 1 == now) {
                boxes.insert(boxes.end(), held.begin(), held.end());
            }
            held.clear();
            Boxes(held);
            heldAt = now;
        }
        if (HasParries() && FeatureOn("parry_readout")) {
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
        if (f.stun.most && FeatureOn("stun_readout")) {
            // (under each health bar, at its outer end: taken / the most before a stun)
            for (int p = 0; p < 2; p++) {
                const uint32_t stun = StunRecord(p);
                if (!stun) {
                    continue;
                }
                char text[32];
                snprintf(text, sizeof(text), "STUN %u/%u", Ram16(stun + f.stun.taken), Ram16(stun + f.stun.most));
                lines.push_back({p == 0 ? 0.10f : 0.90f, 0.115f, p == 0 ? -1 : 1, text, 255, 210, 80});
            }
        }
    }
} // namespace

void TrainingBeforeFrame(Machine &m) {
    const sf3::Facts &f = TheFacts();
    const bool inRound = InRound();
    if (inRound) {
        for (int p = 0; p < 2; p++) {
            const uint32_t base = Player(p);
            // (the gauge kept full: the stocks at their most, the fill emptied as the game leaves it with every stock in;
            // an art whose gauge is a timer, draining while it lasts, is left to drain)
            const uint32_t gauge = f.gauge.stocks ? Pointed(p, R().gauge) : 0;
            if (gauge && FeatureOn("gauge_refill")) {
                const uint16_t most = Ram16(gauge + f.gauge.stocksMost);
                if (most && most < 10 && Ram16(gauge + f.gauge.stocks) < most && Ram8(gauge + f.gauge.kind) != 1) {
                    SetRam16(gauge + f.gauge.stocks, most);
                    SetRam16(gauge + f.gauge.fill, 0);
                    if (f.gaugeRedraw[p]) {
                        SetRam8(f.gaugeRedraw[p], 1);
                    }
                }
            }
            // (nobody knocked out in a session: health back to full below a quarter)
            if (session::NoKnockOut() && Ram8(base + f.life) < f.fullLife / 4) {
                SetRam8(base + f.life, static_cast<uint8_t>(f.fullLife));
            }
            g.health[p] = Ram8(base + f.life); // (as the cheats and this left it: only the frame's own damage is a hit)
        }
        // (no clock: at 99, and its second kept from running out, so that the game never draws 98)
        if (session::NoClock()) {
            SetRam8(f.clock, 99);
            if (Ram16(f.clockFrames) == 0) {
                SetRam16(f.clockFrames, 1);
            }
        }
    }
    // (the menu's Stage, in any mode: set while no round is on, so the next fight's is the one chosen)
    if (f.stage && !inRound) {
        const int stage = FeatureValue("stage");
        if (stage > 0 && stage <= f.stages) {
            SetRam8(f.stage, f.stageNumbers[stage - 1]);
        }
    }
    session::BeforeFrame(m);
    // Played by the game: player 2's record says the computer works it (the record's byte for who works it: 0 the
    // computer, 1 a player), set while a round is on (and player 1's too, to watch the game play itself)
    if (R().computer && session::InSession() && inRound) {
        const bool watch = FeatureOn("training_watch");
        for (int p = 0; p < 2; p++) {
            const bool computer = watch || (p == 1 && FeatureOn("dummy_computer"));
            const uint8_t now = Ram8(Player(p) + R().computer);
            if (computer && now != 0) {
                SetRam8(Player(p) + R().computer, 0);
            } else if (!computer && now == 0) {
                SetRam8(Player(p) + R().computer, 1);
            }
        }
    }
    // Stunned (the record's reaction: routine_no[1] 1 hurt, [2] 25 dizzy; the stun's timer counting down), the dummy
    // mashes: the stick round and the buttons, a different one each frame; nothing in the timer's last frames, so that
    // no button pressed as the stun ends makes a move (as 3rd Strike's)
    if (f.stunMash && session::InSession() && FeatureOn("dummy_stun_mash") && inRound) {
        const uint32_t stun = StunRecord(1);
        const uint16_t timer = stun ? Ram16(stun + f.stun.timer) : 0;
        const bool stunned = Ram16(Player(1) + R().doing) == 1 && Ram16(Player(1) + R().which) == kDizzy && timer != 0 && timer < 0xF0;
        if (stunned && timer > 6) {
            static const uint16_t kMash[] = {kLeft | kPunch1, kDown | kKick1, kRight | kPunch2, kUp | kKick2, kLeft | kPunch3, kDown | kKick3};
            m.players[1] = (g.frame & 1) ? 0 : kMash[(g.frame / 2) % std::size(kMash)];
        }
    }
    // The dummy parries: as player 1's attack starts, a tap towards player 1 (down for an attack made crouching) opens
    // a parry window; again no sooner than 18 frames on (3rd Strike's numbers). The share of attacks it tries for: the
    // session's dice thrown for each as its tap comes due.
    const int parryPercent = HasParries() ? fight::ChoicePercent(FeatureValue("dummy_parry")) : 0;
    if (session::InSession() && parryPercent && inRound) {
        const bool onRight = Signed16(Player(1) + R().x) >= Signed16(Player(0) + R().x);
        // (an animation seen to hit before: the tap as it is about to, `lead` frames before; a move not seen yet: as it starts)
        static const int lead = getenv("SF3_PARRY_LEAD") ? atoi(getenv("SF3_PARRY_LEAD")) : 2;
        const int startup = LearnedStartup(Character(0), g.anim);
        const int into = static_cast<int>(g.frame - g.animFrom);
        const bool due = startup ? into >= startup - lead && !g.animBox : session::Now(0) == fight::Doing::Startup && into == 0;
        // (player 1's projectiles: the nearest with its attack out, tapped for as it comes within three frames of its
        // closing speed, past the boxes' reach of about 60)
        int nearest = 1 << 20;
        for (const uint32_t base : Projectiles()) {
            if (Ram8(base + 2) == 0 && Ram32(base + R().attackBoxes) != 0) {
                nearest = std::min(nearest, std::abs(Signed16(base + R().x) - Signed16(Player(1) + R().x)));
            }
        }
        const int closing = g.projectileDistance - nearest;
        const bool projectile = nearest < (1 << 20) && closing > 0 && closing < 64 && nearest - 60 <= closing * 3;
        g.projectileDistance = nearest;
        constexpr uint32_t kAgain = 18;
        if ((due || projectile) && (g.parryTap == 0 || g.frame - g.parryTap > kAgain)) {
            // (an attack let through: the tap's time taken all the same, so that the same attack is not tried for again)
            const bool tries = session::Chance(parryPercent);
            if (tries) {
                m.players[1] = g.moveLow[0] && !projectile ? kDown : (onRight ? kLeft : kRight);
            }
            if (!RunningAhead()) {
                g.parryTap = g.frame;
                g.parryTried = tries;
            }
        } else if (g.parryTap && g.parryTried && g.frame - g.parryTap < 2) {
            m.players[1] = 0; // (let go, so the next tap is a fresh one)
        }
    }
}

void TrainingAfterFrame(Machine &m) {
    ReadSteps();
    session::Frame(m);
}

void TrainingState(Archive &ar) {
    ar.Pod(g);
    session::State(ar);
}

void RegisterTraining(Game &) {
    const sf3::Facts &f = TheFacts();
    session::Description d;
    d.player[0] = f.player[0];
    d.player[1] = f.player[1];
    d.x = R().x;
    d.health = f.life;
    d.healthFull = static_cast<uint8_t>(f.fullLife);
    d.credits = f.credits;
    d.fighting = InRound;
    // (character select: 0 no player, 1 coming in, 2 choosing a character, 3-4 the super art, 5 chosen)
    d.joined = [](int p) { return Ram8(TheFacts().selectState[p]) >= 2; };
    // (chosen: the character and its super art, so that player 1 then chooses both of player 2's)
    d.chosen = [](int p) { return Ram8(TheFacts().selectState[p]) >= 5; };
    d.choosing = [] {
        const uint8_t one = Ram8(TheFacts().selectState[0]), two = Ram8(TheFacts().selectState[1]);
        return !InRound() && (one >= 1 || two >= 1) && (one < 5 || two < 5);
    };
    d.facingLeft = [](int p) { return Ram8(Player(p) + R().facing) == 0; };
    d.hud = Hud;
    if (f.boxes[0].offset) {
        d.features.push_back({"hit_boxes", session::kPage, "Hit boxes",
                              "Each fighter's boxes over the picture: blue where it can be hit, red where it hits, yellow its throw, pink "
                              "where it can be thrown, green the body that pushes."});
        d.ownAids.push_back("hit_boxes");
    }
    if (f.stage) {
        std::vector<const char *> names = {"The game's choice"};
        names.insert(names.end(), f.stageNames, f.stageNames + f.stages);
        // (a page of its own at the head of the menu: base.cpp's)
        d.features.push_back({"stage", "Stage", "Stage",
                              "Where the next fight is, in the arcade game and in a training session: one of the game's stages. "
                              "Online, the host's choice.",
                              0, names});
    }
    if (R().computer) {
        d.features.push_back({"training_watch", session::kPage, "Watch the game play itself",
                              "In a training session, both players are the game's computer player: a fight to watch, as long as it is on "
                              "(health comes back below a quarter, as in any session)."});
        d.features.push_back({"dummy_computer", "Dummy", "Played by the game",
                              "Player 2 is the game's own computer player, as in a one-player game (at the board's difficulty setting).",
                              kFeatureExclusive});
        d.ownAids.push_back("training_watch");
        d.ownAids.push_back("dummy_computer");
    }
    if (f.stunMash) {
        d.features.push_back({"dummy_stun_mash", "Dummy", "Mashes out of stun",
                              "Stunned, the dummy works the stick and the buttons as fast as it can, as a player does to get out sooner."});
        d.ownAids.push_back("dummy_stun_mash");
    }
    if (HasParries()) {
        d.features.push_back({"dummy_parry", "Dummy", "Parries",
                              "Player 2 tries to parry this share of player 1's attacks (the rest it lets through), at random: it taps "
                              "towards player 1 (or down for an attack made crouching). A move it has seen before it parries just before it "
                              "can hit (it learns how long each animation takes to bring out its attack); one it has not, as it starts.",
                              0, fight::kPercentChoices});
        d.features.push_back({"parry_readout", session::kPage, "Parry readout",
                              f.parryWindow[3] ? "When a parry window opens (the stick forward, down, in the air, or forward against a jump-in): "
                                                 "whether the parry came, on which frame of the window, or how long after the window opened "
                                                 "the hit came instead."
                                               : "When a parry window opens (the stick forward, down, or in the air): whether the parry came, on "
                                                 "which frame of the window, or how long after the window opened the hit came instead."});
        d.ownAids.push_back("dummy_parry");
        d.ownAids.push_back("parry_readout");
    }
    if (f.stun.most) {
        d.features.push_back({"stun_readout", session::kPage, "Stun readout",
                              "Under each health bar: how much stun the player has taken, and how much stuns them (it drains when they are "
                              "left alone)."});
        d.ownAids.push_back("stun_readout");
    }
    if (f.gauge.stocks) {
        d.features.push_back({"gauge_refill", session::kPage, "Gauge refills", "Both players' super art gauge kept full, filled again after a super art."});
        d.ownAids.push_back("gauge_refill");
    }
    // (the corners: the players' walls are `wall` beyond the camera's ends, which the camera's middle keeps between)
    if (f.cameraLeft && f.wall) {
        d.corners = [](int &left, int &right) {
            const sf3::Facts &facts = TheFacts();
            left = Signed16(facts.cameraLeft) - facts.wall;
            right = Signed16(facts.cameraRight) + facts.wall;
            if (right - left <= 200) {
                return false;
            }
            // (asked only as "Start over" puts the players in a corner, the round just put back: the camera goes there
            // with them, to the end the session's choice names (restart_at: 2 and 4 the left corner, 3 and 5 the right);
            // these games keep the players in the picture, and would bring them back to where the camera was)
            const int place = FeatureSetting("restart_at");
            if (place >= 2) {
                SetRam16(facts.cameraX, Ram16(place % 2 == 0 ? facts.cameraLeft : facts.cameraRight));
            }
            return true;
        };
        d.cornerApart = 90;
    }
    // (the frame data log: the character by its number in the record; the move by its animation)
    d.moveStart = [](int p) {
        const sf3::Facts &facts = TheFacts();
        const uint32_t base = Player(p);
        const uint16_t who = Ram16(base + R().character);
        fight::MoveLog::Start start;
        start.character = who < facts.characterCount ? facts.characters[who] : "?";
        const Machine *m = TheMachine();
        start.input = fight::Notation(m ? m->players[p] : 0, Ram8(base + R().facing) == 0);
        start.air = Signed16(base + R().y) > 0;
        start.id = Animation(base);
        return start;
    };
    // The dummy's guard: player 1's move starting or able to hit, near enough (280 across, 3rd Strike's number);
    // standing against an attack from the air
    d.guardView = [](Machine &, bool &inRange, bool &standing) {
        const fight::Doing one = session::Now(0);
        const int dx = std::abs(Signed16(Player(0) + R().x) - Signed16(Player(1) + R().x));
        inRange = (one == fight::Doing::Startup || one == fight::Doing::Active) && dx < 280;
        // (or a projectile of player 1's, its attack out, near: guarded standing or crouching as the dummy likes)
        for (const uint32_t base : Projectiles()) {
            if (Ram8(base + 2) == 0 && Ram32(base + R().attackBoxes) != 0 && std::abs(Signed16(base + R().x) - Signed16(Player(1) + R().x)) < 160) {
                inRange = true;
            }
        }
        // (standing against an attack from the air, or a move learned to be an overhead)
        const int height = LearnedHeight(Character(0), g.moveAnim[0]);
        standing = (Signed16(Player(0) + R().y) > 0 && height != 2) || height == 1;
        g.toldGuard = inRange;
        g.toldStanding = standing;
    };
    if (f.throwEscape) {
        // Being thrown: the throw escape, the stick to a side with a medium or heavy button (check_nagenuke_cmd), tapped as
        // the throw takes hold
        d.dummy.heldAction = [](int frame, uint16_t towards, uint16_t) -> uint16_t {
            return static_cast<uint16_t>((frame & 1) ? 0 : (towards | kPunch3));
        };
        d.dummy.heldActionTitle = "Escapes throws";
        d.dummy.heldActionWords = "Player 2 escapes this share of throws (the stick to a side and heavy punch as the throw takes hold), at "
                                  "random: 100% every one, 50% one in two.";
    }
    if (f.quickStand) {
        // Knocked down: the quick stand (down tapped as it lands)
        d.dummy.getUp.push_back({"Quick stand", [](int frame, uint16_t, uint16_t) -> uint16_t { return static_cast<uint16_t>(((frame / 2) & 1) ? 0 : kDown); }});
        d.dummy.downAfter = 12;
        d.dummy.getUpWords = "How player 2 gets up after being knocked down: where it fell, a quick stand (down as it lands), or either at random.";
    }
    session::Register(d);
}

} // namespace cps3recomp
