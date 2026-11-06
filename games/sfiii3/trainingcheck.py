#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# checktraining.sh's scripted play and judgements (training mode's play-test bugs and options of 2026-11-06).
#
#   trainingcheck.py movie SCRIPT FROM FRAMES FILE     an input movie (port/main.cpp's Movie: "frame p1 p2", hex): player 1
#                                                      picks at character select, then from frame FROM plays SCRIPT
#   trainingcheck.py features LIST                     the --list-features list: no training Stage viewer
#   trainingcheck.py JUDGEMENT LOG...                  judge a run's log (stderr: SFIII3_TRAINING_LOG's "tl" lines,
#                                                      SFIII3_PARRIES's "parry:" lines, CPS3RECOMP_HUD_LOG's "hud:" lines)
#
# Prints what it found and "ok" or "FAILED: ..."; exit 0 when ok.
import csv
import re
import sys

RIGHT, LEFT, DOWN, UP = 1, 2, 4, 8
LP, MP, HP, LK, MK, HK = 0x10, 0x20, 0x40, 0x80, 0x100, 0x200
# fight::Doing
FREE, STARTUP, ACTIVE, RECOVERY, HITSTUN, GUARDSTUN, AIR, HELD = range(8)


def fail(why):
    print("FAILED: " + why)
    sys.exit(1)


def ok(what):
    print(what + ": ok")
    sys.exit(0)


# --- the scripted play: player 1 on the left, facing right (forward is RIGHT) ---

def script(name):
    """(frames, held) steps for one turn of the script, repeated."""
    walk = [(10, RIGHT)]
    if name == 'pokes':  # a crouching light kick now and then, each alone (no combo)
        return walk + [(4, DOWN | LK), (100, 0)]
    if name == 'combos':  # light kicks alone, then two crouching light punches chained (a combo of two)
        return walk + [(4, DOWN | LK), (100, 0), (40, RIGHT), (3, DOWN | LP), (4, DOWN), (3, DOWN | LP), (100, 0)]
    if name == 'normals':  # grounded normals by turns, standing and crouching, each alone
        return [(14, RIGHT), (3, MK), (70, 0), (14, RIGHT), (3, DOWN | MK), (70, 0), (14, RIGHT), (3, HP), (70, 0),
                (14, RIGHT), (3, DOWN | LK), (70, 0), (14, RIGHT), (3, LP), (70, 0)]
    if name == 'throws':  # walked in close, then forward + LP + LK
        return [(50, RIGHT), (3, RIGHT | LP | LK), (110, 0)]
    if name == 'inputs':  # some presses with neutral between, and some with the stick
        return [(20, 0), (3, LP), (20, 0), (6, DOWN), (3, DOWN | LK), (25, 0), (10, RIGHT), (20, 0), (3, MK), (40, 0)]
    if name == 'idle':
        return [(100, 0)]
    raise SystemExit('no script ' + name)


def movie(name, start, frames, path):
    changes = {}
    # character select: light punch tapped (character, super art, colour), as player 1 chooses both sides
    for f in range(300, min(start - 60, 1000), 24):
        changes[f] = LP
        changes[f + 3] = 0
    steps = script(name)
    f, n = start, 0
    while f < frames:
        length, held = steps[n % len(steps)]
        changes[f] = held
        f += length
        n += 1
    with open(path, 'w') as out:
        out.write('# trainingcheck.py movie %s\n' % name)
        for at in sorted(changes):
            out.write('%d %04x 0000\n' % (at, changes[at]))


# --- the logs ---

TL = re.compile(r'^tl (\d+) (\d+) d (\d+) (\d+) held ([0-9a-f]+) ([0-9a-f]+) thrown (\d+) life (\d+) (\d+) stun (\d+) (\d+) (\d+) '
                r'x (-?\d+) (-?\d+) y (-?\d+) (-?\d+) anim ([0-9a-f]+) ([0-9a-f]+) play (-?\d+) win (\d)(\d)(\d)(\d) how (\d+) (\d+)')


def frames_of(log):
    out = []
    for line in open(log, errors='replace'):
        m = TL.match(line)
        if m:
            v = m.groups()
            out.append(dict(frame=int(v[0]), doing=(int(v[2]), int(v[3])), held=(int(v[4], 16), int(v[5], 16)),
                            thrown=int(v[6]), life=(int(v[7]), int(v[8])), stun=(int(v[9]), int(v[10]), int(v[11])),
                            x=(int(v[12]), int(v[13])), y=(int(v[14]), int(v[15])), anim=(int(v[16], 16), int(v[17], 16)),
                            play=int(v[18]), windows=tuple(int(c) for c in v[19:23]), routine=int(v[23]), how=int(v[24])))
    if len(out) < 300:
        fail('%d frames of a round logged (the session never got going?)' % len(out))
    return out


def hud_lines(log):
    out = []
    for line in open(log, errors='replace'):
        if line.startswith('hud: frame '):
            frame = int(line.split()[2].rstrip(':'))
            out.append((frame, re.findall(r'\[([^\]]*)\]', line)))
    return out


# --- judgements ---

def combo(log):
    """Hits that land alone (player 2 free between them) and pairs that combo (player 2 never free between): the readout
    counts a combo of two for each pair, and never more (it counted every hit of the session as one combo)."""
    frames = frames_of(log)
    hits = []  # (frame, player 2 free since the hit before)
    free = True
    for a, b in zip(frames, frames[1:]):
        if b['life'][1] < a['life'][1]:
            hits.append((b['frame'], free))
            free = False
        if b['doing'][1] == FREE:
            free = True
    pairs = sum(1 for _, alone in hits[1:] if not alone)
    counts = {}
    for _, lines in hud_lines(log):
        for text in lines:
            m = re.match(r'^(\d+) hits', text)
            if m:
                counts[int(m.group(1))] = counts.get(int(m.group(1)), 0) + 1
    print('combo: %d hits taken by player 2, %d of them following another before it was free; combos shown: %s' % (
        len(hits), pairs, ', '.join('%d hits %d times' % kv for kv in sorted(counts.items())) or 'none'))
    if len(hits) < 8 or pairs < 3:
        fail('too few hits, or too few combos, landed')
    if max(counts or [0]) > 2:
        fail('hits landing alone counted into a combo')
    if counts.get(2, 0) < pairs:
        fail('combos of two not shown')
    ok('combo')


def parries(log):
    out = []
    for line in open(log, errors='replace'):
        m = re.match(r'^parry: player (\d) (\S+(?:-\S+)?) (parried|wrong window|too early|too late)', line)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out


def parrykind(log):
    """Grounded normals parried: forward (or down for the low ones), never in the air or anti-air."""
    kinds = {}
    for player, kind, result in parries(log):
        if player == 2 and result == 'parried':
            kinds[kind] = kinds.get(kind, 0) + 1
    print('parry kind: player 2 parried %s' % (', '.join('%s %d' % kv for kv in sorted(kinds.items())) or 'nothing'))
    if sum(kinds.values()) < 8:
        fail('fewer than 8 parries')
    if kinds.get('anti-air') or kinds.get('air'):
        fail('grounded attacks read as parried in the air or anti-air')
    if not kinds.get('forward') or not kinds.get('down'):
        fail('standing and crouching attacks not told apart (forward and down)')
    ok('parry kind')


def attacks_and_parries(log):
    """Player 1's attacks that met player 2 (a hit, a guard or a parry), and those parried."""
    frames = frames_of(log)
    met = parried = 0
    for line in open(log, errors='replace'):
        if line.startswith('parry: player 2') and ' parried' in line:
            parried += 1
    for a, b in zip(frames, frames[1:]):
        if b['life'][1] < a['life'][1]:
            met += 1
    return met + parried, parried


def parryrate(low, high, *logs):
    """Each log a run at the rate named by its file (..._NN.log): parried / met in [low, high] percent of the rate."""
    low, high = float(low), float(high)
    bad = []
    for log in logs:
        rate = int(re.search(r'_(\d+)\.log$', log).group(1))
        met, parried = attacks_and_parries(log)
        got = 100.0 * parried / max(met, 1)
        print('parry %d%%: %d of %d attacks parried (%.0f%%)' % (rate, parried, met, got))
        if met < 30:
            bad.append('%d%%: only %d attacks' % (rate, met))
        elif rate == 0 and parried:
            bad.append('0%%: %d parried' % parried)
        elif rate and not (rate * low / 100 <= got <= min(100, rate * high / 100)):
            bad.append('%d%%: %.0f%% parried' % (rate, got))
    if bad:
        fail('; '.join(bad))
    ok('parry rate')


def throws(log):
    """Each throw of player 2: (escaped, thrown) counts. A throw is player 2 held (thrown byte set); escaped when it
    took no damage from it."""
    frames = frames_of(log)
    escaped = thrown = 0
    n = 0
    while n < len(frames):
        if frames[n]['thrown']:
            before = frames[max(0, n - 1)]['life'][1]
            m = n
            while m < len(frames) and (frames[m]['thrown'] or m - n < 2):
                m += 1
            after = min(f['life'][1] for f in frames[n:min(len(frames), m + 40)])
            if after < before:
                thrown += 1
            else:
                escaped += 1
            n = m + 1
        else:
            n += 1
    return escaped, thrown


def techrate(low, high, *logs):
    low, high = float(low), float(high)
    bad = []
    for log in logs:
        rate = int(re.search(r'_(\d+)\.log$', log).group(1))
        escaped, thrown = throws(log)
        total = escaped + thrown
        got = 100.0 * escaped / max(total, 1)
        print('throw escape %d%%: %d of %d throws escaped (%.0f%%)' % (rate, escaped, total, got))
        if total < 20:
            bad.append('%d%%: only %d throws' % (rate, total))
        elif rate == 0 and escaped:
            bad.append('0%%: %d escaped' % escaped)
        elif rate and not (rate * low / 100 <= got <= min(100, rate * high / 100)):
            bad.append('%d%%: %.0f%% escaped' % (rate, got))
    if bad:
        fail('; '.join(bad))
    ok('throw escape rate')


def stunmash(log):
    """Player 2 mashes only while stunned: nothing held from the frame its stun is over, and no move of its own."""
    frames = frames_of(log)
    stuns = held_after = moves = 0
    stunned = False
    for f in frames:
        # (stunned: the record's reaction the stun's, routine_no[1] 1 and [2] 25: the stun timer is left at 254 after
        # some stuns, mashed below 0)
        now = f['routine'] == 1 and f['how'] == 25
        if now and not stunned:
            stuns += 1
        stunned = now
        if not stunned and f['held'][1]:
            held_after += 1
        if stuns and f['doing'][1] in (STARTUP, ACTIVE, RECOVERY):  # (from the first stun: the round's start reads as one)
            moves += 1
    print('stun mash: %d stuns; %d frames of controls held by player 2 while not stunned; %d frames of player 2 in a '
          'move' % (stuns, held_after, moves))
    if stuns < 3:
        fail('only %d stuns' % stuns)
    if held_after or moves:
        fail('player 2 still mashing, or attacking, after a stun')
    ok('stun mash')


def level(low_log, high_log):
    """The computer at its lowest level and its highest: its play time put where the level is, and it plays measurably
    better at the highest (more of player 1's health taken)."""
    result = []
    for log in (low_log, high_log):
        frames = frames_of(log)
        taken = sum(max(0, a['life'][0] - b['life'][0]) for a, b in zip(frames, frames[1:]))
        attacks = sum(1 for a, b in zip(frames, frames[1:]) if b['doing'][1] == STARTUP and a['doing'][1] != STARTUP)
        plays = sorted(set(f['play'] for f in frames[100:]))
        result.append((taken, attacks, plays))
        print('%s: player 1 lost %d health to the computer; %d moves begun; its play time %s' % (
            log.rsplit('/', 1)[-1], taken, attacks, plays[:3]))
    (low_taken, low_attacks, low_plays), (high_taken, high_attacks, high_plays) = result
    # (held each frame before the frame runs: the game's own second's tick may put one on in a frame)
    if low_plays[-1] - low_plays[0] > 1 or high_plays[-1] - high_plays[0] > 1 or low_plays[-1] >= high_plays[0]:
        fail('the play time not held where the levels put it')
    if high_taken < low_taken * 1.5 + 20:
        fail('the highest level took %d health, the lowest %d: no measurable difference' % (high_taken, low_taken))
    ok('computer level')


def inputs(log):
    """The input display shows nothing for neutral: no line of only "5" (with its frames)."""
    neutral, lines = [], 0
    for frame, texts in hud_lines(log):
        for text in texts:
            if re.match(r'^\s*\d+  ', text) or re.match(r'^.*  +\d+$', text):
                lines += 1
                what = re.sub(r'^\s*\d+  |  +\d+$', '', text).strip()
                if what == '5' or what.startswith('5 '):
                    neutral.append((frame, text))
    print('input display: %d lines seen, %d with a 5 for neutral' % (lines, len(neutral)))
    if lines < 10:
        fail('the input display not seen')
    if neutral:
        fail('neutral shown as 5: %r at frame %d' % (neutral[0][1], neutral[0][0]))
    ok('input display')


def framelog(log, path, *expect):
    """The frame data log: a line for every move made (the moves player 1 made, by the training log's player 1
    leaving Free for a move), the same numbers for the same move made the same way, and a note on screen as it
    writes."""
    frames = frames_of(log)
    made = sum(1 for a, b in zip(frames, frames[1:]) if a['doing'][0] == FREE and b['doing'][0] in (STARTUP, ACTIVE))
    rows = list(csv.DictReader(open(path))) if path else []
    rows = [r for r in rows if r['character']]
    by = {}
    for r in rows:
        by.setdefault((r['character'], r['input'], r['air'], r['move']), set()).add((r['startup'], r['active']))
    mixed = {k: v for k, v in by.items() if len(v) > 1}
    notes = [t for _, texts in hud_lines(log) for t in texts if 'frame data saved to' in t]
    print('frame log: %d moves made by player 1, %d lines written; %d moves with differing numbers; note shown %d '
          'times' % (made, len(rows), len(mixed), len(notes)))
    for k, v in list(mixed.items())[:3]:
        print('  %s: %s' % (','.join(k), sorted(v)))
    if made < 20:
        fail('only %d moves made' % made)
    if len(rows) < made * 0.9:
        fail('%d of %d moves written' % (len(rows), made))
    if mixed:
        fail('the same move written with different startup or active')
    if not notes:
        fail('no note on screen as the file was written')
    ok('frame log')


def features(path):
    """The training page has no Stage viewer of its own (the Alternative modes' stage viewer is the mode, and is kept)."""
    names = {line.split('\t')[0]: line.split('\t')[1] if '\t' in line else '' for line in open(path)}
    print('features: %d listed; stage_viewer %s; the mode\'s viewer_stage %s' % (
        len(names), names.get('stage_viewer', 'gone'), 'there' if 'viewer_stage' in names else 'gone'))
    if 'stage_viewer' in names:
        fail('the training page still has its Stage viewer')
    if 'viewer_stage' not in names:
        fail('the stage viewer mode\'s choices are gone')
    ok('features')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    what, args = sys.argv[1], sys.argv[2:]
    if what == 'movie':
        movie(args[0], int(args[1]), int(args[2]), args[3])
    else:
        globals()[what](*args)
