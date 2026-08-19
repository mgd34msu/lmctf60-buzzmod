#!/usr/bin/env python3
"""Reject retired RUNE vocabulary from the one-layout surface."""
from __future__ import annotations

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION_SUFFIXES = {
    '.c', '.cc', '.cpp', '.h', '.hpp', '.py', '.sh', '.def', '.vcxproj',
    '.filters',
}
PRODUCTION_FILENAMES = {'GNUmakefile', 'Makefile'}
EXCLUDED_TOP_LEVEL = {'.git', '.goodvibes', 'assets', 'recovery', 'tests'}


def production_surface_paths() -> tuple[Path, ...]:
    """Return every source/tool/build surface that could hide a second codec."""
    paths = []
    for path in ROOT.rglob('*'):
        relative = path.relative_to(ROOT)
        if not path.is_file() or relative.parts[0] in EXCLUDED_TOP_LEVEL:
            continue
        if (path.suffix in PRODUCTION_SUFFIXES or
                path.name in PRODUCTION_FILENAMES or
                (relative.parts[0] == 'slipgate' and path.suffix == '.json')):
            paths.append(path)
    return tuple(sorted(paths))
RESIDUE = re.compile(
    r'\b(?:forensic|legacy)\s+(?:rune|artifact|wire|sidecar)\b|'
    r'\brune[-_ ](?:active|v[0-9])\b|\bACTIVE_RUNE\b|\bRUNE_V[0-9]\b|'
    r'\brune\b.*\bactive[-_ ](?:format|artifact|reader|codec|wire|sidecar)\b|'
    r'\b(?:rune_version|format_version|mechanism_schema|controller_revision)\b|'
    r'\brune\b.*\brevision\b|\brevision\b.*\brune\b',
    re.IGNORECASE,
)


class RuneNamingTests(unittest.TestCase):
    def test_one_layout_surface_has_no_retired_residue(self):
        hits = []
        paths = production_surface_paths()
        covered = {path.relative_to(ROOT).as_posix() for path in paths}
        self.assertIn('slipgate/sg_rune_file.c', covered)
        self.assertIn('slipgate/sg_rune_contract.h', covered)
        self.assertIn('tools/runeio.py', covered)
        self.assertIn('gravity.vcxproj', covered)
        for path in paths:
            for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
                if RESIDUE.search(line):
                    hits.append(f'{path.relative_to(ROOT)}:{number}:{line.strip()}')
        self.assertEqual([], hits, '\n'.join(hits))

    def test_sidecar_magics_are_neutral(self):
        text = (ROOT / 'tools/sidecario.py').read_text(encoding='utf-8')
        for magic in ('HMNR', 'HMLR', 'HMER', 'DPOR', 'DNGR'):
            self.assertIn(magic, text)
        self.assertNotRegex(text, r'HMN4|HML4|HME4|DPO4|DNG4')


if __name__ == '__main__':
    unittest.main()
