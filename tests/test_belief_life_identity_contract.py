#!/usr/bin/env python3
"""Keep the host player-life mint aligned with the belief contract width."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    local = (ROOT / "g_local.h").read_text()
    client = (ROOT / "p_client.c").read_text()
    save = (ROOT / "g_save.c").read_text()
    strike = (ROOT / "slipgate/sg_strike.h").read_text()
    arach = (ROOT / "slipgate/sg_arach.c").read_text()
    observer = (ROOT / "p_observer.c").read_text()
    commands = (ROOT / "g_cmds.c").read_text()
    menu = (ROOT / "g_menu.c").read_text()
    combat = (ROOT / "slipgate/sg_combat.c").read_text()
    belief_runtime = (ROOT / "slipgate/sg_belief_runtime.c").read_text()
    belief_runtime_header = (ROOT / "slipgate/sg_belief_runtime.h").read_text()

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
    assert "uint64_t\tpovlock_target_ctfid;" in local
    assert "uint64_t\tpov_record_viewer_ctfid;" in local
    assert "uint64_t\tprojectile_owner_ctfid;" in local
    assert "uint64_t life_id;" in strike
    assert "uint64_t member_life[SG_STRIKE_MAX_SLOTS];" in strike
    assert "input->life_id = ent->client->ctf.ctfid;" in arach
    assert "uint64_t cur_val = 0, next_val = UINT64_MAX;" in observer
    assert '"Now observing: [%" PRIu64 "] %s"' in observer
    assert '" id: %" PRIu64 " %s frags: %d\\n"' in commands
    assert '"\\nctfkick %" PRIu64 "\\n"' in menu
    assert "GAME_SAVE_LAYOUT_VERSION __DATE__ \" ctfid64-v1\"" in save
    assert "strcpy(str, GAME_SAVE_LAYOUT_VERSION);" in save
    assert "strcmp(str, GAME_SAVE_LAYOUT_VERSION)" in save
    assert re.search(
        r"if\s*\(\s*strcmp\(str, GAME_SAVE_LAYOUT_VERSION\)\s*\)\s*\{\s*"
        r"fclose\(f\);\s*gi\.error\(\"Savegame from an incompatible client "
        r"layout\.\\n\"\);\s*return;\s*\}",
        save,
    )
    random_identity = combat[
        combat.index("static unsigned Combat_RandomIdentity"):
        combat.index("static int Combat_ClientIndex")
    ]
    client_random_seam = combat[
        combat.index("uint32_t SG_CombatAimTestClientRandom"):
        combat.index("#endif", combat.index("uint32_t SG_CombatAimTestClientRandom"))
    ]
    assert re.search(
        r"static unsigned Combat_RandomIdentity\(int client_index,\s*"
        r"uint64_t client_life\)",
        random_identity,
    )
    assert not re.search(r"\bunsigned\s+long\b", random_identity)
    assert "Combat_RandomIdentity(client_index, client_life)" in client_random_seam
    assert "SG_BeliefRuntimeRetireClient" not in belief_runtime
    assert "SG_BeliefRuntimeRetireClient" not in belief_runtime_header
    assert "BeliefRuntimeClientFenceRetire" not in belief_runtime
    for path in ROOT.glob("*.c"):
        assert not re.search(r"unsigned long\s+\w*ctfid", path.read_text())
    for path in (ROOT / "slipgate").glob("*.c"):
        assert not re.search(r"unsigned long\s+\w*ctfid", path.read_text())
    for path in (ROOT / "slipgate").glob("*.h"):
        assert not re.search(r"unsigned long\s+\w*ctfid", path.read_text())


if __name__ == "__main__":
    main()
