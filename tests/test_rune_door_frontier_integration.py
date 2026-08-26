#!/usr/bin/env python3
"""Regression coverage for exhaustive Link_Doors DROP candidate storage."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "slipgate" / "sg_rune.c").read_text()


def link_doors_body():
    start = SOURCE.index("static void Link_Doors(door_topology_t *topology)")
    end = SOURCE.index("typedef struct compound_drop_plan_context_s", start)
    return SOURCE[start:end]


def test_drop_dest_capacity_covers_both_producers():
    body = link_doors_body()
    compact = "".join(body.split())

    assert "size_t drop_dest_capacity;" in body
    assert "drop_dest_capacity=(size_t)gen_num_seeds+(size_t)gen_num_links;" in compact
    assert ("SG_RuneDoorRankListInit(&drop_dests[ci],"
            "drop_dest_capacity,sg_host.level_alloc)") in compact
    assert ("SG_RuneDoorRankListInit(&drop_dests[ci],"
            "(size_t)gen_num_seeds,sg_host.level_alloc)") not in compact


def main():
    test_drop_dest_capacity_covers_both_producers()
    print("test_rune_door_frontier_integration: ok")


if __name__ == "__main__":
    main()
