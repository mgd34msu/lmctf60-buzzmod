#!/usr/bin/env bash
# gamestat.sh <log> -- every observable from one game log, quoting-proof.
set -u
L="$1"
echo "STEALS=$(grep -c 'stole the' "$L")  CAPS=$(grep -c 'captured the' "$L")  RETURNS=$(grep -c 'returned the' "$L")"
echo "KILLS=$(grep -cE 'was blasted|was railed|ate .* rocket|was blown|almost dodged|was machinegunned|was cut in half|was melted|drown' "$L")"
echo "HOOK: fires=$(grep -c '^HOOKFIRE ' "$L") ends=$(grep -c '^HOOKEND ' "$L")"
echo "SHELVES=$(grep -c SHELVE "$L")  DEADDOORS=$(grep -c DEADDOOR "$L")  CYCLES=$(grep -c CYCLE "$L")"
echo "roles: escort_samples=$(grep -c 'role=4' "$L") recover_samples=$(grep -c 'role=3' "$L")"
echo "telemetry_rows=$(grep -c '^SG ' "$L") chat_lines=$(grep -cE '^\(?(\[SG\])?[A-Z][a-z]+(\[SG\])?\)?: ' "$L")"
echo "kills by weapon: blaster=$(grep -c 'was blasted' "$L") rail=$(grep -c 'was railed' "$L") rocket=$(grep -cE 'ate .* rocket|was blown' "$L") mg=$(grep -c 'was machinegunned' "$L") cg=$(grep -c 'was cut in half' "$L") hb=$(grep -c 'was melted' "$L") ssg=$(grep -cE 'was blown away|was gunned down' "$L")"
grep -h "^ACC " "$L" | tail -12
