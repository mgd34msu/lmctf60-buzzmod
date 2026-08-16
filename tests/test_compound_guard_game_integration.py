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
    retired = disconnect.index("SG_CompoundGuardGameClientDisconnected")
    assert disconnect.index("gi.unlinkentity (ent);") < disconnect.index(
        "ent->solid = SOLID_NOT;"
    ) < disconnect.index("ent->inuse = false;") < retired
    assert "SG_CompoundGuardGameBoltEvicted" not in disconnect


def test_frame_level_hook_and_free_order() -> None:
    arach = source("slipgate/sg_arach.c")
    frame = between(arach, "void SG_RunFrame", "/* ---------------------------------------------------------------- spawn */")
    assert frame.index("SG_LevelChange();") < frame.index(
        "SG_CompoundGuardGameFrame();"
    ) < frame.index("Botfill_Frame();")
    level = arach[arach.index("void SG_LevelChange(void)") :]
    assert level.index("SG_CompoundGuardGameLevelReset") < level.index(
        "SG_AcceptDropLevelReset"
    ) < level.index("SG_RemoveBots();")

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
    test_adapter_is_inert_and_owns_generations()
    print("compound_guard_game_integration: ok")
