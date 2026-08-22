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
    service = source("g_svcmds.c")
    dispatch = between(service, "static void SVCmd_SG_f", "static void SVCmd_POVRecord_f")
    push = between(
        dispatch,
        'else if (Q_stricmp(sub, "push") == 0)',
        'else if (Q_stricmp(sub, "compounddrop") == 0)',
    )
    for required in (
        "strtol(arg, &end, 10)",
        "!*arg",
        "*end",
        "link < 0",
        "link > INT_MAX",
        "SG_PushGameStageAuthenticatedProbe((int)link)",
        "authenticated push probe refused",
    ):
        assert required in push
    assert "push <link>" in dispatch

    game = source("slipgate/sg_push_game.c")
    staging = between(
        game,
        "int SG_PushGameStageAuthenticatedProbe",
        "void SG_PushGameTouched",
    )
    ordered(
        staging,
        "PushWitness(link_index, &witness)",
        "SG_CompoundDropGameIdleAdmission(&sg_bots[slot])",
        "gi.unlinkentity(entity)",
        "witness.source_q8[axis]",
        "SG_PushLiveReset(&bot->push)",
        "gi.linkentity(entity)",
        "SG_PushGameEmit(bot, link_index)",
        "bot->push.phase != SG_PUSH_FLIGHT",
        'PushReport(bot, "probe-staged")',
    )
    begin = between(game, "static qboolean PushBegin", "static qboolean PushConsumeTerminal")
    ordered(begin, "PushWitness", "SG_BallisticSurvivable", "PushObservation")

    trigger = between(source("g_trigger.c"), "void trigger_push_touch", "void SP_trigger_push")
    ordered(
        trigger,
        "VectorCopy (other->velocity, other->client->oldvelocity)",
        "SG_PushGameTouched(self, other)",
    )


if __name__ == "__main__":
    main()
