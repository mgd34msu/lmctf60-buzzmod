#!/usr/bin/env python3
"""Live RL_CHAIN_HOOK ownership and fail-closed wiring contract."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GAME = (ROOT / "slipgate" / "sg_hook_game.c").read_text()
MOVE = (ROOT / "slipgate" / "sg_move.c").read_text()
WEAPON = (ROOT / "p_weapon.c").read_text()


def between(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


proof = between(
    GAME,
    "sg_hook_game_proof_result_t SG_ChainHookGameOnlineProof",
    "static qboolean Hook_AttachmentOK",
)
assert "SG_RunePhysicsCompatible(rune)" in proof
assert "!SG_HookOffhandReady(e)" in proof
assert "link->action != RL_CHAIN_HOOK" in proof
assert "bot->commit_link != bot->hook_link" in proof
assert "(short)(e->velocity[i] * 8.0f) != 0" in proof
assert "Hook_ProofBudgetTake(bot)" in proof
assert "VectorCopy(link->anchor, controls[0]);" in proof
assert "VectorCopy(link->mechanism_anchor, controls[1]);" in proof
assert "rune->seeds[link->to].origin" in proof
assert "SG_OracleChainHookTraverse" in proof
assert "bot->hook_source_health = e->health;" in proof
assert "proof.rope[0].attach_pms" in proof
assert "proof.rope[1].attach_pms" in proof

begin = between(
    GAME,
    "qboolean SG_ChainHookGameBeginAfterFire",
    "static qboolean ChainHook_WaitAttachFrame",
)
assert "e->client->hookstate != 1" in begin
assert "SG_ChainHookReplayBegin" in begin
assert "bot->chain_hook_first_entity = e->client->hook;" in begin
assert "bot->hook_entity = e->client->hook;" in begin

active = between(
    GAME,
    "static qboolean ChainHook_ActiveFrame",
    "void SG_HookLiveEndFrame",
)
assert "SG_ChainHookReplayPreStep" in active
assert "SG_ChainHookReplayPostStep" in active
assert "SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE" in active
release = active.index("ctf_hook_abort(e);")
release_event = active.index("SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED")
assert release < release_event
assert "SG_CHAIN_HOOK_REPLAY_EFFECT_FIRE_NEXT" in active
ray = active.index("ChainHook_SecondRayOK(e, bot)")
fire = active.index("Cmd_Hook_f(e);", ray)
identity = active.index("e->client->hook == first", fire)
next_event = active.index("SG_CHAIN_HOOK_REPLAY_EVENT_NEXT_FIRED", identity)
assert ray < fire < identity < next_event
assert "SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS" in active

for mismatch in (
    "chain-owner",
    "chain-flight",
    "chain-attach-checkpoint",
    "chain-bite",
    "chain-pull-missed",
    "chain-second-reproof",
    "chain-second-bolt",
):
    assert f'"{mismatch}"' in active
assert active.count("SG_HookGameFailDetail") >= 7

endframe = between(
    GAME,
    "void SG_HookLiveEndFrame",
    "qboolean SG_HookGameBeginAfterFire",
)
assert "bot->chain_hook_active" in endframe
assert "SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED" in endframe

proof_call = MOVE.index("SG_ChainHookGameOnlineProof(e, bot)")
fire_start = MOVE.rindex("rune_t *rune = SG_Rune();", 0, proof_call)
fire_end = MOVE.index("else if (bot->hook_phase == 2)", proof_call)
fire_gate = MOVE[fire_start:fire_end]
assert "SG_ChainHookGameOnlineProof(e, bot)" in fire_gate
assert "SG_ChainHookGameBeginAfterFire(e, bot, bot->hook_link)" in fire_gate
assert fire_gate.index("SG_ChainHookGameOnlineProof") < fire_gate.index(
    "Cmd_Hook_f(e);", fire_gate.index("SG_ChainHookGameOnlineProof")
)

human = between(WEAPON, "void Weapon_Hook_Fire (edict_t *ent)\n{",
                "void Weapon_Hook (edict_t *ent)")
assert human.index("if (!SG_OwnsBot(ent))") < human.index(
    "LMCTF_HumanHookFire(ent);"
)
assert "SG_ChainHook" not in human

print("chain_hook_game_integration_contract: ok")
