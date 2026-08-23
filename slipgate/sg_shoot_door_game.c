/* sg_shoot_door_game.c -- full-frame shootable-door adapter. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_guard.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_shoot_door_game.h"
#include "slipgate/sg_util.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define SG_SHOOT_DOOR_STEP_MS 25
#define SG_SHOOT_DOOR_FRAME_STEPS 4

void ClientThink(edict_t *ent, usercmd_t *ucmd);
void door_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);

static sg_bot_t *ShootDoorBotForEntity(const edict_t *entity)
{
	int slot;

	for (slot = 0; entity && slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == entity)
			return &sg_bots[slot];
	return NULL;
}

static int ShootDoorBinding(int link_index, int owned,
	sg_rune_mechanism_binding_t *binding)
{
	rune_t *rune = SG_Rune();

	if (binding)
		memset(binding, 0, sizeof(*binding));
	return rune && binding && link_index >= 0 &&
	       link_index < rune->hdr.num_links &&
	       (owned ? SG_RuneMechanismBindingCaptureOwned(rune,
	                    (uint32_t)link_index, binding)
	              : SG_RuneMechanismBindingCapture(rune,
	                    (uint32_t)link_index, binding)) &&
	       binding->link && binding->plan && binding->entry_node &&
	       binding->mover_node && binding->entry_entity &&
	       binding->mover_entity && !binding->destination_node &&
	       !binding->egress_node && !binding->destination_entity &&
	       !binding->egress_entity && binding->link->action == RL_TRAIN &&
	       binding->link->mode == RLCM_PREOPEN &&
	       binding->plan->controller_kind ==
	           SG_MECHANISM_CONTROLLER_TRAIN_SHOOT &&
	       binding->plan->entry_key == binding->plan->mover_key &&
	       binding->entry_node == binding->mover_node &&
	       binding->entry_entity == binding->mover_entity &&
	       binding->entry_node->kind == SG_MECH_NODE_DOOR_MASTER;
}

static int ShootDoorKeys(const sg_rune_mechanism_binding_t *binding,
	int owned, uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS],
	size_t *count_out)
{
	uint32_t wide[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count = 0U;
	size_t index;

	if (count_out)
		*count_out = 0U;
	if (!binding || !keys || !count_out ||
	    !(owned ? SG_RuneMechanismBindingMoverKeys(binding, wide, &count)
	             : SG_RuneMechanismBindingTopologyMoverKeys(binding, wide,
	                   &count)) ||
	    count == 0U || count > SG_SHOOT_DOOR_GAME_MAX_MEMBERS ||
	    count != binding->plan->expected_members)
		return 0;
	for (index = 0U; index < count; index++)
	{
		if (wide[index] == 0U || wide[index] > UINT16_MAX)
			return 0;
		keys[index] = (uint16_t)wide[index];
	}
	*count_out = count;
	return 1;
}

static int ShootDoorMembers(const sg_rune_mechanism_binding_t *binding,
	int owned, edict_t *members[SG_SHOOT_DOOR_GAME_MAX_MEMBERS],
	uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS], size_t *count_out)
{
	size_t count;
	size_t index;

	if (count_out)
		*count_out = 0U;
	if (!binding || !members || !keys || !count_out ||
	    !ShootDoorKeys(binding, owned, keys, &count))
		return 0;
	for (index = 0U; index < count; index++)
	{
		const rune_mechanism_node_t *node =
			SG_RuneMechanismNodeByKey(binding->rune, keys[index]);

		members[index] = node
		    ? SG_MechCatalogResolveEntity((uint32_t)keys[index], node) : NULL;
		if (!members[index])
			return 0;
	}
	*count_out = count;
	return 1;
}

static int ShootDoorSweep(edict_t *const *members, size_t count,
	vec3_t sweep_min, vec3_t sweep_max, int *passage_axis_out)
{
	int motion_axis = -1;
	int passage_axis = -1;
	float passage_extent = HUGE_VALF;
	size_t index;
	int axis;

	if (passage_axis_out)
		*passage_axis_out = -1;
	if (!members || count == 0U || !sweep_min || !sweep_max ||
	    !passage_axis_out)
		return 0;
	VectorSet(sweep_min, HUGE_VALF, HUGE_VALF, HUGE_VALF);
	VectorSet(sweep_max, -HUGE_VALF, -HUGE_VALF, -HUGE_VALF);
	for (index = 0U; index < count; index++)
	{
		edict_t *member = members[index];

		if (!member)
			return 0;
		for (axis = 0; axis < 3; axis++)
		{
			float start_min = member->moveinfo.start_origin[axis] +
				member->mins[axis];
			float start_max = member->moveinfo.start_origin[axis] +
				member->maxs[axis];
			float end_min = member->moveinfo.end_origin[axis] +
				member->mins[axis];
			float end_max = member->moveinfo.end_origin[axis] +
				member->maxs[axis];

			if (!isfinite(start_min) || !isfinite(start_max) ||
			    !isfinite(end_min) || !isfinite(end_max))
				return 0;
			if (start_min < sweep_min[axis]) sweep_min[axis] = start_min;
			if (end_min < sweep_min[axis]) sweep_min[axis] = end_min;
			if (start_max > sweep_max[axis]) sweep_max[axis] = start_max;
			if (end_max > sweep_max[axis]) sweep_max[axis] = end_max;
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		if (fabsf(members[0]->moveinfo.end_origin[axis] -
		        members[0]->moveinfo.start_origin[axis]) <= 0.125f)
			continue;
		if (motion_axis >= 0)
			return 0;
		motion_axis = axis;
	}
	if (motion_axis < 0)
		return 0;
	for (axis = 0; axis < 2; axis++)
		if (axis != motion_axis &&
		    sweep_max[axis] - sweep_min[axis] < passage_extent)
		{
			passage_axis = axis;
			passage_extent = sweep_max[axis] - sweep_min[axis];
		}
	if (passage_axis < 0)
		return 0;
	*passage_axis_out = passage_axis;
	return 1;
}

static sg_shoot_door_side_t ShootDoorBodySide(const edict_t *entity,
	const vec3_t sweep_min, const vec3_t sweep_max, int passage_axis)
{
	if (!entity || passage_axis < 0 || passage_axis > 1)
		return SG_SHOOT_DOOR_SIDE_NONE;
	if (entity->absmax[passage_axis] <= sweep_min[passage_axis])
		return SG_SHOOT_DOOR_SIDE_MIN;
	if (entity->absmin[passage_axis] >= sweep_max[passage_axis])
		return SG_SHOOT_DOOR_SIDE_MAX;
	return SG_SHOOT_DOOR_SIDE_NONE;
}

static uint16_t ShootDoorHullGapQ8(const edict_t *entity,
	const vec3_t sweep_min, const vec3_t sweep_max, int passage_axis)
{
	float gap = 0.0f;
	float q8;

	if (!entity || passage_axis < 0 || passage_axis > 1)
		return UINT16_MAX;
	if (entity->absmax[passage_axis] <= sweep_min[passage_axis])
		gap = sweep_min[passage_axis] - entity->absmax[passage_axis];
	else if (entity->absmin[passage_axis] >= sweep_max[passage_axis])
		gap = entity->absmin[passage_axis] - sweep_max[passage_axis];
	q8 = ceilf(gap * 8.0f);
	if (!isfinite(q8) || q8 < 0.0f || q8 > UINT16_MAX)
		return UINT16_MAX;
	return (uint16_t)q8;
}

static int ShootDoorTeamPose(edict_t *const *members, size_t count,
	int *closed_out, int *opening_out, int *open_out)
{
	size_t index;
	int closed = 1;
	int opening = 1;
	int open = 1;

	if (closed_out) *closed_out = 0;
	if (opening_out) *opening_out = 0;
	if (open_out) *open_out = 0;
	if (!members || count == 0U || !closed_out || !opening_out || !open_out)
		return 0;
	for (index = 0U; index < count; index++)
	{
		edict_t *member = members[index];

		if (!member)
			return 0;
		closed = closed && member->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
			(VectorCompare(member->s.origin,
			     member->moveinfo.start_origin) ||
			 SG_MoverCompletionMatches(member,
			     SG_MOVER_COMPLETION_BOTTOM)) &&
			VectorCompare(member->velocity, vec3_origin);
		opening = opening && member->moveinfo.state == SG_PLAT_STATE_UP;
		open = open && member->moveinfo.state == SG_PLAT_STATE_TOP &&
			SG_MoverCompletionMatches(member, SG_MOVER_COMPLETION_TOP);
	}
	*closed_out = closed;
	*opening_out = opening;
	*open_out = open;
	return closed + opening + open == 1;
}

static int ShootDoorAim(const edict_t *entity, edict_t *const *members,
	size_t count, vec3_t target_out, vec3_t view_out)
{
	size_t index;
	int axis;

	if (!entity || !entity->client || !members || count == 0U ||
	    !target_out || !view_out)
		return 0;
	for (index = 0U; index < count; index++)
	{
		vec3_t muzzle;
		vec3_t forward;
		vec3_t end;
		trace_t trace;

		for (axis = 0; axis < 3; axis++)
			target_out[axis] =
				0.5f * (members[index]->absmin[axis] +
				        members[index]->absmax[axis]);
		if (!SG_BlasterAimAngles(entity->s.origin, entity->viewheight,
		        entity->client->pers.hand, target_out, view_out, muzzle))
			continue;
		AngleVectors(view_out, forward, NULL, NULL);
		VectorMA(muzzle, 8192.0f, forward, end);
		trace = gi.trace(muzzle, NULL, NULL, end, (edict_t *)entity, MASK_SHOT);
		if (!trace.startsolid && !trace.allsolid &&
		    trace.ent == members[index])
			return 1;
	}
	return 0;
}

static int ShootDoorWitness(const sg_bot_t *bot, int link_index,
	sg_shoot_door_witness_t *witness,
	uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS], size_t *key_count_out,
	int16_t destination_q8[3])
{
	rune_t *rune = SG_Rune();
	sg_rune_mechanism_binding_t binding;
	edict_t *members[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	vec3_t sweep_min;
	vec3_t sweep_max;
	size_t count;
	int passage_axis;
	int axis;

	if (witness) memset(witness, 0, sizeof(*witness));
	if (key_count_out) *key_count_out = 0U;
	if (!bot || !bot->ent || !witness || !keys || !key_count_out ||
	    !destination_q8 || !rune ||
	    !ShootDoorBinding(link_index, 1, &binding) || !rune->seeds ||
	    binding.link->from < 0 || binding.link->to < 0 ||
	    binding.link->from >= rune->hdr.num_seeds ||
	    binding.link->to >= rune->hdr.num_seeds ||
	    binding.plan->cooldown_ms == 0U ||
	    binding.plan->cooldown_ms > UINT16_MAX || binding.link->cost_ms <= 0 ||
	    !ShootDoorMembers(&binding, 1, members, keys, &count) ||
	    !ShootDoorSweep(members, count, sweep_min, sweep_max, &passage_axis))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float value = rune->seeds[binding.link->to].origin[axis] * 8.0f;

		if (!isfinite(value) || value < SHRT_MIN || value > SHRT_MAX ||
		    value != (float)(int16_t)value)
			return 0;
		destination_q8[axis] = (int16_t)value;
	}
	witness->link_index = (uint32_t)link_index;
	witness->master_key = binding.mover_node->key;
	witness->expected_members = binding.plan->expected_members;
	/* The link cost already contains the proved shot flight, complete opening,
	 * and crossing horizon.  The plan cooldown remains the mover-only bound. */
	witness->opening_bound_ms = (uint16_t)binding.link->cost_ms;
	witness->passage_axis = (uint8_t)passage_axis;
	witness->source_side = ShootDoorBodySide(bot->ent, sweep_min, sweep_max,
		passage_axis);
	*key_count_out = count;
	return 1;
}

static int ShootDoorObservation(sg_bot_t *bot,
	sg_shoot_door_observation_t *observation)
{
	sg_rune_mechanism_binding_t binding;
	edict_t *entity;
	edict_t *members[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	vec3_t sweep_min;
	vec3_t sweep_max;
	vec3_t destination;
	vec3_t target;
	vec3_t view;
	size_t count;
	int passage_axis;
	int axis;
	int closed;
	int opening;
	int open;
	int aim_clear;

	if (!bot || !(entity = bot->ent) || !entity->inuse || !entity->client ||
	    !observation)
		return 0;
	memset(observation, 0, sizeof(*observation));
	observation->alive = entity->deadflag == DEAD_NO && entity->health > 0;
	observation->dry = entity->waterlevel == 0;
	observation->supported = entity->groundentity != NULL;
	observation->shot_count = bot->shoot_door.shot_count;
	if (!ShootDoorBinding((int)bot->shoot_door.live.witness.link_index,
	        bot->shoot_door.guard_owned != 0U, &binding) ||
	    !ShootDoorMembers(&binding, bot->shoot_door.guard_owned != 0U,
	        members, keys, &count) ||
	    count != bot->shoot_door.mover_count ||
	    memcmp(keys, bot->shoot_door.mover_keys,
	        count * sizeof(keys[0])) != 0 ||
	    !ShootDoorSweep(members, count, sweep_min, sweep_max, &passage_axis) ||
	    passage_axis != bot->shoot_door.live.witness.passage_axis ||
	    !ShootDoorTeamPose(members, count, &closed, &opening, &open))
		return 1;
	observation->binding_current = 1U;
	observation->team_closed = (uint8_t)closed;
	observation->team_opening = (uint8_t)opening;
	observation->team_open = (uint8_t)open;
	observation->body_side = ShootDoorBodySide(entity, sweep_min, sweep_max,
		passage_axis);
	observation->hull_to_sweep_gap_q8 = ShootDoorHullGapQ8(entity,
		sweep_min, sweep_max, passage_axis);
	observation->body_clear = observation->body_side !=
		SG_SHOOT_DOOR_SIDE_NONE;
	for (axis = 0; axis < 3; axis++)
		destination[axis] = bot->shoot_door.destination_q8[axis] * 0.125f;
	observation->arrived = observation->supported && observation->dry &&
		SG_SupportedArrived(entity->s.origin, destination,
		    entity->groundentity != NULL, entity->watertype,
		    entity->waterlevel, NULL);
	aim_clear = ShootDoorAim(entity, members, count, target, view);
	observation->line_of_fire_clear = aim_clear != 0;
	if (bot->shoot_door.shot_count == 0U)
	{
		gitem_t *blaster = FindItem("Blaster");

		observation->weapon_ready = blaster &&
			entity->client->pers.weapon == blaster &&
			entity->client->weaponstate == WEAPON_READY;
		observation->aim_contact_current = aim_clear &&
			(short)ANGLE2SHORT(entity->client->v_angle[PITCH]) ==
			    (short)ANGLE2SHORT(view[PITCH]) &&
			(short)ANGLE2SHORT(entity->client->v_angle[YAW]) ==
			    (short)ANGLE2SHORT(view[YAW]);
	}
	return 1;
}

static int ShootDoorGuardAuthorize(sg_bot_t *bot)
{
	if (!bot || bot->shoot_door.mover_count == 0U)
		return 0;
	return SG_CompoundGuardAuthorize(&bot->compound_guard,
		SG_MOVER_LAW_TRAIN_GATE, bot->shoot_door.mover_keys,
		bot->shoot_door.mover_count,
		(int)bot->shoot_door.live.witness.link_index,
		bot->shoot_door.live.witness.master_key) == SG_COMPOUND_GUARD_OK;
}

static int ShootDoorBegin(sg_bot_t *bot, int link_index)
{
	sg_shoot_door_witness_t witness;
	sg_shoot_door_observation_t observation;
	uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	int16_t destination_q8[3];
	size_t count;

	if (!bot || !bot->ent || !ShootDoorWitness(bot, link_index, &witness,
	        keys, &count, destination_q8))
		return 0;
	memset(&bot->shoot_door, 0, sizeof(bot->shoot_door));
	bot->shoot_door.live.witness = witness;
	bot->shoot_door.mover_count = (uint8_t)count;
	memcpy(bot->shoot_door.mover_keys, keys, count * sizeof(keys[0]));
	memcpy(bot->shoot_door.destination_q8, destination_q8,
		sizeof(destination_q8));
	if (!ShootDoorObservation(bot, &observation) ||
	    observation.team_closed != 1U || observation.body_clear != 1U ||
	    (observation.body_side != SG_SHOOT_DOOR_SIDE_MIN &&
	     observation.body_side != SG_SHOOT_DOOR_SIDE_MAX))
		goto fail;
	bot->shoot_door.live.witness.source_side = observation.body_side;
	if (SG_CompoundGuardAcquireTrainGate(&bot->compound_guard,
	        bot->shoot_door.mover_keys, bot->shoot_door.mover_count,
	        link_index, witness.master_key) != SG_COMPOUND_GUARD_OK)
		goto fail;
	bot->shoot_door.guard_owned = 1U;
	if (!ShootDoorObservation(bot, &observation) ||
	    !SG_ShootDoorLiveBegin(&bot->shoot_door.live,
	        &bot->shoot_door.live.witness, &observation))
		goto guarded_fail;
	return 1;

guarded_fail:
	(void)SG_CompoundGuardReleaseProvedClear(&bot->compound_guard);
fail:
	memset(&bot->shoot_door, 0, sizeof(bot->shoot_door));
	return 0;
}

int SG_ShootDoorGameOwns(const sg_bot_t *bot)
{
	return bot && bot->shoot_door.guard_owned == 1U &&
	       bot->shoot_door.live.phase >= SG_SHOOT_DOOR_ACTIVATE &&
	       bot->shoot_door.live.phase <= SG_SHOOT_DOOR_FAILED;
}

void SG_ShootDoorGameReset(sg_bot_t *bot)
{
	sg_compound_guard_result_t result;

	if (!bot)
		return;
	if (bot->shoot_door.guard_owned)
	{
		result = SG_CompoundGuardValidate(&bot->compound_guard, NULL);
		if (result != SG_COMPOUND_GUARD_NO_LEASE &&
		    result != SG_COMPOUND_GUARD_NOT_ATTACHED)
			return;
	}
	memset(&bot->shoot_door, 0, sizeof(bot->shoot_door));
}

static int ShootDoorFinish(sg_bot_t *bot)
{
	sg_shoot_door_observation_t observation;
	int complete;

	if (!bot || (bot->shoot_door.live.phase != SG_SHOOT_DOOR_COMPLETE &&
	             bot->shoot_door.live.phase != SG_SHOOT_DOOR_FAILED))
		return 0;
	complete = bot->shoot_door.live.phase == SG_SHOOT_DOOR_COMPLETE;
	if (ShootDoorObservation(bot, &observation) && observation.body_clear &&
	    (observation.team_closed || observation.team_open) &&
	    SG_CompoundGuardReleaseProvedClear(&bot->compound_guard) ==
	        SG_COMPOUND_GUARD_OK)
	{
		memset(&bot->shoot_door, 0, sizeof(bot->shoot_door));
		bot->commit_link = -1;
		return 1;
	}
	(void)SG_CompoundGuardQuarantine(&bot->compound_guard);
	if (complete)
		bot->shoot_door.live.phase = SG_SHOOT_DOOR_FAILED;
	return 1;
}

int SG_ShootDoorGameEmit(sg_bot_t *bot, int selected_link)
{
	edict_t *entity;
	int step;

	if (!bot || !(entity = bot->ent) || !entity->client)
		return 0;
	if (ShootDoorFinish(bot))
		return 1;
	if (!SG_ShootDoorGameOwns(bot))
	{
		sg_rune_mechanism_binding_t binding;

		if (!ShootDoorBinding(selected_link, 1, &binding))
			return 0;
		if (!ShootDoorBegin(bot, selected_link))
		{
			bot->commit_link = -1;
			return 1;
		}
	}
	for (step = 0; step < SG_SHOOT_DOOR_FRAME_STEPS; step++)
	{
		sg_shoot_door_observation_t observation;
		sg_shoot_door_command_t control;
		sg_rune_mechanism_binding_t binding;
		edict_t *members[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
		uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
		size_t count;
		usercmd_t command;
		vec3_t target;
		vec3_t view;
		int axis;

		if (!ShootDoorObservation(bot, &observation))
		{
			bot->shoot_door.live.phase = SG_SHOOT_DOOR_FAILED;
			(void)ShootDoorFinish(bot);
			return 1;
		}
		control = SG_ShootDoorLiveStep(&bot->shoot_door.live,
			&observation, SG_SHOOT_DOOR_STEP_MS);
		if (bot->shoot_door.live.phase == SG_SHOOT_DOOR_COMPLETE ||
		    bot->shoot_door.live.phase == SG_SHOOT_DOOR_FAILED)
		{
			(void)ShootDoorFinish(bot);
			return 1;
		}
		memset(&command, 0, sizeof(command));
		command.msec = SG_SHOOT_DOOR_STEP_MS;
		if (control == SG_SHOOT_DOOR_COMMAND_TO_DESTINATION ||
		    control == SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP)
		{
			for (axis = 0; axis < 3; axis++)
				target[axis] = bot->shoot_door.destination_q8[axis] * 0.125f;
			if (!SG_DeclaredCommand(entity->s.origin, target,
			        &entity->client->ps.pmove, &command))
				bot->shoot_door.live.phase = SG_SHOOT_DOOR_FAILED;
			else if (control == SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP)
				command.upmove = 400;
		}
		else if (control == SG_SHOOT_DOOR_COMMAND_EQUIP)
		{
			gitem_t *blaster = FindItem("Blaster");

			if (!blaster)
				bot->shoot_door.live.phase = SG_SHOOT_DOOR_FAILED;
			else if (entity->client->pers.weapon != blaster)
				entity->client->newweapon = blaster;
		}
		else if (control == SG_SHOOT_DOOR_COMMAND_AIM ||
		         control == SG_SHOOT_DOOR_COMMAND_SHOOT)
		{
			if (!ShootDoorBinding(
			        (int)bot->shoot_door.live.witness.link_index, 1, &binding) ||
			    !ShootDoorMembers(&binding, 1, members, keys, &count) ||
			    !ShootDoorAim(entity, members, count, target, view))
				bot->shoot_door.live.phase = SG_SHOOT_DOOR_FAILED;
			else
			{
				command.angles[PITCH] = (short)(ANGLE2SHORT(view[PITCH]) -
					entity->client->ps.pmove.delta_angles[PITCH]);
				command.angles[YAW] = (short)(ANGLE2SHORT(view[YAW]) -
					entity->client->ps.pmove.delta_angles[YAW]);
				command.angles[ROLL] = (short)(-
					entity->client->ps.pmove.delta_angles[ROLL]);
				if (control == SG_SHOOT_DOOR_COMMAND_SHOOT)
					command.buttons = BUTTON_ATTACK;
			}
		}
		if (bot->shoot_door.live.phase == SG_SHOOT_DOOR_FAILED)
		{
			(void)ShootDoorFinish(bot);
			return 1;
		}
		ClientThink(entity, &command);
	}
	return 1;
}

int SG_ShootDoorGameAuthorizeActivation(edict_t *source,
	edict_t *door_master, edict_t *activator)
{
	sg_bot_t *bot = ShootDoorBotForEntity(activator);
	sg_rune_mechanism_binding_t binding;
	edict_t *members[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	uint16_t keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	size_t count;
	size_t index;
	int damaged = 0;
	int reset = 1;

	if (!bot || !SG_ShootDoorGameOwns(bot))
		return -1;
	if (!source || source != activator || !door_master ||
	    !ShootDoorBinding((int)bot->shoot_door.live.witness.link_index,
	        0, &binding) || binding.mover_entity != door_master ||
	    !ShootDoorMembers(&binding, 0, members, keys, &count) ||
	    count != bot->shoot_door.mover_count ||
	    memcmp(keys, bot->shoot_door.mover_keys,
	        count * sizeof(keys[0])) != 0 || !ShootDoorGuardAuthorize(bot) ||
	    bot->shoot_door.live.phase != SG_SHOOT_DOOR_ACTIVATE ||
	    bot->shoot_door.live.shot_requested != 1U)
		return 0;
	for (index = 0U; index < count; index++)
	{
		edict_t *member = members[index];

		if (member->moveinfo.state != SG_PLAT_STATE_BOTTOM ||
		    member->die != door_killed || member->max_health <= 0)
			return 0;
		if (member->health <= 0 && member->takedamage == DAMAGE_YES)
			damaged++;
		reset = reset && member->health == member->max_health &&
			member->takedamage == DAMAGE_NO;
	}
	if (bot->shoot_door.shot_count == 0U && damaged == 1 && !reset)
	{
		bot->shoot_door.shot_count = 1U;
		return 1;
	}
	return bot->shoot_door.shot_count == 1U && damaged == 0 && reset;
}
