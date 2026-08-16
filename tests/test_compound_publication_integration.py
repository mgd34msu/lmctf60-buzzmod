#!/usr/bin/env python3
"""Pin the dormant D_SWIM loader/publication transaction boundaries."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def between(text: str, start: str, end: str) -> str:
    start_at = text.index(start)
    end_at = text.index(end, start_at)
    return text[start_at:end_at]


def test_loader_replay_stays_inside_proof_scope_and_precedes_doors() -> None:
    arach = (ROOT / "slipgate/sg_arach.c").read_text(encoding="utf-8")
    loader = between(arach, "rune_t *Rune_Load", "int Rune_NearestSeed")
    decoded = loader.index("SG_RuneV3Load(")
    scope = loader.index("SG_RuneProofScopeBegin(")
    compound = loader.index("SG_CompoundPublicationBuild(")
    ordinary = loader.index("Rune_ReplayOrdinaryDoors(")
    scope_end = loader.index("SG_RuneProofScopeEnd();", compound)
    assert decoded < scope < compound < ordinary < scope_end


def test_final_world_recheck_is_adjacent_to_the_only_publication() -> None:
    arach = (ROOT / "slipgate/sg_arach.c").read_text(encoding="utf-8")
    setup = between(arach, "qboolean SG_LevelSetup", "/* ----------------------------------------------------------------- body */")
    assert arach.count("sg_rune = candidate;") == 1
    authority = setup.index("SG_RuneV3AuthorityCapture(")
    revalidate = setup.index("SG_CompoundPublicationRevalidate(candidate)")
    publish = setup.index("sg_rune = candidate;")
    assert authority < revalidate < publish
    boundary = setup[revalidate:publish]
    assert "Danger_Publish(" not in boundary
    assert "Fields_Setup(" not in boundary
    assert "Sidecar_LoadCandidate(" not in boundary


def test_compound_admission_order_and_inert_runtime_metadata() -> None:
    publication = (
        ROOT / "slipgate/sg_compound_publication.c"
    ).read_text(encoding="utf-8")
    build = publication[publication.index(
        "sg_compound_publication_result_t SG_CompoundPublicationBuild"
    ):]
    enumerate = build.index("SG_CompoundWorldEnumeratePreopen(")
    resolve = build.index("SG_CompoundWorldResolvePreopen(")
    prepare = build.index("SG_OracleCompoundSwimPrepareSource(")
    discover = build.index("SG_OracleCompoundSwimDiscoverContact(")
    replay = build.index("SG_OracleCompoundSwimPreopen(")
    assert enumerate < resolve < prepare < discover < replay
    assert "SG_CompoundWorldHoldOpen" not in publication

    compound = (ROOT / "slipgate/sg_compound.h").read_text(encoding="utf-8")
    actions = (
        ROOT / "slipgate/sg_action_contract.generated.h"
    ).read_text(encoding="utf-8")
    assert "#define SG_COMPOUND_LIVE_CONTROLLER_REVISION 0" in compound
    assert "X(RL_DOOR_SWIM, 10, 0," in actions


if __name__ == "__main__":
    test_loader_replay_stays_inside_proof_scope_and_precedes_doors()
    test_final_world_recheck_is_adjacent_to_the_only_publication()
    test_compound_admission_order_and_inert_runtime_metadata()
    print("test_compound_publication_integration: ok")
