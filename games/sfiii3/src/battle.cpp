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
#include <cstring>
#include <iterator>
#include <string>
#include <map>
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
void StageBehindMusic(bool on);
void StageBehindOnlyMusic(bool on);
void StageBehindNextBeside(Machine &machine, int stage);
void StageBehindNextCamera(int x);
bool StageBehindNextEnds(int &left, int &right);
void StageBehindSlide(Machine &machine, int columns, int gain);
void StageBehindSlideSwitch(Machine &machine);
void StageBehindNoSecond(Machine &machine);

namespace {
    constexpr const char *kTeamPage = "Tag team battle";
    constexpr float kLabelY = 0.20f, kTeamLinesY = 0.235f; // (under the bars and names: who is on, and the others of the side)
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
    constexpr int32_t kViewerStick = 384;               // (the stage viewer) ... with the stick held
    enum { kStageScroll, kStageShuffle };               // (endless_stage's first two choices; then the stages)
    constexpr uint32_t kKnockedFor = 50;  // (ring out) frames after a hit that a fighter counts as knocked about
    // The ring out (cps2recomp's fight/battle.cpp and Alpha 2's player.cpp: the same numbers)
    constexpr int kFallFor = 56;          // frames the one out is seen flung out and falling
    constexpr int32_t kFlungAcross = 192, kFlungUp = 1536, kPull = 104; // (256ths of a pixel a frame: away, up; taken off up)
    constexpr int kGoneBelow = 400;       // how far under the floor one falling stops
    constexpr int kDropJump = 8;          // the frame of the way down at which the winner jumps off the old stage
    constexpr int kDropInside = 48;       // the winner is kept at least this far inside the picture's edges
    constexpr int kDropFollow = 4;        // the camera goes a quarter of the way to where it follows the winner a frame
    constexpr int kDropMost = 400;        // frames after which the way down is over, landed or not
    constexpr int kHalfWidth = 24;        // about half a fighter's width
    constexpr int32_t kDropPull = 128;    // (the winner seen falling: 256ths of a pixel a frame taken on each frame, as its jump's)
    // The winner's objects (player 1's: their palettes 0x08-0x0F, as the model viewer tells them)
    bool WinnerObject(const machine::Machine &board, uint32_t object) {
        const uint32_t *list = board.SpriteList();
        const uint32_t palette = list[object * 4] & 0x1FF;
        return (list[object * 4 + 2] & 3) != 0 && palette >= 0x08 && palette <= 0x0F;
    }

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
        // (the stage viewer: an endless battle unseen, only the stage behind it drawn) its camera's middle in 256ths of
        // a pixel, which way it goes by itself, Infinite (every stage in turn), and the menu's stage it was started for
        bool viewer = false;
        int32_t camera = 0;
        int8_t direction = 1;
        bool infinite = false;
        int8_t viewed = -1;
        int fade = 0;               // the fade between two stages: 0 none, 1-kFadeFor out, then in
        int32_t scroll = 0;         // (scrolling) the camera's middle, in 256ths of a pixel
        bool scrolled = false;      // ... across to its right end
        int32_t slide = 0;          // ... and the next stage come in from the right since (256ths of a pixel; continuous)
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
        int drop = 0;           // (ring out) frames into the way down (0: none)
        int8_t jumper = -1;     // (ring out) the winner, who jumps off the old stage and comes down onto the next
        // (ring out, as the CPS-2 ports have it: 0 fighting; 1 the one out flung over the end, falling; 2 the way down, the
        // winner jumping off and handed to the next match at the top of its jump, the camera following it down)
        // (survival, ring out: the player's colours and super art as chosen at character select, every later match's too;
        // 0xFF: not yet known)
        uint8_t colour = 0xFF, art = 0xFF;
        uint8_t ring = 0;
        int8_t outWho = -1, outSide = 0;
        int32_t fx = 0, fy = 0, vx = 0, vy = 0; // (the one out: where, and its speeds, in 256ths of a pixel)
        uint16_t since = 0;
        int8_t dropStage = -1;               // (the next match's stage)
        bool handed = false;                 // (the winner is in the next match)
        int16_t apexX = 0, ringY = 0, handedY = 0, cameraY = 0;
        int32_t raise = 0;                   // (how far above the next stage the camera is held, 256ths of a pixel)
        int16_t jumpX = 0;      // ... where in the picture it was as it went (columns from the picture's middle)
        // (team battle, the tag style) each fighter's health while it rests (0: down), the stage fought on, when each side
        // may tag again, the characters each side's tag spare was made for, and the computer's last look
        bool tagStyle = false;
        uint8_t kept[2][kMost] = {};
        uint8_t stageNow = 0;
        uint32_t tagCool[2] = {};
        uint32_t taggedAt = 0;      // (the frame of the last tag)
        int8_t madeFor[2][2] = {{-1, -1}, {-1, -1}};
        uint16_t rest = 0;
        bool startWas[4] = {};
        // (the tag's movements: the one on jumps back and off the picture, the next jumps in from that end)
        uint8_t move[2] = {};       // 0 none, 1 leaving, 2 coming, 3 knocked out (lying, the next to come in)
        uint16_t moveFor[2] = {};
        int8_t from[2] = {};        // the end it left by (-1 the left)
        bool jumped[2] = {};
        uint16_t hitsAt[2] = {};    // (hits taken as it began to leave: hit on the way, the tag is off)
        int8_t panelFor = -1;       // (a tag's side, its panel to put back the next frame: PanelFrame)
        uint8_t scores[0x20] = {};
        uint8_t gauge[0x2C] = {};
        uint8_t teamCount = 0, teamFill = 0; // (the tagging side's gauge: bars, and the fill of the next)
        uint16_t landedAt[2] = {};
        uint16_t freeFor[2] = {};   // (the computer's tags) frames each fighter has been free to act
        uint16_t busyFor[2] = {};   // ... and not free (reeling, knocked down, in a move)  // (knocked out) the frame of the move it landed (0: not yet)
        bool attacked[2] = {};      // (coming) its jump attack made
        int16_t lastY[2] = {};      // (coming) its height at the last frame
    } b;
    Spare g_tag[2]; // (the tag style) each side's next fighter's fight, made ready
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
    // (the tag team's people: co-op, players 1 and 2 are side 1's fighters in turn; 2 vs 2, players 1 and 3 side 1's and
    // 2 and 4 side 2's. Which of the port's controllers plays a side's fighter on now; -1: the usual one)
    int PeopleStyle() {
        return b.mode == kTeam && b.tagStyle ? FeatureValue("tag_team") : 0;
    }
    int PadOf(int side, int member) {
        switch (PeopleStyle()) {
        case 1: return side == 0 ? member % 2 : -1;
        case 2: return side == 0 ? (member % 2 ? 2 : 0) : (member % 2 ? 3 : 1);
        default: return -1;
        }
    }
    bool Person(int p) {
        if (b.mode == kTeam && PeopleStyle() == 1) {
            return p == 0; // (co-op: side 2 the computer's)
        }
        if (b.mode == kTeam && PeopleStyle() == 2) {
            return true;
        }
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
        if (b.mode == kTeam) {
            const int chosen = FeatureValue("team_stage"); // (0: random, the game's own choice each match)
            return chosen > 0 ? chosen - 1 : -1;
        }
        if (b.mode != kEndless) {
            return -1;
        }
        if (b.viewer) {
            // (the unseen fight's: the stage whose tune is chosen, or the one shown)
            const int music = FeatureValue("viewer_music");
            return music > 0 ? music - 1 : b.infinite ? 0 : b.stage;
        }
        const int choice = FeatureValue("endless_stage");
        return choice <= kStageShuffle ? -1 : choice - 2;
    }
    bool Behind() {
        return b.mode == kEndless && (b.viewer || FeatureValue("endless_stage") <= kStageShuffle);
    }
    bool Scrolling() { // (the stages going by, one after another)
        return b.viewer ? b.infinite : FeatureValue("endless_stage") == kStageScroll;
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
        if (b.mode == kTeam && b.tagStyle && b.started) {
            SetRam8(sfiii3::kStage, b.stageNow); // (a tag's fight: on the stage fought on)
        }
        for (int p = 0; p < 2; p++) {
            const uint8_t state = Ram8(sfiii3::kSelectState[p]);
            if (state == 0) {
                m.players[p] = press ? kStart : 0;
            } else if (!Locked() && state >= 2 && (p == 1 || choosePlayerOne)) {
                SetRam8(sfiii3::kChosen[p], b.want[p]);
                m.players[p] = state < 5 && press ? kPunch1 : 0;
                if (p == 0 && b.colour != 0xFF && (b.mode == kSurvival || b.mode == kRingOut)) {
                    // (the player's character as it chose it: its super art (the cursor moved down to it), its colours)
                    if (state == 4 && Ram8(sfiii3::kSelectArt[0]) != b.art) {
                        m.players[0] = press ? kDown : 0;
                    }
                    if (state >= 4) {
                        SetRam8(sfiii3::kSelectColour[0], b.colour);
                    }
                }
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
        g_tag[0].Clear();
        g_tag[1].Clear();
        StageBehind(-1);
    }
    // The stage viewer: an endless battle of the game's fighters, unseen, with the chosen stage behind it on the stage
    // board, the only thing drawn; its camera taken across with the stick or by itself
    void StartViewer(Machine &machine) {
        Begin(machine, kEndless);
        b.viewer = true;
        b.viewed = static_cast<int8_t>(FeatureValue("viewer_stage"));
        b.infinite = b.viewed >= kCharacterCount;
        b.stage = b.infinite ? 0 : static_cast<uint8_t>(b.viewed);
        b.nextStage = b.stage;
        StageBehind(b.stage);
        StageBehindMusic(FeatureValue("viewer_music") == 0);
        b.want[0] = RandomCharacter();
        b.want[1] = RandomCharacter();
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
        b.stage = Scrolling() ? 0 : static_cast<uint8_t>(Next() % kCharacterCount);
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
            for (int n = 0; n < kMost; n++) {
                b.kept[side][n] = n < b.size ? sfiii3::kFullLife : 0;
            }
        }
        b.tagStyle = FeatureValue("team_style") == 0; // (the tag style first: what a team battle is unless chosen)
    }

    // (the tag style) a side's next fighter still standing, after the one on (-1: none)
    int NextUp(int side) {
        for (int k = 1; k < b.size; k++) {
            const int n = (b.next[side] + k) % b.size;
            if (b.kept[side][n] > 0) {
                return n;
            }
        }
        return -1;
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
            if (b.tagStyle) {
                // (a time over: one round decides it, the team with more health in all the winner, as TimeOver gave the
                // game's time over)
                if (Ram8(sfiii3::kPlayer[loser] + sfiii3::kKnockedOut) == 0) {
                    b.over = true;
                    b.winner = static_cast<int8_t>(winner);
                    b.overAt = b.frames;
                    return;
                }
                // (the one knocked out is down; the side's next standing comes on with the health it rested with)
                b.kept[loser][b.next[loser]] = 0;
                b.kept[winner][b.next[winner]] = b.health[winner] ? b.health[winner] : sfiii3::kFullLife;
                const int next = NextUp(loser);
                if (next < 0) {
                    b.over = true;
                    b.winner = static_cast<int8_t>(winner);
                    b.overAt = b.frames;
                    return;
                }
                b.next[loser] = static_cast<uint8_t>(next);
                b.health[loser] = b.kept[loser][next];
                b.want[loser] = b.member[loser][next];
                break;
            }
            if (++b.next[loser] >= b.size) {
                b.over = true;
                b.winner = static_cast<int8_t>(winner);
                b.overAt = b.frames;
                return;
            }
            b.want[loser] = b.member[loser][b.next[loser]];
            // (the winner stays on with what it has left and a little back, as survival gives: a tenth)
            b.health[winner] = static_cast<uint8_t>(std::min<int>(sfiii3::kFullLife, (b.health[winner] ? b.health[winner] : sfiii3::kFullLife) +
                                                                                       sfiii3::kFullLife / 10));
            break;
        case kRingOut:
            if (loser == 0) {
                b.over = true;
                b.winner = 1;
                b.overAt = b.frames;
                return;
            }
            b.jumper = static_cast<int8_t>(winner);
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
        b.stageNow = Ram8(sfiii3::kStage);
        if (b.mode == kRingOut) {
            // (the next match made ready while this one is fought: the player's character against someone new, on another
            // stage)
            b.ring = 0;
            b.outWho = -1;
            b.handed = false;
            b.want[1] = RandomCharacter();
            b.dropStage = static_cast<int8_t>(AnotherStage(b.stageNow));
            g_next.Clear();
            g_next.state = g_atChoice;
        }
    }

    // --- the ring out's ends: a fighter knocked about (reeling from a hit, guarding one, thrown) at an end of the stage
    // where the camera is at that end is not kept in, and once past the picture's edge it is out ---
    int16_t Word(uint32_t at) {
        return static_cast<int16_t>(Ram16(at));
    }
    bool KnockedAbout(int p) {
        const uint32_t base = sfiii3::kPlayer[p];
        const bool reeling = b.hitAt[p] && b.frames - b.hitAt[p] < kKnockedFor && Ram8(base + sfiii3::kAttacking) == 0 &&
                             ((Ram16(base + sfiii3::kBusy) & 0xFF) != 0 || Word(base + sfiii3::kY) > 0);
        return reeling || Ram8(base + sfiii3::kBlocking) != 0 || Ram8(base + sfiii3::kThrown) != 0;
    }
    // The end of the stage a player is at, where the camera is at it too (-1 the left, 1 the right; 0 neither)
    int OpenEnd(int p) {
        const int camera = Word(sfiii3::kCameraX), x = Word(sfiii3::kPlayer[p] + sfiii3::kX);
        if (x < camera && camera <= Word(sfiii3::kCameraLeft[0]) + 1) {
            return -1;
        }
        if (x > camera && camera >= Word(sfiii3::kCameraRight[0]) - 1) {
            return 1;
        }
        return 0;
    }
    // ... and the picture's edge on that side, in the stage (its middle is the camera's; widescreen's columns too)
    int PictureEdge(int side) {
        const Machine *m = TheMachine();
        const int half = 192 + (m ? m->WideMargin() : 0);
        return Word(sfiii3::kCameraX) + side * half;
    }
    bool RingOutOpen(int p) {
        return b.mode == kRingOut && b.started && Fought() && (b.outWho == p || (b.ring == 0 && KnockedAbout(p) && OpenEnd(p) != 0));
    }
} // namespace

// The walls (0x0611DFB8: a player's record in r4, the wall in r5, which side in r6; it puts the player back inside
// the wall, notes that it is against it (+0x3C9, +0x3CA) and by how much it went over (+0x3D6), and returns 0 in r0
// when it did nothing). In a ring out a fighter knocked back (hit within kKnockedFor frames) is let through.
namespace {
    bool WallLift(Guest &g) {
        // (a tag: the one leaving flies off through its wall, the one coming in comes through it)
        if (b.mode == kTeam && b.tagStyle && b.started && Fought()) {
            for (int p = 0; p < 2; p++) {
                if (g.R(4) == sfiii3::kPlayer[p] && (b.move[p] == 1 || b.move[p] == 2)) {
                    g.R(0) = 0;
                    g.Return(40);
                    return true;
                }
            }
            return false;
        }
        if (b.mode != kRingOut || !b.started || !Fought() || InSpare()) {
            return false; // (and never for a spare's machine, on its way to its round)
        }
        for (int p = 0; p < 2; p++) {
            if (g.R(4) == sfiii3::kPlayer[p] && RingOutOpen(p)) {
                g.R(0) = 0;
                g.Return(40);
                return true;
            }
        }
        return false;
    }
    // The round's knock-out (0x06117D28: each player's record's byte +0x3FD says it is knocked out; one or both, and the
    // round is over). In the tag style a fighter knocked out whose team has another standing is not the round's end:
    // it lies there, and the next one comes in (TagFrame).
    bool KnockOutLift(Guest &g) {
        if (b.mode != kTeam || !b.tagStyle || !b.started || b.decided || !Fought()) {
            return false;
        }
        bool any = false;
        for (int p = 0; p < 2; p++) {
            if (Ram8(sfiii3::kPlayer[p] + sfiii3::kKnockedOut) != 0) {
                if (NextUp(p) < 0) {
                    return false; // (the team's last: the game's knock-out)
                }
                any = true;
            }
        }
        if (!any) {
            return false;
        }
        g.Return(20);
        return true;
    }
    const Lift kWallLift[] = {{"Wall (let go in a ring out)", 0x0611DFB8, 8, 0x2E0F9B3860C660B1ull, WallLift},
                              {"Knock-out (not the end in a tag)", 0x06117D28, 8, 0x5E15D2C766C200CDull, KnockOutLift}};
} // namespace

// The endless battle's stage behind its fighters: shuffled (fought on for its minutes, then faded into another made
// ready beside it), or scrolling (its camera carried across it at the speed chosen, then the next in order faded in)
namespace {
    void StageBehindFrame(Machine &m) {
        const bool scrolling = Scrolling();
        int left = 0, right = 0;
        const bool shown = StageBehindShown() && StageBehindEnds(left, right);
        if (getenv("SFIII3_BATTLE_LOG") && b.frames % 600 == 0) {
            fprintf(stderr, "behind: frame %u stage %d next %d shown %d ends %d-%d for %u fade %d ready %d\n", b.frames, b.stage, b.nextStage, shown,
                    left, right, b.onStageFor, b.fade, StageBehindNextReady());
        }
        if (b.viewer && shown && b.fade == 0) {
            // (the viewer: with the stick, or by itself from end to end and back; Infinite, onwards)
            const int scroll = std::clamp(FeatureValue("viewer_scroll"), 0, 3);
            const uint16_t stick = (m.players[0] | m.players[1]) & (kLeft | kRight);
            const int32_t byStick = stick == kRight ? kViewerStick : stick == kLeft ? -kViewerStick : 0;
            if (b.camera == 0) {
                b.camera = (scrolling ? left : (left + right) / 2) * 256;
            }
            if (scroll == 0) {
                b.camera += byStick;
            } else {
                b.camera += (scrolling ? 1 : b.direction) * kScrollSpeeds[scroll - 1];
            }
            if (b.camera >= right * 256) {
                b.camera = right * 256;
                b.direction = -1;
                b.scrolled = scrolling;
            } else if (b.camera <= left * 256) {
                b.camera = left * 256;
                b.direction = 1;
            }
            StageBehindCamera(b.camera / 256);
        } else if (scrolling && shown && b.fade == 0) {
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
        // (the next stage begun: shuffled, after the minutes on this one, less the time to make it; scrolling, at once:
        // continuous, on the second stage board beside this one, to come in from the right as this one goes out)
        const uint32_t minutes = static_cast<uint32_t>(kShuffleMinutes[std::clamp(FeatureValue("endless_shuffle"), 0, 4)]);
        const bool continuous = scrolling && (b.viewer || FeatureValue("endless_between") == 0);
        const bool due = scrolling ? b.scrolled : b.onStageFor >= minutes * 3600;
        if (shown && b.nextStage == b.stage && (scrolling || b.onStageFor + 600 >= minutes * 3600)) {
            b.nextStage = scrolling ? static_cast<uint8_t>((b.stage + 1) % kCharacterCount) : AnotherStage(b.stage);
            if (continuous) {
                StageBehindNextBeside(m, b.nextStage);
            } else {
                StageBehindNext(b.nextStage);
            }
        }
        if (continuous && b.fade == 0) {
            // (across this stage, then this one out to the left and the next in from the right, its camera at its left
            // end; once it is all there, it is the stage)
            int nextLeft = 0, nextRight = 0;
            if (due && b.nextStage != b.stage && StageBehindNextEnds(nextLeft, nextRight)) {
                const int width = std::max(m.PictureWidth(), 1);
                const int speed = b.viewer ? kScrollSpeeds[std::clamp(FeatureValue("viewer_scroll"), 1, 3) - 1] :
                                             kScrollSpeeds[std::clamp(FeatureValue("endless_scroll"), 0, 2)];
                StageBehindNextCamera(nextLeft);
                b.slide = std::min(b.slide + speed, width * 256);
                const int columns = b.slide / 256;
                StageBehindSlide(m, columns, columns * 256 / width);
                if (columns >= width) {
                    StageBehindSlideSwitch(m);
                    b.stage = b.nextStage;
                    b.onStageFor = 0;
                    b.slide = 0;
                    b.scroll = nextLeft * 256;
                    b.camera = nextLeft * 256;
                    b.scrolled = false;
                    StageBehindCamera(nextLeft);
                }
            }
            return;
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
                b.camera = 0;
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

// After the frame (its picture made): nothing now (the ring out's way down is drawn by the machine: Machine::dropRaise)
void BattleAfterFrame(Machine &) {
}

// The tag style: each side's next fighter's fight is made ready on a spare machine (that fighter against the one on
// the other side, on this stage), a few frames each frame; a tag (Start, or the computer's choice, when the side's
// fighter can act) puts it in the port's machine's place, and carries over from the machine it replaces what of the fight
// goes on: the other side's fighter's record (where it is, what it is doing, its health and meter), the camera, the
// clock and the rounds; the one tagged in comes on where its partner was, with the health it rested with.
namespace {
    constexpr uint32_t kClockShown = 0x02028679;           // (the panel's copy of the clock, found as the one other byte that
                                                          // follows it)
    constexpr uint32_t kRecordSize = 0x498;               // (a player's record: the second is that far on)
    constexpr uint32_t kCameraBlock = 0x02026CA0, kCameraBlockSize = 0x60;
    constexpr uint32_t kScores = 0x020113A0, kScoresSize = 0x20; // (the players' scores, and their shown copies)
    constexpr uint32_t kGauge = 0x020695A0, kGaugeSize = 0x2C;   // (each player's super gauge: its fill at +0x15, its stock at +0x1F)
    constexpr uint32_t kClockDigits = 0x020133F3;
    constexpr uint32_t kRecordOwn = 0x240, kRecordOwnSize = 0x10; // (a player's record: bytes each machine has its own of)
    constexpr uint32_t kClockFrames = 0x02011378; // (a word: frames to the clock's next tick, which comes at 0)
    constexpr uint32_t kTagHold = 4;  // frames the picture before a tag is kept (the new machine's first are wrong)
    constexpr uint32_t kTagCool = 90;                     // frames after a tag before the side may tag again
    constexpr int kTagSpareFrames = 3;                    // the tag spares' frames a frame each, on their way

    // Something thrown is on the screen (the game's object list 3, from the index at 0x02068A96 + 6, records 0x800
    // bytes apart from 0x02028990, the next's index at +0x1C; one with a hurt box list at +0x2A0 is in use)
    bool Projectiles() {
        int16_t index = static_cast<int16_t>(Ram16(0x02068A96 + 3 * 2));
        for (int n = 0; n < 30 && index >= 0; n++) {
            const uint32_t at = 0x02028990u + (static_cast<uint32_t>(index) << 11);
            if (Ram32(at + 0x2A0) != 0) {
                return true;
            }
            index = static_cast<int16_t>(Ram16(at + 0x1C));
        }
        return false;
    }
    bool CanAct(int side) {
        const uint32_t base = sfiii3::kPlayer[side];
        return (Ram16(base + sfiii3::kBusy) & 0xFF) == 0 && Ram8(base + sfiii3::kFreeze) == 0 && Ram8(base + sfiii3::kAttacking) == 0 &&
               static_cast<int16_t>(Ram16(base + sfiii3::kY)) <= 0 && Ram8(base + sfiii3::kThrown) == 0;
    }
    void MakeTagSpares(Machine &m) {
        for (int side = 0; side < 2; side++) {
            const int next = NextUp(side);
            if (next < 0) {
                g_tag[side].Clear();
                continue;
            }
            const int8_t mine = static_cast<int8_t>(b.member[side][next]), theirs = static_cast<int8_t>(b.want[1 - side]);
            if (b.madeFor[side][side] != mine || b.madeFor[side][1 - side] != theirs || g_tag[side].state.empty()) {
                g_tag[side].Clear();
                g_tag[side].state = g_atChoice;
                b.madeFor[side][side] = mine;
                b.madeFor[side][1 - side] = theirs;
            }
            for (int n = 0; n < kTagSpareFrames && !g_tag[side].ready; n++) {
                const uint8_t was[2] = {b.want[0], b.want[1]};
                b.want[side] = static_cast<uint8_t>(mine);
                b.want[1 - side] = static_cast<uint8_t>(theirs);
                Spare &spare = g_tag[side];
                RunSpare(m, spare, [&spare](Machine &machine) {
                    spare.ready = ToRound(machine, static_cast<uint32_t>(spare.frames), true);
                });
                b.want[0] = was[0];
                b.want[1] = was[1];
            }
        }
    }
    // The panel after a tag: the game draws its numbers on the text layer only as they change, so the promoted
    // machine's shows its own (99 on the clock, no score, an empty gauge). The clock is put a second higher, about to
    // tick, so that it is drawn at once; the gauges are put back the frame after (PanelFrame: the staying side's, and
    // the team's carried over to the one come in) and the game told to draw them again (kMeterUpdate); the scores are
    // the replaced machine's again and are drawn as they next change (points scored). (The text
    // layers themselves cannot be mixed: each machine draws characters into codes of its own choosing, and later
    // draws over the other's.)
    void PanelFrame() {
        if (b.panelFor < 0 || b.frames == b.taggedAt) {
            return;
        }
        for (uint32_t n = 0; n < kScoresSize; n++) {
            SetRam8(kScores + n, b.scores[n]);
        }
        const int side = b.panelFor, other = 1 - side;
        for (uint32_t n = 0; n < kGaugeSize; n++) {
            SetRam8(kGauge + other * kGaugeSize + n, b.gauge[n]);
        }
        // (the team's gauge carried over to the one come in, as far as its own super art's goes)
        SetRam8(sfiii3::kMeterCount[side], std::min(b.teamCount, Ram8(sfiii3::kMaxMeterCount[side])));
        SetRam8(sfiii3::kGauge[side], Ram8(sfiii3::kMeterCount[side]) < Ram8(sfiii3::kMaxMeterCount[side]) ?
                                          std::min(b.teamFill, Ram8(sfiii3::kMaxGauge[side])) : 0);
        for (const int p : {side, other}) {
            SetRam8(sfiii3::kMeterUpdate[p], 1); // (the gauges drawn again)
        }
        b.panelFor = -1;
    }
    void Tag(Machine &m, int side) {
        const int next = NextUp(side);
        if (next < 0 || !g_tag[side].ready) {
            return;
        }
        const int other = 1 - side;
        // (what goes on: kept from the machine being replaced)
        std::vector<uint8_t> record(kRecordSize), camera(kCameraBlockSize);
        for (uint32_t n = 0; n < kRecordSize; n++) {
            record[n] = Ram8(sfiii3::kPlayer[other] + n);
        }
        for (uint32_t n = 0; n < kCameraBlockSize; n++) {
            camera[n] = Ram8(kCameraBlock + n);
        }
        const uint8_t clock = Ram8(sfiii3::kClock), won[2] = {Ram8(sfiii3::kRoundsWon[0]), Ram8(sfiii3::kRoundsWon[1])};
        const uint16_t x = Ram16(sfiii3::kPlayer[side] + sfiii3::kX);
        const uint8_t health = Ram8(sfiii3::kPlayer[side] + sfiii3::kLife);
        b.kept[side][b.next[side]] = b.move[side] == 3 ? 0 : health > 0 && health <= sfiii3::kFullLife ? health : 1; // (0: down)
        // (the scores, and the super gauge of the one staying on, put back the frame after: PanelFrame. Found by
        // comparing the two machines at a tag)
        for (uint32_t n = 0; n < kScoresSize; n++) {
            b.scores[n] = Ram8(kScores + n);
        }
        for (uint32_t n = 0; n < kGaugeSize; n++) {
            b.gauge[n] = Ram8(kGauge + other * kGaugeSize + n);
        }
        b.teamCount = Ram8(sfiii3::kMeterCount[side]);
        b.teamFill = Ram8(sfiii3::kGauge[side]);
        b.panelFor = static_cast<int8_t>(side);
        Promote(m, g_tag[side]);
        // (but for its bytes at +0x240 to +0x24F, the machine's own: copied from another machine some of them hang the
        // game, its main loop stopped, as tags in 2 vs 2 showed)
        for (uint32_t n = 0; n < kRecordSize; n++) {
            if (n >= kRecordOwn && n < kRecordOwn + kRecordOwnSize) {
                continue;
            }
            SetRam8(sfiii3::kPlayer[other] + n, record[n]);
        }
        for (uint32_t n = 0; n < kCameraBlockSize; n++) {
            SetRam8(kCameraBlock + n, camera[n]);
        }
        // (a second higher, about to tick: the panel draws the clock as it ticks)
        const uint8_t higher = static_cast<uint8_t>(std::min(clock + 1, 99));
        SetRam8(sfiii3::kClock, higher);
        SetRam8(kClockShown, higher); // (the game's clock's other copy, and the panel's in decimal digits)
        SetRam8(kClockDigits, static_cast<uint8_t>((higher / 10 % 10) << 4 | higher % 10));
        SetRam16(kClockFrames, 0);
        SetRam8(sfiii3::kRoundsWon[0], won[0]);
        SetRam8(sfiii3::kRoundsWon[1], won[1]);
        (void)x;
        SetRam8(sfiii3::kPlayer[side] + sfiii3::kLife, b.kept[side][next]);
        b.next[side] = static_cast<uint8_t>(next);
        b.want[side] = b.member[side][next];
        b.tagCool[side] = b.frames + kTagCool;
        b.taggedAt = b.frames;
        b.wins[0] = won[0];
        b.wins[1] = won[1];
        if (getenv("SFIII3_BATTLE_LOG")) {
            fprintf(stderr, "battle: frame %u (the machine's %llu), side %d tags in %s (health %u)\n", b.frames, static_cast<unsigned long long>(m.frame), side + 1, kCharacters[b.want[side] % kCharacterCount],
                    b.kept[side][next]);
        }
    }
    // Each frame of a tag-style fight: the spares made ready, a tag asked for (Start on a person's side; the game's side
    // when its fighter is low and a partner has more), the resting ones' health coming back
    int16_t X(int side) {
        return static_cast<int16_t>(Ram16(sfiii3::kPlayer[side] + sfiii3::kX));
    }
    bool InAir(int side) {
        return static_cast<int16_t>(Ram16(sfiii3::kPlayer[side] + sfiii3::kY)) > 0;
    }
    int Camera() {
        return static_cast<int16_t>(Ram16(sfiii3::kCameraX));
    }
    constexpr int kOffBy = 240;      // beyond the camera's middle a fighter is off the picture (192 a side and more)
    constexpr int kLeaveSpeed = 9;   // pixels a frame, flying off
    constexpr int kLeaveMost = 50;   // frames, at most, of leaving
    constexpr int kComeMost = 120;   // frames, at most, of coming in
    constexpr int kComeSteps = 34;   // about the frames a jump in takes, for its speed across
    // How well the computer plays, 0 to 7: the team battle's own choice, or the board's
    constexpr uint32_t kDifficulty = 0x0206AC64; // (the game's difficulty, 0-7: written from the settings at start-up)
    int Skill() {
        return std::clamp<int>(Ram8(kDifficulty), 0, 7);
    }
    int Between(int easiest, int hardest) {
        return easiest + (hardest - easiest) * Skill() / 7;
    }
    // The computer's side tags as a person would: when the one on is low and a partner has more, and then at a good
    // moment (the other knocked down or reeling, or far off) more than at another, never in the middle of a combo
    // (free for a while), and more or less often by its difficulty
    bool ComputerTags(int side, int next) {
        const int health = Ram8(sfiii3::kPlayer[side] + sfiii3::kLife);
        if (health >= sfiii3::kFullLife * Between(30, 45) / 100 || b.kept[side][next] < health + 30 || b.freeFor[side] < 8) {
            return false;
        }
        const bool moment = b.busyFor[1 - side] >= 20 || std::abs(X(0) - X(1)) >= 200;
        return static_cast<int>(Next() % 1000) < (moment ? Between(40, 200) : Between(2, 12));
    }
    // A time over: the team with more health in all wins (the fighters on given the order of the teams' sums, as the
    // clock runs out, so that the game's own time over says it)
    void TimeOver() {
        if (Ram8(sfiii3::kClock) != 1 || Ram16(kClockFrames) > 1) {
            return;
        }
        int sum[2] = {};
        for (int side = 0; side < 2; side++) {
            sum[side] = Ram8(sfiii3::kPlayer[side] + sfiii3::kLife);
            for (int n = 0; n < b.size; n++) {
                sum[side] += n == b.next[side] ? 0 : b.kept[side][n];
            }
        }
        if (getenv("SFIII3_BATTLE_LOG")) {
            fprintf(stderr, "battle: frame %u, time over coming: health in all %d %d, on %u %u\n", b.frames, sum[0], sum[1],
                    Ram8(sfiii3::kPlayer[0] + sfiii3::kLife), Ram8(sfiii3::kPlayer[1] + sfiii3::kLife));
        }
        if (sum[0] == sum[1]) {
            return;
        }
        const int winner = sum[0] > sum[1] ? 0 : 1;
        const uint32_t w = sfiii3::kPlayer[winner] + sfiii3::kLife, l = sfiii3::kPlayer[1 - winner] + sfiii3::kLife;
        if (Ram8(w) <= Ram8(l)) {
            if (Ram8(l) < sfiii3::kFullLife) {
                SetRam8(w, static_cast<uint8_t>(Ram8(l) + 1));
            } else {
                SetRam8(l, static_cast<uint8_t>(Ram8(w) - 1 > 0 ? Ram8(w) - 1 : 1));
                SetRam8(w, std::max<uint8_t>(Ram8(w), static_cast<uint8_t>(Ram8(l) + 1)));
            }
        }
    }
    // The other fighter free (not attacking, frozen by a hit, in the air or thrown) and nothing thrown on the screen:
    // a swap at any other time takes what the other is in the middle of over to the next machine, which breaks it
    bool Quiet(int side) {
        const uint32_t them = sfiii3::kPlayer[side];
        return Ram8(them + sfiii3::kAttacking) == 0 && Ram8(them + sfiii3::kFreeze) == 0 && !InAir(side) && Ram8(them + sfiii3::kThrown) == 0 &&
               !Projectiles();
    }
    constexpr int kLieFor = 120;   // frames the one knocked out is seen lying, from landing
    constexpr int kDownMost = 240; // frames, at most, from the knock-out to the next one coming in (and the other quiet)
    constexpr int kStillUp = 20;   // frames after which one knocked out but never in the air is taken to be lying
    // The next one on, swapped in now, off the picture at the end the last one left by (or nearer to), on the floor, to
    // jump in
    void BringIn(Machine &m, int side) {
        const uint32_t base = sfiii3::kPlayer[side];
        b.from[side] = static_cast<int8_t>(X(side) < Camera() ? -1 : 1);
        Tag(m, side);
        SetRam16(base + sfiii3::kX, static_cast<uint16_t>(Camera() + b.from[side] * kOffBy));
        SetRam16(base + sfiii3::kY, 0);
        b.move[side] = 2;
        b.moveFor[side] = 0;
        b.jumped[side] = b.attacked[side] = false;
        b.lastY[side] = 0;
    }
    // Each frame of a tag-style fight: the spares made ready; a tag asked for (Start on a person's side, or its partner's
    // Start in co-op and 2 vs 2; the game's side when its fighter is low and a partner has more) and its movements; the
    // resting ones' health coming back
    void TagFrame(Machine &m, const uint16_t raw[4]) {
        MakeTagSpares(m);
        PanelFrame();
        if (!Fought()) {
            return;
        }
        if (getenv("SFIII3_TAG_LOG") && (b.move[0] || b.move[1] || b.frames % 120 == 0)) { // (debugging aid)
            fprintf(stderr, "tag: frame %u fr %u %u cam %d thrown %d att %u %u moves %d %d (%u %u) state %u clock %u x %d %d y %d %d busy %04X %04X\n", b.frames, Ram8(sfiii3::kPlayer[0] + sfiii3::kFreeze), Ram8(sfiii3::kPlayer[1] + sfiii3::kFreeze), Camera(), Projectiles() ? 1 : 0, Ram8(sfiii3::kPlayer[0] + sfiii3::kAttacking), Ram8(sfiii3::kPlayer[1] + sfiii3::kAttacking), b.move[0], b.move[1],
                    b.moveFor[0], b.moveFor[1], Ram8(sfiii3::kMatchState), Ram8(sfiii3::kClock), X(0), X(1),
                    static_cast<int16_t>(Ram16(sfiii3::kPlayer[0] + sfiii3::kY)), static_cast<int16_t>(Ram16(sfiii3::kPlayer[1] + sfiii3::kY)),
                    Ram16(sfiii3::kPlayer[0] + sfiii3::kBusy), Ram16(sfiii3::kPlayer[1] + sfiii3::kBusy));
        }
        TimeOver();
        for (int side = 0; side < 2; side++) {
            const bool free = CanAct(side);
            b.freeFor[side] = free ? static_cast<uint16_t>(std::min(b.freeFor[side] + 1, 60000)) : 0;
            b.busyFor[side] = free ? 0 : static_cast<uint16_t>(std::min(b.busyFor[side] + 1, 60000));
        }
        for (int side = 0; side < 2; side++) {
            const int other = 1 - side;
            const int toward = X(other) < X(side) ? -1 : 1;
            const uint32_t base = sfiii3::kPlayer[side];
            // (a person's controls for the fighter on: in co-op and 2 vs 2, the one whose fighter it is)
            const int pad = PadOf(side, b.next[side]);
            if (pad >= 0 && b.move[side] == 0) {
                m.players[side] = raw[pad];
            }
            switch (b.move[side]) {
            case 0: {
                const int next = NextUp(side);
                if (next >= 0 && Ram8(base + sfiii3::kKnockedOut) != 0) {
                    b.move[side] = 3; // (its bar emptied, and a partner standing: KnockOutLift)
                    b.moveFor[side] = 0;
                    b.landedAt[side] = 0;
                    b.jumped[side] = false;
                    break;
                }
                if (next < 0 || b.frames < b.tagCool[side] || !CanAct(side)) {
                    break;
                }
                // (asked: Start of the one on, or of the partner whose fighter is next)
                bool pressed = false;
                for (int p = 0; p < 4; p++) {
                    const bool start = (raw[p] & (kStart | kTag)) != 0; // (Start, or a gamepad's left trigger)
                    const bool mine = PeopleStyle() ? (p == PadOf(side, b.next[side]) || p == PadOf(side, next)) : p == side;
                    if (mine && start && !b.startWas[p]) {
                        pressed = true;
                    }
                }
                const bool computerTags = !Person(side) && ComputerTags(side, next);
                if ((Person(side) && pressed) || computerTags) {
                    b.move[side] = 1;
                    b.moveFor[side] = 0;
                    b.hitsAt[side] = Ram16(base + sfiii3::kHitCount);
                }
                break;
            }
            case 1: { // leaving: a jump back, then off the picture at speed
                SetRam8(base + 3, 1); // (its controls are these for the jump, the computer's side too)
                const int away = -toward;
                const uint16_t f = b.moveFor[side]++;
                m.players[side] = f < 3 ? static_cast<uint16_t>(kUp | (away < 0 ? kLeft : kRight)) : 0;
                if (Ram16(base + sfiii3::kHitCount) != b.hitsAt[side] || (f == 8 && !InAir(side))) {
                    b.move[side] = 0; // (hit on the way, or it did not jump: the tag is off)
                    b.tagCool[side] = b.frames + 30;
                    break;
                }
                if (f > 3 && InAir(side) && std::abs(X(side) - Camera()) <= kOffBy) {
                    SetRam16(base + sfiii3::kX, static_cast<uint16_t>(X(side) + away * kLeaveSpeed));
                }
                // (swapped once it is off the picture and the other fighter is free, with nothing of its own on the
                // screen: what the other is in the middle of goes over to the next machine with its record)
                const bool gone = std::abs(X(side) - Camera()) > kOffBy || f > kLeaveMost;
                if (gone && !Quiet(other)) {
                    SetRam16(base + sfiii3::kY, static_cast<uint16_t>(std::max<int>(InAir(side) ? static_cast<int16_t>(Ram16(base + sfiii3::kY)) : 1, 1)));
                    break; // (held off the picture until then)
                }
                if (gone) {
                    BringIn(m, side);
                    return; // (one swap a frame)
                }
                break;
            }
            case 3: { // knocked out: thrown up by the knock-out, then lying a while; the next one comes in after
                m.players[side] = 0;
                const bool air = InAir(side);
                const uint16_t f = b.moveFor[side]++;
                if (air) {
                    b.jumped[side] = true;
                } else if ((b.jumped[side] || f > kStillUp) && !b.landedAt[side]) {
                    b.landedAt[side] = f ? f : 1;
                }
                const bool lain = (b.landedAt[side] && f - b.landedAt[side] >= kLieFor) || f >= kDownMost;
                if (lain && Quiet(other)) {
                    BringIn(m, side);
                    return;
                }
                break;
            }
            case 2: { // coming: a jump in from that end, across fast enough to land by the other
                SetRam8(base + 3, 1);
                b.moveFor[side]++;
                const bool air = InAir(side);
                m.players[side] = 0;
                if (!b.jumped[side]) {
                    m.players[side] = static_cast<uint16_t>(kUp | (toward < 0 ? kLeft : kRight));
                    b.jumped[side] = air;
                } else if (air) {
                    const int distance = std::abs(X(other) - X(side)) - 60; // (landing about that far from the other)
                    if (distance > 0) {
                        SetRam16(base + sfiii3::kX, static_cast<uint16_t>(X(side) + toward * std::min(distance, std::clamp(distance / kComeSteps * 2, 3, 12))));
                    }
                    // (the jump attack, on the way down: a heavy kick, which can hit)
                    const int16_t y = static_cast<int16_t>(Ram16(base + sfiii3::kY));
                    if (!b.attacked[side] && y < b.lastY[side]) {
                        m.players[side] = kKick3;
                        b.attacked[side] = true;
                    }
                    b.lastY[side] = y;
                }
                if ((b.jumped[side] && !air) || b.moveFor[side] > kComeMost) {
                    b.move[side] = 0;
                    b.tagCool[side] = b.frames + kTagCool;
                    if (!Person(side)) {
                        SetRam8(base + 3, 0); // (the computer plays it again)
                    }
                }
                break;
            }
            default: break;
            }
        }
        for (int p = 0; p < 4; p++) {
            b.startWas[p] = (raw[p] & (kStart | kTag)) != 0;
        }
        // (a resting fighter gets back only the health that comes back by itself, as the CPS-2 ports have it: 3rd Strike's
        // health has none such, so it keeps what it had)
    }
} // namespace

// Another mode started (attract mode): this one over
void EndBattle() {
    if (b.mode != kNone) {
        b.mode = kNone;
        StageBehind(-1);
    }
}

bool BattleOn() {
    return b.mode != kNone;
}

// The ring out's frame (before the port's machine's): the next match made ready; a fighter past the picture's edge at
// an open end out, flung over it and falling; then the way down: the winner jumps off the old stage, and at the top of
// its jump is put in the next match as high above its stage as it was above the old one and more, the camera held that
// far up and following it down as it falls, the old stage going up and out as the next comes up from below
// (Machine::dropRaise). True while the ring out has the machine (the rest of the battle's frame not run).
namespace {
    bool RingOutFrame(Machine &m) {
        // (the next match: two frames a frame while this one is fought, four while one falls)
        for (int n = 0; n < (b.ring == 1 ? 4 : 2) && !g_next.ready && !g_next.state.empty(); n++) {
            RunSpare(m, g_next, [](Machine &spare) {
                SetRam8(sfiii3::kStage, static_cast<uint8_t>(b.dropStage));
                g_next.ready = ToRound(spare, static_cast<uint32_t>(g_next.frames), true);
            });
        }
        const int height = Video::kHeight;
        switch (b.ring) {
        case 0: {
            if (!Fought()) {
                return false;
            }
            for (int p = 0; p < 2; p++) {
                const int end = KnockedAbout(p) ? OpenEnd(p) : 0;
                const int x = Word(sfiii3::kPlayer[p] + sfiii3::kX);
                if (end && (end < 0 ? x - kHalfWidth < PictureEdge(-1) : x + kHalfWidth > PictureEdge(1))) {
                    b.ring = 1;
                    b.outWho = static_cast<int8_t>(p);
                    b.outSide = static_cast<int8_t>(end);
                    b.since = 0;
                    b.fx = x * 256;
                    b.fy = Word(sfiii3::kPlayer[p] + sfiii3::kY) * 256;
                    b.vx = end * kFlungAcross;
                    b.vy = kFlungUp;
                    if (p == 1) {
                        b.won++;
                        if (b.won > FeatureValue("ringout_best") && !RunningAhead()) {
                            SetFeatureValue("ringout_best", std::min<int>(b.won, 99));
                        }
                    }
                    if (getenv("SFIII3_BATTLE_LOG")) {
                        fprintf(stderr, "battle: frame %u (machine %llu), side %d rung out over the %s end at %d\n", b.frames, static_cast<unsigned long long>(m.frame), p + 1, end < 0 ? "left" : "right", x);
                    }
                    break;
                }
            }
            return false;
        }
        case 1: {
            // (flung up and away, then falling through the floor: held in hit freeze, moved here)
            const uint32_t base = sfiii3::kPlayer[b.outWho];
            if (b.fy > -kGoneBelow * 256) {
                b.fx += b.vx;
                b.fy += b.vy;
                b.vy -= kPull;
            }
            SetRam16(base + sfiii3::kX, static_cast<uint16_t>(b.fx / 256));
            SetRam16(base + sfiii3::kY, static_cast<uint16_t>(b.fy / 256));
            SetRam8(base + sfiii3::kFreeze, 3);
            if (getenv("SFIII3_BATTLE_LOG") && b.since % 4 == 0) { // (debugging aid)
                fprintf(stderr, "out: since %u set %d %d, was %d %d, freeze %u camera %d\n", b.since, b.fx / 256, b.fy / 256, b.ringY, b.apexX,
                        Ram8(base + sfiii3::kFreeze), Word(sfiii3::kCameraX));
            }
            b.ringY = Word(base + sfiii3::kY);
            b.apexX = Word(base + sfiii3::kX);
            if (++b.since >= kFallFor) {
                if (b.outWho == 0) {
                    b.over = true; // (the player out: the run is over)
                    b.winner = 1;
                    b.overAt = b.frames;
                    return true;
                }
                b.ring = 2;
                b.since = 0;
                b.handed = false;
            }
            return false;
        }
        default: break;
        }
        // The way down
        b.since++;
        if (getenv("SFIII3_BATTLE_LOG") && b.since % 2 == 0 && b.since > 20 && b.since < 60) { // (debugging aid)
            fprintf(stderr, "ring: frame %u since %u handed %d ready %d y %d raise %d locked %d fought %d stage %u cam:", b.frames, b.since, b.handed,
                    g_next.ready, Word(sfiii3::kPlayer[0] + sfiii3::kY), b.raise / 256, Locked(), Fought(), Ram8(sfiii3::kStage));
            for (uint32_t at = 0x02026C90; at < 0x02026D10; at += 2) {
                fprintf(stderr, " %d", Word(at));
            }
            fprintf(stderr, "\n");
        }
        const uint32_t winner = sfiii3::kPlayer[0];
        if (!b.handed) {
            // (on the old stage: the one out still falling; the winner jumps at kDropJump, once the next match is ready)
            const uint32_t out = sfiii3::kPlayer[b.outWho];
            b.fx += b.vx;
            b.fy = std::max(b.fy + b.vy, -kGoneBelow * 256);
            b.vy -= kPull;
            SetRam16(out + sfiii3::kX, static_cast<uint16_t>(b.fx / 256));
            SetRam16(out + sfiii3::kY, static_cast<uint16_t>(b.fy / 256));
            SetRam8(out + sfiii3::kFreeze, 3);
            if (!g_next.ready) {
                b.since = std::min<uint16_t>(b.since, kDropJump - 1); // (waits for it)
                m.players[0] = 0;
                return false;
            }
            const int y = Word(winner + sfiii3::kY);
            m.players[0] = b.since >= kDropJump && b.since < kDropJump + 3 ? kUp : 0;
            SetRam8(winner + 3, 1);
            if (b.since >= kDropJump) {
                // (the next match's winner jumps with it, frame for frame: handed over at the top, it is in the same jump
                // there, which the game goes on with and lands)
                const bool up = b.since < kDropJump + 3;
                RunSpare(m, g_next, [up](Machine &spare) {
                    spare.players[0] = up ? kUp : 0;
                    spare.players[1] = 0;
                    SetRam8(sfiii3::kPlayer[0] + 3, 1);
                    SetRam8(sfiii3::kPlayer[1] + 3, 1);
                });
            }
            if (b.since > kDropJump + 4 && y > 0 && y <= b.ringY) {
                // (the top of its jump: handed to the next match, where it is in the picture, a picture's height up)
                const int across = std::clamp(Word(winner + sfiii3::kX) - PictureEdge(-1), kDropInside, m.PictureWidth() - kDropInside);
                m.DrawStageOnly(m.dropOld);
                Promote(m, g_next);
                b.handed = true;
                b.apexX = static_cast<int16_t>(across);
                b.handedY = static_cast<int16_t>(Word(winner + sfiii3::kY)); // (its own jump's top there)
                b.fy = (b.handedY + height) * 256; // (how high it is seen: as high above the next stage as it was above the old)
                b.vy = 0;
                SetRam16(winner + sfiii3::kX, static_cast<uint16_t>(PictureEdge(-1) + across));
                b.raise = height * 256;
                m.dropRaise = height;
                m.dropLift = height;
                m.dropLifted = WinnerObject;
                return true;
            }
            b.ringY = static_cast<int16_t>(y);
            return false;
        }
        // (in the next match: nobody plays (the winner falls by the game's own jump), the camera held where its fighters
        // stand and followed down as the winner falls)
        m.players[0] = m.players[1] = 0;
        SetRam8(sfiii3::kPlayer[1] + 3, 1);
        // (the winner held at the top of its jump there, seen falling from higher up, as the game's jump falls: at the
        // top of its own jump it is let go, and the game's jump lands it)
        if (b.fy > b.handedY * 256) {
            b.vy += kDropPull;
            b.fy = std::max(b.fy - b.vy, b.handedY * 256);
            SetRam8(winner + sfiii3::kFreeze, 2);
            SetRam16(winner + sfiii3::kY, static_cast<uint16_t>(b.handedY));
        }
        const int y = Word(winner + sfiii3::kY);
        const int seen = b.fy > b.handedY * 256 ? b.fy / 256 : y; // (let go: where the game has it)
        m.dropLift = std::max(seen - y, 0);
        const int fallen = b.handedY + height - seen;
        const int32_t follow = std::clamp(height - fallen, 0, height) * 256;
        b.raise += (follow - b.raise) / kDropFollow;
        if (std::abs(follow - b.raise) < 256) {
            b.raise = follow;
        }
        m.dropRaise = b.raise / 256;
        if ((y <= 0 && b.raise == 0 && m.dropLift == 0) || b.since >= kDropMost) {
            m.dropRaise = -1;
            m.dropLift = 0;
            m.dropLifted = nullptr;
            m.dropOld.clear();
            MatchBegins(); // (the fight on the next stage, and the match after it begun)
        }
        return true;
    }
} // namespace

void BattleBeforeFrame(Machine &m) {
    m.hidePanel = false;
    {
        // (the team battle's CPU difficulty, the board's again once the mode is over)
        static int was = -1;
        const int chosen = b.mode == kTeam ? FeatureValue("team_difficulty") : 0;
        if (chosen > 0) {
            if (was < 0) {
                was = Ram8(kDifficulty);
            }
            SetRam8(kDifficulty, static_cast<uint8_t>(chosen - 1));
        } else if (was >= 0) {
            SetRam8(kDifficulty, static_cast<uint8_t>(was));
            was = -1;
        }
    }
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
    StageBehindOnlyMusic(b.viewer);
    if (b.viewer) {
        m.onlyStage = true; // (with the stage board's stage composed: that alone)
        if (FeatureValue("viewer_stage") != b.viewed) {
            StartViewer(m); // (another stage chosen in the menu: the viewer goes there)
            return;
        }
        StageBehindMusic(FeatureValue("viewer_music") == 0);
    }
    {
        // (the viewer on its way to its stage: run unseen, at speed)
        static bool fast = false;
        const bool want = b.viewer && !StageBehindShown();
        if (want != fast) {
            fast = want;
            SetFastForward(want);
        }
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
            b.colour = Ram8(sfiii3::kSelectColour[0]);
            b.art = Ram8(sfiii3::kSelectArt[0]);
            if (chooses) {
                b.want[0] = static_cast<uint8_t>(Ram16(sfiii3::kPlayer[0] + 0x3C0) % kCharacterCount);
            }
            MatchBegins();
        }
        return;
    }
    const uint16_t raw[4] = {m.players[0], m.players[1], m.players[2], m.players[3]};
    // A person takes an endless battle's fighter by touching its controls
    for (int p = 0; p < 2; p++) {
        if (b.mode == kEndless && !b.viewer && (m.players[p] & (fight::kSticks | fight::kButtons))) {
            b.touched[p] = b.frames;
        }
        if (Locked()) {
            SetRam8(sfiii3::kPlayer[p] + 3, Person(p) ? 1 : 0); // (0: the computer plays it)
        }
        if (!Person(p)) {
            m.players[p] = 0;
        }
    }
    // Ring out: no health lost; a fighter knocked about at an open end goes over it (WallLift), and once past the
    // picture's edge is out: flung over the end, falling (RingOutFrame)
    if (b.mode == kRingOut && Locked()) {
        for (int p = 0; p < 2; p++) {
            const uint32_t base = sfiii3::kPlayer[p];
            const uint16_t hits = Ram16(base + sfiii3::kHitCount);
            if (hits != b.hits[p]) {
                b.hits[p] = hits;
                b.hitAt[p] = b.frames;
            }
            SetRam8(base + sfiii3::kLife, sfiii3::kFullLife);
        }
        if (RingOutFrame(m)) {
            return;
        }
    }
    if (b.mode == kTeam && b.tagStyle && !b.decided) {
        TagFrame(m, raw);
        // (the picture before the tag kept over its first frames, whose sprites the new machine has not yet made)
        m.holdPicture = b.taggedAt && b.frames - b.taggedAt < kTagHold;
        for (int p = 0; p < Machine::kMaxPlayers; p++) {
            if ((p >= 2 || Person(p)) && Fought()) {
                m.players[p] = static_cast<uint16_t>(m.players[p] & ~kStart); // (Start is the tag's, not the game's)
            }
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
        const uint32_t shown = kShownFor;
        if (g_next.ready && b.frames - b.decidedAt >= shown) {
            if (getenv("SFIII3_BATTLE_LOG")) {
                fprintf(stderr, "battle: frame %u, the next match on (its machine run %d frames), %d %d\n", b.frames, g_next.frames, b.want[0], b.want[1]);
            }
            Promote(m, g_next);
            b.decided = false;
            MatchBegins();
        }
    }
}

// What the modes show over the picture, as the CPS-2 ports' (cps2recomp's fight/battle.cpp) show it: the score at the
// top all the while (survival, the ring out), the count at the end; under each team's bars who of it is still to come
// (the tag style: the others, with their health), and the labels of who is on (the tag-team modes)
void BattleHud(Machine &m, std::vector<HudLine> &lines) {
    char text[64];
    // (the ends of the bars' part of the picture: a wide one's bars stay in the board's middle)
    const float width = static_cast<float>(std::max(m.PictureWidth(), 1));
    const float barsLeft = (static_cast<float>(m.WideMargin()) + 0.02f * 384.0f) / width, barsRight = 1.0f - barsLeft;
    switch (b.mode) {
    case kSurvival:
    case kRingOut: {
        const bool ring = b.mode == kRingOut;
        if (!b.over) {
            snprintf(text, sizeof(text), ring ? "Ring outs: %u" : "Beaten: %u", b.won);
            lines.push_back({0.5f, 0.21f, 0, text});
            break;
        }
        lines.push_back({0.5f, 0.36f, 0, ring ? "RING OUT" : "SURVIVAL OVER"});
        snprintf(text, sizeof(text), ring ? "Ring outs: %u" : "Opponents beaten: %u", b.won);
        lines.push_back({0.5f, 0.46f, 0, text});
        snprintf(text, sizeof(text), "Best: %d", std::max<int>(b.won, FeatureValue(ring ? "ringout_best" : "survival_best")));
        lines.push_back({0.5f, 0.54f, 0, text});
        break;
    }
    case kTeam:
        if (b.over) {
            const bool people = FeatureValue("team_opponent") == 1 || PeopleStyle();
            lines.push_back({0.5f, 0.26f, 0, b.winner < 0 ? "DRAW" : b.winner == 0 ? (people ? "TEAM 1 WINS" : "YOUR TEAM WINS") :
                                                                               (people ? "TEAM 2 WINS" : "CPU TEAM WINS")});
            break;
        }
        for (int side = 0; side < 2; side++) {
            std::string line;
            for (int n = 0; n < b.size; n++) {
                const char *name = kCharacters[b.member[side][n] % kCharacterCount];
                if (b.tagStyle) {
                    if (n == b.next[side]) {
                        continue;
                    }
                    char health[16];
                    if (b.kept[side][n] > 0) {
                        snprintf(health, sizeof(health), " %d%%", std::max(1, b.kept[side][n] * 100 / sfiii3::kFullLife));
                    } else {
                        snprintf(health, sizeof(health), " down");
                    }
                    line += std::string(line.empty() ? "" : "  ") + name + health;
                } else if (n > b.next[side]) {
                    line += std::string(line.empty() ? "Next: " : ", ") + name;
                }
            }
            if (!line.empty()) {
                lines.push_back({side == 0 ? barsLeft : barsRight, kTeamLinesY, side == 0 ? -1 : 1, line});
            }
            if (PadOf(side, b.next[side]) >= 0) {
                lines.push_back({side == 0 ? barsLeft : barsRight, kLabelY, side == 0 ? -1 : 1, "P" + std::to_string(PadOf(side, b.next[side]) + 1)});
            }
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
    for (Spare &spare : g_tag) {
        ar.Vec(spare.state);
        ar.Pod(spare.ready);
        ar.Pod(spare.frames);
    }
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
                             "Choose a character. Nobody loses health: the ends of a stage are open, and a fighter knocked over "
                             "one falls. Knock your opponent out and you jump down to the next stage; over when you fall. The ring outs so "
                             "far are shown at the top.",
                             StartRingOut});
    game.features.push_back({"ringout_best", "Ring out", "Best", "The most ring outs in one go. (Left puts it back.)",
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
    std::vector<const char *> stages = {"Scroll", "Shuffle"};
    for (const std::string &name : stageNames) {
        stages.push_back(name.c_str());
    }
    game.features.push_back({"endless_stage", "Endless battle", "Stage",
                             "Where they fight. Scroll: the stages go by, one after another, and the fighters are carried along. "
                             "Shuffle: a stage that fades into another every so often. Or one stage.",
                             kFeatureMenuChoices, stages});
    game.features.push_back({"endless_shuffle", "Endless battle", "Shuffle every", "Shuffle: how long they fight on a stage before it fades "
                             "into another.", 0, {"1 minute", "2 minutes", "3 minutes", "5 minutes", "10 minutes"}});
    game.features.push_back({"endless_scroll", "Endless battle", "Scroll speed", "Scroll: how fast the stages go by.", 0, {"Slow", "Medium", "Fast"}});
    game.features.push_back({"endless_between", "Endless battle", "Between stages",
                             "Scroll: how one stage gives way to the next. Continuous: the next comes in from the right as the one "
                             "crossed goes out to the left, with no break. Fade: the one crossed fades out, and the next fades in.",
                             0, {"Continuous", "Fade"}});

    game.commands.push_back({"viewer_start", "Stage viewer", "Start", "A stage with nobody on it and no panel, to look at and listen to.",
                             StartViewer});
    {
        static std::vector<const char *> viewerStages;
        viewerStages.assign(stages.begin() + 2, stages.end());
        viewerStages.push_back("Infinite");
        game.features.push_back({"viewer_stage", "Stage viewer", "Stage",
                                 "The stage the viewer shows, or Infinite: every stage in turn, each scrolling to its end and fading "
                                 "into the next, with its own tune. Changing it while viewing goes to the new one.",
                                 kFeatureMenuChoices, viewerStages});
        game.features.push_back({"viewer_scroll", "Stage viewer", "Scrolling",
                                 "How the viewer moves across the stage: with the stick, or by itself from one end to the other and "
                                 "back (Infinite: always onwards).",
                                 0, {"With the stick", "Slow", "Medium", "Fast"}});
        static std::vector<const char *> tunes;
        tunes.assign(1, "The stage's own");
        tunes.insert(tunes.end(), stages.begin() + 2, stages.end());
        game.features.push_back({"viewer_music", "Stage viewer", "Music",
                                 "The tune that plays in the viewer: the stage's own, or another stage's. (Infinite with the stage's own "
                                 "plays each stage's.) Another takes effect when the viewer is started.",
                                 kFeatureMenuChoices, tunes});
    }
    game.commands.push_back({"team_start", kTeamPage, "Start",
                             "Each side's fighters come on one at a time, in the order below. The one knocked down is out, and "
                             "the next of that side comes on with full health; the winner stays on with the health it has left, "
                             "and a little back. Over when a side has nobody left.",
                             StartTeam});
    game.features.push_back({"team_style", kTeamPage, "Style",
                             "Tag: each fighter has its own health; the left trigger (or Start) tags the next one in whenever "
                             "yours can act, and when a fighter's health runs out the next comes in. Resting fighters keep the "
                             "health they had. The match is over when a team has nobody left. Knock-out: one fighter of each team "
                             "at a time, the next coming on when one is knocked out.",
                             0, {"Tag", "Knock-out"}});
    game.features.push_back({"team_size", kTeamPage, "Fighters", "How many fighters each team has.", 0, {"2", "3"}});
    game.features.push_back({"team_opponent", kTeamPage, "Team 2",
                             "Who plays the second team: the CPU (the game), or player 2. (With \"Tag team\" below, that says who plays.)",
                             0, {"CPU", "Player 2"}});
    game.features.push_back({"team_difficulty", kTeamPage, "CPU difficulty",
                             "How well the CPU's team fights, 1 (easiest) to 8 (hardest), or as the board is set (the Board page).", 0,
                             {"As the board is set", "1 (easiest)", "2", "3", "4", "5", "6", "7", "8 (hardest)"}});
    {
        static std::vector<const char *> teamStages = {"Random"};
        teamStages.insert(teamStages.end(), stages.begin() + 2, stages.end());
        game.features.push_back({"team_stage", kTeamPage, "Stage", "Where the teams fight, from the first fight to the last.",
                                 kFeatureMenuChoices, teamStages});
    }
    static const char *const kOrder[kMost] = {"first", "second", "third"};
    static std::string titles[2][kMost], helps[2][kMost];
    static const char *const kNames[2][kMost] = {{"team_1_1", "team_1_2", "team_1_3"}, {"team_2_1", "team_2_2", "team_2_3"}};
    for (int side = 0; side < 2; side++) {
        for (int n = 0; n < kMost; n++) {
            titles[side][n] = "Team " + std::to_string(side + 1) + ", " + kOrder[n];
            helps[side][n] = std::string("Team ") + std::to_string(side + 1) + "'s " + kOrder[n] + " fighter on" +
                             (n == 2 ? " (with three fighters a team)" : "") + ": someone at random, or this one.";
            game.features.push_back({kNames[side][n], kTeamPage, titles[side][n].c_str(), helps[side][n].c_str(),
                                     kFeatureMenuChoices, fighters});
        }
    }
    game.features.push_back({"tag_team", kTeamPage, "Tag team",
                             "In the team battle, each fighter of a team played by a person of its own. Co-op: players 1 and 2 are one team "
                             "against the computer's. 2 vs 2: players 1 and 3 against players 2 and 4. A team's panel is whoever's fighter is on "
                             "(in a team of three, the third is the first person's again), and in the tag style the partner tags in with "
                             "their left trigger. The labels under the bars say who is on.",
                             0, {"Off", "Co-op vs. computer", "2 vs 2"}});
}

} // namespace cps3recomp
