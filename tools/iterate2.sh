#!/usr/bin/env bash
# Launch one isolated ten-server development wave.
# Usage: iterate2.sh <wave-name>

if [[ ${POV_ENABLE-} == 1 && ! ${BASH_SOURCE[0]} =~ ^/proc/self/fd/[0-9]+$ ]]; then
    ITERATE_SOURCE_DIR=${BASH_SOURCE[0]%/*}
    [[ $ITERATE_SOURCE_DIR != "${BASH_SOURCE[0]}" ]] || ITERATE_SOURCE_DIR=.
    ITERATE_TRUSTED_SCRIPT_DIR=$(builtin cd -P -- "$ITERATE_SOURCE_DIR" && builtin pwd) || exit 2
    exec {ITERATE_SOURCE_FD}<"${BASH_SOURCE[0]}" || exit 2
    CLEAN_ENV=(POV_ENABLE=1 "ITERATE_TRUSTED_SCRIPT_DIR=$ITERATE_TRUSTED_SCRIPT_DIR")
    for clean_name in HOME USER LANG Q2DED GAMEDIR_ROOT GAME CFG PORT_BASE \
        YAMAGI_CLIENT POV_FINALIZE_DELAY DISPLAY WAYLAND_DISPLAY XAUTHORITY \
        XDG_RUNTIME_DIR PULSE_SERVER POV_LANE POV_TARGET; do
        if [[ -v $clean_name ]]; then
            CLEAN_ENV+=("$clean_name=${!clean_name}")
        fi
    done
    LD_PRELOAD= LD_LIBRARY_PATH= LD_AUDIT= GLIBC_TUNABLES= \
        exec /usr/bin/env -i "${CLEAN_ENV[@]}" /bin/bash --noprofile --norc \
        "/proc/self/fd/$ITERATE_SOURCE_FD" "$@"
fi

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"
PORT_BASE="${PORT_BASE:-28520}"
POV_ENABLED=0
if [[ ${POV_ENABLE-} == 1 ]]; then
    POV_ENABLED=1
    YAMAGI_CLIENT="${YAMAGI_CLIENT:-$HOME/Games/Quake2/engines/yquake2/release/quake2}"
    POV_FINALIZE_DELAY="${POV_FINALIZE_DELAY:-3}"
    POV_LANE="${POV_LANE:-3}"
    POV_TARGET="${POV_TARGET:-[SG]Arach}"
    [[ $POV_FINALIZE_DELAY =~ ^[1-9][0-9]*$ ]] || {
        echo "POV_FINALIZE_DELAY must be positive" >&2
        exit 2
    }
    [[ $POV_LANE =~ ^([1-9]|10)$ ]] || {
        echo "POV_LANE must be an integer in [1,10]" >&2
        exit 2
    }
    [[ $POV_TARGET =~ ^\[SG\][A-Za-z]+$ ]] || {
        echo "POV_TARGET must name one [SG] bot" >&2
        exit 2
    }
    POV_SLOT=$((10#$POV_LANE - 1))
    printf -v POV_SERVER 's%02d' "$POV_LANE"
    POV_SPECTATOR="pov_$POV_SERVER"
fi

if ((POV_ENABLED)); then
    SCRIPT_DIR=$ITERATE_TRUSTED_SCRIPT_DIR
else
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
NAME="$1"
LOG_DIR="$SCRIPT_DIR/iter-$NAME"
if ((POV_ENABLED)); then
    /usr/bin/mkdir -p "$LOG_DIR"
else
    mkdir -p "$LOG_DIR"
fi

if { ((POV_ENABLED)) && /usr/bin/pgrep -x q2ded >/dev/null 2>&1; } ||
   { ((!POV_ENABLED)) && pgrep -x q2ded >/dev/null 2>&1; }; then
    echo "q2ded already running; refusing to overlap fleets" >&2
    exit 3
fi

LABELS=(s01-2v2  s02-5v0  s03-5v5  s04-5v5  s05-5v5   s06-5v3  s07-5v3   s08-7v7  s09-ctrl s10-5v5)
MAP_POOL=()
while IFS= read -r map || [[ -n $map ]]; do
    [[ -z $map || $map == \#* ]] && continue
    if [[ ! $map =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}$ ]]; then
        echo "invalid map in $SCRIPT_DIR/topmaps.txt: $map" >&2
        exit 2
    fi
    for existing_map in "${MAP_POOL[@]}"; do
        if [[ $existing_map == "$map" ]]; then
            echo "duplicate map in $SCRIPT_DIR/topmaps.txt: $map" >&2
            exit 2
        fi
    done
    MAP_POOL+=("$map")
done < "$SCRIPT_DIR/topmaps.txt"
if ((${#MAP_POOL[@]} != 20)); then
    echo "expected exactly 20 maps in $SCRIPT_DIR/topmaps.txt" >&2
    exit 2
fi
WAVE_NUMBER=${NAME//[!0-9]/}
[[ -n $WAVE_NUMBER ]] || WAVE_NUMBER=0
WAVE_NUMBER=$((10#$WAVE_NUMBER))
MAP_OFFSETS=(0 2 4 6 8 10 12 14 16 18)
MAPS=()
for map_slot in 0 1 2 3 4 5 6 7 8 9; do
    MAPS[$map_slot]=${MAP_POOL[$(( (WAVE_NUMBER + MAP_OFFSETS[map_slot]) % 20 ))]}
    if [[ ! -s "$GAMEDIR_ROOT/$GAME/maps/${MAPS[$map_slot]}.rune" ]]; then
        echo "missing ${MAPS[$map_slot]}.rune; refusing empty fleet" >&2
        exit 2
    fi
done
RUNE_COMPACT_READER="${RUNE_COMPACT_READER:-$SCRIPT_DIR/../runecompactread.gnu}"
if [[ ! -x $RUNE_COMPACT_READER ]]; then
    echo "canonical RUNE reader is not executable: $RUNE_COMPACT_READER" >&2
    exit 2
fi
for map in "${MAPS[@]}"; do
    "$RUNE_COMPACT_READER" "$GAMEDIR_ROOT/$GAME/maps/$map.rune" \
        >/dev/null || exit 2
done
module_sha256() {
    local path="$1" digest remainder
    [ -f "$path" ] || return 1
    read -r digest remainder < <(/usr/bin/sha256sum -- "$path") || return 1
    [[ $digest =~ ^[0-9a-f]{64}$ ]] || return 1
    printf '%s' "$digest"
}
MODULE_SHA="$(module_sha256 "$GAMEDIR_ROOT/$GAME/game.so")" || {
    echo "cannot hash deployed game.so" >&2
    exit 2
}
MODULE64_SHA="$(module_sha256 "$GAMEDIR_ROOT/$GAME/gamex86_64.so")" || {
    echo "cannot hash deployed gamex86_64.so" >&2
    exit 2
}
if [[ $MODULE_SHA != "$MODULE64_SHA" ]]; then
    echo "deployed game module hashes disagree" >&2
    exit 2
fi
WAVE_MANIFEST="$LOG_DIR/wave-manifest.tsv"
{
    printf 'format\tlmctf-wave-manifest-1\n'
    printf 'wave\t%s\n' "$NAME"
    printf 'module_before_sha256\t%s\n' "$MODULE_SHA"
    for map_slot in 0 1 2 3 4 5 6 7 8 9; do
        printf 'lane\t%s\t%s\n' "${LABELS[$map_slot]}" "${MAPS[$map_slot]}"
    done
} > "$WAVE_MANIFEST.tmp" || exit 2
/usr/bin/mv -f -- "$WAVE_MANIFEST.tmp" "$WAVE_MANIFEST" || exit 2

finish_wave_manifest() {
    local game_sha game64_sha
    game_sha="$(module_sha256 "$GAMEDIR_ROOT/$GAME/game.so")" || return 1
    game64_sha="$(module_sha256 "$GAMEDIR_ROOT/$GAME/gamex86_64.so")" || return 1
    if [[ $game_sha != "$MODULE_SHA" || $game64_sha != "$MODULE_SHA" ]]; then
        echo "deployed game module changed during wave $NAME" >&2
        return 1
    fi
    printf 'module_after_sha256\t%s\n' "$game_sha" >> "$WAVE_MANIFEST"
}
FILLS=( "2"      "5:0"    "5"      "5"      "5"       "5:3"    "5:3"     "7"      "5"      "5")
SECS=(  600      600      900      900      900       900      900       600      900      900)
LONEWOLF=(1    1        1        1        1         1        1         1        1        1)

ESCORTDOSE=(35  35       35       35       35        35       35        35       100      35)

ESCAPE=(1        1        1        1        1         1        1         1        0        1)
DUEL=(  1        1        0        0        0         0        0         0        0        0)
DEFPOST=(0       0        0        0        0         0        0         0        0        0)
DEFREACT=(3      3        3        3        3         3        3         3        0        3)
LANDTICK=(0      0        0        0        0         0        0         0        0        0)
LINKLATCH=(0     0        0        0        0         0        0         0        0        0)

NOBACKTRACK=(60 60      60       60       60        60       60        60       0        60)
NOWEAVE=(0       0        0        0        0         0        0         0        0        0)
FANDENSE=(0      0        0        0        0         0        0         0        0        0)
AIRGAIN=(0       0        0        0        0         0        0         0        0        0)
WETWORK=(1       1        1        1        1         1        1         1        0        0)
NADELEAD=(1      1        1        1        1         1        1         1        0        1)
RUNEDOSE=(0      0        2        2        2         2        2         0        0        0)
PURSUIT=(0       0        0        0        0         0        0         0        0        0)
PURSUITZ=(8      8        8        8        8         8        8         8        8        8)
APPCOVER=(200    200      200      200      200       200      200       200      0        200)
INTERDOSE=(0     0        0        0        0         0        0         0        0        0)
SMOOTHDOSE=(0    0        0        0        0         0        0         0        0        0)
RAILLANE=(1      1        1        1        1         1        1         1        0        1)
RIBBON=(48       48       48       48       48        48       48        48       0        48)

EDGERIDE=(0     0        0        0        0         0        0         0        0        0)
ROUTEJITTER=(8   8        8        8        8         8        8         8        0        8)

COMMSTACK=(1     1        1        1        1         1        1         1        0        1)

MEGAWORTH=(0     0        0        0        0         0        0         0        0        0)

ATKOBJ=(125    125      125      125      125       125      125       125      100      125)

WSWITCH=(0     0        0        0        0         0        0         0        0        0)

WCOMMIT=(1     1        1        1        1         1        1         1        0        1)

AIMTEX=(1     1        1        1        1         1        1         1        0        1)


FREERIDE=(0    0        0        0        0         0        0         0        0        0)

ROPETRAVEL=(0  0        0        0        0         0        0         0        0        0)

FIREDISC=(0    0        0        0        0         0        0         0        0        0)

TAPVAR=(0     0        0        0        0         0        0         0        0        0)

TEAMSKEW=(1    1        1        1        1         1        1         1        1        1)
ESCORTDOSE2=(0 0        0        0        0         0        0         0        0        0)
STRICTGRAB=(0  0        0        0        0         0        0         0        0        0)
RAILRHYTHM=(0  0        0        0        0         0        0         0        0        0)
PATROL=(1      1        1        1        1         1        1         1        1        1)


HOOKPONG=(0    0        0        0        0         0        0         0        0        0)

ROUTEDITHER=(0 0        0        0        0         0        0         0        0        0)

ROPECOST=(1000  1000     1000     1000     1000      1000     1000      1000     1000     1000)

EXITASYM=(0      0        0        0        0         0        0         0        0        0)

DEPACE=(0      0        0        0        0         0        0         0        0        0)

UNLINGER=(0    0        0        0        0         0        0         0        0        0)

BREATHER=(4      4        4        4        4         4        4         4        0        4)

SHELFCOST=(0     0        0        0        0         0        0         0        0        0)
TACTICS=(1       1        1        1        1         1        1         1        0        1)

ADOPT_ON=( 1 1 1 1 1 1 1 1 0 0 )     # strictgrab/press/interpose/scoop/preturn/flycook/runetoss/soundfire/landlead
COVER=(  800 800 800 800 800 800 800 800 0 0 )

STAGGER=(0       0        1        1        0         0        0         0        0        1)

declare -a FLEET_PIDS
iterate_delay() {
    if ((POV_ENABLED)); then
        /usr/bin/sleep "$1"
    else
        sleep "$1"
    fi
}
if ((POV_ENABLED)); then
    ITERATE_SOURCE_IMAGE_FD=${BASH_SOURCE[0]##*/}
    [[ $ITERATE_SOURCE_IMAGE_FD =~ ^[0-9]+$ ]] || exit 2
    exec {POV_SUPERVISOR_FD}<"$SCRIPT_DIR/pov-supervisor" || exit 2
fi
for i in 0 1 2 3 4 5 6 7 8 9; do
    NORMAL_LOG="$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"
    CHILD_LOG="$NORMAL_LOG"
    POV_SELECTED=0
    if ((POV_ENABLED)) && [[ $i == "$POV_SLOT" ]]; then
        POV_SELECTED=1
        CHILD_LOG="$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.pov-launch.log"
    fi
    (
        (
            if ((POV_SELECTED)); then
                exit 0
            fi
            iterate_delay 20
            echo "serverrecord wave$NAME-${LABELS[$i]}"
            if [ "${STAGGER[$i]}" = "1" ]; then
                iterate_delay 65;  echo "set sv_botfill \"3:3\""
                iterate_delay 70;  echo "set sv_botfill \"4:4\""
                iterate_delay 75;  echo "set sv_botfill \"5:5\""
                iterate_delay 240; echo "set sv_botfill \"5:4\""
                iterate_delay 65;  echo "set sv_botfill \"5:5\""
                iterate_delay $(( ${SECS[$i]} - 515 ))
            else
                iterate_delay "${SECS[$i]}"
            fi
            echo "quit"
        ) | (
            WCFG="waveflags-s$(( i + 1 )).cfg"
            A=${ADOPT_ON[$i]}
            {
                echo "exec $CFG"
                echo "set maplist_file nomaplist.txt"
                echo "set timelimit 0"
                if [ "${STAGGER[$i]}" = "1" ]; then
                    echo "set sv_botfill \"2:2\""
                else
                    echo "set sv_botfill \"${FILLS[$i]}\""
                fi
                echo "set sg_press $A"
                if [ "${INTERDOSE[$i]}" != "0" ]; then
                    echo "set sg_interpose ${INTERDOSE[$i]}"
                else
                    echo "set sg_interpose $A"
                fi
                echo "set sg_scoop $A"
                echo "set sg_preturn $A"
                echo "set sg_flycook $A"
                if [ "${RUNEDOSE[$i]}" != "0" ]; then
                    echo "set sg_runetoss ${RUNEDOSE[$i]}"
                else
                    echo "set sg_runetoss $A"
                fi
                echo "set sg_soundfire $A"
                echo "set sg_landlead $A"
                echo "set sg_carrycover ${COVER[$i]}"
                echo "set sg_escapeprior ${ESCAPE[$i]}"
                echo "set sg_duelroles ${DUEL[$i]}"
                echo "set sg_defpost ${DEFPOST[$i]}"
                echo "set sg_defreact ${DEFREACT[$i]}"
                echo "set sg_landtick ${LANDTICK[$i]}"
                echo "set sg_linklatch ${LINKLATCH[$i]}"
                echo "set sg_nobacktrack ${NOBACKTRACK[$i]}"
                if [ "${COMMSTACK[$i]}" = "1" ]; then
                    echo "set sg_itemcomm 1"
                    echo "set sg_radio 1"
                    echo "set sg_itemlead 1"
                fi
                echo "set sg_megaworth ${MEGAWORTH[$i]}"
                echo "set sg_atkobj ${ATKOBJ[$i]}"
                echo "set sg_wswitch ${WSWITCH[$i]}"
                echo "set sg_wcommit ${WCOMMIT[$i]}"
                echo "set sg_ropecost ${ROPECOST[$i]}"
                echo "set sg_tapvar ${TAPVAR[$i]}"
                echo "set sg_freeride ${FREERIDE[$i]}"
                echo "set sg_routedither ${ROUTEDITHER[$i]}"
                echo "set sg_ropetravel ${ROPETRAVEL[$i]}"
                echo "set sg_firedisc ${FIREDISC[$i]}"
                echo "set sg_edgeride ${EDGERIDE[$i]}"
                echo "set sg_escortdose ${ESCORTDOSE[$i]}"
                echo "set sg_lonewolf ${LONEWOLF[$i]}"
                echo "set sg_unlinger ${UNLINGER[$i]}"
                echo "set sg_depace ${DEPACE[$i]}"
                echo "set sg_hookpong ${HOOKPONG[$i]}"
                echo "set sg_railrhythm ${RAILRHYTHM[$i]}"
                echo "set sg_strictgrab ${STRICTGRAB[$i]}"
                echo "set sg_patrol ${PATROL[$i]}"
                if [ "${ESCORTDOSE2[$i]}" != "0" ]; then
                    echo "set sg_escortdose ${ESCORTDOSE2[$i]}"
                fi
                echo "set sg_teamskew ${TEAMSKEW[$i]}"
                echo "set sg_aimtexture ${AIMTEX[$i]}"
                echo "set sg_shelfcost ${SHELFCOST[$i]}"
                echo "set sg_noweave ${NOWEAVE[$i]}"
                echo "set sg_fandense ${FANDENSE[$i]}"
                echo "set sg_airgain ${AIRGAIN[$i]}"
                echo "set sg_wetwork ${WETWORK[$i]}"
                echo "set sg_nadelead ${NADELEAD[$i]}"
                echo "set sg_pursuit ${PURSUIT[$i]}"
                echo "set sg_pursuitz ${PURSUITZ[$i]}"
                echo "set sg_smooth ${SMOOTHDOSE[$i]}"
                echo "set sg_tactics ${TACTICS[$i]}"
                echo "set sg_raillane ${RAILLANE[$i]}"
                echo "set sg_ribbon ${RIBBON[$i]}"
                echo "set sg_routejitter ${ROUTEJITTER[$i]}"
                echo "set sg_exitasym ${EXITASYM[$i]}"
                echo "set sg_breather ${BREATHER[$i]}"
                if [ "$i" = "7" ]; then
                    echo "set sg_crowdhold 1"
                fi
                echo "set sg_approachcover ${APPCOVER[$i]}"
            } > "$GAMEDIR_ROOT/$GAME/$WCFG"
            if ((POV_SELECTED)); then
                IFS= read -r self_stat < "/proc/$BASHPID/stat" || exit 2
                self_tail=${self_stat##*) }
                read -r -a self_fields <<< "$self_tail"
                (( ${#self_fields[@]} > 1 )) || exit 2
                [[ ${self_fields[1]} =~ ^[1-9][0-9]*$ ]] || exit 2
                parent_pid=${self_fields[1]}
                IFS= read -r parent_stat < "/proc/$parent_pid/stat" || exit 2
                parent_tail=${parent_stat##*) }
                read -r -a parent_fields <<< "$parent_tail"
                (( ${#parent_fields[@]} > 19 )) || exit 2
                [[ ${parent_fields[19]} =~ ^[1-9][0-9]*$ ]] || exit 2
                parent_start=${parent_fields[19]}
                exec "/proc/self/fd/$POV_SUPERVISOR_FD" \
                    --q2ded "$Q2DED" \
                    --client "$YAMAGI_CLIENT" --gamedir-root "$GAMEDIR_ROOT" \
                    --game "$GAME" --config "$GAMEDIR_ROOT/$GAME/$WCFG" --normal-log "$NORMAL_LOG" \
                    --lane-root "$LOG_DIR" --wave "$NAME" --server "$POV_SERVER" \
                    --map "${MAPS[$i]}" --port "$((PORT_BASE + i))" \
                    --spectator "$POV_SPECTATOR" --target "$POV_TARGET" \
                    --duration "${SECS[$i]}" --stagger "${STAGGER[$i]}" \
                    --finalize-delay "$POV_FINALIZE_DELAY" \
                    --supervisor-fd "$POV_SUPERVISOR_FD" \
                    --iterate-fd "$ITERATE_SOURCE_IMAGE_FD" \
                    --parent-pid "$parent_pid" --parent-start "$parent_start"
            else
                if ((POV_ENABLED)); then
                    cd "$GAMEDIR_ROOT" && /usr/bin/stdbuf -oL -eL \
                        /usr/bin/timeout $(( 8 + 20 + ${SECS[$i]} + 8 )) \
                        "$Q2DED" +set game "$GAME" +set dedicated 1 \
                        +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                        +exec "$WCFG" +map "${MAPS[$i]}"
                else
                    cd "$GAMEDIR_ROOT" && stdbuf -oL -eL \
                        timeout $(( 8 + 20 + ${SECS[$i]} + 8 )) \
                        "$Q2DED" +set game "$GAME" +set dedicated 1 \
                        +set port $(( PORT_BASE + i )) +set net_port $(( PORT_BASE + i )) +set maxclients 16 \
                        +exec "$WCFG" +map "${MAPS[$i]}"
                fi
            fi
        ) > "$CHILD_LOG" 2>&1
    ) &
    FLEET_PIDS[$i]=$!
    if ((POV_ENABLED)); then
        /usr/bin/sleep 7
    else
        sleep 7
    fi
done
if ((!POV_ENABLED)); then
    wait
    WAVE_STATUS=0
    echo "=== WAVE $NAME (free layout) ==="
    for i in 0 1 2 3 4 5 6 7 8 9; do
        echo "---- ${LABELS[$i]} ${MAPS[$i]} fill=${FILLS[$i]} ${SECS[$i]}s ----"
        if ! "$SCRIPT_DIR/gamestat.sh" "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"; then
            WAVE_STATUS=1
        fi
    done
    finish_wave_manifest || WAVE_STATUS=1
    exit "$WAVE_STATUS"
fi
POV_STATUS=0
for i in 0 1 2 3 4 5 6 7 8 9; do
    if ! wait "${FLEET_PIDS[$i]}"; then
        POV_STATUS=1
    fi
done

echo "=== WAVE $NAME (free layout) ==="
for i in 0 1 2 3 4 5 6 7 8 9; do
    echo "---- ${LABELS[$i]} ${MAPS[$i]} fill=${FILLS[$i]} ${SECS[$i]}s ----"
    if ! /bin/bash --noprofile --norc "$SCRIPT_DIR/gamestat.sh" \
        "$LOG_DIR/${LABELS[$i]}-${MAPS[$i]}.log"; then
        POV_STATUS=1
    fi
done
finish_wave_manifest || POV_STATUS=1
exit "$POV_STATUS"
