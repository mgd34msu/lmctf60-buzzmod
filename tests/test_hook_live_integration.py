#!/usr/bin/env python3
"""Private integration contract checks for ordinary RL_HOOK.

The engine-only portions of sg_move.c cannot be linked into the host-free
adapter test.  These checks pin the boundary facts that must remain true when
the adapter is connected to the live controller: independent legacy command
ownership, settlement-only contact sampling, legacy gates, tail cadence, and
the three shelf classes.
"""
from pathlib import Path


SOURCE = Path(__file__).resolve().parents[1] / "slipgate" / "sg_move.c"
text = SOURCE.read_text(encoding="utf-8")
P_VIEW = (Path(__file__).resolve().parents[1] / "p_view.c").read_text(
    encoding="utf-8"
)
ADAPTER = (Path(__file__).resolve().parents[1] / "slipgate" / "sg_hook_live.c").read_text(
    encoding="utf-8"
)
REPLAY = (Path(__file__).resolve().parents[1] / "slipgate" / "sg_replay.c").read_text(
    encoding="utf-8"
)
BOT = (Path(__file__).resolve().parents[1] / "slipgate" / "sg_bot.h").read_text(
    encoding="utf-8"
)
CLIENT = (Path(__file__).resolve().parents[1] / "slipgate" / "sg_client.c").read_text(
    encoding="utf-8"
)
ARACH = (Path(__file__).resolve().parents[1] / "slipgate" / "sg_arach.c").read_text(
    encoding="utf-8"
)
ADAPTER_TEST = (Path(__file__).resolve().parents[1] / "tests" /
                "sg_hook_live_test.c").read_text(encoding="utf-8")
DISCIPLINE = (Path(__file__).resolve().parents[1] / "slipgate" /
              "sg_hook_discipline.h").read_text(encoding="utf-8")


def body(start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


legacy = body("static qboolean Hook_LiveLegacyCommand", "static void Hook_LiveResultLog")
assert "hook_legacy_command_bot" in legacy
assert "bot->hook_legacy_settle" in legacy
assert "bot->hook_legacy_arrived" in legacy
assert "bot->hook_dest" in legacy
assert "bot->hook_view" in legacy
assert "state->" not in legacy
assert "SG_HookReplaySettled" not in legacy
assert "bot->hook_replay" not in legacy

observation = body("static void Hook_LiveObservation", "static const sg_bot_t *hook_legacy_command_bot")
assert "sample_settlement_contact && bot->hook_legacy_settle" in observation
assert "observation->contact_clear = Hook_SettleArrived(e, bot);" in observation
assert observation.count("Hook_SettleArrived(e, bot)") == 1

shelves = body("static float Hook_LiveShelfSeconds", "static void Hook_LiveSync")
assert "SG_HOOK_REPLAY_SETTLE" in shelves and "return 60.0f;" in shelves
assert "SG_HOOK_REPLAY_WAIT_PULL" in shelves
assert "SG_HOOK_REPLAY_PULL_FRAME" in shelves
assert "return 30.0f;" in shelves
assert "SG_REPLAY_REASON_HAZARDOUS_LIQUID ? 30.0f : 15.0f" in shelves

active = body("static qboolean Hook_LiveActiveFrame", "void SG_HookLiveEndFrame")
assert "Hook_SourceStateOK(e, bot)" in active
assert "Hook_LiveWitnessOK(e, bot)" in active
assert "Hook_AttachmentMaintained(e, bot)" in active
assert "Hook_LiveTailCommand(bot, frame_settlement" in active
assert "Hook_LiveTailAdvance(bot, frame_pull_clock);" in active
assert "if (failed)" in active and "ClientThink(e, &command);" in active
assert "for (step = 0; step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS; step++)" in active
assert active.count("Hook_LiveTailCommand(bot, frame_settlement") >= 3
assert "hook_legacy_command_bot = bot;" in active
assert "hook_legacy_command_bot = NULL;" in active
assert "release_seen = true;" in active
assert "bot->hook_legacy_settle = true;" in active
assert "post_arrival_contact = observation.contact_clear;" in active
assert "if (frame_settlement && post_arrival_contact)" in active
assert "(bot->hook_legacy_arrived || post_arrival_contact)" in active
assert "SG_HookLivePreStep" in active
assert "SG_HookLiveValidateStoredFinalCommand" in active
assert "&bot->hook_final_guard" in active
assert "usercmd_t expected" not in active
assert "expected = command" not in active
active_guard = active.index("SG_HookLiveValidateStoredFinalCommand")
assert active_guard < active.index("ClientThink(e, &command);", active_guard)
preflight = active[:active.index("if (replay_phase == SG_HOOK_REPLAY_FLIGHT)")]
assert "Hook_GraphFail(e, bot, 30.0f);" in preflight
identity = active[:active.index("if (replay_phase == SG_HOOK_REPLAY_FLIGHT)")]
assert identity.index("Hook_GraphFail(e, bot, 30.0f);") < identity.index(
    "if (!Hook_LiveIdentityCurrent(e, bot))"
)
assert "replay_phase == SG_HOOK_REPLAY_PULL_FRAME" in identity
assert "Hook_GraphFail(e, bot, 15.0f);" in identity

retire = body("static qboolean Hook_LiveRetireNonRunning", "static qboolean Hook_LiveWaitAttachFrame")
assert "Hook_LiveSync(bot);" in retire

wait_attach = body("static qboolean Hook_LiveWaitAttachFrame", "static qboolean Hook_LiveActiveFrame")
assert "entry_pms = e->client->ps.pmove;" in wait_attach
assert "entry_pose.pms = entry_pms;" in wait_attach
assert "SG_HookLiveWaitAttachStep" in wait_attach
assert "SG_HookLiveValidateStoredFinalCommand" in wait_attach
assert "SG_HookLiveValidateFinalCommand" not in wait_attach
assert "&bot->hook_final_guard" in wait_attach
assert "usercmd_t expected" not in wait_attach
assert "expected = command" not in wait_attach
wait_guard = wait_attach.index("SG_HookLiveValidateStoredFinalCommand")
assert wait_guard < wait_attach.index("ClientThink(e, &command);", wait_guard)
assert "SG_HookLivePostStep" not in wait_attach
assert "Hook_LiveTailCommand(bot, false, &entry_pms" in wait_attach
assert "for (step = 0; step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS; step++)" in wait_attach
assert "if (!failed && e->waterlevel > 0" in wait_attach
assert "Hook_GraphFail(e, bot, 30.0f);" in wait_attach
assert "Hook_AttachmentOK" not in wait_attach

late_wait = "if (bot->hook_replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH &&"
attach_gate = "if (bot->hook_replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH)"
late_begin = active.index(late_wait)
attach_begin = active.index(attach_gate, late_begin + len(late_wait))
late_branch = active[late_begin:attach_begin]
attach_branch = active[attach_begin:]
assert "e->client->hookstate == 1 && e->client->hook" in late_branch
assert "Hook_SourceStateOK(e, bot)" in late_branch
assert "Hook_LiveWitnessOK(e, bot)" in late_branch
assert "SG_TimerReadyStrict(bot->hook_deadline)" in late_branch
assert "Hook_GraphFail(e, bot, 15.0f);" in late_branch
assert "return Hook_LiveWaitAttachFrame(bot, e, link_index);" in late_branch
assert "if (!Hook_AttachmentOK(e, bot))" in attach_branch
assert "Hook_GraphFail(e, bot, 15.0f);" in attach_branch
assert active.count("SG_HookLiveAttached(") == 1

bridge = ADAPTER[ADAPTER.index("sg_hook_live_result_t SG_HookLiveWaitAttachStep"):ADAPTER.index("sg_hook_live_result_t SG_HookLiveValidateFinalCommand")]
assert "replay->phase != SG_HOOK_REPLAY_WAIT_ATTACH" in bridge
assert "replay->progress.step_pending" in bridge
assert "SG_HookReplayFixedViewCommand" in bridge
assert "HookLiveCommandEqual(&shadow, &legacy)" in bridge
assert "SG_HookReplayPreStep" not in bridge
assert "SG_HookReplayPostStep" not in bridge
assert "SG_HookLiveDeactivate" not in bridge
assert "SG_HookLiveCommandGuardClear(guard);" in bridge
assert "HookLiveCommandGuardStore(guard, action_link, &shadow);" in bridge
assert "qboolean SG_HookReplayFixedViewCommand" in REPLAY

prestep_adapter = ADAPTER[
    ADAPTER.index("sg_hook_live_result_t SG_HookLivePreStep"):
    ADAPTER.index("sg_hook_live_result_t SG_HookLiveWaitAttachStep")
]
assert "SG_HookLiveCommandGuardClear(guard);" in prestep_adapter
assert "HookLiveCommandGuardStore(guard, action_link, &reducer_command);" in prestep_adapter

reset_adapter = ADAPTER[
    ADAPTER.index("void SG_HookLiveReset"):
    ADAPTER.index("void SG_HookLiveDeactivate")
]
assert "sg_hook_live_command_guard_t *guard" in reset_adapter
assert "SG_HookLiveCommandGuardClear(guard);" in reset_adapter

begin_adapter = ADAPTER[
    ADAPTER.index("sg_hook_live_result_t SG_HookLiveBegin"):
    ADAPTER.index("sg_hook_live_result_t SG_HookLivePreStep")
]
assert "sg_hook_live_command_guard_t *guard" in begin_adapter
assert "SG_HookLiveCommandGuardClear(guard);" in begin_adapter
assert "!guard || !HookLiveOwnerValid" in begin_adapter

stored_final = ADAPTER[
    ADAPTER.index("sg_hook_live_result_t SG_HookLiveValidateStoredFinalCommand"):
    ADAPTER.index("sg_hook_live_result_t SG_HookLivePostStep")
]
assert "!guard || !guard->pending || guard->action_link != action_link" in stored_final
assert "&guard->expected" in stored_final
assert stored_final.index("SG_HookLiveValidateFinalCommand") < stored_final.rindex(
    "SG_HookLiveCommandGuardClear(guard);"
)

assert "sg_hook_live_command_guard_t hook_final_guard;" in BOT
assert CLIENT.count("&bot->hook_final_guard);") == 1
assert ARACH.count("&bot->hook_final_guard);") == 2
assert "SG_HookLiveCommandGuardClear(&bot->hook_final_guard);" not in CLIENT
assert "SG_HookLiveCommandGuardClear(&bot->hook_final_guard);" not in ARACH
assert text.count("Hook_LiveClearFinalGuard(bot);") == 4
assert ADAPTER_TEST.count("command.buttons = BUTTON_USE;") >= 2
assert ADAPTER_TEST.count("SG_HookLiveValidateStoredFinalCommand") >= 5
assert "Reset itself must discard an unconsumed bridge approval" in ADAPTER_TEST
assert "Begin is also a lifecycle boundary" in ADAPTER_TEST

endframe = body("void SG_HookLiveEndFrame", "static qboolean Hook_LiveBeginAfterFire")
assert "SG_HookLivePullApplied" in endframe
assert "Hook_LiveSync(bot)" in endframe
assert "bot->hook_replay.phase != SG_HOOK_REPLAY_WAIT_PULL" in endframe

pull = P_VIEW.index("Weapon_Hook_Fire(ent);")
observe = P_VIEW.index("SG_HookLiveEndFrame(ent);", pull)
assert pull < observe

# Rope discipline is a graph-hook admission/retirement seam, not a replay or
# optional speedhook rewrite. It decides at the exact proved source before
# control decode, and malformed control plus a graph aim timeout share the
# same bounded retirement path.
hook_stage = body("if (l->action == RL_HOOK", "hook_stage_done: ;")
assert "SG_HookStageSourceCompatible" in hook_stage
source_reject = hook_stage.index("if (!SG_HookStageSourceCompatible")
source_abort = hook_stage.index("ballistic_abort = true;", source_reject)
source_release = hook_stage.index("bot->commit_link = -1;", source_reject)
assert source_reject < source_release < source_abort
worth = hook_stage.index("SG_HookExpectedRideWorth")
decode = hook_stage.index("SG_HookControlDecode")
assert worth < decode
assert "!SG_HookRideLaunchAllowed(worth)" in hook_stage
assert '"value-unassessed"' in hook_stage
assert '"value-skip"' in hook_stage
assert 'Hook_DisciplineRetire(e, bot, bestlink, 5.0f, false,' in hook_stage
assert '"decode-retire"' in hook_stage
assert hook_stage.index('"decode-retire"') > decode
assert "SG_HOOK_RIDE_UNASSESSED" in DISCIPLINE
assert "from_goal > to_goal + SG_HOOK_DISCIPLINE_SERVED_FIELD_MS" in DISCIPLINE

discipline_retire = body("static void Hook_DisciplineRetire",
                         "qboolean SG_HookOffhandReady")
assert "Hook_ShelveLink(bot, link_index, shelf_seconds);" in discipline_retire
assert "bot->commit_link = -1;" in discipline_retire
assert "bot->hook_phase = 0;" in discipline_retire
assert "bot->hook_link = -1;" in discipline_retire
assert "SG_Rune()->links[link_index].action != RL_HOOK" in discipline_retire
assert "if (failure)" in discipline_retire
assert "SG_HookFailureStreakAdvance" in discipline_retire
assert "SG_TimerArm(&bot->hookban_until" in discipline_retire
assert "HOOKDISC" in discipline_retire

graph_fail = body("static void Hook_GraphFail", "static void Hook_DisciplineRetire")
assert "SG_HookFailureStreakAdvance" not in graph_fail

aim_start = text.index("else if (bot->hook_phase == 1 && SG_TimerReadyStrict(")
aim_end = text.index("else if (bot->hook_phase == 1)", aim_start + 1)
aim = text[aim_start:aim_end]
assert 'Hook_DisciplineRetire(e, bot, failed_link, 5.0f, true,' in aim
assert '"aim-retire"' in aim
assert "bot->hookfail_streak = 0;" not in aim
assert "if (failed_speedhook)" in aim
speedhook_timeout = aim[aim.index("if (failed_speedhook)"):aim.index("else", aim.index("if (failed_speedhook)"))]
assert "Hook_DisciplineRetire" not in speedhook_timeout

fire_end = text.index("else if (bot->hook_phase == 2)", aim_end)
fire = text[aim_end:fire_end]
fire_worth = fire.index("SG_HookExpectedRideWorth")
fire_retire = fire.index('"value-fire-skip"')
fire_proof = fire.index("Hook_OnlineProof")
fire_command = fire.index("Cmd_Hook_f(e);")
assert fire_worth < fire_retire < fire_proof < fire_command
assert "route_field[hook_link->from]" in fire
assert "route_field[hook_link->to]" in fire
assert "goal_field[hook_link->from]" not in fire
assert "goal_field[hook_link->to]" not in fire
assert "!SG_HookRideLaunchAllowed(worth)" in fire
assert '"value-fire-unassessed"' in fire
assert "if (!route_field ||" in fire

# Cut-rope and rocket-jump landing lookahead must follow the route that owns
# the movement commitment.  Falling back to the strategic goal field is valid
# only when no tactical route field exists; otherwise touchdown immediately
# contradicts the view direction chosen in flight.
preturn = body("THE PRE-TURN (sg_preturn", "if (bot->hook_phase == 3)")
assert "const int *preturn_route_field = route_field" in preturn
assert "? route_field : goal_field;" in preturn
assert "preturn_route_field[candidate->to]" in preturn

# A completed graph ride is judged by the same active route field that
# authorized its irreversible fire.  The strategic goal field may describe a
# different coordinator mission and must not shelf or ban a route-serving ride.
landing_value = body("A ride that did not SERVE the field failed",
                     "bot->hook_link = -1;")
assert "route_field[bot->seed]" in landing_value
assert "route_field[hl->to]" in landing_value
assert "goal_field[bot->seed]" not in landing_value
assert "goal_field[hl->to]" not in landing_value
assert 'Hook_DisciplineRetire(e, bot, link_index, 5.0f, false,' in fire
assert "goto hook_wait;" in fire[fire_retire:fire_proof]
assert "Hook_LiveBeginAfterFire" in fire

print("hook_live_integration_contract: ok")
