#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def ordered(text: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        cursor = text.index(needle, cursor) + len(needle)


def main() -> None:
    rune = source("slipgate/sg_rune.c")
    shoot = between(
        rune,
        "static qboolean Train_ShootButtonShape",
        "static uint32_t Train_OpeningBound",
    )
    for required in (
        "SG_MECH_NODEF_SHOOTABLE",
        "SG_MECH_CALLBACK_BUTTON_USE",
    ):
        assert required in shoot
    assert "SG_MECHANISM_CONTROLLER_TRAIN_SHOOT" in rune
    assert "SG_OracleTrainGateShot" in rune
    oracle = source("slipgate/sg_oracle.c")
    assert "SG_OracleTrainGateShot" in oracle
    assert "passage_axis > 3U" in oracle
    shoot_train = between(
        rune,
        "static void Link_TrainShootButtons",
        "/* Link one canonical door team",
    )
    ordered(
        shoot_train,
        "topology->component[source] !=",
        "topology->component[reverse_source]",
        "SG_OracleTrainGateShot",
        "Train_PoseOpen",
        "SG_OracleTrainGateEntry",
        "SG_OracleTrainGateCross",
        "source_by_axis_side[axis][source_side] = source",
    )
    ordered(
        shoot_train,
        "for (destination = 0; destination < gen_num_seeds; destination++)",
        "new_bits = topology->objective_mask[destination] & missing",
        "SG_OracleTrainGateCross(best_contact",
        "selected_destination[slot] = destination",
    )
    assert shoot_train.count("SG_OracleTrainGateShot") == 1
    assert "TRAIN_SHOOT_DEST_FAN" not in shoot_train

    game = source("slipgate/sg_train_gate_game.c")
    witness = between(game, "static int TrainWitness", "static sg_train_gate_pose_t")
    for required in (
        "SG_RuneMechanismBindingMoverKeys",
        "mover_count != 1U",
        "witness->entry_q8",
        "binding.entry_entity->absmin",
        "witness->closed_corner_key",
        "witness->open_corner_key",
    ):
        assert required in witness

    begin = between(game, "static int TrainBegin", "int SG_TrainGateGameOwns")
    ordered(
        begin,
        "TrainWitness",
        "observation.pose != SG_TRAIN_GATE_POSE_CLOSED",
        "SG_CompoundGuardAcquireTrainGate",
        "bot->train_gate.guard_owned = 1U",
        "SG_TrainGateLiveBegin",
    )
    reset = between(game, "void SG_TrainGateGameReset", "static int TrainFinish")
    ordered(
        reset,
        "SG_CompoundGuardValidate",
        "result != SG_COMPOUND_GUARD_NO_LEASE",
        "return;",
        "memset(&bot->train_gate",
    )
    emit = between(game, "int SG_TrainGateGameEmit", "static int TrainActiveEvent")
    ordered(
        emit,
        "TrainObservation(bot, &observation)",
        "SG_TrainGateLiveStep",
        "SG_TRAIN_GATE_COMMAND_TO_ENTRY",
        "SG_DeclaredCommand",
        "ClientThink(entity, &command)",
    )
    dispatch = between(
        game,
        "int SG_TrainGateGameHandleTargets",
        "int SG_TrainGateGameAuthorizeTrainUse",
    )
    ordered(
        dispatch,
        "target_dispatch_count = 1U",
        "SG_RuneMechanismBindingDispatchTargets",
        "train_use_count != 1U",
    )
    assert "G_UseTargets" not in dispatch

    train_use = between(
        source("g_func.c"), "void train_use", "void SP_func_train"
    )
    ordered(train_use, "SG_AuthorizeTrainUse", "self->activator = activator")
    button_killed = between(
        source("g_func.c"), "void button_killed", "void SP_func_button"
    )
    ordered(button_killed, "SG_AuthorizeButtonShot", "self->activator = attacker")

    for required in (
        "SG_TRAIN_GATE_COMMAND_EQUIP",
        "SG_TRAIN_GATE_COMMAND_AIM_BUTTON",
        "SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON",
        "SG_BlasterAimAngles",
        "BUTTON_ATTACK",
        "SG_TrainGateGameAuthorizeButtonShot",
    ):
        assert required in game

    move = source("slipgate/sg_move.c")
    for seam in (
        "SG_TrainGateGameAuthorizeButtonTouch",
        "SG_TrainGateGameAuthorizeButtonUse",
        "SG_TrainGateGameAuthorizeButtonTargets",
        "SG_TrainGateGameHandleTargets",
    ):
        assert seam in move
    think = move[move.index("void Think_Emit"):]
    ordered(think, "SG_TrainGateGameEmit(bot, bestlink)", "SG_PushGameEmit(bot, bestlink)")

    guard = source("slipgate/sg_compound_guard_game.c")
    pusher = between(
        guard,
        "int SG_CompoundGuardGameEntityMayDispatch",
        "void SG_CompoundGuardGameEntityDeferred",
    )
    ordered(
        pusher,
        "SG_CompoundGuardTrainGatePusherFence",
        "SG_MoverProspectivePusherValid",
        "GameAllSubjectsOutsideMode",
    )


if __name__ == "__main__":
    main()
