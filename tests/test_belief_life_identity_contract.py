#!/usr/bin/env python3
"""Keep the host player-life mint aligned with the belief contract width."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    local = (ROOT / "g_local.h").read_text()
    client = (ROOT / "p_client.c").read_text()

    assert re.search(r"\buint64_t\s+ctfid\s*;", local)
    assert re.search(
        r"\bstatic\s+uint64_t\s+unique_id\s*=\s*UINT64_C\(6\)\s*;",
        client,
    )
    assignment = client.index("client->ctf.ctfid = unique_id++;")
    exhaustion = re.search(
        r"if\s*\(\s*unique_id\s*==\s*UINT64_MAX\s*\)\s*\{\s*"
        r"gi\.error\(\"PutClientInServer: player life identity exhausted\"\);"
        r"\s*return;\s*\}",
        client,
    )
    assert exhaustion is not None
    assert exhaustion.start() < assignment


if __name__ == "__main__":
    main()
