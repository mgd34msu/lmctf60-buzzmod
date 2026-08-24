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

    def test_match_end_closes_once_at_the_intermission_boundary(self) -> None:
        hud = (ROOT / "p_hud.c").read_text(encoding="utf-8")
        entry = hud.index("void BeginIntermission (edict_t *targ)")
        body = hud[entry:hud.index("void MoveClientToIntermission", entry)
                   if "void MoveClientToIntermission" in hud[entry + 1:]
                   else len(hud)]
        guard = body.index("if (level.intermissiontime)")
        close = body.index("SG_HumanTraceMatchEnd();")
        victory = body.index("Victory();")
        self.assertLess(guard, close)
        self.assertLess(close, victory)

        header = (ROOT / "slipgate" / "sg_human_trace.h").read_text(
            encoding="utf-8")
        source = (ROOT / "slipgate" / "sg_human_trace.c").read_text(
            encoding="utf-8")
        self.assertIn("void SG_HumanTraceMatchEnd(void);", header)
        self.assertIn("sg_human_trace_match_ended", source)
        self.assertIn(
            "int flush_failed = fflush(sg_human_trace_file) != 0;",
            source)
        self.assertIn(
            "int close_failed = fclose(sg_human_trace_file) != 0;",
            source)
        self.assertIn('\\"kind\\":\\"rune-bind\\"', source)

    def test_hook_observers_do_not_control_legacy_hook_flow(self) -> None:
        weapon = (ROOT / "p_weapon.c").read_text(encoding="utf-8")
        commands = (ROOT / "g_cmds.c").read_text(encoding="utf-8")
        header = (ROOT / "slipgate" / "sg_human_trace.h").read_text(
            encoding="utf-8")

        human_touch = weapon[
            weapon.index("void hook_touch"):
            weapon.index("void Grapple_Bolt_Think")]
        attach = human_touch.index("SG_HumanTraceHookAttach(")
        link = human_touch.rindex("gi.linkentity(self);", 0, attach)
        self.assertLess(link, attach)

        human_fire = weapon[
            weapon.index("static edict_t *LMCTF_FireHumanHook"):
            weapon.index("edict_t *fire_hook")]
        observe_fire = human_fire.index("SG_HumanTraceHookFire(")
        link_fire = human_fire.index("gi.linkentity(bolt);")
        trace_fire = human_fire.index("tr = gi.trace")
        self.assertLess(link_fire, observe_fire)
        self.assertLess(observe_fire, trace_fire)

        selected = weapon[
            weapon.index("void Weapon_Hook (edict_t *ent)"):
            weapon.index("// END CTF CODE")]
        observe_release = selected.index("SG_HumanTraceHookRelease(ent);")
        abort = selected.index("ctf_hook_abort(ent);")
        self.assertLess(observe_release, abort)

        unhook = commands[
            commands.index("void Cmd_Unhook_f"):
            commands.index("void Cmd_Ctfmenu_f")]
        self.assertEqual(unhook.count("SG_HumanTraceHookRelease(ent);"), 2)
        self.assertNotIn("if (SG_HumanTrace", weapon + commands)
        self.assertNotIn("SG_HumanTraceHook", weapon[
            weapon.index("static void SG_BotHookTouch"):
            weapon.index("void hook_touch")])
        self.assertIn("void SG_HumanTraceHookFire", header)
        self.assertIn("void SG_HumanTraceHookAttach", header)
        self.assertIn("void SG_HumanTraceHookRelease", header)

    def test_hook_observation_binds_without_a_bot_owned_rune(self) -> None:
        source = (ROOT / "slipgate" / "sg_human_trace.c").read_text(
            encoding="utf-8")
        bind = source[
            source.index("static qboolean HumanTraceBindRune"):
            source.index("void SG_HumanTraceNewLevel")]
        ready = source[
            source.index("static qboolean HumanTraceHookReady"):
            source.index("static void HumanTraceHookCommit")]

        self.assertIn("Rune_Load(level.mapname)", bind)
        self.assertIn("Rune_Free(transient_rune);", bind)
        self.assertIn("HumanTraceBindRune(true)", ready)
        self.assertNotIn("SG_LevelSetup()", bind + ready)


if __name__ == "__main__":
    unittest.main()
