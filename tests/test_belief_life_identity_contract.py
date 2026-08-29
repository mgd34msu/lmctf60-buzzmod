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
    assert client.index("static uint64_t unique_id") < client.index(
        "client->ctf.ctfid = unique_id++;"
    )


if __name__ == "__main__":
    main()
