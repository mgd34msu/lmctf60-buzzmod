#!/usr/bin/env python3
"""Pin the production ownership boundary for human-speed movement."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HumanSpeedIntegrationTest(unittest.TestCase):
    def test_clientthink_owns_every_real_pmove(self) -> None:
        source = (ROOT / "p_client.c").read_text(encoding="utf-8")
        entry = source.index("void ClientThink (edict_t *ent, usercmd_t *ucmd)")
        body = source[entry : source.index("void ClientBeginServerFrame", entry)]

        client = body.index("client = ent->client;")
        boundary = body.index("SG_HumanSpeedClientThinkBegin(ent);")
        begin = body.index("SG_HumanSpeedPmoveBegin(ent, &pm.s, pm.cmd.msec);")
        pmove = body.index("gi.Pmove (&pm);")
        end = body.index("SG_HumanSpeedPmoveEnd(ent, &pm.s, pm.cmd.msec);")
        self.assertLess(client, boundary)
        self.assertLess(boundary, begin)
        self.assertLess(begin, pmove)
        self.assertLess(pmove, end)
        self.assertEqual(body.count("gi.Pmove (&pm);"), 1)

    def test_chain_marks_only_the_immediate_command(self) -> None:
        source = (ROOT / "slipgate" / "sg_move.c").read_text(encoding="utf-8")
        marker = (
            "bot->as_landing_command = as_ok && as_chain &&\n"
            "\t\t\t\t    !proved_control && !door_hold;"
        )
        marker_at = source.index(marker)
        think_at = source.index("ClientThink(e, cmd);", marker_at)
        pending = source[marker_at + len(marker) : think_at]
        self.assertNotIn("return;", pending)
        self.assertNotIn("continue;", pending)
        self.assertNotIn("as_landing_command", pending)
        self.assertNotIn("ClientThink(", pending)
        self.assertIn("!proved_control && !door_hold", marker)
        self.assertEqual(
            source.count("SG_HumanSpeedLandingPrepare("),
            1,
            "only the central Pmove boundary may prepare the timer",
        )
        self.assertEqual(
            source.count("SG_HumanSpeedLandingObserve("),
            1,
            "only the central Pmove boundary may observe the timer",
        )

    def test_skipped_owned_command_breaks_cadence(self) -> None:
        source = (ROOT / "slipgate" / "sg_move.c").read_text(encoding="utf-8")
        begin = source.index("void SG_HumanSpeedClientThinkBegin")
        end = source.index("void SG_HumanSpeedPmoveBegin", begin)
        body = source[begin:end]
        self.assertIn("skipped_owned_command = bot->as_landing_pending;", body)
        self.assertIn("owned && !skipped_owned_command", body)
        self.assertLess(
            body.index("SG_HumanSpeedCommandBoundary"),
            body.index("bot->as_landing_pending = owned;"),
        )

    def test_production_builds_own_the_adapter(self) -> None:
        for name in ("GNUmakefile", "Makefile"):
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertIn("slipgate/sg_human_speed.o", source, name)
            self.assertIn("human-speed-test", source, name)
        project = (ROOT / "gravity.vcxproj").read_text(encoding="utf-8")
        self.assertIn(r'slipgate\sg_human_speed.c', project)
        self.assertIn(r'slipgate\sg_human_speed.h', project)


if __name__ == "__main__":
    unittest.main()
