#!/usr/bin/env python3

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SpectatorLimitWiringTest(unittest.TestCase):
    def test_production_policy_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sg-spectator-limit-") as tmp:
            binary = Path(tmp) / "sg_spectator_limit_test"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wpedantic", "-ffunction-sections",
                "-fdata-sections", "-I.",
                "tests/sg_spectator_limit_test.c", "p_observer.c",
                "-Wl,--gc-sections", "-o", str(binary),
            ], cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                    capture_output=True, text=True)
            self.assertIn("sg_spectator_limit_test: ok", result.stdout)

    def test_all_admission_paths_share_the_observer_limit(self) -> None:
        commands = (ROOT / "g_cmds.c").read_text()
        client = (ROOT / "p_client.c").read_text()
        observe = commands[commands.index("void Cmd_Observe_f"):
                           commands.index("void fire_rune")]
        respawn = client[client.index("void spectator_respawn"):
                         client.index("void PutClientInServer")]
        connect = client[client.index("qboolean ClientConnect"):
                         client.index("void ClientBeginServerFrame")]

        self.assertRegex(observe,
                         r"G_SpectatorLimitBlocksAdmission\(\s*ent,\s*"
                         r"ent->client->resp\.spectator\)")
        self.assertIn("G_SpectatorLimitBlocksAdmission(ent, false)", respawn)
        self.assertIn("G_SpectatorLimitBlocksAdmission(ent, false)", connect)
        for section in (observe, respawn, connect):
            self.assertNotIn("numspec", section)

    def test_teamless_observer_join_does_not_use_suicide_switch(self) -> None:
        commands = (ROOT / "g_cmds.c").read_text()
        team = commands[commands.index("void Cmd_Team_f"):
                        commands.index("void Cmd_FlagStatus_f")]
        observer = team[team.index(
            "ent->client->ctf.teamnum <= CTF_TEAM_UNDEFINED"):
            team.index("if(ent->client->resp.spectator)")]

        self.assertIn("!ent->client->resp.spectator", observer)
        self.assertIn("ctf_SetEntTeam(ent, newnum);", observer)
        self.assertIn("respawn(ent);", observer)
        self.assertNotIn("Team_Change", observer)


if __name__ == "__main__":
    unittest.main()
