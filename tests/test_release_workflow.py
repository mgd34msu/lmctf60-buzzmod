#!/usr/bin/env python3
"""Pin the release workflow's cross-dialect behavior-test authority."""
from pathlib import Path
import re
import unittest


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


if __name__ == '__main__':
    unittest.main()
