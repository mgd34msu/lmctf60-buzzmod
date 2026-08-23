#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def ordered(text: str, *needles: str) -> None:
    haystack = " ".join(text.split())
    cursor = 0
    for needle in needles:
        normalized = " ".join(needle.split())
        cursor = haystack.index(normalized, cursor) + len(normalized)


def main() -> None:
    game = source("slipgate/sg_shoot_door_game.c")
    binding = between(game, "static int ShootDoorBinding", "static int ShootDoorKeys")
    for required in (
        "SG_MECHANISM_CONTROLLER_TRAIN_SHOOT",
        "RLCM_PREOPEN",
        "!binding->destination_node",
        "binding->entry_node == binding->mover_node",
        "SG_MECH_NODE_DOOR_MASTER",
    ):
        assert required in binding

    begin = between(game, "static int ShootDoorBegin", "int SG_ShootDoorGameOwns")
    ordered(
        begin,
        "ShootDoorWitness",
        "observation.team_closed != 1U",
        "SG_CompoundGuardAcquireTrainGate",
        "bot->shoot_door.guard_owned = 1U",
        "SG_ShootDoorLiveBegin",
    )
    assert "bot->shoot_door.mover_keys" in begin
    assert "bot->shoot_door.mover_count" in begin

    authorize = between(
        game,
        "int SG_ShootDoorGameAuthorizeActivation",
        "\n}",
    )
    ordered(
        authorize,
        "ShootDoorBinding",
        "ShootDoorMembers",
        "ShootDoorGuardAuthorize",
        "shot_requested != 1U",
        "damaged == 1",
        "shot_count = 1U",
        "damaged == 0 && reset",
    )
    assert "door_killed" in authorize

    emit = between(game, "int SG_ShootDoorGameEmit", "int SG_ShootDoorGameAuthorizeActivation")
    for required in (
        "SG_SHOOT_DOOR_COMMAND_EQUIP",
        "SG_SHOOT_DOOR_COMMAND_AIM",
        "SG_SHOOT_DOOR_COMMAND_SHOOT",
        "SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP",
        "SG_BlasterAimAngles",
        "SG_DeclaredCommand",
        "BUTTON_ATTACK",
        "ClientThink(entity, &command)",
    ):
        assert required in emit or required in game

    move = source("slipgate/sg_move.c")
    ordered(
        between(move, "qboolean SG_AuthorizeDoorActivation", "static sg_bot_t *TraversalLiveEventOwner"),
        "SG_ShootDoorGameAuthorizeActivation",
        "SG_CompoundDropGameAuthorizeActivation",
    )
    think = move[move.index("void Think_Emit"):]
    ordered(think, "SG_ShootDoorGameEmit(bot, bestlink)", "SG_TrainGateGameEmit(bot, bestlink)")

    transition = source("slipgate/sg_traversal_transition.c")
    assert "SG_ShootDoorGameOwns(bot)" in transition
    assert "SG_ShootDoorGameReset(bot)" in transition

    guard = source("slipgate/sg_compound_guard_game.c")
    for required in (
        "GameShootDoorTeam",
        "SG_MechCatalogEntityExecutionMatches",
        "SG_DeclaredDoorHoldMembers",
        "SG_DeclaredDoorMembersTerminal",
    ):
        assert required in guard

    forbidden = ("tw2ctf4", "214", "215", "244", "245")
    for value in forbidden:
        assert value not in game


if __name__ == "__main__":
    main()
