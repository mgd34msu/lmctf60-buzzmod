#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def section(text: str, start: str, end: str) -> str:
    first = text.index(start)
    return text[first:text.index(end, first)]


def test_station_owns_exactly_four_substeps_before_generic_movement() -> None:
    move = (ROOT / "slipgate/sg_move.c").read_text(encoding="utf-8")
    owned = section(
        move,
        "if (SG_TrainStationGameEmit(bot, bestlink))",
        "if (SG_PushGameEmit(bot, bestlink))",
    )
    station = owned.index("if (SG_TrainStationGameEmit(bot, bestlink))")
    station_return = owned.index("return;", station)
    train_gate = owned.index("if (SG_TrainGateGameEmit(bot, bestlink))")
    assert station < station_return < train_gate

    game = (ROOT / "slipgate/sg_train_station_game.c").read_text(
        encoding="utf-8"
    )
    emit = section(game, "int SG_TrainStationGameEmit", "return 1;\n}")
    assert "#define SG_TRAIN_STATION_FRAME_STEPS 4" in game
    assert "step < SG_TRAIN_STATION_FRAME_STEPS" in emit
    assert emit.count("ClientThink(entity, &command);") == 1
    assert "if (!StationSelected(selected_link))\n\t\t\treturn 0;" in emit
    binding_failure = section(
        emit,
        "if (!StationBinding((uint32_t)selected_link, &binding))",
        "if (!StationBegin(bot, selected_link))",
    )
    assert "bot->commit_link = -1;" in binding_failure
    assert "return 1;" in binding_failure


def test_slot_reset_retires_station_state_before_storage_reuse() -> None:
    client = (ROOT / "slipgate/sg_client.c").read_text(encoding="utf-8")
    reset = section(client, "static void BotSlot_Reset", "memset(bot, 0")
    assert "SG_TrainStationGameReset(bot);" in reset


if __name__ == "__main__":
    test_station_owns_exactly_four_substeps_before_generic_movement()
    test_slot_reset_retires_station_state_before_storage_reuse()
    print("train station game integration tests passed")
