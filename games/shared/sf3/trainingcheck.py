#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# checktraining.sh's scripted play and judgements, for the games built from games/shared/sf3 (New Generation and
# 2nd Impact). The same kind of check as 3rd Strike's (games/sfiii3/trainingcheck.py).
#
#   trainingcheck.py movie SCRIPT FROM FRAMES FILE     an input movie (port/main.cpp's Movie: "frame p1 p2", hex): player 1
#                                                      picks at character select, then from frame FROM plays SCRIPT
#   trainingcheck.py JUDGEMENT LOG...                  judge a run's log (stderr: SF3_TRAINING_LOG's "tl" lines,
#                                                      SF3_PARRIES's "parry:" lines, SF3_MOVES's "move" lines,
#                                                      CPS3RECOMP_HUD_LOG's "hud:" lines)
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
    if name == 'combos':  # light kicks alone, then two crouching light kicks 16 frames apart (a combo of two in both games)
        return walk + [(4, DOWN | LK), (100, 0), (40, RIGHT), (3, DOWN | LK), (13, DOWN), (3, DOWN | LK), (100, 0)]
    if name == 'normals':  # grounded normals by turns, standing and crouching, each alone
        return [(14, RIGHT), (3, MK), (70, 0), (14, RIGHT), (3, DOWN | MK), (70, 0), (14, RIGHT), (3, HP), (70, 0),
                (14, RIGHT), (3, DOWN | LK), (70, 0), (14, RIGHT), (3, LP), (70, 0)]
    if name == 'heavies':  # the hardest hits there are without a special move, one after another
        return [(14, RIGHT), (3, HP), (40, 0), (14, RIGHT), (3, DOWN | HK), (90, 0)]
    if name == 'throws':  # walked in close, then forward and heavy punch (these games' throw)
        return [(50, RIGHT), (3, RIGHT | HP), (110, 0)]
    if name == 'sweeps':  # walked in, then a crouching heavy kick (it knocks down)
        return [(30, RIGHT), (3, DOWN | HK), (150, 0)]
    if name == 'jumps':  # a jump forward with a heavy kick on the way down, then a walk back out
        return [(10, RIGHT), (4, UP | RIGHT), (24, 0), (3, HK), (50, 0), (60, LEFT)]
    if name == 'fireballs':  # down, down-forward, forward and punch, from where the round began
        return [(3, DOWN), (3, DOWN | RIGHT), (3, RIGHT), (3, RIGHT | HP), (130, 0)]
    if name == 'supers':  # two quarter circles and a punch (the super art), then a wait
        return [(3, DOWN), (3, DOWN | RIGHT), (3, RIGHT), (3, DOWN), (3, DOWN | RIGHT), (3, RIGHT), (3, RIGHT | HP), (240, 0)]
    if name == 'inputs':  # some presses with neutral between, and some with the stick
        return [(20, 0), (3, LP), (20, 0), (6, DOWN), (3, DOWN | LK), (25, 0), (10, RIGHT), (20, 0), (3, MK), (40, 0)]
    if name == 'light':  # a standing light punch out of reach, alone
        return [(3, LP), (60, 0)]
    if name == 'back':  # walking away (to a corner)
        return [(100, LEFT)]
    if name == 'idle':
        return [(100, 0)]
    raise SystemExit('no script ' + name)


def movie(name, start, frames, path):
    changes = {}
    # character select: light punch tapped (character, super art), as player 1 chooses both sides
    for f in range(300, min(start - 60, 1100), 24):
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
                r'x (-?\d+) (-?\d+) y (-?\d+) (-?\d+) anim ([0-9a-f]+) ([0-9a-f]+) win (\d)(\d)(\d)(\d) how (\d+) (\d+) '
                r'clock (\d+) gauge (\d+) (\d+) stage (\d+)')


def frames_of(log, least=300):
    out = []
    for line in open(log, errors='replace'):
        m = TL.match(line)
        if m:
            v = m.groups()
            out.append(dict(frame=int(v[0]), doing=(int(v[2]), int(v[3])), held=(int(v[4], 16), int(v[5], 16)),
                            thrown=int(v[6]), life=(int(v[7]), int(v[8])), stun=(int(v[9]), int(v[10]), int(v[11])),
                            x=(int(v[12]), int(v[13])), y=(int(v[14]), int(v[15])), anim=(int(v[16], 16), int(v[17], 16)),
                            windows=tuple(int(c) for c in v[18:22]), routine=int(v[22]), how=int(v[23]), clock=int(v[24]),
                            gauge=(int(v[25]), int(v[26])), stage=int(v[27])))
    if len(out) < least:
        fail('%d frames of a round logged (the session never got going?)' % len(out))
    return out


def hud_lines(log):
    out = []
    for line in open(log, errors='replace'):
        if line.startswith('hud: frame '):
            frame = int(line.split()[2].rstrip(':'))
            out.append((frame, re.findall(r'\[([^\]]*)\]', line)))
    return out


def hits_on(frames, player):
    return [(b['frame'], a['life'][player] - b['life'][player]) for a, b in zip(frames, frames[1:]) if b['life'][player] < a['life'][player]]


# --- judgements ---

def session(log):
    """A session got to a round, which went on to the run's end: the clock at 99 throughout, nobody's health at 0,
    player 2 (hit hard again and again) with health coming back."""
    frames = frames_of(log)
    clocks = sorted(set(f['clock'] for f in frames))
    lowest = min(f['life'][1] for f in frames)
    taken = sum(d for _, d in hits_on(frames, 1))
    refills = sum(1 for a, b in zip(frames, frames[1:]) if b['life'][1] > a['life'][1])
    gaps = sum(1 for a, b in zip(frames, frames[1:]) if b['frame'] != a['frame'] + 1)
    print('session: a round of %d frames (from frame %d, %d breaks); the clock read %s; player 2 took %d health in all, its lowest %d, '
          'its health went up %d times' % (len(frames), frames[0]['frame'], gaps, clocks, taken, lowest, refills))
    if clocks != [99]:
        fail('the clock moved')
    if taken < 400:
        fail('player 2 took only %d health: no test of a knock-out' % taken)
    if lowest == 0 or gaps:
        fail('a knock-out, or the round ended')
    if not refills:
        fail('health never came back')
    ok('session')


def refill(log):
    """Health refills: after each hit on player 2 left alone, its health is back at the full 160 within 150 frames."""
    frames = frames_of(log)
    hits = hits_on(frames, 1)
    by = {f['frame']: f for f in frames}
    back = 0
    for at, _ in hits:
        if any(by.get(at + n, {'life': (0, 0)})['life'][1] == 160 for n in range(60, 200)):
            back += 1
    print('health refill: %d hits on player 2, full again after %d of them' % (len(hits), back))
    if len(hits) < 8:
        fail('too few hits')
    if back < len(hits) - 1:
        fail('health not back to full after a hit')
    ok('health refill')


def combo(log):
    """Hits that land alone (player 2 free between them) and pairs that combo (player 2 never free between): the readout
    counts a combo of two for each pair, and never more."""
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
    singles = set()
    for _, lines in hud_lines(log):
        for text in lines:
            m = re.match(r'^(\d+) hits', text)
            if m:
                counts[int(m.group(1))] = counts.get(int(m.group(1)), 0) + 1
            elif re.match(r'^\d+$', text):
                singles.add(int(text))
    print('combo: %d hits taken by player 2, %d of them following another before it was free; combos shown: %s; single hits shown: %s' % (
        len(hits), pairs, ', '.join('%d hits %d times' % kv for kv in sorted(counts.items())) or 'none', sorted(singles)))
    if len(hits) < 8 or pairs < 3:
        fail('too few hits, or too few combos, landed')
    if max(counts or [0]) > 2:
        fail('hits landing alone counted into a combo')
    if counts.get(2, 0) < pairs:
        fail('combos of two not shown')
    damages = set(d for _, d in hits_on(frames, 1))
    if not singles or not singles <= damages:
        fail('a single hit shown with a damage the health did not lose: shown %s, lost %s' % (sorted(singles), sorted(damages)))
    ok('combo')


def guard(log_always, log_off):
    """The dummy's guard: told to guard always it guards player 1's normals (its health goes down by chip or not at all,
    it is seen guarding); left alone it is hit by the same play."""
    out = []
    for log in (log_always, log_off):
        frames = frames_of(log)
        guards = sum(1 for a, b in zip(frames, frames[1:]) if b['doing'][1] == GUARDSTUN and a['doing'][1] != GUARDSTUN)
        hit = sum(1 for a, b in zip(frames, frames[1:]) if b['doing'][1] == HITSTUN and a['doing'][1] != HITSTUN)
        out.append((guards, hit))
    print('guard: told to guard always, %d guarded and %d hit; not told, %d guarded and %d hit' % (out[0] + out[1]))
    if out[0][0] < 15 or out[0][1] > out[0][0] * 0.2:
        fail('the dummy did not guard')
    if out[1][0] or out[1][1] < 15:
        fail('the dummy left alone was not hit')
    ok('guard')


def stance(log_crouch, log_jump, log_stand):
    """The dummy crouches (down held for it all through), jumps (it leaves the floor again and again), stands."""
    crouch, jump, stand = frames_of(log_crouch), frames_of(log_jump), frames_of(log_stand)
    held = sum(1 for f in crouch[60:] if f['held'][1] & DOWN)
    jumps = sum(1 for a, b in zip(jump, jump[1:]) if a['y'][1] <= 0 < b['y'][1])
    high = max(f['y'][1] for f in stand)
    crouchAnim = len(set(f['anim'][1] for f in crouch[200:])), len(set(f['anim'][1] for f in stand[200:]))
    differs = set(f['anim'][1] for f in crouch[200:]) != set(f['anim'][1] for f in stand[200:])
    print('stance: crouching, down held %d of %d frames (its animations not the standing ones: %s); jumping, it left the floor %d '
          'times; standing, its highest %d' % (held, len(crouch) - 60, differs, jumps, high))
    if held < len(crouch) - 70 or not differs:
        fail('not crouching')
    if jumps < 5:
        fail('not jumping')
    if high > 0:
        fail('the standing dummy left the floor')
    ok('stance')


def record(log):
    """Record and replay: player 1's controls worked player 2 while recording (player 2's moves seen then, player 1 still),
    and replaying, player 2 makes those moves again by itself, over and over."""
    frames = frames_of(log)
    notes = [t for _, texts in hud_lines(log) for t in texts if t.startswith('REC ') or t.startswith('PLAY ')]
    rec = sum(1 for t in notes if t.startswith('REC '))
    play = sum(1 for t in notes if t.startswith('PLAY '))
    moves = [b['frame'] for a, b in zip(frames, frames[1:]) if a['doing'][1] == FREE and b['doing'][1] in (STARTUP, ACTIVE)]
    own = sum(1 for a, b in zip(frames, frames[1:]) if a['doing'][0] == FREE and b['doing'][0] in (STARTUP, ACTIVE))
    print('record: %d moves made by player 2, %d by player 1; REC shown %d times, PLAY %d times' % (len(moves), own, rec, play))
    if not rec or not play:
        fail('recording or replaying not shown')
    if len(moves) < 8:
        fail('player 2 made too few moves')
    ok('record and replay')


def inputs(log):
    """The input display shows what is pressed, and nothing for neutral: no line of only "5" (with its frames)."""
    neutral, lines, seen = [], 0, set()
    for frame, texts in hud_lines(log):
        for text in texts:
            if re.match(r'^\s*\d+  \S', text):
                lines += 1
                what = re.sub(r'^\s*\d+  ', '', text).strip()
                seen.add(what)
                if what == '5' or what.startswith('5 '):
                    neutral.append((frame, text))
    print('input display: %d lines seen (%s), %d with a 5 for neutral' % (lines, ', '.join(sorted(seen)), len(neutral)))
    if lines < 10:
        fail('the input display not seen')
    if not {'LP', '2', '2 LK', '6', 'MK'} <= seen:
        fail('a press of the script not shown')
    if neutral:
        fail('neutral shown as 5: %r at frame %d' % (neutral[0][1], neutral[0][0]))
    ok('input display')


def stun(log):
    """The stun readout: it goes up with the hits on player 2 and drains when it is left alone; "taken" never above
    "the most"."""
    values = []
    for _, texts in hud_lines(log):
        stuns = [t for t in texts if t.startswith('STUN ')]
        if len(stuns) == 2:
            m = re.match(r'STUN (\d+)/(\d+)', stuns[1])
            values.append((int(m.group(1)), int(m.group(2))))
    ups = sum(1 for a, b in zip(values, values[1:]) if b[0] > a[0])
    downs = sum(1 for a, b in zip(values, values[1:]) if b[0] < a[0])
    most = sorted(set(v[1] for v in values))
    peak = max((v[0] for v in values), default=0)
    print('stun readout: player 2\'s went up %d times and down %d times, its peak %d of %s' % (ups, downs, peak, most))
    if ups < 5 or downs < 5:
        fail('the stun shown did not follow the hits')
    if peak > max(most):
        fail('more stun shown than stuns')
    ok('stun readout')


def framelog(log, path):
    """The frame data log: a line for every move made, the same numbers for the same move made the same way (and meeting
    the other or missing), and a note on screen as it writes."""
    frames = frames_of(log)
    made = sum(1 for a, b in zip(frames, frames[1:]) if a['doing'][0] == FREE and b['doing'][0] in (STARTUP, ACTIVE))
    rows = [r for r in csv.DictReader(open(path)) if r['character']]
    by = {}
    for r in rows:
        # (met or not, apart: an animation frame can say that a hit or a guard moves the animation on at once, so a move
        # that meets the other can be shorter than the same move missing)
        met = 'met' if r.get('on') or r.get('advantage') else 'missed'
        by.setdefault((r['character'], r['input'], r['air'], r['move'], met), set()).add((r['startup'], r['active'], r['recovery']))
    mixed = {k: v for k, v in by.items() if len(v) > 1}
    notes = [t for _, texts in hud_lines(log) for t in texts if 'frame data saved to' in t]
    print('frame log: %d moves made by player 1, %d lines written; %d moves with differing numbers; note shown %d times' % (
        made, len(rows), len(mixed), len(notes)))
    for k, v in sorted(by.items()):
        print('  %s: %s' % (' '.join(k), ' or '.join('/'.join(n) for n in sorted(v))))
    if made < 20:
        fail('only %d moves made' % made)
    if len(rows) < made * 0.9:
        fail('%d of %d moves written' % (len(rows), made))
    if mixed:
        fail('the same move written with different numbers')
    if not notes:
        fail('no note on screen as the file was written')
    ok('frame log')


def moves_of(log):
    out = []
    for line in open(log, errors='replace'):
        m = re.match(r'^move (\d) (\d+) ([0-9a-f]+) (\d+) (\d+) (\d+)', line)
        if m:
            out.append(tuple([int(m.group(1)), int(m.group(2)), m.group(3)] + [int(v) for v in m.groups()[3:]]))
    return out


def framedata(log):
    """A standing light punch out of reach, again and again: the same startup, active and recovery every time, and
    their sum (startup counts the first frame it can hit on: startup - 1 + active + recovery) the frames player 1 is seen
    not free for (by the training log's doings: from leaving Free to Free again)."""
    frames = frames_of(log)
    moves = [m for m in moves_of(log) if m[0] == 0]
    numbers = sorted(set(m[3:] for m in moves))
    spans = []
    start = None
    for a, b in zip(frames, frames[1:]):
        if a['doing'][0] == FREE and b['doing'][0] != FREE:
            start = b['frame']
        elif a['doing'][0] != FREE and b['doing'][0] == FREE and start is not None:
            spans.append(b['frame'] - start)
            start = None
    # (and the animation: the frames player 1's animation is not the standing one's)
    idle = frames[0]['anim'][0]
    anims = []
    run = 0
    for f in frames:
        if f['anim'][0] != idle:
            run += 1
        elif run:
            anims.append(run)
            run = 0
    print('frame data: %d light punches; startup/active/recovery %s; player 1 not free for %s frames; its animation not the '
          'standing one for %s frames' % (len(moves), ' or '.join('%d/%d/%d' % n for n in numbers), sorted(set(spans)), sorted(set(anims))))
    if len(moves) < 10:
        fail('too few moves')
    if len(numbers) != 1:
        fail('the same move measured differently')
    startup, active, recovery = numbers[0]
    if sorted(set(spans)) != [startup - 1 + active + recovery]:
        fail('the numbers do not add up to the move\'s length')
    ok('frame data')


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
    shown = set(t for _, texts in hud_lines(log) for t in texts if t.startswith('PARRIED'))
    print('parry kind: player 2 parried %s; shown: %s' % (', '.join('%s %d' % kv for kv in sorted(kinds.items())) or 'nothing',
                                                         '; '.join(sorted(shown)[:4])))
    if sum(kinds.values()) < 8:
        fail('fewer than 8 parries')
    if kinds.get('anti-air') or kinds.get('air'):
        fail('grounded attacks read as parried in the air or anti-air')
    if not kinds.get('forward') or not kinds.get('down'):
        fail('standing and crouching attacks not told apart (forward and down)')
    if not shown:
        fail('no parry shown on screen')
    ok('parry kind')


def attacks_and_parries(log):
    """Player 1's attacks that met player 2 (a hit, a guard or a parry), and those parried."""
    frames = frames_of(log)
    parried = sum(1 for player, _, result in parries(log) if player == 2 and result == 'parried')
    met = len(hits_on(frames, 1))
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


def gauge(log_on, log_off):
    """Gauge refills: with it, player 1's stocks are at their most again after each super art; without, never a stock."""
    on, off = frames_of(log_on), frames_of(log_off)
    most = max(f['gauge'][0] for f in on)
    spent = sum(1 for a, b in zip(on, on[1:]) if b['gauge'][0] < a['gauge'][0])
    full = sum(1 for f in on[30:] if f['gauge'][0] == most)
    print('gauge refill: with it, player 1 had %d stocks at most, spent one %d times, and was full on %d of %d frames; without, %d at most' % (
        most, spent, full, len(on) - 30, max(f['gauge'][0] for f in off[:600])))
    if most < 1 or spent < 3:
        fail('no super art made with the gauge kept full')
    if full < (len(on) - 30) * 0.9:
        fail('the gauge not kept full')
    ok('gauge refill')


def restart(log, where):
    """"Start over": the players put back; where: 'middle' (as the round began), 'left' or 'right' (a corner), and
    'swapped' in the name for the sides swapped."""
    frames = frames_of(log)
    first = frames[0]
    jumps = [(a, b) for a, b in zip(frames, frames[1:]) if abs(b['x'][0] - a['x'][0]) > 40 or abs(b['x'][1] - a['x'][1]) > 40]
    print('start over (%s): the round began with the players at %d and %d; put back %d times, to %s' % (
        where, first['x'][0], first['x'][1], len(jumps), sorted(set(b['x'] for _, b in jumps))))
    if not jumps:
        fail('the players never put back')
    for _, b in jumps:
        one, two = b['x']
        if 'swapped' in where:
            one, two = two, one
        if where.startswith('middle') and (one, two) != first['x']:
            fail('not where the round began')
        if where.startswith('left') and not (one < first['x'][0] - 100 and 60 < two - one < 120):
            fail('not the left corner')
        if where.startswith('right') and not (one > first['x'][1] + 100 and 60 < one - two < 120):
            fail('not the right corner')
        if b['life'] != (160, 160):
            fail('health not full after it')
    ok('start over')


def stage(log, number):
    """The Stage row: the session's fight is on the stage chosen (the game's stage byte through the round)."""
    frames = frames_of(log)
    stages = sorted(set(f['stage'] for f in frames))
    print('stage: the round fought on stage %s (asked for %s)' % (stages, number))
    if stages != [int(number)]:
        fail('not the stage chosen')
    ok('stage')


def not_free(frames, player):
    """The lengths of the stretches a player is not free for."""
    spans, start = [], None
    for a, b in zip(frames, frames[1:]):
        if a['doing'][player] == FREE and b['doing'][player] != FREE:
            start = b['frame']
        elif a['doing'][player] != FREE and b['doing'][player] == FREE and start is not None:
            spans.append(b['frame'] - start)
            start = None
    return spans


def getup(log_quick, log_plain):
    """The dummy's quick stand: swept, it is free again sooner than when it gets up where it fell."""
    quick, plain = not_free(frames_of(log_quick), 1), not_free(frames_of(log_plain), 1)
    print('get up: swept %d times it was down for %s frames with the quick stand, %s without (%d times)' % (
        len(quick), sorted(set(quick)), sorted(set(plain)), len(plain)))
    if len(quick) < 5 or len(plain) < 5:
        fail('too few knock-downs')
    if max(quick) >= min(plain):
        fail('no sooner up')
    ok('get up')


def stunmash(log_mash, log_plain):
    """Player 2 stunned again and again (the record's reaction the stun's: routine_no[1] 1 and [2] 25): mashing, its stuns
    are shorter; it mashes only while stunned (nothing held from the frame the stun is over), and makes no move."""
    out = []
    for log in (log_mash, log_plain):
        frames = frames_of(log)
        runs, n, held_after, moves = [], 0, 0, 0
        for f in frames[60:]:
            now = f['routine'] == 1 and f['how'] == 25
            if now:
                n += 1
            elif n:
                runs.append(n)
                n = 0
            if not now and f['held'][1]:
                held_after += 1
            if f['doing'][1] in (STARTUP, ACTIVE, RECOVERY):
                moves += 1
        out.append((runs, held_after, moves))
    (mash, held_after, moves), (plain, _, _) = out
    print('stun mash: %d stuns of %d to %d frames mashing, %d of %d to %d without; %d frames of controls held by player 2 while not '
          'stunned; %d frames of player 2 in a move' % (len(mash), min(mash or [0]), max(mash or [0]), len(plain), min(plain or [0]),
                                                        max(plain or [0]), held_after, moves))
    if len(mash) < 3 or len(plain) < 3:
        fail('too few stuns')
    if sum(mash) / len(mash) > 0.8 * sum(plain) / len(plain):
        fail('mashing did not shorten the stuns')
    if held_after or moves:
        fail('player 2 still mashing, or attacking, after a stun')
    ok('stun mash')


def computer(log):
    """The dummy played by the game: it makes moves of its own and takes health off player 1, who does nothing."""
    frames = frames_of(log)
    taken = sum(d for _, d in hits_on(frames, 0))
    moves = sum(1 for a, b in zip(frames, frames[1:]) if a['doing'][1] == FREE and b['doing'][1] in (STARTUP, ACTIVE))
    print('computer: player 2 made %d moves and took %d health off player 1' % (moves, taken))
    if moves < 5 or taken < 50:
        fail('the computer did not fight')
    ok('computer')


def features(path, *expect):
    """The feature list: each name given is there (NAME) or is not (-NAME)."""
    names = set(line.split('\t')[0] for line in open(path))
    wrong = [e for e in expect if (e[1:] in names) if e.startswith('-')] + [e for e in expect if not e.startswith('-') and e not in names]
    print('features: %d listed; %s' % (len(names), 'as expected' if not wrong else 'wrong: ' + ', '.join(wrong)))
    if wrong:
        fail('the feature list')
    ok('features')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    what, args = sys.argv[1], sys.argv[2:]
    if what == 'movie':
        movie(args[0], int(args[1]), int(args[2]), args[3])
    else:
        globals()[what](*args)
