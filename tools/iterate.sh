#!/usr/bin/env bash
#
# iterate.sh <name> <duel_map> <five1> <five2> <five3> <five4> <five5> <dens_map> <ctrl_map>
#
# One SLIPGATE wave in the owner's mixed-density format (2026-08-02):
#   servers 1-2 : 2v2 on duel_map (threshold-duel isolation, within-map replication)
#   servers 3-7 : 5v5 on five different maps (baseline lineage)
#   servers 8-9 : 7v7 on dens_map (room-density stress, within-map replication)
#   server  10  : 5v1 on ctrl_map (conversion-mechanics control)
#
# Ten-minute games, staggered launches (same-second starts duplicate Q2's
# RNG), sv_botfill owns every roster ("R B" form carries the asymmetric
# fill). One gamestat block per server at the end.
#
# Process discipline as everywhere in this tree: PID-only, no pattern
# kills, fresh server per game.

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
SECS="${SECS:-600}"  # ten-minute games, owner-ordered wave format
PORT_BASE="${PORT_BASE:-28520}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="$1"; shift
[ $# -eq 8 ] || { echo "need 8 maps: duel five1..five5 dens ctrl" >&2; exit 2; }
DUEL="$1"; F1="$2"; F2="$3"; F3="$4"; F4="$5"; F5="$6"; DENS="$7"; CTRL="$8"
LOG_DIR="$SCRIPT_DIR/iter-$NAME"
mkdir -p "$LOG_DIR"

# server layout: label, map, botfill spec (single value or "R B")
LABELS=(s01-2v2 s02-2v2 s03-5v5 s04-5v5 s05-5v5 s06-5v5 s07-5v5 s08-7v7 s09-7v7 s10-5v1)
# the five 5v5 maps ROTATE across servers by wave number: fixed assignment
# let map identity confound every arm comparison (the owner's frag check,
# wave 184: the "scoop suppression" was mactf06's 1.62 defender K/D
# wearing a doctrine's clothes). Arms keep servers; maps move under them.
ROT=$(( $(echo "$NAME" | tr -cd '0-9') % 5 ))
FIVES=("$F1" "$F2" "$F3" "$F4" "$F5")
R0=${FIVES[$(( (0 + ROT) % 5 ))]}; R1=${FIVES[$(( (1 + ROT) % 5 ))]}
R2=${FIVES[$(( (2 + ROT) % 5 ))]}; R3=${FIVES[$(( (3 + ROT) % 5 ))]}
R4=${FIVES[$(( (4 + ROT) % 5 ))]}
MAPS=("$DUEL" "$DUEL" "$R0" "$R1" "$R2" "$R3" "$R4" "$DENS" "$DENS" "$CTRL")
FILLS=("2" "2" "5" "5" "5" "5" "5" "7" "7" "5:1")
# strict grab adopted (crossed A/B, waves 151-152: strict 9 steals to
# current's 4 on identical map coverage). All 5v5 servers run strict.
GRABS=("0" "0" "1" "1" "1" "1" "1" "0" "0" "0")
# naked-carry was the wave 166-167 diagnostic; sticky routing is the
# shipping candidate for the same root (A/B wave 168+): incumbent route
# holds unless beaten by 15% on s03-05, control on s06-07
# sticky read null on wave 168 (its reach was within-seed only); press
# ADOPTED wave 172 (A/B 169-171: steals 11-11, caps 2-0 press) -- all
# 5v5 servers press; 7v7 stays stock as the density control
PRESS=("0" "0" "1" "1" "1" "1" "1" "0" "0" "0")
# no-weave read null (wave 176: turns 67 vs 79, eff identical) -- dodges
# stay, they cost nothing. Tactics is the wave 177+ A/B: the owner's
# strategy/tactics architecture on s03-05.
# tactics read mixed on 177 (commits worked, walk unchanged) -- the churn
# was BELOW the field layer: seed-center servoing. Lookahead is the fix
# on trial (178+): aim slides down the route when the seed underfoot is
# near. Tactics stays on its servers; lookahead joins them.
# both instruments that motivated 176-178 collapsed against the 5v1
# control (see memory); flags stay built but off pending a caps-based
# verdict on a real sample
# interpose: +27% carrier lifespan pooled 180-182 -- ADOPTED all parity
# servers wave 183. Scoop is the new A/B on s03-05: escorts relay the
# dropped flag instead of escorting a corpse.
INTERPOSE=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
# scoop ADOPTED wave 193: full-cycle crossed verdict 188-192 read +59%
# steals (1.27 vs 0.80/g) and 2-3x carrier lifespan, map-balanced.
# human prior read null on steals (1.07 vs 1.10) -- off; the scoop+prior
# carry interaction (30.1s cell median) is recorded, not adopted.
SCOOP=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
HUMAN=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# steady hand read mixed (195-197: steals 1.6 vs 2.2, suicides 9 vs 15)
# -- off, in the null ledger. The conductor is the 198+ A/B on s03-05.
SMOOTH=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# conductor v1 (metronome) AND v2 (broadcast surge) both read negative
# (198-200: 1.3 vs 2.2; 201-203: 1.4 vs 2.7 steals/g) -- the organic
# rally outperforms every synchronization overlay tried. Null ledger.
WAVEPUSH=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# fast carry (wave 205+): human bar = whole route in 14s; rope tax under
# contact drops 2000->500 on s03-05 now that escorts exist to spend it
# fast carry read NEGATIVE (205-208: survival halved, 9.7 vs 17.6s) --
# the rope tax earns its keep even with escorts; null ledger.
FASTCARRY=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# flag-live prior (wave 213+): human carry-window roads, discount only
# while a flag is out
# flag-live prior NULL across dose 1 / dose 2 / role-scoped (213-215) --
# wave 213's 7-0 was rotation luck. Off; ledger.
FLAGPRIOR=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# quickrope NEGATIVE pooled 216-217 (sloppy ropes worse than ritual).
# legcarrier (218+): carrier skips optional speed-bursts, keeps climbs.
QUICKROPE=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# legs read POSITIVE pooled 218-219 (spd 216 vs 136, slow 26 vs 50) --
# dose 2 adds the landing-brake exemption for carriers
# dose 3 (no shopping) REVERSED the gains (221: spd 112, slow 45%) --
# the detour term was also the tie-breaker keeping routes flowing.
# Dose 2 is the validated peak (14% slow, 229 spd).
LEGCARRIER=("0" "0" "2" "2" "2" "0" "0" "0" "0" "0")
# hop-fire (wave 222+): jump into the rope fire -- airborne drag, no
# ground friction (the owner's technique)
# hop-fire null on hookland%% in both implementations (222-223); off.
# pre-turn (224+): mid-flight aim slides to the onward step -- the 93deg
# grammar constant
HOPFIRE=("0" "0" "0" "0" "0" "0" "0" "0" "0" "0")
# pre-turn ADOPTED wave 227 (one-step peak: +46%/+10% chains, 2x carry
# speed twice; two-step flattened -- corridor clipping)
PRETURN=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
# flying cook (228+): grenade cooks during the approach run
# flying cook ADOPTED fleet-wide wave 239 as free area denial (pooled
# cost margin positive since silent cook; kill-chasing parked after 7
# null mechanisms -- static aims cannot hit movers)
FLYCOOK=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
# rune handoff trial (240+): teammates arm the carrier on s03-05
# courier ADOPTED-DORMANT wave 244: correct, free, fires when a rune-
# holding teammate meets a bare carrier (funnel starved: 0-107-0 cand/wave)
RUNETOSS=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
# sound-directed fire trial (244+): speculative rockets at heard ghosts
# soundfire ADOPTED wave 246 (244-245 pooled: 1.3 vs 0.5 steals/g, free)
SOUNDFIRE=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
# landing-point lead trial (247+): rockets aim at airborne targets' touchdown
# landlead ADOPTED wave 249 (247-248: steals 1.7 vs 1.0, rkt dmg +20%,
# airborne share 47 vs 33)
LANDLEAD=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")
# wet route trial (253+): carriers price swim links as rail-free highways
# wet route ADOPTED wave 255 (253-254: carrier swim 68 vs 1, steals
# 1.2 vs 0.8, free; the moat is the rail-free highway)
WATERCARRY=("0" "0" "1" "1" "1" "1" "1" "1" "1" "0")

for i in 0 1 2 3 4 5 6 7 8 9; do
    (
        (
            sleep 8
            sleep 45   # botfill assembles the roster: 3s hysteresis per seat, 14 seats worst case
            sleep "$SECS"
            echo "quit"
        ) | (
            # doctrine flags travel by config file, never argv: the engine
            # caps the command line at 50 tokens and has killed two waves
            # silently when adopted flags outgrew it (188, 253)
            WCFG="waveflags-s$(( i + 1 )).cfg"
            {
                echo "exec $CFG"
                echo "set sv_botfill \"${FILLS[$i]}\""
                echo "set sg_strictgrab ${GRABS[$i]}"
                echo "set sg_press ${PRESS[$i]}"
                echo "set sg_interpose ${INTERPOSE[$i]}"
                echo "set sg_scoop ${SCOOP[$i]}"
                echo "set sg_preturn ${PRETURN[$i]}"
                echo "set sg_flycook ${FLYCOOK[$i]}"
                echo "set sg_runetoss ${RUNETOSS[$i]}"
                echo "set sg_soundfire ${SOUNDFIRE[$i]}"
                echo "set sg_landlead ${LANDLEAD[$i]}"
                echo "set sg_watercarry ${WATERCARRY[$i]}"
            } > "$GAMEDIR_ROOT/$GAME/$WCFG"
            cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                timeout $(( 8 + 45 + SECS + 40 )) \
                "$Q2DED" +set game "$GAME" +set dedicated 1 \
                +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                +exec "$WCFG" +map "${MAPS[$i]}"
        ) > "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log" 2>&1
    ) &
    sleep 7
done
wait

echo "=== WAVE $NAME: duel=$DUEL fives=$F1,$F2,$F3,$F4,$F5 dens=$DENS ctrl=$CTRL ==="
for i in 0 1 2 3 4 5 6 7 8 9; do
    echo "---- ${LABELS[$i]} ${MAPS[$i]} ----"
    "$SCRIPT_DIR/gamestat.sh" "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"
    python3 "$SCRIPT_DIR/effstat.py" "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"
done
python3 "$SCRIPT_DIR/botledger.py" "$NAME" "$LOG_DIR"/*.log
echo "logs: $LOG_DIR"
