#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
"""The knock-out's check (checkko.sh makes its runs): its sound and its slow motion, by default and on the board model,
and as the window's frontend plays it to a sound device's clock.

  checkko.py DIR

DIR holds checkko.sh's runs: native.wav, board.wav (a one-player game to past its first knock-out, by default and with
--board), native.slow, board.slow (main RAM 0x0200EEC0-0x0200EEDF after every frame: the slow motion's bytes), from.wav and
from.slow (the same game from a state saved before the knock-out) and heard-RATE.raw (the frontend from that state,
its sound taken by CPS3RECOMP_AUDIO_SINK at RATE). Prints what it measured and "checkko: all the same", or what is wrong.
Exit 0 when all is well.

The slow motion is the game's own (3sx's names, slowf.c): at a knock-out SLOW_timer (0x0200EED0, a word) is set to 95; the
knock-out's message holds the game (Game_pause) for Time_Data[1], 90 frames, the timer with it; then each frame it counts
down, SLOW_flag (0x0200EECE) is slow_timer_to_flag[timer / 8] (3 down to 64, 2 down to 48, then 1) and EXE_flag (0x0200EECC)
is the frame's place in each SLOW_flag + 1 (the game's work done on its 0s: a quarter, a third, then half speed). The arcade
counts those by its vertical blanks, 59.6 a second, and its sound driver runs at every one whatever the game's speed: so the
sound does not slow with the picture. The checks: native and the board model the same (the sound byte for byte, the slow
motion's bytes every frame); the slow motion as the rules say; the knock-out's sound not clipped and in stereo (its boom
is a pair of voices, one a side: with the board's JAMMA sound both play on both sides, twice as loud, and the mix clips);
and the frontend's sound at the game's pace through the hold and the slow motion, at either rate (no drift between what
it plays and the game's own sound), as loud as the game's.
"""
import array
import math
import os
import sys

CHIP = 42954545.0 / 3 / 384        # the sound chip's rate
FPS = 42954545.0 / 6 / 455 / 264   # the board's frames a second
PER = CHIP / FPS
TABLE = [1, 1, 1, 1, 1, 1, 2, 2] + [3] * 24  # slow_timer_to_flag
HOLD = 90 + 1     # Time_Data[1], and the frame the message starts on
SLOW = 95 - 2     # the frames counted down after it (94 to 1)
KO_AFTER = 60     # frames of the knock-out's sound looked at
WINDOW = 0.010    # seconds an envelope step
DRIFT = 0.040     # seconds the heard sound may move against the game's over the knock-out
LOUDNESS = 1.0    # dB
FOUND = 0.6       # the envelopes' correlation that counts as the place found (about 0.8 at the knock-out, 0.9-1 elsewhere)


def wav(path, raw=False):
    a = array.array('h')
    with open(path, 'rb') as f:
        d = f.read()
    if not raw:
        d = d[44:]
    a.frombytes(d[:len(d) // 4 * 4])
    return a


def slow_rows(path):
    with open(path, 'rb') as f:
        d = f.read()
    rows = []
    for i in range(0, len(d) - 31, 32):
        b = d[i:i + 32]
        rows.append((b[0xC] << 8 | b[0xD], b[0xE] << 8 | b[0xF], b[0x10] << 8 | b[0x11]))  # EXE_flag, SLOW_flag, SLOW_timer
    return rows


def knock_out(rows):
    """The first knock-out's frame (the timer set to 95), the hold's length, the countdown's, and what is wrong"""
    k = next((i for i, r in enumerate(rows) if r[2] == 95), None)
    if k is None:
        return None, 0, 0, ['no knock-out (the slow motion timer never set)']
    wrong = []
    i = k + 1
    held = rows[i][2] if i < len(rows) else 0
    hold = 0
    while i < len(rows) and rows[i][2] == held:
        hold += 1
        i += 1
    count = 0
    while i < len(rows) and rows[i][2] != 0:
        t, flag, exe = rows[i][2], rows[i][1], rows[i][0]
        if t != rows[i - 1][2] - 1:
            wrong.append('frame %d: the timer %d after %d (one a frame)' % (i, t, rows[i - 1][2]))
            break
        if flag != TABLE[min(31, t // 8)]:
            wrong.append('frame %d: SLOW_flag %d with the timer at %d (the table: %d)' % (i, flag, t, TABLE[min(31, t // 8)]))
            break
        if exe > flag:
            wrong.append('frame %d: EXE_flag %d past SLOW_flag %d' % (i, exe, flag))
            break
        count += 1
        i += 1
    if hold != HOLD:
        wrong.append('the knock-out held the game %d frames (the game: %d)' % (hold, HOLD))
    if count != SLOW:
        wrong.append('the slow motion %d frames (the game: %d)' % (count, SLOW))
    return k, hold, count, wrong


def envelope(a, rate):
    n = int(rate * WINDOW)
    out = []
    for i in range(0, len(a) // 2 - n, n):
        s = 0
        for j in range(i * 2, (i + n) * 2, 8):
            s += abs(a[j]) + abs(a[j + 1])
        out.append(s / n)
    return out


def lag_at(ref, heard, t, span=0.5, search=2.0):
    """Where `heard` best matches `ref`'s envelope from `t` seconds on: its lag in seconds and the correlation"""
    i0 = int(t / WINDOW)
    n = int(span / WINDOW)
    a = ref[i0:i0 + n]
    if len(a) < n:
        return None, 0
    ma = sum(a) / n
    da = math.sqrt(sum((x - ma) ** 2 for x in a)) or 1
    best = (None, -2)
    for lag in range(-int(search / WINDOW), int(search / WINDOW)):
        j = i0 + lag
        if j < 0 or j + n > len(heard):
            continue
        b = heard[j:j + n]
        mb = sum(b) / n
        db = math.sqrt(sum((y - mb) ** 2 for y in b)) or 1
        c = sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (da * db)
        if c > best[1]:
            best = (lag * WINDOW, c)
    return best


def rms(a, first, last):
    seg = a[first * 2:last * 2]
    return math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))


def main():
    d = sys.argv[1]
    problems = []

    # Native against the board model
    native, board = open(os.path.join(d, 'native.wav'), 'rb').read(), open(os.path.join(d, 'board.wav'), 'rb').read()
    if native == board:
        print('sound: native and the board model the same (%d samples)' % ((len(native) - 44) // 4))
    else:
        at = next((i for i in range(min(len(native), len(board))) if native[i] != board[i]), min(len(native), len(board)))
        problems.append('sound: native and the board model DIFFERENT from sample %d (frame %d)' % ((at - 44) // 4, int((at - 44) // 4 / PER)))
    rows, board_rows = slow_rows(os.path.join(d, 'native.slow')), slow_rows(os.path.join(d, 'board.slow'))
    if rows == board_rows:
        print('slow motion: native and the board model the same (%d frames)' % len(rows))
    else:
        at = next((i for i in range(min(len(rows), len(board_rows))) if rows[i] != board_rows[i]), 0)
        problems.append('slow motion: native and the board model DIFFERENT at frame %d' % at)

    # The slow motion against the game's rules
    k, hold, count, wrong = knock_out(rows)
    problems += wrong
    if k is not None:
        print('knock-out at frame %d: the game held %d frames, then %d frames of slow motion (%.2f s in all at %.4f frames a second)'
              % (k, hold, count, (1 + hold + count) / FPS, FPS))

        # The knock-out's sound
        s = wav(os.path.join(d, 'native.wav'))
        first, last = int(k * PER), int((k + KO_AFTER) * PER)
        seg = s[first * 2:last * 2]
        clipped = sum(1 for x in seg if x >= 32767 or x <= -32768)
        pairs = len(seg) // 2
        same = sum(1 for i in range(pairs) if seg[2 * i] == seg[2 * i + 1])
        before = rms(s, int((k - 120) * PER), first)
        print('knock-out sound (%d frames): %d samples clipped, left and right the same in %.0f%% of them, %.1f dB over the two seconds before'
              % (KO_AFTER, clipped, 100.0 * same / max(1, pairs), 20 * math.log10(max(1.0, rms(s, first, last)) / max(1.0, before))))
        if clipped:
            problems.append('knock-out sound: %d samples clipped' % clipped)
        if same * 2 > pairs:
            problems.append('knock-out sound: not stereo (left and right the same in %d of %d samples: the board\'s JAMMA sound?)' % (same, pairs))

    # What the frontend plays, against the game's own sound from the same state
    from_rows = slow_rows(os.path.join(d, 'from.slow'))
    fk, _, _, wrong = knock_out(from_rows)
    if fk is None:
        problems.append('the run from the state: ' + '; '.join(wrong))
        fk = 0
    ref = wav(os.path.join(d, 'from.wav'))
    eref = envelope(ref, CHIP)
    for name in sorted(n for n in os.listdir(d) if n.startswith('heard-') and n.endswith('.raw')):
        rate = int(name[6:-4])
        heard = wav(os.path.join(d, name), raw=True)
        eh = envelope(heard, rate)
        points = [fk - 120, fk, fk + 45, fk + 100, fk + 150, fk + 200, fk + 260]
        lags = []
        for f in points:
            lag, c = lag_at(eref, eh, f / FPS)
            if lag is None or c < FOUND:
                problems.append('%s: frame %d of the game not found in what was heard (correlation %.2f)' % (name, f, c))
                continue
            lags.append(lag)
        if lags:
            drift = max(lags) - min(lags)
            # (its loudness over the knock-out, lined up by the lag found there)
            lag = lags[1] if len(lags) > 1 else lags[0]
            h0 = int((fk / FPS + lag) * rate)
            loud = 20 * math.log10(max(1.0, rms(heard, h0, h0 + int(KO_AFTER / FPS * rate))) / max(1.0, rms(ref, int(fk * PER), int((fk + KO_AFTER) * PER))))
            print('%s: the game\'s sound heard %.3f s late, moving %.0f ms over the knock-out (%d places), %.2f dB against the game\'s'
                  % (name, sum(lags) / len(lags), drift * 1000, len(lags), loud))
            if drift > DRIFT:
                problems.append('%s: what is heard drifts %.0f ms against the game over the knock-out (at most %.0f): it does not keep the game\'s pace'
                                % (name, drift * 1000, DRIFT * 1000))
            if abs(loud) > LOUDNESS:
                problems.append('%s: %.2f dB louder than the game\'s own sound' % (name, loud))
    for p in problems:
        print(p)
    print('checkko: all the same' if not problems else 'checkko: DIFFERENT')
    return 0 if not problems else 1


if __name__ == '__main__':
    sys.exit(main())
