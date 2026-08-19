#!/usr/bin/env bash
# gamestat.sh <log> -- every observable from one game log, quoting-proof.
set -u
L="$1"
echo "STEALS=$(grep -c 'stole the' "$L")  CAPS=$(grep -c 'captured the' "$L")  RETURNS=$(grep -c 'returned the' "$L")"
echo "KILLS=$(grep -cE 'was blasted|was railed|ate .* rocket|was blown|almost dodged|was machinegunned|was cut in half|was melted|drown' "$L")"
echo "HOOK: fires=$(grep -c HOOKFIRE "$L") lands=$(grep -c HOOKLAND "$L") fails=$(grep -c HOOKFAIL "$L")"
echo "SHELVES=$(grep -c SHELVE "$L")  DEADDOORS=$(grep -c DEADDOOR "$L")  CYCLES=$(grep -c CYCLE "$L")"
echo "acts: swim=$(grep -c 'act=4' "$L") lift=$(grep -c 'act=5' "$L") tele=$(grep -c 'act=6' "$L") rj=$(grep -c 'act=7' "$L")"
echo "roles: escort_samples=$(grep -c 'role=4' "$L") recover_samples=$(grep -c 'role=3' "$L")"
python3 - "$L" <<'EOF'
import re, sys
lines = open(sys.argv[1], errors='replace').read().splitlines()
goals = {}          # per attacker: min positive goal
patrol_pos = set()
patrol_moving = still = 0
recognized = 0
telemetry = re.compile(
    r'^SG (\S+): role=(\d+) seed=-?\d+ goal=(-?\d+)'
    r'(?: sgoal=(-?\d+))? spd=(\d+) org=\((-?\d+) (-?\d+)'
)
for ln in lines:
    m = telemetry.match(ln)
    if not m: continue
    recognized += 1
    name = m.group(1)
    role = int(m.group(2))
    # sgoal is the stable destination-field cost.  Current telemetry always
    # emits it; the dynamic goal remains the compatibility fallback for older
    # logs that predate sgoal.
    goal = int(m.group(4)) if m.group(4) is not None else int(m.group(3))
    spd = int(m.group(5))
    if role == 0 and goal > 0:
        goals[name] = min(goals.get(name, 1 << 30), goal)
    if role == 1:
        patrol_pos.add((int(m.group(6)) // 100, int(m.group(7)) // 100))
        if spd > 50: patrol_moving += 1
        else: still += 1
if not recognized:
    print(f"gamestat: no SG telemetry rows recognized: {sys.argv[1]}",
          file=sys.stderr)
    raise SystemExit(1)
print("attacker floors:", dict(sorted(goals.items(), key=lambda kv: kv[1])))
print(f"defenders: {len(patrol_pos)} distinct 100u-cells, moving={patrol_moving} still={still}")
# chat lines (public "Name[SG]: msg" or team "(Name[SG]): msg")
chat = [l for l in lines if re.match(r'^\(?(?:\[SG\])?[A-Z][a-z]+(?:\[SG\])?\)?: [a-z]', l)]
print(f"chat lines: {len(chat)}", ("| sample: " + chat[0]) if chat else "")
EOF
telemetry_status=$?
if [ "$telemetry_status" -ne 0 ]; then
    exit "$telemetry_status"
fi
echo "kills by weapon: blaster=$(grep -c 'was blasted' "$L") rail=$(grep -c 'was railed' "$L") rocket=$(grep -cE 'ate .* rocket|was blown' "$L") mg=$(grep -c 'was machinegunned' "$L") cg=$(grep -c 'was cut in half' "$L") hb=$(grep -c 'was melted' "$L") ssg=$(grep -cE 'was blown away|was gunned down' "$L")"
grep -h "^ACC " "$L" | tail -12
