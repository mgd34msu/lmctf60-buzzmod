from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[start:offset + 1]
    raise AssertionError(f"unterminated function: {signature}")


class HumanHookOwnershipIntegrationTest(unittest.TestCase):
    def test_bot_ownership_requires_engine_bot_identity(self) -> None:
        source = (ROOT / "slipgate" / "sg_client_ownership.c").read_text(
            encoding="utf-8")
        self.assertIn("!(ent->flags & FL_BOT)", source)
        self.assertIn("sg_bots[i].active && sg_bots[i].ent == ent", source)

    def test_human_connection_retires_old_owner_before_reuse(self) -> None:
        source = (ROOT / "p_client.c").read_text(encoding="utf-8")
        start = source.index("qboolean ClientConnect (")
        connect = source[start:source.index("void ClientDisconnect (", start)]
        self.assertLess(connect.index("SG_RetireBotForClient(ent)"),
                        connect.index("ent->client = game.clients"))

    def test_end_frame_observer_is_bot_gated(self) -> None:
        source = (ROOT / "p_view.c").read_text(encoding="utf-8")
        start = source.index("if (ent->client->hookstate)")
        hook = source[start:source.index("If the origin or velocity", start)]
        self.assertLess(hook.index("Weapon_Hook_Fire(ent)"),
                        hook.index("if (SG_OwnsBot(ent))"))
        self.assertLess(hook.index("if (SG_OwnsBot(ent))"),
                        hook.index("SG_HookLiveEndFrame(ent)"))

    def test_human_fire_path_is_stock_and_bot_private_code_is_segregated(self) -> None:
        source = (ROOT / "p_weapon.c").read_text(encoding="utf-8")
        dispatch = function_body(source, "void Weapon_Hook_Fire (")
        human_fire = function_body(source, "static void LMCTF_HumanHookFire(")
        human_bolt = function_body(source, "static edict_t *LMCTF_FireHumanHook(")
        human_touch = function_body(source, "void hook_touch (")

        human_branch = dispatch[:dispatch.index("v = tv(")]
        self.assertIn("if (!SG_OwnsBot(ent))", human_branch)
        self.assertIn("LMCTF_HumanHookFire(ent);", human_branch)
        self.assertIn("return;", human_branch)

        start_case = human_fire[human_fire.index("case 0:"):
                                human_fire.index("case 1:")]
        sustain_case = human_fire[human_fire.index("case 1:"):
                                  human_fire.index("case 2:")]
        pull_case = human_fire[human_fire.index("case 2:"):
                               human_fire.index("default:")]
        self.assertEqual(start_case.count("LMCTF_FireHumanHook("), 1)
        self.assertNotIn("LMCTF_FireHumanHook(", sustain_case)
        self.assertNotIn("LMCTF_FireHumanHook(", pull_case)
        self.assertIn("Draw_Hook", sustain_case)
        self.assertIn("VectorCopy(dir, ent->velocity)", pull_case)

        self.assertIn("G_ProjectileOwnerSet(bolt, self);", human_bolt)
        self.assertNotIn("bolt->owner = self;", human_bolt)
        self.assertIn("bolt->touch = hook_touch;", human_bolt)
        self.assertIn("bolt->clipmask = MASK_SHOT;", human_bolt)
        self.assertIn("bolt->touch(bolt, tr.ent, &tr.plane, NULL);",
                      human_bolt)
        self.assertNotIn("bolt->touch(bolt, tr.ent, NULL, NULL);", human_bolt)
        self.assertNotIn("SG_BotHookTouch", human_bolt)
        self.assertNotIn("SG_Compound", human_bolt)

        self.assertIn("if (!other)", human_touch)
        self.assertNotIn("SG_BotHookTouch", human_touch)
        self.assertNotIn("SG_Compound", human_touch)

    def test_human_release_aborts_immediately_and_can_refire(self) -> None:
        weapon = (ROOT / "p_weapon.c").read_text(encoding="utf-8")
        commands = (ROOT / "g_cmds.c").read_text(encoding="utf-8")
        abort_source = (ROOT / "g_ctffunc.c").read_text(encoding="utf-8")

        held_weapon = function_body(weapon, "void Weapon_Hook (")
        release = held_weapon[held_weapon.index("BUTTON_ATTACK"):
                              held_weapon.index("Weapon_Generic")]
        self.assertIn("ctf_hook_abort(ent);", release)

        unhook = function_body(commands, "void Cmd_Unhook_f (")
        selected = unhook[unhook.index("if (ent->client->pers.weapon == it)"):
                          unhook.index("else")]
        self.assertLess(selected.index("ctf_hook_abort(ent);"),
                        selected.index('ForceCommand(ent, "-attack\\n")'))

        abort = function_body(abort_source, "void ctf_hook_abort(")
        self.assertIn("ent->client->hookstate = 0;", abort)
        self.assertIn("ent->client->hook = NULL;", abort)
        human_prefix = abort[:abort.index("//\t\tent->client->fall_time")]
        self.assertIn("if (SG_OwnsBot(ent))", human_prefix)
        self.assertIn("SG_CompoundHookGameAbortBegin", human_prefix)

    def test_both_build_dialects_run_the_ownership_test(self) -> None:
        for name in ("GNUmakefile", "Makefile"):
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertIn("slipgate/sg_client_ownership.o", source, name)
            self.assertIn("human-hook-ownership-test", source, name)


if __name__ == "__main__":
    unittest.main()
