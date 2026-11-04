// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's match, written again as C++ (rewrites: runtime/lift.h; the parts of the fight: fight_core.h): the round
// counted once it is concluded (0x060999B0), and what that calls.
//
// The round counted. After the fight's step (fight_round.cpp's ManageFight) has seen the round concluded, the round's
// management's fourth step (Game_Manage_4th) counts it by how it ended (Conclusion_Type):
//   a knock-out    the winner's mark for the round on the panel (Setup_Win_Mark: 1 a plain one, 7 a chip knock-out,
//                  4 a super art's or Shin Akuma's finish, with their sounds; 3 a perfect, by Check_Perfect), its win
//                  (PL_Wins) and the match's records (Update_VS_Data, Ck_Win_Record), the computer's play time
//                  (Update_Level_Control: more when a person beat it, less when it won); then the round's end (step 6)
//   a double K.O.  both win the round (mark 5), unless both are one round from the match and level: then the judges'
//                  round (step 4; Judge_Next_Disposal); a player with the rounds wins the match (step 5 either way)
//   a time over    the one with more health wins as by a knock-out (mark 1); with the health level, the judges (step 4)
// The match is won at the board's number of rounds (Battle_Number, by Play_Type: 0 against the computer, 1 two people)
// and one more: a player with that many wins (PL_Wins) has it.
//
// The judges' round (step 5, Game_Manage_5th, its own steps by C_No[1]): once the round's end is over (Complete_Victory,
// or its 900 frames), the message (3), a pause, the judges' choice of a winner (Judge_Winner) and the screen's switch to
// the judges, their verdict, the screen back; then the winner's round counted as for a knock-out (mark 6) and on to the
// round's end. The round's end (step 6, Game_Manage_6th): once it is over, the fighters to their poses, the winner's
// banner and both players' marks drawn on the panel; a second later the next round (step 7, the round's number on) and
// a challenger let in (Quick_Entry).
//
// Names in the comments are the game's own source's (fight_core.h); the arcade's build differs from the PlayStation 2's
// here and there (no Mode_Type tests, Gill's number 18 where the PS2 has 17, no setFinishType), and the arcade's is what
// is written.
#include "fight_core.h"

#include <iterator>

namespace cps3recomp {

namespace fighting {

namespace {
    constexpr uint32_t kManageStep = 0x020154A6;  // word (C_No[0])
    constexpr uint32_t kConclusion = 0x020113C0;  // word: 0 a knock-out, 1 a double, 2 a time over (Conclusion_Type)
    constexpr uint32_t kRoundResult = 0x0201137C; // word: how the round ended, bits (Round_Result)
    constexpr uint32_t kWinner = 0x0201137F, kLoser = 0x02011380; // bytes (Winner_id, Loser_id)
    constexpr uint32_t kWins = 0x02011382;        // words, a player each: rounds won (PL_Wins)
    constexpr uint32_t kMarks = 0x020113C4;       // words [player][round of 4]: the panel's mark for each round won
                                                  // (win_type)
    constexpr uint32_t kPlayType = 0x020113B4;    // word: 0 against the computer, 1 two people (Play_Type)
    constexpr uint32_t kRounds = 0x02016D7D;      // bytes, by Play_Type: rounds to win, less one (Battle_Number)
    constexpr uint32_t kPerson = 0x020156E4;      // bytes, a player each: a person plays it this round (Round_Operator)
    constexpr uint32_t kPlayTime = 0x02011372;    // word: seconds fought, the computer's levels' (Control_Time)
    constexpr uint32_t kMostPlayTime = 0x02016AD4; // word (Limit_Time)
    constexpr uint32_t kPerfect = 0x020113DE;     // byte: the round a perfect (Perfect_Flag)
    constexpr uint32_t kPerfects = 0x02016AED;    // bytes, a player each (Perfect_Counter)
    constexpr uint32_t kShinGoukiMusic = 0x02016B79; // byte (Shin_Gouki_BGM)
    constexpr uint32_t kMessage = 0x0201137E;     // byte: the big message asked for (request_message)
    constexpr uint32_t kMessageWhich = 0x020113D6; // word (message_index)
    // ... the match's records
    constexpr uint32_t kMatchWinner = 0x020113BC, kMatchLoser = 0x020113BD; // bytes (WINNER, LOSER)
    constexpr uint32_t kCharacters = 0x02011387;  // bytes, a player each (My_char)
    constexpr uint32_t kColours = 0x02015683;     // bytes (Player_Color)
    constexpr uint32_t kLoserCharacter = 0x020156AA, kLoserColour = 0x020156AC; // bytes (Stock_My_char, _Player_Color)
    constexpr uint32_t kPersonalTime = 0x0201583A; // words (SC_Personal_Time)
    constexpr uint32_t kStageContinues = 0x02016B74; // bytes (Stage_Continue)
    constexpr uint32_t kRanksShown = 0x02015452;  // bytes [player][4] (Request_Disp_Rank)
    constexpr uint32_t kComputerColour = 0x020156BC; // words (Stock_Com_Color)
    constexpr uint32_t kComputerArt = 0x020156C0; // bytes (Stock_Com_Arts)
    constexpr uint32_t kOpponent = 0x02015504;    // byte: the computer's character (EM_id)
    constexpr uint32_t kOpponentsBeaten = 0x02015506; // words, a player each (VS_Index)
    constexpr uint32_t kBeaten = 0x02016D1C;      // bytes [player][10]: the opponents beaten, in order (EM_History)
    constexpr uint32_t kResultTime = 0x02016DA4;  // words (Result_Timer)
    constexpr uint32_t kBrokenCom = 0x020157D4;   // bytes [player][24] (Break_Com)
    constexpr uint32_t kStraight = 0x02016AEF;    // bytes (Straight_Counter)
    constexpr uint32_t kStraightFlag = 0x02016DAF; // bytes (Straight_Flag)
    constexpr uint32_t kRoundLevel = 0x0201137A;  // word (Round_Level)
    constexpr uint32_t kScores = 0x0201139C;      // longs [player][3] (Score)
    constexpr uint32_t kStageScores = 0x02016B28; // longs (Stage_Stock_Score)
    constexpr uint32_t kRoundScores = 0x020156E8; // longs (Stock_Score)
    constexpr uint32_t kWinRecord = 0x02016CD4, kWinRecordKept = 0x02016CD8; // words (Win_Record, Stock_Win_Record)
    constexpr int8_t kGill = 18;
    // ... and the routines called
    constexpr uint32_t kSound = 0x061394D0;       // SsRequest (r4 the sound)
    constexpr uint32_t kFinishSound = 0x0608FFBA; // Finish_SE
    constexpr uint32_t kFadeIn = 0x0609CAEC;      // Setup_BGM_Fade_In (r4 the frames)
    constexpr uint32_t kMusicControl = 0x0613A324; // SsBgmControl (r4, r5 the volume)
    constexpr uint32_t kWinMark = 0x06099B56, kCheckPerfect = 0x0609C2F0, kMatchRecords = 0x0609C750;
    constexpr uint32_t kWinRecords = 0x0609BE48, kLevelControl = 0x0609C232, kToJudges = 0x0609C280;
    constexpr uint32_t kPoolScore = 0x0609A924;   // Pool_Score (r4 the player)
    constexpr uint32_t kRecordWon = 0x0609BEB6, kRecordLost = 0x0609C0D8; // (r4 the player: the two people's records)

    // ... the steps'
    constexpr uint32_t kSubStep = 0x020154A8;     // word (C_No[1])
    constexpr uint32_t kStepTime = 0x020154A4;    // word: frames left in a step (C_Timer)
    constexpr uint32_t kOver = 0x020154AE;        // byte: the round's end is over (Complete_Victory)
    constexpr uint32_t kEndFrames = 0x02015434;   // word: frames the round's end has (900 from the conclusion)
    constexpr uint32_t kJudgesRound = 0x020113C2; // byte: the judges' round under way
    constexpr uint32_t kJudgeGals = 0x02015636;   // byte: the judges' girls (Event_Judge_Gals: -1 on)
    constexpr uint32_t kSettleSteps = 0x02068C5C; // words: the players' routine's step, its kind, its step, ... (pcon_rno)
    constexpr uint32_t kAppearType = 0x02068C64;  // word (appear_type)
    constexpr uint32_t kComboStopped = 0x02015660; // byte (Stop_Combo)
    constexpr uint32_t kCover = 0x0201542A;       // byte: frames the screen stays covered (Cover_Timer)
    constexpr uint32_t kSuicide = 0x02011425;     // byte (Suicide[6])
    constexpr uint32_t kStageNumber = 0x02026BF6; // word: the stage (bg_w.stage)
    constexpr uint32_t kJudgeScreens = 0x0619DE02, kJudgeScreens2 = 0x0619DDD4; // words, by stage
    constexpr uint32_t kSoundsStopped = 0x020156D2; // byte (Stop_SG)
    constexpr uint32_t kMusicStep = 0x02016D10, kMusicTime = 0x02016D12; // bytes (BGM_No[0], BGM_Timer[0])
    constexpr uint32_t kHalfFade = 0x0201140A;    // byte (Fade_Half_Flag)
    constexpr uint32_t kJudgement = 0x020113DC;   // byte (Complete_Judgement)
    constexpr uint32_t kJudgedWins = 0x02016B82;  // bytes, a player each: rounds the judges gave it
    constexpr uint32_t kRoundNumber = 0x0201138A; // byte (Round_num)
    constexpr uint32_t kSkip = 0x06091C50;        // Button_Cut_EX (r4 the frames' word, r5 the frames a button cuts it
                                                  // to): r0 1 when they are over
    constexpr uint32_t kJudgeWinner = 0x0609C342; // Judge_Winner
    constexpr uint32_t kComputerWins = 0x060D7396;
    constexpr uint32_t kScreenStart = 0x060911E4, kScreen = 0x06091214, kScreenBack = 0x060912FC; // Switch_Screen_Init,
                                                  // Switch_Screen, Switch_Screen_Revival (r0 1 when done)
    constexpr uint32_t kJudgesShown = 0x060BA6B4; // effect_J4_init
    constexpr uint32_t kScreenLayer = 0x060D4E56;
    constexpr uint32_t kBackground = 0x0609148A;
    constexpr uint32_t kPoses = 0x06093308;       // the fighters to their poses (r0 0)
    constexpr uint32_t kBanner = 0x060EE152;      // effect_58_init (r4, r5, r6: 100 and the winner)
    constexpr uint32_t kPanelMark = 0x060F64D6;   // effect_92_init (r4 the player, r5 its mark)
    constexpr uint32_t kChallenger = 0x0609C4C8;  // Quick_Entry

    uint32_t Extended(int32_t value) {
        return Signed(static_cast<int16_t>(value));
    }
    int8_t Winner(Guest &g) {
        return Byte(g, kWinner);
    }
    int8_t Loser(Guest &g) {
        return Byte(g, kLoser);
    }
    // The rounds a match needs (the board's number and one more)
    int32_t RoundsToWin(Guest &g) {
        return g.Read8(kRounds + Signed(Word(g, kPlayType))) + 1;
    }
    void Sound(Guest &g, uint32_t sound) {
        g.R(4) = sound;
        g.Call(kSound);
    }
    // Control_Music_Fade: the music faded in again from silence over the frames given
    void MusicFade(Guest &g, uint32_t frames) {
        g.R(4) = frames;
        g.Call(kFadeIn);
        g.R(5) = Signed(-128);
        g.R(4) = 0;
        g.Call(kMusicControl);
    }

    // 060999B0 (Game_Manage_4th): the round counted, by how it ended (above). Leaves r1, r2 and r7 as the game's code (the
    // management's step's caller, and what it calls, are not followed by liveout.py): mostly the last routine's.
    bool CountWin(Guest &g) {
        uint32_t tail = 0;
        {
            Frame frame(g, 28); // (r9-r14, pr)
            const int16_t kind = Word(g, kConclusion);
            if (kind == 0) {
                // (a knock-out)
                g.R(2) = 6;
                g.Write16(kManageStep, 6);
                g.Call(kWinMark);
                g.R(3) = kCheckPerfect;
                g.R(4) = Signed(Winner(g));
                g.Call(kCheckPerfect);
            } else if (kind == 1) {
                // (a double knock-out)
                Sound(g, 121);
                Sound(g, 139);
                g.R(3) = kToJudges;
                g.Call(kToJudges);
                if (g.R(0) != 0) {
                    g.R(3) = 4;
                    g.Write16(kManageStep, 4);
                    g.Return(60);
                    return true;
                }
                g.Write16(kManageStep, 5);
                g.Write16(kRoundResult, static_cast<uint16_t>(g.Read16(kRoundResult) | 0x400));
                g.Write16(kMarks + Signed(Word(g, kWins)) * 2, 5);
                g.Write16(kMarks + 8 + Signed(Word(g, kWins + 2)) * 2, 5);
                g.Write16(kWins, static_cast<uint16_t>(Word(g, kWins) + 1));
                g.Write16(kWins + 2, static_cast<uint16_t>(Word(g, kWins + 2) + 1));
                const int32_t rounds = RoundsToWin(g);
                g.R(0) = 5;
                g.R(1) = kRounds;
                g.R(2) = Signed(rounds);
                g.R(4) = kWins;
                g.R(5) = kRounds + Signed(Word(g, kPlayType));
                g.R(6) = 0;
                g.R(7) = kLoser;
                const int16_t one = Word(g, kWins), two = Word(g, kWins + 2);
                g.R(3) = Extended(one);
                const bool first = one >= rounds;
                if (!first) {
                    g.R(0) = Extended(two);
                    g.R(3) = Signed(rounds);
                    if (two < rounds) {
                        g.Return(80);
                        return true;
                    }
                }
                // (a player has the match: player 1 first)
                g.Write8(kWinner, first ? 0 : 1);
                g.Write8(kLoser, first ? 1 : 0);
                if (first) {
                    g.R(2) = kMatchRecords;
                } else {
                    g.R(3) = kMatchRecords;
                }
                g.Call(kMatchRecords);
                if (first) {
                    g.R(3) = kWinRecords;
                } else {
                    g.R(2) = kWinRecords;
                }
                tail = kWinRecords;
            } else {
                // (a time over)
                Sound(g, 143);
                g.R(4) = kPlayers;
                g.R(0) = kPlayerSize + 0x9E;
                g.R(2) = Extended(Word(g, kPlayers + 0x9E));
                g.R(3) = Extended(Word(g, kPlayers + kPlayerSize + 0x9E));
                if (g.R(2) == g.R(3)) {
                    g.R(2) = 4;
                    g.Write16(kManageStep, 4);
                    g.Return(40);
                    return true;
                }
                g.Write16(kManageStep, 6);
                g.Write16(kRoundResult, static_cast<uint16_t>(g.Read16(kRoundResult) | 1));
                const int8_t winner = Winner(g);
                g.Write16(kMarks + Signed(winner) * 8 + Signed(Word(g, kWins + Signed(winner) * 2)) * 2, 1);
                g.R(0) = Signed(winner) * 2;
                g.R(1) = kMarks + Signed(winner) * 8;
                g.R(2) = Signed(winner) * 8;
                g.R(3) = kCheckPerfect;
                g.R(4) = Signed(winner);
                g.Call(kCheckPerfect);
            }
            if (tail == 0) {
                // (the winner's round counted, the records kept)
                const int8_t winner = Winner(g);
                g.R(0) = Signed(winner) * 2;
                g.R(2) = kMatchRecords;
                g.R(3) = Signed(Word(g, kWins + g.R(0))) + 1;
                g.Write16(kWins + g.R(0), static_cast<uint16_t>(g.R(3)));
                g.Call(kMatchRecords);
                g.R(1) = kWinRecords;
                g.Call(kWinRecords);
                g.R(2) = kLevelControl;
                tail = kLevelControl;
            }
        }
        g.Call(tail);
        g.Return(100);
        return true;
    }

    // 06099B56 (Setup_Win_Mark): the winner's mark for the round (kMarks): 7 a knock-out by a guarded hit (Round_Result's
    // 0x200), 4 a super art's finish (0x180) or Shin Akuma's (0x800), the music faded in again for either (but for Shin
    // Akuma's own music), else 1; and the round's sounds (the bell, the crowd) and the finish's
    bool WinMark(Guest &g) {
        Frame frame(g, 28); // (r9-r14, pr)
        const uint16_t result = g.Read16(kRoundResult);
        auto mark = [&](uint16_t kind) {
            const int8_t winner = Winner(g);
            g.Write16(kMarks + Signed(winner) * 8 + Signed(Word(g, kWins + Signed(winner) * 2)) * 2, kind);
        };
        bool bell = true;
        if (result & 0x200) {
            mark(7);
        } else if (result & 0x180) {
            MusicFade(g, 150);
            mark(4);
            bell = false;
        } else if (result & 0x800) {
            if (g.Read8(kShinGoukiMusic) == 0) {
                MusicFade(g, 150);
            } else {
                g.Write8(kShinGoukiMusic, 0);
            }
            mark(4);
            bell = false;
        } else {
            mark(1);
        }
        if (bell) {
            Sound(g, 121);
        }
        Sound(g, 140);
        g.Call(kFinishSound);
        g.Return(60);
        return true;
    }

    // 0609C2F0 (Check_Perfect): a player (r4) who took no damage in the round: a perfect (its mark 3, Round_Result's bit
    // 2, its count). Leaves r5 its record, or kRoundResult's place when it was.
    bool Perfect(Guest &g) {
        const int16_t side = static_cast<int16_t>(g.R(4));
        const uint32_t player = kPlayers + Extended(side * static_cast<int32_t>(kPlayerSize));
        g.R(5) = player;
        if (g.Read16(player + 0x9C) != g.Read16(player + 0x9E)) { // (vitality, vital_new)
            g.Return(14);
            return true;
        }
        g.Write8(kPerfect, 1);
        const uint32_t winner = Signed(Winner(g));
        g.Write8(kPerfects + winner, static_cast<uint8_t>(g.Read8(kPerfects + winner) + 1));
        g.R(5) = kRoundResult;
        g.Write16(kRoundResult, static_cast<uint16_t>(g.Read16(kRoundResult) | 2));
        g.Write16(kMarks + Signed(side) * 8 + Signed(Word(g, kWins + Signed(side) * 2)) * 2, 3);
        g.Return(30);
        return true;
    }

    // 0609C232 (Update_Level_Control): the computer's play time: 40 seconds more when a person beat it (to the most), 40
    // less (to none) when it won; none between two people. Leaves the registers as the game's code (it is jumped to).
    bool LevelControl(Guest &g) {
        g.R(4) = kPlayTime;
        g.R(5) = kPerson;
        g.R(3) = kWinner;
        g.R(0) = Signed(Winner(g));
        g.R(2) = Signed(Byte(g, kPerson + g.R(0)));
        if (g.R(2) == 0) {
            const int16_t time = static_cast<int16_t>(Word(g, kPlayTime) - 40);
            g.Write16(kPlayTime, static_cast<uint16_t>(time));
            g.R(1) = Extended(time);
            g.SetT(time >= 0);
            if (time < 0) {
                g.R(1) = 0;
                g.Write16(kPlayTime, 0);
            }
            g.Return(14);
            return true;
        }
        g.R(1) = kLoser;
        g.R(0) = Signed(Loser(g));
        g.R(2) = Signed(Byte(g, kPerson + g.R(0)));
        if (g.R(2) != 0) {
            g.SetT(false);
            g.Return(12);
            return true;
        }
        const int16_t time = static_cast<int16_t>(Word(g, kPlayTime) + 40);
        g.Write16(kPlayTime, static_cast<uint16_t>(time));
        const int16_t most = Word(g, kMostPlayTime);
        g.R(0) = kMostPlayTime;
        g.R(2) = Extended(most);
        g.R(3) = Extended(time);
        g.R(5) = Extended(most);
        g.SetT(time > most);
        if (time > most) {
            g.Write16(kPlayTime, static_cast<uint16_t>(most));
        }
        g.Return(20);
        return true;
    }

    // 0609C280 (Judge_Next_Disposal): r0 1 when both players have won as many rounds and are one from the match: the
    // judges decide
    bool ToJudges(Guest &g) {
        const int16_t one = Word(g, kWins);
        g.R(0) = one == Word(g, kWins + 2) && one >= g.Read8(kRounds + Signed(Word(g, kPlayType))) ? 1 : 0;
        g.Return(12);
        return true;
    }

    // 0609C274 (request_center_message): the big message (r4: 2 time over, 3 ...) asked for
    bool CentreMessage(Guest &g) {
        g.R(1) = kMessageWhich;
        g.R(2) = kMessage;
        g.R(3) = 1;
        g.Write8(kMessage, 1);
        g.Write16(kMessageWhich, static_cast<uint16_t>(g.R(4)));
        g.Return(5);
        return true;
    }

    // 0609C750 (Update_VS_Data): the match's records, once a player has won it (else the loser's score put back as it was
    // at the round's start, or a person's points pooled: Pool_Score): the match's winner and loser, the loser's
    // character and colour kept; against the computer, a person's win: its play time kept, the next opponent's ranks,
    // colour and art not chosen yet, the opponent beaten noted (Gill's, kGill, by the count so far), the results' time,
    // a straight win's, the round level up to 7; the computer's win: the person's score put back as at the stage's
    // start, its record and straight wins none. Leaves the registers as the game's code (its callers' are not known).
    bool MatchRecords(Guest &g) {
        uint32_t tail = 0;
        {
            Frame frame(g, 8); // (r13, r14)
            const int8_t winner = Winner(g);
            g.R(7) = kLoser;
            g.R(6) = kPerson;
            g.R(3) = kPlayType;
            g.R(1) = Extended(Word(g, kWins + Signed(winner) * 2));
            g.R(2) = Signed(RoundsToWin(g));
            if (static_cast<int32_t>(g.R(1)) < static_cast<int32_t>(g.R(2))) {
                // (not won yet)
                g.R(0) = Signed(winner);
                g.R(2) = Signed(Byte(g, kPerson + g.R(0)));
                if (g.R(2) != 0) {
                    g.R(4) = Signed(winner);
                    g.R(2) = kPoolScore;
                    g.SetT(false);
                    tail = kPoolScore;
                } else {
                    const int8_t loser = Loser(g);
                    g.R(2) = Signed(loser * 12);
                    g.R(0) = kScores;
                    g.R(1) = g.Read32(kRoundScores + Signed(loser) * 4);
                    g.Write32(kScores + g.R(2), g.R(1));
                    g.SetT(false); // (from a shift)
                }
            } else {
                g.Write8(kMatchWinner, static_cast<uint8_t>(winner));
                g.Write8(kMatchLoser, g.Read8(kLoser));
                const uint32_t loser = Signed(Byte(g, kMatchLoser));
                g.Write8(kLoserCharacter + loser, g.Read8(kCharacters + loser));
                g.R(1) = Signed(Byte(g, kColours + loser));
                g.Write8(kLoserColour + loser, static_cast<uint8_t>(g.R(1)));
                g.R(0) = kColours;
                g.R(4) = kMatchWinner;
                g.R(5) = kMatchLoser;
                g.R(7) = loser;
                g.R(2) = Extended(Word(g, kPlayType));
                g.SetT(g.R(2) == 0);
                if (g.R(2) == 0) {
                    const uint32_t won = Signed(Byte(g, kMatchWinner));
                    g.R(7) = 0;
                    g.SetT(Byte(g, kPerson + won) == 0);
                    if (Byte(g, kPerson + won) != 0) {
                        // (a person beat the computer)
                        g.Write16(kPersonalTime + won * 2, g.Read16(kPlayTime));
                        g.Write8(kStageContinues + won, 0);
                        for (uint32_t n = 0; n < 4; n++) {
                            g.Write8(kRanksShown + loser * 4 + n, 0xFF);
                        }
                        g.Write16(kComputerColour + won * 2, 0xFFFF);
                        g.Write8(kComputerArt + won, 0xFF);
                        const int8_t opponent = Byte(g, kOpponent);
                        const uint32_t beaten = kOpponentsBeaten + won * 2;
                        g.Write8(kBeaten + Signed(static_cast<int8_t>(won * 10)) + Signed(Word(g, beaten)), static_cast<uint8_t>(opponent));
                        g.Write16(kResultTime + won * 2, static_cast<uint16_t>(g.Read16(kResultTime + won * 2) + 30));
                        const uint32_t broken = kBrokenCom + Signed(static_cast<int8_t>(won * 24)) + Signed(opponent);
                        if (opponent == kGill) {
                            g.Write8(broken, g.Read8(beaten + 1));
                        } else {
                            g.Write16(beaten, static_cast<uint16_t>(g.Read16(beaten) + 1));
                            g.Write8(broken, 1);
                        }
                        g.R(1) = broken;
                        g.R(0) = kWins;
                        g.R(2) = Extended(Word(g, kWins + loser * 2));
                        if (g.R(2) != 0) {
                            g.Write8(kStraight + won, 0);
                            g.Write8(kStraightFlag + won, 1);
                            g.R(0) = kStraightFlag;
                            g.R(2) = won;
                        }
                        g.R(4) = kRoundLevel;
                        g.R(5) = 7;
                        g.R(6) = kOpponent;
                        const int16_t level = static_cast<int16_t>(Word(g, kRoundLevel) + 1);
                        g.Write16(kRoundLevel, static_cast<uint16_t>(level));
                        g.R(3) = Extended(level);
                        g.SetT(level > 7);
                        if (level > 7) {
                            g.Write16(kRoundLevel, 7);
                        }
                    } else {
                        // (the computer won)
                        g.Write32(kScores + Signed(static_cast<int8_t>(loser * 12)), g.Read32(kStageScores + loser * 4));
                        g.Write16(kWinRecord + loser * 2, 0);
                        g.Write8(kStraight + loser, 0);
                        g.Write8(kStraightFlag + loser, 1);
                        g.R(0) = kStraightFlag;
                        g.R(1) = loser;
                        g.R(2) = loser;
                        g.SetT(false); // (from a shift)
                    }
                }
            }
        }
        if (tail != 0) {
            g.Call(tail);
        }
        g.Return(60);
        return true;
    }

    // 0609BE48 (Ck_Win_Record): once a player has won the match: between two people, the winner's run of wins (to 999)
    // and the two people's records (kRecordWon, kRecordLost, while it is not past 999); against the computer, the
    // loser's record (kRecordLost). Leaves the registers as the game's code (it is jumped to).
    bool WinRecords(Guest &g) {
        uint32_t tail = 0;
        {
            Frame frame(g, 12); // (r13, r14, pr)
            const int8_t winner = Winner(g);
            const int16_t type = Word(g, kPlayType);
            g.R(3) = kPlayType;
            g.R(4) = Signed(winner) * 2;
            g.R(5) = Signed(type);
            g.R(2) = Extended(Word(g, kWins + g.R(4)));
            g.R(0) = kRounds;
            g.R(1) = Signed(RoundsToWin(g));
            g.SetT(static_cast<int32_t>(g.R(2)) >= static_cast<int32_t>(g.R(1)));
            if (!g.T()) {
                g.Return(20);
                return true;
            }
            g.R(0) = Signed(type);
            g.SetT(type == 1);
            if (type != 1) {
                g.R(1) = kLoser;
                g.R(4) = Signed(Loser(g));
                tail = kRecordLost;
            } else {
                const uint32_t record = kWinRecord + Signed(winner) * 2;
                g.R(5) = 999;
                g.R(4) = record;
                const uint16_t wins = static_cast<uint16_t>(g.Read16(record) + 1);
                g.Write16(record, wins);
                g.R(2) = wins;
                g.SetT(wins > 999);
                if (wins > 999) {
                    g.R(0) = Signed(winner) * 2;
                    g.Write16(record, 999);
                } else {
                    g.R(4) = Signed(winner);
                    g.Call(kRecordWon);
                    g.R(3) = kLoser;
                    g.R(4) = Signed(Loser(g));
                    g.Call(kRecordLost);
                }
                const uint32_t at = Signed(Winner(g)) * 2;
                g.R(4) = at;
                g.R(0) = kWinRecordKept;
                g.R(3) = Extended(Word(g, kWinRecord + at));
                g.Write16(kWinRecordKept + at, static_cast<uint16_t>(g.R(3)));
                g.SetT(false); // (from a shift)
            }
        }
        if (tail != 0) {
            g.Call(tail);
        }
        g.Return(40);
        return true;
    }

    // A step's own steps: the routine by C_No[1] from its table (copied onto the stack: the frame holds it), r0 the
    // stack, r1 kSubStep's place, r2 the routine, r3 its offset in the table, as the game's code calls it
    bool Steps(Guest &g, uint32_t table, uint32_t count) {
        Frame frame(g, 4 + count * 4); // (pr, the table)
        g.R(1) = kSubStep;
        g.R(3) = Signed(Word(g, kSubStep)) << 2;
        g.R(2) = g.Read32(table + g.R(3));
        g.R(0) = g.R(15);
        g.Call(g.R(2));
        g.Return(40);
        return true;
    }

    // C_No[1] on a step (r4 its place, r3 the step now)
    void NextSubStep(Guest &g) {
        g.R(4) = kSubStep;
        g.R(3) = Signed(Word(g, kSubStep)) + 1;
        g.Write16(kSubStep, static_cast<uint16_t>(g.R(3)));
    }

    // 06099D5E (Game_Manage_5th): the judges' round, its steps (table 0x06194518: the eight below)
    bool ManageJudges(Guest &g) {
        return Steps(g, 0x06194518, 8);
    }

    // 06099D9C (Game_Manage_5_0): once the round's end is over (Complete_Victory, or its frames run out), the judges
    // called, half a second's wait
    bool JudgesCalled(Guest &g) {
        g.R(0) = kOver;
        g.R(1) = Signed(Byte(g, kOver));
        g.SetT(g.R(1) == 0);
        if (g.R(1) == 0) {
            g.R(3) = kEndFrames;
            g.R(2) = Extended(Word(g, kEndFrames) - 1);
            g.Write16(kEndFrames, static_cast<uint16_t>(g.R(2)));
            g.SetT(g.R(2) == 0);
            if (g.R(2) != 0) {
                g.Return(10);
                return true;
            }
        }
        NextSubStep(g);
        g.R(1) = kStepTime;
        g.Write16(kStepTime, 30);
        g.R(0) = kJudgesRound;
        g.Write8(kJudgesRound, 1);
        g.R(2) = 0;
        g.R(3) = kJudgeGals;
        g.Write8(kJudgeGals, 0);
        g.Return(20);
        return true;
    }

    // 06099DD0 (Game_Manage_5_1): the wait over (a button cuts it short), the message (3) and its sound
    bool JudgesMessage(Guest &g) {
        {
            Frame frame(g, 4); // (pr)
            g.R(4) = kStepTime;
            g.R(5) = 10;
            g.Call(kSkip);
            g.SetT(g.R(0) == 0);
            if (g.R(0) == 0) {
                g.Return(20);
                return true;
            }
            NextSubStep(g);
            g.R(1) = kMessageWhich;
            g.R(3) = kMessage;
            g.Write8(kMessage, 1);
            g.Write16(kMessageWhich, 3);
            g.R(2) = kSound;
        }
        Sound(g, 0x9A);
        g.Return(20);
        return true;
    }

    // 06099E00 (Game_Manage_5_2): the message gone, half a second's wait
    bool JudgesWait(Guest &g) {
        g.R(3) = kMessage;
        g.R(0) = Signed(Byte(g, kMessage));
        g.SetT(g.R(0) == 0);
        if (g.R(0) == 0) {
            NextSubStep(g);
            g.R(2) = g.R(3);
            g.R(3) = 30;
            g.R(1) = kStepTime;
            g.Write16(kStepTime, 30);
        }
        g.Return(10);
        return true;
    }

    // 06099E54 (Game_Manage_5_3): the wait over, the judges' choice made (Judge_Winner), the combos stopped, the screen
    // switched to the judges
    bool JudgesChoose(Guest &g) {
        {
            Frame frame(g, 4); // (pr)
            g.R(4) = kStepTime;
            g.R(5) = 10;
            g.Call(kSkip);
            g.SetT(g.R(0) == 0);
            if (g.R(0) == 0) {
                g.Return(20);
                return true;
            }
            NextSubStep(g);
            g.R(2) = kJudgeWinner;
            g.Call(kJudgeWinner);
            g.R(1) = kComputerWins;
            g.Call(kComputerWins);
            g.R(1) = kComboStopped;
            g.R(2) = 1;
            g.Write8(kComboStopped, 1);
            g.R(3) = kScreenStart;
            g.R(4) = 3;
            g.R(5) = 3;
        }
        g.Call(kScreenStart);
        g.Return(40);
        return true;
    }

    // 06099E8A (Game_Manage_5_4): the screen switched: the judges shown (their screens by the stage), the fighters'
    // routine back to its start
    bool JudgesShown(Guest &g) {
        Frame frame(g, 4); // (pr)
        g.R(3) = kScreen;
        g.Call(kScreen);
        g.SetT(g.R(0) == 0);
        if (g.R(0) == 0) {
            g.Return(20);
            return true;
        }
        NextSubStep(g);
        g.R(5) = 3;
        g.R(2) = kScreenStart;
        g.R(4) = 3;
        g.Call(kScreenStart);
        g.R(3) = 5;
        g.R(2) = kCover;
        g.Write8(kCover, 5);
        g.R(1) = kJudgesShown;
        g.Call(kJudgesShown);
        for (const uint32_t screens : {kJudgeScreens, kJudgeScreens2}) {
            if (screens == kJudgeScreens) {
                g.R(1) = kSuicide;
                g.Write8(kSuicide, 1);
            }
            g.R(0) = screens;
            g.R(2) = kScreenLayer;
            g.R(3) = kStageNumber;
            g.R(5) = g.R(6) = g.R(7) = 15;
            g.R(4) = Signed(Word(g, kStageNumber)) << 1;
            g.SetT(false); // (from a shift: the stage's number is small)
            g.R(4) = g.Read16(screens + g.R(4));
            g.Call(kScreenLayer);
        }
        g.R(0) = 0;
        g.R(2) = kAppearType;
        g.R(3) = 3;
        g.R(4) = 0;
        g.R(5) = kSettleSteps;
        for (uint32_t n = 0; n < 4; n++) {
            g.Write16(kSettleSteps + n * 2, 0);
        }
        g.Write16(kAppearType, 3);
        g.Return(80);
        return true;
    }

    // 06099EFC (Game_Manage_5_5): the screen covered for five frames, then the fighters to the judges' verdict, the
    // background, the screen switched back
    bool JudgesCover(Guest &g) {
        {
            Frame frame(g, 4); // (pr)
            g.R(4) = kCover;
            g.R(3) = Signed(static_cast<int8_t>(Byte(g, kCover) - 1));
            g.Write8(kCover, static_cast<uint8_t>(g.R(3)));
            g.SetT(g.R(3) == 0);
            if (g.R(3) != 0) {
                g.Return(14);
                return true;
            }
            NextSubStep(g);
            g.R(4) = kSettleSteps;
            g.Write16(kSettleSteps + 2, 3);
            g.Write16(kSettleSteps + 4, 1);
            g.R(0) = 1;
            g.R(3) = kBackground;
            g.Call(kBackground);
            g.R(5) = 3;
            g.R(2) = kScreenStart;
            g.R(4) = 3;
        }
        g.Call(kScreenStart);
        g.Return(30);
        return true;
    }

    // 06099F32 (Game_Manage_5_6): the screen back: a second's wait, the sounds on, the judges' music asked for
    bool JudgesBack(Guest &g) {
        Frame frame(g, 4); // (pr)
        g.R(3) = kScreenBack;
        g.Call(kScreenBack);
        g.SetT(g.R(0) == 0);
        if (g.R(0) == 0) {
            g.Return(20);
            return true;
        }
        NextSubStep(g);
        g.Write16(kStepTime, 60);
        g.Write8(kSoundsStopped, 0);
        g.Write8(kMusicStep, 3);
        g.Write8(kMusicTime, 1);
        g.R(0) = kSoundsStopped;
        g.R(1) = kMusicTime;
        g.R(2) = kMusicStep;
        g.R(3) = 1;
        g.Return(30);
        return true;
    }

    // 06099F64 (Game_Manage_5_7): the second over, the judges' winner's round counted (mark 6, its count of the judges'
    // rounds, a perfect, its win, the records, the computer's play time) and on to the round's end (step 6, its step 7)
    bool JudgesCount(Guest &g) {
        {
            Frame frame(g, 8); // (r14, pr)
            g.R(4) = kStepTime;
            g.R(3) = Extended(Word(g, kStepTime) - 1);
            g.Write16(kStepTime, static_cast<uint16_t>(g.R(3)));
            g.SetT(g.R(3) == 0);
            if (g.R(3) != 0) {
                g.Return(14);
                return true;
            }
            g.Write16(kManageStep, 6);
            g.Write16(kSubStep, 7);
            g.Write16(kStepTime, 30);
            g.Write8(kHalfFade, 1);
            g.Write8(kJudgement, 1);
            g.Write16(kRoundResult, static_cast<uint16_t>(g.Read16(kRoundResult) | 0x8000));
            const int8_t winner = Winner(g);
            g.Write16(kMarks + Signed(winner) * 8 + Signed(Word(g, kWins + Signed(winner) * 2)) * 2, 6);
            g.R(2) = Signed(Byte(g, kJudgedWins + Signed(winner))) + 1;
            g.Write8(kJudgedWins + Signed(winner), static_cast<uint8_t>(g.R(2)));
            g.R(0) = kJudgedWins;
            g.R(1) = kMarks;
            g.R(5) = 6;
            g.R(3) = kCheckPerfect;
            g.R(4) = Signed(winner);
            g.Call(kCheckPerfect);
            g.R(0) = kWins;
            g.R(2) = Signed(Winner(g)) * 2;
            g.R(3) = Signed(Word(g, kWins + g.R(2))) + 1;
            g.Write16(kWins + g.R(2), static_cast<uint16_t>(g.R(3)));
            g.R(2) = kMatchRecords;
            g.Call(kMatchRecords);
            g.R(1) = kLevelControl;
        }
        g.Call(kLevelControl);
        g.Return(100);
        return true;
    }

    // 06099FE4 (Game_Manage_6th): the round's end (C_No[1] 0): once it is over, the fighters to their poses, the
    // winner's banner, both players' last marks on the panel; (1) a second later, the next round (step 7, the round's
    // number on) and a challenger let in. Leaves r1, r2 and r7 as the game's code: the last routine's, or r1
    // kEndFrames' place and r2 the round's end's flag's place or the step's frames left.
    bool RoundEnd(Guest &g) {
        uint32_t tail = 0;
        {
            Frame frame(g, 8); // (pr, a long)
            g.R(5) = kStepTime;
            g.R(4) = kSubStep;
            g.R(0) = Extended(Word(g, kSubStep));
            g.R(6) = 0;
            const int16_t step = Word(g, kSubStep);
            if (step == 0) {
                g.R(2) = kOver;
                g.R(3) = Signed(Byte(g, kOver));
                if (g.R(3) == 0) {
                    g.R(1) = kEndFrames;
                    g.R(0) = Extended(Word(g, kEndFrames) - 1);
                    g.Write16(kEndFrames, static_cast<uint16_t>(g.R(0)));
                    if (g.R(0) != 0) {
                        g.SetT(false);
                        g.Return(20);
                        return true;
                    }
                }
                g.R(2) = 60;
                g.R(3) = Signed(Word(g, kSubStep)) + 1;
                g.Write16(kSubStep, static_cast<uint16_t>(g.R(3)));
                g.Write16(kStepTime, 60);
                g.R(4) = kSettleSteps;
                g.Write16(kSettleSteps + 2, 3);
                g.Write16(kSettleSteps + 4, 0);
                g.R(0) = 0;
                g.R(3) = kPoses;
                g.Call(kPoses);
                g.R(5) = 1;
                g.R(2) = kBanner;
                g.R(3) = kWinner;
                g.R(6) = Signed(Winner(g)) + 100;
                g.R(4) = 6;
                g.Call(kBanner);
                g.R(5) = Signed(Word(g, kMarks + ((Signed(Word(g, kWins)) - 1) << 1)));
                g.R(3) = kPanelMark;
                g.R(4) = 0;
                g.Call(kPanelMark);
                g.R(4) = 1;
                g.R(2) = kPanelMark;
                g.R(3) = kMarks;
                g.R(0) = Signed(Word(g, kMarks + 8 + ((Signed(Word(g, kWins + 2)) - 1) << 1)));
                g.R(5) = g.R(0);
                tail = kPanelMark;
            } else if (step == 1) {
                g.R(2) = Extended(Word(g, kStepTime) - 1);
                g.Write16(kStepTime, static_cast<uint16_t>(g.R(2)));
                if (g.R(2) != 0) {
                    g.SetT(false);
                    g.Return(20);
                    return true;
                }
                g.R(1) = kManageStep;
                g.Write16(kManageStep, 7);
                g.Write16(kSubStep, 0);
                g.R(4) = kRoundNumber;
                g.R(3) = Signed(Byte(g, kRoundNumber)) + 1;
                g.Write8(kRoundNumber, static_cast<uint8_t>(g.R(3)));
                g.R(2) = kChallenger;
                tail = kChallenger;
            } else {
                g.SetT(false);
                g.Return(10);
                return true;
            }
        }
        g.Call(tail);
        g.Return(60);
        return true;
    }

    // --- the next round's start (step 7, Game_Manage_7th, its steps by C_No[1]) ---
    constexpr uint32_t kRoundGrades = 0x0609317E; // grade_makeup_round_parameter (r4 the winner)
    constexpr uint32_t kCombos = 0x020288A4;      // bytes: a combo's count still shown (cmb_all_stock, cmb_calc_now)
    constexpr uint32_t kWinnerShown = 0x0609C114; // Check_Disp_Winner: r0 1 when the winner's words are shown
    constexpr uint32_t kShowWinner = 0x0609C198;  // Disp_Winner
    constexpr uint32_t kMusicOut = 0x0609C912;    // Check_Fade_Out_BGM (r4 the frames)
    constexpr uint32_t kMessages = 0x020113E2;    // words: the messages' place (+4 the other)
    constexpr uint32_t kMessageGone = 0x060D632A;

    // 0609A13C (Game_Manage_7th): the next round's start, its steps (table 0x06194538: ten)
    bool ManageNextRound(Guest &g) {
        return Steps(g, 0x06194538, 10);
    }

    // 0609A182 (Game_Manage_7_0): once the round's end is over (Complete_Victory, or its frames), the round's grades for
    // the winner, its mark on the panel and its banner
    bool NextRoundStart(Guest &g) {
        uint32_t tail = 0;
        {
            Frame frame(g, 8); // (r14, pr)
            g.R(0) = kOver;
            g.R(1) = Signed(Byte(g, kOver));
            if (g.R(1) == 0) {
                g.R(3) = kEndFrames;
                g.R(2) = Extended(Word(g, kEndFrames) - 1);
                g.Write16(kEndFrames, static_cast<uint16_t>(g.R(2)));
                g.SetT(g.R(2) == 0);
                if (g.R(2) != 0) {
                    g.Return(14);
                    return true;
                }
            }
            NextSubStep(g);
            g.R(2) = 1;
            g.R(1) = kStepTime;
            g.Write16(kStepTime, 1);
            g.R(3) = kRoundGrades;
            g.R(4) = Signed(Winner(g));
            g.Call(kRoundGrades);
            const int8_t winner = Winner(g);
            g.R(0) = Signed((Signed(Word(g, kWins + Signed(winner) * 2)) - 1) << 1);
            g.R(1) = kWins;
            g.R(2) = kPanelMark;
            g.R(3) = kMarks;
            g.R(5) = Signed(Word(g, kMarks + Signed(winner) * 8 + g.R(0)));
            g.R(4) = Signed(winner);
            g.Call(kPanelMark);
            g.R(6) = Signed(Winner(g)) + 100;
            g.R(5) = 1;
            g.R(4) = 6;
            g.R(3) = kBanner;
            tail = kBanner;
        }
        g.Call(tail);
        g.Return(40);
        return true;
    }

    // 0609A1E6 (Game_Manage_7_1): a frame's wait, then ten
    bool NextRoundWait(Guest &g) {
        g.R(4) = kStepTime;
        g.R(3) = Extended(Word(g, kStepTime) - 1);
        g.Write16(kStepTime, static_cast<uint16_t>(g.R(3)));
        g.SetT(g.R(3) == 0);
        if (g.R(3) == 0) {
            g.R(5) = kSubStep;
            g.R(2) = 10;
            g.R(3) = Signed(Word(g, kSubStep)) + 1;
            g.Write16(kSubStep, static_cast<uint16_t>(g.R(3)));
            g.Write16(kStepTime, 10);
        }
        g.Return(12);
        return true;
    }

    // 0609A2A6 (Check_Disp_Combo): r0 1 while a combo's count is still shown
    bool CombosShown(Guest &g) {
        g.R(2) = kCombos;
        g.R(3) = Signed(Byte(g, kCombos));
        if (g.R(3) == 0) {
            g.R(1) = kCombos + 4;
            g.R(3) = Signed(Byte(g, kCombos + 4));
            if (g.R(3) == 0) {
                g.R(3) = kCombos + 5;
                g.R(2) = Signed(Byte(g, kCombos + 5));
                if (g.R(2) == 0) {
                    // (the rounds' count looked at, to no end: none)
                    g.R(2) = kPlayType;
                    g.R(3) = Extended(Word(g, kWins + Signed(Winner(g)) * 2));
                    g.R(1) = Signed(RoundsToWin(g));
                    g.SetT(static_cast<int32_t>(g.R(3)) >= static_cast<int32_t>(g.R(1)));
                    g.R(0) = 0;
                    g.Return(20);
                    return true;
                }
            }
        }
        g.SetT(false);
        g.R(0) = 1;
        g.Return(10);
        return true;
    }

    // 0609A204 (Game_Manage_7_2): the wait over (a button cuts it short) and no combo's count shown, the winner's words
    // shown (90 frames) or not (50); the music faded out when the computer won with no perfect
    bool NextRoundWinner(Guest &g) {
        uint32_t tail = 0;
        {
            Frame frame(g, 8); // (r14, pr)
            g.R(5) = 0x7FFF;
            g.R(4) = kStepTime;
            g.R(3) = kSkip;
            g.Call(kSkip);
            g.SetT(g.R(0) == 0);
            if (g.R(0) == 0) {
                g.Return(20);
                return true;
            }
            g.Call(0x0609A2A6);
            g.SetT(g.R(0) == 0);
            if (g.R(0) != 0) {
                g.R(3) = 1;
                g.Write16(kStepTime, 1);
                g.Return(30);
                return true;
            }
            g.R(4) = kSubStep;
            g.R(3) = kWinnerShown;
            g.R(2) = Signed(Word(g, kSubStep)) + 1;
            g.Write16(kSubStep, static_cast<uint16_t>(g.R(2)));
            g.Call(kWinnerShown);
            g.SetT(g.R(0) == 0);
            if (g.R(0) != 0) {
                g.R(1) = kShowWinner;
                g.Call(kShowWinner);
                g.R(2) = 90;
                g.Write16(kStepTime, 90);
            } else {
                g.R(3) = 50;
                g.Write16(kStepTime, 50);
            }
            g.R(1) = kWinner;
            g.R(0) = kPerson;
            g.R(3) = Signed(Byte(g, kPerson + Signed(Winner(g))));
            if (g.R(3) == 0) {
                g.R(2) = kPerfect;
                g.R(3) = Signed(Byte(g, kPerfect));
                if (g.R(3) == 0) {
                    g.SetT(true);
                    g.R(4) = 182;
                    g.R(3) = kMusicOut;
                    tail = kMusicOut;
                }
            }
            if (tail == 0) {
                g.SetT(false);
                g.Return(40);
                return true;
            }
        }
        g.Call(tail);
        g.Return(50);
        return true;
    }

    // 0609A2E6 (Game_Manage_7_3): the winner's words' frames over (against the computer, with a perfect too), the
    // messages taken off; with a perfect, its message next (ten frames); else the next step (8: the round's start or the
    // match's end), the judges' girls on (Event_Judge_Gals -1)
    bool NextRoundWordsOver(Guest &g) {
        Frame frame(g, 8); // (r14, pr)
        g.R(2) = kPlayType;
        g.R(3) = Extended(Word(g, kPlayType));
        if (g.R(3) == 0) {
            g.R(0) = kPerfect;
            g.R(1) = Signed(Byte(g, kPerfect));
        }
        if (g.R(3) == 0 && g.R(1) == 0) {
            g.R(2) = Extended(Word(g, kStepTime) - 1);
            g.Write16(kStepTime, static_cast<uint16_t>(g.R(2)));
            g.SetT(g.R(2) == 0);
            if (g.R(2) != 0) {
                g.Return(20);
                return true;
            }
        } else {
            g.R(1) = Extended(Word(g, kStepTime) - 1);
            g.Write16(kStepTime, static_cast<uint16_t>(g.R(1)));
            g.SetT(g.R(1) == 0);
            if (g.R(1) != 0) {
                g.Return(20);
                return true;
            }
        }
        g.R(5) = 9;
        g.R(7) = 16;
        g.R(3) = kMessageGone;
        g.R(6) = Signed(Word(g, kMessages)) + 47;
        g.R(4) = Signed(Word(g, kMessages + 4)) + 8;
        g.Call(kMessageGone);
        g.R(4) = kSubStep;
        g.R(3) = kPerfect;
        g.R(0) = Signed(Byte(g, kPerfect));
        g.SetT(g.R(0) == 0);
        if (g.R(0) != 0) {
            g.R(1) = 10;
            g.R(2) = Signed(Word(g, kSubStep)) + 1;
            g.Write16(kSubStep, static_cast<uint16_t>(g.R(2)));
            g.Write16(kStepTime, 10);
        } else {
            g.R(2) = 0;
            g.R(0) = kJudgeGals;
            g.R(5) = kManageStep;
            g.Write16(kManageStep, static_cast<uint16_t>(Word(g, kManageStep) + 1));
            g.R(1) = Signed(-1);
            g.Write16(kSubStep, 0);
            g.Write8(kJudgeGals, 0xFF);
        }
        g.Return(40);
        return true;
    }

    // --- the frame's hit tests (from the match's frame, Game2_1) ---
    constexpr uint32_t kHitsChecked = 0x0200EE44; // byte: throws tested this frame (ca_check_flag)
    constexpr uint32_t kSameTime = 0x0201581E;    // byte: both hit at once (aiuchi_flag)
    constexpr uint32_t kQueued = 0x0200EEC8;      // word: records in the hit queue (hpq_in)
    // ... and the routines
    constexpr uint32_t kQueueCleared = 0x0608EB5C; // clear_hit_queue
    constexpr uint32_t kCatchCheck = 0x0608E43C, kAttackCheck = 0x0608E5C4; // catch_hit_check, attack_hit_check
                                                                           // (fight_hits.cpp)
    constexpr uint32_t kJudgeResults = 0x0608C7F8; // set_judge_result: r0 1 when anything was hit or caught
    constexpr uint32_t kResultsExtra = 0x0608C860; // check_result_extra: the two fighters' hits on each other at once

    // (Not rewritten: the match's frame itself, 0x06095B24 (Game2_1: the frame counted, Time_Control, Player_control,
    // Game_Management, the panel, then hit_check_main_process) and its first frames, 0x06095B8E. A rewrite's calls to
    // the game's routines run at once, no interrupt taken; the frame's whole work, on a frame that runs long, is met by
    // the vertical blank's interrupt part way in the game's code (seen in a training session against Shin Akuma: its
    // writes at 0x02079BD4 came out otherwise), so the two cannot do the same.)

    // 0608C7C0 (hit_check_main_process): with two records or more queued, the throws tested (while they are: the flag
    // ca_check_flag), the attacks, and what was hit or caught set (set_judge_result; check_result_extra after it); the
    // queue cleared
    bool HitCheck(Guest &g) {
        {
            Frame frame(g, 4); // (pr)
            g.R(3) = 0;
            g.R(2) = kSameTime;
            g.Write8(kSameTime, 0);
            g.R(0) = kQueued;
            g.R(3) = 1;
            g.R(1) = Extended(Word(g, kQueued));
            g.SetT(Word(g, kQueued) > 1);
            if (Word(g, kQueued) > 1) {
                g.R(2) = kHitsChecked;
                g.R(1) = Signed(Byte(g, kHitsChecked));
                if (g.R(1) != 0) {
                    g.R(1) = kCatchCheck;
                    g.Call(kCatchCheck);
                }
                g.R(2) = kAttackCheck;
                g.Call(kAttackCheck);
                g.Call(kJudgeResults);
                g.SetT(g.R(0) == 0);
                if (g.R(0) != 0) {
                    g.Call(kResultsExtra);
                }
            }
            g.R(2) = kQueueCleared;
        }
        g.Call(kQueueCleared);
        g.Return(40);
        return true;
    }

    // (free: what liveout.py found no caller reads)
    const Lift kLifts[] = {
        {"CountWin", 0x060999B0, 0x1A6, 0xEA669C61DDBA99EAull, CountWin, true, 0x10079},
        {"WinMark", 0x06099B56, 0x116, 0x66E3D4704978B717ull, WinMark, true, 0x100FF},
        {"Perfect", 0x0609C2F0, 0x52, 0x65D59A6E87D514EEull, Perfect, true, 0x100DF},
        {"LevelControl", 0x0609C232, 0x42, 0x027F6587470AB7B0ull, LevelControl, true, 0},
        {"ToJudges", 0x0609C280, 0x2A, 0x8D9E3C406EBD0325ull, ToJudges, true, 0x100FE},
        {"CentreMessage", 0x0609C274, 0xC, 0x9612893C862EB6EFull, CentreMessage, true, 0},
        {"MatchRecords", 0x0609C750, 0x1A6, 0x8195301B256D1619ull, MatchRecords, true, 0},
        {"WinRecords", 0x0609BE48, 0x6E, 0x934E5A8694CE21AEull, WinRecords, true, 0},
        // (the steps' leftovers reach the round's management's caller, whose reading liveout.py does not follow: r1,
        // r2 and r7 kept as the game's code, as Game_Manage_4th's; and r0, which the judges' round's own steps leave it)
        {"ManageJudges", 0x06099D5E, 0x3E, 0x7E6FD5CE98FC86C9ull, ManageJudges, true, 0x10079},
        {"JudgesCalled", 0x06099D9C, 0x34, 0xDF0188E6A5D06DEAull, JudgesCalled, true, 0x10078},
        {"JudgesMessage", 0x06099DD0, 0x30, 0xF75805B8235E9009ull, JudgesMessage, true, 0x10078},
        {"JudgesWait", 0x06099E00, 0x1A, 0x6033C5C4FDB6F469ull, JudgesWait, true, 0x10078},
        {"JudgesChoose", 0x06099E54, 0x36, 0x1AF0A7A5B9B693BFull, JudgesChoose, true, 0x10078},
        {"JudgesShown", 0x06099E8A, 0x72, 0x0679FAA7FE581E29ull, JudgesShown, true, 0x10078},
        {"JudgesCover", 0x06099EFC, 0x36, 0xC1F8F0B323C6E7A4ull, JudgesCover, true, 0x10078},
        {"JudgesBack", 0x06099F32, 0x32, 0xBCC90E03807166B5ull, JudgesBack, true, 0x10078},
        {"JudgesCount", 0x06099F64, 0x80, 0x5CC6F13F2E58E305ull, JudgesCount, true, 0x10078},
        {"RoundEnd", 0x06099FE4, 0xB0, 0xC824B18CF9119CAEull, RoundEnd, true, 0x10079},
        {"ManageNextRound", 0x0609A13C, 0x46, 0x2FE905DDB90A10D4ull, ManageNextRound, true, 0x10079},
        {"NextRoundStart", 0x0609A182, 0x64, 0x79E33F43EF06EEEFull, NextRoundStart, true, 0x10078},
        {"NextRoundWait", 0x0609A1E6, 0x1E, 0xF785FDEECBB439DAull, NextRoundWait, true, 0x10078},
        {"NextRoundWinner", 0x0609A204, 0xA2, 0x3F0C3A3C50941DD7ull, NextRoundWinner, true, 0x10078},
        {"CombosShown", 0x0609A2A6, 0x40, 0x67BE59926BCECCAAull, CombosShown, true, 0x1003E},
        {"NextRoundWordsOver", 0x0609A2E6, 0x74, 0xEDCC5B4436EB3E01ull, NextRoundWordsOver, true, 0x10078},
        {"HitCheck", 0x0608C7C0, 0x38, 0x8F3BEAE5BEB5090Eull, HitCheck, true, 0},
    };
} // namespace

} // namespace fighting

void RegisterGameLifts(const Lift *lifts, size_t count); // lifts.cpp

void RegisterFightMatch() {
    RegisterGameLifts(fighting::kLifts, std::size(fighting::kLifts));
}

} // namespace cps3recomp
