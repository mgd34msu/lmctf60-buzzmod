#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def ordered(text: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        cursor = text.index(needle, cursor) + len(needle)


def main() -> None:
    service = source("g_svcmds.c")
    dispatch = between(service, "static void SVCmd_SG_f", "static void SVCmd_POVRecord_f")
    compound_hook = between(
        dispatch,
        'else if (Q_stricmp(sub, "compoundhook") == 0)',
        "else\n\t\tgi.cprintf",
    )
    for required in (
        "strtol(arg, &end, 10)",
        "!*arg",
        "*end",
        "link < 0",
        "link > INT_MAX",
        "SG_CompoundHookGameStageAuthenticatedProbe((int)link)",
        "authenticated compound hook probe refused",
    ):
        assert required in compound_hook
    assert "compoundhook <link>" in dispatch

    game = source("slipgate/sg_compound_hook_game.c")
    staging = between(
        game,
        "int SG_CompoundHookGameStageAuthenticatedProbe",
        "sg_compound_hook_live_result_t SG_CompoundHookGameBegin",
    )
    ordered(
        staging,
        "SG_CompoundHookGameIdleAdmission(&sg_bots[slot])",
        "SG_HookOffhandReady(sg_bots[slot].ent)",
        "gi.unlinkentity(entity)",
        "binding->source.pms",
        "SG_CompoundHookGameReset(bot)",
        "sg_host.linkentity(entity)",
        "SG_HookOffhandReady(entity)",
        "SG_CompoundHookGameBegin(bot, (uint32_t)link_index, true)",
        'SG_CompoundHookGameDebugResult(bot, "begin", &result)',
        "dhook probe-staged",
    )
    assert "*entity->client = client_before" in staging
    assert "*entity = entity_before" in staging
    assert "*bot = bot_before" in staging

    weapon = source("p_weapon.c")
    launch = between(weapon, "edict_t *fire_hook", "void Draw_Hook")
    assert "bolt->touch = SG_BotHookTouch;" in launch
    assert "bolt->touch (bolt, tr.ent, &tr.plane, NULL);" in launch
    assert "bolt->touch (bolt, tr.ent, NULL, NULL);" not in launch
    ordered(
        launch,
        "if (SG_OwnsBot(self))",
        "SG_CompoundGuardGameHookLinked(",
        "SG_CompoundHookGameLinked(",
        "tr = gi.trace",
        "if (SG_OwnsBot(self))",
        "self->client->hook = bolt",
        "bolt->touch",
        "if (self->client->hook != bolt)",
        "return NULL",
    )
    assert "G_FreeEdict" in launch
    assert "return NULL" in launch

    bot_touch = between(
        weapon, "static void SG_BotHookTouch", "void hook_touch"
    )
    ordered(
        bot_touch,
        "SG_CompoundHookGameAttachWillApply(",
        "VectorClear (self->velocity)",
        "self->hook_target = other",
        "gi.linkentity(self)",
        "SG_CompoundHookGameAttached(",
    )

    human_touch = between(weapon, "void hook_touch", "void Grapple_Bolt_Think")
    assert "SG_HumanTraceHookAttach" in human_touch
    for bot_only in ("SG_OwnsBot", "SG_BotHookTouch", "SG_Compound"):
        assert bot_only not in human_touch
    ordered(
        human_touch,
        "VectorClear (self->velocity)",
        "self->hook_target = other",
        "gi.linkentity(self)",
    )

    human_launch = between(
        weapon, "static edict_t *LMCTF_FireHumanHook", "edict_t *fire_hook"
    )
    assert "G_ProjectileOwnerSet(bolt, self);" in human_launch
    assert "bolt->owner = self;" not in human_launch
    assert "bolt->touch = hook_touch;" in human_launch
    assert "SG_HumanTraceHookFire" in human_launch
    for bot_only in ("SG_OwnsBot", "SG_BotHookTouch", "SG_Compound"):
        assert bot_only not in human_launch

    pull = between(weapon, "void CTF_HookPullStep", "void Weapon_Hook_Fire")
    ordered(
        pull,
        "ent->client->hooklength = speed",
        "VectorCopy (velocity, ent->velocity)",
        "VectorCopy (ent->velocity, ent->client->oldvelocity)",
        "if (SG_OwnsBot(ent))",
        "SG_CompoundHookGamePullApplied(",
    )

    fire = between(
        weapon, "void Weapon_Hook_Fire", "void Weapon_Hook (edict_t *ent)"
    )
    assert "if (!SG_OwnsBot(ent))" in fire
    assert "LMCTF_HumanHookFire(ent);" in fire
    assert "if (SG_OwnsBot(ent) && !ent->client->hook)" in fire

    commands = source("g_cmds.c")
    release = between(commands, "void Cmd_Unhook_f", "void Cmd_Ctfmenu_f")
    assert "ctf_hook_abort(ent);" in release
    selected_release = between(
        release,
        "if (ent->client->pers.weapon == it)",
        "else",
    )
    ordered(selected_release, "ctf_hook_abort(ent);", 'ForceCommand(ent, "-attack\\n");')
    assert "hook_input_release" not in commands
    assert "hook_input_release" not in source("g_local.h")
    assert "LMCTF_HumanHookInputFrame" not in source("p_view.c")

    abort = between(source("g_ctffunc.c"), "void ctf_hook_abort", "char *")
    ordered(
        abort,
        "if (SG_OwnsBot(ent))",
        "SG_CompoundHookGameAbortBegin(",
        "ent->client->hookstate = 0",
        "G_FreeEdict",
        "ent->client->hook = NULL",
        "SG_CompoundHookGameAbortEnd(",
    )

    disconnect = between(
        source("p_client.c"), "void ClientDisconnect", "void ClientThink"
    )
    ordered(
        disconnect,
        "SG_CancelBotDelayedUses(ent)",
        "SG_CompoundGuardGameClientDisconnecting(ent)",
        "G_FreeEdict (dead_hook)",
        "ent->client->hook = NULL",
        "SG_CompoundGuardGameBoltEvicted(",
        "gi.unlinkentity (ent)",
        "ent->inuse = false",
        "SG_CompoundGuardGameClientDisconnected(",
    )

    move = source("slipgate/sg_move.c")
    trigger = between(
        move,
        "qboolean SG_AuthorizeDoorTriggerTouch",
        "qboolean SG_AuthorizeDoorTriggerUse",
    )
    assert trigger.index("SG_CompoundDropGameAuthorizeTouch(") < trigger.index(
        "SG_CompoundHookGameAuthorizeTouch("
    )
    activation = between(
        move,
        "qboolean SG_AuthorizeDoorActivation",
        "static sg_bot_t *TraversalLiveEventOwner",
    )
    assert activation.index(
        "SG_CompoundDropGameAuthorizeActivation("
    ) < activation.index("SG_CompoundHookGameAuthorizeActivation(")

    command_loop = between(move, "for (step = 0; step < sub; step++)", "hook_wait:;")
    ordered(
        command_loop,
        "SG_CompoundHookLivePreStep(",
        "SG_CompoundHookLiveApproveCommand(",
        "ClientThink(e, cmd)",
    )
    approval = between(
        command_loop,
        "result = SG_CompoundHookLiveApproveCommand(",
        "ClientThink(e, cmd)",
    )
    ordered(
        approval,
        "!bot->compound_hook_live.command_pending",
        "!bot->compound_hook_live.command_approved",
        "SG_CompoundHookGameRecoverOwnedFailure(",
    )
    assert command_loop.index("if (bot->compound_hook_live.guard_owned)") < command_loop.index(
        "ctf_hook_abort(e)"
    )
    after_client = command_loop[command_loop.index("ClientThink(e, cmd)") :]
    hook_step = between(
        after_client,
        "if (compound_hook && bot->compound_hook_live.guard_owned)",
        "if (proved_swim && bot->swim_replay_active",
    )
    assert "step < sub - 1 ?" in hook_step
    assert "SG_CompoundHookLivePostStep(" in hook_step
    assert "SG_CompoundHookLiveBoundary(" in hook_step
    assert "step == sub - 1" in hook_step
    ordered(
        hook_step,
        "SG_COMPOUND_HOOK_LIVE_COMPLETE",
        "SG_CompoundHookGameApplyRequestedRelease(",
        "Cmd_Hook_f(e)",
    )
    begin = between(
        move,
        "if (compound_hook && !bot->compound_hook_live.guard_owned)",
        "if (tc->think_over)",
    )
    assert "SG_HookOffhandReady(e)" in begin
    ordered(
        begin,
        "SG_CompoundHookGameBegin(",
        'SG_CompoundHookGameDebugResult(bot, "begin", &result)',
        "result.outcome != SG_COMPOUND_HOOK_LIVE_RUNNING",
    )

    hook_game = source("slipgate/sg_compound_hook_game.c")
    touch_debug = between(
        hook_game,
        "SG_CompoundHookGameAuthorizeTouch(",
        "SG_CompoundHookGameAuthorizeActivation",
    )
    ordered(
        touch_debug,
        "SG_CompoundHookLiveTouch(",
        'SG_CompoundHookGameDebugResult(bot, "touch", &result)',
    )
    activation_debug = between(
        hook_game,
        "SG_CompoundHookGameAuthorizeActivation(",
        "SG_CompoundHookGameRecoverOwnedFailure",
    )
    ordered(
        activation_debug,
        "SG_CompoundHookLiveActivate(",
        'SG_CompoundHookGameDebugResult(bot, "activation", &result)',
    )
    assert 'CompoundHookGameDebugStage(bot, "release-requested")' in hook_game

    events = source("slipgate/sg_compound_hook_game_events.c")
    for reducer, stage in (
        ("SG_CompoundHookLiveLinked(", "linked"),
        ("SG_CompoundHookLiveAttached(", "attached"),
        ("SG_CompoundHookLivePullApplied(", "pull"),
        ("SG_CompoundHookLiveReleaseApplied(", "release"),
    ):
        ordered(
            events,
            reducer,
            f'SG_CompoundHookGameDebugResult(bot, "{stage}", &result)',
        )
    assert 'SG_CompoundHookGameDebugResult(bot, "terminal", &result)' in hook_step

    physical_hook_game = source("slipgate/sg_hook_game.c")
    offhand = between(
        physical_hook_game,
        "qboolean SG_HookOffhandReady",
        "static qboolean Hook_LiveWitnessOK",
    )
    for required in (
        "CTF_OFFHAND_HOOK",
        "RIGHT_HANDED",
        "pers.inventory",
        "pers.weapon != hook",
        "newweapon != hook",
        "hookstate == 0",
        "hook == NULL",
    ):
        assert required in offhand

    dominance = between(
        move,
        "guard_result = SG_CompoundGuardValidate",
        "qboolean drop_yaw_locked",
    )
    assert "RL_DOOR_DROP" in dominance and "RL_DOOR_HOOK" in dominance
    assert "bot->compound_drop_live.guard_owned" in dominance
    assert "bot->compound_hook_live.guard_owned" in dominance

    arach = source("slipgate/sg_arach.c")
    stale_cleanup = between(
        arach, "if (bot->hook_phase == 0 &&", "Think_RespawnEdge(bot, e);"
    )
    ordered(
        stale_cleanup,
        "!SG_CompoundHookGameOwnsHostRope(bot)",
        "ctf_hook_abort(e)",
        "ClientThink(e, &tc.cmd)",
    )

    descend = source("slipgate/sg_descend.c")
    assert "SG_CompoundHookLiveBoundary(" not in descend
    assert "Cmd_Hook_f(e)" not in descend
    assert (
        "l->action == RL_HOOK || l->action == RL_CHAIN_HOOK ||" in descend
        and "l->action == RL_DOOR_HOOK" in descend
    )


if __name__ == "__main__":
    main()
    print("test_compound_hook_game_integration: ok")
