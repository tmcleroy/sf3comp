// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The bonus games (the Alternative modes page): 3rd Strike's two bonus stages of the one-player game, Crush the Car and
// Parry the Ball (Sean's basketballs), each played straight from the menu with the character chosen at character select.
// The board has only these two: the routine that decides on one (0x060A2EE8) gives 21 (the car) once the player has
// beaten three opponents and 22 (the ball) once six, each only if that one has not been played yet.
//
// How (the game's own way there, worked unseen): the board reset and a one-player game started (credits and Start, at
// speed); character select shown, the player choosing; then, with the one-player game told it has beaten two
// opponents (five, for the ball) and played no bonus stage yet, the first match is won by itself unseen (the
// opponent's health taken), and the game goes on to the bonus stage as after the third (sixth) win. The bonus stage is
// shown and played; when the game marks it done (its byte of Completion_Bonus), its result shows for a while and the
// menu opens again on the page.
//
// The places (found in the program, 0x060A2E66 the check for a bonus stage after a win and 0x060A2EE8 its choice):
//   0x02015500  the one-player game's player (a byte: 0 player 1)
//   0x02015506  opponents beaten, each player (words)
//   0x02016B89  the bonus stage chosen (a byte: 21 the car, 22 the ball, 0 none)
//   0x02016B8A  each bonus stage played, each player (two bytes a player: the car's, the ball's; 1 chosen, 0x80 done)
// The board's setting for the bonus games (its byte kept at 0x0206AC66) does not stop it: seen with it off.
#include "game.h"
#include "recomp.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace cps3recomp {

void EndBattle(); // battle.cpp
bool AttractOn(); // attract.cpp

namespace {
    constexpr const char *kPage = "Bonus games";
    constexpr uint32_t kPlayerId = 0x02015500, kBeaten = 0x02015506, kBonusType = 0x02016B89, kBonusDone = 0x02016B8A;
    constexpr uint8_t kCar = 21, kBall = 22;
    constexpr uint32_t kBootMost = 3000;   // frames the way to character select may take
    constexpr uint32_t kWinMost = 6000;    // ... and the match won unseen, to the bonus stage
    constexpr uint32_t kPlayMost = 7200;   // ... and the bonus stage (the car's clock run out takes about 3800)
    constexpr uint32_t kShownFor = 220;    // frames of its result after the bonus stage is done (to the screen's wipe)

    enum Phase : uint8_t { kOff, kBoot, kChoose, kToStage, kPlaying, kOver };

    struct State {
        Phase phase = kOff;
        uint8_t type = 0;      // kCar or kBall
        uint32_t frames = 0;   // in the phase
        bool fast = false;     // the game run unseen, at speed
        uint32_t session = 0;  // the sessions started before it (another started since: it is over)
    } g;

    void Fast(bool on) {
        if (g.fast != on) {
            g.fast = on;
            SetFastForward(on);
        }
    }
    void SetPhase(Phase phase) {
        if (getenv("SFIII3_BONUS_LOG")) { // (debugging aid)
            fprintf(stderr, "bonus: phase %d -> %d after %u frames (beaten %u, bonus %u, done %02x %02x)\n", g.phase, phase, g.frames,
                    Ram16(kBeaten), Ram8(kBonusType), Ram8(kBonusDone), Ram8(kBonusDone + 1));
        }
        g.phase = phase;
        g.frames = 0;
    }
    bool Locked() {
        return Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    }

    void Begin(Machine &machine, uint8_t type) {
        EndBattle();
        SetFeature("training_mode", false);
        machine.Reset();
        StateLoaded();
        g = {};
        g.type = type;
        g.session = session::Starts();
        SetPhase(kBoot);
        Fast(true);
    }
    void StartCar(Machine &machine) {
        Begin(machine, kCar);
    }
    void StartBall(Machine &machine) {
        Begin(machine, kBall);
    }
    void End() {
        Fast(false);
        SetPhase(kOff);
        AskMenu();
    }
} // namespace

bool BonusOn() {
    return g.phase != kOff;
}

void BonusBeforeFrame(Machine &m) {
    if (g.phase == kOff) {
        return;
    }
    if (session::Starts() != g.session || AttractOn()) {
        Fast(false);
        SetPhase(kOff); // (another mode started, training or a battle: the bonus game is over)
        return;
    }
    g.frames++;
    m.players[1] = 0; // (nobody joins in: a one-player game)
    const bool press = g.frames % 16 < 4;
    const int player = Ram8(kPlayerId) & 1;
    switch (g.phase) {
    case kBoot:
        // (a credit in, and player 1's Start until character select is on)
        SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 1));
        m.players[0] = press ? kStart : 0;
        if (Ram8(sfiii3::kSelectState[0]) == 2) {
            Fast(false);
            SetPhase(kChoose);
        } else if (g.frames > kBootMost) {
            End();
        }
        break;
    case kChoose:
        // (the player chooses; once a character is locked in, the rest is the game's own way to the bonus stage)
        if (Locked()) {
            Fast(true);
            SetPhase(kToStage);
        }
        break;
    case kToStage: {
        m.players[0] = press ? kPunch1 : 0; // (through the match's screens)
        const uint32_t opponent = sfiii3::kPlayer[1 - player];
        if (Locked() && Ram8(sfiii3::kMatchState) == 2) {
            // (while a round is fought: the opponents beaten so far, and the bonus stages not played; the win counted
            // after the knock-out is the third, or the sixth)
            SetRam16(kBeaten + 2 * player, g.type == kCar ? 2 : 5);
            SetRam8(kBonusDone + 2 * player, 0);
            SetRam8(kBonusDone + 2 * player + 1, 0);
            if (Ram8(opponent + sfiii3::kLife) > 0 && Ram8(opponent + sfiii3::kLife) <= sfiii3::kFullLife) {
                SetRam8(opponent + sfiii3::kLife, 0);
            }
        }
        if (Ram8(kBonusType) == g.type) {
            Fast(false);
            SetPhase(kPlaying);
        } else if (g.frames > kWinMost) {
            End();
        }
        break;
    }
    case kPlaying:
        if (Ram8(kBonusDone + 2 * player + (g.type == kCar ? 0 : 1)) & 0x80) {
            SetPhase(kOver);
        } else if (g.frames > kPlayMost) {
            End(); // (never marked done: the board reset under it, say)
        }
        break;
    case kOver:
        if (g.frames > kShownFor) {
            End();
        }
        break;
    default: break;
    }
}

void BonusState(Archive &ar) {
    ar.Mark(0x424F4E53); // BONS
    ar.Pod(g);
}

void RegisterBonus(Game &game) {
    game.commands.push_back({"bonus_car", kPage, "Crush the car",
                             "Choose a character, and go straight to the bonus stage of the car: break it up before the time runs out.",
                             StartCar});
    game.commands.push_back({"bonus_ball", kPage, "Parry the ball",
                             "Choose a character, and go straight to the bonus stage of Sean's basketballs: parry as many as you can.",
                             StartBall});
}

} // namespace cps3recomp
