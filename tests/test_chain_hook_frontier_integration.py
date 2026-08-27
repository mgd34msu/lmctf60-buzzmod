#!/usr/bin/env python3
"""Two-rope transitions enter through trace evidence and exact reproof."""
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] /
          "slipgate" / "sg_rune_hook_frontier.c").read_text()


def between(start: str, end: str) -> str:
    begin = SOURCE.index(start)
    return SOURCE[begin:SOURCE.index(end, begin)]


nomination = between("static qboolean RuneHook_ProveChainNomination",
                     "qboolean SG_RuneProveHookNomination")
entry = between("qboolean SG_RuneProveHookNomination",
                "qboolean SG_RuneReproveHookControl")
generation = SOURCE[SOURCE.index("qboolean SG_RuneGenerateHookFrontier"):]

assert "RuneHook_ChainEligible(&state, from, to)" in nomination
assert "SG_OracleChainHookDiscover" in nomination
assert "rope_count == 1U" in entry
assert "RuneHook_ProveChainNomination" in entry
assert "SG_RuneProofSelectHookFrontier" not in generation
assert "RuneHook_ProveChainNomination" not in generation
assert "RUNE_CHAIN_HOOK_PAIR_LIMIT" not in SOURCE
assert "RUNE_CHAIN_HOOK_REPLAY_LIMIT" not in SOURCE
assert "SG_CHAIN_HOOK_ROPE_COUNT" in SOURCE
assert "SG_CHAIN_HOOK_ROPE_COUNT == 2" in SOURCE

print("chain_hook_frontier_integration_contract: ok")
