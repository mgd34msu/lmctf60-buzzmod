#!/usr/bin/env python3
"""Focused canonical RUNE reader coverage for film and seedservo."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from tests.test_rune_artifact import _build_rune
import rune_contracts_generated as contract
import runeio
import seedservo


def _load_film_reader_module():
    """Load film's reader without requiring its optional rendering stack."""
    numpy = types.ModuleType("numpy")
    numpy.linspace = lambda *_args, **_kwargs: ()
    matplotlib = types.ModuleType("matplotlib")
    matplotlib.use = lambda *_args, **_kwargs: None
    pyplot = types.ModuleType("matplotlib.pyplot")
    pyplot.get_cmap = lambda _name: lambda values: values
    collections = types.ModuleType("matplotlib.collections")
    collections.LineCollection = object
    lines = types.ModuleType("matplotlib.lines")
    lines.Line2D = object
    colors = types.ModuleType("matplotlib.colors")
    colors.ListedColormap = lambda values: values
    module_name = "film_rune_reader_test"
    spec = importlib.util.spec_from_file_location(module_name, TOOLS / "film.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    with patch.dict(sys.modules, {
        "numpy": numpy,
        "matplotlib": matplotlib,
        "matplotlib.pyplot": pyplot,
        "matplotlib.collections": collections,
        "matplotlib.lines": lines,
        "matplotlib.colors": colors,
        module_name: module,
    }):
        spec.loader.exec_module(module)
    return module


film = _load_film_reader_module()


class RuneToolReaderTests(unittest.TestCase):
    def _write_rune(self, data: bytes) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "runetest.rune"
        path.write_bytes(data)
        return path

    def test_tools_read_seed_positions_and_counts_from_current_artifact(self):
        data = _build_rune()
        self.assertEqual(160, runeio.RUNE_HEADER_BYTES)
        self.assertEqual(runeio.RUNE_HEADER_BYTES,
                         struct.unpack_from("<H", data, 6)[0])
        path = self._write_rune(data)

        self.assertEqual([(0.0, 0.0), (128.0, 0.0)],
                         film.load_rune_seeds(path))
        seeds, links = seedservo.load_rune(path)
        self.assertEqual([(0.0, 0.0, 0.0), (128.0, 0.0, 0.0)], seeds)
        self.assertEqual(2, len(links))
        self.assertIsInstance(links[0], runeio.RuneLink)

    def test_tools_reject_nonzero_header_reserved_slot(self):
        malformed = bytearray(_build_rune())
        struct.pack_into("<H", malformed, 4, 1)
        path = self._write_rune(bytes(malformed))

        for reader in (film.load_rune_seeds, seedservo.load_rune):
            with self.subTest(reader=reader.__module__), \
                    self.assertRaises(runeio.RuneWireError) as caught:
                reader(path)
            self.assertEqual(contract.RLR_NONZERO_RESERVED,
                             caught.exception.code)


if __name__ == "__main__":
    unittest.main()
