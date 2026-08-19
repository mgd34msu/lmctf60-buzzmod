"""Strict schema and shared-consumer checks for hook diagnostic logs."""
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
import hookevents


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


close = load("hookclose")
diag = load("hookdiag")
FIRE = "HOOKFIRE id=i100.0.1 bot=Arach kind=graph link=12 role=-3 map=lmctf6 anchor_q8=80,-160,240"
END = FIRE.replace("HOOKFIRE", "HOOKEND") + " reason=arrived detail=reducer"


def result(*lines):
    return hookevents.pair_lines([line + "\n" for line in lines])


def assert_malformed(line):
    parsed = result(line)
    assert not parsed.pairs and not parsed.incomplete
    assert parsed.anomalies and parsed.anomalies[0].code == "malformed"


good = result(FIRE, END)
assert len(good.pairs) == 1 and not good.incomplete and not good.anomalies
assert good.pairs[0].end.reason == "arrived"

# Every key is required, key order is part of the emitted writer schema, and
# writer records cannot contain extra, duplicate, or bare tokens.
for template, keys in ((FIRE, hookevents.FIRE_KEYS), (END, hookevents.END_KEYS)):
    head, *tokens = template.split(" ")
    for missing in keys:
        assert_malformed(" ".join([head] + [token for token in tokens
                                             if not token.startswith(missing + "=")]))
    assert_malformed(template + " extra=x")
    assert_malformed(template + " " + tokens[0])
    assert_malformed(template + " bare")
    swapped = tokens[:]
    swapped[0], swapped[1] = swapped[1], swapped[0]
    assert_malformed(" ".join([head] + swapped))

for broken in (
    FIRE.replace("i100.0.1", "i0100.0.1"),
    FIRE.replace("i100.0.1", "z0.0.0.1"),
    FIRE.replace("i100.0.1", "i18446744073709551616.0.1"),
    FIRE.replace("bot=Arach", "bot=Arach!"),
    FIRE.replace("map=lmctf6", "map=lm/ctf6"),
    FIRE.replace("kind=graph", "kind=rope"),
    FIRE.replace("link=12", "link=01"),
    FIRE.replace("role=-3", "role=2147483648"),
    FIRE.replace("80,-160,240", "80,-160"),
    FIRE.replace("80,-160,240", "80,-160,2147483648"),
    END.replace("reason=arrived", "reason=unknown"),
    END.replace(" detail=reducer", ""),
    END.replace("detail=reducer", "detail=not/safe"),
):
    assert_malformed(broken)

# The old presentation-only log forms are neither records nor errors.
legacy = result("HOOKFIRE Arach at (10 20 30)", "HOOKEND Arach noattach")
assert not legacy.pairs and not legacy.incomplete and not legacy.anomalies

duplicate_fire = result(FIRE, FIRE, END)
duplicate_end = result(FIRE, END, END)
orphan = result(END)
reuse = result(FIRE, END, FIRE, END)
mismatch = result(FIRE, END.replace("link=12", "link=13"))
malformed_then_end = result(FIRE, END.replace("detail=reducer", "detail=not/safe"), END)
for parsed, code in ((duplicate_fire, "duplicate-fire"),
                     (duplicate_end, "duplicate-end"),
                     (orphan, "orphan-end"), (reuse, "duplicate-fire"),
                     (mismatch, "immutable-mismatch"),
                     (malformed_then_end, "malformed")):
    assert not parsed.pairs and not parsed.incomplete
    assert any(anomaly.code == code for anomaly in parsed.anomalies)

# A malformed record can only preserve unrelated evidence when every possible
# ownership key is recoverable.  Repeated id/bot values taint their full
# cross-product; no apparently valid duplicate can repair an existing ride.
for malformed in (
    FIRE.replace("id=i100.0.1", "id=wrong id=i100.0.1"),
    FIRE.replace("id=i100.0.1", "id=i100.0.1 id=wrong"),
    FIRE.replace("bot=Arach", "bot=bad! bot=Arach"),
    FIRE.replace("bot=Arach", "bot=Arach bot=bad!"),
):
    parsed = result(FIRE, malformed, END)
    assert not parsed.global_fatal and not parsed.pairs and not parsed.incomplete
    assert ("i100.0.1", "Arach") in parsed.tainted_keys

two_ids = result(FIRE,
                 FIRE.replace("id=i100.0.1", "id=i100.0.1 id=i101.0.1"),
                 END)
assert not two_ids.global_fatal and not two_ids.pairs
assert set(two_ids.tainted_keys) == {("i100.0.1", "Arach"),
                                    ("i101.0.1", "Arach")}

two_by_two = result(
    FIRE,
    FIRE.replace("id=i100.0.1", "id=i100.0.1 id=i101.0.1").replace(
        "bot=Arach", "bot=Arach bot=Caco"),
    END,
)
assert not two_by_two.global_fatal and not two_by_two.pairs
assert set(two_by_two.tainted_keys) == {
    ("i100.0.1", "Arach"), ("i100.0.1", "Caco"),
    ("i101.0.1", "Arach"), ("i101.0.1", "Caco"),
}

for malformed in (
    FIRE.replace("id=i100.0.1 ", ""),
    FIRE.replace(" bot=Arach", ""),
    "HOOKFIRE",
):
    parsed = result(FIRE, malformed, END)
    assert parsed.global_fatal and not parsed.pairs and not parsed.incomplete
    assert any(anomaly.global_fatal for anomaly in parsed.anomalies)

other_fire = FIRE.replace("i100.0.1", "i101.0.1").replace("bot=Arach", "bot=Caco")
other_end = END.replace("i100.0.1", "i101.0.1").replace("bot=Arach", "bot=Caco")
partial = result(FIRE, END, other_fire + " extra=x", other_end)
assert not partial.global_fatal and {pair.key for pair in partial.pairs} == {
    ("i100.0.1", "Arach")}
assert partial.tainted_keys == (("i101.0.1", "Caco"),)

# EOF is deliberately analytics-only: diag exposes NOEND, close never turns
# the unmatched FIRE into an arrived/closure record.
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "one.log"
    path.write_text("SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=100 org=(0 0 0) link=12 act=1 hp=100 x gnd=1\n" + FIRE + "\n")
    close_records, close_pairing = close.parse_with_result(path)
    diag_records, diag_pairing = diag.parse_with_result(path)
    assert not close_records and len(diag_records) == 1 and diag_records[0].end == "NOEND"
    assert close_pairing == diag_pairing and len(close_pairing.incomplete) == 1

# Both consumers share pairing authority: corrupt records produce no arrived
# records, while the valid fixture yields exactly the same keys and outcomes.
fixture = ROOT / "tests" / "fixtures" / "hook_diagnostics_kv.log"
close_records, close_pairing = close.parse_with_result(fixture)
diag_records, diag_pairing = diag.parse_with_result(fixture)
assert close_pairing == diag_pairing
assert {record["id"] for record in close_records} == {record.id for record in diag_records}
assert {record["tag"] for record in close_records} == {record.end for record in diag_records}
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "bad.log"
    path.write_text("SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=100 org=(0 0 0) link=12 act=1 hp=100 x gnd=1\n" + FIRE + "\n" + FIRE + "\n" + END + "\n" + "SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=800 org=(80 -160 240) link=12 act=1 hp=100 x gnd=1\n")
    assert not close.parse(path) and not diag.parse(path)


SG_BEFORE = ("SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=100 org=(0 0 0) "
             "link=12 act=1 hp=100 x gnd=1\n")
SG_AFTER = ("SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=800 org=(80 -160 240) "
            "link=12 act=1 hp=100 x gnd=1\n")


def run_cli(script, directory):
    return subprocess.run([sys.executable, str(TOOLS / script), str(directory)],
                          text=True, capture_output=True, check=False)


# The report consumers are CLI-safe for every sparse clean class, including an
# empty stream.  In particular hookdiag's quantile sections never index an
# empty sample list.
clean_cases = {
    "arrived": SG_BEFORE + FIRE + "\n" + END + "\n" + SG_AFTER,
    "noattach": SG_BEFORE + FIRE + "\n" + END.replace("reason=arrived", "reason=noattach") + "\n" + SG_AFTER,
    "noend": SG_BEFORE + FIRE + "\n",
    "empty": "",
}
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name, contents in clean_cases.items():
        case = root / name
        case.mkdir()
        (case / "server.log").write_text(contents)
        for script in ("hookclose.py", "hookdiag.py"):
            completed = run_cli(script, case)
            assert completed.returncode == 0, (script, name, completed.stderr)
            assert not completed.stderr

# An unowned malformed prefix makes the whole stream unusable.  Both CLIs
# report it nonzero without emitting a legitimate arrived report/count.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    path = root / "corrupt.log"
    path.write_text(SG_BEFORE + FIRE + "\nHOOKFIRE\n" + END + "\n" + SG_AFTER)
    close_records, close_pairing = close.parse_with_result(path)
    diag_records, diag_pairing = diag.parse_with_result(path)
    assert not close_records and not diag_records
    assert close_pairing == diag_pairing and close_pairing.global_fatal
    for script in ("hookclose.py", "hookdiag.py"):
        completed = run_cli(script, root)
        assert completed.returncode != 0
        assert "hook protocol" in completed.stderr
        assert "arrived" not in completed.stdout

for consumer in (TOOLS / "hookclose.py", TOOLS / "hookdiag.py"):
    source = consumer.read_text()
    expected_import = ("from hookevents import AuxMarker, HookEvent, SGTelemetry, scan_file"
                       if consumer.name == "hookclose.py"
                       else "from hookevents import scan_file")
    assert expected_import in source
    if consumer.name == "hookclose.py":
        assert "with open(" not in source and "for line in" not in source
    assert "def hook_fields" not in source and "def anchor_q8" not in source

# The shared decimal readers must accept the uint64/int32 boundaries without
# ever widening their conversion domain.
for value, accepted in (
    ("18446744073709551614", True),
    ("18446744073709551615", True),
    ("18446744073709551616", False),
):
    parsed = result(FIRE.replace("i100.0.1", "i%s.0.1" % value),
                    END.replace("i100.0.1", "i%s.0.1" % value))
    assert bool(parsed.pairs) is accepted

for value, accepted in (
    ("2147483647", True), ("-2147483648", True),
    ("2147483648", False), ("-2147483649", False),
):
    candidate = FIRE.replace("link=12", "link=%s" % value)
    candidate = candidate.replace("role=-3", "role=%s" % value)
    ending = END.replace("link=12", "link=%s" % value)
    ending = ending.replace("role=-3", "role=%s" % value)
    parsed = result(candidate, ending)
    assert bool(parsed.pairs) is accepted

for value in ("0", "01", "-0", "+1", "١", "", "0000000001"):
    assert hookevents.parse_int32(value) == (0 if value == "0" else None)
for value in ("", "01", "-0", "+1", "١", "0000000001"):
    assert hookevents.parse_uint64(value) is None

SG_TEMPLATE = ("SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=100 "
               "org=(0 0 0) link=12 act=1 hp=100 x gnd=1")


def assert_cli_rejects(name, contents):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / (name + ".log")).write_text(contents)
        for script in ("hookclose.py", "hookdiag.py"):
            completed = subprocess.run(
                [sys.executable, str(TOOLS / script), str(root)],
                text=True, capture_output=True, check=False, timeout=5)
            assert completed.returncode != 0, (script, name)
            assert "Traceback" not in completed.stderr
            assert "arrived" not in completed.stdout


# Every key-shaped SG token participates in duplicate detection, including
# keys that are ignored for analytics and the expanded org field.
for duplicate_name, duplicate_line in (
    ("known", SG_TEMPLATE.replace("role=3", "role=3 role=4")),
    ("org", SG_TEMPLATE + " org=again"),
    ("unknown", SG_TEMPLATE.replace("x gnd=1", "meta=one meta=two x gnd=1")),
):
    sample, anomaly = hookevents.parse_sg_line(duplicate_line, 1)
    assert sample is None and anomaly is not None
    assert anomaly.code == "duplicate-field" and anomaly.fatal
    assert_cli_rejects("duplicate-sg-" + duplicate_name,
                       duplicate_line + "\n" + FIRE + "\n" + END)


# Exact aggregate boundaries accept their boundary item; the first item over
# each ceiling produces a bounded global-fatal result with no partial output.
PAIRING_LIMIT_NAMES = (
    "MAX_INPUT_LINES", "MAX_RECOGNIZED_EVENTS", "MAX_CANDIDATE_KEYS",
    "MAX_PAIRS", "MAX_INCOMPLETES", "MAX_ANOMALIES",
)


def bounded_pairing_probe(limit_name, exact_lines, over_lines):
    saved = {name: getattr(hookevents, name) for name in PAIRING_LIMIT_NAMES}
    try:
        for name in PAIRING_LIMIT_NAMES:
            setattr(hookevents, name, 10 ** 9)
        setattr(hookevents, limit_name, 3)
        exact = hookevents.pair_lines((line + "\n" for line in exact_lines))
        over = hookevents.pair_lines((line + "\n" for line in over_lines))
    finally:
        for name, value in saved.items():
            setattr(hookevents, name, value)
    assert not exact.global_fatal, (limit_name, exact.anomalies)
    assert over.global_fatal and not over.pairs and not over.incomplete
    assert any(anomaly.code == "aggregate-limit"
               for anomaly in over.anomalies), (limit_name, over.anomalies)
    return exact


def fire(number):
    return FIRE.replace("i100.0.1", "i%d.0.1" % number)


def end(number):
    return END.replace("i100.0.1", "i%d.0.1" % number)


bounded_pairing_probe("MAX_INPUT_LINES", ["noise"] * 3,
                      ["noise"] * 4)
recognized_exact = bounded_pairing_probe(
    "MAX_RECOGNIZED_EVENTS", [fire(1), fire(2), fire(3)],
    [fire(1), fire(2), fire(3), fire(4)])
assert len(recognized_exact.incomplete) == 3
candidate_exact = bounded_pairing_probe(
    "MAX_CANDIDATE_KEYS",
    ["HOOKFIRE id=i%d.0.1 bot=Arach" % n for n in (1, 2, 3)],
    ["HOOKFIRE id=i%d.0.1 bot=Arach" % n for n in (1, 2, 3, 4)])
assert len(candidate_exact.anomalies) == 3
pair_exact = bounded_pairing_probe(
    "MAX_PAIRS", [fire(1), end(1), fire(2), end(2), fire(3), end(3)],
    [fire(1), end(1), fire(2), end(2), fire(3), end(3), fire(4), end(4)])
assert len(pair_exact.pairs) == 3
incomplete_exact = bounded_pairing_probe(
    "MAX_INCOMPLETES", [fire(1), fire(2), fire(3)],
    [fire(1), fire(2), fire(3), fire(4)])
assert len(incomplete_exact.incomplete) == 3
anomaly_exact = bounded_pairing_probe(
    "MAX_ANOMALIES", [fire(1), fire(1), fire(1), fire(1)],
    [fire(1)] + [fire(1)] * 4)
assert len(anomaly_exact.anomalies) == 3


# A real CLI probe crosses the input ceiling and must stop without reading a
# giant suffix or exposing a partial report.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "aggregate.log").write_text(
        "noise\n" * (hookevents.MAX_INPUT_LINES + 1))
    for script in ("hookclose.py", "hookdiag.py"):
        completed = subprocess.run(
            [sys.executable, str(TOOLS / script), str(root)],
            text=True, capture_output=True, check=False, timeout=5)
        assert completed.returncode != 0
        assert completed.stdout == ""
        assert "aggregate-limit" in completed.stderr
        assert "Traceback" not in completed.stderr


# Decimal tokens at every adversarial width are rejected in bounded time by
# both central-record and SG consumers.  The SG parser also rejects each
# numeric field independently, rather than letting one field poison parsing
# of another or fabricate analytics.
for digits in (4299, 4300, 5000, 100000):
    huge = "9" * digits
    assert_cli_rejects("id-%d" % digits,
                       FIRE.replace("i100.0.1", "i%s.0.1" % huge) + "\n" + END)
    assert_cli_rejects("link-%d" % digits,
                       FIRE.replace("link=12", "link=%s" % huge) + "\n" + END)
    assert_cli_rejects("anchor-%d" % digits,
                       FIRE.replace("80,-160,240", "80,%s,240" % huge) + "\n" + END)

for field in ("spd", "role", "seed", "act", "gnd"):
    huge = "9" * 5000
    sg_line = SG_TEMPLATE.replace(field + "=" + {
        "spd": "100", "role": "3", "seed": "1", "act": "1",
        "gnd": "1"}[field], field + "=" + huge)
    assert_cli_rejects("sg-%s" % field, sg_line + "\n" + FIRE + "\n" + END)
for coordinate in range(3):
    parts = ["0", "0", "0"]
    parts[coordinate] = "9" * 5000
    sg_line = SG_TEMPLATE.replace("org=(0 0 0)", "org=(%s)" % " ".join(parts))
    assert_cli_rejects("sg-org-%d" % coordinate,
                       sg_line + "\n" + FIRE + "\n" + END)

# Unrelated nonnumeric noise is not a diagnostic candidate and remains a
# deterministic clean empty stream in both reports.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "noise.log").write_text(
        "noise-" + "x" * 100000 + "\n"
        "SG source snap rejected ent=7 from=(-207.125 -30.625 296.125)\n")
    for script in ("hookclose.py", "hookdiag.py"):
        completed = run_cli(script, root)
        assert completed.returncode == 0
        assert completed.stderr == ""


# Physical reads themselves are bounded.  Oversized noise is drained as one
# ignored line, while an oversized recognized hook line is a controlled fatal
# protocol anomaly.  The stream refuses any unbounded readline request, so the
# regression cannot pass by allocating first and rejecting afterward.
class BoundedReadlineProbe:
    def __init__(self, text):
        self.text = text
        self.offset = 0
        self.sizes = []

    def readline(self, size=-1):
        assert size == hookevents.PHYSICAL_LINE_MAX + 1
        self.sizes.append(size)
        if self.offset >= len(self.text):
            return ""
        end = min(len(self.text), self.offset + size)
        newline = self.text.find("\n", self.offset, end)
        if newline >= 0:
            end = newline + 1
        chunk = self.text[self.offset:end]
        self.offset = end
        return chunk


probe = BoundedReadlineProbe("noise-" + "x" * 100000 + "\n" + FIRE + "\n" + END)
bounded_lines = hookevents._bounded_file_lines(probe)
bounded_pairing = hookevents._scan_lines(bounded_lines)
assert len(bounded_pairing.pairs) == 1 and not bounded_pairing.anomalies
assert (bounded_pairing.pairs[0].fire.line,
        bounded_pairing.pairs[0].end.line) == (2, 3)
assert len(probe.sizes) > 3

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "oversized-noise.log").write_text("x" * (8 * 1024 * 1024))
    for script in ("hookclose.py", "hookdiag.py"):
        completed = run_cli(script, root)
        assert completed.returncode == 0
        assert completed.stderr == ""

    # Draining one oversized physical line must advance the shared cursor
    # exactly once.  Both real consumers must still pair and locate the SG
    # samples that follow it; splitting the drain into logical lines would
    # shift these positions or lose the closure record.
    (root / "oversized-abort.log").write_text("")
    (root / "oversized-noise.log").write_text(
        "x" * (8 * 1024 * 1024) + "\n" +
        SG_BEFORE + FIRE + "\n" + END + "\n" + SG_AFTER)
    close_records, close_pairing = close.parse_with_result(
        root / "oversized-noise.log")
    diag_records, diag_pairing = diag.parse_with_result(
        root / "oversized-noise.log")
    assert close_pairing == diag_pairing
    assert not close_pairing.anomalies and len(close_pairing.pairs) == 1
    assert (close_pairing.pairs[0].fire.line,
            close_pairing.pairs[0].end.line) == (3, 4)
    assert len(close_records) == len(diag_records) == 1
    assert close_records[0]["id"] == diag_records[0].id == "i100.0.1"

    (root / "oversized-hook.log").write_text(
        "HOOKFIRE " + "x" * (8 * 1024 * 1024)
    )
    for script in ("hookclose.py", "hookdiag.py"):
        completed = run_cli(script, root)
        assert completed.returncode != 0
        assert "hook protocol" in completed.stderr
        assert "Traceback" not in completed.stderr
    (root / "oversized-hook.log").write_text("")

    # Valid-prefix auxiliary input is optional and ignored when over the fixed
    # writer capacity; it must not split the full physical line.
    (root / "oversized-abort.log").write_text(
        "HOOKABORT Arach teammate " + "x" * (8 * 1024 * 1024)
    )
    for script in ("hookclose.py", "hookdiag.py"):
        completed = run_cli(script, root)
        assert completed.returncode == 0
        assert completed.stderr == ""


# scan_file is the one-pass physical authority.  It dispatches typed records
# and line-numbered auxiliary/anomaly markers while retaining pair_file's
# deterministic PairingResult contract.
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "observer.log"
    path.write_text(SG_TEMPLATE + "\n" + FIRE + "\nHOOKSKYHOLD Arach\n" + END)
    seen = []
    pairing = hookevents.scan_file(path, seen.append)
    assert len(pairing.pairs) == 1
    assert any(isinstance(item, hookevents.HookEvent) for item in seen)
    assert any(isinstance(item, hookevents.SGTelemetry) for item in seen)
    assert any(isinstance(item, hookevents.AuxMarker) for item in seen)
    assert isinstance(seen[-1], hookevents.EOFMarker)
    assert all(getattr(item, "line", 0) > 0 for item in seen)


# The former active_for_bot shape must stay linear even when every FIRE is
# incomplete and every SG sample belongs to an unrelated bot.  Exercise the
# CLI through a subprocess so the test covers its actual file-read path.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    fire_line = ("HOOKFIRE id=i%d.0.1 bot=Arach kind=graph link=12 role=3 "
                 "map=lmctf6 anchor_q8=80,160,240\n")
    unrelated_sg = ("SG Caco: role=3 seed=1 goal=2 sgoal=2 spd=100 "
                    "org=(0 0 0) link=12 act=1 hp=100 x gnd=1\n")
    (root / "quadratic.log").write_text(
        "".join(fire_line % number for number in range(1, 16001)) +
        unrelated_sg * 16000)
    completed = subprocess.run(
        [sys.executable, str(TOOLS / "hookdiag.py"), str(root)],
        text=True, capture_output=True, check=False, timeout=5)
    assert completed.returncode == 0
    assert "total HOOKFIRE records parsed: 16000" in completed.stdout
    assert completed.stderr == ""


# Overlapping same-bot windows use exact [FIRE, END) ranges.  SG before a
# FIRE and SG/markers after its END cannot leak into either window.
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "overlap.log"
    sg = lambda speed, x: (
        "SG Arach: role=3 seed=1 goal=2 sgoal=2 spd=%d "
        "org=(%d 0 0) link=12 act=1 hp=100 x gnd=1\n" % (speed, x))
    fire_one = FIRE + "\n"
    fire_two = fire(2) + "\n"
    end_one = END + "\n"
    end_two = end(2) + "\n"
    path.write_text(sg(10, 0) + fire_one + sg(20, 1) + fire_two +
                    sg(30, 2) + end_one + sg(40, 3) + end_two +
                    sg(99, 4) + "HOOKSKYHOLD Arach\n")
    records, pairing = diag.parse_with_result(path)
    assert not pairing.anomalies and len(records) == 2
    first, second = records
    assert (first.nsg, first.maxspd, first.postorg, first.tick,
            first.endtick) == (2, 30, (1, 0, 0), 1, 3)
    assert (second.nsg, second.maxspd, second.postorg, second.tick,
            second.endtick) == (2, 40, (2, 0, 0), 2, 4)
    assert first.skyholds == second.skyholds == 0


# A fatal SG anomaly suppresses every analytics record, even when valid pairs
# precede and follow the malformed sample.
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "fatal-sg.log"
    path.write_text(FIRE + "\n" + END + "\n" +
                    SG_TEMPLATE.replace("spd=100", "spd=999999999999") +
                    "\n" + FIRE.replace("i100.0.1", "i100.0.2") +
                    "\n" + END.replace("i100.0.1", "i100.0.2"))
    records, pairing = diag.parse_with_result(path)
    assert not records and pairing.telemetry_anomalies
    assert all(anomaly.fatal for anomaly in pairing.telemetry_anomalies)


# Protocol and SG anomalies share one aggregate budget.  The exact boundary
# remains non-global-fatal; the first item beyond it emits one deterministic
# aggregate-limit anomaly and suppresses even an otherwise valid pair.
with tempfile.TemporaryDirectory() as directory:
    saved_limit = hookevents.MAX_ANOMALIES
    try:
        hookevents.MAX_ANOMALIES = 3
        valid_pair = fire(1) + "\n" + end(1) + "\n"
        mixed_prefix = (valid_pair + fire(2) + "\n" + fire(2) + "\n" +
                        SG_TEMPLATE.replace("spd=100", "spd=999999999999") +
                        "\n" + end(2) + "\n")
        exact_path = Path(directory) / "mixed-exact.log"
        exact_path.write_text(mixed_prefix)
        exact = hookevents.scan_file(exact_path)
        assert not exact.global_fatal and len(exact.pairs) == 1
        assert len(exact.anomalies) == 2
        assert len(exact.telemetry_anomalies) == 1

        over_path = Path(directory) / "mixed-over.log"
        over_path.write_text(mixed_prefix +
                             SG_TEMPLATE.replace("spd=100",
                                                 "spd=999999999999") + "\n")
        over = hookevents.scan_file(over_path)
        assert over.global_fatal and not over.pairs and not over.incomplete
        assert any(anomaly.code == "aggregate-limit"
                   for anomaly in over.anomalies)
    finally:
        hookevents.MAX_ANOMALIES = saved_limit


# Large overlapping auxiliary windows retain shared range descriptors rather
# than N copies of the N attributed marker records.  Iteration still exposes
# exact marker semantics for small compatibility probes.
with tempfile.TemporaryDirectory() as directory:
    marker_path = Path(directory) / "overlap-markers.log"
    marker_count = 8000
    marker_path.write_text(
        "".join(fire(number) + "\n" for number in range(1, marker_count + 1)) +
        "HOOKABORT Arach teammate\n" * marker_count +
        "".join(end(number).replace("reason=arrived", "reason=noattach") +
                "\n" for number in range(1, marker_count + 1)))
    records, pairing = diag.parse_with_result(marker_path)
    assert not pairing.global_fatal and len(records) == marker_count
    assert all(not hasattr(record, "aborts") and
               not hasattr(record, "bites") for record in records)
    assert all(record.abort_count == marker_count for record in records)
    assert all(record.bite_count == 0 for record in records)
    causes = diag.aggregate_noattach_causes(records)
    assert causes.abort_counts["teammate"] == marker_count * marker_count
    assert causes.bite_fires == 0 and causes.death_fires == 0
    assert causes.any_cause_fires == marker_count


# Cause windows preserve strict boundaries and multiplicity, while bite/death
# metrics remain per-fire booleans.  Pre-FIRE and post-END markers are absent.
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "cause-boundaries.log"
    bite = ("HOOKBITE Arach off=200 into=world org=(1 2 3) "
            "want=(4 5 6) got=(7 8 9)\n")
    path.write_text(
        "HOOKABORT Arach before\n" + fire(1) + "\n" +
        "HOOKABORT Arach teammate\n" * 2 + bite * 2 +
        "BOTDEATH: Arach t1\n" + end(1).replace(
            "reason=arrived", "reason=noattach") + "\n" +
        "HOOKABORT Arach after\n")
    records, pairing = diag.parse_with_result(path)
    assert not pairing.anomalies and len(records) == 1
    record = records[0]
    assert (record.abort_count, record.bite_count, record.deaths,
            record.any_cause) == (2, 2, 1, True)
    causes = diag.aggregate_noattach_causes(records)
    assert dict(causes.abort_counts) == {"teammate": 2}
    assert (causes.bite_fires, causes.death_fires,
            causes.any_cause_fires) == (1, 1, 1)


# Timelines are isolated by bot and by file.  Non-noattach windows are not
# included in the cause summary.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    caco_fire = FIRE.replace("bot=Arach", "bot=Caco").replace(
        "i100.0.1", "i100.0.3")
    caco_end = END.replace("bot=Arach", "bot=Caco").replace(
        "i100.0.1", "i100.0.3").replace(
            "reason=arrived", "reason=noattach")
    caco_marker = "HOOKABORT Caco caco\n"
    path_a = root / "bot-a.log"
    path_a.write_text(fire(1) + "\nHOOKABORT Arach arach\n" + end(1) +
                      "\n" + caco_fire + "\n" + caco_marker + caco_end)
    path_b = root / "bot-b.log"
    path_b.write_text(fire(2) + "\nHOOKABORT Arach other\n" +
                      end(2).replace(
                          "reason=arrived", "reason=noattach") + "\n")
    records_a, pairing_a = diag.parse_with_result(path_a)
    records_b, pairing_b = diag.parse_with_result(path_b)
    assert not pairing_a.anomalies and not pairing_b.anomalies
    assert {record.end for record in records_a} == {"arrived", "noattach"}
    assert len(records_b) == 1 and records_b[0].end == "noattach"
    causes = diag.aggregate_noattach_causes(records_a + records_b)
    assert dict(causes.abort_counts) == {"caco": 1, "other": 1}
    assert causes.any_cause_fires == 2


# Cause presentation is deterministic for equal counts: descending count,
# then lexical reason.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    path = root / "cause-order.log"
    path.write_text(fire(1) + "\nHOOKABORT Arach z\n" +
                    end(1).replace("reason=arrived", "reason=noattach") +
                    "\n" + fire(2) + "\nHOOKABORT Arach a\n" +
                    end(2).replace("reason=arrived", "reason=noattach") +
                    "\n")
    completed = run_cli("hookdiag.py", root)
    assert completed.returncode == 0, completed.stderr
    cause_lines = [line.strip() for line in completed.stdout.splitlines()
                   if line.strip().startswith("HOOKABORT ")]
    assert cause_lines[0].startswith("HOOKABORT a")
    assert cause_lines[1].startswith("HOOKABORT z")
    assert all("1  (50.0% of noattach)" in line
               for line in cause_lines[:2])


# A tainted key and a global-fatal protocol record cannot contribute causes.
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "cause-taint.log"
    path.write_text(fire(1) + "\n" + fire(1) + "\n" +
                    "HOOKABORT Arach tainted\n" + end(1) + "\n")
    records, pairing = diag.parse_with_result(path)
    assert not records and pairing.anomalies
    assert not diag.aggregate_noattach_causes(records).abort_counts
    path.write_text(fire(1) + "\nHOOKFIRE\n" + end(1) + "\n")
    records, pairing = diag.parse_with_result(path)
    assert not records and pairing.global_fatal
    assert not diag.aggregate_noattach_causes(records).abort_counts


# Keep the source-level complexity guard close to the regression test: the
# report may not reintroduce a helper that scans all open fires per sample.
diag_source = (TOOLS / "hookdiag.py").read_text()
assert "active_for_bot" not in diag_source
assert "open_fire" not in diag_source
assert "_MarkerRange" not in diag_source
assert "iter_markers" not in diag_source
assert ".aborts" not in diag_source and ".bites" not in diag_source
assert "for marker in" not in diag_source

print("hookevents: ok")
