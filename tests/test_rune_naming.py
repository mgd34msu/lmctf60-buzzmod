#!/usr/bin/env python3
"""Reject retired RUNE vocabulary from the one-layout surface."""
from __future__ import annotations

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATHS = (
    ROOT / 'slipgate/rune_actions.json',
    ROOT / 'slipgate/sg_action_contract.generated.h',
    ROOT / 'slipgate/sg_rune_contract.h',
    ROOT / 'slipgate/sg_sidecar_wire.h',
    ROOT / 'tools/gen_rune_contracts.py',
    ROOT / 'tools/rune_contracts_generated.py',
    ROOT / 'tools/runeio.py',
    ROOT / 'tools/sidecario.py',
    ROOT / 'tools/corpusgraph.py',
    ROOT / 'tools/runelint.py',
    ROOT / 'tools/runeview.py',
    ROOT / 'tools/film.py', ROOT / 'tools/seedservo.py',
    ROOT / 'GNUmakefile', ROOT / 'Makefile', ROOT / 'tools/README.md',
)
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
        for path in PATHS:
            if not path.exists():
                continue
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
