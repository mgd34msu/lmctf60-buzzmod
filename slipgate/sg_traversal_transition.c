#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_swim_live.h"
#include "slipgate/sg_traversal_transition.h"

qboolean SG_TraversalControllerPhysical(const sg_bot_t *bot, int action)
{
	sg_strike_weapon_controller_state_t state;

	if (!bot)
		return false;
	if (action == RL_DOOR_DROP && bot->compound_drop_live.guard_owned)
		return true;
	memset(&state, 0, sizeof(state));
	state.action = action;
	state.hook_phase = bot->hook_phase;
	state.rocketjump_phase = bot->rocketjump.phase;
	state.jump_started = bot->jump_started;
	state.drop_started = bot->drop_started;
	state.swim_active = bot->swim_replay_active;
	state.swim_validated = bot->swim_validated;
	state.declared_started = bot->declared_started;
	state.declared_touched = bot->declared_touched;
	state.declared_triggered = bot->declared_triggered;
	state.declared_activated = bot->declared_activated;
	state.declared_guard_paused = bot->declared_guard_paused;
	return SG_StrikeWeaponControllerPhysical(&state) ? true : false;
}

qboolean SG_DeclaredDoorRouteRequiresRelease(const sg_bot_t *bot, int action)
{
	return bot && (action == RL_DOOR || action == RL_BUTTON_DOOR) &&
	       bot->declared_started && !bot->declared_touched &&
	       !bot->declared_triggered && !bot->declared_activated;
}

sg_door_lease_retirement_t SG_DoorLeaseRetirement(
	int release_proved_clear, int recovery_expired, int hold_open_ready)
{
	if (release_proved_clear)
		return SG_DOOR_LEASE_RELEASE;
	if (recovery_expired || !hold_open_ready)
		return SG_DOOR_LEASE_TERMINAL;
	return SG_DOOR_LEASE_HOLD;
}

void SG_StagedTraversalCancel(sg_bot_t *bot, int action)
{
	if (!bot)
		return;
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
	bot->commit_route_goal = (sg_field_key_t){ 0 };
	bot->commit_retirement_pending = false;
	bot->sticky_link = -1;
	bot->latch_until = 0.0f;
	bot->rail_link = -1;
	bot->rail_stage = 0;
	bot->rail_until = 0.0f;
	if (bot->hook_phase == 1)
	{
		bot->hook_phase = 0;
		bot->hook_link = -1;
		bot->hook_deadline = 0.0f;
		bot->hook_bite_logged = false;
		bot->hook_attached_validated = false;
		bot->speedhook = false;
		bot->speedhook_pull_applied = false;
		bot->flow_release = false;
	}
	if (bot->rocketjump.phase == SG_ROCKETJUMP_EQUIP)
	{
		memset(&bot->rocketjump, 0, sizeof(bot->rocketjump));
	}
	switch (action)
	{
	case RL_JUMP:
		bot->jump_link = -1;
		bot->jump_started = false;
		break;
	case RL_DROP:
		bot->drop_link = -1;
		bot->drop_started = false;
		bot->drop_walkoff = false;
		bot->drop_airborne = false;
		bot->drop_recover = false;
		SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
		    &bot->drop_replay_link, &bot->drop_live_events);
		break;
	case RL_HOOK:
		bot->hook_phase = 0;
		bot->hook_link = -1;
		bot->hook_deadline = 0.0f;
		bot->hook_bite_logged = false;
		bot->hook_attached_validated = false;
		bot->speedhook = false;
		bot->speedhook_pull_applied = false;
		bot->flow_release = false;
		break;
	case RL_SWIM:
		SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
		    &bot->swim_replay_link, &bot->swim_validated,
		    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
		break;
	case RL_ROCKETJUMP:
		memset(&bot->rocketjump, 0, sizeof(bot->rocketjump));
		break;
	case RL_LIFT:
	case RL_TELEPORT:
	case RL_DOOR:
	case RL_BUTTON_DOOR:
		bot->declared_activated = false;
		bot->declared_started = false;
		bot->declared_start_frame = -1;
		bot->declared_touched = false;
		bot->declared_touch_frame = -1;
		SG_ButtonExecutionActionReset(bot);
		bot->declared_triggered = false;
		bot->declared_trigger_frame = -1;
		bot->declared_egress_proof_frame = -1;
		bot->declared_door_retreat = false;
		bot->declared_door_suffix_ms = 0;
		bot->declared_guard_paused = false;
		bot->declared_guard_pause_started = 0.0f;
		bot->declared_door_recovery_since = 0.0f;
		break;
	default:
		break;
	}
}

void SG_SpeedHookReleaseFinish(sg_bot_t *bot)
{
	bot->hook_phase = 0;
	bot->hook_deadline = 0.0f;
	bot->hook_bite_logged = false;
	bot->hook_attached_validated = false;
	bot->speedhook = false;
	bot->speedhook_pull_applied = false;
	bot->flow_release = false;
	SG_StagedTraversalCancel(bot, RL_RUN);
}

sg_speedhook_terminal_t SG_SpeedHookTerminalFinish(sg_bot_t *bot,
	qboolean reached_speed, int hookstate, qboolean hook_present)
{
	rune_t *rune = SG_Rune();
	sg_speedhook_terminal_t terminal = reached_speed ?
	    SG_SPEEDHOOK_TERMINAL_BURST :
	    (hookstate == 0 && !hook_present &&
	         !bot->speedhook_pull_applied ?
	             SG_SPEEDHOOK_TERMINAL_NOATTACH :
	             SG_SPEEDHOOK_TERMINAL_BURSTSTALL);
	qboolean retain_run = terminal == SG_SPEEDHOOK_TERMINAL_NOATTACH &&
	    !bot->commit_retirement_pending &&
	    rune && rune->links &&
	    bot->commit_link >= 0 && bot->commit_link < rune->hdr.num_links &&
	    rune->links[bot->commit_link].action == RL_RUN;

	bot->hook_phase = 0;
	bot->hook_link = -1;
	bot->hook_deadline = 0.0f;
	bot->hook_bite_logged = false;
	bot->hook_attached_validated = false;
	bot->speedhook = false;
	bot->speedhook_pull_applied = false;
	bot->flow_release = false;
	if (!retain_run)
		SG_StagedTraversalCancel(bot, RL_RUN);
	return terminal;
}

void SG_CarryStartRetireSupersededRoute(sg_bot_t *bot, qboolean carry_started)
{
	rune_t *rune;
	int action;
	if (!bot || !carry_started)
		return;
	bot->rail_link = bot->rally_cover = -1;
	bot->rail_stage = 0;
	bot->rail_until = bot->rally_since = bot->escape_until = bot->escape_yaw = 0.0f;
	if (bot->speedhook && bot->hook_phase == 1)
	{
		SG_StagedTraversalCancel(bot, RL_RUN);
		return;
	}
	rune = SG_Rune();
	if (!rune || !rune->links ||
	    bot->commit_link < 0 || bot->commit_link >= rune->hdr.num_links)
		return;
	action = rune->links[bot->commit_link].action;
	if ((action != RL_HOOK && action != RL_JUMP && action != RL_DROP &&
	     action != RL_ROCKETJUMP) ||
	    SG_TraversalControllerPhysical(bot, action))
		return;
	SG_StagedTraversalCancel(bot, action);
}

qboolean SG_NonCarryHandoffRetireSupersededRoute(sg_bot_t *bot,
	int previous_role, int current_role)
{
	rune_t *rune;
	int action;

	if (!bot || previous_role < 0 || previous_role >= SG_ROLES ||
	    current_role < 0 || current_role >= SG_ROLES ||
	    previous_role == current_role || current_role == SG_ROLE_CARRY)
		return false;
	bot->tac_seed = -1;
	bot->tac_time = 0.0f;
	bot->sticky_link = -1;
	bot->latch_until = 0.0f;
	bot->rail_link = -1;
	bot->rail_stage = 0;
	bot->rail_until = 0.0f;
	rune = SG_Rune();
	if (!rune || !rune->links || bot->commit_link < 0 ||
	    bot->commit_link >= rune->hdr.num_links)
		return true;
	action = rune->links[bot->commit_link].action;
	if (SG_DeclaredDoorRouteRequiresRelease(bot, action))
	{
		bot->commit_retirement_pending = true;
		return true;
	}
	if (SG_TraversalControllerPhysical(bot, action))
	{
		if (action == RL_RUN)
			bot->commit_retirement_pending = true;
		return true;
	}
	SG_StagedTraversalCancel(bot, action);
	return true;
}

void SG_StrikeDutyRetireSupersededRoute(sg_bot_t *bot,
	qboolean duty_replaces_route)
{
	rune_t *rune;
	int action;

	if (!bot || !duty_replaces_route)
		return;
	bot->tac_seed = -1;
	bot->tac_time = 0.0f;
	rune = SG_Rune();
	if (!rune || !rune->links || bot->commit_link < 0 ||
	    bot->commit_link >= rune->hdr.num_links)
		return;
	action = rune->links[bot->commit_link].action;
	if (SG_TraversalControllerPhysical(bot, action))
		return;
	SG_StagedTraversalCancel(bot, action);
}

qboolean SG_DefensePatrolRetire(sg_bot_t *bot, qboolean patrol_allowed)
{
	qboolean owned;
	int action = RL_RUN;
	int retired_link;
	rune_t *rune;

	if (!bot || patrol_allowed || bot->patrol_link < 0)
		return false;
	retired_link = bot->patrol_link;
	owned = bot->commit_link == retired_link;
	bot->patrol_link = -1;
	bot->patrol_seed = -1;
	if (owned)
	{
		rune = SG_Rune();
		if (rune && rune->links && retired_link < rune->hdr.num_links)
			action = rune->links[retired_link].action;
		if (SG_TraversalControllerPhysical(bot, action))
			bot->commit_retirement_pending = true;
		else
			SG_StagedTraversalCancel(bot, action);
	}
	else
	{
		if (bot->sticky_link == retired_link)
		{
			bot->sticky_link = -1;
			bot->latch_until = 0.0f;
		}
		if (bot->rail_link == retired_link)
		{
			bot->rail_link = -1;
			bot->rail_stage = 0;
			bot->rail_until = 0.0f;
		}
	}
	return true;
}
