#!/usr/bin/env python3
"""Live residence reducer for the hash-bound persistent fleet runner."""

from __future__ import annotations

from dataclasses import dataclass, field
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import re
import selectors
import signal
import stat
import subprocess
import time
from typing import Any


IDENTITY_RE = re.compile(
    rb"^slipgate: rune identity committed map=([a-z0-9._-]+) "
)
EXIT_RE = re.compile(rb"^EXITLEVEL frame=([0-9]+) time=([0-9.]+) changemap=(\S+)$")
ROUTE_CONTRACT_RE = re.compile(rb"^slipgate: route contract local-only$")
RUNE_READY_RE = re.compile(rb"^slipgate: rune ready ([a-z0-9._-]+), ")
STATS_BACKUP_RE = re.compile(rb"^statsdb: backed up to (.+)$")
TIMELIMIT_RE = re.compile(rb"^Timelimit hit\.$")
CENSUS_RE = re.compile(rb"^SGCENSUS (\[SG\][A-Za-z0-9_-]+): frm=([0-9]+) alive=([01])$")
ROSTER_RE = re.compile(
    rb"^\s*([0-9]+)\s+(\[SG\][A-Za-z0-9_-]+)\s+(red|blue)\s+"
)
ROSTER_END_RE = re.compile(rb"^slipgate: ([0-9]+) bots$")


@dataclass
class LaneRun:
    lane: str
    spec: dict
    engine: Any
    client: subprocess.Popen[bytes]
    cycle: Any
    server_log: Any
    client_log: Any
    engine_buffer: bytearray = field(default_factory=bytearray)
    client_buffer: bytearray = field(default_factory=bytearray)
    console: list[bytes] = field(default_factory=list)
    roster: dict[str, tuple[int, int]] = field(default_factory=dict)
    census: dict[str, list[int]] = field(default_factory=dict)
    server_demo: Path | None = None
    pov_source: Path | None = None
    pov_started: bool = False
    pov_stopped: bool = False
    pov_requested: bool = False
    spectator_entered: bool = False
    route_ready: bool = False
    rune_ready: bool = False
    exit_frame: int | None = None
    exit_time: float | None = None
    backup_confirmed: bool = False
    backup_requested: bool = False
    timelimit_seen: bool = False


def _write_all(stream, payload: bytes) -> None:
    if stream is None:
        raise ValueError("fleet child command stream is closed")
    stream.write(payload)
    stream.flush()


def _write_atomic(core, path: Path, payload: bytes, mode: int = 0o600) -> None:
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC
    fd = os.open(temporary, flags, mode)
    try:
        offset = 0
        while offset < len(payload):
            offset += os.write(fd, payload[offset:])
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rename(temporary, path)
    directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def _copy_stable(core, source: Path, destination: Path, *, remove: bool = False) -> dict:
    payload, before = core._read_regular(source, maximum=2 * 1024 * 1024 * 1024)
    _write_atomic(core, destination, payload)
    record = core._file_record(destination)
    if record["sha256"] != core._hash(payload) or record["size"] != before.st_size:
        raise ValueError("copied residence artifact identity drift")
    if remove:
        source.unlink()
    return record


def _runtime_inputs(core, spec: dict) -> dict:
    required = {
        "format", "fleet_id", "engine", "client", "config", "film",
        "runtime", "module_aliases", "spectator", "target", "timeout_seconds",
        "installed_bundle", "lanes",
    }
    if set(spec) != required:
        raise ValueError("fleet run specification has unknown or missing fields")
    runtime = core._verify_file_record(spec["runtime"], "fleet runtime")
    if runtime.resolve() != Path(__file__).resolve():
        raise ValueError("fleet run specification names another runtime helper")
    client = core._verify_file_record(spec["client"], "client")
    config = core._verify_file_record(spec["config"], "config")
    film = core._verify_file_record(spec["film"], "film decoder")
    aliases = spec["module_aliases"]
    if not isinstance(aliases, list) or len(aliases) != 2:
        raise ValueError("fleet run needs two module aliases")
    for alias in aliases:
        core._verify_file_record(alias, "module alias")
    if aliases[0]["sha256"] != aliases[1]["sha256"]:
        raise ValueError("fleet module aliases differ")
    bundle, bundle_verifier = core._verify_installed_bundle(spec["installed_bundle"])
    roles = core._bundle_role_records(bundle)
    core._verify_bundle_copy(spec["config"], roles, "config", "config")
    for alias, role in zip(
            aliases, ("module-primary", "module-secondary"), strict=True):
        core._verify_bundle_copy(alias, roles, role, "module alias")
    if (not isinstance(spec["fleet_id"], str) or not spec["fleet_id"] or
            not isinstance(spec["spectator"], str) or not spec["spectator"] or
            not isinstance(spec["target"], str) or not spec["target"].startswith("[SG]") or
            type(spec["timeout_seconds"]) is not int or spec["timeout_seconds"] < 60):
        raise ValueError("invalid fleet run identity or timeout")
    return {"client": spec["client"], "config": spec["config"],
            "film": spec["film"], "runtime": spec["runtime"],
            "module_aliases": aliases, "client_path": client,
            "config_path": config, "film_path": film,
            "bundle": bundle, "bundle_roles": roles,
            "bundle_verifier": bundle_verifier}


def _lane_inputs(core, spec: dict, by_lane: dict,
                 bundle_roles: dict[str, dict]) -> dict[str, dict]:
    result = {}
    expected_keys = {
        "lane", "offset", "root", "maplist", "argv", "client_argv",
        "serverrecord_dir", "pov_demo", "artifacts",
    }
    for lane_spec in spec["lanes"]:
        if set(lane_spec) != expected_keys:
            raise ValueError("lane run specification has unknown or missing fields")
        lane = lane_spec["lane"]
        client_argv = lane_spec["client_argv"]
        if (not isinstance(client_argv, list) or not client_argv or
                client_argv[0] != spec["client"]["path"] or
                any(not isinstance(item, str) or not item or "\0" in item
                    for item in client_argv)):
            raise ValueError(f"invalid client argv for {lane}")
        serverrecord_dir = core._inside(
            Path(lane_spec["serverrecord_dir"]), by_lane[lane]["root"],
            f"{lane} serverrecord directory",
        )
        pov_demo = core._inside(
            Path(lane_spec["pov_demo"]), by_lane[lane]["root"],
            f"{lane} POV demo",
        )
        artifacts = lane_spec["artifacts"]
        core._verify_bundle_copy(
            lane_spec["maplist"], bundle_roles, f"maplist:{lane}", f"{lane} maplist"
        )
        if not isinstance(artifacts, dict) or set(artifacts) != set(core.CANONICAL_TOPMAPS):
            raise ValueError(f"{lane} artifact inventory is incomplete")
        for map_name, authority in artifacts.items():
            if not isinstance(authority, dict) or set(authority) != {
                    "bsp_file", "rune_file", "snag_file", "red_flag_origin",
                    "blue_flag_origin"}:
                raise ValueError(f"invalid artifact authority for {lane}/{map_name}")
            core._verify_bundle_copy(
                authority["bsp_file"], bundle_roles, f"bsp:{map_name}", "BSP"
            )
            core._verify_bundle_copy(
                authority["rune_file"], bundle_roles, f"rune:{map_name}", "RUNE"
            )
            core._verify_bundle_copy(
                authority["snag_file"], bundle_roles, f"snag:{map_name}", "SNAG"
            )
            core._vector(authority["red_flag_origin"], "red flag origin")
            core._vector(authority["blue_flag_origin"], "blue flag origin")
        result[lane] = {**by_lane[lane], **lane_spec,
                        "serverrecord_dir": serverrecord_dir, "pov_demo": pov_demo}
    return result


def _load_film(path: Path):
    module_spec = importlib.util.spec_from_file_location("fleet_film", path)
    if module_spec is None or module_spec.loader is None:
        raise ValueError("cannot load pinned film decoder")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def _config_payload(core, path: Path) -> bytes:
    payload, _info = core._read_regular(path, maximum=1024 * 1024)
    if not payload or not payload.endswith(b"\n") or b"\0" in payload:
        raise ValueError("fleet config is not a complete line command file")
    for line in payload.splitlines():
        if (not line or len(line) > 1024 or
                not (line.startswith(b"set ") or line.startswith(b"exec ")) or
                any(byte < 32 or byte > 126 for byte in line) or
                b";" in line or b"\\" in line):
            raise ValueError("fleet config contains a non-declarative command")
    return payload


def _spawn_client(core, lane: str, lane_spec: dict, client_record: dict, output):
    parent = os.getpid()
    process = subprocess.Popen(
        lane_spec["client_argv"], cwd=lane_spec["root"], stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        preexec_fn=lambda: core._arm_parent_death(parent),
    )
    os.set_blocking(process.stdout.fileno(), False)
    pidfd = os.pidfd_open(process.pid)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise ValueError(f"{lane} client exited during launch")
        if Path(f"/proc/{process.pid}/exe").resolve() == Path(client_record["path"]).resolve():
            break
        time.sleep(0.005)
    else:
        raise ValueError(f"{lane} client did not execute its pinned image")
    identity = {
        "pid": process.pid,
        "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
        "start_ticks": core._proc_start_ticks(process.pid),
        "executable": client_record,
        "argv": lane_spec["client_argv"],
        "command_sha256": core._hash_argv(lane_spec["client_argv"]),
        "pidfd_captured": True,
    }
    return process, pidfd, identity


def _start(core, spec: dict, lanes: dict, evidence: Path, *,
           lane_order=None, cycle_factory=None):
    lane_order = core.LANES if lane_order is None else tuple(lane_order)
    cycle_factory = core.FleetCycle if cycle_factory is None else cycle_factory
    children = []
    try:
        for lane in lane_order:
            child = core._spawn_held_engine(
                lane, spec["engine"], lanes[lane]["argv"], lanes[lane]["root"],
                subprocess.PIPE,
            )
            children.append(child)
        release_ns = time.monotonic_ns()
        for child in children:
            os.write(child.release_write, b"R\n")
            os.close(child.release_write)
            child.release_write = -1
        for child in children:
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                if Path(f"/proc/{child.process.pid}/exe").resolve() == \
                        Path(spec["engine"]["path"]).resolve():
                    break
                time.sleep(0.005)
            else:
                raise ValueError(f"{child.lane} engine image did not settle")
            core._capture_child(child, spec["engine"], lanes[child.lane]["argv"])
        runs = {}
        for child in children:
            lane = child.lane
            server_log = open(evidence / f"{lane}-server.log", "xb", buffering=0)
            client_log = open(evidence / f"{lane}-client.log", "xb", buffering=0)
            client, pidfd, client_identity = _spawn_client(
                core, lane, lanes[lane], spec["client"], client_log
            )
            runs[lane] = LaneRun(
                lane, lanes[lane], child, client,
                cycle_factory(lane, child.identity), server_log, client_log,
            )
            runs[lane].client_pidfd = pidfd
            runs[lane].client_identity = client_identity
        return runs, release_ns
    except BaseException:
        _stop_all(core, {child.lane: child for child in children}, {})
        raise


def _command(run: LaneRun, text: str) -> None:
    _write_all(run.engine.process.stdin, text.encode("ascii") + b"\n")


def _start_residence(core, spec: dict, run: LaneRun, map_name: str) -> None:
    sequence = run.cycle.sequence
    basename = f"fleet-{spec['fleet_id']}-{run.lane}-{sequence:02d}"
    run.server_demo = run.spec["serverrecord_dir"] / f"{basename}.dm2"
    run.pov_source = run.spec["pov_demo"]
    if run.server_demo.exists() or run.pov_source.exists():
        raise ValueError(f"{run.lane} residence output already exists")
    run.console = []
    run.roster = {}
    run.census = {}
    run.pov_started = False
    run.pov_stopped = False
    run.pov_requested = False
    _command(run, f"serverrecord {basename}")
    _command(run, "sv sg list")
    _maybe_start_pov(spec, run)


def _maybe_start_pov(spec: dict, run: LaneRun) -> None:
    if (run.cycle.sequence >= 0 and run.spectator_entered and
            len(run.roster) == 10 and not run.pov_requested):
        run.pov_requested = True
        _command(run, f"sv povrecord {spec['spectator']} {spec['target']}")


def _player_inventory(core, film, demo: dict, run: LaneRun, sequence: int) -> list[dict]:
    by_name = {}
    for client, epochs in demo.get("skin_epochs", {}).items():
        names = {value.split("\\", 1)[0] for _frame, value in epochs}
        if len(names) != 1:
            raise ValueError("serverrecord client identity changed inside residence")
        name = next(iter(names))
        if name in run.roster:
            by_name[name] = client + 1
    if set(by_name) != set(run.roster) or len(by_name) != 10:
        raise ValueError("serverrecord roster does not equal SG roster")
    players = []
    for name, (slot, team) in sorted(run.roster.items()):
        token = core._hash(
            f"{run.lane}\0{sequence}\0{slot}\0{by_name[name]}\0{name}".encode()
        )
        players.append({"client": by_name[name], "instance": token,
                        "name": name, "slot": slot, "team": team})
    core._verify_players(players)
    return players


def _finish_residence(core, film, spec: dict, run: LaneRun,
                      evidence: Path, bundle_id: str,
                      campaign_deadline: float) -> tuple[Path, dict]:
    sequence = run.cycle.sequence - 1
    map_name = core.expected_map(run.lane, sequence)
    if (sequence < 0 or sequence > 20 or run.server_demo is None or
            run.pov_source is None):
        raise ValueError(f"{run.lane} has an incomplete residence lifecycle")
    _await_pov_lifecycle(spec, run, campaign_deadline)
    directory = evidence / "receipts" / run.lane / f"{sequence:02d}"
    (directory / "segments").mkdir(parents=True)
    deadline = min(campaign_deadline, time.monotonic() + 5.0)
    while time.monotonic() < deadline:
        if run.server_demo.is_file() and run.pov_source.is_file():
            break
        time.sleep(0.02)
    else:
        raise ValueError(f"{run.lane} residence demos did not close")
    server = _copy_stable(core, run.server_demo, directory / "serverrecord.dm2")
    pov = _copy_stable(core, run.pov_source, directory / "pov.dm2", remove=True)
    demo = film.walk_demo(directory / "serverrecord.dm2", strict=True)
    if demo.get("map") != map_name or not demo.get("svrecord"):
        raise ValueError(f"{run.lane} serverrecord map or shape drift")
    wire = demo.get("wire_framenums")
    if not isinstance(wire, list) or not wire:
        raise ValueError("serverrecord has no wire frame authority")
    players = _player_inventory(core, film, demo, run, sequence)
    expected_names = {player["name"] for player in players}
    if (set(run.census) != expected_names or
            any(not frames or frames != sorted(set(frames)) for frames in run.census.values())):
        raise ValueError("residence census is missing, duplicate, or unordered")
    segment_payload = b"".join(run.console)
    segment_path = directory / "segments" / "console.log"
    _write_atomic(core, segment_path, segment_payload)
    segment = core._file_record(segment_path)
    artifact = run.spec["artifacts"][map_name]
    receipt = {
        "format": core.FORMAT_RECEIPT, "fleet_id": spec["fleet_id"],
        "bundle_id": bundle_id,
        "lane": run.lane, "offset": core.OFFSETS[core.LANES.index(run.lane)],
        "sequence": sequence, "map": map_name,
        "runner_sha256": core._hash(Path(core.__file__).read_bytes()),
        "topmaps_sha256": core.CANONICAL_TOPMAPS_SHA256,
        "engine_generation": run.engine.identity,
        "client_generation": run.client_identity,
        "bsp_file": artifact["bsp_file"], "rune_file": artifact["rune_file"],
        "rune_sha256": artifact["rune_file"]["sha256"],
        "snag_file": artifact["snag_file"], "sg_players": players,
        "residence": {"start_frame": wire[0] - 1, "end_frame": wire[-1],
                      "red_flag_origin": artifact["red_flag_origin"],
                      "blue_flag_origin": artifact["blue_flag_origin"]},
        "serverrecord": {"demo_path": server["path"],
                         "demo_sha256": server["sha256"], "demo_size": server["size"],
                         "demo_frame_range": {"start": 1,
                                              "end_exclusive": len(wire) + 1}},
        "console_segment": {"path": "console.log", "sha256": segment["sha256"],
                            "size": segment["size"]},
        "pov": {"demo_path": pov["path"], "demo_sha256": pov["sha256"],
                "demo_size": pov["size"], "spectator": spec["spectator"],
                "target": spec["target"], "start_confirmed": True,
                "stop_confirmed": True},
    }
    receipt["receipt_hash"] = core.receipt_hash(receipt)
    path = directory / "receipt.json"
    _write_atomic(core, path, core._canonical(receipt))
    return path, receipt


def _consume_engine(core, film, spec: dict, run: LaneRun, line: bytes,
                    evidence: Path, receipts: list, bundle_id: str,
                    campaign_deadline: float) -> None:
    run.server_log.write(line)
    if run.cycle.sequence >= 0:
        run.console.append(line)
    match = IDENTITY_RE.match(line.rstrip(b"\n"))
    if match:
        map_name = match.group(1).decode("ascii")
        prior = run.cycle.sequence
        run.cycle.map_committed(map_name, run.engine.identity)
        if prior >= 0:
            receipts.append(
                _finish_residence(
                    core, film, spec, run, evidence, bundle_id, campaign_deadline
                )
            )
        if not run.cycle.complete:
            _start_residence(core, spec, run, map_name)
        return
    match = EXIT_RE.match(line.rstrip(b"\n"))
    if match and run.cycle.sequence <= 20:
        run.cycle.level_exited(run.engine.identity)
        if run.pov_started and not run.pov_stopped:
            _command(run, f"sv povrecord off {spec['spectator']}")
        return
    match = CENSUS_RE.match(line.rstrip(b"\n"))
    if match:
        run.census.setdefault(match.group(1).decode(), []).append(int(match.group(2)))
        return
    match = ROSTER_RE.match(line.rstrip(b"\n"))
    if match:
        name = match.group(2).decode()
        run.roster[name] = (int(match.group(1)), 1 if match.group(3) == b"red" else 2)
        _maybe_start_pov(spec, run)
        return
    match = ROSTER_END_RE.match(line.rstrip(b"\n"))
    if match and int(match.group(1)) != 10:
        raise ValueError(f"{run.lane} roster is not exactly ten bots")
    if f"{spec['spectator']} entered the game".encode() in line:
        run.spectator_entered = True
        _maybe_start_pov(spec, run)
    if b"povrecord: directive rejected" in line:
        raise ValueError(f"{run.lane} POV directive was rejected")


def _consume_client(spec: dict, run: LaneRun, line: bytes) -> None:
    run.client_log.write(line)
    if b"recording to " in line and b"pov.dm2" in line:
        run.pov_started = True
    if (b"Stopped demo" in line or b"Stopped recording" in line or
            b"completed demo" in line):
        run.pov_stopped = True


def _drain_lines(buffer: bytearray, chunk: bytes):
    buffer.extend(chunk)
    while True:
        newline = buffer.find(b"\n")
        if newline < 0:
            return
        yield bytes(buffer[:newline + 1])
        del buffer[:newline + 1]


def _await_pov_lifecycle(spec: dict, run: LaneRun,
                         campaign_deadline: float) -> None:
    if run.pov_started and run.pov_stopped:
        return
    if run.client.stdout is None:
        raise ValueError(f"{run.lane} client output stream is closed")
    waiter = selectors.DefaultSelector()
    waiter.register(run.client.stdout, selectors.EVENT_READ)
    deadline = min(campaign_deadline, time.monotonic() + 5.0)
    try:
        while time.monotonic() < deadline:
            if run.client.poll() is not None:
                raise ValueError(f"{run.lane} client exited before POV completion")
            remaining = max(0.0, deadline - time.monotonic())
            events = waiter.select(min(0.1, remaining))
            if not events:
                continue
            try:
                chunk = os.read(run.client.stdout.fileno(), 65536)
            except BlockingIOError:
                continue
            if not chunk:
                raise ValueError(f"{run.lane} client output closed before POV completion")
            for line in _drain_lines(run.client_buffer, chunk):
                _consume_client(spec, run, line)
            if run.pov_started and run.pov_stopped:
                return
    finally:
        waiter.close()
    raise ValueError(f"{run.lane} client did not confirm POV completion")


def _stop_process(process, pidfd: int) -> None:
    if process.poll() is None:
        signal.pidfd_send_signal(pidfd, signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            signal.pidfd_send_signal(pidfd, signal.SIGKILL)
            process.wait(timeout=3)
    os.close(pidfd)
    if process.stdin is not None:
        process.stdin.close()
    if process.stdout is not None:
        process.stdout.close()


def _stop_all(core, engines: dict, runs: dict) -> None:
    for run in runs.values():
        try:
            _stop_process(run.client, run.client_pidfd)
        except (OSError, ProcessLookupError):
            pass
        run.server_log.close()
        run.client_log.close()
    values = engines.values() if isinstance(engines, dict) else engines
    for child in values:
        try:
            _stop_process(child.process, child.pidfd)
        except (OSError, ProcessLookupError):
            pass


def _freeze(root: Path) -> None:
    for directory, names, files in os.walk(root, topdown=False, followlinks=False):
        path = Path(directory)
        for name in files:
            os.chmod(path / name, 0o400)
        for name in names:
            os.chmod(path / name, 0o500)
        os.chmod(path, 0o500)


def run_fleet(core, spec_path: Path, state_root: Path, evidence_root: Path) -> None:
    """Run one 21-residence cycle per lane under ten unchanged engine PIDs."""
    core._reject_development_controller_environment()
    if state_root.exists() or evidence_root.exists():
        raise ValueError("fleet state and evidence roots must be absent")
    spec, by_lane = core._validate_run_spec(spec_path)
    inputs = _runtime_inputs(core, spec)
    bundle = inputs["bundle"]
    lanes = _lane_inputs(core, spec, by_lane, inputs["bundle_roles"])
    state_root.mkdir(mode=0o700)
    evidence_root.mkdir(mode=0o700)
    lock_path = state_root / "fleet.lock"
    lock_fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
    fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    runs = {}
    receipts = []
    try:
        runs, release_ns = _start(core, spec, lanes, evidence_root)
        owner_processes = {lane: runs[lane].engine.identity for lane in core.LANES}
        owner_clients = {lane: runs[lane].client_identity for lane in core.LANES}
        owner = {
            "format": core.FORMAT_OWNER, "state": "RUNNING",
            "fleet_id": spec["fleet_id"],
            "bundle_id": bundle["bundle_id"],
            "runner_sha256": core._hash(Path(core.__file__).read_bytes()),
            "topmaps_sha256": core.CANONICAL_TOPMAPS_SHA256,
            "release_monotonic_ns": release_ns, "processes": owner_processes,
            "clients": owner_clients,
            "inputs": {"engine": spec["engine"], "client": spec["client"],
                       "config": spec["config"], "film": spec["film"],
                       "module_aliases": spec["module_aliases"],
                       "runtime": spec["runtime"],
                       "installed_bundle": spec["installed_bundle"],
                       "bundle_verifier": inputs["bundle_verifier"]},
            "maplists": {lane: runs[lane].spec["maplist"] for lane in core.LANES},
            "ledger_entries": 0, "ledger_tail_hash": core.ZERO_HASH,
            "lock_path": str(lock_path.resolve()),
        }
        owner_path = state_root / "fleet-owner.json"
        _write_atomic(core, owner_path, core._canonical(owner))
        config_payload = _config_payload(core, inputs["config_path"])
        for lane in core.LANES:
            _write_all(runs[lane].engine.process.stdin, config_payload)
            _command(runs[lane], f"map {core.expected_map(lane, 0)}")
        film = _load_film(inputs["film_path"])
        selector = selectors.DefaultSelector()
        for lane, run in runs.items():
            selector.register(run.engine.process.stdout, selectors.EVENT_READ,
                              (lane, "engine"))
            selector.register(run.client.stdout, selectors.EVENT_READ, (lane, "client"))
        deadline = time.monotonic() + spec["timeout_seconds"]
        while not all(run.cycle.complete for run in runs.values()):
            if time.monotonic() >= deadline:
                raise ValueError("persistent fleet cycle exceeded its timeout")
            events = selector.select(0.25)
            if not events:
                if any(run.engine.process.poll() is not None or run.client.poll() is not None
                       for run in runs.values()):
                    raise ValueError("fleet child exited before native cycle completion")
                continue
            for key, _mask in events:
                lane, kind = key.data
                try:
                    chunk = os.read(key.fileobj.fileno(), 65536)
                except BlockingIOError:
                    continue
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                run = runs[lane]
                buffer = run.engine_buffer if kind == "engine" else run.client_buffer
                for line in _drain_lines(buffer, chunk):
                    if kind == "engine":
                        _consume_engine(
                            core, film, spec, run, line, evidence_root, receipts,
                            bundle["bundle_id"], deadline,
                        )
                    else:
                        _consume_client(spec, run, line)
        selector.close()
        _stop_all(core, {lane: run.engine for lane, run in runs.items()}, runs)
        runs = {}
        if len(receipts) != 210:
            raise ValueError("fleet did not publish exactly 210 residences")
        ledger = []
        previous = core.ZERO_HASH
        receipts_by_key = {(receipt["lane"], receipt["sequence"]): (path, receipt)
                           for path, receipt in receipts}
        for index in range(210):
            lane, sequence = core.LANES[index // 21], index % 21
            path, receipt = receipts_by_key[(lane, sequence)]
            entry = {"format": core.FORMAT_LEDGER, "index": index,
                     "previous_hash": previous,
                     "receipt_path": path.relative_to(evidence_root).as_posix(),
                     "receipt_hash": receipt["receipt_hash"]}
            entry["entry_hash"] = core.ledger_entry_hash(entry)
            previous = entry["entry_hash"]
            ledger.append(entry)
        _write_atomic(core, evidence_root / "evidence-ledger.jsonl",
                      b"".join(core._canonical(entry) for entry in ledger))
        owner.update(state="SAFE_STOPPED", ledger_entries=210,
                     ledger_tail_hash=previous)
        _write_atomic(core, owner_path.with_name("fleet-owner.final.json"),
                      core._canonical(owner))
        os.replace(owner_path.with_name("fleet-owner.final.json"), owner_path)
        state_fd = os.open(state_root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(state_fd)
        finally:
            os.close(state_fd)
        _freeze(evidence_root)
        _freeze(state_root)
        fcntl.flock(lock_fd, fcntl.LOCK_UN)
        os.close(lock_fd)
        lock_fd = -1
        core.verify_stopped_residence_evidence(state_root, evidence_root)
    except BaseException as exc:
        if runs:
            _stop_all(core, {lane: run.engine for lane, run in runs.items()}, runs)
        try:
            _write_atomic(core, state_root / "failure.json",
                          core._canonical({"error": str(exc), "state": "FAILED"}))
        except OSError:
            pass
        raise
    finally:
        if lock_fd >= 0:
            try:
                fcntl.flock(lock_fd, fcntl.LOCK_UN)
            finally:
                os.close(lock_fd)


def _route_inputs(core, spec: dict) -> dict:
    client = core._verify_file_record(spec["client"], "route-only client")
    config = core._verify_file_record(spec["route_config"], "route-only config")
    film = core._verify_file_record(spec["film"], "route-only film decoder")
    runtime = core._verify_file_record(spec["runtime"], "route-only runtime")
    if runtime.resolve() != Path(__file__).resolve():
        raise ValueError("route-only run specification names another runtime helper")
    aliases = spec["module_aliases"]
    if not isinstance(aliases, list) or len(aliases) != 2:
        raise ValueError("route-only run needs two module aliases")
    for alias in aliases:
        core._verify_file_record(alias, "route-only module alias")
    if aliases[0]["sha256"] != aliases[1]["sha256"]:
        raise ValueError("route-only module aliases differ")
    bundle, bundle_verifier = core._verify_installed_bundle(spec["installed_bundle"])
    roles = core._bundle_role_records(bundle)
    core._verify_bundle_copy(spec["route_config"], roles, "route-only-config",
                             "route-only config")
    for alias, role in zip(aliases, ("module-primary", "module-secondary"), strict=True):
        core._verify_bundle_copy(alias, roles, role, "route-only module alias")
    controller_results = core._validate_route_controller_authority(
        spec["controller_authority"], roles, spec["engine"], aliases
    )
    return {
        "client": spec["client"], "client_path": client,
        "route_config": spec["route_config"], "route_config_path": config,
        "film": spec["film"], "film_path": film, "runtime": spec["runtime"],
        "module_aliases": aliases, "bundle": bundle, "bundle_roles": roles,
        "bundle_verifier": bundle_verifier, "controller_results": controller_results,
    }


def _route_lane_inputs(core, lanes: dict, bundle_roles: dict[str, dict],
                       lane_order: tuple[str, ...]) -> None:
    for lane in lane_order:
        map_name = core.expected_route_only_map(lane)
        artifacts = lanes[lane]["artifacts"]
        runtime_files = core._route_runtime_files(
            lane, map_name, lanes[lane]["game_root"], bundle_roles
        )
        for field, role, label in (
                ("bsp_file", f"bsp:{map_name}", "BSP"),
                ("rune_file", f"rune:{map_name}", "RUNE"),
                ("snag_file", f"snag:{map_name}", "SNAG")):
            core._verify_bundle_copy(artifacts[field], bundle_roles, role,
                                     f"{lane} {label}")
            if artifacts[field] != runtime_files[field]:
                raise ValueError(f"{lane} supplied {label} is not its loaded runtime file")
        lanes[lane]["runtime_files"] = runtime_files


def _start_route_match(core, spec: dict, run: LaneRun) -> None:
    map_name = core.expected_route_only_map(run.lane)
    basename = f"route-only-{spec['campaign_id']}-{run.lane}"
    run.server_demo = run.spec["serverrecord_dir"] / f"{basename}.dm2"
    run.pov_source = run.spec["pov_demo"]
    if (run.server_demo.exists() or run.pov_source.exists() or
            run.spec["statsdb_backup"].exists()):
        raise ValueError(f"{run.lane} route-only output already exists")
    run.console = []
    run.roster = {}
    run.census = {}
    run.pov_started = False
    run.pov_stopped = False
    run.pov_requested = False
    run.route_ready = False
    run.rune_ready = False
    run.exit_frame = None
    run.exit_time = None
    run.backup_confirmed = False
    run.backup_requested = False
    run.timelimit_seen = False
    _command(run, f"serverrecord {basename}")
    _command(run, "sv sg list")
    _maybe_start_pov(spec, run)
    if map_name != run.spec["map"]:
        raise ValueError(f"{run.lane} route-only map identity drift")


def _finish_route_match(core, film, spec: dict, run: LaneRun,
                        evidence: Path, bundle_id: str,
                        controller_results: dict[str, dict],
                        campaign_deadline: float) -> tuple[Path, dict]:
    map_name = core.expected_route_only_map(run.lane)
    if (run.server_demo is None or run.pov_source is None or
            not run.route_ready or not run.rune_ready or
            run.exit_frame is None or run.exit_time is None or run.exit_time < 600.0 or
            not run.backup_confirmed or not run.spec["statsdb_backup"].is_file()):
        raise ValueError(f"{run.lane} route-only match lifecycle is incomplete")
    _await_pov_lifecycle(spec, run, campaign_deadline)
    directory = evidence / "receipts" / run.lane
    (directory / "segments").mkdir(parents=True)
    deadline = min(campaign_deadline, time.monotonic() + 5.0)
    while time.monotonic() < deadline:
        if run.server_demo.is_file() and run.pov_source.is_file():
            break
        time.sleep(0.02)
    else:
        raise ValueError(f"{run.lane} route-only demos did not close")
    server = _copy_stable(core, run.server_demo, directory / "serverrecord.dm2")
    pov = _copy_stable(core, run.pov_source, directory / "pov.dm2", remove=True)
    database = _copy_stable(core, run.spec["statsdb_backup"],
                            directory / "session.db")
    demo = film.walk_demo(directory / "serverrecord.dm2", strict=True)
    if (demo.get("map") != map_name or demo.get("svrecord") is not True or
            demo.get("parse_complete") is not True or demo.get("terminated") is not True):
        raise ValueError(f"{run.lane} route-only serverrecord map or shape drift")
    wire = demo.get("wire_framenums")
    if (not isinstance(wire, list) or not wire or run.exit_frame != wire[-1] or
            wire != list(range(wire[0], wire[-1] + 1)) or len(wire) < 6000):
        raise ValueError("route-only serverrecord has no wire frame authority")
    players = _player_inventory(core, film, demo, run, 0)
    segment_payload = b"".join(run.console)
    segment_path = directory / "segments" / "console.log"
    _write_atomic(core, segment_path, segment_payload)
    segment = core._file_record(segment_path)
    artifact = run.spec["artifacts"]
    measurement = {"start_frame": wire[0] - 1, "end_frame": wire[-1]}
    try:
        lines = segment_payload.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ValueError("route-only console is not ASCII") from exc
    behavior = core._route_telemetry(
        lines, players, measurement, Path(artifact["rune_file"]["path"]), map_name, demo
    )
    behavior["session"] = core._route_session_database(
        directory / "session.db", players, map_name
    )
    receipt = {
        "format": core.FORMAT_ROUTE_RECEIPT, "campaign_id": spec["campaign_id"],
        "bundle_id": bundle_id, "lane": run.lane, "map": map_name,
        "runner_sha256": core._hash(Path(core.__file__).read_bytes()),
        "engine_generation": run.engine.identity,
        "client_generation": run.client_identity,
        "bsp_file": artifact["bsp_file"], "rune_file": artifact["rune_file"],
        "rune_sha256": artifact["rune_file"]["sha256"],
        "snag_file": artifact["snag_file"], "sg_players": players,
        "residence": {"start_frame": wire[0] - 1, "end_frame": wire[-1],
                      "measurement": measurement, "exit_frame": run.exit_frame,
                      "exit_time_seconds": run.exit_time},
        "serverrecord": {"demo_path": server["path"], "demo_sha256": server["sha256"],
                         "demo_size": server["size"],
                         "demo_frame_range": {"start": 1,
                                              "end_exclusive": len(wire) + 1}},
        "console_segment": {"path": "console.log", "sha256": segment["sha256"],
                            "size": segment["size"]},
        "runtime_files": run.spec["runtime_files"],
        "pov": {"demo_path": pov["path"], "demo_sha256": pov["sha256"],
                "demo_size": pov["size"], "spectator": spec["spectator"],
                "target": spec["target"], "start_confirmed": True,
                "stop_confirmed": True},
        "session_database": database,
        "runtime": {"identity_committed": True, "route_contract": "local-only",
                    "rune_ready": True},
        "behavior": behavior, "controller_result": controller_results[map_name],
    }
    receipt["receipt_hash"] = core.receipt_hash(receipt)
    path = directory / "receipt.json"
    _write_atomic(core, path, core._canonical(receipt))
    return path, receipt


def _consume_route_engine(core, film, spec: dict, run: LaneRun, line: bytes,
                          evidence: Path, receipts: list, bundle_id: str,
                          controller_results: dict[str, dict],
                          campaign_deadline: float) -> None:
    run.server_log.write(line)
    stripped = line.rstrip(b"\n")
    identity = IDENTITY_RE.match(stripped)
    if identity:
        map_name = identity.group(1).decode("ascii")
        prior = run.cycle.sequence
        if prior >= 0:
            run.console.append(line)
        run.cycle.map_committed(map_name, run.engine.identity)
        if prior < 0:
            _start_route_match(core, spec, run)
            run.console.append(line)
        else:
            receipts.append(_finish_route_match(
                core, film, spec, run, evidence, bundle_id, controller_results,
                campaign_deadline,
            ))
        return
    if run.cycle.sequence >= 0:
        run.console.append(line)
    if ROUTE_CONTRACT_RE.fullmatch(stripped):
        run.route_ready = True
        return
    ready = RUNE_READY_RE.match(stripped)
    if ready:
        if ready.group(1).decode("ascii") != core.expected_route_only_map(run.lane):
            raise ValueError(f"{run.lane} route-only RUNE-ready map drift")
        run.rune_ready = True
        return
    if TIMELIMIT_RE.fullmatch(stripped):
        if run.timelimit_seen or run.backup_requested:
            raise ValueError(f"{run.lane} route-only timelimit repeated")
        run.timelimit_seen = True
        # EndDMLevel/BeginIntermission has committed the match before q2ded
        # processes its next stdin command.  Back up in that intermission
        # window; waiting for EXITLEVEL races the queued gamemap command.
        _command(run, "sv statsdb backup route-only-session.db")
        run.backup_requested = True
        return
    exited = EXIT_RE.match(stripped)
    if exited:
        run.cycle.level_exited(run.engine.identity)
        run.exit_frame = int(exited.group(1))
        run.exit_time = float(exited.group(2))
        if not run.timelimit_seen or run.exit_time < 600.0:
            raise ValueError(f"{run.lane} route-only match did not reach timelimit")
        if run.pov_started and not run.pov_stopped:
            _command(run, f"sv povrecord off {spec['spectator']}")
        return
    census = CENSUS_RE.match(stripped)
    if census:
        run.census.setdefault(census.group(1).decode(), []).append(int(census.group(2)))
        return
    roster = ROSTER_RE.match(stripped)
    if roster:
        name = roster.group(2).decode()
        run.roster[name] = (int(roster.group(1)), 1 if roster.group(3) == b"red" else 2)
        _maybe_start_pov(spec, run)
        return
    roster_end = ROSTER_END_RE.match(stripped)
    if roster_end and int(roster_end.group(1)) != 10:
        raise ValueError(f"{run.lane} route-only roster is not exactly ten bots")
    backup = STATS_BACKUP_RE.match(stripped)
    if backup:
        try:
            backup_name = Path(backup.group(1).decode("ascii")).name
        except UnicodeDecodeError as exc:
            raise ValueError("route-only stats backup name is not ASCII") from exc
        if backup_name != run.spec["statsdb_backup"].name:
            raise ValueError(f"{run.lane} route-only stats backup path drift")
        run.backup_confirmed = True
    if f"{spec['spectator']} entered the game".encode() in line:
        run.spectator_entered = True
        _maybe_start_pov(spec, run)
    if b"povrecord: directive rejected" in line:
        raise ValueError(f"{run.lane} route-only POV directive was rejected")


def _graceful_route_engine_stop(runs: dict[str, LaneRun]) -> None:
    for run in runs.values():
        _command(run, "quit")
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if all(run.engine.process.poll() is not None for run in runs.values()):
            return
        time.sleep(0.02)
    raise ValueError("route-only engine did not exit after authenticated quit")


def run_route_only(core, spec_path: Path, state_root: Path, evidence_root: Path) -> None:
    """Run the controller-selected ordinary-match exception subset."""
    core._reject_development_controller_environment()
    if state_root.exists() or evidence_root.exists():
        raise ValueError("route-only state and evidence roots must be absent")
    spec, lanes = core._validate_route_only_run_spec(spec_path)
    inputs = _route_inputs(core, spec)
    lane_order = core._validate_route_only_lane_selection(
        lanes, inputs["controller_results"]
    )
    _route_lane_inputs(core, lanes, inputs["bundle_roles"], lane_order)
    state_root.mkdir(mode=0o700)
    evidence_root.mkdir(mode=0o700)
    lock_path = state_root / "route-only.lock"
    lock_fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
    fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    runs = {}
    receipts = []
    try:
        release_ns = 0
        if lane_order:
            runs, release_ns = _start(
                core, spec, lanes, evidence_root, lane_order=lane_order,
                cycle_factory=core.RouteOnlyCycle,
            )
        owner = {
            "format": core.FORMAT_ROUTE_OWNER, "state": "RUNNING",
            "campaign_id": spec["campaign_id"], "bundle_id": inputs["bundle"]["bundle_id"],
            "runner_sha256": core._hash(Path(core.__file__).read_bytes()), "no_op": not lane_order,
            "release_monotonic_ns": release_ns, "graceful_quit": False,
            "selected_lanes": list(lane_order), "selected_count": len(lane_order),
            "processes": {lane: runs[lane].engine.identity for lane in lane_order},
            "clients": {lane: runs[lane].client_identity for lane in lane_order},
            "inputs": {"engine": spec["engine"], "client": spec["client"],
                       "route_config": spec["route_config"], "film": spec["film"],
                       "runtime": spec["runtime"], "module_aliases": spec["module_aliases"],
                       "installed_bundle": spec["installed_bundle"],
                       "bundle_verifier": inputs["bundle_verifier"],
                       **core._route_helper_records()},
            "controller_authority": spec["controller_authority"],
            "matches": {lane: core.expected_route_only_map(lane)
                        for lane in lane_order},
            "lane_runtime": {lane: lanes[lane]["runtime_files"]
                             for lane in lane_order},
            "ledger_entries": 0, "ledger_tail_hash": core.ZERO_HASH,
            "lock_path": str(lock_path.resolve()),
        }
        owner_path = state_root / "route-only-owner.json"
        _write_atomic(core, owner_path, core._canonical(owner))
        if not lane_order:
            _write_atomic(core, evidence_root / "route-only-ledger.jsonl", b"")
            owner.update(state="SAFE_STOPPED")
            _write_atomic(core, owner_path.with_name("route-only-owner.final.json"),
                          core._canonical(owner))
            os.replace(owner_path.with_name("route-only-owner.final.json"), owner_path)
            _freeze(evidence_root)
            _freeze(state_root)
            fcntl.flock(lock_fd, fcntl.LOCK_UN)
            os.close(lock_fd)
            lock_fd = -1
            core.verify_stopped_route_only_evidence(state_root, evidence_root)
            return
        config = _config_payload(core, inputs["route_config_path"])
        for lane in lane_order:
            _write_all(runs[lane].engine.process.stdin, config)
            _command(runs[lane], f"map {core.expected_route_only_map(lane)}")
        film = _load_film(inputs["film_path"])
        selector = selectors.DefaultSelector()
        for lane, run in runs.items():
            selector.register(run.engine.process.stdout, selectors.EVENT_READ, (lane, "engine"))
            selector.register(run.client.stdout, selectors.EVENT_READ, (lane, "client"))
        deadline = time.monotonic() + spec["timeout_seconds"]
        while not all(run.cycle.complete for run in runs.values()):
            if time.monotonic() >= deadline:
                raise ValueError("route-only campaign exceeded its timeout")
            events = selector.select(0.25)
            if not events:
                if any(run.engine.process.poll() is not None or run.client.poll() is not None
                       for run in runs.values()):
                    raise ValueError("route-only child exited before match completion")
                continue
            for key, _mask in events:
                lane, kind = key.data
                try:
                    chunk = os.read(key.fileobj.fileno(), 65536)
                except BlockingIOError:
                    continue
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                run = runs[lane]
                buffer = run.engine_buffer if kind == "engine" else run.client_buffer
                for line in _drain_lines(buffer, chunk):
                    if kind == "engine":
                        _consume_route_engine(
                            core, film, spec, run, line, evidence_root, receipts,
                            inputs["bundle"]["bundle_id"], inputs["controller_results"],
                            deadline,
                        )
                    else:
                        _consume_client(spec, run, line)
        selector.close()
        if len(receipts) != len(lane_order):
            raise ValueError("route-only campaign did not publish each selected receipt")
        _graceful_route_engine_stop(runs)
        owner["graceful_quit"] = True
        _stop_all(core, {lane: run.engine for lane, run in runs.items()}, runs)
        runs = {}
        previous, ledger = core.ZERO_HASH, []
        by_lane = {receipt["lane"]: (path, receipt) for path, receipt in receipts}
        for index, lane in enumerate(lane_order):
            path, receipt = by_lane[lane]
            entry = {"format": core.FORMAT_ROUTE_LEDGER, "index": index,
                     "previous_hash": previous,
                     "receipt_path": path.relative_to(evidence_root).as_posix(),
                     "receipt_hash": receipt["receipt_hash"]}
            entry["entry_hash"] = core.ledger_entry_hash(entry)
            previous = entry["entry_hash"]
            ledger.append(entry)
        _write_atomic(core, evidence_root / "route-only-ledger.jsonl",
                      b"".join(core._canonical(entry) for entry in ledger))
        owner.update(
            state="SAFE_STOPPED", ledger_entries=len(lane_order), ledger_tail_hash=previous
        )
        _write_atomic(core, owner_path.with_name("route-only-owner.final.json"),
                      core._canonical(owner))
        os.replace(owner_path.with_name("route-only-owner.final.json"), owner_path)
        _freeze(evidence_root)
        _freeze(state_root)
        fcntl.flock(lock_fd, fcntl.LOCK_UN)
        os.close(lock_fd)
        lock_fd = -1
        core.verify_stopped_route_only_evidence(state_root, evidence_root)
    except BaseException as exc:
        if runs:
            _stop_all(core, {lane: run.engine for lane, run in runs.items()}, runs)
        try:
            _write_atomic(core, state_root / "failure.json",
                          core._canonical({"error": str(exc), "state": "FAILED"}))
        except OSError:
            pass
        raise
    finally:
        if lock_fd >= 0:
            try:
                fcntl.flock(lock_fd, fcntl.LOCK_UN)
            finally:
                os.close(lock_fd)
