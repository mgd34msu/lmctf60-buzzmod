#!/usr/bin/env python3
"""Chain-hook search exhausts every eligible pair with two-hook transitions."""
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] /
          "slipgate" / "sg_rune_hook_frontier.c").read_text()


def between(start: str, end: str) -> str:
    begin = SOURCE.index(start)
    return SOURCE[begin:SOURCE.index(end, begin)]


publish = between("static qboolean RuneHook_PublishCandidate",
                  "static qboolean RuneHook_InputValid")
eligibility = publish.index("RuneHook_ChainEligible")
charge = publish.index("state->chain_pairs++")
prove = publish.index("RuneHook_ProveChain")
assert eligibility < charge < prove
assert "RUNE_CHAIN_HOOK_PAIR_LIMIT" not in SOURCE
assert "RUNE_CHAIN_HOOK_REPLAY_LIMIT" not in SOURCE
assert "SG_CHAIN_HOOK_ROPE_COUNT" in SOURCE
assert "SG_CHAIN_HOOK_ROPE_COUNT == 2" in SOURCE

chain = between("static qboolean RuneHook_ProveChain",
                "static qboolean RuneHook_PublishCandidate")
assert "RuneHook_ChainEligible(state, from, to)" in chain

print("chain_hook_frontier_integration_contract: ok")
