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
    assert authorized < touch.index("self->touch_debounce_time = level.time + 1.0;")
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


def test_malformed_source_passthrough_excludes_held_claims() -> None:
    move = source("slipgate/sg_move.c")
    helper = between(
        move,
        "static qboolean DoorStep_DeclaredClaimHeld",
        "/* Touch_Multi and Touch_DoorTrigger",
    )
    assert "bot->declared_guard_paused" in helper
    assert "DoorStep_DeclaredLinkSnapshot(bot, anchor, source)" in helper
    assert "record.law == SG_MOVER_LAW_DECLARED_DOOR" in helper
    assert "record.state == SG_MOVER_LEASE_ACTIVE" in helper
    assert "record.state == SG_MOVER_LEASE_PAUSED" in helper
    assert "record.state == SG_MOVER_LEASE_QUARANTINED" not in helper
    touch = between(
        move,
        "qboolean SG_AuthorizeDoorTriggerTouch",
        "/* G_UseTargets reaches",
    )
    unsupported = touch.index("if (!DoorStep_SupportedActivator(source))")
    deny = touch.index("return !DoorStep_DeclaredClaimHeld(bot) &&", unsupported)
    global_gate = touch.index("!SG_DeclaredDoorGuardAnyClaim()", deny)
    assert unsupported < deny < global_gate
    activation = between(
        move,
        "qboolean SG_AuthorizeDoorActivation",
        "static sg_bot_t *Drop_LiveEventOwner",
    )
    unsupported = activation.index("if (!DoorStep_SupportedActivator(source))")
    deny = activation.index("return !DoorStep_DeclaredClaimHeld(bot) &&", unsupported)
    available = activation.index(
        "SG_DeclaredDoorGuardActivationAvailable(door_master)", deny
    )
    global_gate = activation.index("!SG_DeclaredDoorGuardAnyClaim()", deny)
    assert deny < global_gate < available

    snapshot = between(
        move,
        "static qboolean DoorStep_DeclaredLinkSnapshot",
        "static qboolean DoorStep_DeclaredClaimHeld",
    )
    assert "!rune->links || !rune->seeds" in snapshot
    assert "!SG_RunePublishedShapeValid(rune)" in snapshot
    assert "!SG_RunePhysicsCompatible(rune)" in snapshot
    assert "rune->hdr.num_links > RUNE_MAX_LINKS" in snapshot
    assert "rune->hdr.num_seeds > SG_MAX_SEEDS" in snapshot
    for callback in (touch, activation):
        assert callback.index("SG_DeclaredDoorGuardAuthorizeActivation") < callback.index(
            "DoorStep_DeclaredLinkSnapshot"
        )
    assert unsupported < deny < available


if __name__ == "__main__":
    test_declared_authorization_surface_replaces_observers()
    test_touch_multi_authorizes_before_publication()
    test_remote_trigger_use_authorizes_before_publication()
    test_auto_door_touch_authorizes_before_debounce_and_use()
    test_door_use_authorizes_before_all_team_mutation()
    test_shootable_door_authorizes_before_team_reset()
    test_malformed_source_passthrough_excludes_held_claims()
    print("declared_door_guard_integration: ok")
