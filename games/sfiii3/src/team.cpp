// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Team battle (knock-out style): two teams of two or three fighters each, one of each on at a time. A round decides it:
// the loser's fighter is out and the next of that team comes on, the winner stays with the health it has left; the
// battle is over when a team has nobody left. Team 2 is the computer's or player 2's.
//
// It is the game's own versus, worked from outside: the board reset and taken to character select for two players
// (as a training session is), each side's fighter the team's next (the chosen character's byte, written through the
// choice), the computer's side played by the computer (record byte 3). A round's win ends the match: the winner's
// rounds won (sfiii3.h, kRoundsWon) set to what the board's rounds setting asks for a match, as the game counts the
// win; the winner's health carried into its next round (written as the round begins, as Survival does).
#include "game.h"
#include "session.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace cps3recomp {

namespace {
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                       "Ken", "Sean", "Urien", "Akuma", "Akuma (red)", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
    constexpr int kMost = 3;

    struct State {
        bool on = false, over = false;
        uint32_t random = 0x9E3779B9u, frames = 0;
        uint8_t member[2][kMost] = {};  // the teams' fighters, by character number
        uint8_t size = 2;
        uint8_t next[2] = {};           // each team's fighter on (from 0)
        uint8_t health[2] = {};         // health carried into the next round (0: full)
        uint8_t wins[2] = {};           // rounds won at the last frame
        uint8_t lastState = 0;
        bool fighting = false;
        int8_t winner = -1;             // the team that won the battle
        uint32_t overAt = 0;
    } t;

    uint32_t Next() {
        t.random ^= t.random << 13;
        t.random ^= t.random >> 17;
        t.random ^= t.random << 5;
        return t.random;
    }
    bool Locked() {
        const uint8_t state = Ram8(sfiii3::kMatchState);
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && state >= 1 && state <= 8;
    }
    bool Computer() {
        return FeatureValue("team_opponent") == 0;
    }
    // Rounds a match needs between two players, as the board is set (the EEPROM's byte 0x24, its high half: 0 one
    // round, 1 best of three, 2 best of five; board.cpp)
    int RoundsToWin(const Machine &m) {
        const uint8_t rounds = static_cast<uint8_t>(m.eeprom.words[0x24 / 4] >> ((0x24 % 4) * 8));
        return std::clamp((rounds >> 4) + 1, 1, 3);
    }

    void Start(Machine &machine) {
        session::StartSession(machine);
        SetFeature("training_mode", false);
        SetFeature("endless", false);
        t = {};
        t.on = true;
        t.random ^= static_cast<uint32_t>(machine.frame * 2654435761u) | 1u;
        t.size = static_cast<uint8_t>(FeatureValue("team_size") == 1 ? 3 : 2);
        for (int side = 0; side < 2; side++) {
            for (int n = 0; n < kMost; n++) {
                char name[16];
                snprintf(name, sizeof(name), "team_%d_%d", side + 1, n + 1);
                const int value = FeatureValue(name);
                t.member[side][n] = static_cast<uint8_t>(value > 0 ? value - 1 : Next() % std::size(kCharacters));
            }
        }
    }
} // namespace

void TeamBeforeFrame(Machine &m) {
    if (t.on && session::InSession()) {
        t.on = false; // (a training session started: the battle is over)
    }
    if (!t.on || session::Booting()) {
        return;
    }
    t.frames++;
    SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 2));
    const bool press = t.frames % 20 < 4;
    const bool locked = Locked();
    const uint8_t matchState = Ram8(sfiii3::kMatchState);
    uint16_t held[2] = {m.players[0], m.players[1]};
    if (t.over) {
        // (the battle won: said for five seconds, then the game goes on as it is, the battle over)
        if (t.frames - t.overAt > 300) {
            t.on = false;
        }
        return;
    }
    for (int p = 0; p < 2; p++) {
        const uint8_t state = Ram8(sfiii3::kSelectState[p]);
        const bool computer = p == 1 && Computer();
        if (state == 0) {
            held[p] = press ? kStart : 0; // (in for the next match)
        } else if (!locked && state >= 2) {
            SetRam8(sfiii3::kChosen[p], t.member[p][std::min<int>(t.next[p], t.size - 1)]);
            if (computer || state < 5) {
                // (the character is the team's whatever is chosen; the computer's side, and the first press of a
                // person's, take it at once)
                if (computer) {
                    held[p] = state < 5 && press ? kPunch1 : 0;
                }
            }
        } else if (locked && computer) {
            SetRam8(sfiii3::kPlayer[1] + 3, 0); // (the computer plays it)
            held[p] = 0;
        }
    }
    // A round begins: the winner of the last with the health it had left
    const bool fighting = (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && matchState == 2;
    if (fighting && !t.fighting) {
        for (int p = 0; p < 2; p++) {
            if (t.health[p] > 0 && t.health[p] < sfiii3::kFullLife) {
                SetRam8(sfiii3::kPlayer[p] + sfiii3::kLife, t.health[p]);
            }
        }
        t.wins[0] = Ram8(sfiii3::kRoundsWon[0]);
        t.wins[1] = Ram8(sfiii3::kRoundsWon[1]);
    }
    t.fighting = fighting;
    // A round won (the winner's count goes up): the loser's fighter out, the match ended there
    if (locked) {
        for (int p = 0; p < 2; p++) {
            const uint8_t won = Ram8(sfiii3::kRoundsWon[p]);
            if (won > t.wins[p]) {
                const int loser = 1 - p;
                const uint8_t left = Ram8(sfiii3::kPlayer[p] + sfiii3::kLife);
                t.health[p] = left > 0 && left <= sfiii3::kFullLife ? left : 0;
                t.health[loser] = 0;
                t.next[loser]++;
                if (t.next[loser] >= t.size) {
                    t.over = true;
                    t.winner = static_cast<int8_t>(p);
                    t.overAt = t.frames;
                }
                SetRam8(sfiii3::kRoundsWon[p], static_cast<uint8_t>(std::max<int>(won, RoundsToWin(m))));
            }
            t.wins[p] = Ram8(sfiii3::kRoundsWon[p]);
        }
    }
    t.lastState = matchState;
    if (!locked) {
        // (between matches: the game's screens go on with a press now and then for the computer's side)
        if (Computer() && held[1] == 0) {
            held[1] = press ? kPunch1 : 0;
        }
    }
    m.players[0] = held[0];
    m.players[1] = held[1];
}

void TeamHud(std::vector<HudLine> &lines) {
    if (!t.on) {
        return;
    }
    for (int side = 0; side < 2; side++) {
        std::string text = side == 0 ? "TEAM 1: " : "TEAM 2: ";
        for (int n = 0; n < t.size; n++) {
            if (n) {
                text += n < t.next[side] ? "  " : "  ";
            }
            std::string name = kCharacters[t.member[side][n] % std::size(kCharacters)];
            for (char &c : name) {
                c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            }
            text += n < t.next[side] ? "(" + name + ")" : n == t.next[side] ? "[" + name + "]" : name;
        }
        lines.push_back({side == 0 ? 0.03f : 0.97f, side == 0 ? 0.235f : 0.275f, side == 0 ? -1 : 1, text, 160, 220, 255});
    }
    if (t.over) {
        lines.push_back({0.5f, 0.40f, 0, t.winner == 0 ? "TEAM 1 WINS THE BATTLE" : "TEAM 2 WINS THE BATTLE", 255, 230, 60});
    }
}

void TeamState(Archive &ar) {
    ar.Mark(0x5445414D); // TEAM
    ar.Pod(t);
}

void RegisterTeam(Game &game) {
    static const char *const kPage = "Team battle";
    game.commands.push_back({"team_start", kPage, "Start",
                             "Starts a team battle: the game starts over and goes to character select for two players, each side's "
                             "fighter its team's next.",
                             Start});
    game.features.push_back({"team_size", kPage, "Fighters", "How many fighters each team has.", 0, {"2", "3"}});
    game.features.push_back({"team_opponent", kPage, "Team 2",
                             "Who plays the second team: the computer, or player 2.", 0, {"Computer", "Player 2"}});
    std::vector<const char *> names = {"Random"};
    for (const char *name : kCharacters) {
        names.push_back(name);
    }
    static const char *const kTitles[2][kMost] = {{"Team 1, first", "Team 1, second", "Team 1, third"},
                                                  {"Team 2, first", "Team 2, second", "Team 2, third"}};
    static const char *const kNames[2][kMost] = {{"team_1_1", "team_1_2", "team_1_3"}, {"team_2_1", "team_2_2", "team_2_3"}};
    for (int side = 0; side < 2; side++) {
        for (int n = 0; n < kMost; n++) {
            game.features.push_back({kNames[side][n], kPage, kTitles[side][n],
                                     n == 2 ? "The team's third fighter (with three fighters a team): someone at random, or this one." :
                                              "The team's fighter: someone at random, or this one.",
                                     kFeatureMenuChoices, names});
        }
    }
}

} // namespace cps3recomp
