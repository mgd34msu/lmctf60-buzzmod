#!/usr/bin/env python3
"""Measure visible bot movement grammar from serverrecord demos.

Kinematics are derived from 10 Hz entity-origin deltas because serverrecord bot
entities do not carry player-state velocity. Output includes pooled and per-bot
movement, turn, stop, wall-contact, and route-texture metrics.
"""
import argparse
import collections
import json
import math
import os
from pathlib import Path
import statistics as st
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D

U_ORIGIN1 = 1 << 0
U_ORIGIN2 = 1 << 1
U_ANGLE2 = 1 << 2
U_ANGLE3 = 1 << 3
U_FRAME8 = 1 << 4
U_EVENT = 1 << 5
U_REMOVE = 1 << 6
U_MOREBITS1 = 1 << 7
U_NUMBER16 = 1 << 8
U_ORIGIN3 = 1 << 9
U_ANGLE1 = 1 << 10
U_MODEL = 1 << 11
U_RENDERFX8 = 1 << 12
U_EFFECTS8 = 1 << 14
U_MOREBITS2 = 1 << 15
U_SKIN8 = 1 << 16
U_FRAME16 = 1 << 17
U_RENDERFX16 = 1 << 18
U_EFFECTS16 = 1 << 19
U_MODEL2 = 1 << 20
U_MODEL3 = 1 << 21
U_MODEL4 = 1 << 22
U_MOREBITS3 = 1 << 23
U_OLDORIGIN = 1 << 24
U_SKIN16 = 1 << 25
U_SOUND = 1 << 26
U_SOLID = 1 << 27


def parse_delta_entity_xyzyaw(r, bits, o):
    """o = [x, y, z, yaw_deg]. Only origin (0-2) and body yaw (3, from
    ANGLE2) are kept; everything else is skipped."""
    if bits & U_MODEL: r.skip(1)
    if bits & U_MODEL2: r.skip(1)
    if bits & U_MODEL3: r.skip(1)
    if bits & U_MODEL4: r.skip(1)
    if bits & U_FRAME8: r.skip(1)
    if bits & U_FRAME16: r.skip(2)
    if (bits & U_SKIN8) and (bits & U_SKIN16): r.skip(4)
    elif bits & U_SKIN8: r.skip(1)
    elif bits & U_SKIN16: r.skip(2)
    if (bits & U_EFFECTS8) and (bits & U_EFFECTS16): r.skip(4)
    elif bits & U_EFFECTS8: r.skip(1)
    elif bits & U_EFFECTS16: r.skip(2)
    if (bits & U_RENDERFX8) and (bits & U_RENDERFX16): r.skip(4)
    elif bits & U_RENDERFX8: r.skip(1)
    elif bits & U_RENDERFX16: r.skip(2)
    if bits & U_ORIGIN1: o[0] = r.s16() / 8.0
    if bits & U_ORIGIN2: o[1] = r.s16() / 8.0
    if bits & U_ORIGIN3: o[2] = r.s16() / 8.0
    if bits & U_ANGLE1: r.skip(1)
    if bits & U_ANGLE2: o[3] = r.u8() * 360.0 / 256.0
    if bits & U_ANGLE3: r.skip(1)
    if bits & U_OLDORIGIN: r.skip(6)
    if bits & U_SOUND: r.skip(1)
    if bits & U_EVENT: r.skip(1)
    if bits & U_SOLID: r.skip(2)


def walk(path, maxplayers=32, *, strict=False):
    """Returns {'map', 'svrecord', 'skins', 'tracks': {entnum: [(frame,x,y,z,yaw), ...]}}"""
    with open(path, 'rb') as source:
        data = source.read()
    off = 0
    mapname = None
    skins = {}
    ents = {}
    tracks = {}
    frame_idx = 0
    svrecord = None
    parse_complete = True
    terminated = False
    message_count = 0
    wire_frames = []
    import re as _re

    def read_packetentities():
        while True:
            bits, num = D.parse_entity_bits(r)
            if num == 0:
                break
            if bits & U_REMOVE:
                ents.pop(num, None)
                continue
            o = ents.setdefault(num, [0.0, 0.0, 0.0, 0.0])
            parse_delta_entity_xyzyaw(r, bits, o)

    while off < len(data):
        if off + 4 > len(data):
            if strict:
                raise ValueError("truncated demo message length")
            parse_complete = False
            break
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            terminated = True
            if strict and off != len(data):
                raise ValueError("demo has bytes after terminal marker")
            break
        if mlen < 0 or off + mlen > len(data):
            if strict:
                raise ValueError("invalid or truncated demo message")
            parse_complete = False
            break
        message_count += 1
        r = D.R(data[off:off+mlen])
        off += mlen
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); pn = r.u16(); r.str_()
                    svrecord = (pn == 0xffff)
                elif svc == 13:
                    idx = r.u16(); s = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = s
                    elif idx == 33:
                        m = _re.match(r'maps/(\w+)\.bsp', s)
                        if m: mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0, 0.0])
                    parse_delta_entity_xyzyaw(r, bits, o)
                elif svc == 20:
                    if svrecord:
                        wire_frames.append(r.s32())
                        svc2 = r.u8()
                        if svc2 != 18:
                            raise ValueError(f"expected packetentities, got {svc2}")
                        ents.clear()
                        read_packetentities()
                        frame_idx += 1
                        for num, o in ents.items():
                            if 1 <= num <= maxplayers:
                                tracks.setdefault(num, []).append(
                                    (frame_idx, o[0], o[1], o[2], o[3]))
                    else:
                        r.skip(9); ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    from demokin import parse_playerstate_full
                    parse_playerstate_full(r, {})
                elif svc == 18:
                    read_packetentities()
                    frame_idx += 1
                    for num, o in ents.items():
                        if 1 <= num <= maxplayers:
                            tracks.setdefault(num, []).append(
                                (frame_idx, o[0], o[1], o[2], o[3]))
                elif svc == 9: D.parse_sound(r)
                elif svc == 3: D.parse_temp_entity(r)
                elif svc in (1, 2): r.skip(3)
                elif svc == 10: r.skip(1); r.str_()
                elif svc in (11, 15, 4): r.str_()
                elif svc == 5: r.skip(512)
                elif svc in (6, 7): pass
                else: raise ValueError(svc)
            if strict and r.pos() != len(r.b):
                raise ValueError("demo message was not consumed exactly")
        except Exception as error:
            if strict:
                raise ValueError(
                    f"malformed demo message ending at byte {off}"
                ) from error
            parse_complete = False
            continue
    if strict:
        if message_count == 0 or svrecord is None or frame_idx == 0:
            raise ValueError("strict demo has no serverdata or frame messages")
        if svrecord and (
            len(wire_frames) != frame_idx
            or any(
                right != left + 1
                for left, right in zip(wire_frames, wire_frames[1:])
            )
        ):
            raise ValueError("serverrecord wire frames are incomplete")
    parse_complete = parse_complete and (terminated or off == len(data))
    return {'map': mapname, 'svrecord': bool(svrecord), 'skins': skins,
            'tracks': tracks, 'frames': frame_idx,
            'parse_complete': parse_complete, 'terminated': terminated,
            'wire_frames': wire_frames}


# ------------------------------------------------------------- grammar
def ang_diff(a, b):
    """smallest signed difference a-b in degrees, wrapped to [-180,180]"""
    return (a - b + 180.0) % 360.0 - 180.0


def entity_grammar(track, name='?'):
    """track: sorted list of (frame, x, y, z, yaw). Returns dict of stats,
    only using consecutive-frame pairs (frame delta == 1, dt = 0.1s) so
    gaps (death/respawn/out-of-PVS) don't inject fake velocity spikes."""
    if len(track) < 20:
        return None

    steps = []  # (t, hspeed, vz, heading, yaw)
    for i in range(1, len(track)):
        f0, x0, y0, z0, yaw0 = track[i-1]
        f1, x1, y1, z1, yaw1 = track[i]
        if f1 - f0 != 1:
            continue
        dt = 0.1
        vx, vy, vz = (x1-x0)/dt, (y1-y0)/dt, (z1-z0)/dt
        hspeed = math.hypot(vx, vy)
        heading = math.degrees(math.atan2(vy, vx)) if hspeed > 1 else None
        steps.append((f1, hspeed, vz, heading, yaw1))

    if len(steps) < 20:
        return None

    gain, relaunch, touchloss, viewdiv = [], 0, [], []
    still_frames = 0
    total_frames = len(steps)
    stopstart = 0        # crossings of the moving/stopped boundary
    wallbumps = 0
    turn180 = 0
    was_moving = None
    prev_hspeed = None
    prev_heading = None

    for i in range(1, len(steps)):
        _, hs, vz, hd, yaw = steps[i]
        _, phs, pvz, phd, pyaw = steps[i-1]

        # air-strafe gain / relaunch / touchdown loss (demokin.py thresholds,
        # same units since our vz/hspeed are also map-units/sec)
        if abs(vz) > 40 and abs(pvz) > 40 and hs > 200:
            gain.append(hs - phs)
        if pvz < -100 and vz > 100:
            relaunch += 1
        if pvz < -100 and abs(vz) < 20 and phs > 100:
            touchloss.append(phs - hs)

        # body-yaw vs velocity-heading divergence (airborne, moving) --
        # proxy for demokin's view_div (entity yaw is body/model facing,
        # not the true eye pitch/yaw pair playerstate would give)
        if hd is not None and abs(vz) > 40 and hs > 250:
            viewdiv.append(abs(ang_diff(yaw, hd)))

        moving = hs > 60
        if was_moving is not None and moving != was_moving:
            stopstart += 1
        was_moving = moving
        if hs < 40 and abs(vz) < 40:
            still_frames += 1

        # wall bump: big horizontal speed collapses in one 10Hz tick with
        # no corresponding vertical event (not a jump landing) -- i.e. the
        # bot walked into geometry and got stopped dead instead of sliding
        if phs > 220 and hs < 90 and abs(pvz) < 60 and abs(vz) < 60:
            wallbumps += 1

        # in-place ~180 turnaround at the 10Hz tick rate itself (not the
        # 1Hz gauge below) -- heading reverses while still moving both sides
        if phd is not None and hd is not None and phs > 80 and hs > 80:
            if abs(ang_diff(hd, phd)) > 150:
                turn180 += 1

    # 1Hz turn gauge: resample heading once/sec using the median heading of
    # each 10-frame window (robust to single-tick noise), then take frame-
    # to-frame (i.e. second-to-second) turn deltas.
    by_sec = collections.defaultdict(list)
    for f, hs, vz, hd, yaw in steps:
        if hd is not None and hs > 60:
            by_sec[f // 10].append(hd)
    sec_headings = []
    for k in sorted(by_sec):
        hs_list = by_sec[k]
        # circular-safe representative: just take the first sample in the
        # window as the "at this second" heading (matches a 1Hz poll)
        sec_headings.append(hs_list[0])
    turns = [abs(ang_diff(sec_headings[i], sec_headings[i-1]))
             for i in range(1, len(sec_headings))]
    reversals = sum(1 for t in turns if t > 90)

    # straight vs curved: 1s rolling windows of heading stdev (circular
    # stdev approximated via consecutive-diff RMS, cheap and monotonic
    # with "curviness")
    curv = []
    win = []
    for f, hs, vz, hd, yaw in steps:
        if hd is None or hs < 60:
            continue
        win.append(hd)
        if len(win) > 10:
            win.pop(0)
        if len(win) == 10:
            diffs = [abs(ang_diff(win[j], win[j-1])) for j in range(1, len(win))]
            curv.append(sum(diffs) / len(diffs))
    straight_share = (sum(1 for c in curv if c < 8) / len(curv)) if curv else None

    hspeeds = [s[1] for s in steps if s[1] > 40]

    out = {'name': name, 'n': total_frames}
    if gain: out['air_gain_med'] = round(st.median(gain), 2)
    out['relaunches'] = relaunch
    if touchloss: out['touch_loss_med'] = round(st.median(touchloss), 1)
    if viewdiv: out['view_div_med'] = round(st.median(viewdiv), 1)
    out['still_share'] = round(still_frames / total_frames, 3)
    out['stopstart_per_min'] = round(stopstart / (total_frames / 10.0 / 60.0), 1)
    out['wallbumps_per_min'] = round(wallbumps / (total_frames / 10.0 / 60.0), 1)
    out['turn180_10hz_per_min'] = round(turn180 / (total_frames / 10.0 / 60.0), 1)
    if turns:
        out['turn1hz_med'] = round(st.median(turns), 1)
        out['turn1hz_reversal_pct'] = round(100 * reversals / len(turns), 1)
    if straight_share is not None:
        out['straight_share'] = round(straight_share, 3)
    if hspeeds:
        out['hspeed_med'] = round(st.median(hspeeds), 1)
    return out


def analyze_demo(path, *, strict=False):
    d = walk(path, strict=strict)
    base = os.path.basename(path)
    if d['parse_complete'] is not True:
        raise ValueError("demo decode is incomplete")
    if not d['svrecord']:
        raise ValueError("demo is not a serverrecord")
    if d['map'] is None:
        raise ValueError("demo has no map identity")
    if strict and not d['tracks']:
        raise ValueError("demo has no player tracks")
    names = {}
    for number in d['tracks']:
        skin = d['skins'].get(number - 1)
        name = skin.split('\\', 1)[0] if skin else ''
        if name:
            names[number] = name
    if strict and not names:
        raise ValueError("demo has no named player tracks")
    stats = []
    for number, name in names.items():
        track = d['tracks'][number]
        grammar = entity_grammar(track, f"{base}:{name}")
        if grammar is None:
            if strict:
                raise ValueError(f"{name} lacks 20 consecutive movement steps")
            continue
        stats.append(grammar)
    if strict and len(stats) != len(names):
        raise ValueError("not every player track produced movement grammar")
    return d, stats, len(names)


def print_report(all_stats, stream):
    print(f"\n{len(all_stats)} bot-tracks with grammar\n", file=stream)

    def pool(key):
        return [row[key] for row in all_stats if key in row]

    print("POOLED (median of per-track values):", file=stream)
    for key in ('air_gain_med', 'touch_loss_med', 'view_div_med',
                'relaunches', 'still_share', 'stopstart_per_min',
                'wallbumps_per_min', 'turn180_10hz_per_min',
                'turn1hz_med', 'turn1hz_reversal_pct', 'straight_share',
                'hspeed_med'):
        values = pool(key)
        if values:
            print(
                f"  {key:26s} n={len(values):3d} "
                f"median={st.median(values):8.2f} mean={st.mean(values):8.2f}",
                file=stream,
            )


def write_json(path, value):
    destination = Path(path)
    payload = json.dumps(value, indent=1) + "\n"
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=destination.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, destination)
        directory = os.open(
            destination.parent, os.O_RDONLY | getattr(os, 'O_DIRECTORY', 0)
        )
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser()
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument('--stdout', action='store_true')
    output.add_argument('--output', type=Path)
    parser.add_argument(
        '--strict', action='store_true',
        help='reject incomplete demos and player tracks before publishing JSON',
    )
    parser.add_argument('demos', nargs='+')
    args = parser.parse_args(argv)

    all_stats = []
    for path in args.demos:
        base = os.path.basename(path)
        try:
            d, stats, named = analyze_demo(path, strict=args.strict)
        except (OSError, ValueError) as error:
            if args.strict:
                print(f"botkin: {base}: {error}", file=sys.stderr)
                return 1
            print(f"botkin: skipping {base}: {error}", file=sys.stderr)
            continue
        print(
            f"{base}: map={d['map']} frames={d['frames']} "
            f"entities={len(d['tracks'])} named={named}",
            file=sys.stderr,
        )
        all_stats.extend(stats)

    print_report(all_stats, sys.stderr)
    try:
        if args.stdout:
            json.dump(all_stats, sys.stdout, indent=1)
            sys.stdout.write('\n')
        else:
            write_json(args.output, all_stats)
            print(f"wrote {args.output}", file=sys.stderr)
    except OSError as error:
        print(f"botkin: cannot publish JSON: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
