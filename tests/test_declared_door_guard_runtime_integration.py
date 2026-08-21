#!/usr/bin/env python3
"""Pin ordinary RL_DOOR execution to the shared mover guard."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def test_old_late_owner_scan_is_gone() -> None:
    move = source("slipgate/sg_move.c")
    assert "DoorStep_OwnedByOther" not in move
    assert "SG_DeclaredDoorSameSet" not in move


def test_acquire_precedes_declared_start_and_first_command() -> None:
    move = source("slipgate/sg_move.c")
    control = between(
        move,
        "if (!water_tele && !bot->declared_started &&\n"
        "\t\t\t\t    source_exact && source_rest",
        "if (declared_door)\n\t\t\t\t{\n\t\t\t\t\tshort wait_fixed",
    )
    acquire = control.index("SG_DeclaredDoorGuardAcquire(bot, bestlink)")
    started = control.index("bot->declared_started = true;", acquire)
    failed = control.index("if (acquire_result == SG_COMPOUND_GUARD_NOT_CLEAR)", acquire)
    failed_return = control.index("return;", failed)
    assert acquire < started
    assert acquire < failed < failed_return
    assert "ClientThink(" not in control


def test_stale_frame_link_cannot_acquire_or_execute() -> None:
    move = source("slipgate/sg_move.c")
    stale = between(
        move,
        "/* Think_Move may retire a previous action",
        "/* The graph's nominal SWIM proves",
    )
    assert "declared_door && bot->commit_link != bestlink" in stale
    assert stale.index("DoorStep_AbortOrRetain(bot, bestlink)") < stale.rindex(
        "return;"
    )
    emit = move[move.index("void Think_Emit") :]
    mismatch = emit.index("declared_door && bot->commit_link != bestlink")
    acquire = emit.index("SG_DeclaredDoorGuardAcquire(bot, bestlink)")
    clientthink = emit.index("ClientThink(e, cmd);", acquire)
    assert mismatch < acquire < clientthink


def test_every_abort_is_release_gated() -> None:
    move = source("slipgate/sg_move.c")
    abort = between(move, "static qboolean DoorStep_AbortDeclared", "static void DoorStep_StopOutside")
    assert abort.index("SG_DeclaredDoorGuardReleaseProvedClear") < abort.index(
        "bot->commit_link = -1;"
    )
    assert "return false;" in abort
    assert "return true;" in abort
    # Raw release is private to the wrapper.  Every action-site retirement uses
    # AbortOrRetain so a foreign body or uncertain host observation also renews
    # the exact TOP set and enters the bounded recovery/death path.
    assert move.count("DoorStep_AbortDeclared(") == 2
    assert "static qboolean DoorStep_AbortOrRetain" in abort
    assert "DoorStep_RetainDeclared(bot);" in abort
    assert move.count("DoorStep_AbortOrRetain(bot, bestlink)") == 6
    # Successful release arms a physical retirement fence.  Call sites that
    # have no further cleanup return unconditionally instead of interpreting
    # success as permission to emit a zero or generic command.
    assert move.count("(void)DoorStep_AbortOrRetain(bot, bestlink);") == 2
    assert "(void)DoorStep_AbortOrRetain(bot, link_index);" in abort


def test_hold_and_clientthink_have_exact_authorization() -> None:
    move = source("slipgate/sg_move.c")
    hold = between(
        move,
        "static void DoorStep_RetainDeclared",
        "static qboolean DoorStep_AbortOrRetain",
    )
    assert "SG_DeclaredDoorGuardHoldOpen(bot, 500)" in hold
    assert "SG_DeclaredDoorHoldOpen" not in hold
    emit = between(
        move,
        "/* Physics preflight and mover authority are independent.",
        "if (proved_swim && bot->swim_replay_active",
    )
    assert emit.index("SG_DeclaredDoorGuardAuthorize") < emit.index(
        "ClientThink(e, cmd);"
    )
    assert "SG_DeclaredDoorHoldOpen(" not in move.replace(
        "SG_DeclaredDoorHoldOpen(trigger, lease_ms)", ""
    )


def test_started_frame_authorizes_before_body_canonicalization() -> None:
    move = source("slipgate/sg_move.c")
    started = between(
        move,
        "if (declared_door)\n\t\t\t\t{\n\t\t\t\t\tshort wait_fixed[3];",
        "/* Activation can become true in the unactivated branch above",
    )
    binding = started.index("DoorStep_DeclaredBindingForLink(bestlink,")
    authorize = started.index("SG_DeclaredDoorGuardAuthorize(bot, bestlink)")
    canonicalize = started.index("Ballistic_CanonicalizeSource")
    assert binding < authorize < canonicalize
    assert "SG_DeclaredDoorForLink" not in started


def test_commit_retirement_requires_positive_clear() -> None:
    descend = source("slipgate/sg_descend.c")
    terminal = between(
        descend,
        "/* A declared door commitment may retire only",
        "else\n\t\t\tbestlink = bot->commit_link;",
    )
    release = terminal.index("SG_DeclaredDoorGuardReleaseProvedClear")
    retire = terminal.index("if (drop_commit)")
    clear = terminal.index("bot->commit_link = -1;")
    assert release < retire < clear
    assert "drop_commit = false;" in terminal
    assert "bestlink = bot->commit_link;" in terminal


def test_door_swim_controller_and_contract_are_enabled_together() -> None:
    owned_runtime = "\n".join(
        source(path)
        for path in (
            "slipgate/sg_move.c",
            "slipgate/sg_descend.c",
            "slipgate/sg_arach.c",
            "slipgate/sg_declared_door_guard.c",
            "slipgate/sg_compound_swim_game.c",
        )
    )
    assert "SG_CompoundGuardAcquireCompoundPreopen" in owned_runtime
    registry = source("slipgate/rune_actions.json")
    for action in ("RL_DOOR_DROP", "RL_DOOR_HOOK"):
        row = registry[registry.index(f'"symbol": "{action}"') :]
        row = row[: row.index("}")]
        assert '"runtime_supported": 0' in row
    row = registry[registry.index('"symbol": "RL_DOOR_SWIM"') :]
    row = row[: row.index("}")]
    assert '"runtime_supported": 1' in row


if __name__ == "__main__":
    test_old_late_owner_scan_is_gone()
    test_acquire_precedes_declared_start_and_first_command()
    test_stale_frame_link_cannot_acquire_or_execute()
    test_every_abort_is_release_gated()
    test_hold_and_clientthink_have_exact_authorization()
    test_started_frame_authorizes_before_body_canonicalization()
    test_commit_retirement_requires_positive_clear()
    test_door_swim_controller_and_contract_are_enabled_together()
    print("declared_door_guard_runtime_integration: ok")
