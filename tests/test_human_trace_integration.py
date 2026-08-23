from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HumanTraceIntegrationTest(unittest.TestCase):
    def test_capture_wraps_the_real_pmove_boundary(self) -> None:
        source = (ROOT / "p_client.c").read_text(encoding="utf-8")
        entry = source.index("void ClientThink (edict_t *ent, usercmd_t *ucmd)")
        body = source[entry:source.index("void ClientBeginServerFrame", entry)]
        prepare = body.index("SG_HumanSpeedPmoveBegin(ent, &pm.s, pm.cmd.msec);")
        snapshot = body.index("human_trace_before = pm.s;")
        pmove = body.index("gi.Pmove (&pm);")
        record = body.index(
            "SG_HumanTracePmove(ent, &human_trace_before, &pm);")
        self.assertLess(prepare, snapshot)
        self.assertLess(snapshot, pmove)
        self.assertLess(pmove, record)

    def test_capture_is_owned_by_every_production_build(self) -> None:
        for name in ("GNUmakefile", "Makefile"):
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertIn("slipgate/sg_human_trace.o", source, name)
            self.assertIn("human-trace-test", source, name)
        project = (ROOT / "gravity.vcxproj").read_text(encoding="utf-8")
        self.assertIn(r"slipgate\sg_human_trace.c", project)
        self.assertIn(r"slipgate\sg_human_trace.h", project)

    def test_capture_is_disabled_by_default_and_map_independent(self) -> None:
        cvars = (ROOT / "slipgate" / "sg_cvars.h").read_text(encoding="utf-8")
        source = (ROOT / "slipgate" / "sg_human_trace.c").read_text(
            encoding="utf-8")
        self.assertIn('X(humantrace, "sg_humantrace", "0")', cvars)
        for map_name in (
                "lmctf01", "lmctf12", "lmctf19", "tomb05", "xmap13",
                "xmap18"):
            self.assertNotIn(map_name, source)


if __name__ == "__main__":
    unittest.main()
