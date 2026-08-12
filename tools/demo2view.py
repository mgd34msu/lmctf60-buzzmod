#!/usr/bin/env python3
"""demo2view.py -- rewrites a serverrecord .dm2 capture into a client-playable
.dm2 demo for the yq2 client (protocol 34).

WHY: SV_RecordDemoMessage (yquake2 server/sv_entities.c) writes a stripped
capture format meant for offline analysis, not client playback: the signon
block is svc_serverdata + every configstring (no spawnbaselines -- none are
needed, see below), and each subsequent block is svc_frame + framenum(int32)
+ svc_packetentities + full-state delta entities (deltaed against an
all-zero state, force=true, so every field is written explicitly every
frame -- no baseline is ever referenced) + a terminating short 0, followed
by that frame's accumulated multicast bytes verbatim (sounds, temp
entities, configstring updates -- svc_print is never present in this
capture shape, see film.py's docstring for why). There is no
player_state_t, no areabits, and SV_ServerRecord_f (server/sv_cmd.c)
hardwires playernum to -1 in the signon block.

None of that plays in a real client. CL_ParseServerData (client/cl_parse.c)
treats playernum == -1 as "this is a cinematic, not a level" and never
loads a map -- confirmed against SV_New_f (server/sv_user.c): for a
`demomap`-launched local server (sv.state == ss_demo), SV_New_f skips
building its own svc_serverdata entirely ("demo servers just dump the file
message") and just opens the .dm2 -- SV_SendClientMessages /
SV_NextDemoChunk (server/sv_send.c) then streams this file's
length-prefixed blocks straight to the client's netchan, one block per
server frame tick, unmodified. So the signon block THIS TOOL writes is,
byte for byte (modulo the playernum patch), what CL_ParseServerData
actually receives -- there is no other synthesis step in between. And even
with a valid playernum, CL_ParseFrame (client/cl_parse.c) requires a
frame message shaped serverframe(int32) + deltaframe(int32) +
suppresscount(byte) + areabytes(byte)+areabits + svc_playerinfo (a
player_state_t delta) + svc_packetentities -- fields this capture format
never had.

This tool bridges the gap:
  * patches the signon block's playernum from -1 to PLAYERNUM_SENTINEL
    (-2), a value CL_ParseServerData's `== -1` cinematic check will never
    match, and which `cl.playernum + 1` equality checks elsewhere
    (CL_AddPacketEntities' own-viewmodel hide in client/cl_entities.c,
    CL_ParsePredictedMovement in cl_prediction.c, the hook-cable-owner
    check in cl_tempentities.c) can never match either, because a real
    entity_state_t.number is never negative. Everything else in the signon
    block -- protocol, spawncount, attractloop, gamedir, levelname, every
    configstring -- passes through unchanged.
  * turns every svc_frame block into a real client frame: svc_frame +
    serverframe(original framenum, so servertime pacing at 10Hz is
    unchanged) + deltaframe(-1, uncompressed -- CL_ParseFrame treats
    deltaframe <= 0 as "valid, delta from nothing") + suppresscount(0) +
    areabytes(32)+areabits(32 bytes of 0xff, full visibility -- the
    client's cl.frame.areabits buffer is exactly MAX_MAP_AREAS/8 = 32
    bytes and MSG_ReadData does no bounds check, so 32 is both the
    generous choice and the only safe one) + svc_playerinfo carrying a
    synthesized spectator player_state_t (pm_type PM_FREEZE; see
    build_playerstate below for why this makes the client use the sent
    origin/viewangles directly every frame instead of predicting movement
    from usercmds that a demo playback never has) + svc_packetentities
    with the ORIGINAL entity-delta bytes copied verbatim -- they are
    already exactly what an uncompressed full-state client frame wants,
    see copy_entity_payload -- + the original multicast tail, also copied
    verbatim (CL_ParseServerMessage keeps reading svc commands from the
    same message after CL_ParseFrame returns, exactly like a live game
    would with its own per-frame extras appended after the frame message).

The camera is a synthetic spectator, not a reconstruction of what any
recorded player actually saw -- see build_camera_path / --chase.

CLI:
    demo2view.py <in.dm2> [-o out.dm2] [--chase auto|<playername>]

Default output name is <in>-view.dm2 in the same directory as the input.
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm2speed as D
import film

# -------------------------------------------------------------- wire layout
SVC_MUZZLEFLASH = 1
SVC_MUZZLEFLASH2 = 2
SVC_TEMP_ENTITY = 3
SVC_SOUND = 9
SVC_PRINT = 10
SVC_SERVERDATA = 12
SVC_CONFIGSTRING = 13
SVC_SPAWNBASELINE = 14
SVC_CENTERPRINT = 15
SVC_PLAYERINFO = 17
SVC_PACKETENTITIES = 18
SVC_FRAME = 20

PM_FREEZE = 4  # common/header/shared.h pmtype_t: NORMAL,SPECTATOR,DEAD,GIB,FREEZE

PS_M_TYPE = 1 << 0
PS_M_ORIGIN = 1 << 1
PS_VIEWANGLES = 1 << 8
PS_FOV = 1 << 11

# playernum -1 means "cinematic, no level" to CL_ParseServerData; any other
# negative value sidesteps that check AND can never equal a real (always
# non-negative) entity_state_t.number when the client does
# `s1->number == cl.playernum + 1` own-entity checks (see module docstring).
PLAYERNUM_SENTINEL = -2

AREA_BYTES = 32          # MAX_MAP_AREAS(256) / 8, and cl.frame.areabits'
AREABITS_FULL = b'\xff' * AREA_BYTES  # exact fixed size -- see module docstring

MAX_BLOCK = 32768        # matches this fleet's raised-MAX_MSGLEN test client;
                          # SV_NextDemoChunk (server/sv_send.c) hard-errors
                          # ("msglen > MAX_MSGLEN") on a stock client if
                          # exceeded, so this is asserted, never silently
                          # allowed through.

# EF_FLAG1/EF_FLAG2 (q_shared.h) -- same constants film.py uses for carry
# detection.
EF_FLAG1 = 0x00040000
EF_FLAG2 = 0x00080000
EF_FLAG_MASK = EF_FLAG1 | EF_FLAG2

# Camera tuning. See build_camera_path.
CHASE_BEHIND_U = 120.0
CHASE_UP_U = 40.0
CHASE_LOOK_UP_U = 20.0       # aim a bit above the chased entity's feet
ORBIT_RADIUS_U = 420.0
ORBIT_HEIGHT_U = 260.0
ORBIT_LOOK_UP_U = 20.0
ORBIT_DEG_PER_FRAME = 0.6    # 6 deg/s at this protocol's 10Hz frame rate
CAMERA_LERP_ALPHA = 0.15     # exponential smoothing factor per frame
CAMERA_FOV = 90


# ------------------------------------------------------------- angle/coord
def pack_angle16(deg):
    """MSG_WriteAngle16 inverse: degrees -> the wire's 16-bit representation
    (client/cl_parse.c's MSG_ReadAngle16 is s16 * 360.0/65536.0), returned
    as the 2 little-endian bytes to write."""
    v = int(round((deg % 360.0) * (65536.0 / 360.0))) & 0xFFFF
    return struct.pack('<H', v)


def pack_coord16(u, warned=[False]):
    """World units -> the wire's 1/8-unit short fixed point used for
    pmove.origin (same encoding as entity origins, see dm2speed.py). Clamps
    to the short's range rather than raising -- a camera that briefly
    strays past a map's extreme edge should not abort a whole conversion,
    but it is reported once so it isn't silently invisible."""
    v = int(round(u * 8.0))
    if v < -32768 or v > 32767:
        if not warned[0]:
            sys.stderr.write(
                f"warning: camera coordinate {u:.1f} clamped to short "
                f"range (further clamps not individually reported)\n")
            warned[0] = True
        v = max(-32768, min(32767, v))
    return struct.pack('<h', v)


def build_playerstate(eye, yaw, pitch, roll):
    """svc_playerinfo payload: a player_state_t delta against the null
    state CL_ParsePlayerstate resets to whenever deltaframe <= 0 (every
    frame here, since deltaframe is always sent as -1). Only the fields
    the camera needs are flagged; everything else -- velocity, gravity,
    blend, weapon, stats -- stays at its zero default, which is correct
    for a pure spectator view.

    pm_type = PM_FREEZE matters twice on the client side:
      * common/pmove.c's Pmove() returns immediately for PM_FREEZE, so
        CL_PredictMovement's client-side prediction never moves
        cl.predicted_origin away from the origin this function writes.
      * CL_CalcViewValues (client/cl_entities.c) only uses
        cl.predicted_angles (built from accumulated mouse-look deltas
        that a demo playback never receives) when pm_type < PM_DEAD;
        PM_FREEZE is ordered after PM_DEAD/PM_GIB, so it takes the
        `else` branch and interpolates cl.refdef.viewangles directly from
        this playerstate's viewangles instead -- exactly the field this
        function writes every frame.
    """
    flags = PS_M_TYPE | PS_M_ORIGIN | PS_VIEWANGLES | PS_FOV
    out = bytearray()
    out += struct.pack('<H', flags)
    out += struct.pack('<B', PM_FREEZE)
    out += pack_coord16(eye[0])
    out += pack_coord16(eye[1])
    out += pack_coord16(eye[2])
    out += pack_angle16(yaw)
    out += pack_angle16(pitch)
    out += pack_angle16(roll)
    out += struct.pack('<B', CAMERA_FOV)
    out += struct.pack('<I', 0)   # statbits: no stats sent
    return bytes(out)


def viewangles_from(eye, look):
    """yaw/pitch/roll (degrees) so AngleVectors(viewangles) points from eye
    toward look -- see common/collision.c's AngleVectors for the forward
    vector convention this inverts: forward = (cos(yaw)cos(pitch),
    sin(yaw)cos(pitch), -sin(pitch))."""
    dx = look[0] - eye[0]
    dy = look[1] - eye[1]
    dz = look[2] - eye[2]
    horiz = math.hypot(dx, dy)
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(-dz, horiz)) if horiz or dz else 0.0
    return yaw, pitch, 0.0


# --------------------------------------------------------------- camera
def resolve_player_name(d, labels, name):
    """Matches --chase <playername> against the CS_PLAYERSKINS configstrings
    (film.py's d['skins'], keyed by player-slot index = entnum - 1, value
    "name\\model/skin"). Exact case-insensitive match preferred, then a
    unique case-insensitive substring match. Raises ValueError (with the
    rostered name list) if nothing or more than one candidate matches --
    a silent wrong-player lock would be worse than refusing."""
    wanted = name.strip().lower()
    exact = []
    partial = []
    for n in sorted(labels):
        skin = d['skins'].get(n - 1, '')
        pname = skin.split('\\')[0]
        if not pname:
            continue
        low = pname.lower()
        if low == wanted:
            exact.append((n, pname))
        elif wanted in low:
            partial.append((n, pname))
    if exact:
        return exact[0][0]
    if len(partial) == 1:
        return partial[0][0]
    roster = ', '.join(
        d['skins'].get(n - 1, '?').split('\\')[0] for n in sorted(labels))
    if partial:
        names = ', '.join(p[1] for p in partial)
        raise ValueError(
            f"--chase {name!r} matches multiple rostered players: {names}")
    raise ValueError(
        f"--chase {name!r} matches no rostered player (roster: {roster})")


def compute_camera_targets(d, labels, chase_arg):
    """Per-frame (target_pos, facing_yaw_or_None, mode) for frames 1..N,
    mode in {'chase', 'orbit'}. Chase mode's facing_yaw is the chased
    entity's own yaw (for the behind-and-above offset in
    build_camera_path); orbit mode has no facing (None).

    --chase auto: chases whichever rostered player's effects field carries
    EF_FLAG1/EF_FLAG2 that frame (film.py's own carry signal, see
    EF_FLAG_MASK), preferring to KEEP the current carrier across a frame
    where multiple players show a flag bit simultaneously (a handoff tick)
    rather than flapping between them. With no carrier that frame, the
    target is the centroid of every rostered player position sampled that
    frame (mode='orbit').

    --chase <playername>: locks onto that one entity's own track for the
    whole demo (mode='chase' throughout); its position is held at the last
    known sample for frames before it has one (e.g. before that player's
    entity first appears)."""
    tracks = d['tracks']
    yaws = d.get('yaws', {})
    frames = d['frames']

    pos_by_ent = {n: {f: (x, y, z) for f, x, y, z, _eff in tracks[n]}
                  for n in labels}
    eff_by_ent = {n: {f: eff for f, _x, _y, _z, eff in tracks[n]}
                  for n in labels}

    fixed_ent = None
    if chase_arg not in (None, 'auto'):
        fixed_ent = resolve_player_name(d, labels, chase_arg)

    carrying = {}
    for n in labels:
        for f, eff in eff_by_ent[n].items():
            if int(eff) & EF_FLAG_MASK:
                carrying.setdefault(f, []).append(n)

    out = []
    current_carrier = None
    last_pos = None
    for f in range(1, frames + 1):
        if fixed_ent is not None:
            p = pos_by_ent[fixed_ent].get(f, last_pos)
            yaw = yaws.get(fixed_ent, {}).get(f)
            mode = 'chase'
        else:
            cands = carrying.get(f)
            if cands:
                if current_carrier not in cands:
                    current_carrier = min(cands)
            else:
                current_carrier = None
            if current_carrier is not None:
                p = pos_by_ent[current_carrier].get(f, last_pos)
                yaw = yaws.get(current_carrier, {}).get(f)
                mode = 'chase'
            else:
                pts = [pos_by_ent[n][f] for n in labels if f in pos_by_ent[n]]
                if pts:
                    p = (sum(pt[0] for pt in pts) / len(pts),
                         sum(pt[1] for pt in pts) / len(pts),
                         sum(pt[2] for pt in pts) / len(pts))
                else:
                    p = last_pos
                yaw = None
                mode = 'orbit'
        if p is None:
            p = (0.0, 0.0, 0.0)
        last_pos = p
        out.append((p, yaw, mode))
    return out


def build_camera_path(targets):
    """Raw eye/look-at points per build spec (chase: ~120u behind, ~40u
    above, aimed at the target; orbit: a slow ring around the target
    centroid), then exponentially smoothed (CAMERA_LERP_ALPHA) across
    consecutive frames so a carrier change, a mode switch, or a target
    entity's own teleport-on-respawn never appears as a camera teleport.
    Returns (eyes, looks), each a list aligned with targets."""
    eyes = []
    looks = []
    orbit_angle = 0.0
    sm_eye = None
    sm_look = None
    for p, yaw, mode in targets:
        if mode == 'chase':
            facing = yaw if yaw is not None else 0.0
            rad = math.radians(facing)
            fwd = (math.cos(rad), math.sin(rad))
            raw_eye = (p[0] - fwd[0] * CHASE_BEHIND_U,
                       p[1] - fwd[1] * CHASE_BEHIND_U,
                       p[2] + CHASE_UP_U)
            raw_look = (p[0], p[1], p[2] + CHASE_LOOK_UP_U)
        else:
            orbit_angle = (orbit_angle + ORBIT_DEG_PER_FRAME) % 360.0
            rad = math.radians(orbit_angle)
            raw_eye = (p[0] + ORBIT_RADIUS_U * math.cos(rad),
                       p[1] + ORBIT_RADIUS_U * math.sin(rad),
                       p[2] + ORBIT_HEIGHT_U)
            raw_look = (p[0], p[1], p[2] + ORBIT_LOOK_UP_U)

        if sm_eye is None:
            sm_eye, sm_look = raw_eye, raw_look
        else:
            sm_eye = tuple(sm_eye[i] + CAMERA_LERP_ALPHA *
                            (raw_eye[i] - sm_eye[i]) for i in range(3))
            sm_look = tuple(sm_look[i] + CAMERA_LERP_ALPHA *
                             (raw_look[i] - sm_look[i]) for i in range(3))
        eyes.append(sm_eye)
        looks.append(sm_look)
    return eyes, looks


# ---------------------------------------------------------------- rewrite
def patch_signon_block(block):
    """Passes the signon block (svc_serverdata + every configstring) through
    unchanged except the playernum short, patched from -1 to
    PLAYERNUM_SENTINEL in place (fixed-width field, no length change)."""
    r = D.R(block)
    svc = r.u8()
    if svc != SVC_SERVERDATA:
        raise ValueError(
            f"first block: expected svc_serverdata ({SVC_SERVERDATA}), "
            f"got {svc} -- not a serverrecord capture?")
    r.skip(8)          # protocol(4) + spawncount(4)
    r.skip(1)           # attractloop -- passed through unchanged
    r.str_()             # gamedir
    pn_off = r.i
    pn = r.s16()
    if pn != -1:
        sys.stderr.write(
            f"warning: signon playernum was {pn}, not the expected -1 "
            f"(SV_ServerRecord_f always writes -1) -- patching anyway\n")
    out = bytearray(block)
    struct.pack_into('<h', out, pn_off, PLAYERNUM_SENTINEL)
    return bytes(out)


def copy_entity_payload(r):
    """Walks a svc_packetentities delta-entity list purely to find its end
    offset (the terminating short 0), reusing dm2speed's generic bit/delta
    parser -- same wire shapes film.py verified against this mod's actual
    writer. Returns nothing; advances r past the terminator. The caller
    slices the raw bytes from the position before this call to r.i after
    it -- that slice, unparsed, IS the verbatim payload an uncompressed
    client frame wants (see module docstring)."""
    while True:
        bits, num = D.parse_entity_bits(r)
        if num == 0:
            break
        D.parse_delta_entity(r, bits)


def rewrite_frame_block(block, idx, eyes, looks):
    r = D.R(block)
    svc = r.u8()
    if svc != SVC_FRAME:
        raise ValueError(f"frame block {idx}: expected svc_frame "
                          f"({SVC_FRAME}), got {svc}")
    framenum = r.s32()
    svc2 = r.u8()
    if svc2 != SVC_PACKETENTITIES:
        raise ValueError(f"frame block {idx}: svc_frame not immediately "
                          f"followed by svc_packetentities, got {svc2}")
    payload_start = r.i
    copy_entity_payload(r)
    payload_end = r.i                      # includes the terminating short 0
    entity_payload = block[payload_start:payload_end]
    multicast_tail = block[payload_end:]

    eye, look = eyes[idx], looks[idx]
    yaw, pitch, roll = viewangles_from(eye, look)

    out = bytearray()
    out.append(SVC_FRAME)
    out += struct.pack('<i', framenum)     # serverframe: original numbering
    out += struct.pack('<i', -1)           # deltaframe: uncompressed
    out.append(0)                          # suppresscount
    out.append(AREA_BYTES)
    out += AREABITS_FULL
    out.append(SVC_PLAYERINFO)
    out += build_playerstate(eye, yaw, pitch, roll)
    out.append(SVC_PACKETENTITIES)
    out += entity_payload                  # verbatim, incl. terminator
    out += multicast_tail                  # verbatim
    return bytes(out)


def convert(in_path, out_path, chase_arg):
    raw = open(in_path, 'rb').read()

    d = film.walk_demo(in_path)
    if not d['svrecord']:
        raise SystemExit(
            f"{in_path}: not a serverrecord capture (signon playernum != "
            f"0xffff) -- this tool only converts serverrecord .dm2s")
    labels, _teams = film.anonymize(d)
    if not labels:
        raise SystemExit(f"{in_path}: no rostered (red/blue, moving) "
                          f"players found -- nothing to build a camera "
                          f"from")

    targets = compute_camera_targets(d, labels, chase_arg)
    eyes, looks = build_camera_path(targets)

    n_frames_written = 0
    with open(out_path, 'wb') as out:
        off = 0
        first = True
        frame_idx = 0
        total = len(raw)
        while off + 4 <= total:
            (mlen,) = struct.unpack_from('<i', raw, off)
            off += 4
            if mlen == -1:
                out.write(struct.pack('<i', -1))
                break
            block = raw[off:off + mlen]
            off += mlen
            if first:
                out_block = patch_signon_block(block)
                first = False
            else:
                out_block = rewrite_frame_block(block, frame_idx, eyes, looks)
                frame_idx += 1
                n_frames_written += 1
            if len(out_block) > MAX_BLOCK:
                raise SystemExit(
                    f"{in_path}: output block {len(out_block)} bytes at "
                    f"input offset {off - mlen} exceeds MAX_BLOCK "
                    f"({MAX_BLOCK}) -- refusing to write a block the test "
                    f"client would drop")
            out.write(struct.pack('<i', len(out_block)))
            out.write(out_block)

    return n_frames_written, d['frames'], d.get('map')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demo', help='input serverrecord .dm2')
    ap.add_argument('-o', '--output', default=None,
                     help='output path (default: <in>-view.dm2 next to '
                          'the input)')
    ap.add_argument('--chase', default='auto',
                     help='"auto" (default): chase the flag carrier, '
                          'orbit otherwise. Or a player name to lock the '
                          'camera onto for the whole demo.')
    args = ap.parse_args()

    if args.output:
        out_path = args.output
    else:
        base, ext = os.path.splitext(args.demo)
        out_path = f"{base}-view{ext or '.dm2'}"

    try:
        n_written, n_total, mapname = convert(args.demo, out_path, args.chase)
    except ValueError as exc:
        raise SystemExit(f"{args.demo}: {exc}")
    sys.stderr.write(
        f"{args.demo} -> {out_path}: {n_written}/{n_total} frame blocks "
        f"rewritten, map={mapname}, chase={args.chase!r}\n")


if __name__ == '__main__':
    main()
