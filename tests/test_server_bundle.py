import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

from tools import server_bundle


ROOT = Path(__file__).resolve().parents[1]
MAPS = tuple(
    line for line in (ROOT / "tools" / "rune-corpus-maps.txt").read_text().splitlines()
    if line
)
TOPMAPS = tuple(
    line for line in (ROOT / "tools" / "topmaps.txt").read_text().splitlines()
    if line and not line.startswith("#")
)
LANES = tuple(f"s{number:02d}" for number in range(1, 11))


def load_fleet_runner():
    path = ROOT / "tools" / "fleet-runner.py"
    spec = importlib.util.spec_from_file_location("server_bundle_fleet_test", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


class BundleFixture:
    def __init__(self, root: Path, label: str, module: bytes):
        self.root = root / label
        self.root.mkdir()
        self.entries = []
        self._add("module-primary", "game/game.so", module)
        self._add("module-secondary", "game/gamex86_64.so", module)
        self._add("pak", "game/lmctf6-buzzmod.pak", b"pak\n")
        self.config = b"set dedicated 1\n"
        self._add("config", "game/server.cfg", self.config)
        self._add(
            "topmaps", "game/topmaps.txt", (ROOT / "tools" / "topmaps.txt").read_bytes()
        )
        for offset, lane in enumerate(LANES):
            rotation = TOPMAPS[offset:] + TOPMAPS[:offset]
            self._add(
                f"maplist:{lane}", f"game/maplists/{lane}.txt",
                ("\n".join(rotation) + "\n").encode(),
            )
        for map_name in MAPS:
            self._add(f"bsp:{map_name}", f"game/maps/{map_name}.bsp", b"bsp\n")
            self._add(f"rune:{map_name}", f"game/maps/{map_name}.rune", b"rune\n")
            self._add(f"snag:{map_name}", f"game/maps/{map_name}.snag", b"snag\n")

    def _add(self, role: str, relative: str, payload: bytes):
        source = self.root / relative.replace("/", "-")
        source.write_bytes(payload)
        self.entries.append({"source": str(source), "path": relative, "role": role})

    def write_spec(self) -> Path:
        value = {
            "format": "lmctf-server-bundle-build-v1",
            "identities": {
                "source": "1" * 64,
                "engine": "2" * 64,
                "rune_format": "3" * 64,
                "action_contract": "4" * 64,
                "mechanism_contract": "5" * 64,
                "configuration": hashlib.sha256(self.config).hexdigest(),
            },
            "files": self.entries,
        }
        path = self.root / "build-spec.json"
        path.write_bytes(canonical(value))
        return path


class ServerBundleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        for directory, names, files in os.walk(self.root, topdown=False):
            for name in names + files:
                try:
                    Path(directory, name).chmod(0o700)
                except FileNotFoundError:
                    pass
        self.temporary.cleanup()

    def build(self, label: str, module: bytes):
        fixture = BundleFixture(self.root, label, module)
        archive = self.root / f"{label}.tar"
        manifest = self.root / f"{label}.json"
        server_bundle.build_bundle(fixture.write_spec(), archive, manifest)
        return archive, manifest, server_bundle.verify_release(manifest, archive)

    def test_build_install_failure_recovery_and_rollback(self):
        archive_a, manifest_a, release_a = self.build("a", b"module-a\n")
        install = self.root / "installed"
        state_a = server_bundle.install_bundle(
            manifest_a, archive_a, install, expected_active=None
        )
        self.assertEqual(release_a["bundle_id"], state_a["active"]["bundle_id"])
        verified_a = server_bundle.verify_state_file(install / "install-state.json")
        self.assertEqual(state_a, verified_a)
        fleet = load_fleet_runner()
        active, verifier = fleet._verify_installed_bundle(
            fleet._file_record(install / "install-state.json")
        )
        self.assertEqual(release_a["bundle_id"], active["bundle_id"])
        self.assertEqual(
            verifier["sha256"],
            hashlib.sha256((ROOT / "tools" / "server_bundle.py").read_bytes()).hexdigest(),
        )
        roles = fleet._bundle_role_records(active)
        module_copy = self.root / "module-copy.so"
        module_copy.write_bytes(b"module-a\n")
        fleet._verify_bundle_copy(
            fleet._file_record(module_copy), roles, "module-primary", "module copy"
        )
        module_copy.write_bytes(b"wrong\n")
        with self.assertRaisesRegex(ValueError, "bundle"):
            fleet._verify_bundle_copy(
                fleet._file_record(module_copy), roles, "module-primary", "module copy"
            )
        generation_a = Path(state_a["active"]["generation_root"])
        self.assertFalse(stat.S_IMODE(generation_a.stat().st_mode) & 0o222)
        by_role = {item["role"]: item for item in state_a["active"]["files"]}
        self.assertEqual(
            by_role["module-primary"]["sha256"],
            by_role["module-secondary"]["sha256"],
        )

        archive_b, manifest_b, release_b = self.build("b", b"module-b\n")
        os.environ["LMCTF_BUNDLE_FAULT"] = "before-state-replace"
        try:
            with self.assertRaisesRegex(server_bundle.BundleError, "injected"):
                server_bundle.install_bundle(
                    manifest_b, archive_b, install,
                    expected_active=release_a["bundle_id"],
                )
        finally:
            os.environ.pop("LMCTF_BUNDLE_FAULT", None)
        self.assertTrue((install / ".install-state.json.tmp").is_file())
        self.assertEqual(
            release_a["bundle_id"],
            server_bundle.verify_state_file(install / "install-state.json")["active"]["bundle_id"],
        )
        state_b = server_bundle.install_bundle(
            manifest_b, archive_b, install, expected_active=release_a["bundle_id"]
        )
        self.assertFalse((install / ".install-state.json.tmp").exists())
        self.assertEqual(release_b["bundle_id"], state_b["active"]["bundle_id"])
        self.assertEqual(release_a["bundle_id"], state_b["rollback"]["bundle_id"])

        with self.assertRaisesRegex(server_bundle.BundleError, "expected active"):
            server_bundle.install_bundle(
                manifest_a, archive_a, install, expected_active="0" * 64
            )
        rolled = server_bundle.rollback_bundle(
            install, expected_active=release_b["bundle_id"],
            target_bundle=release_a["bundle_id"],
        )
        self.assertEqual(release_a["bundle_id"], rolled["active"]["bundle_id"])
        self.assertEqual(release_b["bundle_id"], rolled["rollback"]["bundle_id"])
        repeated = server_bundle.rollback_bundle(
            install, expected_active=release_a["bundle_id"],
            target_bundle=release_a["bundle_id"],
        )
        self.assertEqual(rolled, repeated)

    def test_mismatch_partial_generation_and_alias_drift_fail_closed(self):
        archive, manifest, release = self.build("good", b"same-module\n")
        damaged = self.root / "damaged.tar"
        damaged.write_bytes(archive.read_bytes() + b"changed")
        with self.assertRaisesRegex(server_bundle.BundleError, "archive identity"):
            server_bundle.verify_release(manifest, damaged)

        bad = BundleFixture(self.root, "bad-alias", b"one\n")
        secondary = next(item for item in bad.entries if item["role"] == "module-secondary")
        Path(secondary["source"]).write_bytes(b"two\n")
        with self.assertRaisesRegex(server_bundle.BundleError, "module aliases"):
            server_bundle.build_bundle(
                bad.write_spec(), self.root / "bad.tar", self.root / "bad.json"
            )

        rotation = BundleFixture(self.root, "bad-rotation", b"same\n")
        lane = next(item for item in rotation.entries if item["role"] == "maplist:s01")
        Path(lane["source"]).write_text("lmctf09\n")
        with self.assertRaisesRegex(server_bundle.BundleError, "rotation"):
            server_bundle.build_bundle(
                rotation.write_spec(),
                self.root / "bad-rotation.tar",
                self.root / "bad-rotation.json",
            )

        install = self.root / "partial"
        generations = install / "generations"
        generation = generations / release["bundle_id"]
        generation.mkdir(parents=True)
        (generation / "partial").write_text("partial")
        with self.assertRaisesRegex(server_bundle.BundleError, "generation"):
            server_bundle.install_bundle(
                manifest, archive, install, expected_active=None
            )

    def test_active_generation_cannot_escape_the_install_root(self):
        archive, manifest, _release = self.build("escape", b"module\n")
        first, second = self.root / "first", self.root / "second"
        server_bundle.install_bundle(manifest, archive, first, expected_active=None)
        server_bundle.install_bundle(manifest, archive, second, expected_active=None)
        first_state = json.loads((first / "install-state.json").read_text())
        second_state = json.loads((second / "install-state.json").read_text())
        first_state["active"] = second_state["active"]
        state_path = first / "install-state.json"
        state_path.chmod(0o600)
        state_path.write_bytes(canonical(first_state))
        state_path.chmod(0o444)
        with self.assertRaisesRegex(server_bundle.BundleError, "escapes"):
            server_bundle.verify_state_file(state_path)


if __name__ == "__main__":
    unittest.main()
