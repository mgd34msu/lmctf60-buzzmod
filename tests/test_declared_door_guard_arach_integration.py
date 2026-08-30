#!/usr/bin/env python3
"""Pin RL_DOOR authority holds around the SG bot think pipeline."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def test_header_and_life_reset() -> None:
    arach = source("slipgate/sg_arach.c")
    assert '#include "slipgate/sg_declared_door_guard.h"' in arach
    reset = between(arach, "static void Bot_ResetLifeActions", "static qboolean Think_Dead")
    orphan_comment = source("p_client.c")
    assert reset.index("bot->declared_guard_paused = false;") < reset.index(
        "bot->commit_link = -1;"
    )
    assert "bot->declared_guard_pause_started = 0.0f;" in reset
    assert orphan_comment.index("SG_CompoundGuardGamePlayerDie(self)") < orphan_comment.index(
        "SG_NoteDeath(self)"
    )


def test_release_pause_and_restore_law() -> None:
    arach = source("slipgate/sg_arach.c")
    identify = between(
        arach,
        "static qboolean Bot_DeclaredDoorGuardAction",
        "static void Bot_DeclaredDoorGuardClearAction",
    )
    validate = identify.index("SG_CompoundGuardValidate")
    durable_law = identify.index("record.law == SG_MOVER_LAW_DECLARED_DOOR", validate)
    durable_state = identify.index("record.state == SG_MOVER_LEASE_ACTIVE", durable_law)
    paused_state = identify.index("record.state == SG_MOVER_LEASE_PAUSED", durable_state)
    orphan_exclusion = identify.index("ORPHAN belongs solely", paused_state)
    local_fallback = identify.rindex("return local_door;")
    assert validate < durable_law < durable_state < paused_state < orphan_exclusion < local_fallback

    retain = between(
        arach,
        "static qboolean Bot_DeclaredDoorGuardRetainOrRelease",
        "/* Restore only an exact PAUSED claim.",
    )
    release = retain.index("SG_DeclaredDoorGuardReleaseProvedClear")
    positive = retain.index("result == SG_COMPOUND_GUARD_OK", release)
    pause_latch = retain.index("bot->declared_guard_paused = true;", positive)
    hold = retain.index("SG_DeclaredDoorGuardHoldOpen(bot, 500)", pause_latch)
    terminal = retain.index("SG_DeclaredDoorTerminalDeath(bot)", hold)
    not_clear = retain.index("result == SG_COMPOUND_GUARD_NOT_CLEAR", terminal)
    pause = retain.index("SG_DeclaredDoorGuardPause", hold)
    assert (
        release < positive < pause_latch < hold < terminal < not_clear < pause
        < retain.rindex("return true;")
    )

    restore = between(arach, "static qboolean Bot_DeclaredDoorGuardRestore", "void SG_BotThink")
    release = restore.index("SG_DeclaredDoorGuardReleaseProvedClear")
    clear = restore.index("Bot_DeclaredDoorGuardClearAction", release)
    held_only = restore.index("result != SG_COMPOUND_GUARD_NOT_CLEAR", clear)
    hold = restore.index("SG_DeclaredDoorGuardHoldOpen(bot, 500)", held_only)
    record = restore.index("Bot_DeclaredDoorGuardRecord", hold)
    elapsed = restore.index("paused_for = level.time", held_only)
    resume = restore.index("SG_DeclaredDoorGuardResume", elapsed)
    authorize = restore.index("SG_DeclaredDoorGuardAuthorize", resume)
    deadline = restore.index("bot->commit_until = shifted_deadline", resume)
    unlatch = restore.index("bot->declared_guard_paused = false;", deadline)
    fresh = restore.index("bot->declared_egress_proof_frame = -1;", unlatch)
    suffix = restore.index("bot->declared_door_suffix_ms = 0;", fresh)
    assert (
        release < clear < held_only < hold < record < elapsed < resume < authorize < deadline
        < unlatch < fresh < suffix
    )
    assert "paused_for < 0.0f" in restore
    assert "!isfinite(shifted_deadline)" in restore


def test_incompatible_physics_holds_before_clear_or_clientthink() -> None:
    arach = source("slipgate/sg_arach.c")
    think = arach[arach.index("void SG_BotThink") :]
    assert "if (!rune_compatible && !declared_door_guarded)" in think
    incompatible = between(
        think,
        "if (!rune_compatible)\n\t{",
        "if (bot->declared_guard_paused)\n\t{",
    )
    abort = incompatible.index("ctf_hook_abort")
    retain = incompatible.index("Bot_DeclaredDoorGuardRetainOrRelease", abort)
    held_return = incompatible.index("return;", retain)
    clear = incompatible.index("bot->declared_activated = false;", held_return)
    refence = incompatible.index("SG_DeclaredDoorGuardRunState(bot)", clear)
    clientthink = incompatible.index("ClientThink(e, &tc.cmd);", clear)
    assert abort < retain < held_return < clear < refence < clientthink
    assert incompatible.index("SG_BotLocalizationInvalidate(bot);", held_return) < clear


def test_stale_rope_obeys_the_same_held_lease_rule() -> None:
    arach = source("slipgate/sg_arach.c")
    stale = between(
        arach,
        "if (bot->hook_phase == 0 &&",
        "Think_RespawnEdge(bot, e);",
    )
    abort = stale.index("ctf_hook_abort")
    identify = stale.index("Bot_DeclaredDoorGuardAction", abort)
    retain = stale.index("Bot_DeclaredDoorGuardRetainOrRelease", identify)
    held_return = stale.index("return;", retain)
    clear = stale.index("bot->commit_link = -1;", held_return)
    refence = stale.index("SG_DeclaredDoorGuardRunState(bot)", clear)
    clientthink = stale.index("ClientThink(e, &tc.cmd);", clear)
    assert abort < identify < retain < held_return < clear < refence < clientthink


def test_durable_door_dominates_late_link_overrides_before_move() -> None:
    arach = source("slipgate/sg_arach.c")
    pipeline = between(
        arach,
        "bestlink = Think_CommitLink(bot, &tc);",
        "Think_Emit(bot, &tc);",
    )
    durable = pipeline.index("Bot_DeclaredDoorGuardRecord(bot, &record)")
    normalize = pipeline.index("bestlink = record.link_index;", durable)
    refence = pipeline.index("SG_DeclaredDoorGuardRunState(bot)", normalize)
    publish = pipeline.index("tc.bestlink = bestlink;", refence)
    move = pipeline.index("Think_Move(bot, &tc);", publish)
    assert durable < normalize < refence < publish < move
    assert pipeline.index("SG_DeclaredDoorTerminalDeath(bot)", durable) < normalize


def test_retirement_terminal_gibs_before_dead_command_path() -> None:
    arach = source("slipgate/sg_arach.c")
    think = arach[arach.index("void SG_BotThink") :]
    scheduler = between(
        think,
        "run_state = SG_DeclaredDoorGuardRunState(bot);",
        "if (Think_Dead(bot, e, &tc.cmd, true))",
    )
    terminal = scheduler.index("run_state == SG_COMPOUND_GUARD_RUN_TERMINAL")
    gib = scheduler.index("SG_DeclaredDoorTerminalDeath(bot)", terminal)
    dead = scheduler.index("if (e->deadflag)", gib)
    corpse = scheduler.index("Think_Dead(bot, e, &tc.cmd", dead)
    assert terminal < gib < dead < corpse

    move = source("slipgate/sg_move.c")
    death = between(
        move, "void SG_DeclaredDoorTerminalDeath", "static qboolean DoorStep_AbortDeclared"
    )
    assert "ent->deadflag || ent->health <= 0" not in death
    assert death.index("ent->health = -100;") < death.index("player_die(")


if __name__ == "__main__":
    test_header_and_life_reset()
    test_release_pause_and_restore_law()
    test_incompatible_physics_holds_before_clear_or_clientthink()
    test_stale_rope_obeys_the_same_held_lease_rule()
    test_durable_door_dominates_late_link_overrides_before_move()
    test_retirement_terminal_gibs_before_dead_command_path()
    print("declared_door_guard_arach_integration: ok")
