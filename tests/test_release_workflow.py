#!/usr/bin/env python3
"""Pin the release workflow's cross-dialect behavior-test authority."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / '.github/workflows/build.yml'


class ReleaseWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = WORKFLOW.read_text(encoding='utf-8')

    def test_host_matrix_covers_both_dialects_and_compilers(self):
        self.assertRegex(self.source, r'(?m)^  verification:\n')
        for name, build_file, compiler in (
                ('GNUmakefile GCC', 'GNUmakefile', 'gcc'),
                ('Makefile GCC', 'Makefile', 'gcc'),
                ('GNUmakefile Clang', 'GNUmakefile', 'clang'),
                ('Makefile Clang', 'Makefile', 'clang')):
            row = (rf'- name: {re.escape(name)}\n'
                   rf'\s+build_file: {re.escape(build_file)}\n'
                   rf'\s+compiler: {compiler}\n')
            self.assertRegex(self.source, row)
        self.assertIn('CC="${{ matrix.compiler }}" all host-test', self.source)
        self.assertIn('set -o pipefail', self.source)
        self.assertIn('ldd -r "$module"', self.source)

    def test_release_cannot_bypass_host_matrix(self):
        self.assertRegex(
            self.source,
            r'(?m)^\s+needs: \[ version, linux, verification, windows \]$')
        self.assertIn('if grep -q "warning:" host-test.log;', self.source)

    def test_host_matrix_installs_its_sheet_test_runtime(self):
        self.assertIn(
            'python3 -m venv "$RUNNER_TEMP/slipgate-film"', self.source)
        self.assertIn(
            '"$RUNNER_TEMP/slipgate-film/bin/pip" install -r '
            'tools/requirements.txt',
            self.source,
        )
        self.assertIn(
            'FILM_PYTHON="$RUNNER_TEMP/slipgate-film/bin/python"',
            self.source,
        )

    def test_windows_project_builds_current_traversal_sources(self):
        required = {
            r"slipgate\sg_compound_publication_build.c",
            r"slipgate\sg_rocketjump_live.c",
            r"slipgate\sg_rocketjump_cadence.c",
            r"slipgate\sg_rocketjump_game.c",
            r"slipgate\sg_combat_land_lead.c",
        }
        namespace = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
        project = ET.parse(ROOT / "gravity.vcxproj").getroot()
        filters = ET.parse(ROOT / "gravity.vcxproj.filters").getroot()
        for name, document in (("project", project), ("filters", filters)):
            sources = {
                item.attrib["Include"]
                for item in document.findall(".//ms:ClCompile", namespace)
                if "Include" in item.attrib
            }
            self.assertEqual(set(), required - sources, name)


if __name__ == '__main__':
    unittest.main()
