#!/usr/bin/env python3
"""Pin ordinary-door authorization ahead of every callback mutation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def test_declared_authorization_surface_replaces_observers() -> None:
    header = source("slipgate/sg_local.h")
    trigger = source("g_trigger.c")
    func = source("g_func.c")
    owned_surface = header + trigger + func

    assert "SG_NoteDoorTriggerTouch" not in owned_surface
    assert "SG_NoteDoorActivation" not in owned_surface
    assert "qboolean\tSG_AuthorizeDoorTriggerTouch" in header
    assert "qboolean\tSG_AuthorizeDoorTriggerUse" in header
    assert "qboolean\tSG_AuthorizeDoorActivation" in header


def test_touch_multi_authorizes_before_publication() -> None:
    touch = between(source("g_trigger.c"), "void Touch_Multi", "/*QUAKED trigger_multiple")
    authorized = touch.index("if (!SG_AuthorizeDoorTriggerTouch(self, other))")

    assert touch.index("if(other->client)") < touch.index(
        "else if (other->svflags & SVF_MONSTER)"
    ) < touch.index("if (!VectorCompare(self->movedir, vec3_origin))") < authorized
    assert authorized < touch.index("self->activator = other;") < touch.index(
        "multi_trigger (self);"
    )


def test_remote_trigger_use_authorizes_before_publication() -> None:
    use = between(source("g_trigger.c"), "void Use_Multi", "void Touch_Multi")
    authorized = use.index("if (!SG_AuthorizeDoorTriggerUse(ent, activator))")
    assert authorized < use.index("ent->activator = activator;")
    assert authorized < use.index("multi_trigger (ent);")


def test_auto_door_touch_authorizes_before_debounce_and_use() -> None:
    touch = between(source("g_func.c"), "void Touch_DoorTrigger", "void Think_CalcMoveSpeed")
    authorized = touch.index("if (!SG_AuthorizeDoorTriggerTouch(self, other))")

    assert touch.index("if (other->health <= 0)") < touch.index(
        "if (!(other->svflags & SVF_MONSTER) && (!other->client))"
    ) < touch.index("if ((self->owner->spawnflags & DOOR_NOMONSTER)") < authorized
    assert authorized < touch.index("if (level.time < self->touch_debounce_time)")
    assert authorized < touch.index(
        "self->touch_debounce_time = level.time + SG_MOVER_DOOR_TRIGGER_DEBOUNCE;"
    )
    assert authorized < touch.index("door_use (self->owner, self, other);")


def test_door_use_authorizes_before_all_team_mutation() -> None:
    use = between(source("g_func.c"), "void door_use (", "void Touch_DoorTrigger")
    team_slave = use.index("if (self->flags & FL_TEAMSLAVE)")
    authorized = use.index("if (!SG_AuthorizeDoorActivation(other, self, activator))")

    assert team_slave < authorized < use.index("if (self->spawnflags & DOOR_TOGGLE)")
    assert authorized < use.index("ent->message = NULL;")
    assert authorized < use.index("ent->touch = NULL;")
    assert authorized < use.index("door_go_down (ent);")
    assert authorized < use.index("door_go_up (ent, activator);")


def test_shootable_door_authorizes_before_team_reset() -> None:
    killed = between(source("g_func.c"), "void door_killed", "void door_touch")
    authorized = killed.index("if (!SG_AuthorizeDoorActivation")
    assert authorized < killed.index("ent->health = ent->max_health;")
    assert authorized < killed.index("ent->takedamage = DAMAGE_NO;")
    assert killed.index("door_use (self->teammaster, attacker, attacker);") > authorized


def test_generator_serializes_bounded_delayed_sound_terminals() -> None:
    oracle = source("slipgate/sg_oracle.c")
    delayed = between(
        oracle,
        "static qboolean SG_OraclePlanSoundOnlyTargets",
        "static qboolean SG_OracleDoorEffectsSafe",
    )
    activator = between(
        oracle,
        "static qboolean SG_OracleDeclaredActivatorSafeWithDelay(",
        "static qboolean SG_OracleDeclaredActivatorSafe(edict_t *trigger)\n{",
    )
    door_effects = between(
        oracle,
        "static qboolean SG_OracleDoorEffectsSafe",
        "/* Static declared mechanisms",
    )
    assert "!isfinite(source->delay) || source->delay < 0.0f" in delayed
    assert "source->killtarget" in delayed
    assert "depth > 4" in delayed
    assert "target_speaker" in delayed
    assert "trigger_relay" in delayed
    assert "SG_OraclePlanSoundOnlyTargets(target, depth + 1)" in delayed
    assert "trigger->wait > (float)RUNE_MAX_COST_MS / 1000.0f" not in activator
    assert "SG_OraclePlanSoundOnlyTargets(target, 1)" in activator
    assert "SG_OraclePlanSoundOnlyTargets(target, 1)" in door_effects

    generator = between(
        source("slipgate/sg_rune.c"),
        "static void Link_Doors(door_topology_t *topology)",
        "rune: %d declared door links",
    )
    admission = generator.index("SG_DeclaredDoorActivatorSafe(door)")
    travel = generator.index("Door_TravelMs(door)", admission)
    combined_replay = generator.index(
        "SG_OracleValidateDeclaredDoorLink(", travel
    )
    insert_call = generator.index("Door_LinkInsert(", travel)
    assert admission < travel < combined_replay < insert_call

    insert = between(
        source("slipgate/sg_rune.c"),
        "static qboolean Door_LinkInsert",
        "/* Link_Doors runs before objective pruning",
    )
    assert "Mechanism_BindDoor" in insert

    materializer = source("slipgate/sg_rune_mechanism_plan.c")
    delayed_closure = between(
        materializer,
        "static int Mechanism_DelayedSoundOnlyRelay",
        "static int Mechanism_AppendInventoryEdge",
    )
    side_effect = between(
        materializer,
        "static int Mechanism_AddSideEffect",
        "static int Mechanism_MaterializePlatform",
    )
    direct = between(
        materializer,
        "case SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR:",
        "default:",
    )
    assert "node->delay_ms < 0" in delayed_closure
    assert "edge->delay_ms != (uint32_t)node->delay_ms" in delayed_closure
    assert "depth > 4" in delayed_closure
    assert "Mechanism_AppendInventoryEdge(state, inventory_index)" in side_effect
    assert "destination->delay_ms > 0" in side_effect
    assert "Mechanism_DelayedSoundOnlyRelay(state, destination_index, 1)" in side_effect
    assert "entry->wait_ms > RUNE_MAX_COST_MS" in direct
    assert "binding->cooldown_ms != cooldown" in direct

    codec = source("slipgate/sg_rune_codec.c")
    assert "static int Codec_DelayedSoundOnlyRelay" in codec
    assert "nodes[entry_index].wait_ms >" in codec
    assert "SG_RUNE_CODEC_MAX_TIME_MS" in codec

    use_targets = between(
        source("g_utils.c"),
        "void G_UseTargets (edict_t *ent, edict_t *activator)",
        "if ((ent->message)",
    )
    assert use_targets.index("SG_HandleMechanismTargets(ent, activator)") < \
        use_targets.index("if (ent->delay)")
    runtime = between(
        source("slipgate/sg_move.c"),
        "qboolean SG_HandleMechanismTargets",
        "static qboolean MechanismStep_Binding",
    )
    assert "source->delay != 0.0f" in runtime
    assert "SG_RuneMechanismBindingDispatchTargets" in runtime

    contract = between(
        oracle,
        "static int SG_DoorContractCost",
        "int SG_DeclaredDoorContractCost",
    )
    assert "binding->entry_node->wait_ms" in contract
    assert "int64_t longest" in contract
    assert "nominal > 12500.0" in contract
    assert "hold_ms >= (double)INT64_MAX" in contract

    cooldown = between(
        source("slipgate/sg_rune.c"),
        "static int Door_CooldownGapMs",
        "typedef struct door_drop_candidate_s",
    )
    assert "int64_t longest_cycle" in cooldown
    assert "nominal > 12500.0" in cooldown
    assert "hold_ms >= (double)INT64_MAX" in cooldown


def test_unbound_source_passthrough_excludes_held_claims() -> None:
    move = source("slipgate/sg_move.c")
    binding = between(
        move,
        "static qboolean DoorStep_DeclaredBindingForLink",
        "static qboolean DoorStep_DeclaredClaimHeld",
    )
    assert "SG_RuneMechanismBindingCapture" in binding
    assert "SG_RunePhysicsCompatible" in binding
    assert "SG_RuneMechanismBindingMoverKeys" in binding
    assert "SG_RuneMechanismBindingResolveNode" in binding
    assert "SG_RuneMechanismBindingCurrent" in binding
    assert "SG_DeclaredDoorForLink" not in binding

    helper = between(
        move,
        "static qboolean DoorStep_DeclaredClaimHeld",
        "/* Touch_Multi and Touch_DoorTrigger",
    )
    assert "bot->declared_guard_paused" in helper
    assert "DoorStep_DeclaredBinding(bot, &binding)" in helper
    assert "record.law == SG_MOVER_LAW_DECLARED_DOOR" in helper
    assert "record.state == SG_MOVER_LEASE_ACTIVE" in helper
    assert "record.state == SG_MOVER_LEASE_PAUSED" in helper
    assert "record.state == SG_MOVER_LEASE_QUARANTINED" not in helper

    touch = between(
        move,
        "qboolean SG_AuthorizeDoorTriggerTouch",
        "/* G_UseTargets reaches",
    )
    unbound = touch.index("if (!DoorStep_DeclaredBinding(bot, &binding) ||")
    entry = touch.index("binding.entry_entity != source", unbound)
    deny = touch.index("return !DoorStep_DeclaredClaimHeld(bot) &&", entry)
    global_gate = touch.index("!SG_DeclaredDoorGuardAnyClaim()", deny)
    authorize = touch.index("SG_DeclaredDoorGuardAuthorizeActivation", global_gate)
    current = touch.index("SG_RuneMechanismBindingCurrent", authorize)
    assert unbound < entry < deny < global_gate < authorize < current

    activation = between(
        move,
        "qboolean SG_AuthorizeDoorActivation",
        "static sg_bot_t *TraversalLiveEventOwner",
    )
    unbound = activation.index("if (!DoorStep_DeclaredBinding(bot, &binding) ||")
    entry = activation.index("binding.entry_entity != source", unbound)
    deny = activation.index("return !DoorStep_DeclaredClaimHeld(bot) &&", entry)
    available = activation.index(
        "SG_DeclaredDoorGuardActivationAvailable(door_master)", deny
    )
    global_gate = activation.index("!SG_DeclaredDoorGuardAnyClaim()", deny)
    authorize = activation.index("SG_DeclaredDoorGuardAuthorizeActivation", available)
    mover = activation.index("DoorStep_BindingContainsMover", authorize)
    current = activation.index("SG_RuneMechanismBindingCurrent", mover)
    assert unbound < entry < deny < global_gate < available < authorize < mover < current

    assert "DoorStep_DeclaredLinkSnapshot" not in move
    assert "DoorStep_SupportedActivator" not in move
    assert "SG_DeclaredDoorForLink" not in move


if __name__ == "__main__":
    test_declared_authorization_surface_replaces_observers()
    test_touch_multi_authorizes_before_publication()
    test_remote_trigger_use_authorizes_before_publication()
    test_auto_door_touch_authorizes_before_debounce_and_use()
    test_door_use_authorizes_before_all_team_mutation()
    test_shootable_door_authorizes_before_team_reset()
    test_generator_serializes_bounded_delayed_sound_terminals()
    test_unbound_source_passthrough_excludes_held_claims()
    print("declared_door_guard_integration: ok")
