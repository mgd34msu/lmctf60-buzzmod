#!/usr/bin/env python3
"""Pin mechanism rebinds ahead of every RUNE publication boundary."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def test_all_mechanisms_rebind_before_loader_acceptance() -> None:
    arach = (ROOT / "slipgate/sg_arach.c").read_text(encoding="utf-8")
    loader = between(arach, "rune_t *Rune_Load", "int Rune_NearestSeed")
    decoded = loader.index("SG_RuneFileLoad(")
    catalog = loader.index("SG_MechCatalogMatches(")
    indexes = loader.index("Rune_BuildOutboundIndexes(")
    rebind = loader.index("SG_RuneMechanismBindingsReady(")
    proof = loader.index("SG_RuneProofScopeBegin(")
    accepted = loader.index("accepted = true;")
    cleanup = loader.index("if (!accepted)")
    release = loader.index("Rune_Free(rune);", cleanup)
    assert decoded < catalog < indexes < rebind < proof < accepted < cleanup < release


def test_final_rebind_fails_before_the_only_global_publication() -> None:
    arach = (ROOT / "slipgate/sg_arach.c").read_text(encoding="utf-8")
    setup = between(arach, "qboolean SG_LevelSetup", "/* ----------------------------------------------------------------- body */")
    compound = setup.index("SG_CompoundPublicationRevalidate(candidate)")
    rebind = setup.index("SG_RuneMechanismBindingsReady(candidate")
    publish = setup.index("sg_rune = candidate;")
    failure = setup[rebind:publish]
    assert compound < rebind < publish
    assert "goto fail;" in failure
    assert "sg_rune =" not in failure
    assert arach.count("sg_rune = candidate;") == 1


if __name__ == "__main__":
    test_all_mechanisms_rebind_before_loader_acceptance()
    test_final_rebind_fails_before_the_only_global_publication()
    print("test_mechanism_publication_integration: ok")
