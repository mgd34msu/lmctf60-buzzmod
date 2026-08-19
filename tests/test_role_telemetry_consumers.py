"""Behavioral coverage for the production SG role-telemetry consumers."""

import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GAMESTAT = ROOT / "tools" / "gamestat.sh"
ROLESTAT = ROOT / "tools" / "rolestat.py"

CURRENT_ROWS = """\
SG [SG]Arach: role=1 seed=1 goal=9000 sgoal=1000 spd=120 org=(0 0 0) link=1 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0
SG [SG]Caco: role=0 seed=2 goal=12000 sgoal=7000 spd=300 org=(1000 0 0) link=2 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0
SG [SG]Rune: role=2 seed=3 goal=5000 sgoal=4000 spd=250 org=(0 0 0) link=3 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0
SG [SG]Slip: role=4 seed=4 goal=4500 sgoal=3500 spd=200 org=(600 0 0) link=4 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0
"""

LEGACY_ROW = (
    "SG Arach[SG]: role=1 seed=1 goal=1000 spd=75 org=(-200 300 0) "
    "link=1 act=0 hp=100 dh=0 dl=0 st=0.0 gnd=1 eng=0\n"
)


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


with tempfile.TemporaryDirectory(prefix="sg-role-consumers-") as tmp:
    tmpdir = Path(tmp)
    current = tmpdir / "current.log"
    current.write_text(CURRENT_ROWS)

    game = run_gamestat(current)
    assert game.returncode == 0, game.stderr
    assert "attacker floors: {'[SG]Caco': 7000}" in game.stdout
    assert "defenders: 1 distinct 100u-cells, moving=1 still=0" in game.stdout

    role = run_rolestat(current)
    assert role.returncode == 0, role.stderr
    assert "defense 100% (1s)" in role.stdout
    assert "pressure 100% (1s)" in role.stdout
    assert "escorted-carry 100% (1s)" in role.stdout

    legacy = tmpdir / "legacy.log"
    legacy.write_text(LEGACY_ROW)
    game = run_gamestat(legacy)
    role = run_rolestat(legacy)
    assert game.returncode == 0, game.stderr
    assert role.returncode == 0, role.stderr
    assert "defense 100% (1s)" in role.stdout

    empty = tmpdir / "wrong-schema.log"
    empty.write_text(
        "SG [SG]Arach: role=1 seed=1 goal=1000 speed=75 org=(0 0 0)\n"
    )
    for result in (run_gamestat(empty), run_rolestat(empty)):
        assert result.returncode != 0, result.stdout
        assert "no SG telemetry rows recognized" in result.stderr

print("role_telemetry_consumers: ok")
