import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AirHookRuntimeIntegrationTest(unittest.TestCase):
    def test_wire_contract_authenticates_air_hook_marker(self):
        contract = json.loads((ROOT / "slipgate/rune_actions.json").read_text())
        law = contract["contract"]["proof_law"]
        self.assertEqual(law["air_hook_control_marker"], 252)
        self.assertEqual(law["air_hook_runup_frames"], 8)

        generated = (ROOT / "slipgate/sg_action_contract.generated.h").read_text()
        self.assertIn("SG_RUNE_PROOF_AIR_HOOK_CONTROL_MARKER 252", generated)
        self.assertIn("SG_RUNE_PROOF_AIR_HOOK_RUNUP_FRAMES 8", generated)

    def test_generator_and_runtime_share_launch_replay(self):
        frontier = (ROOT / "slipgate/sg_rune_hook_frontier.c").read_text()
        game = (ROOT / "slipgate/sg_hook_game.c").read_text()
        move = (ROOT / "slipgate/sg_move.c").read_text()

        self.assertIn("SG_OracleAirHookLaunchFrame", frontier)
        self.assertIn("SG_OracleAirHookCoastFrame", frontier)
        self.assertIn("SG_OracleAirHookLaunchFrame", game)
        self.assertIn("SG_OracleAirHookCoastFrame", game)
        self.assertIn("SG_AirHookLaunchCommand", game)
        self.assertIn("SG_AirHookGameStage(bot, bestlink)", move)
        self.assertIn("SG_AirHookGameEmit(bot, bestlink)", move)

    def test_runtime_reproves_exact_airborne_fire_state(self):
        game = (ROOT / "slipgate/sg_hook_game.c").read_text()
        codec = (ROOT / "slipgate/sg_rune_codec.c").read_text()

        self.assertIn("!AirHook_LiveMatches(e, &expected_air)", game)
        self.assertIn("source_air = link->heading_slack ==", game)
        self.assertIn("SG_RUNE_PROOF_AIR_HOOK_CONTROL_MARKER", codec)
        self.assertIn("link->min_speed == 0U", codec)

    def test_player_hook_implementation_has_no_bot_air_hook_coupling(self):
        for relative in ("g_cmds.c", "g_ctffunc.c", "p_weapon.c", "p_client.c"):
            source = (ROOT / relative).read_text()
            self.assertNotIn("air_hook_launch", source, relative)
            self.assertNotIn("RUNE_AIR_HOOK_CONTROL_MARKER", source, relative)
            self.assertNotIn("SG_AirHookGame", source, relative)

        game = (ROOT / "slipgate/sg_hook_game.c").read_text()
        self.assertIn("entity = bot->ent", game)


if __name__ == "__main__":
    unittest.main()
