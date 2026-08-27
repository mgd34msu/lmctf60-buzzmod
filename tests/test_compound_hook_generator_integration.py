#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "slipgate" / "sg_rune.c").read_text(encoding="utf-8")
CONTRACT = (ROOT / "slipgate/sg_action_contract.generated.h").read_text(
    encoding="utf-8"
)


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def main() -> None:
    pose_operators = between(
        SOURCE,
        "static qboolean Prove_PoseFieldOperators(",
        "#ifndef SG_RUNE_TOPOLOGY_RUN_PROVER",
    )
    assert "Prove_HookFrontier()" in pose_operators
    assert "for (i = 0; i < gen_num_seeds" not in pose_operators
    assert "HOOK_PAIR_REACH * HOOK_PAIR_REACH" not in pose_operators

    publisher = between(
        SOURCE,
        "static int Compound_HookPublish",
        "static void Link_CompoundDrops(void)",
    )
    hook = between(
        SOURCE,
        "static void Link_CompoundHooks(void)",
        "static void Prove_RocketJumps(void)",
    )
    compact_hook = " ".join(hook.split())
    assert "COMPOUND_WORLD_MAX" not in hook
    assert "COMPOUND_SOURCE_FAN" not in hook
    assert "COMPOUND_HOOK_FAN" not in hook
    assert "SG_CompoundWorldEnumeratePreopen(NULL, 0," in hook
    assert "for (source = 0; source < gen_num_seeds; source++)" in hook
    assert "sg_host.level_alloc(sizeof(*mechanisms)" in hook
    assert "COMPOUND_HOOK_PRODUCTION 1" in hook
    assert "SG_OracleCompoundSwimPrepareSource(" in hook
    assert "SG_OracleCompoundSwimDiscoverContact(" in hook
    assert "SG_OracleCompoundHookPreopen(" in hook
    assert "suffix->action != RL_HOOK" in hook
    assert "request.action = RL_DOOR_HOOK" in hook
    assert "request.output_capacity = 4" in hook
    assert (
        "request.production_enabled = COMPOUND_HOOK_PRODUCTION;"
        in compact_hook
    )
    assert "sg_phantom_t phantom = prepared.phantom;" in hook
    assert "&exact, NULL, true, false" in compact_hook
    assert "Link_Add(" not in hook
    assert "planner_published += Compound_HookPublish(output," in hook
    assert "gen_links[gen_num_links++]" not in hook
    assert "published=%d" in hook
    assert "X(RL_DOOR_HOOK, 11, 1," in CONTRACT

    planner_ok = hook.index("if (result.status == SG_COMPOUND_ACTION_GEN_OK)")
    emitted = hook.index("planner_emitted += (int)result.emitted;", planner_ok)
    published = hook.index(
        "planner_published += Compound_HookPublish(output,", emitted
    )
    assert planner_ok < emitted < published

    assert "candidate->action != RL_DOOR_HOOK" in publisher
    assert "SG_CompoundValidateLink(" in publisher
    assert "gen_links[link_index].action == RL_DOOR_HOOK" in publisher
    assert "gen_links[existing].cost_ms <= candidate->cost_ms" in publisher
    assert "gen_links[existing] = *candidate;" in publisher
    assert "gen_link_overflow = true;" in publisher
    assert "gen_links[gen_num_links++] = *candidate;" in publisher
    assert "RL_DOOR_DROP" not in publisher

    phase = between(
        SOURCE,
        'Rune_TelemetryPhaseStart("compound-links")',
        "Rune_TelemetryPhaseEnd();",
    )
    assert phase.index("Link_CompoundDrops();") < phase.index(
        "Link_CompoundHooks();"
    )

    rocket = between(
        SOURCE,
        "static void Prove_RocketJumps(void)",
        "static qboolean Prove_HookFrontier(void)",
    )
    assert "rune: rocket jumps omitted; hook owns LMCTF traversal" in rocket
    assert rocket.index("return;") < rocket.index("SG_OracleRocketJumpCeiling()")


if __name__ == "__main__":
    main()
    print("test_compound_hook_generator_integration: ok")
