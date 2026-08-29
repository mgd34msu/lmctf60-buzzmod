#!/usr/bin/env python3
"""Pin the release workflow's cross-dialect behavior-test authority."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / '.github/workflows/build.yml'

OFFLINE_PHASE_GENERATOR_SOURCES = {
    'slipgate/sg_authority_entropy.c',
    'slipgate/sg_bsp_completeness_proof.c',
    'slipgate/sg_bsp_completeness_core.c',
    'slipgate/sg_bsp_completeness_region.c',
    'slipgate/sg_bsp_completeness_traversal.c',
    'slipgate/sg_bsp_completeness_lattice.c',
    'slipgate/sg_bsp_completeness_coverage.c',
    'slipgate/sg_bsp_completeness_state.c',
    'slipgate/sg_bsp_completeness_portal.c',
    'slipgate/sg_bsp_completeness_portal_index.c',
    'slipgate/sg_configuration_semantics.c',
    'slipgate/sg_configuration_lattice.c',
    'slipgate/sg_configuration_space.c',
    'slipgate/sg_configuration_audit.c',
    'slipgate/sg_mechanism_capability.c',
    'slipgate/sg_mechanism_capability_seal.c',
    'slipgate/sg_phase_catalog.c',
    'slipgate/sg_phase_catalog_audit.c',
    'slipgate/sg_phase_catalog_publication.c',
    'slipgate/sg_phase_catalog_owner.c',
    'slipgate/sg_phase_mover_support_provider.c',
}

RUNTIME_MODEL_SOURCE = 'slipgate/sg_rune_model.c'
RUNTIME_MODEL_HEADER = 'slipgate/sg_rune_model.h'


def make_object_inventory(path, variable):
    lines = path.read_text(encoding='utf-8').splitlines()
    assignment = re.compile(rf'^{re.escape(variable)}\s*[:+?]?=')
    for first, line in enumerate(lines):
        if not assignment.match(line):
            continue
        logical = [line.partition('=')[2]]
        index = first
        while lines[index].rstrip().endswith('\\'):
            index += 1
            logical.append(lines[index])
        return {
            token
            for token in ' '.join(logical).replace('\\', ' ').split()
            if token.endswith('.o')
        }
    raise AssertionError(f'{variable} is absent from {path.name}')


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
        self.assertIn("grep -Eq 'lib(isl|gmp)\\.so'", self.source)

    def test_release_cannot_bypass_host_matrix(self):
        self.assertRegex(
            self.source,
            r'(?m)^\s+needs: \[ version, linux, verification, windows \]$')
        self.assertIn('if grep -q "warning:" host-test.log;', self.source)

    def test_host_matrix_installs_its_sheet_test_runtime(self):
        verification = self.source.partition('\n  verification:\n')[2].partition(
            '\n  windows:\n')[0]
        self.assertTrue(verification)
        setup_python = (
            'uses: actions/setup-python@'
            '5fda3b95a4ea91299a34e894583c3862153e4b97 # v7.0.0'
        )
        self.assertIn(
            setup_python,
            verification,
        )
        self.assertIn('python-version: "3.14"', verification)
        self.assertIn(
            'python3 -m venv "$RUNNER_TEMP/slipgate-film"', verification)
        self.assertIn(
            '"$RUNNER_TEMP/slipgate-film/bin/pip" install -r '
            'tools/requirements.txt',
            verification,
        )
        self.assertIn(
            'FILM_PYTHON="$RUNNER_TEMP/slipgate-film/bin/python"',
            verification,
        )
        self.assertLess(
            verification.index(setup_python),
            verification.index(
                'python3 -m venv "$RUNNER_TEMP/slipgate-film"'),
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

    def test_offline_phase_generator_stays_out_of_runtime_modules(self):
        offline_objects = {
            source.removesuffix('.c') + '.o'
            for source in OFFLINE_PHASE_GENERATOR_SOURCES
        }
        runtime_object = RUNTIME_MODEL_SOURCE.removesuffix('.c') + '.o'
        for make_name, variable in (
                ('Makefile', 'OBJS'), ('GNUmakefile', 'C_OBJS')):
            objects = make_object_inventory(ROOT / make_name, variable)
            self.assertIn(runtime_object, objects, make_name)
            self.assertEqual(set(), offline_objects & objects, make_name)
            make_text = (ROOT / make_name).read_text(encoding='utf-8')
            for forbidden in ('ISL_CFLAGS', 'ISL_LIBS', '-lisl', '-lgmp'):
                self.assertNotIn(forbidden, make_text, make_name)

        namespace = {'ms': 'http://schemas.microsoft.com/developer/msbuild/2003'}
        for name in ('gravity.vcxproj', 'gravity.vcxproj.filters'):
            document = ET.parse(ROOT / name).getroot()
            sources = document.findall('.//ms:ClCompile', namespace)
            headers = document.findall('.//ms:ClInclude', namespace)
            source_paths = [
                item.attrib['Include'].replace('\\', '/')
                for item in sources if 'Include' in item.attrib
            ]
            header_paths = [
                item.attrib['Include'].replace('\\', '/')
                for item in headers if 'Include' in item.attrib
            ]
            self.assertEqual(
                1, source_paths.count(RUNTIME_MODEL_SOURCE), name)
            self.assertEqual(
                1, header_paths.count(RUNTIME_MODEL_HEADER), name)
            self.assertEqual(
                set(), OFFLINE_PHASE_GENERATOR_SOURCES & set(source_paths), name)
            if name == 'gravity.vcxproj':
                dependencies = ' '.join(
                    item.text or '' for item in document.findall(
                        './/ms:AdditionalDependencies', namespace))
                self.assertNotRegex(
                    dependencies.lower(),
                    r'\b(?:lib)?(?:isl|gmp)(?:[-_.][^;\s]+)?\.lib\b')
                configurations = {
                    item.attrib.get('Include')
                    for item in document.findall(
                        './/ms:ProjectConfiguration', namespace)
                }
                self.assertTrue(
                    {'Release|Win32', 'Release|x64'} <= configurations)
                for item in sources:
                    if (item.attrib.get('Include', '').replace('\\', '/') ==
                            RUNTIME_MODEL_SOURCE):
                        self.assertEqual(
                            [], item.findall('ms:ExcludedFromBuild', namespace))
            else:
                for item in sources:
                    if (item.attrib.get('Include', '').replace('\\', '/') ==
                            RUNTIME_MODEL_SOURCE):
                        self.assertEqual(
                            'Source Files', item.find('ms:Filter', namespace).text)
                for item in headers:
                    if (item.attrib.get('Include', '').replace('\\', '/') ==
                            RUNTIME_MODEL_HEADER):
                        self.assertEqual(
                            'Header Files', item.find('ms:Filter', namespace).text)

    def test_offline_phase_generator_owns_full_solver_stack(self):
        runner = (ROOT / 'tests/run_sg_phase_catalog_test.sh').read_text(
            encoding='utf-8')
        for source in OFFLINE_PHASE_GENERATOR_SOURCES:
            self.assertIn(source, runner)
        self.assertIn('isl_cflags=$(pkg-config --cflags isl)', runner)
        self.assertIn('isl_libs=$(pkg-config --libs isl)', runner)
        for make_name in ('Makefile', 'GNUmakefile'):
            make_text = (ROOT / make_name).read_text(encoding='utf-8')
            target = make_text.partition('phase-catalog-publication-test:')[2]
            self.assertTrue(target, make_name)
            self.assertIn('tests/run_sg_phase_catalog_test.sh', target, make_name)

    def test_drop_live_results_are_initialized_for_strict_compilers(self):
        for source in (
            "slipgate/sg_compound_drop_live.c",
            "slipgate/sg_compound_drop_live_finish.c",
        ):
            text = (ROOT / source).read_text()
            self.assertNotIn("sg_drop_live_result_t live_result;", text, source)


if __name__ == '__main__':
    unittest.main()
