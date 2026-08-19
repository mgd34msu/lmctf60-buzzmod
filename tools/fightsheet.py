#!/usr/bin/env python3
"""fightsheet.py -- rung 3 (FIGHTS): a blind duel/skirmish sheet.

Same blinding discipline as film.py and routesheet.py: identical extraction
for client (human) and serverrecord (bot) demos, no durations, no roster
counts, fixed geometry, constant axis scales.  See film.py's MODULE NOTES
1-11; notes 2, 7 and 10 apply here verbatim.  Nothing on the PNG reveals demo
shape; the sidecar is the unblinding artifact.

WHAT THIS SHEET ASKS: when two players on opposite teams are close enough to
fight, what happens?  Who shoots, at what range, with what weapon, closing
straight in or moving across, how ragged is the trigger, and how does the
fight end?

WHY IT NEEDS A NEW PARSING LAYER, unlike rung 2.  Rungs 1 and 2 are built
entirely from the entity position stream.  A fight is an *event*, and the
events are in message types both existing walkers skip to stay in byte-sync:

  svc_muzzleflash (svc 1)  short entnum + byte MZ_*   -- skipped as 3 bytes
  svc_temp_entity (svc 3)  byte type + payload        -- skipped by shape table

That first one is the whole rung.  It names the shooter's entity number AND
the weapon, it is emitted by every player weapon in p_weapon.c, and every
emission is gi.multicast(ent->s.origin, MULTICAST_PVS) -- the same scoping
the entity stream already has.  So the event stream carries the same partial
-view scars a client demo's entity stream does, pov-parity generalizes to it
(apply_pov_parity_events), and one 3-byte message makes the rung possible.

WIRE FACTS THIS MODULE DEPENDS ON, all verified against the lmctf60 game
source rather than vanilla id Software behaviour:

  * svc_muzzleflash payload is WriteShort(ent-g_edicts) + WriteByte(MZ_*),
    3 bytes, at every call site (p_weapon.c 814/893/942/1163/1302/1374/
    1490/1559/1644/1699, p_client.c 1740/2168/2280/2560, p_observer.c 103).
    MZ_SILENCED (128) is a bit flag ORed onto the weapon id.
  * The ONLY non-weapon MZ_* values this game ever emits are MZ_LOGIN (9)
    and MZ_LOGOUT (10).  MZ_RESPAWN and MZ_ITEMRESPAWN appear in q_shared.h
    but are written nowhere in the game source, so this module's discard
    rules for them are belt-and-braces, not load-bearing.
  * MZ_LOGIN IS NOT A RESPAWN SIGNAL IN THIS GAME, and the death signal is
    somewhere else entirely.  This is the one place the rung's design brief
    was wrong about the wire, it was checked rather than assumed, and the
    correction is load-bearing, so it is written out in full:

    The brief expected vanilla Quake II behaviour, where respawn() sends
    MZ_LOGIN and it therefore marks every death.  In lmctf60 it does not.
    respawn() (p_client.c:1620) sends no muzzle flash at all -- it sets
    `self->s.event = EV_PLAYER_TELEPORT` (p_client.c:1642).  Every MZ_LOGIN
    call site in this game is a JOIN path, not a death path: spectator_
    respawn (p_client.c:1740), the two team-join paths that print "%s
    entered the game" (2168, 2280), and observer exit (p_observer.c:103).

    Measured, exactly as the shape of the code predicts: a 383-second human
    client demo contains ZERO MZ_LOGINs, and a full 15-minute bot wave
    contains 7, in near-simultaneous pairs -- scheduled team joins, not
    deaths.  So MZ_LOGIN here carries no death information and its entire
    remaining content is roster churn: who joined or switched teams and
    when.  That is the same class of signal as MZ_LOGOUT, and it favours
    the same corpus (fixed bot rosters vs pub churn).  It is therefore
    discarded outright, not merely inside a leading window.

    The real death signal is EV_PLAYER_TELEPORT on the entity's `event`
    field, which this module captures (see parse_delta_entity_fight).  It is
    better than the MZ_LOGIN the brief wanted: it rides the entity stream,
    so it is scoped by exactly the same PVS test the positions are and needs
    no separate parity gate, and it fires on the death itself rather than on
    a rejoin.  It is shared with map teleporters (g_misc.c:1903), which is
    the same ambiguity film.py's >180u/tick heuristic already carries, and
    the two are used together.
  * MZ_LOGOUT is POISON and never enters this module past the parse
    boundary.  Bot waves have fixed rosters and never disconnect mid-match;
    human pub demos have people leaving constantly.  Any panel whose shape
    changed when a LOGOUT occurred would be a bot tell of the instrument's
    own making.  MZ_LOGIN is discarded beside it, for the reason above.
  * svc_muzzleflash2 (svc 2) is the monster stream.  Discarded entirely.
  * TE_BLOOD is emitted from exactly one call site, g_combat.c:553, and only
    when `take` damage lands on a client or monster, and SpawnDamage
    (g_combat.c:123) multicasts it MULTICAST_PVS from the impact point.  It
    is an honest DAMAGE signal, but T_Damage is called for more than a
    weapon hit landing: lava/slime tick damage (p_view.c, no per-victim
    cooldown -- it fires every 0.1s a player stands in it), rocket/grenade
    splash hitting everyone in the blast radius from one shot, a rail slug
    piercing several stacked players, and grapple contact damage all fire
    the same TE_BLOOD.  Counting every one as "a shot landed" measured
    hits=1236 against shots=180 on a bot demo, with 47% of blood events
    having no preceding shot at all -- see attribute_hits, which ties a
    blood event to a plausible preceding shot before it is allowed to draw
    an engagement-timeline "hit landed" triangle, and keeps the
    unattributed count alongside it rather than dropping it.

    Hand-audited limitation, symmetric across both shapes and stated in the
    notes strip: the hook (p_weapon.c:1943) and the plasma gun
    (p_weapon.c:2205) have their muzzleflash calls COMMENTED OUT in this
    game's source, so shots from those two weapons are invisible to this
    instrument.  They are invisible on human and bot demos alike.

CLI:
    fightsheet.py <demo.dm2> [...] --out <dir>
                  [--pov-parity [--pov-ent N] [--pov-radius U] [--pov-fov D]]
    fightsheet.py <demo.dm2> [...] --scalars [--pov-parity]
    fightsheet.py <demo.dm2> [...] --verify-parser
    fightsheet.py <demo.dm2> [...] --out <dir> --leak-audit
    fightsheet.py --calibrate [--human <glob>...] [--bot <glob>...]
                  [--maps mactf06 ...] [--radius-check]

Writes <hash>.png (the sheet) and <hash>.json (source-mapping sidecar, NOT
blind -- that file exists for the unblinding step only) per demo, hash-named
by film.py's hash_demo so one demo carries one hash across every rung and a
single unblinding table serves all of them.
"""
import argparse
import bisect
import collections
import glob as globmod
import json
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import film as F
import dm2speed as D
import routesheet as RS          # roc_auc / _ranks / glob helpers ONLY, so
                                 # every rung's Stage A uses one AUC function
from demokin import parse_playerstate_full

import numpy as np

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.text
from matplotlib.lines import Line2D


# ------------------------------------------------------- muzzle flash ids
MZ_BLASTER = 0
MZ_MACHINEGUN = 1
MZ_SHOTGUN = 2
MZ_CHAINGUN1 = 3
MZ_CHAINGUN2 = 4
MZ_CHAINGUN3 = 5
MZ_RAILGUN = 6
MZ_ROCKET = 7
MZ_GRENADE = 8
MZ_LOGIN = 9
MZ_LOGOUT = 10
MZ_RESPAWN = 11
MZ_BFG = 12
MZ_SSHOTGUN = 13
MZ_HYPERBLASTER = 14
MZ_ITEMRESPAWN = 15
MZ_SILENCED = 128

# Never parsed into an event, ever.  See the module docstring: in this game
# MZ_LOGIN and MZ_LOGOUT are both pure roster churn (join / team switch /
# disconnect) and neither carries death information, so both are poison of
# the same kind -- bot waves have fixed rosters, human pub demos do not.
# MZ_RESPAWN and MZ_ITEMRESPAWN are written nowhere in this game's source but
# are listed so a future game change cannot leak in through this door.
MZ_DISCARD = {MZ_LOGIN, MZ_LOGOUT, MZ_RESPAWN, MZ_ITEMRESPAWN}

# entity_state_t.event, q_shared.h:1163.  Set by respawn() (p_client.c:1642)
# and by a teleporter destination (g_misc.c:1903).  This is the death signal.
EV_PLAYER_TELEPORT = 6

# ---------------------------------------------------------- weapon classes
# The seven classes the design names, plus BFG as an eighth.  BFG is a real
# weapon in this game (MZ_BFG is emitted at p_weapon.c:1699) and dropping its
# shots would silently discard data from one panel but not another; giving it
# its own fixed slot keeps every shot accounted for on every panel.  A class
# nobody used renders as an empty slot, which is the same shape on every
# sheet, so an unused slot leaks nothing (L3).
CLASS_BLASTER = 'blaster'
CLASS_SHOTGUN = 'shotgun'
CLASS_MACHINEGUN = 'machinegun'
CLASS_GRENADE = 'grenade'
CLASS_ROCKET = 'rocket'
CLASS_HYPER = 'hyperblaster'
CLASS_RAIL = 'rail'
CLASS_BFG = 'bfg'

CLASS_ORDER = [CLASS_BLASTER, CLASS_SHOTGUN, CLASS_MACHINEGUN, CLASS_GRENADE,
               CLASS_ROCKET, CLASS_HYPER, CLASS_RAIL, CLASS_BFG]
CLASS_INDEX = {c: i for i, c in enumerate(CLASS_ORDER)}
N_CLASSES = len(CLASS_ORDER)

# The slow-refire weapons: railgun, super shotgun, rocket launcher, grenade
# launcher, shotgun, BFG (super shotgun and shotgun already share
# CLASS_SHOTGUN, see MZ_TO_CLASS below).  Used to isolate the cadence tell a
# judge actually reads off panel 5 -- rail/shotgun/rocket panels -- from the
# rapid-fire classes (blaster, machinegun, hyperblaster) whose sub-second
# refire cycle dominates the class-weighted intershot_cv average and dilutes
# it (see slow_cadence_cv in _compute_scalars).
SLOW_CLASSES = {CLASS_RAIL, CLASS_SHOTGUN, CLASS_ROCKET, CLASS_GRENADE,
                CLASS_BFG}

MZ_TO_CLASS = {
    MZ_BLASTER: CLASS_BLASTER,
    MZ_MACHINEGUN: CLASS_MACHINEGUN,
    MZ_SHOTGUN: CLASS_SHOTGUN,
    MZ_CHAINGUN1: CLASS_MACHINEGUN,
    MZ_CHAINGUN2: CLASS_MACHINEGUN,
    MZ_CHAINGUN3: CLASS_MACHINEGUN,
    MZ_RAILGUN: CLASS_RAIL,
    MZ_ROCKET: CLASS_ROCKET,
    MZ_GRENADE: CLASS_GRENADE,
    MZ_BFG: CLASS_BFG,
    MZ_SSHOTGUN: CLASS_SHOTGUN,
    MZ_HYPERBLASTER: CLASS_HYPER,
}

# Sustained-fire refire cycle per class, in seconds, DERIVED FROM THIS GAME'S
# SOURCE, not guessed from vanilla lore.  Weapon_Generic (p_weapon.c:417)
# runs one gunframe per server frame (0.1s) and, while +attack is held, the
# cycle is: READY sets gunframe = FRAME_FIRE_FIRST (= FRAME_ACTIVATE_LAST+1);
# the next tick fires; gunframe then advances one per tick until it reaches
# FRAME_IDLE_FIRST+1 (= FRAME_FIRE_LAST+2) at which point the state returns
# to READY; the tick after that re-arms.  That is
#     period_ticks = FRAME_FIRE_LAST - FRAME_ACTIVATE_LAST + 2
# from the Weapon_Generic(ent, ACTIVATE_LAST, FIRE_LAST, ...) arguments:
#     blaster   (4, 8)  ->  6 ticks = 0.6s
#     shotgun   (7,18)  -> 13 ticks = 1.3s      supershotgun (6,17) -> 1.3s
#     grenade launcher (5,16) -> 13 ticks = 1.3s
#     rocket    (4,12)  -> 10 ticks = 1.0s
#     railgun   (3,18)  -> 17 ticks = 1.7s
#     bfg       (8,32)  -> 26 ticks = 2.6s
# The three rapid-fire weapons do not use that path: Machinegun_Fire toggles
# gunframe 4<->5 (both fire frames), Chaingun_Fire cycles 15..21, and
# Weapon_HyperBlaster_Fire cycles 6..11 -- each fires on EVERY server tick
# while held, so their cycle is one tick, 0.1s.
#
# This is an instrument constant, identical on every sheet, so an error of a
# tick would cost readability and nothing else.  It is drawn as a reference
# line, never used to normalize a measured value.
CLASS_REFIRE_S = {
    CLASS_BLASTER: 0.6,
    CLASS_SHOTGUN: 1.3,
    CLASS_MACHINEGUN: 0.1,
    CLASS_GRENADE: 1.3,
    CLASS_ROCKET: 1.0,
    CLASS_HYPER: 0.1,
    CLASS_RAIL: 1.7,
    CLASS_BFG: 2.6,
}

# Fixed qualitative palette, one colour per class, in CLASS_ORDER.  Matches
# film.py's house style (saturated but not neon, readable on white).  Fixed
# for the instrument, so a colour always means the same weapon on every sheet.
CLASS_COLOR = {
    CLASS_BLASTER: '#8c8c00',
    CLASS_SHOTGUN: '#d95f02',
    CLASS_MACHINEGUN: '#7570b3',
    CLASS_GRENADE: '#1b9e77',
    CLASS_ROCKET: '#c0392b',
    CLASS_HYPER: '#e7298a',
    CLASS_RAIL: '#2166ac',
    CLASS_BFG: '#66a61e',
}

# ---------------------------------------------------------- temp entities
TE_BLOOD = 1

# Structured field layout per TE_ type.  The BYTE COUNTS here are exactly
# dm2speed.parse_temp_entity's audited shape table -- that audit (dm2speed.py
# lines 120-153, 2026-08-03, against this game DLL's actual gi.Write* call
# sites) is the expensive part and it is already done.  The only thing added
# is which bytes are a position and which are a direction, so a TE can be
# decoded instead of skipped.  _assert_shapes_match() below verifies at
# import time that no entry here consumes a different number of bytes than
# the audited table; if it ever does, this module refuses to load rather than
# silently desyncing every block after the first temp entity.  Wire-format
# work has bitten this toolbox twice (the EFFECTS8|EFFECTS16 4-byte read, the
# TE shape corrections), and this assertion is the cheap insurance.
POS, DIR, ENT, BYTE = 'POS', 'DIR', 'ENT', 'BYTE'
_FIELD_SIZE = {POS: 6, DIR: 1, ENT: 2, BYTE: 1}

TE_FIELDS = {
    0: (POS, DIR),                 # TE_GUNSHOT
    1: (POS, DIR),                 # TE_BLOOD           <-- the hit signal
    2: (POS, DIR),                 # TE_BLASTER
    3: (POS, POS),                 # TE_RAILTRAIL       start, end
    4: (POS, DIR),                 # TE_SHOTGUN
    5: (POS,), 6: (POS,), 7: (POS,), 8: (POS,),
    9: (POS, DIR),                 # TE_SPARKS
    10: (BYTE, POS, DIR, BYTE),    # TE_SPLASH
    11: (POS, POS),                # TE_BUBBLETRAIL
    12: (POS, DIR), 13: (POS, DIR), 14: (POS, DIR),
    15: (BYTE, POS, DIR, BYTE),    # TE_LASER_SPARKS
    16: (ENT, POS, POS, POS),      # TE_PARASITE_ATTACK (monster, unverified)
    17: (POS,), 18: (POS,), 19: (POS,), 20: (POS,), 21: (POS,),
    22: (POS, POS),                # TE_BOSSTPORT       (monster, unverified)
    23: (POS, POS),                # TE_BFG_LASER
    24: (ENT, POS, POS, POS),      # TE_GRAPPLE_CABLE
    25: (POS,), 26: (POS, DIR), 27: (POS, DIR), 28: (POS,), 29: (POS,),
    30: (POS, DIR), 31: (POS,), 32: (POS,), 33: (POS,),
}


def _assert_shapes_match():
    """Byte-for-byte agreement with dm2speed's audited skip table.

    Import-time, not test-time, on purpose: a mismatch here does not produce
    a wrong number, it produces a desynced byte stream and silently different
    coverage on whichever demos happen to contain that TE type -- which is
    precisely the asymmetric-extraction failure the leak checklist's L6
    exists to prevent."""
    ref = {
        0: 7, 1: 7, 2: 7, 3: 12, 4: 7, 5: 6, 6: 6, 7: 6, 8: 6, 9: 7,
        10: 9, 11: 12, 12: 7, 13: 7, 14: 7, 15: 9, 16: 20, 17: 6, 18: 6,
        19: 6, 20: 6, 21: 6, 22: 12, 23: 12, 24: 20, 25: 6, 26: 7, 27: 7,
        28: 6, 29: 6, 30: 7, 31: 6, 32: 6, 33: 6,
    }
    for t, fields in TE_FIELDS.items():
        got = sum(_FIELD_SIZE[f] for f in fields)
        if ref.get(t) != got:
            raise AssertionError(
                f"fightsheet TE shape for type {t} consumes {got} bytes but "
                f"dm2speed's audited table consumes {ref.get(t)}")
    if set(ref) != set(TE_FIELDS):
        raise AssertionError("fightsheet TE table does not cover the same "
                             "types as dm2speed's audited table")


_assert_shapes_match()


# ------------------------------------------------------------- engagements
ENGAGE_RADIUS = 1200.0     # u; two opposite-team players closer than this are
                           # "in contact" -- a candidate engagement
ENGAGE_GAP_S = 2.0         # contact may lapse this long without ending it
ENGAGE_MIN_S = 1.0         # shorter contacts are passing traffic, not fights
TARGET_FOV_DEG = 35.0      # half-width of the cone a shot is attributed in
REAR_ACQUIRE_CONE_DEG = 120.0  # full width of the forward-facing cone used by
                           # rear_acquire() -- matches combat's own firing
                           # cone per TRIALS.md's sg_beliefcone note
                           # (sg_arach.c: belief is meant to stay WIDER than
                           # the 120-degree firing cone). A target more than
                           # half this, 60 degrees, off the shooter's view
                           # yaw counts as acquired from outside the cone.
HIT_RADIUS = 64.0          # u; TE_BLOOD this close to a player = that player
                           # was hit (player origin is box centre, blood
                           # spawns on the box surface ~<32u away)
HIT_ATTRIB_WINDOW_S = 1.5  # a TE_BLOOD may trail the shot that plausibly
                           # caused it by up to this long and still be
                           # credited to it. Wide enough to cover a slow
                           # projectile's flight time (rocket/grenade travel
                           # well under a second at ctf engagement ranges)
                           # plus the +/-1 frame slop the 10Hz snapshot grid
                           # already introduces into every event timestamp
                           # (see EVENT TIMING in walk_demo_events); tight
                           # enough that an unrelated lava tick or a second,
                           # separate fight rarely drifts inside it.
HIT_ATTRIB_WINDOW_FRAMES = int(round(HIT_ATTRIB_WINDOW_S * F.FPS))
DEATH_LOOKAHEAD_S = 6.0    # a respawn this soon after a fight ends means the
                           # fight ended in a death
DISENGAGE_WINDOW_S = 3.0   # trailing window drawn in panel 4
BREAK_SEPARATION_U = 900.0 # separation at fight end above which, absent a
                           # death, someone deliberately left
BREAK_GROWTH = 1.6         # or: final separation this multiple of the window
                           # minimum, which catches a break at closer range
CONTACT_TAIL_S = 1.0       # both players must still be sampled this soon
                           # after the end for "broke off" vs "lost contact"

INTERSHOT_MAX_S = 2.0      # right edge of the fire-discipline axis
INTERSHOT_BINS = 20        # 0.1s bins -- the server tick, so every possible
                           # interval lands in its own bin
RANGE_MAX_U = 1600.0       # right edge of the range-at-fire axis
RANGE_BINS = 32            # 50u bins
RANGE_MIN_SHOTS = 12       # a weapon class with fewer attributed ranges than
                           # this gets no curve on panel 2. Below about a
                           # dozen samples a 50u-binned density is spikes, and
                           # a spike is easy to mistake for a preferred range.
                           # Same rule as routesheet's ROUTE_MIN_TRANSITIONS
                           # and it has the same property: the panel's shape
                           # is fixed (the legend lists all eight classes on
                           # every sheet) so dropping a curve does not change
                           # the geometry (L3/L7).

TIMELINE_ROWS = 12         # FIXED, padded (L3)
DISENGAGE_SLOTS = 8        # FIXED, padded (L3)

DISENGAGE_CLASSES = ['killed', 'broke off', 'lost contact']
DISENGAGE_COLOR = {'killed': '#c0392b', 'broke off': '#2166ac',
                   'lost contact': '#9a9a9a'}

# ------------------------------------------------------------ figure setup
GRID_COLS = 24             # divisible by 8, so the eight weapon-class slots
                           # and the eight disengage slots both tile exactly
ROW_HEIGHTS = [4.6, 2.8, 1.9, 2.0, 2.6, 1.3]
FIGSIZE = (12, 17.0)
FIGDPI = 140

# Fixed axis ceilings.  Every scale on this sheet is a CONSTANT, never a
# function of this demo's own values, so content can never change
# units-per-pixel between two sheets (L7/L8).  A value above its ceiling is
# CLIPPED, and the fact that clipping can happen is stated in the
# always-present notes strip -- never in a note that appears only on the
# sheets where it happened (L2).
RANGE_DENSITY_YMAX = 0.0050     # per world unit (a 50u bin holding every
                                # shot would read 0.020, so this is a real
                                # ceiling with headroom, not the maximum)
INTERSHOT_DENSITY_YMAX = 12.0   # per second. A 0.1s bin holding every
                                # interval reads exactly 10.0, so nothing can
                                # clip here -- the ceiling is the scale.
ROSE_RMAX = 0.16                # fraction of engagements per 15-deg sector
SEPARATION_YMAX = ENGAGE_RADIUS

TEAM_COLOR = F.TEAM_COLOR


# ============================================================ parsing layer
def parse_temp_entity_full(r):
    """svc_temp_entity, decoded instead of skipped.

    Returns (te_type, positions, dirs) where positions is a list of (x,y,z)
    floats in world units and dirs a list of packed direction bytes.  Byte
    consumption is identical to dm2speed.parse_temp_entity for every type
    (enforced by _assert_shapes_match), so swapping this in cannot change
    where the reader ends up in the block.

    An unknown type raises, exactly as the skipping version does -- the block
    is then abandoned by the caller's handler, which is the correct response
    to a stream we can no longer trust the position of."""
    t = r.u8()
    fields = TE_FIELDS.get(t)
    if fields is None:
        raise ValueError(f"unknown TE {t}")
    positions = []
    dirs = []
    for f in fields:
        if f is POS:
            positions.append((r.s16() / 8.0, r.s16() / 8.0, r.s16() / 8.0))
        elif f is DIR:
            dirs.append(r.u8())
        elif f is ENT:
            r.skip(2)
        else:
            r.skip(1)
    return t, positions, dirs


def parse_delta_entity_fight(r, bits, o, is_svrecord=False):
    """F.parse_delta_entity_film with ONE line changed: the U_EVENT byte is
    captured into o[5] instead of being skipped.

    Everything else -- field order, the EFFECTS8|EFFECTS16 4-byte rule, the
    serverrecord zero-reference handling for effects and yaw -- is identical,
    and F.parse_delta_entity_film's docstring is the reference for why each
    of those is the way it is.  Read it before touching this.

    Duplicating a wire parser is a real cost and it is taken deliberately:
    the alternative is editing film.py, and film.py is the frozen rung-1
    instrument that four judge sets were rendered by.  The duplication is
    kept honest by --verify-parser, which requires this walker to produce
    frame counts and per-entity sample counts identical to F.walk_demo's on
    every demo before any sheet may be rendered -- so a drift between the two
    parsers is a loud failure, not a silent one.

    THE EVENT FIELD IS ONE-SHOT, and is handled the way the vanilla client
    handles it, on both demo shapes without a branch: it is reset to 0 at the
    top of every delta and set only when the U_EVENT bit is present.  That is
    correct for both shapes for the same reason -- MSG_WriteDeltaEntity sets
    the bit whenever `to->event` is nonzero, without comparing it to the
    `from` state, precisely because an event describes one frame rather than
    a persistent value.  So unlike effects and yaw (see the film.py
    docstring), event needs no serverrecord special case."""
    if is_svrecord:
        o[3] = 0
        o[4] = 0.0
    o[5] = 0
    if bits & F.U_MODEL: r.skip(1)
    if bits & F.U_MODEL2: r.skip(1)
    if bits & F.U_MODEL3: r.skip(1)
    if bits & F.U_MODEL4: r.skip(1)
    if bits & F.U_FRAME8: r.skip(1)
    if bits & F.U_FRAME16: r.skip(2)
    if (bits & F.U_SKIN8) and (bits & F.U_SKIN16): r.skip(4)
    elif bits & F.U_SKIN8: r.skip(1)
    elif bits & F.U_SKIN16: r.skip(2)
    if (bits & F.U_EFFECTS8) and (bits & F.U_EFFECTS16):
        o[3] = r.u32()
    elif bits & F.U_EFFECTS8:
        o[3] = r.u8()
    elif bits & F.U_EFFECTS16:
        o[3] = r.u16()
    if (bits & F.U_RENDERFX8) and (bits & F.U_RENDERFX16): r.skip(4)
    elif bits & F.U_RENDERFX8: r.skip(1)
    elif bits & F.U_RENDERFX16: r.skip(2)
    if bits & F.U_ORIGIN1: o[0] = r.s16() / 8.0
    if bits & F.U_ORIGIN2: o[1] = r.s16() / 8.0
    if bits & F.U_ORIGIN3: o[2] = r.s16() / 8.0
    if bits & F.U_ANGLE1: r.skip(1)
    if bits & F.U_ANGLE2: o[4] = r.u8() * (360.0 / 256.0)
    if bits & F.U_ANGLE3: r.skip(1)
    if bits & F.U_OLDORIGIN: r.skip(6)
    if bits & F.U_SOUND: r.skip(1)
    if bits & F.U_EVENT: o[5] = r.u8()          # <-- the one changed line
    if bits & F.U_SOLID: r.skip(2)


def walk_demo_events(path, maxplayers=32, capture_events=True):
    """A sibling of film.py's walk_demo that also captures the event stream.

    The entity side is not reimplemented: this calls F.parse_delta_entity_film
    for every delta, so tracks and yaws come out of the identical code rung 1
    and rung 2 use, and `capture_events=False` reproduces walk_demo's exact
    message dispatch (svc 1 skipped as 3 bytes, svc 3 skipped by shape).  That
    flag exists so --verify-parser can A/B the two paths on the same file and
    show the error counters do not rise when events are captured, which §2.2
    of the design makes a mandatory precondition for rendering any sheet.

    Returns walk_demo's dict plus:
      'events': [ {'kind': 'shot',  'frame', 'ent', 'mz', 'silenced'},
                  {'kind': 'blood', 'frame', 'pos': (x,y,z)} ]
      'respawns': [ {'frame', 'ent'} ]   -- EV_PLAYER_TELEPORT, the death
                                            signal; rides the entity stream,
                                            so it is listed separately from
                                            the multicast events
      'te_counts': {te_type: n}      -- parser health, sidecar only
      'block_errors': n              -- blocks abandoned mid-parse
      'blocks': n

    EVENT TIMING.  An event is stamped with the frame index most recently
    completed by a packetentities snapshot, i.e. the 10Hz position grid the
    rest of the toolbox already runs on.  Within one demo message the
    multicast datagram and the entity snapshot can be written in either
    order, so an event can land one frame early or late; that granularity is
    the same on both demo shapes and no panel here resolves finer than a
    server tick.

    WHAT IS DISCARDED AT THIS BOUNDARY, and never reaches any consumer:
    svc_muzzleflash2 (monsters), MZ_LOGOUT, MZ_LOGIN, MZ_RESPAWN and
    MZ_ITEMRESPAWN.  Discarding here rather than downstream means no panel
    can accidentally grow a dependency on them."""
    data = open(path, 'rb').read()
    off = 0
    mapname = None
    skins = {}
    ents = {}
    tracks = {}
    yaws = {}
    events = []
    respawns = []
    te_counts = collections.Counter()
    frame_idx = 0
    svrecord = None
    block_errors = 0
    blocks = 0

    def read_packetentities():
        while True:
            bits, num = D.parse_entity_bits(r)
            if num == 0:
                break
            if bits & F.U_REMOVE:
                ents.pop(num, None)
                continue
            o = ents.setdefault(num, [0.0, 0.0, 0.0, 0, 0.0, 0])
            parse_delta_entity_fight(r, bits, o, is_svrecord=bool(svrecord))

    def snapshot():
        nonlocal frame_idx
        frame_idx += 1
        for num, o in ents.items():
            if 1 <= num <= maxplayers:
                tracks.setdefault(num, []).append(
                    (frame_idx, o[0], o[1], o[2], o[3]))
                yaws.setdefault(num, {})[frame_idx] = o[4]
                if o[5] == EV_PLAYER_TELEPORT:
                    respawns.append({'frame': frame_idx, 'ent': num})

    while off + 4 <= len(data):
        (mlen,) = struct.unpack_from('<i', data, off)
        off += 4
        if mlen == -1:
            break
        r = D.R(data[off:off + mlen])
        off += mlen
        blocks += 1
        try:
            while not r.done():
                svc = r.u8()
                if svc == 12:
                    r.skip(9); r.str_(); pn = r.u16(); r.str_()
                    svrecord = (pn == 0xffff)
                elif svc == 13:
                    idx = r.u16()
                    s = r.str_()
                    if 1312 <= idx < 1312 + 256:
                        skins[idx - 1312] = s
                    elif idx == 33:
                        m = F.re.match(r'maps/(\w+)\.bsp', s)
                        if m:
                            mapname = m.group(1)
                elif svc == 14:
                    bits, num = D.parse_entity_bits(r)
                    o = ents.setdefault(num, [0.0, 0.0, 0.0, 0, 0.0, 0])
                    parse_delta_entity_fight(r, bits, o)
                elif svc == 20:
                    if svrecord:
                        r.skip(4)
                        svc2 = r.u8()
                        if svc2 != 18:
                            raise ValueError(
                                f"svrecord frame not followed by "
                                f"packetentities (got {svc2})")
                        read_packetentities()
                        snapshot()
                    else:
                        r.skip(9)
                        ab = r.u8(); r.skip(ab)
                elif svc == 17:
                    parse_playerstate_full(r, {})
                elif svc == 18:
                    read_packetentities()
                    snapshot()
                elif svc == 9:
                    D.parse_sound(r)
                elif svc == 3:
                    if capture_events:
                        t, positions, _dirs = parse_temp_entity_full(r)
                        te_counts[t] += 1
                        if t == TE_BLOOD and positions:
                            events.append({'kind': 'blood',
                                           'frame': frame_idx,
                                           'pos': positions[0]})
                    else:
                        D.parse_temp_entity(r)
                elif svc == 1:
                    if capture_events:
                        entnum = r.u16()
                        mz = r.u8()
                        base = mz & 0x7f
                        if base in MZ_DISCARD:
                            pass
                        elif base in MZ_TO_CLASS:
                            events.append({'kind': 'shot',
                                           'frame': frame_idx,
                                           'ent': entnum, 'mz': base,
                                           'silenced': bool(mz & MZ_SILENCED)})
                    else:
                        r.skip(3)
                elif svc == 2:
                    # monster muzzle flashes -- discarded entirely (§0.2)
                    r.skip(3)
                elif svc == 10:
                    r.skip(1); r.str_()
                elif svc in (11, 15, 4):
                    r.str_()
                elif svc == 5:
                    r.skip(512)
                elif svc in (6, 7):
                    pass
                else:
                    raise ValueError(svc)
        except Exception:
            block_errors += 1
            continue
    return {'map': mapname, 'skins': skins, 'tracks': tracks, 'yaws': yaws,
            'frames': frame_idx, 'svrecord': bool(svrecord),
            'events': events, 'respawns': respawns,
            'te_counts': dict(te_counts),
            'block_errors': block_errors, 'blocks': blocks}


def forward_fill_yaws(d):
    """Make the view-yaw series mean the same thing on both demo shapes.

    A small symmetric normalization, and the measurement that sizes it is
    recorded here because the reasoning that motivated it was wrong and the
    correction is worth more than the guess.

    The worry was this: film.py's walker resets the yaw accumulator to 0.0 at
    the top of every serverrecord delta, because a serverrecord capture
    deltas every entity against an all-zero reference state (see
    parse_delta_entity_film's docstring).  On a serverrecord demo an absent
    U_ANGLE2 bit therefore reads as yaw 0, while on a client demo the same
    absent bit means "unchanged".  Since film.py's docstring reports the bit
    present on only 79.8% of serverrecord player updates, that looked like it
    would hand a fifth of all bot frames a spurious 0-degree view angle and
    none of the human ones -- which would matter here, because this rung
    reads the yaw at the exact frame of every shot and a bogus yaw produces a
    bogus target, range and aim offset on one corpus only.

    MEASURED, it is nothing like that.  Restricted to ROSTERED players -- the
    only entities any panel uses -- yaw reads exactly 0.0 on 0.95% of
    serverrecord samples and 0.82% of client samples (wave431 and
    lmctf-2022-02-08-mactf06-20.37, n = 81198 and 15703).  The 20% figure is
    dominated by the non-player entities that share the 1..32 entnum range
    and never carry an angle at all; F.anonymize drops every one of them.
    The reason the zero-reference does no harm is that the bit is omitted
    precisely when the quantized yaw IS zero, so "absent" and "zero" agree.

    So this function is kept, but as what it actually is: one rule applied to
    both shapes without a branch, which takes a 0.95%/0.82% residual to
    0%/0%.  A yaw of exactly 0.0 is treated as "not reported this frame" and
    the last reported yaw is carried forward.  The cost is that a genuine yaw
    in bin 0 of 256 is replaced by the previous one, on both corpora
    equally."""
    for ent, series in d.get('yaws', {}).items():
        last = None
        for f in sorted(series):
            v = series[f]
            if v == 0.0:
                if last is None:
                    continue
                series[f] = last
            else:
                last = v


def cap_events_to_duration(d):
    """F.cap_tracks_to_duration knows about tracks and yaws, not events.

    Called immediately after it, with the already-updated d['frames'], so the
    event stream shares the identical time budget every other quantity on the
    sheet is computed from."""
    cap = d['frames']
    d['events'] = [e for e in d['events'] if e['frame'] <= cap]
    d['respawns'] = [e for e in d['respawns'] if e['frame'] <= cap]


def apply_pov_parity_events(d, labels, pov_info, prepos):
    """Extend --pov-parity from the entity stream to the event stream (L5).

    Both multicast messages this rung reads are MULTICAST_PVS from a world
    origin: the shooter's origin for svc_muzzleflash (p_weapon.c:817 etc.)
    and the impact point for TE_BLOOD (g_combat.c:132).  A client demo
    therefore only contains the events whose origin was in the recording
    client's PVS, and a serverrecord demo contains all of them.  So the event
    stream needs the same treatment the entity stream gets, and it can share
    the recorder and radius F.apply_pov_parity already picked -- same virtual
    recorder, same sphere, one calibration.

    Respawns are handled differently and more simply, because they are not a
    multicast at all: EV_PLAYER_TELEPORT arrives on the entity's own delta,
    so it has ALREADY been scoped by whatever scoping the entity stream got.
    Filtering them to the frames that survived the entity gate reproduces
    that exactly, with no second radius test to calibrate -- a respawn is
    visible precisely when the respawning player is.

    `prepos` is the PRE-PARITY position index: the gate must be evaluated
    against the event's true world origin, which for a muzzle flash is the
    shooter's real position at that frame, not the punched-through copy that
    survives filtering.  (The two agree wherever it matters: a flash passes
    the gate exactly when the shooter's own sample at that frame also passes
    it, so a surviving shot always has a surviving shooter position.)

    The recorder's own events are always kept -- distance zero from itself --
    so there is no self-asymmetry, exactly as in the entity gate."""
    if not pov_info.get('applied'):
        return {'applied': False}
    rec_ent = pov_info['pov_entnum']
    radius = pov_info['radius_u']
    r2 = radius * radius
    rec = {s[0]: (s[1], s[2], s[3]) for s in d['tracks'].get(rec_ent, [])}

    before = len(d['events'])
    kept = []
    for e in d['events']:
        if e['kind'] == 'blood':
            org = e['pos']
        else:
            if e['ent'] == rec_ent:
                kept.append(e)
                continue
            org = prepos.get(e['ent'], {}).get(e['frame'])
            if org is None:
                # No position for the emitter at this frame means we cannot
                # evaluate the gate.  Dropping is the conservative choice: it
                # matches what a client demo does with an event whose source
                # it could not see.
                continue
        p = rec.get(e['frame'])
        if p is None:
            continue
        dx, dy, dz = org[0] - p[0], org[1] - p[1], org[2] - p[2]
        if dx * dx + dy * dy + dz * dz > r2:
            continue
        kept.append(e)
    d['events'] = kept

    surviving = {n: {s[0] for s in t} for n, t in d['tracks'].items()}
    rb = len(d['respawns'])
    d['respawns'] = [e for e in d['respawns']
                     if e['frame'] in surviving.get(e['ent'], ())]
    return {'applied': True, 'pov_entnum': rec_ent, 'radius_u': radius,
            'events_before': before, 'events_after': len(kept),
            'event_keep_fraction': (len(kept) / before) if before else 0.0,
            'respawns_before': rb, 'respawns_after': len(d['respawns'])}


# ================================================================ analysis
def position_index(tracks, ents=None):
    """{entnum: {frame: (x, y, z)}} -- events carry a frame, not a position,
    so every geometric question about an event is answered through this."""
    out = {}
    for n, t in tracks.items():
        if ents is not None and n not in ents:
            continue
        out[n] = {s[0]: (s[1], s[2], s[3]) for s in t}
    return out


def _pos_near(idx_ent, frame, slack=1):
    """The entity's sampled position at `frame`, or at the nearest frame
    within +/-slack.  Not an interpolation: the wire gives no sub-tick
    timestamp, so inventing one would be false precision."""
    p = idx_ent.get(frame)
    if p is not None:
        return p
    for k in range(1, slack + 1):
        p = idx_ent.get(frame - k)
        if p is not None:
            return p
        p = idx_ent.get(frame + k)
        if p is not None:
            return p
    return None


def weapon_class(mz):
    return MZ_TO_CLASS.get(mz)


def split_events(d, labels):
    """Events, restricted to rostered players and tagged with weapon class.

    Non-rostered emitters (referees, spectators) are dropped here for the
    same reason F.anonymize drops their tracks: a sheet is about roster
    participants."""
    shots, bloods = [], []
    for e in d['events']:
        if e['kind'] == 'shot':
            if e['ent'] not in labels:
                continue
            c = weapon_class(e['mz'])
            if c is None:
                continue
            shots.append({'frame': e['frame'], 'ent': e['ent'], 'cls': c,
                          'silenced': e['silenced']})
        elif e['kind'] == 'blood':
            bloods.append({'frame': e['frame'], 'pos': e['pos']})
    respawns = [dict(e) for e in d['respawns'] if e['ent'] in labels]
    shots.sort(key=lambda s: (s['frame'], s['ent']))
    bloods.sort(key=lambda s: s['frame'])
    respawns.sort(key=lambda s: s['frame'])
    return shots, respawns, bloods


def attribute_shots(shots, posidx, yaws, teams, labels):
    """Give every shot a target, a range and an aim offset, where one can be
    argued for.

    The rule, identical on both demo shapes: among opposite-team players
    sampled at that frame, keep those whose bearing from the shooter is
    within TARGET_FOV_DEG of the shooter's captured view yaw, and take the
    nearest.  The yaw is a REAL view angle -- this mod writes the client's
    v_angle[YAW] into the entity's angles[YAW] at p_view.c:1033 -- not a
    movement heading, which is what makes this worth doing at all.

    THIS IS A HYPOTHESIS, NOT GROUND TRUTH, and it has exactly the status
    rung 1's captured/died/lost labels have (film.py MODULE NOTE 3).  A shot
    down a corridor at nobody, with an enemy incidentally in the cone behind
    a wall, is attributed to that enemy.  The notes strip says so on every
    sheet, in identical words, so a judge weighs it the same way on both
    corpora.  Shots with no candidate get no range and simply do not appear
    in the range panel; they still appear as shots everywhere else."""
    out = []
    for s in shots:
        f, e = s['frame'], s['ent']
        sp = posidx.get(e, {}).get(f)
        yaw = yaws.get(e, {}).get(f)
        rec = dict(s)
        rec['target'] = None
        rec['range'] = None
        rec['aim_off'] = None
        if sp is None or yaw is None:
            out.append(rec)
            continue
        best = None
        for o in labels:
            if o == e or teams.get(o) != _other_team(teams.get(e)):
                continue
            op = posidx.get(o, {}).get(f)
            if op is None:
                continue
            dx, dy = op[0] - sp[0], op[1] - sp[1]
            bearing = math.degrees(math.atan2(dy, dx))
            delta = abs((bearing - yaw + 180.0) % 360.0 - 180.0)
            if delta > TARGET_FOV_DEG:
                continue
            dist = math.sqrt(dx * dx + dy * dy +
                             (op[2] - sp[2]) * (op[2] - sp[2]))
            if best is None or dist < best[0]:
                best = (dist, o, delta)
        if best is not None:
            rec['range'] = best[0]
            rec['target'] = best[1]
            rec['aim_off'] = best[2]
        out.append(rec)
    return out


def _other_team(t):
    if t == 'red':
        return 'blue'
    if t == 'blue':
        return 'red'
    return None


def attribute_hits(bloods, shots, teams, posidx, labels):
    """A TE_BLOOD is attributed to the nearest rostered player within
    HIT_RADIUS of it at that frame -- that part is unchanged, and still
    correctly identifies the VICTIM: TE_BLOOD comes from exactly one call
    site (g_combat.c:553) and only fires when damage actually lands on a
    client or monster.

    What TE_BLOOD does NOT tell you is who or what caused it, and that is
    the artifact this function exists to fix.  T_Damage -- the one call
    site -- fires for a weapon hit, but also for lava/slime tick damage
    (p_view.c has no per-victim cooldown on it: it fires every 0.1s a
    player stands in it), for a rocket/grenade splashing everyone in its
    blast radius from one shot, for a rail slug piercing every stacked
    player it passes through, and for grapple contact damage.  Counting
    every blood event as "a shot landed" measured hits=1236 against
    shots=180 on a bot demo, with 47% of blood events having no preceding
    shot at all -- and that inflated count is what drives the "hit landed"
    triangle markers on the engagement timeline, which is not a cosmetic
    bug: judges have quoted "hit triangles over almost every bar" as
    evidence of bot-ness, when the triangles were mostly lava.

    So: a blood event is additionally attributed to a SHOT when a shot was
    fired by a player on the team opposite the victim, no more than
    HIT_ATTRIB_WINDOW_FRAMES frames before the blood frame (never after --
    a shot cannot cause damage that already happened), and the (shot,
    victim) pair has not already been used.  Ties among legal candidates
    resolve to the most recent shot, the more causally plausible one.

    The cap is on the (shot, victim) PAIR, not on the shot alone, on
    purpose: one shotgun blast or rail slug legitimately hitting three
    stacked players is three real hits and must stay three.  What the pair
    cap forecloses is a single shot being blamed for a whole run of
    unrelated blood ticks against the SAME victim -- e.g. that victim
    wandering through lava for the next second, which would otherwise ride
    the shot's attribution window and look like the shot kept landing.

    A blood event that finds no legal candidate shot is still returned,
    marked unattributed, rather than dropped -- so environmental damage
    stays visible in the totals (console/sidecar) instead of silently
    vanishing.  Requiring an opposite-team shooter means a friendly-fire
    hit on a server running FF on would show up unattributed too; accepted
    as the conservative side to be wrong on, since the alternative (any
    shot by anyone) reopens the door to crediting an unrelated bystander's
    shot for damage it did not cause.

    Returns a list of dicts: frame, victim, dist, attributed (bool),
    shot_frame, shot_ent, shot_idx -- the last three are None when
    unattributed.  shot_idx indexes into `shots` and is what the caller
    uses to dedupe multiple attributed hits from one splash shot down to a
    single "landed" marker."""
    shots_by_shooter_team = collections.defaultdict(list)
    for i, s in enumerate(shots):
        shots_by_shooter_team[teams.get(s['ent'])].append((i, s))

    used = set()   # (shot_idx, victim) pairs already spent
    out = []
    for b in bloods:
        f, p = b['frame'], b['pos']
        best = None
        for e in labels:
            q = _pos_near(posidx.get(e, {}), f)
            if q is None:
                continue
            dx, dy, dz = q[0] - p[0], q[1] - p[1], q[2] - p[2]
            dist = math.sqrt(dx * dx + dy * dy + dz * dz)
            if dist > HIT_RADIUS:
                continue
            if best is None or dist < best[0]:
                best = (dist, e)
        if best is None:
            continue
        dist, victim = best
        rec = {'frame': f, 'victim': victim, 'dist': dist,
               'attributed': False, 'shot_frame': None, 'shot_ent': None,
               'shot_idx': None}
        cand_team = _other_team(teams.get(victim))
        best_shot = None
        for i, s in shots_by_shooter_team.get(cand_team, []):
            if s['ent'] == victim:
                continue
            delta = f - s['frame']
            if delta < 0 or delta > HIT_ATTRIB_WINDOW_FRAMES:
                continue
            if (i, victim) in used:
                continue
            if best_shot is None or s['frame'] > best_shot[1]['frame']:
                best_shot = (i, s)
        if best_shot is not None:
            i, s = best_shot
            used.add((i, victim))
            rec['attributed'] = True
            rec['shot_frame'] = s['frame']
            rec['shot_ent'] = s['ent']
            rec['shot_idx'] = i
        out.append(rec)
    return out


def segment_engagements(tracks, labels, teams, shots):
    """Maximal intervals of opposite-team contact that somebody shot during.

    Contact = both players sampled in the same frame and within
    ENGAGE_RADIUS.  Runs of contact frames are joined across gaps up to
    ENGAGE_GAP_S (so a corner, or a PVS hole, does not chop one fight into
    five), kept when they last at least ENGAGE_MIN_S, and kept only if at
    least one weapon shot by either player falls inside.

    A player fighting two enemies at once appears in two engagements, and a
    shot inside both intervals is listed in both.  That is inherent to a
    pairwise definition of a fight and it is the same on both demo shapes."""
    gap = int(round(ENGAGE_GAP_S * F.FPS))
    minlen = int(round(ENGAGE_MIN_S * F.FPS))
    shots_by_ent = collections.defaultdict(list)
    for s in shots:
        shots_by_ent[s['ent']].append(s)

    arr = {}
    for n in labels:
        t = tracks.get(n, [])
        if not t:
            continue
        arr[n] = (np.array([s[0] for s in t], dtype=np.int64),
                  np.array([(s[1], s[2], s[3]) for s in t],
                           dtype=np.float64))

    out = []
    ents = sorted(labels)
    for i, a in enumerate(ents):
        for b in ents[i + 1:]:
            if teams.get(a) is None or teams.get(a) == teams.get(b):
                continue
            if a not in arr or b not in arr:
                continue
            fa, pa = arr[a]
            fb, pb = arr[b]
            common, ia, ib = np.intersect1d(fa, fb, assume_unique=True,
                                            return_indices=True)
            if not len(common):
                continue
            dv = pa[ia] - pb[ib]
            dist = np.sqrt((dv * dv).sum(axis=1))
            close = dist <= ENGAGE_RADIUS
            if not close.any():
                continue
            cf = common[close]
            cd = dist[close]
            splits = np.nonzero(np.diff(cf) > gap)[0]
            starts = np.concatenate(([0], splits + 1))
            ends = np.concatenate((splits, [len(cf) - 1]))
            for s0, s1 in zip(starts, ends):
                f0, f1 = int(cf[s0]), int(cf[s1])
                if f1 - f0 + 1 < minlen:
                    continue
                ev_shots = [s for e in (a, b) for s in shots_by_ent.get(e, [])
                            if f0 <= s['frame'] <= f1]
                if not ev_shots:
                    continue
                ev_shots.sort(key=lambda s: s['frame'])
                out.append({
                    'pair': (a, b), 'f0': f0, 'f1': f1,
                    'shots': ev_shots,
                    'sep_frames': cf[s0:s1 + 1].astype(int).tolist(),
                    'sep_dist': cd[s0:s1 + 1].astype(float).tolist(),
                })
    out.sort(key=lambda e: (-len(e['shots']), e['f0']))
    return out


def classify_disengage(eng, respawns, posidx, deaths_by_ent):
    """How did this fight end: killed, broke off, or lost contact?

    `killed` is decided by EV_PLAYER_TELEPORT -- a respawn by either fighter
    within DEATH_LOOKAHEAD_S of the end.  That is a real death signal off the
    wire (respawn() sets it at p_client.c:1642) and it is the correction to
    the design brief's MZ_LOGIN, which in this game marks a team join and not
    a death; see the module docstring.  film.py's >180u/tick teleport
    heuristic is kept alongside it, because the two are complementary: the
    event catches a respawn whose position jump the sampling missed, and the
    jump catches a respawn whose event frame was culled.  Both are shared
    with map teleporters, and both are equally so on either corpus.

    `broke off` requires that both players are still being sampled after the
    end -- otherwise we cannot tell a retreat from the recording simply
    losing sight of them, and calling that a retreat would credit the losing
    side of a fight to whichever demo shape has the worse coverage.  That
    distinction is the entire reason `lost contact` is a class."""
    a, b = eng['pair']
    f1 = eng['f1']
    look = int(round(DEATH_LOOKAHEAD_S * F.FPS))
    for rs in respawns:
        if rs['ent'] in (a, b) and f1 < rs['frame'] <= f1 + look:
            return 'killed'
    for e in (a, b):
        for df in deaths_by_ent.get(e, ()):
            if f1 < df <= f1 + look:
                return 'killed'

    tail = int(round(CONTACT_TAIL_S * F.FPS))
    seen_after = all(
        any(_pos_near(posidx.get(e, {}), f1 + k) is not None
            for k in range(1, tail + 1))
        for e in (a, b))
    if not seen_after:
        return 'lost contact'

    win = int(round(DISENGAGE_WINDOW_S * F.FPS))
    sep = [dd for ff, dd in zip(eng['sep_frames'], eng['sep_dist'])
           if ff >= f1 - win]
    if not sep:
        return 'lost contact'
    if sep[-1] >= BREAK_SEPARATION_U or sep[-1] >= BREAK_GROWTH * max(min(sep), 1.0):
        return 'broke off'
    return 'lost contact'


def approach_angle(eng, posidx):
    """Angle at the fight's first shot between the shooter's own direction of
    travel and the bearing to the other player.

    Near 0 means walking straight at the enemy; near +/-90 means moving
    across them, which is what fighting while moving looks like from above.
    The heading comes from consecutive position samples, so a player who was
    standing still at that instant has no heading and contributes nothing --
    the alternative, calling a stationary player's heading 0, would pile
    every camper into the straight-in bin."""
    s = eng['shots'][0]
    e, f = s['ent'], s['frame']
    other = eng['pair'][1] if eng['pair'][0] == e else eng['pair'][0]
    idx = posidx.get(e, {})
    p1 = idx.get(f)
    p0 = None
    for k in (1, 2, 3):
        p0 = idx.get(f - k)
        if p0 is not None:
            break
    op = posidx.get(other, {}).get(f)
    if p1 is None or p0 is None or op is None:
        return None
    hx, hy = p1[0] - p0[0], p1[1] - p0[1]
    if math.hypot(hx, hy) < 1.0:
        return None
    heading = math.degrees(math.atan2(hy, hx))
    bearing = math.degrees(math.atan2(op[1] - p1[1], op[0] - p1[0]))
    return (bearing - heading + 180.0) % 360.0 - 180.0


def rear_acquire(eng, posidx, yaws):
    """Whether the fight's opening shot was aimed at a target outside the
    shooter's forward REAR_ACQUIRE_CONE_DEG-wide cone -- a target the shooter
    had not turned to face before opening fire.

    FACING PROXY.  The design brief for this scalar asked for the shooter's
    movement heading over the prior 0.5s, on the assumption that view angles
    "may not be in the demo stream." They are: this mod writes the client's
    real v_angle[YAW] into the entity's angles[YAW] on the wire
    (p_view.c:1033) and `forward_fill_yaws` already normalizes the one
    serverrecord-only quirk in it (a zero-reference delta reads as an absent
    bit exactly when the true yaw quantizes to 0, so "absent" and "zero"
    agree on both demo shapes -- see that function's docstring for the
    measurement). `attribute_shots` already trusts this same series as
    ground truth for "what was this player looking at" when it assigns a
    shot's target. View yaw is used here instead of movement heading for the
    same reason: target ACQUISITION is a facing/vision-cone question, not a
    footwork one -- a player can walk one direction while looking another,
    and sg_beliefcone/sg_beliefrange gate on facing (a human-like FOV), not
    on where the feet are pointed. Movement heading (approach_angle, above)
    stays movement heading because that panel asks a different question --
    how a player WALKS into a fight -- and is left unchanged.

    Returns True (target outside the cone -- a "rear acquire"), False
    (target inside it), or None where the shot frame has no usable yaw or
    position for either player. Kept in that three-way shape, matching
    approach_angle, so a caller can drop the Nones without special-casing."""
    s = eng['shots'][0]
    e, f = s['ent'], s['frame']
    other = eng['pair'][1] if eng['pair'][0] == e else eng['pair'][0]
    sp = posidx.get(e, {}).get(f)
    yaw = yaws.get(e, {}).get(f)
    op = posidx.get(other, {}).get(f)
    if sp is None or yaw is None or op is None:
        return None
    bearing = math.degrees(math.atan2(op[1] - sp[1], op[0] - sp[0]))
    off_yaw = abs((bearing - yaw + 180.0) % 360.0 - 180.0)
    return off_yaw > (REAR_ACQUIRE_CONE_DEG / 2.0)


# --------------------------------------------------- rail-rhythm crossings
# sg_railrhythm (TRIALS.md #2) has no scalar that can see it: the doctrine
# it drives -- a bot timing a lane crossing against a believed railer's
# reload window -- fires in open movement, before any fight is detected, so
# it sits upstream of every engagement-scoped scalar above (approach_angle,
# classify_disengage). fightsheet has no notion of a lane or a sightline, so
# this is not the crossing itself, but the nearest proxy fightsheet's own
# structures reach: the CROSSING INTO CONTACT an engagement already marks --
# its start frame (an approach into the enemy's line) and, when a fight ends
# in a deliberate retreat, its end frame (crossing back OUT). Both are
# moments a player begins exposure to one specific opposing player, which is
# what the doctrine's timing is measured against.
RAIL_WINDOW_S = 0.7        # a crossing begun this soon after an enemy's rail
                            # shot counts as "timed" (spec). At
                            # CLASS_REFIRE_S[CLASS_RAIL] (1.7s, derived from
                            # the weapon's own gunframe cycle -- see that
                            # table's comment), a crossing with no knowledge
                            # of the cadence would land in this window about
                            # 0.7/1.7 = 41% of the time by chance -- the
                            # scalar is read against that null, not zero.
RAIL_MIN_CROSSINGS = 8     # per-team sample gate before a team's fraction is
                            # trusted -- the same threshold intershot_cv and
                            # slow_cadence_cv use (len(vals) < 8) for a
                            # per-class mean.


def rail_window_crossings(engs, teams, shots):
    """Per-team (qualify, total) crossing counts for rail_window_exposure.

    A CROSSING is the frame a player begins exposure to one specific
    opposing player: an engagement's start frame (e['f0']) or, only when the
    engagement ends in class == 'broke off' (classify_disengage), its end
    frame (e['f1']) as well. classify_disengage is decided at the PAIR level
    -- fightsheet does not record which of the two players initiated a
    break -- so both members of the pair are credited at both frames,
    exactly as every other pair-level fact on this sheet (disengage class,
    the engagement itself) already applies to both players alike.

    A crossing is ELIGIBLE only when the opposing player's held weapon class
    at the crossing frame is rail. There is no inventory signal on this
    wire, so "held" is read the only way the shot stream allows: the class
    of that player's most recent shot at or before the crossing frame (a
    player with no prior shot has no known held class and is not eligible).
    Because eligibility is defined by that shot, the shot making a crossing
    eligible for CLASS_RAIL IS the enemy's last rail shot -- there is no
    separate "find the last rail shot" step once held-class is read.

    A crossing QUALIFIES when it begins within RAIL_WINDOW_S seconds of that
    shot (0 <= time_since <= RAIL_WINDOW_S; a crossing cannot be timed
    against a shot that has not happened yet, so a negative gap cannot
    occur here -- the held-class lookup only ever looks backward).

    Returns {'red': (qualify, total), 'blue': (qualify, total)} -- counts,
    not a fraction, so the caller pools before dividing: 'weighted by
    crossing count' in the spec, not an unweighted mean of per-player
    fractions."""
    by_ent = collections.defaultdict(list)
    for s in shots:
        by_ent[s['ent']].append(s)
    for ent in by_ent:
        by_ent[ent].sort(key=lambda s: s['frame'])
    frames_by_ent = {ent: [s['frame'] for s in arr]
                     for ent, arr in by_ent.items()}

    def held_at(enemy, frame):
        arr = by_ent.get(enemy)
        fr = frames_by_ent.get(enemy)
        if not arr:
            return None, None
        i = bisect.bisect_right(fr, frame) - 1
        if i < 0:
            return None, None
        s = arr[i]
        return s['cls'], frame - s['frame']

    counts = {'red': [0, 0], 'blue': [0, 0]}
    for e in engs:
        a, b = e['pair']
        frames = [e['f0']]
        if e.get('class') == 'broke off':
            frames.append(e['f1'])
        for f in frames:
            for player, enemy in ((a, b), (b, a)):
                t = teams.get(player)
                if t not in counts:
                    continue
                cls, since_frames = held_at(enemy, f)
                if cls != CLASS_RAIL or since_frames is None:
                    continue
                counts[t][1] += 1
                if 0 <= (since_frames / F.FPS) <= RAIL_WINDOW_S:
                    counts[t][0] += 1
    return {t: tuple(v) for t, v in counts.items()}


def intershot_intervals(shots):
    """{class: [seconds]} -- gaps between consecutive shots by the same
    player with the same weapon class.

    Capped at INTERSHOT_MAX_S: beyond a couple of seconds the gap is not
    trigger discipline any more, it is the time between two different fights,
    and letting those in would fill the tail with match structure instead of
    firing behaviour."""
    per = collections.defaultdict(list)
    by = collections.defaultdict(list)
    for s in shots:
        by[(s['ent'], s['cls'])].append(s['frame'])
    for (ent, cls), fr in by.items():
        fr.sort()
        for i in range(1, len(fr)):
            dt = (fr[i] - fr[i - 1]) / F.FPS
            if 0 < dt <= INTERSHOT_MAX_S:
                per[cls].append(dt)
    return per


def weapon_switch_matrix(shots, teams, team):
    """Row-normalized transitions between weapon classes over each player's
    own consecutive shots, pooled across that team.

    Self-transitions are kept deliberately: a player who holds one weapon all
    match produces a matrix that is pure diagonal, and that is exactly the
    thing the panel is asking about."""
    counts = np.zeros((N_CLASSES, N_CLASSES), dtype=np.float64)
    by = collections.defaultdict(list)
    for s in shots:
        if teams.get(s['ent']) != team:
            continue
        by[s['ent']].append((s['frame'], s['cls']))
    for ent, seq in by.items():
        seq.sort()
        for i in range(1, len(seq)):
            counts[CLASS_INDEX[seq[i - 1][1]], CLASS_INDEX[seq[i][1]]] += 1
    P = np.zeros_like(counts)
    rs = counts.sum(axis=1)
    nz = rs > 0
    P[nz] = counts[nz] / rs[nz, None]
    return P, counts


def _density(values, lo, hi, bins):
    """Normalized histogram, or an all-zero one when there is nothing to
    show.  Returning zeros rather than None keeps every panel the same shape
    on a demo with no data for it (L2/L7)."""
    edges = np.linspace(lo, hi, bins + 1)
    if not len(values):
        return edges, np.zeros(bins, dtype=np.float64)
    h, _ = np.histogram(np.clip(values, lo, hi), bins=edges, density=True)
    return edges, h


def wasserstein_1d(a, b, lo, hi, bins=256):
    """1-D Wasserstein distance between two empirical samples, normalized to
    the [0,1] fraction of the axis, so it is comparable across sheets and
    bounded.  Computed off the CDFs on a fixed grid -- no sorting-order
    subtleties, and the grid is an instrument constant."""
    if len(a) < 5 or len(b) < 5:
        return None
    edges = np.linspace(lo, hi, bins + 1)
    ha, _ = np.histogram(np.clip(a, lo, hi), bins=edges)
    hb, _ = np.histogram(np.clip(b, lo, hi), bins=edges)
    ca = np.cumsum(ha) / ha.sum()
    cb = np.cumsum(hb) / hb.sum()
    return float(np.abs(ca - cb).sum() / bins)


# ------------------------------------------------------------- the scalars
SCALAR_KEYS = [
    'range_sep_rail_shotgun',
    'range_sep_mean_pairwise',
    'straight_in_mass',
    'brokeoff_share',
    'switch_diagonal_mass',
    'intershot_cv',
    'slow_cadence_cv',
    'mean_aim_offset_deg',
    'rail_window_exposure',
    'rear_acquire_share',
]

SCALAR_PANEL = {
    'range_sep_rail_shotgun': 'panel 2 (range-at-fire)',
    'range_sep_mean_pairwise': 'panel 2 (range-at-fire)',
    'straight_in_mass': 'panel 3 (approach rose)',
    'brokeoff_share': 'panel 4 (disengage traces)',
    'switch_diagonal_mass': 'panel 6 (weapon-switch matrix)',
    'intershot_cv': 'panel 5 (fire discipline)',
    'slow_cadence_cv': 'panel 5 (fire discipline, slow weapons only)',
    'mean_aim_offset_deg': '(no panel -- diagnostic scalar only)',
    'rail_window_exposure': '(no panel -- scalar only, see TRIALS.md #2 '
                            'sg_railrhythm)',
    'rear_acquire_share': '(no panel -- scalar only, see TRIALS.md #3 '
                          'sg_beliefcone/sg_beliefrange)',
}

# RULE 21 GUARD, declared in writing before any judge sees a sheet.
#
# Rule 21: never build or keep anything that makes the bots play worse in
# order to look human.  Two quantities on this rung could tempt exactly that
# fix, so they are labelled here, in the code, in the CLI output and in every
# sidecar:
#
#   intershot_cv        -- making bots fire raggedly to look human is
#                          deliberately degrading aim cadence.
#   mean_aim_offset_deg -- adding aim error to look human is the same
#                          category.
#
# Both are DIAGNOSTIC ONLY.  They may be read, reported and argued about;
# they must never be a target for a bot change.  The legitimate way either
# number moves is as a side effect of better play: bots that stop firing
# because they are repositioning, taking a better angle, or breaking line of
# sight produce ragged intervals for free.  If a judge convicts on one of
# these, the documented response is a declared accepted honest difference,
# not a bot change.
#
# The contrast worth noting: brokeoff_share (panel 4) is the opposite case.
# Retreating from a losing fight is straightforwardly better play, so a bot
# that gains it plays better AND films better, and it IS a legitimate target.
RULE21_DIAGNOSTIC_ONLY = {'intershot_cv', 'mean_aim_offset_deg'}
RULE21_NOTE = (
    "RULE 21 GUARD: the scalars marked [D] are DIAGNOSTIC ONLY and must "
    "never be a target for a bot change. Firing raggedly on purpose, or "
    "adding aim error, is playing worse to look human. Read them; do not "
    "aim at them.")


def analyze_demo(demo_path, pov_parity=False, pov_ent=None,
                 pov_radius=F.POV_RADIUS_DEFAULT,
                 pov_fov=F.POV_FOV_DEG_DEFAULT):
    """Everything --scalars and the renderer both need, computed once.

    Control flow mirrors F.render_sheet's and routesheet.analyze_demo's
    exactly -- refuse, cap, anonymize, parity-filter, re-anonymize -- because
    that ordering was debugged there and re-deriving it would be a good way to
    reintroduce a fixed bug.  The two additions are event-shaped: the event
    stream is capped alongside the tracks, and the parity filter is applied to
    it against a position index snapshotted BEFORE the tracks were filtered."""
    d = walk_demo_events(demo_path)
    uncapped = d['frames'] / F.FPS
    if uncapped < F.DURATION_MIN_S:
        raise F.DemoUndersampled(
            f"demo duration {uncapped:.1f}s is under the "
            f"{F.DURATION_MIN_S:.0f}s minimum sample threshold -- too short "
            f"to render reliable stats, skipped rather than producing a "
            f"misleading sheet")
    forward_fill_yaws(d)
    duration_capped, orig_duration = F.cap_tracks_to_duration(d)
    cap_events_to_duration(d)

    labels, teams = F.anonymize(d)
    prepos = position_index(d['tracks'])

    pov_info = {'applied': False}
    pov_ev_info = {'applied': False}
    if pov_parity:
        if not d['svrecord']:
            pov_info = {'applied': False,
                        'reason': 'not a serverrecord demo -- a client demo '
                                  'is already PVS-filtered by the engine'}
        else:
            pov_info = F.apply_pov_parity(d, labels, pov_ent=pov_ent,
                                          radius=pov_radius, fov_deg=pov_fov)
            pov_ev_info = apply_pov_parity_events(d, labels, pov_info, prepos)
            labels, teams = F.anonymize(d)

    tracks = d['tracks']
    posidx = position_index(tracks, ents=set(labels))
    shots, respawns, bloods = split_events(d, labels)
    shots = attribute_shots(shots, posidx, d['yaws'], teams, labels)
    hits = attribute_hits(bloods, shots, teams, posidx, labels)
    deaths_by_ent = {n: F.death_ticks(tracks.get(n, [])) for n in labels}

    engs = segment_engagements(tracks, labels, teams, shots)
    # Markers are built from ATTRIBUTED hits only (unattributed = no
    # plausible shot behind it -- see attribute_hits) so the timeline stops
    # drawing a triangle over every lava tick.
    hits_by_frame = collections.defaultdict(list)
    for h in hits:
        if h['attributed']:
            hits_by_frame[h['frame']].append(h)
    for e in engs:
        a, b = e['pair']
        eng_hits = [h for f in range(e['f0'], e['f1'] + 1)
                    for h in hits_by_frame.get(f, [])
                    if h['victim'] in (a, b)]
        # One splash/pierce shot may legitimately be attributed to several
        # victims (attribute_hits' per-(shot,victim) cap allows that), but
        # it still only "landed" once for the marker -- dedupe by shot so a
        # single rocket does not draw three stacked triangles.
        seen_shots = set()
        landed = []
        for h in sorted(eng_hits, key=lambda h: h['frame']):
            if h['shot_idx'] in seen_shots:
                continue
            seen_shots.add(h['shot_idx'])
            landed.append(h)
        e['hits'] = landed
        e['class'] = classify_disengage(e, respawns, posidx, deaths_by_ent)
        e['approach'] = approach_angle(e, posidx)
        e['rear_acquire'] = rear_acquire(e, posidx, d['yaws'])

    ranges_by_class = collections.defaultdict(list)
    for s in shots:
        if s['range'] is not None:
            ranges_by_class[s['cls']].append(s['range'])
    intervals = intershot_intervals(shots)
    approaches = [e['approach'] for e in engs if e['approach'] is not None]
    P_by_team, counts_by_team = {}, {}
    for t in ('red', 'blue'):
        P_by_team[t], counts_by_team[t] = weapon_switch_matrix(shots, teams, t)

    dis_counts = collections.Counter(e['class'] for e in engs)
    dis_total = sum(dis_counts.values())
    dis_mix = {c: (dis_counts.get(c, 0) / dis_total if dis_total else 0.0)
               for c in DISENGAGE_CLASSES}
    rail_crossings = rail_window_crossings(engs, teams, shots)

    # rear_acquire_share's per-PLAYER groups: the opening shooter of each
    # engagement (eng['shots'][0]['ent']) against whether that opening shot
    # was a rear acquire (see rear_acquire). Built here, once, rather than
    # inside _compute_scalars, so collect_scalars callers that only want the
    # pooled scalar and callers (this task's own gate run) that want the
    # per-player breakdown read the identical grouping.
    rear_by_shooter = collections.defaultdict(list)
    for e in engs:
        if e['rear_acquire'] is None:
            continue
        rear_by_shooter[e['shots'][0]['ent']].append(e['rear_acquire'])

    scalars = _compute_scalars(ranges_by_class, intervals, approaches,
                               dis_mix, dis_total, P_by_team, counts_by_team,
                               shots, rail_crossings, rear_by_shooter)
    coverage = F.coverage_stats(tracks, labels, d['frames'])
    return {
        'd': d, 'labels': labels, 'teams': teams, 'tracks': tracks,
        'posidx': posidx, 'shots': shots, 'respawns': respawns, 'hits': hits,
        'engagements': engs, 'ranges_by_class': ranges_by_class,
        'intervals': intervals, 'approaches': approaches,
        'P_by_team': P_by_team, 'counts_by_team': counts_by_team,
        'disengage_mix': dis_mix, 'disengage_total': dis_total,
        'rail_crossings': rail_crossings, 'rear_by_shooter': dict(rear_by_shooter),
        'scalars': scalars, 'coverage': coverage, 'pov_parity': pov_info,
        'pov_parity_events': pov_ev_info,
        'duration_capped': duration_capped,
        'duration_original_s': orig_duration,
    }


def _compute_scalars(ranges_by_class, intervals, approaches, dis_mix,
                     dis_total, P_by_team, counts_by_team, shots,
                     rail_crossings, rear_by_shooter):
    """The Stage A headline scalars.

    Every one is a shape statistic -- a dispersion, a distance between two
    densities, a fraction -- never a count, because counts are PVS-sensitive
    in both directions (film.py MODULE NOTE 10c) and would separate the two
    corpora for reasons that have nothing to do with play."""
    rail = ranges_by_class.get(CLASS_RAIL, [])
    sg = ranges_by_class.get(CLASS_SHOTGUN, [])
    w_rs = wasserstein_1d(rail, sg, 0.0, RANGE_MAX_U)

    pairs = []
    for i, ca in enumerate(CLASS_ORDER):
        for cb in CLASS_ORDER[i + 1:]:
            w = wasserstein_1d(ranges_by_class.get(ca, []),
                               ranges_by_class.get(cb, []), 0.0, RANGE_MAX_U)
            if w is not None:
                pairs.append(w)
    w_mean = float(np.mean(pairs)) if pairs else None

    straight = (float(np.mean([abs(a) < 30.0 for a in approaches]))
                if approaches else None)

    # Inter-shot dispersion: the per-class coefficient of variation, weighted
    # by how many intervals each class contributed.  Pooling the raw
    # intervals across classes instead would measure the weapon MIX, not the
    # trigger discipline, because a rail interval and a chaingun interval
    # differ by an order of magnitude for reasons of hardware.
    cvs, wts = [], []
    for c, vals in intervals.items():
        if len(vals) < 8:
            continue
        m = float(np.mean(vals))
        if m <= 0:
            continue
        cvs.append(float(np.std(vals)) / m)
        wts.append(len(vals))
    cv = (float(np.average(cvs, weights=wts)) if cvs else None)

    # Same maths as intershot_cv, restricted to the slow-refire classes
    # (SLOW_CLASSES): rail, shotgun, rocket, grenade, bfg.  intershot_cv pools
    # every class weighted by shot count, and the rapid-fire classes
    # (blaster, machinegun, hyperblaster) throw far more shots than the slow
    # ones, so they dominate that average and dilute the ragged-cadence tell
    # a judge actually reads off the rail/shotgun/rocket panels.
    slow_cvs, slow_wts = [], []
    for c, vals in intervals.items():
        if c not in SLOW_CLASSES or len(vals) < 8:
            continue
        m = float(np.mean(vals))
        if m <= 0:
            continue
        slow_cvs.append(float(np.std(vals)) / m)
        slow_wts.append(len(vals))
    slow_cv = (float(np.average(slow_cvs, weights=slow_wts))
               if slow_cvs else None)

    aim = [s['aim_off'] for s in shots if s['aim_off'] is not None]
    aim_mean = float(np.mean(aim)) if len(aim) >= 8 else None

    diag = []
    for t in ('red', 'blue'):
        cnt = counts_by_team[t]
        tot = cnt.sum()
        if tot > 0:
            diag.append(float(np.trace(cnt) / tot))
    diag_mass = float(np.mean(diag)) if diag else None

    # rail_window_exposure: per-team fraction of eligible crossings (see
    # rail_window_crossings) begun within RAIL_WINDOW_S of the opposing
    # player's last rail shot, pooled within each team (not averaged
    # per-player, so a team with one railer-heavy pair does not get diluted
    # by teammates who never crossed an armed rail), then averaged red/blue
    # so the final scalar does not favour whichever team happened to draw
    # more rail crossings this demo.
    rail_fracs = []
    for t in ('red', 'blue'):
        q, tot = rail_crossings.get(t, (0, 0))
        if tot >= RAIL_MIN_CROSSINGS:
            rail_fracs.append(q / tot)
    rail_exposure = float(np.mean(rail_fracs)) if rail_fracs else None

    # rear_acquire_share: same shape as slow_cadence_cv -- a per-group mean,
    # then a group-count-weighted average across groups -- except the group
    # is a PLAYER (the opening shooter of an engagement, see rear_by_shooter
    # above) rather than a weapon class. Per spec: "the share per player,
    # aggregated per demo." Pooling every episode-start across all shooters
    # before dividing, instead of this per-player-then-weighted-average
    # step, would let a single trigger-happy shooter's episode count dominate
    # the demo number; averaging per player and weighting by how many
    # episode-starts each contributed keeps one prolific player from
    # swamping teammates who opened fewer fights, same reasoning as
    # intershot_cv's per-class weighting.
    rear_shares, rear_wts = [], []
    for ent, vals in rear_by_shooter.items():
        if len(vals) < 8:
            continue
        rear_shares.append(float(np.mean(vals)))
        rear_wts.append(len(vals))
    rear_share = (float(np.average(rear_shares, weights=rear_wts))
                 if rear_shares else None)

    return {
        'range_sep_rail_shotgun': w_rs,
        'range_sep_mean_pairwise': w_mean,
        'straight_in_mass': straight,
        'brokeoff_share': (dis_mix['broke off'] if dis_total else None),
        'switch_diagonal_mass': diag_mass,
        'intershot_cv': cv,
        'slow_cadence_cv': slow_cv,
        'mean_aim_offset_deg': aim_mean,
        'rail_window_exposure': rail_exposure,
        'rear_acquire_share': rear_share,
    }


# ==================================================================== draw
def _blank_axes(ax, title, xlabel=None, ylabel=None):
    ax.set_title(title, fontsize=8, pad=3)
    if xlabel:
        ax.set_xlabel(xlabel, fontsize=7)
    if ylabel:
        ax.set_ylabel(ylabel, fontsize=7)
    ax.tick_params(labelsize=6)
    for s in ('top', 'right'):
        ax.spines[s].set_visible(False)


def draw_engagement_timeline(ax, engs, frames):
    """Panel 1 -- the panel a judge actually reads.

    One row per engagement, TIMELINE_ROWS rows always: the busiest fights by
    shot count, then laid out top to bottom in the order they happened so the
    panel reads as a timeline, and padded with empty rows.  x is normalized
    match time, never seconds (L1).  Within a row: the pair's separation as a
    faint background trace scaled over 0..ENGAGE_RADIUS, a tick per shot
    coloured by weapon class, and a filled triangle per landed hit -- ATTRIBUTED
    hits only (attribute_hits), one triangle per shot even when that shot's
    splash is attributed to several victims, so this panel cannot be read as
    "hit triangle over almost every bar" off environmental damage that was
    never a weapon hit in the first place.

    Selecting by shot count and DRAWING in time order are deliberately
    different things: which fights get a slot must not depend on when they
    happened, but once chosen, sorting the rows by start time costs nothing
    and turns a scatter of blobs into something a reader can follow."""
    engs = sorted(engs[:TIMELINE_ROWS], key=lambda e: e['f0'])
    ax.set_xlim(0.0, 1.0)
    ax.set_ylim(TIMELINE_ROWS, 0)
    ax.set_yticks([i + 0.5 for i in range(TIMELINE_ROWS)])
    ax.set_yticklabels([str(i + 1) for i in range(TIMELINE_ROWS)], fontsize=6)
    ax.set_xticks(np.linspace(0, 1, 11))
    ax.tick_params(labelsize=6)
    ax.set_xlabel('normalized match time', fontsize=7)
    ax.set_ylabel('engagement slot', fontsize=7)
    ax.set_title('1. engagement timeline  '
                 '(bar = shot, coloured by weapon class; triangle = hit '
                 'landed; grey trace = separation, 0-1200u)', fontsize=8,
                 pad=4)
    ax.grid(axis='x', color='#eeeeee', lw=0.6)
    for s in ('top', 'right'):
        ax.spines[s].set_visible(False)

    denom = float(max(frames, 1))
    for i in range(TIMELINE_ROWS):
        ax.axhline(i, color='#e8e8e8', lw=0.5)
        if i >= len(engs):
            continue
        e = engs[i]
        top, bot = i + 0.08, i + 0.92
        xs = np.array(e['sep_frames'], dtype=np.float64) / denom
        ys = np.clip(np.array(e['sep_dist']), 0, SEPARATION_YMAX)
        ys = bot - (ys / SEPARATION_YMAX) * (bot - top)
        ax.plot(xs, ys, color='#b8b8b8', lw=0.7, zorder=1)
        for s in e['shots']:
            x = s['frame'] / denom
            ax.plot([x, x], [i + 0.55, i + 0.95],
                    color=CLASS_COLOR[s['cls']], lw=0.9, zorder=3)
        if e['hits']:
            hx = [h['frame'] / denom for h in e['hits']]
            ax.plot(hx, [i + 0.30] * len(hx), marker='^', ls='none',
                    ms=3.0, color='#111111', zorder=4)

    handles = [Line2D([], [], color=CLASS_COLOR[c], lw=1.6, label=c)
               for c in CLASS_ORDER]
    handles.append(Line2D([], [], color='#111111', marker='^', ls='none',
                          ms=3.5, label='hit landed'))
    ax.legend(handles=handles, loc='upper center',
              bbox_to_anchor=(0.5, -0.085), ncol=9, fontsize=6,
              frameon=False, handlelength=1.4, columnspacing=1.1)


def draw_range_density(ax, ranges_by_class):
    """Panel 2 -- range-at-fire, one normalized density per weapon class.

    The tell this is built for: a player who picks a weapon for the distance
    produces seven or eight curves in different places -- rail long, shotgun
    point-blank, rockets mid with a lobe at their own feet -- while a player
    who fires whatever is in hand at whatever range they happen to be at
    produces curves that all lie on top of each other.  The scalar version of
    that sentence is range_sep_mean_pairwise."""
    _blank_axes(ax, '2. range at fire, per weapon class (normalized density)',
                'range to attributed target (world units)', 'density')
    ax.set_xlim(0, RANGE_MAX_U)
    ax.set_ylim(0, RANGE_DENSITY_YMAX)
    for c in CLASS_ORDER:
        vals = ranges_by_class.get(c, [])
        if len(vals) >= RANGE_MIN_SHOTS:
            edges, h = _density(vals, 0.0, RANGE_MAX_U, RANGE_BINS)
            mid = 0.5 * (edges[:-1] + edges[1:])
            ax.step(mid, np.clip(h, 0, RANGE_DENSITY_YMAX), where='mid',
                    color=CLASS_COLOR[c], lw=1.0)
    # The legend lists all eight classes on every sheet whether or not each
    # one drew a curve, so the panel's drawn string set is a constant.
    handles = [Line2D([], [], color=CLASS_COLOR[c], lw=1.2, label=c)
               for c in CLASS_ORDER]
    ax.legend(handles=handles, fontsize=5.5, ncol=2, frameon=False,
              loc='upper right')


def draw_approach_rose(ax, approaches):
    """Panel 3 -- at each fight's first shot, the angle between the shooter's
    heading and the bearing to the enemy.

    0 degrees at the top means walking straight in; +/-90 means crossing.
    Drawn as a fraction of engagements per 15-degree sector on a fixed radial
    scale, so two sheets are directly comparable and the radius cannot encode
    how many fights the demo contained (L4)."""
    ax.set_theta_zero_location('N')
    ax.set_theta_direction(-1)
    ax.set_title('3. approach angle at first shot (0 = straight in)',
                 fontsize=8, pad=4)
    ax.set_ylim(0, ROSE_RMAX)
    ax.set_yticks([ROSE_RMAX / 2, ROSE_RMAX])
    ax.set_yticklabels([f'{ROSE_RMAX/2:.2f}', f'{ROSE_RMAX:.2f}'],
                       fontsize=5.5)
    ax.set_xticks(np.deg2rad(np.arange(0, 360, 45)))
    ax.tick_params(labelsize=6)
    nsec = 24
    edges = np.linspace(-180, 180, nsec + 1)
    if approaches:
        h, _ = np.histogram(approaches, bins=edges)
        frac = h / max(h.sum(), 1)
    else:
        frac = np.zeros(nsec)
    centers = np.deg2rad(0.5 * (edges[:-1] + edges[1:]))
    ax.bar(centers, np.clip(frac, 0, ROSE_RMAX),
           width=np.deg2rad(360.0 / nsec), color='#4a6f9c',
           edgecolor='white', lw=0.4, align='center')


def draw_fire_discipline(axes, intervals):
    """Panel 5 -- inter-shot interval per weapon class, against that weapon's
    refire cycle.

    Eight fixed slots in CLASS_ORDER, always all eight, blank when the class
    was never fired.  The dashed line is CLASS_REFIRE_S, derived from the
    game's own weapon frame tables (see that constant's comment), and it is
    the same line on every sheet.  A trigger held down piles everything into
    the bin at the cycle time; aiming, repositioning and waiting for a corner
    put mass out in the tail.

    DIAGNOSTIC ONLY -- see RULE21_DIAGNOSTIC_ONLY.  Nothing on this panel is
    a legitimate target for a bot change."""
    for i, c in enumerate(CLASS_ORDER):
        ax = axes[i]
        _blank_axes(ax, c, 'interval (s)' if i == 0 else None,
                    'density' if i == 0 else None)
        ax.set_xlim(0, INTERSHOT_MAX_S)
        ax.set_ylim(0, INTERSHOT_DENSITY_YMAX)
        ax.set_xticks([0, 1, 2])
        if i:
            ax.set_yticklabels([])
        edges, h = _density(intervals.get(c, []), 0.0, INTERSHOT_MAX_S,
                            INTERSHOT_BINS)
        ax.bar(0.5 * (edges[:-1] + edges[1:]),
               np.clip(h, 0, INTERSHOT_DENSITY_YMAX),
               width=(INTERSHOT_MAX_S / INTERSHOT_BINS) * 0.9,
               color=CLASS_COLOR[c])
        ax.axvline(CLASS_REFIRE_S[c], color='#333333', lw=0.8, ls='--')


def draw_disengage_traces(axes, engs):
    """Panel 4 -- how each fight ended.

    DISENGAGE_SLOTS fixed slots, always all of them, showing the pair's
    separation over the trailing window, normalized on both axes so neither
    the length of the window nor the absolute distance can be read off as a
    duration (L1).  The line colour is the class.

    The classes are NOT written next to the traces.  A text label reading
    "broke off" would appear on the sheets where someone broke off and nowhere
    else, and the pre-set string audit would (correctly) flag a string that
    shows up on all of one group and none of the other.  The legend below the
    panel lists all three classes on every sheet instead, so the string set is
    constant and only the colours carry the information (L2)."""
    engs = sorted(engs[:DISENGAGE_SLOTS], key=lambda e: e['f0'])
    win = int(round(DISENGAGE_WINDOW_S * F.FPS))
    for i in range(DISENGAGE_SLOTS):
        ax = axes[i]
        _blank_axes(ax, f'end {i + 1}',
                    'trailing window' if i == 0 else None,
                    'separation' if i == 0 else None)
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)
        ax.set_xticks([0, 1])
        ax.set_yticks([0, 0.5, 1])
        if i:
            ax.set_yticklabels([])
        if i >= len(engs):
            continue
        e = engs[i]
        pts = [(f, dd) for f, dd in zip(e['sep_frames'], e['sep_dist'])
               if f >= e['f1'] - win]
        if len(pts) < 2:
            continue
        f0 = pts[0][0]
        span = max(pts[-1][0] - f0, 1)
        xs = [(f - f0) / span for f, _ in pts]
        ys = [min(dd / SEPARATION_YMAX, 1.0) for _, dd in pts]
        ax.plot(xs, ys, color=DISENGAGE_COLOR[e['class']], lw=1.2)

    # The constant three-class legend, always drawn with all three entries
    # whatever this demo contained -- that is what keeps the drawn string set
    # identical across sheets while the colours carry the actual finding.
    handles = [Line2D([], [], color=DISENGAGE_COLOR[c], lw=2.0, label=c)
               for c in DISENGAGE_CLASSES]
    axes[0].legend(handles=handles, loc='upper left',
                   bbox_to_anchor=(0.0, -0.30), ncol=3, fontsize=6.5,
                   frameon=False, handlelength=1.6,
                   title='4. how each fight ended', title_fontsize=7)


def draw_disengage_mix(ax, mix):
    """Companion to panel 4: the share of fight endings in each class.

    Three fixed bars, always all three with their labels, so the tick-label
    string set is identical on every sheet whatever the demo contained.  A
    share, never a count (L4): bot demos contain more fights than a human POV
    recording sees, and plotting that would measure the recording."""
    _blank_axes(ax, '4b. how fights ended (share)', None, 'share')
    ax.set_ylim(0, 1.0)
    xs = np.arange(len(DISENGAGE_CLASSES))
    ax.bar(xs, [mix.get(c, 0.0) for c in DISENGAGE_CLASSES],
           color=[DISENGAGE_COLOR[c] for c in DISENGAGE_CLASSES], width=0.6)
    ax.set_xticks(xs)
    ax.set_xticklabels(DISENGAGE_CLASSES, fontsize=6)


def draw_switch_matrix(ax, P, team, idx):
    """Panel 6 -- weapon-class transition matrix over each player's own
    consecutive shots, pooled per team, row-normalized.

    Row-normalized so the number of shots behind a row -- which is
    coverage-sensitive -- cannot show through (L4).  A pure diagonal means
    everybody held one weapon; off-diagonal mass means weapons were being
    chosen."""
    ax.imshow(P, cmap='magma_r', vmin=0.0, vmax=1.0, aspect='equal',
              interpolation='nearest')
    ax.set_title(f'6{"ab"[idx]}. weapon switching, {team} team',
                 fontsize=8, pad=3, color=TEAM_COLOR[team])
    ax.set_xticks(range(N_CLASSES))
    ax.set_yticks(range(N_CLASSES))
    ax.set_xticklabels(CLASS_ORDER, fontsize=5, rotation=90)
    ax.set_yticklabels(CLASS_ORDER, fontsize=5)
    ax.tick_params(length=1.5)


# The notes strip is CONSTANT text, rendered on every sheet without
# exception.  Leak checklist L2: judge set #3 was ruled partly void because
# the PRESENCE of a note discriminated -- only bot demos ever tripped the
# condition that produced it.  The fix is not "write a better note", it is
# "never let a note's presence, absence or wording depend on anything about
# this demo".
#
# The second bullet is the design's mandated judge preamble sentence for this
# rung: this sheet carries derived labels, and a judge who does not know that
# can convict on an artifact of the attribution heuristic.
NOTES_TEXT = "\n".join([
    "reading notes (identical on every sheet of this instrument):",
    "  * engagement pairing, target attribution and the fight-ending labels are geometric estimates computed from",
    "    position, view angle and event timing -- they are NOT read from game state. Weigh them accordingly.",
    "  * time on panel 1 is normalized to the match. No panel shows an absolute duration, a player count, or an",
    "    event count; every histogram is a density and every matrix is row-normalized.",
    "  * panels with numbered slots always show the same number of slots, chosen by size and drawn in time order,",
    "    and padded; an empty slot means nothing filled it.",
    "  * every axis has a fixed ceiling chosen once for the instrument; a value above its ceiling is drawn at it.",
    "  * the grappling hook and the plasma gun emit no muzzle flash in this game's code, so shots from those two",
    "    weapons are invisible to this sheet, on every recording alike.",
])


def draw_notes_strip(ax):
    ax.axis('off')
    ax.text(0.005, 1.0, NOTES_TEXT, ha='left', va='top', fontsize=6.2,
            family='monospace', color='#555555', linespacing=1.35,
            transform=ax.transAxes)


# ================================================================== render
def render_fight_sheet(demo_path, out_dir, pov_parity=False, pov_ent=None,
                       pov_radius=F.POV_RADIUS_DEFAULT,
                       pov_fov=F.POV_FOV_DEG_DEFAULT, collect_audit=False):
    a = analyze_demo(demo_path, pov_parity=pov_parity, pov_ent=pov_ent,
                     pov_radius=pov_radius, pov_fov=pov_fov)
    d = a['d']
    labels, teams = a['labels'], a['teams']
    h = F.hash_demo(demo_path)
    os.makedirs(out_dir, exist_ok=True)

    fig = plt.figure(figsize=FIGSIZE, dpi=FIGDPI)
    gs = fig.add_gridspec(len(ROW_HEIGHTS), GRID_COLS,
                          height_ratios=ROW_HEIGHTS,
                          hspace=0.62, wspace=1.6,
                          top=0.955, bottom=0.030, left=0.07, right=0.97)

    draw_engagement_timeline(fig.add_subplot(gs[0, :]), a['engagements'],
                             d['frames'])
    draw_range_density(fig.add_subplot(gs[1, 0:14]), a['ranges_by_class'])
    draw_approach_rose(fig.add_subplot(gs[1, 15:24], projection='polar'),
                       a['approaches'])
    draw_fire_discipline(
        [fig.add_subplot(gs[2, i * 3:(i + 1) * 3]) for i in range(N_CLASSES)],
        a['intervals'])
    draw_disengage_traces(
        [fig.add_subplot(gs[3, i * 3:(i + 1) * 3])
         for i in range(DISENGAGE_SLOTS)], a['engagements'])
    draw_switch_matrix(fig.add_subplot(gs[4, 0:7]), a['P_by_team']['red'],
                       'red', 0)
    draw_switch_matrix(fig.add_subplot(gs[4, 8:15]), a['P_by_team']['blue'],
                       'blue', 1)
    draw_disengage_mix(fig.add_subplot(gs[4, 17:24]), a['disengage_mix'])
    draw_notes_strip(fig.add_subplot(gs[5, :]))

    # L10: map and hash, nothing else.  The map is matched across a judge set
    # by design; the hash is the blind identity.  No players=, no duration,
    # no parity marker, no engagement count -- see film.py's caption comment
    # for the ledger of what each of those cost.
    caption = f"map={d['map'] or '?'}   hash={h}"
    fig.text(0.5, 0.99, caption, ha='center', fontsize=10, weight='bold')

    audit = _collect_audit(fig) if collect_audit else None

    png_path = os.path.join(out_dir, f'{h}.png')
    fig.savefig(png_path)
    plt.close(fig)

    sidecar = {
        'hash': h,
        'source_path': os.path.abspath(demo_path),
        'source_basename': os.path.basename(demo_path),
        'map': d['map'],
        'demo_shape': 'serverrecord(bot)' if d['svrecord'] else 'client(human)',
        'sheet': 'fights',
        'frames': d['frames'],
        'duration_s': d['frames'] / F.FPS,
        'duration_capped': a['duration_capped'],
        'duration_original_s': a['duration_original_s'],
        'players_rendered': len(labels),
        'entnum_to_label': {str(k): v for k, v in labels.items()},
        'label_to_name': {v: d['skins'].get(k - 1, '?').split('\\')[0]
                          for k, v in labels.items()},
        'label_to_team': {labels[k]: teams[k] for k in labels},
        'pov_parity': a['pov_parity'],
        'pov_parity_events': a['pov_parity_events'],
        'coverage': {
            'visible_fraction': a['coverage']['visible_fraction'],
            'max_track_fraction': a['coverage']['max_track_fraction'],
            'median_other_track_fraction': a['coverage']['median_other_fraction'],
            'per_track_fraction': {labels[e]: v for e, v
                                   in a['coverage']['per_track'].items()},
        },
        'parser': {
            'blocks': d['blocks'], 'block_errors': d['block_errors'],
            'te_counts': d['te_counts'],
        },
        'events': {
            'shots': len(a['shots']),
            'shots_attributed': sum(1 for s in a['shots']
                                    if s['target'] is not None),
            # 'hits' is TE_BLOOD events tied to a plausible preceding shot
            # (attribute_hits) -- what the timeline's triangle markers draw.
            # 'hits_unattributed' is TE_BLOOD events that found a victim but
            # no plausible shot: lava/slime tick damage, monster damage, or
            # a shot outside the attribution window.  Both are kept and
            # printed so the artifact -- environmental damage inflating raw
            # blood counts -- stays visible instead of being silently
            # dropped by only reporting the attributed number.
            'hits': sum(1 for hh in a['hits'] if hh['attributed']),
            'hits_unattributed': sum(1 for hh in a['hits']
                                     if not hh['attributed']),
            'respawns_ev_player_teleport': len(a['respawns']),
            'shots_by_class': dict(collections.Counter(
                s['cls'] for s in a['shots'])),
            'shots_by_label': {labels[e]: n for e, n in collections.Counter(
                s['ent'] for s in a['shots']).items() if e in labels},
        },
        'engagements': {
            'n': len(a['engagements']),
            'disengage_mix': a['disengage_mix'],
            'disengage_total': a['disengage_total'],
            'shots_per_engagement': [len(e['shots'])
                                     for e in a['engagements'][:TIMELINE_ROWS]],
        },
        'switch_counts': {t: a['counts_by_team'][t].astype(int).tolist()
                          for t in ('red', 'blue')},
        'class_order': CLASS_ORDER,
        'scalars': a['scalars'],
        'rule21_diagnostic_only': sorted(RULE21_DIAGNOSTIC_ONLY),
        'rule21_note': RULE21_NOTE,
        'rendered_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
    }
    json_path = os.path.join(out_dir, f'{h}.json')
    with open(json_path, 'w') as f:
        json.dump(sidecar, f, indent=1)

    return {'hash': h, 'map': d['map'], 'svrecord': d['svrecord'],
            'players': len(labels), 'png': png_path, 'json': json_path,
            'pov_parity': a['pov_parity'], 'scalars': a['scalars'],
            'n_engagements': len(a['engagements']),
            'n_shots': len(a['shots']),
            'n_hits': sum(1 for hh in a['hits'] if hh['attributed']),
            'n_hits_unattributed': sum(1 for hh in a['hits']
                                       if not hh['attributed']),
            'block_errors': d['block_errors'],
            'visible_fraction': a['coverage']['visible_fraction'],
            'audit': audit}


def _collect_audit(fig):
    """Everything the pre-judge-set leak audit needs, harvested from the live
    figure before it is written out.

    Enumerating the Text artists is the mechanical version of "read every
    string on the sheet": it catches a caption leak, a conditional note and a
    tick label that only exists on one corpus, which between them are L1, L2
    and L10.  Doing it from the figure object rather than by OCR means it
    cannot miss a string that is small, rotated or off in a legend."""
    strings = []
    for t in fig.findobj(matplotlib.text.Text):
        s = t.get_text()
        if s and s.strip():
            strings.append(s.strip())
    boxes = []
    for ax in fig.get_axes():
        p = ax.get_position()
        boxes.append((round(p.x0, 5), round(p.y0, 5),
                      round(p.width, 5), round(p.height, 5)))
    return {'strings': sorted(set(strings)), 'n_text': len(strings),
            'n_axes': len(fig.get_axes()), 'axes_boxes': sorted(boxes),
            'figsize': list(fig.get_size_inches()), 'dpi': fig.dpi}


def run_leak_audit(results):
    """The design's appendix audit, run over a rendered set.

    1. Any string that appears on all sheets of one demo shape and none of
       the other is a leak.
    2. Panel geometry must be identical across the set.
    3. Fixed-slot panels must have the same number of axes on every sheet.

    This is reported, not enforced silently: the point is that a set which
    fails it does not go in front of judges."""
    groups = {'human': [], 'bot': []}
    for r in results:
        if r.get('audit'):
            groups['bot' if r['svrecord'] else 'human'].append(r)
    out = {'ok': True, 'findings': [], 'n_human': len(groups['human']),
           'n_bot': len(groups['bot'])}
    if not groups['human'] or not groups['bot']:
        out['findings'].append(
            'only one demo shape present -- the string diff needs both, so '
            'this run checked geometry only')
    else:
        hsets = [set(r['audit']['strings']) for r in groups['human']]
        bsets = [set(r['audit']['strings']) for r in groups['bot']]
        h_all = set.intersection(*hsets)
        b_all = set.intersection(*bsets)
        h_any = set.union(*hsets)
        b_any = set.union(*bsets)
        for s in sorted(h_all - b_any):
            out['ok'] = False
            out['findings'].append(f'LEAK: string on every human sheet and no '
                                   f'bot sheet: {s!r}')
        for s in sorted(b_all - h_any):
            out['ok'] = False
            out['findings'].append(f'LEAK: string on every bot sheet and no '
                                   f'human sheet: {s!r}')

    every = [r for r in results if r.get('audit')]
    geo = {tuple(r['audit']['axes_boxes']) for r in every}
    if len(geo) > 1:
        out['ok'] = False
        out['findings'].append(
            f'GEOMETRY: {len(geo)} distinct panel layouts across the set '
            f'(L7 requires one)')
    nax = {r['audit']['n_axes'] for r in every}
    if len(nax) > 1:
        out['ok'] = False
        out['findings'].append(
            f'SLOTS: sheets carry different axes counts {sorted(nax)} '
            f'(L3 requires a fixed slot count)')
    sizes = {tuple(r['audit']['figsize']) + (r['audit']['dpi'],)
             for r in every}
    if len(sizes) > 1:
        out['ok'] = False
        out['findings'].append(f'GEOMETRY: differing figure sizes {sizes}')
    for r in every:
        if r['svrecord'] and not r['pov_parity'].get('applied'):
            out['ok'] = False
            out['findings'].append(
                f"L5: bot sheet {r['hash']} was rendered without pov-parity")
    return out


# ============================================================= calibration
DEFAULT_HUMAN_GLOB = '~/Games/Quake2/lmctf-hooktest/demos/*.dm2'
DEFAULT_BOT_GLOBS = [
    '~/.local/share/YamagiQ2/lmctf-hooktest/demos/wave4[3-6]*-s03*.dm2',
]
DEFAULT_CACHE = '~/.cache/fightsheet-scalars.json'


def _cache_key(path, pov_parity, radius, fov):
    st = os.stat(path)
    if not pov_parity:
        radius = fov = None
    return f"{os.path.abspath(path)}|{st.st_mtime_ns}|{st.st_size}|" \
           f"{int(bool(pov_parity))}|{radius}|{fov}|fightsheet-v1"


def collect_scalars(paths, pov_parity, radius, fov, cache, maps=None,
                    label=''):
    """Walk a file list and return [{'path','map','shape', **scalars}].

    Cached on (path, mtime, size, parity settings) because the gate gets
    re-run with perturbed parity radii and the demo walk is the expensive
    part."""
    rows = []
    for p in paths:
        hint = RS._map_from_basename(p)
        if maps and hint is not None and hint not in maps:
            continue
        key = _cache_key(p, pov_parity, radius, fov)
        if key in cache:
            row = dict(cache[key])
        else:
            try:
                a = analyze_demo(p, pov_parity=pov_parity, pov_radius=radius,
                                 pov_fov=fov)
            except F.DemoUndersampled as e:
                cache[key] = {'skip': type(e).__name__}
                continue
            except Exception as e:
                sys.stderr.write(f"FAIL {os.path.basename(p)}: "
                                 f"{type(e).__name__}: {e}\n")
                continue
            row = {'map': a['d']['map'],
                   'shape': 'bot' if a['d']['svrecord'] else 'human',
                   'parity_applied': bool(a['pov_parity'].get('applied')),
                   'visible_fraction': a['coverage']['visible_fraction'],
                   'n_shots': len(a['shots']),
                   'n_engagements': len(a['engagements'])}
            row.update(a['scalars'])
            cache[key] = row
        if row.get('skip'):
            continue
        if maps and row.get('map') not in maps:
            continue
        row = dict(row)
        row['path'] = p
        rows.append(row)
        sys.stderr.write(f"  [{label}] {os.path.basename(p):45s} "
                         f"map={row.get('map')} shots={row.get('n_shots')}\n")
    return rows


def run_calibration(human_paths, bot_paths, radius, fov, cache, maps=None):
    """Stage A of the design's two-stage gate.

    Machine-side only, and it never renders or labels a sheet: it walks a
    known-set, computes the sheet's headline scalars, and reports how
    separable the two arms are.  The point of running this first is that the
    judge pass standard rewards an instrument that cannot see anything -- a
    blank sheet convicts both arms at the same rate and 'passes' forever --
    so the instrument has to prove it has power on data where the answer is
    known before it is allowed to certify anything.

    pov-parity is forced ON for the bot arm (L5): without it the bot side is
    an omniscient recording and any separation could be coverage rather than
    behaviour."""
    hr = collect_scalars(human_paths, False, radius, fov, cache, maps=maps,
                         label='human')
    br = collect_scalars(bot_paths, True, radius, fov, cache, maps=maps,
                         label='bot')
    shared = sorted({r['map'] for r in hr} & {r['map'] for r in br})
    hr = [r for r in hr if r['map'] in shared]
    br = [r for r in br if r['map'] in shared]
    out = {'maps': shared, 'n_human': len(hr), 'n_bot': len(br), 'auc': {},
           'human_paths': [r['path'] for r in hr],
           'bot_paths': [r['path'] for r in br]}
    for k in SCALAR_KEYS:
        hv = [r.get(k) for r in hr]
        bv = [r.get(k) for r in br]
        auc = RS.roc_auc(bv, hv)
        out['auc'][k] = {
            'auc_bot_over_human': auc,
            'separability': (max(auc, 1.0 - auc) if auc is not None else None),
            'n_human': sum(1 for v in hv if v is not None),
            'n_bot': sum(1 for v in bv if v is not None),
            'human_mean': (float(np.mean([v for v in hv if v is not None]))
                           if any(v is not None for v in hv) else None),
            'bot_mean': (float(np.mean([v for v in bv if v is not None]))
                         if any(v is not None for v in bv) else None),
        }
    return out


def print_calibration(res, title='STAGE A'):
    print(f"\n=== {title}: maps={','.join(res['maps']) or '(none shared)'} "
          f"n_human={res['n_human']} n_bot={res['n_bot']} ===")
    print(f"{'scalar':28s} {'human_mean':>11s} {'bot_mean':>11s} "
          f"{'AUC(b>h)':>9s} {'separab.':>9s}  panel")
    best = None
    for k in SCALAR_KEYS:
        a = res['auc'][k]
        mark = ' [D]' if k in RULE21_DIAGNOSTIC_ONLY else ''
        hm = '        n/a' if a['human_mean'] is None else f"{a['human_mean']:11.4f}"
        bm = '        n/a' if a['bot_mean'] is None else f"{a['bot_mean']:11.4f}"
        au = '      n/a' if a['auc_bot_over_human'] is None \
            else f"{a['auc_bot_over_human']:9.3f}"
        sp = '      n/a' if a['separability'] is None \
            else f"{a['separability']:9.3f}"
        print(f"{k + mark:28s} {hm} {bm} {au} {sp}  {SCALAR_PANEL[k]}")
        if a['separability'] is not None and (best is None
                                              or a['separability'] > best[1]):
            best = (k, a['separability'], a['auc_bot_over_human'])
    print(f"\n{RULE21_NOTE}")
    if best:
        direction = 'higher on bots' if best[2] >= 0.5 else 'higher on humans'
        print(f"top separating statistic: {best[0]} "
              f"(separability {best[1]:.3f}, {direction}, "
              f"{SCALAR_PANEL[best[0]]})")
        gate = 'PASS' if best[1] >= 0.85 else 'FAIL'
        print(f"Stage A gate (separability >= 0.85 on at least one "
              f"scalar): {gate}")
    hot = [k for k in SCALAR_KEYS
           if (res['auc'][k]['separability'] or 0) >= 0.95]
    if hot:
        print(f"WARNING: separability >= 0.95 on {', '.join(hot)} -- the "
              f"design requires these be inspected by hand and put through "
              f"the +/-100u parity-radius check before they are believed. A "
              f"near-perfect separator is what an instrument leak looks like "
              f"from the inside.")
    return best, hot


# =================================================================== main
def verify_parser(paths):
    """The design makes this a precondition for rendering anything (§2.2).

    Wire-format work has already bitten this toolbox twice, so before a sheet
    is drawn: (a) the event-capturing walker must return byte-identical
    tracks to the frozen rung-1 walker, (b) capturing events must not raise
    the count of abandoned blocks, and (c) the events it captures must be
    sane -- legal weapon ids only, non-zero hits, shot rates in a plausible
    range."""
    bad = 0
    for p in paths:
        base = os.path.basename(p)
        try:
            ref = F.walk_demo(p)
            off = walk_demo_events(p, capture_events=False)
            on = walk_demo_events(p, capture_events=True)
        except Exception as e:
            print(f"FAIL {base}: {type(e).__name__}: {e}")
            bad += 1
            continue
        same_frames = (ref['frames'] == off['frames'] == on['frames'])
        rs = {n: len(t) for n, t in ref['tracks'].items()}
        os_ = {n: len(t) for n, t in off['tracks'].items()}
        on_ = {n: len(t) for n, t in on['tracks'].items()}
        same_tracks = (rs == os_ == on_)
        err_delta = on['block_errors'] - off['block_errors']
        shots = [e for e in on['events'] if e['kind'] == 'shot']
        blood = [e for e in on['events'] if e['kind'] == 'blood']
        resp = on['respawns']
        illegal = [e for e in shots if e['mz'] not in MZ_TO_CLASS]
        mins = max(on['frames'] / F.FPS / 60.0, 1e-9)
        ok = (same_frames and same_tracks and err_delta <= 0 and not illegal)
        if not ok:
            bad += 1
        print(f"{'OK  ' if ok else 'FAIL'} {base}  "
              f"frames={on['frames']} tracks_match={same_tracks} "
              f"blocks={on['blocks']} err_noevents={off['block_errors']} "
              f"err_events={on['block_errors']} (delta {err_delta:+d})  "
              f"shots={len(shots)} ({len(shots)/mins:.0f}/min) "
              f"blood={len(blood)} respawns={len(resp)} "
              f"illegal_mz={len(illegal)}")
        if shots:
            byc = collections.Counter(MZ_TO_CLASS[e['mz']] for e in shots)
            print("       weapon mix: " + "  ".join(
                f"{c}={byc.get(c, 0)}" for c in CLASS_ORDER))
    print(f"\n{len(paths) - bad}/{len(paths)} demo(s) passed parser "
          f"verification")
    return bad == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('demos', nargs='*', help='.dm2 demo files')
    ap.add_argument('--out', help='output directory (required to render)')
    ap.add_argument('--pov-parity', action='store_true',
                    help='serverrecord demos only: keep another player\'s '
                         'sample, and each event, only when a virtual '
                         'recorder could plausibly have seen it. MANDATORY '
                         'on every serverrecord sheet in a judge set (L5).')
    ap.add_argument('--pov-ent', type=int, default=None)
    ap.add_argument('--pov-radius', type=float, default=F.POV_RADIUS_DEFAULT)
    ap.add_argument('--pov-fov', type=float, default=F.POV_FOV_DEG_DEFAULT)
    ap.add_argument('--scalars', action='store_true',
                    help='render nothing; print one CSV row of Stage A '
                         'scalars per demo')
    ap.add_argument('--verify-parser', action='store_true',
                    help='A/B the event-capturing walker against the frozen '
                         'rung-1 walker on each demo. Run this before '
                         'rendering anything.')
    ap.add_argument('--leak-audit', action='store_true',
                    help='with --out: after rendering, diff the drawn string '
                         'sets and panel geometry of the human sheets '
                         'against the bot sheets (design appendix)')
    ap.add_argument('--calibrate', action='store_true',
                    help='Stage A gate: run the instrument over a labeled '
                         'known-set and report ROC AUC per scalar. Renders '
                         'nothing and never writes a label onto any sheet.')
    ap.add_argument('--human', nargs='+', default=[DEFAULT_HUMAN_GLOB],
                    help='globs for the human client-demo arm of --calibrate')
    ap.add_argument('--bot', nargs='+', default=DEFAULT_BOT_GLOBS,
                    help='globs for the serverrecord arm of --calibrate')
    ap.add_argument('--maps', nargs='+', default=None,
                    help='restrict --calibrate to these maps')
    ap.add_argument('--radius-check', action='store_true',
                    help='force the +/-100u parity-radius perturbation check '
                         '(it runs automatically for any scalar that reaches '
                         '0.95 separability)')
    ap.add_argument('--cache', default=DEFAULT_CACHE)
    args = ap.parse_args()

    if args.calibrate:
        cpath = os.path.expanduser(args.cache)
        cache = {}
        if os.path.exists(cpath):
            try:
                cache = json.load(open(cpath))
            except Exception:
                cache = {}
        human = RS._expand(args.human)
        bot = RS._expand(args.bot)
        sys.stderr.write(f"calibrate: {len(human)} human candidate(s), "
                         f"{len(bot)} bot candidate(s)\n")
        res = run_calibration(human, bot, args.pov_radius, args.pov_fov,
                              cache, maps=args.maps)
        best, hot = print_calibration(res)
        if args.radius_check or hot:
            if hot and not args.radius_check:
                print(f"\n(running the parity-radius check automatically "
                      f"because {', '.join(hot)} reached 0.95 separability)")
            for dr in (-100.0, +100.0):
                r2 = args.pov_radius + dr
                alt = run_calibration(human, bot, r2, args.pov_fov, cache,
                                      maps=args.maps)
                print_calibration(alt, title=f'STAGE A (parity radius '
                                             f'{r2:.0f}u)')
                print("  radius-perturbation swing vs baseline:")
                for k in SCALAR_KEYS:
                    a0 = res['auc'][k]['auc_bot_over_human']
                    a1 = alt['auc'][k]['auc_bot_over_human']
                    if a0 is None or a1 is None:
                        continue
                    flag = '  <-- COVERAGE-SENSITIVE' if abs(a1 - a0) > 0.10 \
                        else ''
                    print(f"    {k:28s} {a0:.3f} -> {a1:.3f} "
                          f"(d={a1 - a0:+.3f}){flag}")
        os.makedirs(os.path.dirname(cpath) or '.', exist_ok=True)
        with open(cpath, 'w') as f:
            json.dump(cache, f)
        return

    if not args.demos:
        ap.error('no demos given')

    if args.verify_parser:
        verify_parser(args.demos)
        return

    if args.scalars:
        print('demo_shape,map,basename,' + ','.join(SCALAR_KEYS))
        for p in args.demos:
            try:
                a = analyze_demo(p, pov_parity=args.pov_parity,
                                 pov_ent=args.pov_ent,
                                 pov_radius=args.pov_radius,
                                 pov_fov=args.pov_fov)
            except F.DemoUndersampled as e:
                sys.stderr.write(f"SKIP {os.path.basename(p)}: {e}\n")
                continue
            except Exception as e:
                sys.stderr.write(f"FAIL {os.path.basename(p)}: "
                                 f"{type(e).__name__}: {e}\n")
                continue
            shape = 'bot' if a['d']['svrecord'] else 'human'
            vals = ','.join('' if a['scalars'][k] is None
                            else f"{a['scalars'][k]:.6f}" for k in SCALAR_KEYS)
            print(f"{shape},{a['d']['map']},{os.path.basename(p)},{vals}")
        return

    if not args.out:
        ap.error('--out is required to render sheets')

    ok, skipped, failed = [], [], []
    for p in args.demos:
        try:
            res = render_fight_sheet(p, args.out, pov_parity=args.pov_parity,
                                     pov_ent=args.pov_ent,
                                     pov_radius=args.pov_radius,
                                     pov_fov=args.pov_fov,
                                     collect_audit=args.leak_audit)
            ok.append(res)
            pov = res['pov_parity']
            if res['svrecord'] and not pov.get('applied'):
                sys.stderr.write(
                    f"WARNING {os.path.basename(p)}: serverrecord demo "
                    f"rendered WITHOUT pov-parity. Leak checklist L5 makes "
                    f"parity mandatory on every bot sheet that enters a "
                    f"judge set; this sheet must not be used in one.\n")
            pov_str = (f" pov=ent{pov['pov_entnum']}@{pov['radius_u']:.0f}u"
                       if pov.get('applied') else "")
            s = res['scalars']
            def _f(k, fmt='.3f'):
                v = s[k]
                return 'n/a' if v is None else format(v, fmt)
            print(f"OK   {os.path.basename(p)} -> {res['hash']}.png  "
                  f"map={res['map']} "
                  f"{'bot' if res['svrecord'] else 'human'} "
                  f"players={res['players']}{pov_str} "
                  f"vis={res['visible_fraction']:.3f} "
                  f"eng={res['n_engagements']} shots={res['n_shots']} "
                  f"hits={res['n_hits']} "
                  f"(unattributed={res['n_hits_unattributed']}) "
                  f"err={res['block_errors']} "
                  f"rangesep={_f('range_sep_mean_pairwise')} "
                  f"straight={_f('straight_in_mass')} "
                  f"broke={_f('brokeoff_share')} "
                  f"cv[D]={_f('intershot_cv')}")
        except F.DemoUndersampled as e:
            skipped.append((p, str(e)))
            print(f"SKIP {os.path.basename(p)}: {e}")
        except Exception as e:
            failed.append((p, str(e)))
            print(f"FAIL {os.path.basename(p)}: {type(e).__name__}: {e}")

    print(f"\n{len(ok)} sheet(s) written to {args.out}, "
          f"{len(skipped)} skipped, {len(failed)} failed")
    print(RULE21_NOTE)

    if args.leak_audit:
        rep = run_leak_audit(ok)
        print(f"\n=== LEAK AUDIT: {rep['n_human']} human sheet(s), "
              f"{rep['n_bot']} bot sheet(s) ===")
        for f_ in rep['findings']:
            print(f"  {f_}")
        print(f"  verdict: {'PASS' if rep['ok'] else 'FAIL'}")


if __name__ == '__main__':
    main()


# ----------------------------------------------------------------- MODULE
# NOTES (limitations a judge should know before trusting a fight sheet):
#
# 1. Target attribution is a hypothesis. A shot is credited to the nearest
#    opposite-team player inside a 35-degree cone around the shooter's real
#    view yaw, with no line-of-sight test, because the demo carries no BSP.
#    A shot fired at a wall with an enemy behind it is attributed to that
#    enemy. This has exactly the status of rung 1's captured/died/lost labels
#    (film.py MODULE NOTE 3) and it is stated on every sheet in identical
#    words, so it cannot bias one corpus against the other -- but it does put
#    a floor under how much the range panel can be trusted in tight geometry.
#
# 2. An engagement is a PAIR, so a 2-on-1 is three engagements and a shot
#    fired during it is listed in two of them. The timeline's rows are
#    therefore not disjoint slices of the match. This is the same on both
#    demo shapes.
#
# 3. The hook and the plasma gun emit no muzzle flash in this game's source
#    (p_weapon.c:1943 and 2205, both commented out), so this instrument is
#    blind to them. In a hooktest corpus that is a large fraction of all
#    trigger pulls -- but it is an equally large fraction on both corpora,
#    and the hook is a mobility tool rather than a duelling weapon.
#
# 4. EV_PLAYER_TELEPORT on the entity event is the death signal, and it is
#    only observable where the dying entity was included in the recorder's
#    snapshot. Under pov-parity that culls some deaths on the bot side exactly
#    as the engine culls them on the human side, which is correct -- but it
#    means 'lost contact' absorbs the deaths neither shape could see, and the
#    disengage mix should be read as three classes with one soft boundary, not
#    three clean ones. MZ_LOGIN is discarded because it marks joins, not deaths.
#
# 5. Everything on this sheet is capped to F.DURATION_CAP_S, including the
#    event stream, so two sheets always cover the same time budget. The
#    original duration is in the sidecar and never on the PNG (L1).
#
# 6. Panel 5 (fire discipline) and the mean_aim_offset_deg scalar are
#    DIAGNOSTIC ONLY under Rule 21 (see RULE21_DIAGNOSTIC_ONLY). They exist to
#    explain what a bot is doing, never as a target to move. The honest way
#    for a bot's inter-shot intervals to become ragged is for it to stop
#    firing when it should be repositioning or breaking line of sight.
#
# 7. FIRST STAGE A RESULT (2026-08-06, mactf06, n_human=4, n_bot=38,
#    B-arm = wave430-468 s03-5v5, parity radius 900u). Recorded here because
#    it contradicts the design's prediction and the next person should not
#    have to rediscover that.
#
#      scalar                     human    bot     separability  radius-stable
#      switch_diagonal_mass       0.899    0.599   1.000         YES (+/-0.000)
#      mean_aim_offset_deg [D]   10.89    6.97     1.000         YES (+/-0.000)
#      brokeoff_share             0.468    0.306   0.993         NO  (+0.145)
#      range_sep_rail_shotgun     0.123    0.166   0.875         NO  (-0.125)
#      intershot_cv [D]           0.581    0.228   0.750         YES
#      range_sep_mean_pairwise    0.084    0.085   0.513         NO  (-0.178)
#      straight_in_mass           0.241    0.235   0.572         YES
#
#    The gate PASSES on switch_diagonal_mass, which is also perfectly stable
#    under the +/-100u parity-radius perturbation, so panel 6 is measuring
#    behaviour and not coverage. Three things worth knowing:
#
#    (a) The design predicted panels 5 and 2 would carry the gate. They do
#        not. Panel 5 lands at 0.750, below the 0.85 bar, and BOTH panel-2
#        scalars swing more than 0.10 under the radius perturbation, which
#        by the design's own standard (§3.3) makes panel 2 a coverage
#        artifact rather than a behavioural finding. Panel 4's brokeoff_share
#        swings +0.145 and is in the same position. Panels 2 and 4 must be
#        reported to judges as unreliable, or pulled, until that is fixed.
#    (b) The direction on panel 6 is the opposite of the design's guess. The
#        design expected bots to be diagonal-dominant ("holds one weapon all
#        match"). Measured, HUMANS are the diagonal ones (0.899 vs 0.599),
#        because human rapid-fire weapons -- chaingun, hyperblaster,
#        machinegun -- emit long runs of same-class flashes, while these bots
#        alternate between exactly two slow weapons, rail and rocket. The
#        panel separates strongly; it just does not separate for the stated
#        reason, and a judge briefed on the stated reason would read it
#        backwards.
#    (c) n_human = 4, against the design's n >= 8 per side. That is a hard
#        ceiling on this map, not a bug: only 4 of the 9 mactf06 human demos
#        clear F.DURATION_MIN_S, the other 5 running 1-119s. A properly
#        powered Stage A needs bot demos on the maps where the human corpus
#        is deep (lmctf01 31, lmctf09 14, lmctf22 12), and every s03 wave in
#        430-468 is mactf06.
