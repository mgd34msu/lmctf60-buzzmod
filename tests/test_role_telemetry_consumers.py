"""Behavioral coverage for the production SG role-telemetry consumers."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import hookevents

GAMESTAT = ROOT / "tools" / "gamestat.sh"
ROLESTAT = ROOT / "tools" / "rolestat.py"

CURRENT_ROWS = (
    "[SG]Arach is now on the red team.\n"
    "[SG]Caco is now on the red team.\n"
    "[SG]Rune is now on the blue team.\n"
    "[SG]Slip is now on the blue team.\n"
    "[SG]Vore is now on the red team.\n"
    "[SG]Field is now on the red team.\n"
    "[SG]Gate is now on the blue team.\n"
    "HOOKFIRE id=1 bot=Arach kind=graph link=3 role=0 map=test anchor_q8=0,0,0\n"
    "SG [SG]Arach: role=1 seed=1 goal=9000 sgoal=1000 spd=120 org=(0 0 0) "
    "link=1 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=100\n"
    "SG [SG]Caco: role=0 seed=2 goal=12000 sgoal=7000 spd=300 org=(1000 0 0) "
    "link=2 act=1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=101\n"
    "SG [SG]Rune: role=2 seed=3 goal=5000 sgoal=4000 spd=250 org=(0 0 0) "
    "link=3 act=3 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=102\n"
    + "noise\n" * 13
    + "[SG]Enemy stole the red flag.\n"
    + "SG [SG]Slip: role=4 seed=4 goal=4500 sgoal=3500 spd=200 org=(600 0 0) "
    "link=4 act=10 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=109\n"
    "SG [SG]Vore: role=3 seed=5 goal=4000 sgoal=9000 spd=250 org=(0 0 0) "
    "link=5 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=103\n"
    "SG [SG]Vore: role=3 seed=6 goal=2500 sgoal=9000 spd=250 org=(100 0 0) "
    "link=6 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=113\n"
    "SG [SG]Gate: role=3 seed=6 goal=100 sgoal=100 spd=250 org=(100 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=114\n"
    "SG [SG]Vore: role=0 seed=7 goal=7000 sgoal=7000 spd=250 org=(200 0 0) "
    "link=7 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=123\n"
    "[SG]Vore returned the red flag.\n"
    "SG [SG]Rune: role=2 seed=8 goal=5000 sgoal=4000 spd=250 org=(0 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=200\n"
    "SG [SG]Field: role=4 seed=9 goal=4500 sgoal=3500 spd=200 org=(0 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=201\n"
    "SG [SG]Arach: role=2 seed=10 goal=5000 sgoal=4000 spd=250 org=(0 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=202\n"
    "[SG]Enemy stole the blue flag.\n"
    "SG [SG]Gate: role=3 seed=11 goal=6000 sgoal=9000 spd=250 org=(0 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=210\n"
    "SG [SG]Gate: role=3 seed=12 goal=5000 sgoal=9000 spd=250 org=(100 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=220\n"
    "[SG]Enemy captured the blue flag.\n"
    "[SG]Enemy stole the red flag.\n"
    "SG [SG]Vore: role=3 seed=13 goal=4500 sgoal=9000 spd=250 org=(200 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=230\n"
    "SG [SG]Vore: role=3 seed=14 goal=4000 sgoal=9000 spd=250 org=(300 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=240\n"
    "SG [SG]Arach: role=1 seed=15 goal=-1 sgoal=-1 spd=0 org=(0 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=300\n"
    "SG [SG]Caco: role=0 seed=16 goal=-1 sgoal=-1 spd=0 org=(0 0 0) "
    "link=-1 act=-1 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0 frm=301\n"
    "HOOKEND id=1 bot=Arach kind=graph link=3 role=0 map=test anchor_q8=0,0,0 "
    "reason=arrived detail=none\n"
)

LEGACY_ROW = (
    "SG Arach[SG]: role=1 seed=1 goal=1000 spd=75 org=(-200 300 0) "
    "link=1 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0\n"
)

WRITER_PROBE = r"""
#include <stdio.h>
#include "slipgate/sg_move.c"

int main(void)
{
	printf("%d %d %d %d\n",
		SG_TelemetryCoordinate(-0.25f),
		SG_TelemetryCoordinate(-0.0f),
		SG_TelemetryCoordinate(-137.4f),
		SG_TelemetryCoordinate(612.4f));
	printf("SG writer: role=1 seed=1 goal=1000 sgoal=900 spd=75 "
		"org=(%d %d %d) link=1 act=0 hp=100 dh=0 dl=0 "
		"st=0.0 gnd=1 eng=0 frm=1\n",
		SG_TelemetryCoordinate(-0.25f),
		SG_TelemetryCoordinate(-137.4f),
		SG_TelemetryCoordinate(612.4f));
	return 0;
}
"""


def run_gamestat(path):
    return subprocess.run(
        ["bash", str(GAMESTAT), str(path)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def run_rolestat(path):
    return subprocess.run(
        [sys.executable, str(ROLESTAT), str(path)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def run_writer_probe(directory):
    source = directory / "sg_telemetry_writer_probe.c"
    binary = directory / "sg_telemetry_writer_probe"
    source.write_text(WRITER_PROBE)
    build = subprocess.run(
        [
            os.environ.get("CC", "cc"), "-std=c11", "-ffunction-sections",
            "-fdata-sections",
            "-I", str(ROOT), str(source), "-Wl,--gc-sections", "-lm",
            "-o", str(binary),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert build.returncode == 0, build.stderr
    return subprocess.run(
        [str(binary)], text=True, capture_output=True, check=False
    )


with tempfile.TemporaryDirectory(prefix="sg-role-consumers-") as tmp:
    tmpdir = Path(tmp)
    writer = run_writer_probe(tmpdir)
    assert writer.returncode == 0, writer.stderr
    writer_lines = writer.stdout.splitlines(keepends=True)
    assert writer_lines[0] == "0 0 -137 612\n"
    assert "org=(0 -137 612)" in writer_lines[1]
    sample, anomaly = hookevents.parse_sg_line(writer_lines[1].rstrip(), 1)
    assert anomaly is None
    assert sample.org == (0, -137, 612)
    emitted = tmpdir / "writer.log"
    emitted.write_text(writer_lines[1])
    for result in (run_gamestat(emitted), run_rolestat(emitted)):
        assert result.returncode == 0, result.stderr

    current = tmpdir / "current.log"
    current.write_text(CURRENT_ROWS)

    game = run_gamestat(current)
    assert game.returncode == 0, game.stderr
    assert "HOOK: fires=1 ends=1" in game.stdout
    assert "route_action_samples: RUN=4 JUMP=1 DROP=0 HOOK=1" in game.stdout
    assert "D_SWIM=1" in game.stdout
    assert "attacker floors: {'[SG]Caco': 7000, '[SG]Vore': 7000}" in game.stdout
    assert "defenders: 1 distinct 100u-cells, moving=1 still=1" in game.stdout

    role = run_rolestat(current)
    assert role.returncode == 0, role.stderr
    assert "defense 50% (2 bot-samples)" in role.stdout
    assert "pressure 66% (3 bot-samples)" in role.stdout
    assert "same-team escort-buckets 66% (3 one-second team-buckets)" in role.stdout
    assert "recover-cost closed -1250ms median delta (2 bot-traces, 2 windows)" in role.stdout
    assert "open -500ms median delta (1 bot-traces, 1 windows)" in role.stdout

    legacy = tmpdir / "legacy.log"
    legacy.write_text(LEGACY_ROW)
    game = run_gamestat(legacy)
    role = run_rolestat(legacy)
    assert game.returncode == 0, game.stderr
    assert role.returncode == 0, role.stderr
    assert "defense 100% (1 bot-samples)" in role.stdout

    empty = tmpdir / "wrong-schema.log"
    empty.write_text(
        "SG [SG]Arach: role=1 seed=1 goal=1000 speed=75 org=(0 0 0)\n"
    )
    for result in (run_gamestat(empty), run_rolestat(empty)):
        assert result.returncode != 0, result.stdout
        assert "no SG telemetry rows recognized" in result.stderr

print("role_telemetry_consumers: ok")
