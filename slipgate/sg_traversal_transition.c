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
	memset(&state, 0, sizeof(state));
	state.action = action;
	state.hook_phase = bot->hook_phase;
	state.rocketjump_phase = bot->rj_phase;
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

void SG_StagedTraversalCancel(sg_bot_t *bot, int action)
{
	if (!bot)
		return;
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
	bot->commit_route_field = NULL;
	bot->sticky_link = -1;
	bot->latch_until = 0.0f;
	if (bot->hook_phase == 1)
	{
		bot->hook_phase = 0;
		bot->hook_link = -1;
		bot->hook_deadline = 0.0f;
		bot->hook_bite_logged = false;
		bot->hook_attached_validated = false;
		bot->speedhook = false;
		bot->flow_release = false;
	}
	if (bot->rj_phase == 1)
	{
		bot->rj_phase = 0;
		bot->rj_deadline = 0.0f;
		bot->rj_fire_until = 0.0f;
		bot->rj_use_next = 0.0f;
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
		bot->flow_release = false;
		break;
	case RL_SWIM:
		SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
		    &bot->swim_replay_link, &bot->swim_validated,
		    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
		break;
	case RL_ROCKETJUMP:
		bot->rj_phase = 0;
		bot->rj_deadline = 0.0f;
		bot->rj_fire_until = 0.0f;
		bot->rj_use_next = 0.0f;
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

void SG_CarryStartRetireSupersededRoute(sg_bot_t *bot, qboolean carry_started)
{
	rune_t *rune;
	int action;
	if (!bot || !carry_started)
		return;
	bot->rail_link = bot->rally_cover = -1;
	bot->rail_stage = 0;
	bot->rail_until = bot->rally_since = 0.0f;
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
