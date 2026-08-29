from __future__ import annotations

import hashlib
import os
from pathlib import Path
import stat
import struct
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock

from tools import rune_pair_preflight
from tests.test_rune_artifact import _build_rune, _fix_header_crc


def _rune_for_map(map_name: str) -> bytes:
    encoded = bytearray(_build_rune())
    encoded[64:128] = map_name.encode("ascii") + b"\0" * (64 - len(map_name))
    _fix_header_crc(encoded)
    return bytes(encoded)


class RunePreflightTest(unittest.TestCase):
    def test_valid_rune_needs_no_sidecar(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            (maps / "lmctf01.rune").write_bytes(_rune_for_map("lmctf01"))
            rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    def test_embedded_map_name_must_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            (maps / "lmctf01.rune").write_bytes(_rune_for_map("lmctf02"))
            with self.assertRaisesRegex(ValueError, "authenticates map"):
                rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    def test_malformed_rune_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            (maps / "lmctf01.rune").write_bytes(struct.pack("<I", 0))
            with self.assertRaises(ValueError):
                rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    def test_symlink_rune_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            target = maps / "target.rune"
            target.write_bytes(_rune_for_map("lmctf01"))
            try:
                (maps / "lmctf01.rune").symlink_to(target)
            except OSError as exc:
                self.skipTest(f"symlinks are unavailable: {exc}")
            with self.assertRaisesRegex(ValueError, "regular file|symlink"):
                rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    def test_symlinked_maps_directory_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            container = Path(temporary)
            maps = container / "maps"
            maps.mkdir()
            (maps / "lmctf01.rune").write_bytes(_rune_for_map("lmctf01"))
            linked_maps = container / "linked-maps"
            try:
                linked_maps.symlink_to(maps, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"directory symlinks are unavailable: {exc}")
            with self.assertRaisesRegex(ValueError, "symlink path component"):
                rune_pair_preflight.validate_runes(linked_maps, ("lmctf01",))
            with mock.patch.object(
                    rune_pair_preflight, "_HAS_PINNED_PARENT", False):
                with self.assertRaisesRegex(ValueError, "symlink path component"):
                    rune_pair_preflight.validate_runes(
                        linked_maps, ("lmctf01",))

    @unittest.skipUnless(
        rune_pair_preflight._HAS_PINNED_PARENT,
        "parent swap proof requires openat directory handles",
    )
    def test_parent_swap_before_leaf_open_cannot_redirect_read(self):
        with tempfile.TemporaryDirectory() as temporary:
            container = Path(temporary)
            maps = container / "maps"
            retained_maps = container / "retained-maps"
            attacker_maps = container / "attacker-maps"
            maps.mkdir()
            attacker_maps.mkdir()
            valid_rune = _rune_for_map("lmctf01")
            malformed_rune = bytearray(valid_rune)
            struct.pack_into("<I", malformed_rune, 0, 0)
            (maps / "lmctf01.rune").write_bytes(malformed_rune)
            (attacker_maps / "lmctf01.rune").write_bytes(valid_rune)
            probe = container / "symlink-probe"
            try:
                probe.symlink_to(attacker_maps, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"directory symlinks are unavailable: {exc}")
            probe.unlink()
            original_open = os.open
            swaps = 0

            def swap_parent_before_open(path, flags, *args, **kwargs):
                nonlocal swaps
                if Path(path).name == "lmctf01.rune":
                    swaps += 1
                    maps.rename(retained_maps)
                    maps.symlink_to(attacker_maps, target_is_directory=True)
                    try:
                        return original_open(path, flags, *args, **kwargs)
                    finally:
                        maps.unlink()
                        retained_maps.rename(maps)
                return original_open(path, flags, *args, **kwargs)

            def weak_identity(info):
                return stat.S_IFMT(info.st_mode), info.st_size, None, None

            with mock.patch.object(
                    rune_pair_preflight, "_file_id", return_value=None), \
                    mock.patch.object(
                        rune_pair_preflight, "_portable_identity",
                        side_effect=weak_identity), \
                    mock.patch.object(
                        os, "open", side_effect=swap_parent_before_open):
                with self.assertRaises(ValueError):
                    rune_pair_preflight.validate_runes(maps, ("lmctf01",))
            self.assertGreater(swaps, 0)

    def test_multiply_linked_rune_fails_closed_when_reported(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            rune_path = maps / "lmctf01.rune"
            rune_path.write_bytes(_rune_for_map("lmctf01"))
            try:
                os.link(rune_path, maps / "rune-alias")
            except OSError as exc:
                self.skipTest(f"hard links are unavailable: {exc}")
            if rune_path.stat().st_nlink <= 1:
                self.skipTest("filesystem does not report multiple links")
            with self.assertRaisesRegex(ValueError, "multiple links"):
                rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO requires POSIX mkfifo")
    def test_fifo_rune_fails_closed_without_waiting_for_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            rune_path = maps / "lmctf01.rune"
            os.mkfifo(rune_path)
            writer_errors = []

            def write_valid_rune():
                try:
                    with rune_path.open("wb", buffering=0) as stream:
                        stream.write(_rune_for_map("lmctf01"))
                except OSError as exc:
                    writer_errors.append(exc)

            writer = threading.Thread(target=write_valid_rune)
            writer.start()
            release_fd = None
            try:
                with self.assertRaisesRegex(ValueError, "regular file"):
                    rune_pair_preflight.validate_runes(maps, ("lmctf01",))
            finally:
                if writer.is_alive():
                    release_fd = os.open(rune_path, os.O_RDONLY | os.O_NONBLOCK)
                # Bound teardown if failed preflight leaves the FIFO writer blocked.
                writer.join(10)
                if release_fd is not None:
                    os.close(release_fd)
            self.assertFalse(writer.is_alive(), "FIFO writer did not exit")
            if writer_errors:
                self.assertIsInstance(writer_errors[0], BrokenPipeError)

    def test_named_rune_replacement_during_read_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            rune_path = maps / "lmctf01.rune"
            replacement = maps / "replacement.rune"
            rune_path.write_bytes(_rune_for_map("lmctf01"))
            replacement.write_bytes(_rune_for_map("lmctf01"))
            original_read = os.read
            replaced = False

            def replace_after_read(fd, count):
                nonlocal replaced
                payload = original_read(fd, count)
                if payload and not replaced:
                    replaced = True
                    os.replace(replacement, rune_path)
                return payload

            with mock.patch.object(os, "read", side_effect=replace_after_read):
                with self.assertRaisesRegex(ValueError, "changed|replaced"):
                    rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    def test_open_rune_mutation_during_read_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            maps = Path(temporary)
            rune_path = maps / "lmctf01.rune"
            rune_path.write_bytes(_rune_for_map("lmctf01"))
            original_read = os.read
            mutated = False

            def mutate_after_read(fd, count):
                nonlocal mutated
                payload = original_read(fd, count)
                if payload and not mutated:
                    mutated = True
                    rune_path.write_bytes(_rune_for_map("lmctf02"))
                return payload

            with mock.patch.object(os, "read", side_effect=mutate_after_read):
                with self.assertRaisesRegex(ValueError, "changed"):
                    rune_pair_preflight.validate_runes(maps, ("lmctf01",))

    def test_stable_reader_sha_binds_the_decoded_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            rune_path = Path(temporary) / "lmctf01.rune"
            payload = _rune_for_map("lmctf01")
            rune_path.write_bytes(payload)
            rune, digest = rune_pair_preflight._read_stable_rune(rune_path)
            self.assertEqual("lmctf01", rune.header.map_name)
            self.assertEqual(hashlib.sha256(payload).hexdigest(), digest)

    def test_meaningful_path_and_handle_file_id_mismatch_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            rune_path = Path(temporary) / "lmctf01.rune"
            rune_path.write_bytes(_rune_for_map("lmctf01"))
            actual = os.lstat(rune_path)
            mismatched = SimpleNamespace(
                st_mode=actual.st_mode,
                st_size=actual.st_size,
                st_mtime_ns=actual.st_mtime_ns,
                st_ctime_ns=actual.st_ctime_ns,
                st_dev=actual.st_dev,
                st_ino=actual.st_ino + 1,
                st_nlink=actual.st_nlink,
            )
            with mock.patch.object(
                    rune_pair_preflight, "_stat_leaf", return_value=mismatched):
                with self.assertRaisesRegex(ValueError, "replaced"):
                    rune_pair_preflight._read_named_once(rune_path)

    def test_unavailable_path_file_id_uses_portable_checks(self):
        with tempfile.TemporaryDirectory() as temporary:
            rune_path = Path(temporary) / "lmctf01.rune"
            payload = _rune_for_map("lmctf01")
            rune_path.write_bytes(payload)
            actual = os.lstat(rune_path)
            unavailable = SimpleNamespace(
                st_mode=actual.st_mode,
                st_size=actual.st_size,
                st_mtime_ns=actual.st_mtime_ns,
                st_ctime_ns=actual.st_ctime_ns,
                st_dev=actual.st_dev,
                st_ino=0,
                st_nlink=actual.st_nlink,
            )
            with mock.patch.object(
                    rune_pair_preflight, "_stat_leaf", return_value=unavailable):
                observed, file_id = rune_pair_preflight._read_named_once(rune_path)
            self.assertEqual(payload, observed)
            self.assertIsNone(file_id)

    def test_windows_binary_open_flag_is_used_when_available(self):
        with tempfile.TemporaryDirectory() as temporary:
            rune_path = Path(temporary) / "lmctf01.rune"
            rune_path.write_bytes(_rune_for_map("lmctf01"))
            original_open = os.open
            binary_flag = 1 << 29
            observed_flags = []

            def open_without_synthetic_flag(path, flags, *args, **kwargs):
                observed_flags.append(flags)
                return original_open(
                    path, flags & ~binary_flag, *args, **kwargs)

            with mock.patch.object(os, "O_BINARY", binary_flag, create=True):
                with mock.patch.object(
                        rune_pair_preflight, "_HAS_PINNED_PARENT", False):
                    with mock.patch.object(
                            os, "open", side_effect=open_without_synthetic_flag):
                        rune_pair_preflight._read_stable_rune(rune_path)
            self.assertTrue(observed_flags)
            self.assertTrue(all(flags & binary_flag for flags in observed_flags))

    def test_portable_policy_does_not_require_device_or_inode_identity(self):
        fixtures = {
            "linux-ext4": (2049, 42, 1, (2049, 42)),
            "macos-apfs": (1, 42, 1, (1, 42)),
            "windows-ntfs": (0, 42, 1, (0, 42)),
            "linux-exfat-without-inode": (2049, 0, 1, None),
        }
        for name, (device, inode, links, expected_file_id) in fixtures.items():
            with self.subTest(name=name):
                info = SimpleNamespace(
                    st_mode=stat.S_IFREG | 0o644,
                    st_size=123,
                    st_mtime_ns=100,
                    st_ctime_ns=200,
                    st_dev=device,
                    st_ino=inode,
                    st_nlink=links,
                )
                self.assertEqual(expected_file_id, rune_pair_preflight._file_id(info))
                self.assertFalse(rune_pair_preflight._has_multiple_links(info))

    def test_link_policy_rejects_only_an_affirmative_multiple_link_report(self):
        for name, links, rejected in (
                ("linux-hardlink", 2, True),
                ("macos-hardlink", 2, True),
                ("windows-ntfs-hardlink", 2, True),
                ("linux-exfat-single-link", 1, False),
                ("unavailable-link-count", 0, False),
                ("missing-link-count", None, False)):
            with self.subTest(name=name):
                info = SimpleNamespace(st_nlink=links)
                self.assertEqual(
                    rejected, rune_pair_preflight._has_multiple_links(info))

    def test_pair_compatibility_api_is_deleted(self):
        self.assertFalse(hasattr(rune_pair_preflight, "validate_pairs"))


if __name__ == "__main__":
    unittest.main()
