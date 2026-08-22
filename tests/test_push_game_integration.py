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
    observation = between(
        game,
        "static qboolean PushObservation",
        "static qboolean PushWitness",
    )
    for required in (
        "entity->s.origin[axis] * 8.0f",
        "isfinite(fixed)",
        "fixed < SHRT_MIN",
        "fixed > SHRT_MAX",
        "fixed != (float)(short)fixed",
        "entity->groundentity == g_edicts",
        "SG_ImmutableSupport(entity->groundentity)",
        "entity->velocity[axis] != 0.0f",
        "observation->immutable_support",
        "observation->at_rest",
        "observation->ordinary_control",
        "entity->client->ps.pmove.pm_type == PM_NORMAL",
        "entity->client->ps.pmove.pm_time == 0",
    ):
        assert required in observation
    assert "ps.pmove.origin" not in observation
    assert "ps.pmove.velocity" not in observation
    arrived = between(game, "static qboolean PushArrived", "static edict_t *PushEntry")
    assert "observation->alive" in arrived
    report = between(game, "static void PushReport", "int SG_PushGameOwns")
    assert "health=%d alive=%d" in report
    emit = between(game, "int SG_PushGameEmit", "int SG_PushGameStageAuthenticatedProbe")
    ordered(
        emit,
        "PushObservation(entity, &observation)",
        "SG_PushLiveCommand(&bot->push, &observation)",
        "SG_PushLiveBoundary(&bot->push",
    )
    ordered(
        emit,
        "SG_PushLiveCommand(&bot->push, &observation)",
        "memset(&command, 0, sizeof(command))",
        "bot->push.phase == SG_PUSH_APPROACH",
        "ClientThink(entity, &command)",
        "SG_PushLiveStep(&bot->push, SG_PUSH_STEP_MS)",
    )
    staging = between(
        game,
        "int SG_PushGameStageAuthenticatedProbe",
        "void SG_PushGameTouched",
    )
    ordered(
        staging,
        "PushWitness(link_index, &witness)",
        "SG_CompoundDropGameIdleAdmission(&sg_bots[slot])",
        "bot_before = *bot",
        "entity_before = *entity",
        "client_before = *entity->client",
        "gi.unlinkentity(entity)",
        "witness.source_q8[axis]",
        "SG_PushLiveReset(&bot->push)",
        "gi.linkentity(entity)",
        "SG_PushGameEmit(bot, link_index)",
        "!SG_PushGameOwns(bot)",
        "*entity->client = client_before",
        "*entity = entity_before",
        "*bot = bot_before",
        "gi.linkentity(entity)",
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
    touched = game[game.index("void SG_PushGameTouched"):]
    ordered(
        touched,
        "binding.entry_entity != trigger",
        "memcmp(entity->velocity, entity->client->oldvelocity",
        "SG_PushLiveTouched(&bot->push, key, entity->velocity)",
        'PushReport(bot, "touch")',
    )


if __name__ == "__main__":
    main()
