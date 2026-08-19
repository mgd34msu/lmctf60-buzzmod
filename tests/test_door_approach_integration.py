#!/usr/bin/env python3
"""Pin production build and symbol wiring for the door-approach reducer."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
OBJECT = "slipgate/sg_door_approach.o"
SOURCE = "slipgate/sg_door_approach.c"
HEADER = "slipgate/sg_door_approach.h"


def assignment(text: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^{re.escape(name)}\s*(?::=|=)\s*(.*?)(?=\n\S|\Z)", text
    )
    if match is None:
        raise AssertionError(f"missing {name} assignment")
    return match.group(1).replace("\\\n", " ")


class DoorApproachIntegrationTest(unittest.TestCase):
    def test_production_object_lists_and_host_aggregates(self) -> None:
        for name, object_variable in (
            ("GNUmakefile", "C_OBJS"),
            ("Makefile", "OBJS"),
        ):
            source = (ROOT / name).read_text(encoding="utf-8")
            objects = assignment(source, object_variable).split()
            self.assertEqual(objects.count(OBJECT), 1, name)


            flavor = "gnu" if name == "GNUmakefile" else "make"
            button_objects = assignment(
                source, "BUTTON_GAME_TEST_OBJS"
            ).split()
            self.assertIn(
                f".sg_door_approach_under_test.{flavor}.o",
                button_objects,
                name,
            )

            host_start = source.index("host-test:")
            host_end = source.index("\n\naction-test:", host_start)
            host_block = source[host_start:host_end]
            self.assertIn("$(DOOR_APPROACH_TEST_BIN)", host_block, name)
            self.assertIn(
                "$(DOOR_APPROACH_INTEGRATION_TEST)", host_block, name
            )
            self.assertIn("./$(DOOR_APPROACH_TEST_BIN)", host_block, name)
            self.assertIn(
                "python3 -B $(DOOR_APPROACH_INTEGRATION_TEST)", host_block,
                name,
            )

    def test_gnu_path_dependency_target_is_exact(self) -> None:
        source = (ROOT / "GNUmakefile").read_text(encoding="utf-8")
        dep_start = source.index("$(DEPEND_FILE):")
        dep_end = source.index("\nstripcr:", dep_start)
        dep = source[dep_start:dep_end]
        generic_start = dep.index("$(CC) $(CPPFLAGS) -MM $(filter-out")
        generic_end = dep.index(") > \"$$tmp\";", generic_start)
        self.assertIn(SOURCE, dep[generic_start:generic_end])
        explicit = (
            "$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_door_approach.o \\\n"
            "\t\tslipgate/sg_door_approach.c >> \"$$tmp\";"
        )
        self.assertEqual(dep.count(explicit), 1)

    def test_visual_studio_project_and_filters_own_source_and_header(self) -> None:
        for name in ("gravity.vcxproj", "gravity.vcxproj.filters"):
            ET.parse(ROOT / name)
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertEqual(
                source.count(r'slipgate\sg_door_approach.c'), 1, name
            )
            self.assertEqual(
                source.count(r'slipgate\sg_door_approach.h'), 1, name
            )
        filters = (ROOT / "gravity.vcxproj.filters").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            filters,
            r'(?s)<ClCompile Include="slipgate\\sg_door_approach\.c">'
            r'.*?<Filter>Source Files</Filter>',
        )
        self.assertRegex(
            filters,
            r'(?s)<ClInclude Include="slipgate\\sg_door_approach\.h">'
            r'.*?<Filter>Header Files</Filter>',
        )

    def test_production_call_surface_resolves_to_reducer_object(self) -> None:
        header = (ROOT / HEADER).read_text(encoding="utf-8")
        production = "\n".join(
            (ROOT / path).read_text(encoding="utf-8")
            for path in ("slipgate/sg_move.c", "slipgate/sg_oracle.c")
        )
        calls = set(re.findall(r"\bSG_DoorApproach[A-Za-z0-9_]+", production))
        exports = set(re.findall(r"\bSG_DoorApproach[A-Za-z0-9_]+\s*\(", header))
        exports = {name.rstrip("(").strip() for name in exports}
        self.assertTrue(calls)
        self.assertLessEqual(calls, exports)

        probe_source = r'''
#include "g_local.h"
#include "slipgate/sg_door_approach.h"

int main(void)
{
    sg_door_approach_state_t state = { 0 };
    sg_door_approach_observation_t observation = { 0 };
    short point[3] = { 0, 0, 0 };

    SG_DoorApproachReset(&state);
    (void)SG_DoorApproachPmoveEqual(&observation.pms, &observation.pms);
    (void)SG_DoorApproachInsideCapsule(point, point, point);
    (void)SG_DoorApproachBegin(&state, point, point, &observation);
    (void)SG_DoorApproachPreStep(&state, &observation, 25);
    (void)SG_DoorApproachPostStep(&state, &observation, 25);
    (void)SG_DoorApproachSnapped(&state, &observation);
    return SG_DoorApproachReasonName(SG_DOOR_APPROACH_REASON_NONE) ? 0 : 1;
}
'''
        with tempfile.TemporaryDirectory(prefix="sg-door-wiring-") as tmp:
            tmp_path = Path(tmp)
            probe = tmp_path / "probe.c"
            probe.write_text(probe_source, encoding="utf-8")
            probe_object = tmp_path / "probe.o"
            reducer_object = tmp_path / "sg_door_approach.o"
            binary = tmp_path / "probe"
            common = [
                "gcc", "-std=c11", "-I.", "-DSTDC_HEADERS",
                '-DARCH="x86_64"', '-DVER="door-wiring-test"', "-DLINUX",
            ]
            subprocess.run(
                common + ["-c", str(probe), "-o", str(probe_object)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                common + ["-c", SOURCE, "-o", str(reducer_object)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            missing = subprocess.run(
                ["gcc", str(probe_object), "-lm", "-o", str(binary)],
                cwd=ROOT, check=False, capture_output=True, text=True,
            )
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("SG_DoorApproach", missing.stderr)
            subprocess.run(
                ["gcc", str(probe_object), str(reducer_object), "-lm",
                 "-o", str(binary)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(binary)], cwd=ROOT, check=True,
                capture_output=True, text=True,
            )


if __name__ == "__main__":
    unittest.main()
