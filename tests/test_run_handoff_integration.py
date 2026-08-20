#!/usr/bin/env python3
"""Static call-order pins for the production RUN handoff transaction."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def main() -> None:
    descend = (ROOT / "slipgate/sg_descend.c").read_text(encoding="utf-8")
    commit = descend[descend.index("int Think_CommitLink(") :]
    handoff = commit.index("SG_RunCompletionHandoff(")
    invalidate = commit.index("SG_RunInvalidateCompletedCandidate(")
    latch = commit.index(
        "if (!defense_shift_selected && !defense_patrol_selected &&"
    )
    assert invalidate < handoff < latch
    assert commit.count("SG_RunCompletionHandoff(") == 1
    early = commit[invalidate:latch]
    assert "completion == SG_RUN_ARRIVED" in early
    assert "bot->seed != incoming->to" in early
    assert "return -1;" in early

    transaction = section(
        descend,
        "qboolean SG_RunCompletionHandoff(",
        "\nvoid SG_RunInvalidateCompletedCandidate(",
    )
    retire = transaction.index("bot->commit_link = -1;")
    pmove = transaction.index("ClientThink(")
    validate = transaction.index("Run_HandoffBodyValid(")
    publish = transaction.index("bot->seed = completed->to;")
    assert retire < pmove < validate < publish
    assert transaction.count("ClientThink(") == 1
    assert "for (step = 0; step < 4; step++)" in transaction
    assert "coast.msec = 25;" in transaction
    assert "bot->seed = -1;" in transaction

    # The handoff ends the current think. The next frame still traverses the
    # ordinary TrackSeed -> PickLink -> CommitLink pipeline; no mechanism is
    # directly armed by the transaction itself.
    arach = (ROOT / "slipgate/sg_arach.c").read_text(encoding="utf-8")
    bot_think = section(arach, "void SG_BotThink(", "\n\n\nvoid SG_RunFrame(")
    track = bot_think.index("Think_TrackSeed(")
    pick = bot_think.index("Think_PickLink(")
    commit_call = bot_think.index("Think_CommitLink(")
    assert track < pick < commit_call
    assert "if (think_over)\n\t\treturn;" in bot_think[commit_call:]

    print("test_run_handoff_integration: ok")


if __name__ == "__main__":
    main()
