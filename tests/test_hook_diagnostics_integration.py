"""Source-level wiring checks for the diagnostic-only hook pairing seam."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MOVE = (ROOT / "slipgate" / "sg_move.c").read_text()
ARACH = (ROOT / "slipgate" / "sg_arach.c").read_text()
CLIENT = (ROOT / "slipgate" / "sg_client.c").read_text()
BOT = (ROOT / "slipgate" / "sg_bot.h").read_text()
WRITER = (ROOT / "slipgate" / "sg_hook_diagnostics.c").read_text()

assert "sg_hook_diagnostic_state_t hook_diagnostics" in BOT
assert '"HOOKFIRE id=%s bot=%s kind=%s link=%d role=%d map=%s anchor_q8=%d,%d,%d\\n"' in WRITER
assert '"HOOKEND id=%s bot=%s kind=%s link=%d role=%d map=%s anchor_q8=%d,%d,%d reason=%s detail=%s\\n"' in WRITER
assert "if (!state || !emit)" in WRITER
assert 'SG_HookDiagnosticsFinish(state, "superseded", "reentrant-fire")' in WRITER
assert "if (!state || state->open || instance_token" not in WRITER
assert "sequence_epoch" in WRITER and "UINT64_MAX" in WRITER

fire = MOVE[MOVE.index("Cmd_Hook_f(e);"):MOVE.index("else if (bot->hook_phase == 2)", MOVE.index("Cmd_Hook_f(e);"))]
assert fire.index("Hook_DiagnosticBegin(bot, role);") < fire.index("Hook_LiveBeginAfterFire")
assert 'Hook_GraphFailDetail(e, bot, 15.0f, "begin-failed")' in fire
assert "Hook_DiagnosticAnchorQ8" in MOVE
assert "bot->instance_token,\n\t    Hook_DiagnosticEmit, NULL" in MOVE

arrived = MOVE[MOVE.index("static qboolean Hook_LiveRetireNonRunning"):MOVE.index("static qboolean Hook_LiveWaitAttachFrame")]
assert 'SG_HookDiagnosticsFinish(&bot->hook_diagnostics,\n\t\t    "arrived", "reducer")' in arrived
graph_fail = MOVE[MOVE.index("static void Hook_GraphFailDetail"):MOVE.index("static void Hook_GraphFail(")]
assert graph_fail.index("SG_HookDiagnosticsFinish") < graph_fail.index("ctf_hook_abort")
assert "SG_ReplayReasonName(result->replay_reason)" in MOVE
legacy_noattach = MOVE[MOVE.index("qboolean completed = attached"):MOVE.index("bot->hook_phase = completed ? 3 : 0")]
assert legacy_noattach.index("SG_HookDiagnosticsFinish") < legacy_noattach.index("ctf_hook_abort")

for reason in ("apex", "landed", "landing_timeout", "burst", "burststall", "noattach"):
    assert f'"{reason}"' in MOVE
for reason in ("death", "physics-incompatible", "declared-door-interrupt", "stale-host-rope", "map-transition"):
    assert f'"{reason}"' in ARACH
assert '"slot-retirement", "lifecycle"' in CLIENT

level = ARACH[ARACH.index("void SG_LevelChange(void)"):]
assert level.index('"map-transition", "level-change"') < level.index("SG_RemoveBots();")
assert "for (i = 0; i < SG_MAXBOTS; i++)" in level

# Reducer release remains deliberately nonterminal; all raw schema lives in one writer.
release = MOVE[MOVE.index("SG_HookLiveReleaseApplied"):MOVE.index("void SG_HookLiveEndFrame")]
assert "SG_HookDiagnosticsFinish" not in release
for path in (ROOT / "slipgate").glob("*.c"):
    if path.name == "sg_hook_diagnostics.c":
        continue
    source = path.read_text()
    assert "HOOKFIRE" not in source
    assert "HOOKEND" not in source
