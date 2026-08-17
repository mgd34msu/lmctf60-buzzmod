"""New-schema fixtures for the two diagnostic log consumers."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "fixtures" / "hook_diagnostics_kv.log"


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


close = load("hookclose")
diag = load("hookdiag")
expected = {
    "superseded", "burst", "graph-fail", "arrived", "apex", "landed",
    "landing_timeout", "burststall", "noattach", "death",
    "physics-incompatible", "declared-door-interrupt", "stale-host-rope",
    "slot-retirement", "map-transition",
}

close_records = close.parse(FIXTURE)
assert len(close_records) == len(expected)
assert {record["tag"] for record in close_records} == expected
expected_ids = {f"i100.0.{n}" for n in range(1, 15)} | {"z0.1.0.1"}
assert {record["id"] for record in close_records} == expected_ids
assert all(record["bot"] == "Arach" for record in close_records)
assert all(record["anchor"] == (10.0, 20.0, 30.0) for record in close_records)

diag_records = diag.parse(FIXTURE)
assert len(diag_records) == len(expected)
assert {record.end for record in diag_records} == expected
assert {record.id for record in diag_records} == expected_ids
assert all(record.bot == "Arach" and record.anchor == (10.0, 20.0, 30.0)
           for record in diag_records)

# The intentionally incompatible legacy prefix lines at the top never create a record.
assert all(record.id.startswith(("i100.", "z0.1.")) for record in diag_records)

print("hook_diagnostic_consumers: ok")
