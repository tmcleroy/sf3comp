// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
//
// macOS: the program in front hands the front to the launcher that started it. A Multi game run's game, ending for
// the next game, does it: macOS brings forward only a program started by the one in front, and the launcher, behind
// the game, starts the next one.
#import <AppKit/AppKit.h>
#include <unistd.h>

namespace port {

void HandFrontToLauncher() {
    @autoreleasepool {
        if (NSRunningApplication *launcher = [NSRunningApplication runningApplicationWithProcessIdentifier:getppid()]) {
            [launcher activateWithOptions:NSApplicationActivateAllWindows];
        }
    }
}

} // namespace port
