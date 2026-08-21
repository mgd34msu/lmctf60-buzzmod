#!/usr/bin/env python3
"""Pin the executable D_SWIM runtime slice and keep sibling actions disabled."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "slipgate" / "sg_compound.h").read_text(encoding="utf-8")
LIVE_HEADER = (
    ROOT / "slipgate" / "sg_compound_swim_live.h"
).read_text(encoding="utf-8")
LIVE_SOURCE = (
    ROOT / "slipgate" / "sg_compound_swim_live.c"
).read_text(encoding="utf-8")
LIVE_TEST = (
    ROOT / "tests" / "sg_compound_swim_live_test.c"
).read_text(encoding="utf-8")
CONTRACT = (
    ROOT / "slipgate" / "sg_action_contract.generated.h"
).read_text(encoding="utf-8")


def test_door_drop_and_swim_are_admitted() -> None:
    registry = json.loads(
        (ROOT / "slipgate" / "rune_actions.json").read_text(encoding="utf-8")
    )
    actions = {row["id"]: row for row in registry["contract"]["actions"]}
    assert set(actions) >= {9, 10, 11}
    assert actions[9]["runtime_supported"] == 1
    assert actions[10]["runtime_supported"] == 1
    assert actions[11]["runtime_supported"] == 0

    assert "X(RL_DOOR_DROP, 9, 1," in CONTRACT
    assert "X(RL_DOOR_SWIM, 10, 1," in CONTRACT
    assert "X(RL_DOOR_HOOK, 11, 0," in CONTRACT


def test_controller_owns_one_literal_action_without_handoff() -> None:
    joined = LIVE_HEADER + LIVE_SOURCE
    assert "RL_DOOR_SWIM" in LIVE_SOURCE
    assert "RL_CONTRACTED" in LIVE_SOURCE
    assert "RLCM_PREOPEN" in LIVE_SOURCE
    assert "SG_CompoundBegin" in LIVE_SOURCE
    assert "SG_CompoundAdvance" in LIVE_SOURCE
    assert "SG_CompoundDelegateSuffix" in LIVE_SOURCE
    assert "SG_SwimReplayBegin" in LIVE_SOURCE
    assert "SG_SwimReplayPreStep" in LIVE_SOURCE
    assert "SG_SwimReplayPostStep" in LIVE_SOURCE
    assert "SG_ActionEffectiveSuffix" not in joined
    assert "FALLBACK" not in joined.upper()
    assert "SG_COMPOUND_SWIM_LIVE_RECOVERING" in LIVE_HEADER
    assert "SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED" in LIVE_HEADER
    assert "sweep_segment_clear" in LIVE_HEADER


def test_state_has_no_unguarded_reset_api() -> None:
    joined = LIVE_HEADER + LIVE_SOURCE + LIVE_TEST
    assert "SG_CompoundSwimLiveReset" not in joined
    assert "#define SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER { 0 }" in LIVE_HEADER
    assert "SG_COMPOUND_SWIM_LIVE_STATE_INITIALIZER" in LIVE_TEST


def test_production_registration_and_callsite() -> None:
    for make_name in ("GNUmakefile", "Makefile"):
        make_text = (ROOT / make_name).read_text(encoding="utf-8")
        if make_name == "GNUmakefile":
            production_objects = make_text[
                make_text.index("C_OBJS =") : make_text.index("G_OBJS =")
            ]
        else:
            production_objects = make_text[
                make_text.index("OBJS :=") :
                make_text.index("ifdef CONFIG_VARIABLE_SERVER_FPS")
            ]
        assert "sg_compound_swim_live.o" in production_objects
        assert "sg_compound_swim_game.o" in production_objects
        assert "compound-swim-live-test" in make_text
    bot = (ROOT / "slipgate" / "sg_bot.h").read_text(encoding="utf-8")
    move = (ROOT / "slipgate" / "sg_move.c").read_text(encoding="utf-8")
    arach = (ROOT / "slipgate" / "sg_arach.c").read_text(encoding="utf-8")
    assert "sg_compound_swim_live_state_t compound_swim" in bot
    assert "SG_CompoundSwimGameEmit" in move
    assert "SG_CompoundSwimGameOwns" in arach


if __name__ == "__main__":
    test_door_drop_and_swim_are_admitted()
    test_controller_owns_one_literal_action_without_handoff()
    test_state_has_no_unguarded_reset_api()
    test_production_registration_and_callsite()
    print("test_compound_swim_live_integration: ok")
