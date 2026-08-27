#!/usr/bin/env python3
"""Hook discovery searches a 3D BSP ray volume at every proof gravity."""
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] /
          "slipgate" / "sg_rune_hook_frontier.c").read_text()


def between(start: str, end: str) -> str:
    begin = SOURCE.index(start)
    return SOURCE[begin:SOURCE.index(end, begin)]


volume = between("static qboolean RuneHook_ProveSurfaceVolume",
                 "static qboolean RuneHook_ProveOrdinary")
ordinary = between("static qboolean RuneHook_ProveOrdinary",
                   "static qboolean RuneHook_ChainEligible")
frontier = SOURCE[SOURCE.index("qboolean SG_RuneGenerateHookFrontier"):]

assert "RuneHook_ProveRay" in volume
assert "SG_RuneProofGravity() > 200 &&" in volume
assert "state->input->component[from] == state->input->component[to]" in volume
assert "SURF_SKY" in between("static qboolean RuneHook_ProveRay", volume)
assert "SG_OracleHookTraverse" in between(
    "static qboolean RuneHook_ProveRay", volume)
assert "RuneHook_ProveSurfaceVolume(state, from, to" in ordinary
assert ordinary.index("RuneHook_ProveSurfaceVolume") > ordinary.index(
    "SG_OracleHookTraverse")
assert "state.retention_limit = RUNE_MAX_LINKS - input->seed_count" in frontier
assert "RuneHook_SurfaceRay" in frontier
assert "RuneHook_NearestSurfaceSeeds" in frontier
assert "RuneHook_ProveSurfaceRay" in frontier
assert "source_crouched" in frontier
assert "hook volume progress=%u%%" in frontier
assert "SG_RuneProofSelectHookFrontier" not in frontier
assert "state->capacity_skips++" in SOURCE
assert "*input->link_overflow = true" not in between(
    "static rune_link_t *RuneHook_AddLink", "static void RuneHook_SetEnvelope")

print("hook_surface_volume_integration_contract: ok")
