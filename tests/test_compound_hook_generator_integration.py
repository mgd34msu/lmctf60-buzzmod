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
    hook = between(
        SOURCE,
        "static void Link_CompoundHooks(void)",
        "static void Prove_RocketJumps(void)",
    )
    compact_hook = " ".join(hook.split())
    assert "COMPOUND_WORLD_MAX 64" in hook
    assert "COMPOUND_SOURCE_FAN 24" in hook
    assert "COMPOUND_HOOK_FAN 24" in hook
    assert "COMPOUND_HOOK_PRODUCTION 0" in hook
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
    assert "Compound_HookPublish" not in hook
    assert "gen_links[gen_num_links++]" not in hook
    assert "X(RL_DOOR_HOOK, 11, 0," in CONTRACT

    phase = between(
        SOURCE,
        'Rune_TelemetryPhaseStart("compound-links")',
        "Rune_TelemetryPhaseEnd();",
    )
    assert phase.index("Link_CompoundDrops();") < phase.index(
        "Link_CompoundHooks();"
    )


if __name__ == "__main__":
    main()
    print("test_compound_hook_generator_integration: ok")
