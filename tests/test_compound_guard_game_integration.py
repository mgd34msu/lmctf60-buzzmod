#!/usr/bin/env python3
"""Pin the live ordering and inertness of the compound-guard lifecycle."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def test_bot_slot_reset_and_attach_order() -> None:
    client = source("slipgate/sg_client.c")
    reset = between(client, "static void BotSlot_Reset", "static const char *sg_names")
    assert reset.index("SG_CompoundGuardGameBotSlotReset") < reset.index(
        "SG_CompoundSwimGameReset"
    ) < reset.index(
        "memset(bot, 0, sizeof(*bot))"
    )
    add = between(client, "qboolean SG_AddBotTeam", "int SG_RemoveBots")
    assert add.index("sg_bots[slot].ent = ent;") < add.index(
        "sg_bots[slot].active = true;"
    ) < add.index("SG_CompoundGuardGameBotAttach")


def test_death_body_respawn_and_disconnect_order() -> None:
    client = source("p_client.c")
    death = between(client, "void player_die", "void InitClientPersistent")
    first_death = between(death, "if (!self->deadflag)", "int\t\tn;")
    assert first_death.index("SG_CompoundGuardGamePlayerDie") < first_death.index(
        "SG_NoteDeath"
    )
    hook_free = death.index("G_FreeEdict (dead_hook);")
    assert hook_free < death.index("self->client->hook = NULL;", hook_free) < death.index(
        "SG_CompoundGuardGameBoltEvicted", hook_free
    )
    assert death.count("SG_CompoundGuardGameBoltEvicted") == 1

    init_body = between(client, "void InitBodyQue", "void body_die")
    assert init_body.index('ent->classname = "bodyque";') < init_body.index(
        "SG_CompoundGuardGameBodyQueueInit"
    )
    copy = between(client, "void CopyToBodyQue", "void respawn")
    assert copy.index("SG_CompoundGuardGameBodyWillReplace") < copy.index(
        "gi.unlinkentity (ent);"
    )
    assert copy.index("gi.linkentity (body);") < copy.index(
        "SG_CompoundGuardGameBodyDidCopy"
    )

    spawn = between(client, "void PutClientInServer", "void ClientBeginDeathmatch")
    spawned = spawn.index("SG_CompoundGuardGameClientSpawned")
    assert spawn.rindex("gi.linkentity (ent);", 0, spawned) < spawn.index(
        "client->ctf.ctfid = unique_id++;"
    ) < spawn.index("ChangeWeapon (ent);", 0, spawned) < spawned

    disconnect = between(client, "void ClientDisconnect", "//==============================================================")
    cancelled = disconnect.index("SG_CancelBotDelayedUses")
    retired = disconnect.index("SG_CompoundGuardGameClientDisconnected")
    assert cancelled < disconnect.index("gi.unlinkentity (ent);")
    assert disconnect.index("gi.unlinkentity (ent);") < disconnect.index(
        "ent->solid = SOLID_NOT;"
    ) < disconnect.index("ent->inuse = false;") < retired
    assert "SG_CompoundGuardGameBoltEvicted" not in disconnect

    adapter = source("slipgate/sg_compound_guard_game.c")
    die_adapter = between(
        adapter,
        "sg_compound_guard_result_t SG_CompoundGuardGamePlayerDie",
        "void SG_CompoundGuardGameEntityFreed",
    )
    disconnected_adapter = between(
        adapter,
        "sg_compound_guard_result_t SG_CompoundGuardGameClientDisconnected",
        "#ifdef SG_COMPOUND_GUARD_GAME_TEST",
    )
    assert die_adapter.index("SG_CompoundGuardOrphan") < die_adapter.index(
        "SG_CompoundSwimGameClientRetired"
    )
    assert disconnected_adapter.index("SG_CompoundGuardBotDisconnected") < (
        disconnected_adapter.index("SG_CompoundSwimGameClientRetired")
    )

    utils = source("g_utils.c")
    delayed = between(
        utils, "void Think_Delay", "/*\n==============================\nG_UseTargets"
    )
    assert delayed.index("SG_DELAYED_USE_BOT_ACTIVATOR") < delayed.index(
        "G_UseTargets (ent, ent->activator);"
    )
    use_targets = between(utils, "void G_UseTargets", "/*\n=============\nTempVector")
    assert 'strcmp(ent->classname, "DelayedUse") == 0' in use_targets
    assert use_targets.index("SG_OwnsBot(activator)") < use_targets.index(
        "t->spawnflags |= SG_DELAYED_USE_BOT_ACTIVATOR;"
    )


def test_frame_level_hook_and_free_order() -> None:
    arach = source("slipgate/sg_arach.c")
    frame = between(arach, "void SG_RunFrame", "/* ---------------------------------------------------------------- spawn */")
    assert frame.index("SG_LevelChange();") < frame.index(
        "SG_CompoundGuardGameFrame();"
    ) < frame.index("Botfill_Frame();")
    level = arach[arach.index("void SG_LevelChange(void)") :]
    assert level.index("SG_CompoundGuardGameLevelReset") < level.index("SG_RemoveBots();")

    weapon = source("p_weapon.c")
    fire = between(weapon, "edict_t *fire_hook", "// Ent is the owner")
    assert fire.index("gi.linkentity (bolt);") < fire.index(
        "SG_CompoundGuardGameHookLinked"
    ) < fire.index("tr = gi.trace")

    utils = between(source("g_utils.c"), "void G_FreeEdict", "G_TouchTriggers")
    protected = utils.index("return;", utils.index("BODY_QUEUE_SIZE"))
    retired = utils.index("SG_CompoundGuardGameEntityFreed")
    assert protected < utils.index("memset (ed, 0, sizeof(*ed));") < retired

    shutdown = between(source("g_main.c"), "void ShutdownGame", "//===================================================================")
    assert shutdown.index("SG_RosterStorageReset") < shutdown.index(
        "SG_CompoundGuardGameStorageWillFree"
    ) < shutdown.index(
        "gi.FreeTags (TAG_LEVEL)"
    )
    read_game = between(source("g_save.c"), "void ReadGame", "//==========================================================")
    assert read_game.index("SG_RosterStorageReset") < read_game.index(
        "DB_Conn_Cleanup"
    ) < read_game.index("SG_CompoundGuardGameStorageWillFree") < read_game.index(
        "gi.FreeTags(TAG_GAME)"
    )
    assert read_game.index("gi.FreeTags(TAG_GAME)") < read_game.index(
        "g_edicts = gi.TagMalloc"
    ) < read_game.index("globals.edicts = g_edicts;")

    roster = between(
        source("slipgate/sg_client.c"),
        "void SG_RosterStorageReset",
        "/*\n * The roster as the admin sees it.",
    )
    assert roster.index("SG_RemoveBots();") < roster.index(
        "for (i = 0; i < SG_MAXBOTS; i++)"
    ) < roster.index("BotSlot_Reset(&sg_bots[i]);")


def test_guarded_pusher_fence_precedes_move_and_think() -> None:
    physics = source("g_phys.c")
    pusher = between(physics, "void SV_Physics_Pusher", "//==================================================================")
    run_entity = physics[physics.index("void G_RunEntity"):]
    fence = run_entity.index("SG_CompoundGuardGameEntityMayDispatch")
    assert fence < run_entity.index("ent->prethink (ent)")
    assert fence < run_entity.index("SV_Physics_Pusher (ent)")
    assert "SG_CompoundGuardGameEntityMayDispatch" not in pusher
    denied = between(
        run_entity,
        "if (ent != g_edicts && !SG_CompoundGuardGameEntityMayDispatch(ent))",
        "if (ent->prethink)",
    )
    assert "return;" in denied
    assert denied.index("SG_CompoundGuardGameEntityDeferred(ent)") < denied.index(
        "return;"
    )
    assert "prethink" not in denied and "nextthink" not in denied

    adapter = source("slipgate/sg_compound_guard_game.c")
    interlock = between(
        adapter,
        "int SG_CompoundGuardGameEntityMayDispatch",
        "static sg_compound_guard_result_t GameMint",
    )
    assert "SG_CompoundGuardDoorPusherFence" in interlock
    assert "SG_MoverProspectivePusherValid" in interlock
    assert "GameAllSubjectsOutsideMode(&game_guard, keys, key_count, 1)" in interlock
    assert "ctf_hook_abort" not in interlock


def test_door_completion_witness_order_and_lifecycle() -> None:
    func = source("g_func.c")
    move_done = between(func, "void Move_Done", "void Move_Final")
    assert move_done.index("VectorClear (ent->velocity)") < move_done.index(
        "SG_MoverCompletionDispatch (ent)"
    )
    assert move_done.index("ent->nextthink = 0") < move_done.index(
        "SG_MoverCompletionDispatch (ent)"
    )
    assert "moveinfo.endfunc (" not in move_done
    move_calc = between(func, "void Move_Calc", "//\n// Support routines for angular")
    assert move_calc.index("ent->moveinfo.endfunc = func;") < move_calc.index(
        "SG_MoverCompletionArm (ent)"
    )
    angle_done = between(func, "void AngleMove_Done", "void AngleMove_Final")
    assert angle_done.index("VectorClear (ent->avelocity)") < angle_done.index(
        "SG_MoverCompletionDispatch (ent)"
    )
    assert angle_done.index("ent->nextthink = 0") < angle_done.index(
        "SG_MoverCompletionDispatch (ent)"
    )
    assert "moveinfo.endfunc (" not in angle_done
    angle_calc = between(func, "void AngleMove_Calc", "/*\n==============\nThink_AccelMove")
    assert angle_calc.index("ent->moveinfo.endfunc = func;") < angle_calc.index(
        "SG_MoverCompletionArm (ent)"
    )

    hit_top = between(func, "void door_hit_top", "void door_hit_bottom")
    assert hit_top.index("self->moveinfo.state = STATE_TOP;") < hit_top.index(
        "SG_MoverCompletionPublish(self, SG_MOVER_COMPLETION_TOP)"
    )
    assert hit_top.index("self->nextthink = level.time + self->moveinfo.wait;") < (
        hit_top.index("SG_MoverCompletionPublish")
    )

    hit_bottom = between(func, "void door_hit_bottom", "void door_go_down")
    assert hit_bottom.index("self->moveinfo.state = STATE_BOTTOM;") < (
        hit_bottom.index("door_use_areaportals")
    ) < hit_bottom.index(
        "SG_MoverCompletionPublish(self, SG_MOVER_COMPLETION_BOTTOM)"
    )

    go_down = between(func, "void door_go_down", "void door_go_up")
    assert go_down.index("SG_MoverCompletionTransition(self)") < go_down.index(
        "self->moveinfo.state = STATE_DOWN;"
    ) < go_down.index("Move_Calc")
    go_up = between(func, "void door_go_up", "void door_use")
    assert go_up.index("if (self->moveinfo.state == STATE_TOP)") < go_up.index(
        "SG_MoverCompletionTransition(self)"
    ) < go_up.index("self->moveinfo.state = STATE_UP;") < go_up.index("Move_Calc")

    adapter = source("slipgate/sg_compound_guard_game.c")
    level_reset = between(
        adapter,
        "sg_compound_guard_result_t SG_CompoundGuardGameLevelReset",
        "void SG_CompoundGuardGameStorageWillFree",
    )
    assert level_reset.index("SG_MoverCompletionReset") < level_reset.index(
        "GameEnsureInitialized"
    )
    storage_free = between(
        adapter,
        "void SG_CompoundGuardGameStorageWillFree",
        "void SG_CompoundGuardGameFrame",
    )
    assert storage_free.index("SG_MoverCompletionReset") < storage_free.index(
        "if (!game_guard.initialized)"
    )
    entity_free = between(
        adapter,
        "void SG_CompoundGuardGameEntityFreed",
        "sg_compound_guard_result_t SG_CompoundGuardGameBoltEvicted",
    )
    assert entity_free.index("SG_MoverCompletionForget") < entity_free.index(
        "if (!game_guard.initialized"
    )


def test_adapter_is_inert_and_owns_generations() -> None:
    adapter = source("slipgate/sg_compound_guard_game.c")
    assert "SG_CompoundGuardAcquire" not in adapter
    assert "SG_MoverLeaseAcquire" not in adapter
    assert "linkcount" not in adapter
    assert "ctfid" not in adapter
    assert "uint64_t generation[MAX_EDICTS];" in adapter
    assert "next_generation == UINT64_MAX" in adapter
    assert "game_guard.generation_exhausted = 1U;" in adapter
    assert "entity - g_edicts" not in adapter
    assert "body - g_edicts" not in adapter


if __name__ == "__main__":
    test_bot_slot_reset_and_attach_order()
    test_death_body_respawn_and_disconnect_order()
    test_frame_level_hook_and_free_order()
    test_guarded_pusher_fence_precedes_move_and_think()
    test_door_completion_witness_order_and_lifecycle()
    test_adapter_is_inert_and_owns_generations()
    print("compound_guard_game_integration: ok")
