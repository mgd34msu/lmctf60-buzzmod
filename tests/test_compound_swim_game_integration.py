from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


class CompoundSwimGameIntegrationTest(unittest.TestCase):
    def test_owned_transaction_precedes_planning(self) -> None:
        source = (ROOT / "slipgate" / "sg_arach.c").read_text()
        think = section(source, "void SG_BotThink(", "\n\n\nvoid SG_RunFrame(")
        owned = think.index("if (SG_CompoundSwimGameOwns(bot))")
        dispatch = think.index("SG_CompoundSwimGameEmit(bot,", owned)
        compatibility = think.index("SG_RunePhysicsCompatible", dispatch)
        move = think.index("Think_Move(bot, &tc);", compatibility)
        self.assertLess(owned, dispatch)
        self.assertLess(dispatch, compatibility)
        self.assertLess(compatibility, move)

    def test_fresh_dispatch_precedes_generic_command_writers(self) -> None:
        source = (ROOT / "slipgate" / "sg_move.c").read_text()
        emit = source[source.index("void Think_Emit(") :]
        dispatch = emit.index("SG_CompoundSwimGameEmit(bot, bestlink)")
        guard = emit.index("SG_CompoundGuardValidate", dispatch)
        self.assertLess(dispatch, guard)

    def test_compound_callbacks_precede_declared_door_callbacks(self) -> None:
        source = (ROOT / "slipgate" / "sg_move.c").read_text()
        touch = section(
            source,
            "qboolean SG_AuthorizeDoorTriggerTouch(",
            "qboolean SG_AuthorizeDoorActivation(",
        )
        activation = section(
            source,
            "qboolean SG_AuthorizeDoorActivation(",
            "void SG_NoteDropTriggerContact(",
        )
        self.assertLess(
            touch.index("SG_CompoundSwimGameAuthorizeTouch"),
            touch.index("DoorStep_DeclaredBinding"),
        )
        self.assertLess(
            activation.index("SG_CompoundSwimGameAuthorizeActivation"),
            activation.index("DoorStep_DeclaredBinding"),
        )

    def test_generator_reuses_the_predeclaration_topology(self) -> None:
        source = (ROOT / "slipgate" / "sg_rune.c").read_text()
        base = section(
            source, "static qboolean Prove_BaseLinks(", "/* A field is useful"
        )
        self.assertIn("Link_Doors(topology)", base)
        generate = section(
            source, "static qboolean Rune_GenerateMode(", "\ncleanup:"
        )
        snapshot = generate.index("Prove_BaseLinks(&compound_topology)")
        restore = generate.index("Doors_Restore(&doors)", snapshot)
        compound = generate.index(
            "SG_CompoundGenGameGenerate(gen_seeds", restore
        )
        self.assertLess(snapshot, restore)
        self.assertLess(restore, compound)

    def test_admin_probe_parses_one_bounded_link_index(self) -> None:
        source = (ROOT / "g_svcmds.c").read_text()
        command = section(
            source,
            'else if (Q_stricmp(sub, "compoundswim") == 0)',
            "\n\telse\n",
        )
        self.assertIn("long link = strtol(arg, &end, 10);", command)
        self.assertIn("!*arg || !end || *end || link < 0 || link > INT_MAX", command)
        self.assertIn(
            "SG_CompoundSwimGameStageAuthenticatedProbe((int)link)", command
        )
        self.assertIn("authenticated compound swim probe refused", command)


if __name__ == "__main__":
    unittest.main()
