import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class RuneSourceAuthorityIntegrationTest(unittest.TestCase):
    def test_spawn_policy_order_is_the_capture_authority(self):
        spawn = (ROOT / "g_spawn.c").read_text()
        authority = (ROOT / "slipgate/sg_rune_source_authority.c").read_text()

        begin = spawn.index("void SpawnEntities")
        end = spawn.index("//===================================================================", begin)
        body = spawn[begin:end]

        reset = body.index("SG_RuneSourceAuthorityReset();")
        identity_reset = body.index("SG_LevelIdentityReset();")
        select = body.index("entities = ReadEntFile(mapname, entities);")
        identity_capture = body.index(
            "SG_LevelIdentityCaptureEntities(mapname, entities);")
        source_begin = body.index("SG_RuneSourceAuthorityBegin(mapname, entities);")
        command_mutation = body.index(
            "ent->spawnflags &= ~SPAWNFLAG_NOT_HARD;")
        deathmatch_inhibit = body.index(
            "ent->spawnflags & SPAWNFLAG_NOT_DEATHMATCH")
        deathmatch_continue = body.index("continue;", deathmatch_inhibit)
        skill_inhibit = body.index("ent->spawnflags & SPAWNFLAG_NOT_EASY")
        skill_continue = body.index("continue;", skill_inhibit)
        strip = body.index(
            "ent->spawnflags &= ~(SPAWNFLAG_NOT_EASY|SPAWNFLAG_NOT_MEDIUM|")
        record = body.index("SG_RuneSourceAuthorityRecord(")
        dispatch = body.index("ED_CallSpawn (ent);")
        host_publish = body.index("SG_HostLawProductionBeginLevel(mapname);")
        source_publish = body.index("SG_RuneSourceAuthorityPublish(mapname);")

        self.assertLess(reset, identity_reset)
        self.assertLess(select, identity_capture)
        self.assertLess(identity_capture, source_begin)
        self.assertLess(command_mutation, record)
        self.assertLess(deathmatch_continue, record)
        self.assertLess(skill_continue, record)
        self.assertLess(strip, record)
        self.assertLess(record, dispatch)
        self.assertLess(host_publish, source_publish)
        self.assertEqual(spawn.count('!Q_stricmp(level.mapname, "command")'), 1)
        self.assertNotIn('"command"', authority)

    def test_production_build_and_teardown_own_the_authority(self):
        makefile = (ROOT / "Makefile").read_text()
        gnumakefile = (ROOT / "GNUmakefile").read_text()
        main = (ROOT / "g_main.c").read_text()
        save = (ROOT / "g_save.c").read_text()
        arach = (ROOT / "slipgate/sg_arach.c").read_text()

        self.assertIn("slipgate/sg_rune_source_authority.o", makefile)
        self.assertIn("slipgate/sg_rune_source_authority.o", gnumakefile)

        shutdown = main[main.index("void ShutdownGame"):main.index(
            "GetGameAPI", main.index("void ShutdownGame"))]
        self.assertLess(
            shutdown.index("SG_RuneSourceAuthorityReset();"),
            shutdown.index("SG_HostLawProductionReset();"))

        init = save[save.index("void InitGame(void)"):]
        self.assertLess(
            init.index("SG_RuneSourceAuthorityReset();"),
            init.index("SG_LevelIdentityReset();"))

        level_change = arach[arach.index("void SG_LevelChange(void)"):]
        self.assertLess(
            level_change.index("SG_RuneSourceAuthorityReset();"),
            level_change.index("SG_HostLawProductionReset();"))


if __name__ == "__main__":
    unittest.main()
