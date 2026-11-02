// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The fighting modes of the Alternative modes page, as cps2recomp's fight/battle.h has them: each a match set up
// (who fights, where, which side the game itself plays), watched, and followed by the next, made ready on a spare
// machine (runtime/spare.h) while the last one's knock-out plays, and put in its place, with no screens between.
//
//   Survival        the player against one opponent after another, with the health left from the fight before
//                   (and a tenth back); over at the first knock-down, with the count of opponents beaten.
//   Endless battle  two fighters the game plays, with no panel and no clock; the one knocked down is replaced. A
//                   person takes a fighter over by touching the controls, and gives it back by leaving them alone.
//   Ring out        the player against one opponent after another, with no health: a stage's ends are open to a
//                   fighter knocked back, and whoever goes over one is thrown out and loses; the next opponent
//                   comes on (the opponents rung out are the score). Over when the player is rung out.
//   Team battle     two teams of two or three, chosen on the menu: one fighter of each side at a time, the one
//                   knocked down out and the next of that side in, with full health; the winner stays on with
//                   the health it has left. Over when a side has nobody left. Side 2 is a person or the game.
//
// How (3rd Strike's facts, sfiii3.h): a mode starts with the board reset and taken to character select for two players
// (as a training session is: fight/session.h); the machine there is kept, and every later match is a spare begun from
// it: each side's character written through the choice (kChosen), the stage (kStage), the buttons pressed now and then
// until its round is fought; the computer plays a side by its record's byte 3. A round decided (a side's rounds won
// going up, kRoundsWon) is the match's end: the next is made then, and takes the machine's place a moment later.
#include "fight.h"
#include "game.h"
#include "lift.h"
#include "session.h"
#include "sfiii3.h"
#include "recomp.h"
#include "spare.h"
#include "state.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace cps3recomp {

// (stages.cpp: 3rd Strike's own stages drawn behind the fighters from the stage board)
void StageBehind(int stage);
void StageBehindCamera(int x);
int StageBehindFollow();
bool StageBehindEnds(int &left, int &right);
bool StageBehindShown();
void StageBehindNext(int stage);
bool StageBehindNextReady();
void StageBehindSwitch(Machine &machine);

namespace {
    const char *const kCharacters[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                       "Ken", "Sean", "Urien", "Akuma", "Akuma (red)", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
    constexpr int kCharacterCount = static_cast<int>(std::size(kCharacters));
    constexpr int kMost = 3;              // fighters a team
    constexpr uint32_t kTakenFor = 600;   // frames a person keeps an endless battle's fighter after letting go
    constexpr int kSpareFramesAFrame = 6; // the next match's machine, run so many frames a frame on its way
    constexpr uint32_t kShownFor = 240;   // frames of the knock-out (and the end's line) before the next is put on
    constexpr int kShuffleMinutes[] = {1, 2, 3, 5, 10}; // (endless, shuffled) minutes on a stage before it fades into another
    constexpr int kFadeFor = 75;                        //   frames of fading out (and as many in)
    constexpr int kScrollSpeeds[] = {64, 128, 256};     // (endless, scrolling) 256ths of a pixel a frame
    enum { kStageShuffle, kStageScroll };               // (endless_stage's first two choices; then the stages)
    constexpr int kOutPast = 40;          // (ring out) how far past its wall a fighter is out
    constexpr uint32_t kKnockedFor = 50;  // (ring out) frames after a hit that a fighter's wall lets it go

    enum Mode : uint8_t { kNone, kSurvival, kEndless, kTeam, kRingOut };

    struct State {
        Mode mode = kNone;
        bool started = false;       // the first match is on (the one the boot led to)
        uint32_t frames = 0, random = 0x2545F491u;
        uint8_t want[2] = {};       // the characters of the next match (the first's: what the choice took)
        uint8_t health[2] = {};     // health carried into the next match (0: full)
        uint8_t wins[2] = {};       // rounds won at the last frame (a round decided: one goes up)
        bool decided = false;       // the match on is decided: the next being made
        uint32_t decidedAt = 0;
        uint32_t touched[2] = {};   // (endless) when a person last worked each side
        uint8_t stage = 0;          // the stage behind (endless, shuffled or scrolling)
        uint8_t nextStage = 0;      // ... the one to come
        uint32_t onStageFor = 0;    // frames fought on it
        int fade = 0;               // the fade between two stages: 0 none, 1-kFadeFor out, then in
        int32_t scroll = 0;         // (scrolling) the camera's middle, in 256ths of a pixel
        bool scrolled = false;      // ... across to its right end
        // team
        uint8_t member[2][kMost] = {};
        uint8_t size = 2;
        uint8_t next[2] = {};
        // survival, team: over
        bool over = false;
        int8_t winner = -1;
        uint32_t overAt = 0;
        uint16_t won = 0; // (survival, ring out) opponents beaten
        // (ring out) the hits each side has taken at the last frame, the frame it was last hit, rung out this match
        uint16_t hits[2] = {};
        uint32_t hitAt[2] = {};
        bool out[2] = {};
    } b;
    std::vector<uint8_t> g_atChoice; // the machine at character select, both players in: where each match begins
    Spare g_next;

    uint32_t Next() {
        b.random ^= b.random << 13;
        b.random ^= b.random >> 17;
        b.random ^= b.random << 5;
        return b.random;
    }
    uint8_t RandomCharacter() {
        return static_cast<uint8_t>(Next() % kCharacterCount);
    }
    bool Locked() {
        return Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF;
    }
    bool Fought() {
        return Locked() && Ram8(sfiii3::kMatchState) == 2;
    }
    bool Person(int p) {
        switch (b.mode) {
        case kEndless: return b.touched[p] && b.frames - b.touched[p] < kTakenFor;
        case kSurvival:
        case kRingOut: return p == 0;
        case kTeam: return p == 0 || FeatureValue("team_opponent") == 1;
        default: return true;
        }
    }
    int Choice(const char *name) {
        const int value = FeatureValue(name);
        return value > 0 ? value - 1 : -1;
    }
    // The stage of the next match: endless's one stage, or (shuffled, scrolling: a stage drawn behind) the game's
    int StageWanted() {
        if (b.mode != kEndless) {
            return -1;
        }
        const int choice = FeatureValue("endless_stage");
        return choice <= kStageScroll ? -1 : choice - 2;
    }
    bool Behind() {
        return b.mode == kEndless && FeatureValue("endless_stage") <= kStageScroll;
    }
    uint8_t AnotherStage(uint8_t not1) {
        uint8_t stage = not1;
        for (int n = 0; n < 8 && stage == not1; n++) {
            stage = static_cast<uint8_t>(Next() % kCharacterCount);
        }
        return stage;
    }

    // A machine on its way through character select to its round, as the mode wants it: true once its round is fought
    bool ToRound(Machine &m, uint32_t frames, bool choosePlayerOne) {
        SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 2));
        const bool press = frames % 16 < 3;
        if (StageWanted() >= 0) {
            SetRam8(sfiii3::kStage, static_cast<uint8_t>(StageWanted()));
        }
        for (int p = 0; p < 2; p++) {
            const uint8_t state = Ram8(sfiii3::kSelectState[p]);
            if (state == 0) {
                m.players[p] = press ? kStart : 0;
            } else if (!Locked() && state >= 2 && (p == 1 || choosePlayerOne)) {
                SetRam8(sfiii3::kChosen[p], b.want[p]);
                m.players[p] = state < 5 && press ? kPunch1 : 0;
            }
        }
        if (!Locked()) {
            // (the versus screen and the rest: a press now and then)
            if (!m.players[0] && choosePlayerOne) {
                m.players[0] = press ? kPunch1 : 0;
            }
            if (!m.players[1]) {
                m.players[1] = press ? kPunch1 : 0;
            }
        }
        return Fought();
    }

    void Begin(Machine &machine, Mode mode) {
        session::StartSession(machine); // (the board reset, the way to character select for two players)
        SetFeature("training_mode", false);
        b = {};
        b.mode = mode;
        b.random ^= static_cast<uint32_t>(machine.frame * 2654435761u) | 1u;
        g_atChoice.clear();
        g_next.Clear();
        StageBehind(-1);
    }
    void StartSurvival(Machine &machine) {
        Begin(machine, kSurvival);
        b.want[1] = RandomCharacter();
    }
    void StartRingOut(Machine &machine) {
        Begin(machine, kRingOut);
        b.want[1] = RandomCharacter();
    }
    void StartEndless(Machine &machine) {
        Begin(machine, kEndless);
        b.stage = FeatureValue("endless_stage") == kStageScroll ? 0 : static_cast<uint8_t>(Next() % kCharacterCount);
        b.nextStage = b.stage; // (none to come yet)
        if (Behind()) {
            StageBehind(b.stage);
        }
        for (int p = 0; p < 2; p++) {
            const int chosen = Choice(p == 0 ? "endless_one" : "endless_two");
            b.want[p] = chosen >= 0 ? static_cast<uint8_t>(chosen) : RandomCharacter();
        }
    }
    void StartTeam(Machine &machine) {
        Begin(machine, kTeam);
        b.size = static_cast<uint8_t>(FeatureValue("team_size") == 1 ? 3 : 2);
        static const char *const kNames[2][kMost] = {{"team_1_1", "team_1_2", "team_1_3"}, {"team_2_1", "team_2_2", "team_2_3"}};
        for (int side = 0; side < 2; side++) {
            for (int n = 0; n < kMost; n++) {
                const int chosen = Choice(kNames[side][n]);
                b.member[side][n] = chosen >= 0 ? static_cast<uint8_t>(chosen) : RandomCharacter();
            }
            b.want[side] = b.member[side][0];
        }
    }

    // The match on decided: who goes on, and the next match begun on the spare
    void Decided(int winner) {
        const int loser = 1 - winner;
        if (getenv("SFIII3_BATTLE_LOG")) { // (debugging aid)
            fprintf(stderr, "battle: frame %u, side %d wins (%u to %u), the next: %d %d\n", b.frames, winner + 1, Ram8(sfiii3::kRoundsWon[0]),
                    Ram8(sfiii3::kRoundsWon[1]), b.want[0], b.want[1]);
        }
        b.decided = true;
        b.decidedAt = b.frames;
        const uint8_t left = Ram8(sfiii3::kPlayer[winner] + sfiii3::kLife);
        b.health[winner] = left > 0 && left <= sfiii3::kFullLife ? left : 0;
        b.health[loser] = 0;
        b.want[winner] = static_cast<uint8_t>(Ram16(sfiii3::kPlayer[winner] + 0x3C0) % kCharacterCount);
        switch (b.mode) {
        case kEndless: {
            const int chosen = Choice(loser == 0 ? "endless_one" : "endless_two");
            b.want[loser] = chosen >= 0 ? static_cast<uint8_t>(chosen) : RandomCharacter();
            b.health[winner] = 0; // (both fresh: the next pair's fight)

            break;
        }
        case kTeam:
            if (++b.next[loser] >= b.size) {
                b.over = true;
                b.winner = static_cast<int8_t>(winner);
                b.overAt = b.frames;
                return;
            }
            b.want[loser] = b.member[loser][b.next[loser]];
            break;
        case kRingOut:
            if (loser == 0) {
                b.over = true;
                b.winner = 1;
                b.overAt = b.frames;
                return;
            }
            b.won++;
            if (b.won > FeatureValue("ringout_best") && !RunningAhead()) {
                SetFeatureValue("ringout_best", std::min<int>(b.won, 99));
            }
            b.health[0] = 0;
            b.want[1] = RandomCharacter();
            break;
        case kSurvival:
            if (loser == 0) {
                b.over = true;
                b.winner = 1;
                b.overAt = b.frames;
                return;
            }
            b.won++;
            if (b.won > FeatureValue("survival_best") && !RunningAhead()) {
                SetFeatureValue("survival_best", std::min<int>(b.won, 99));
            }
            b.health[0] = static_cast<uint8_t>(std::min<int>(sfiii3::kFullLife, (b.health[0] ? b.health[0] : sfiii3::kFullLife) + sfiii3::kFullLife / 10));
            b.want[1] = RandomCharacter();
            break;
        default: break;
        }
        g_next.Clear();
        g_next.state = g_atChoice;
    }

    // The machine as a match begins on it: the carried health, the computer's sides
    void MatchBegins() {
        for (int p = 0; p < 2; p++) {
            b.out[p] = false;
            b.hits[p] = Ram16(sfiii3::kPlayer[p] + sfiii3::kHitCount);
            b.hitAt[p] = 0;
            if (b.health[p] > 0 && b.health[p] < sfiii3::kFullLife) {
                SetRam8(sfiii3::kPlayer[p] + sfiii3::kLife, b.health[p]);
            }
            b.wins[p] = Ram8(sfiii3::kRoundsWon[p]);
        }
    }
} // namespace

// The walls (0x0611DFB8: a player's record in r4, the wall in r5, which side in r6; it puts the player back inside
// the wall, notes that it is against it (+0x3C9, +0x3CA) and by how much it went over (+0x3D6), and returns 0 in r0
// when it did nothing). In a ring out a fighter knocked back (hit within kKnockedFor frames) is let through.
namespace {
    bool WallLift(Guest &g) {
        if (b.mode != kRingOut || !b.started || !Fought() || b.decided) {
            return false; // (and never for a spare's machine, on its way to its round)
        }
        for (int p = 0; p < 2; p++) {
            if (g.R(4) == sfiii3::kPlayer[p] && b.hitAt[p] && b.frames - b.hitAt[p] < kKnockedFor) {
                g.R(0) = 0;
                g.Return(40);
                return true;
            }
        }
        return false;
    }
    const Lift kWallLift[] = {{"Wall (let go in a ring out)", 0x0611DFB8, 8, 0x2E0F9B3860C660B1ull, WallLift}};
} // namespace

// The endless battle's stage behind its fighters: shuffled (fought on for its minutes, then faded into another made
// ready beside it), or scrolling (its camera carried across it at the speed chosen, then the next in order faded in)
namespace {
    void StageBehindFrame(Machine &m) {
        const bool scrolling = FeatureValue("endless_stage") == kStageScroll;
        int left = 0, right = 0;
        const bool shown = StageBehindShown() && StageBehindEnds(left, right);
        if (getenv("SFIII3_BATTLE_LOG") && b.frames % 600 == 0) {
            fprintf(stderr, "behind: frame %u stage %d next %d shown %d ends %d-%d for %u fade %d ready %d\n", b.frames, b.stage, b.nextStage, shown,
                    left, right, b.onStageFor, b.fade, StageBehindNextReady());
        }
        if (scrolling && shown && b.fade == 0) {
            const int speed = kScrollSpeeds[std::clamp(FeatureValue("endless_scroll"), 0, 2)];
            if (b.scroll == 0) {
                b.scroll = left * 256;
            }
            b.scroll += speed;
            if (b.scroll >= right * 256) {
                b.scroll = right * 256;
                b.scrolled = true;
            }
            StageBehindCamera(b.scroll / 256);
        }
        if (shown && b.fade == 0) {
            b.onStageFor++;
        }
        // (the next stage begun: shuffled, after the minutes on this one, less the time to make it; scrolling, at once)
        const uint32_t minutes = static_cast<uint32_t>(kShuffleMinutes[std::clamp(FeatureValue("endless_shuffle"), 0, 4)]);
        const bool due = scrolling ? b.scrolled : b.onStageFor >= minutes * 3600;
        if (shown && b.nextStage == b.stage && (scrolling || b.onStageFor + 600 >= minutes * 3600)) {
            b.nextStage = scrolling ? static_cast<uint8_t>((b.stage + 1) % kCharacterCount) : AnotherStage(b.stage);
            StageBehindNext(b.nextStage);
        }
        // (the fade: this stage out to black, the next one put in its place, and in from black)
        if (b.fade == 0 && due && b.nextStage != b.stage && StageBehindNextReady()) {
            b.fade = 1;
        }
        if (b.fade > 0) {
            if (b.fade == kFadeFor) {
                StageBehindSwitch(m);
                b.stage = b.nextStage;
                b.onStageFor = 0;
                b.scroll = 0;
                b.scrolled = false;
                StageBehindCamera(scrolling ? left : StageBehindFollow());
            }
            m.stageDarken = (b.fade <= kFadeFor ? b.fade : 2 * kFadeFor - b.fade) * 256 / kFadeFor;
            if (++b.fade > 2 * kFadeFor) {
                b.fade = 0;
                m.stageDarken = 0;
            }
        }
    }
} // namespace

bool BattleOn() {
    return b.mode != kNone;
}

void BattleBeforeFrame(Machine &m) {
    m.hidePanel = false;
    if (b.mode != kNone && session::InSession()) {
        b.mode = kNone; // (a training session started: the mode is over)
        StageBehind(-1);
    }
    if (b.mode == kNone || session::Booting()) {
        return;
    }
    b.frames++;
    SetRam8(sfiii3::kCredits, std::max<uint8_t>(Ram8(sfiii3::kCredits), 2));
    if (b.mode == kEndless) {
        m.hidePanel = true; // (no bars, no clock: the game's panel is its text layer)
    }
    if ((b.mode == kEndless || b.mode == kRingOut) && Ram8(sfiii3::kClock) > 0 && Ram8(sfiii3::kClock) < 99) {
        SetRam8(sfiii3::kClock, 99); // (no clock: a round is ended by a knock-out, or a ring out)
    }
    if (Behind() && b.started) {
        StageBehindFrame(m);
    }
    if (b.over) {
        // (the battle won or the run over: said for a while, then the mode ends and the board starts over)
        if (b.frames - b.overAt > kShownFor + 60) {
            b.mode = kNone;
            StageBehind(-1);
            m.Reset(); // (back to the board's start, as after a reset)
            StateLoaded();
        }
        return;
    }
    // The first match: the machine at character select kept (where every later match begins), the choice made
    if (!b.started) {
        if (g_atChoice.empty() && Ram8(sfiii3::kSelectState[0]) == 2 && Ram8(sfiii3::kSelectState[1]) == 2) {
            KeepMachine(m, g_atChoice);
        }
        if (g_atChoice.empty()) {
            return;
        }
        // (in survival player 1 chooses its own character, and keeps it)
        const uint16_t mine = m.players[0];
        const bool chooses = b.mode == kSurvival || b.mode == kRingOut;
        const bool fought = ToRound(m, b.frames, !chooses);
        if (chooses) {
            m.players[0] = mine;
        }
        if (fought) {
            b.started = true;
            if (chooses) {
                b.want[0] = static_cast<uint8_t>(Ram16(sfiii3::kPlayer[0] + 0x3C0) % kCharacterCount);
            }
            MatchBegins();
        }
        return;
    }
    // A person takes an endless battle's fighter by touching its controls
    for (int p = 0; p < 2; p++) {
        if (b.mode == kEndless && (m.players[p] & (fight::kSticks | fight::kButtons))) {
            b.touched[p] = b.frames;
        }
        if (Locked()) {
            SetRam8(sfiii3::kPlayer[p] + 3, Person(p) ? 1 : 0); // (0: the computer plays it)
        }
        if (!Person(p)) {
            m.players[p] = 0;
        }
    }
    // Ring out: no health lost; a fighter knocked back over a stage's end (its wall let go: WallLift) is out
    if (b.mode == kRingOut && Locked()) {
        const int left = static_cast<int16_t>(Ram16(sfiii3::kCameraLeft[0])) - 164, right = static_cast<int16_t>(Ram16(sfiii3::kCameraRight[0])) + 164;
        for (int p = 0; p < 2; p++) {
            const uint32_t base = sfiii3::kPlayer[p];
            const uint16_t hits = Ram16(base + sfiii3::kHitCount);
            if (hits != b.hits[p]) {
                b.hits[p] = hits;
                b.hitAt[p] = b.frames;
            }
            const int x = static_cast<int16_t>(Ram16(base + sfiii3::kX));
            if (!b.out[0] && !b.out[1] && Fought() && (x < left - kOutPast || x > right + kOutPast)) {
                b.out[p] = true;
                if (getenv("SFIII3_BATTLE_LOG")) {
                    fprintf(stderr, "battle: frame %u, side %d thrown out at %d (walls %d %d)\n", b.frames, p + 1, x, left, right);
                }
            }
            SetRam8(base + sfiii3::kLife, b.out[p] ? 0 : sfiii3::kFullLife);
        }
    }
    // A round decided: the match is
    if (!b.decided && Locked()) {
        for (int p = 0; p < 2; p++) {
            if (Ram8(sfiii3::kRoundsWon[p]) > b.wins[p]) {
                Decided(p);
                break;
            }
        }
    }
    // The next match made ready on the spare, a few frames a frame, and put in this one's place when its knock-out
    // has played
    if (b.decided && !b.over) {
        for (int n = 0; n < kSpareFramesAFrame && !g_next.ready; n++) {
            RunSpare(m, g_next, [](Machine &spare) {
                g_next.ready = ToRound(spare, static_cast<uint32_t>(g_next.frames), true);
            });
        }
        if (g_next.ready && b.frames - b.decidedAt >= kShownFor) {
            if (getenv("SFIII3_BATTLE_LOG")) {
                fprintf(stderr, "battle: frame %u, the next match on (its machine run %d frames), %d %d\n", b.frames, g_next.frames, b.want[0], b.want[1]);
            }
            Promote(m, g_next);
            b.decided = false;
            MatchBegins();
        }
    }
}

void BattleHud(std::vector<HudLine> &lines) {
    char text[64];
    switch (b.mode) {
    case kSurvival:
        snprintf(text, sizeof(text), "SURVIVAL  %u BEATEN  BEST %d", b.won, std::max<int>(b.won, FeatureValue("survival_best")));
        lines.push_back({0.5f, 0.235f, 0, text, 255, 210, 80});
        if (b.over) {
            lines.push_back({0.5f, 0.40f, 0, "THE RUN IS OVER", 255, 230, 60});
        }
        break;
    case kTeam:
        for (int side = 0; side < 2; side++) {
            std::string line = side == 0 ? "TEAM 1: " : "TEAM 2: ";
            for (int n = 0; n < b.size; n++) {
                std::string name = kCharacters[b.member[side][n] % kCharacterCount];
                for (char &c : name) {
                    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                }
                line += (n ? "  " : "") + (n < b.next[side] ? "(" + name + ")" : n == b.next[side] ? "[" + name + "]" : name);
            }
            lines.push_back({side == 0 ? 0.03f : 0.97f, side == 0 ? 0.235f : 0.275f, side == 0 ? -1 : 1, line, 160, 220, 255});
        }
        if (b.over) {
            lines.push_back({0.5f, 0.40f, 0, b.winner == 0 ? "TEAM 1 WINS THE BATTLE" : "TEAM 2 WINS THE BATTLE", 255, 230, 60});
        }
        break;
    case kRingOut:
        snprintf(text, sizeof(text), "RING OUT  %u THROWN OUT  BEST %d", b.won, std::max<int>(b.won, FeatureValue("ringout_best")));
        lines.push_back({0.5f, 0.235f, 0, text, 255, 160, 80});
        if (b.out[0] || b.out[1]) {
            lines.push_back({0.5f, 0.40f, 0, "RING OUT!", 255, 230, 60});
        }
        break;
    default: break; // (endless: nothing over it)
    }
}

void BattleState(Archive &ar) {
    ar.Mark(0x4241544C); // BATL
    ar.Pod(b);
    ar.Vec(g_atChoice);
    ar.Vec(g_next.state);
    ar.Pod(g_next.ready);
    ar.Pod(g_next.frames);
}

void RegisterBattle(Game &game) {
    std::vector<const char *> fighters = {"Random"};
    for (const char *name : kCharacters) {
        fighters.push_back(name);
    }
    // (records kept with the features; the numbers are their choices)
    static std::vector<std::string> numbers;
    if (numbers.empty()) {
        numbers.reserve(100);
        for (int n = 0; n < 100; n++) {
            numbers.push_back(std::to_string(n));
        }
    }
    std::vector<const char *> counts;
    for (const std::string &number : numbers) {
        counts.push_back(number.c_str());
    }

    game.commands.push_back({"survival_start", "Survival", "Start",
                             "Choose a character; then one opponent after another, each on a knock-down. Your health is what the "
                             "fight before left you, and a little back. Over the first time you go down.",
                             StartSurvival});
    game.features.push_back({"survival_best", "Survival", "Best", "The most opponents beaten in one survival. (Left puts it back.)",
                             kFeatureOwn, counts});

    game.commands.push_back({"ringout_start", "Ring out", "Start",
                             "Choose a character; then one opponent after another, with no health: knock your opponent back over an "
                             "end of the stage to throw them out. Over the first time you are thrown out.",
                             StartRingOut});
    game.features.push_back({"ringout_best", "Ring out", "Best", "The most opponents thrown out in one go. (Left puts it back.)",
                             kFeatureOwn, counts});
    RegisterLifts(kWallLift, std::size(kWallLift));

    game.commands.push_back({"endless_start", "Endless battle", "Start",
                             "Two fighters played by the game: no bars, no clock, and the one knocked down is replaced. Press "
                             "anything to take a fighter over; ten idle seconds give it back.",
                             StartEndless});
    game.features.push_back({"endless_one", "Endless battle", "Fighter 1", "Who is on player 1's side: someone new each time, or always the same.",
                             kFeatureMenuChoices, fighters});
    game.features.push_back({"endless_two", "Endless battle", "Fighter 2", "Who is on player 2's side: someone new each time, or always the same.",
                             kFeatureMenuChoices, fighters});
    static std::vector<std::string> stageNames;
    if (stageNames.empty()) {
        for (const char *name : kCharacters) {
            stageNames.push_back(std::string(name) + "'s");
        }
    }
    std::vector<const char *> stages = {"Shuffle", "Scroll"};
    for (const std::string &name : stageNames) {
        stages.push_back(name.c_str());
    }
    game.features.push_back({"endless_stage", "Endless battle", "Stage",
                             "Where they fight. Shuffle: a stage that fades into another every so often. Scroll: the stages go by, one "
                             "after another, and the fighters are carried along. Or one stage.",
                             kFeatureMenuChoices, stages});
    game.features.push_back({"endless_shuffle", "Endless battle", "Shuffle every", "Shuffle: how long they fight on a stage before it fades "
                             "into another.", 0, {"1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes"}});
    game.features.push_back({"endless_scroll", "Endless battle", "Scroll speed", "Scroll: how fast the stages go by.", 0, {"Slow", "Medium", "Fast"}});

    game.commands.push_back({"team_start", "Team battle", "Start",
                             "Two teams: knock a fighter down and the next of that team comes on with full health; the winner stays "
                             "on with the health it has left. The last team standing wins.",
                             StartTeam});
    game.features.push_back({"team_size", "Team battle", "Fighters", "How many fighters each team has.", 0, {"2", "3"}});
    game.features.push_back({"team_opponent", "Team battle", "Team 2", "Who plays the second team: the computer, or player 2.", 0,
                             {"Computer", "Player 2"}});
    static const char *const kTitles[2][kMost] = {{"Team 1, first", "Team 1, second", "Team 1, third"},
                                                  {"Team 2, first", "Team 2, second", "Team 2, third"}};
    static const char *const kNames[2][kMost] = {{"team_1_1", "team_1_2", "team_1_3"}, {"team_2_1", "team_2_2", "team_2_3"}};
    for (int side = 0; side < 2; side++) {
        for (int n = 0; n < kMost; n++) {
            game.features.push_back({kNames[side][n], "Team battle", kTitles[side][n],
                                     n == 2 ? "The team's third fighter (with three a team): someone at random, or this one." :
                                              "The team's fighter: someone at random, or this one.",
                                     kFeatureMenuChoices, fighters});
        }
    }
}

} // namespace cps3recomp
