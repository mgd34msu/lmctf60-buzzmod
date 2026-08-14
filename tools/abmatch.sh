#!/usr/bin/env bash
#
# abmatch.sh -- one A/B match: 4 legacy bots vs 4 SLIPGATE bots, one map.
#
# Legacy bot spawn syntax (confirmed by grepping "addbot" -- see
# bl_spawn.c:BotAddDeathmatch, which requires exactly 4 args after the
# command name when prefixed with "sv", and assets/bots.cfg which is the
# real roster file shipped with the mod and documents the same form in a
# comment):
#
#   "sv" "addbot" "<name>" "<skin>" "<charfile>" "<charname>"
#   e.g. "sv" "addbot" "Ranger" "male/grunt" "bots/default_c.c" "defaultbot"
#
# (NOT "sv bot addbot" -- there is no "bot" sub-verb; ServerCommand hands
# any unclaimed "sv" verb straight to BotCmd/BotServerCmd, and "addbot" is
# one of the verbs BotServerCmd matches directly. See g_svcmds.c:
# ServerCommand -> BotCmd(cmd, NULL, true) -> BotServerCmd.)
#
# SLIPGATE bot spawn syntax (bl_cmd.c:BotCmd, the "sg" branch; confirmed by
# grepping "SG_AddBot"):
#
#   "sv" "sg" "add"
#
# SLIPGATE bots are auto-named from slipgate/sg_arach.c's sg_names[] table
# ("Arach", "Caco", "Rune", "Slip", "Gate", "Phase", "Field", "Trace") with
# a literal "[SG]" prefix prepended (sg_arach.c:997,
# va("[SG]%s", sg_names[slot & 15])) -- the first 4 "sv sg add" calls in a
# fresh match produce [SG]Arach, [SG]Caco, [SG]Rune, [SG]Slip.
#
# Flag-steal log line (g_ctffunc.c:1028): "%s stole the %s flag.\n"
# Flag-capture log line (g_ctffunc.c:765): "%s captured the %s flag.\n"
# Per-side steal/capture counts cannot be split out by grep alone -- the
# format doesn't carry a team/bot-system tag on the line -- so this script
# captures the full raw log and additionally greps, per known bot name,
# any line mentioning that name (covers frags/deaths, since obituary
# messages are free-text per weapon and not worth hand-enumerating, and
# steals/captures for that bot).
#
# Usage:
#   tools/abmatch.sh <map> <secs>

set -u

Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
GAMEDIR_ROOT="${GAMEDIR_ROOT:-$HOME/Games/Quake2}"
GAME="${GAME:-lmctf-hooktest}"
CFG="${CFG:-rune.cfg}"

AB_PORT="${AB_PORT:-28700}"       # separate range from runegen.sh's 28500+
STARTUP_SLEEP=8                    # seconds before the first "sv addbot"
BOT_SPACING=1                       # seconds between each bot-add command

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LEGACY_BOTS=(
    "Ranger male/grunt bots/default_c.c defaultbot"
    "Sarge male/grunt bots/default_c.c defaultbot"
    "Bitterman male/grunt bots/default_c.c defaultbot"
    "Grunt male/grunt bots/default_c.c defaultbot"
)
# First 4 "sv sg add" calls always land these 4 names (sg_names[slot & 7]
# for slot 0..3), see the header comment above.
SG_BOT_NAMES=("[SG]Arach" "[SG]Caco" "[SG]Rune" "[SG]Slip")
LEGACY_BOT_NAMES=("Ranger" "Sarge" "Bitterman" "Grunt")

usage() {
    echo "usage: $0 <map> <secs>" >&2
    exit 1
}

[ $# -eq 2 ] || usage
MAP="$1"
SECS="$2"

case "$SECS" in
    ''|*[!0-9]*) echo "abmatch: <secs> must be a positive integer, got: $SECS" >&2; exit 1 ;;
esac

TS="$(date +%Y%m%d-%H%M%S)"
LOGFILE="$SCRIPT_DIR/ab-${MAP}-${TS}.log"
TIMEOUT_SECS=$(( STARTUP_SLEEP + (BOT_SPACING * 8) + SECS + 60 ))

if [ ! -x "$Q2DED" ]; then
    echo "abmatch: q2ded binary not found or not executable: $Q2DED" >&2
    exit 1
fi
if ! command -v readlink >/dev/null 2>&1; then
    echo "abmatch: readlink is required to attest the engine binary" >&2
    exit 1
fi
if ! command -v sha256sum >/dev/null 2>&1; then
    echo "abmatch: sha256sum is required to attest the engine binary" >&2
    exit 1
fi
if ! Q2DED_RESOLVED="$(readlink -f -- "$Q2DED")" ||
        [ -z "$Q2DED_RESOLVED" ] || [ ! -x "$Q2DED_RESOLVED" ]; then
    echo "abmatch: cannot resolve executable q2ded path: $Q2DED" >&2
    exit 1
fi
if ! Q2DED_SHA256_LINE="$(sha256sum -- "$Q2DED_RESOLVED")"; then
    echo "abmatch: cannot hash q2ded binary: $Q2DED_RESOLVED" >&2
    exit 1
fi
Q2DED_SHA256="${Q2DED_SHA256_LINE%% *}"
if [[ ! "$Q2DED_SHA256" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "abmatch: invalid q2ded SHA-256 result: $Q2DED_SHA256_LINE" >&2
    exit 1
fi
ENGINE_DIR="$SCRIPT_DIR/ab-${MAP}-${TS}.engine"
if ! mkdir -m 700 -- "$ENGINE_DIR"; then
    echo "abmatch: cannot create private engine directory: $ENGINE_DIR" >&2
    exit 1
fi
Q2DED_SNAPSHOT="$ENGINE_DIR/q2ded-$Q2DED_SHA256"
if ! cp -- "$Q2DED_RESOLVED" "$Q2DED_SNAPSHOT" ||
        ! chmod 0500 -- "$Q2DED_SNAPSHOT"; then
    echo "abmatch: cannot snapshot q2ded binary in $ENGINE_DIR" >&2
    exit 1
fi
if ! Q2DED_SNAPSHOT_LINE="$(sha256sum -- "$Q2DED_SNAPSHOT")"; then
    echo "abmatch: cannot verify q2ded snapshot: $Q2DED_SNAPSHOT" >&2
    exit 1
fi
Q2DED_SNAPSHOT_SHA256="${Q2DED_SNAPSHOT_LINE%% *}"
if [ "$Q2DED_SNAPSHOT_SHA256" != "$Q2DED_SHA256" ]; then
    echo "abmatch: q2ded changed while it was being snapshotted" >&2
    exit 1
fi
Q2DED="$Q2DED_SNAPSHOT"

# Read-only heads-up, exact-binary match only -- never used to kill. See
# tools/runegen.sh for why -f patterns are forbidden here.
if command -v pgrep >/dev/null 2>&1; then
    existing="$(pgrep -x q2ded 2>/dev/null || true)"
    if [ -n "$existing" ]; then
        echo "abmatch: note -- q2ded already running (pid(s): $existing)." \
             "Not touching it. Make sure --port/\$AB_PORT doesn't collide." >&2
    fi
fi

echo "=== abmatch: $MAP -- 4 legacy vs 4 SLIPGATE, ${SECS}s, port $AB_PORT ==="
echo "log: $LOGFILE"
echo "engine: $Q2DED sha256=$Q2DED_SHA256"
if ! printf 'source_path=%s\nsource_sha256=%s\nexecution_path=%s\nexecution_sha256=%s\n' \
        "$Q2DED_RESOLVED" "$Q2DED_SHA256" "$Q2DED" "$Q2DED_SNAPSHOT_SHA256" \
        > "$LOGFILE"; then
    echo "abmatch: cannot write engine attestation to $LOGFILE" >&2
    exit 1
fi

(
    sleep "$STARTUP_SLEEP"
    for bot in "${LEGACY_BOTS[@]}"; do
        echo "sv addbot $bot"
        sleep "$BOT_SPACING"
    done
    for _ in "${SG_BOT_NAMES[@]}"; do
        echo "sv sg add"
        sleep "$BOT_SPACING"
    done
    sleep "$SECS"
    echo "quit"
) | (
    cd "$GAMEDIR_ROOT" && exec stdbuf -oL timeout "$TIMEOUT_SECS" "$Q2DED" \
        +set game "$GAME" +set dedicated 1 +set port "$AB_PORT" \
        +exec "$CFG" +map "$MAP"
) >> "$LOGFILE" 2>&1 &
pid=$!

wait "$pid"

# Belt-and-braces, exact PID only -- see resilience note in runegen.sh.
if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null
fi

# ------------------------------------------------------------------ report

if [ ! -s "$LOGFILE" ]; then
    echo "abmatch: empty log, server produced no output -- $LOGFILE"
    exit 1
fi

total_steals=$(grep -c "stole the .* flag\." "$LOGFILE")
total_captures=$(grep -c "captured the .* flag\." "$LOGFILE")

echo
echo "total steals:    $total_steals"
echo "total captures:  $total_captures"

echo
echo "--- per-bot-name lines (frags/deaths + steals, from the raw log) ---"
for name in "${LEGACY_BOT_NAMES[@]}" "${SG_BOT_NAMES[@]}"; do
    # -F: literal match, so the "[SG]" prefix's brackets aren't treated as
    # a regex character class.
    matches="$(grep -F "$name" "$LOGFILE")"
    count=$(printf '%s\n' "$matches" | grep -c . || true)
    echo
    echo "== $name ($count lines) =="
    if [ -n "$matches" ]; then
        printf '%s\n' "$matches"
    fi
done

echo
echo "raw log: $LOGFILE"
