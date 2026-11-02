// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// 3rd Strike's training aids, on the toolkit's fight/ parts: health and meter kept up, hit boxes drawn through the
// game's camera, an input display. (A first set: frame data, the dummy and the rest need the fighters' states read
// with more care, and are next.)
#include "fight.h"
#include "game.h"
#include "sfiii3.h"
#include "state.h"
#include "system.h"

namespace cps3recomp {

namespace {
    struct State {
        int idle[2] = {};
        fight::InputHistory inputs;
    } g;

    int16_t Signed16(uint32_t address) {
        return static_cast<int16_t>(Ram16(address));
    }
    bool InMatch() {
        return (Ram8(sfiii3::kLocked[0]) == 0xFF || Ram8(sfiii3::kLocked[1]) == 0xFF) && Ram8(sfiii3::kMatchState) == 2;
    }
    // Free to act: not attacking, guarding, thrown, busy or frozen, and taking input
    bool Idle(int p) {
        const uint32_t base = sfiii3::kPlayer[p];
        return Ram8(base + sfiii3::kAttacking) == 0 && !(Ram8(base + sfiii3::kBlocking) > 0 && Ram8(base + sfiii3::kBlocking) < 5) &&
               Ram8(base + sfiii3::kThrown) == 0 && (Ram16(base + sfiii3::kBusy) & 0xFF) == 0 && Ram8(base + sfiii3::kFreeze) == 0 &&
               Ram16(base + sfiii3::kInputCapacity) > 0;
    }

    void Boxes() {
        std::vector<HudBox> boxes;
        const int cameraX = Signed16(sfiii3::kCameraX), cameraY = Signed16(sfiii3::kCameraY);
        for (int p = 0; p < 2; p++) {
            const uint32_t base = sfiii3::kPlayer[p];
            const int px = Signed16(base + sfiii3::kX) - cameraX + Video::kWidth / 2;
            const int py = Video::kHeight - (Signed16(base + sfiii3::kY) - cameraY) - 23;
            const bool flipped = Ram8(base + sfiii3::kFacing) != 0;
            for (const sfiii3::BoxList &list : sfiii3::kBoxes) {
                const uint32_t at = Ram32(base + list.offset);
                if ((at & 0xFFF80000u) != 0x02000000u) {
                    continue; // (no list, or one outside main RAM)
                }
                for (int n = 0; n < list.count; n++) {
                    const uint32_t box = at + n * 8;
                    const int left = Signed16(box), width = Signed16(box + 2), bottom = Signed16(box + 4), height = Signed16(box + 6);
                    if (!left && !width && !bottom && !height) {
                        continue;
                    }
                    const int x = flipped ? px - left - width : px + left;
                    static constexpr uint8_t kColour[5][3] = {{60, 120, 255}, {255, 40, 40}, {255, 60, 255}, {255, 230, 40}, {60, 230, 60}};
                    const uint8_t *c = kColour[list.kind];
                    boxes.push_back({x, py - bottom - height, width, height, c[0], c[1], c[2]});
                }
            }
        }
        SetBoxes(boxes);
    }

    void BeforeFrame(Machine &m) {
        if (!InMatch()) {
            return;
        }
        for (int p = 0; p < 2; p++) {
            const uint32_t base = sfiii3::kPlayer[p];
            g.idle[p] = Idle(p) ? g.idle[p] + 1 : 0;
            if (FeatureOn("refill_health") && g.idle[p] > 30 && Ram8(base + sfiii3::kLife) < sfiii3::kFullLife) {
                SetRam8(base + sfiii3::kLife, static_cast<uint8_t>(std::min(Ram8(base + sfiii3::kLife) + 6, sfiii3::kFullLife)));
            }
            const uint8_t most = Ram8(sfiii3::kMaxMeterCount[p]);
            if (FeatureOn(p == 0 ? "full_meter" : "full_meter_2") && most && Ram8(sfiii3::kMeterCount[p]) != most &&
                Ram8(sfiii3::kMeterCountShown[p]) != most) {
                SetRam8(sfiii3::kGauge[p], 0);
                SetRam8(sfiii3::kMeterCount[p], most);
                SetRam8(sfiii3::kMeterUpdate[p], 1);
            }
        }
    }

    void AfterFrame(Machine &m) {
        std::vector<HudLine> lines;
        const bool match = InMatch();
        if (match && FeatureOn("input_display")) {
            for (int p = 0; p < 2; p++) {
                g.inputs.Push(p, m.players[p], Ram8(sfiii3::kPlayer[p] + sfiii3::kFacing) == 0);
                g.inputs.Hud(p, lines);
            }
        }
        SetHud(lines);
        if (match && FeatureOn("hit_boxes")) {
            Boxes();
        } else {
            SetBoxes({});
        }
    }
} // namespace

void RegisterTraining(Game &game) {
    game.features.push_back({"refill_health", "Training", "Refill health", "Health comes back to full a moment after a combo ends."});
    game.features.push_back({"full_meter", "Training", "Full meter", "Player 1's super art meter is always full."});
    game.features.push_back({"full_meter_2", "Training", "Full meter, player 2", "Player 2's super art meter is always full."});
    game.features.push_back({"hit_boxes", "Training", "Hit boxes",
                             "Each fighter's boxes over the picture: blue where it can be hit, red where it hits, yellow its throw, green "
                             "where it can be thrown, pink the body that pushes."});
    game.features.push_back({"input_display", "Training", "Input display", "What each player holds, newest first, in numpad notation."});
    game.state = [](Archive &ar) {
        ar.Pod(g.idle);
        ar.Pod(g.inputs);
    };
}

void TrainingBeforeFrame(Machine &m) {
    BeforeFrame(m);
}
void TrainingAfterFrame(Machine &m) {
    AfterFrame(m);
}

} // namespace cps3recomp
