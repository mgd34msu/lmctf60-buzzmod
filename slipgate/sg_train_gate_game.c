/* sg_train_gate_game.c -- full-frame adapter for sealed train-gate links. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_guard.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_train_gate_game.h"
#include "slipgate/sg_util.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define SG_TRAIN_GATE_STEP_MS 25
#define SG_TRAIN_GATE_FRAME_STEPS 4

void ClientThink(edict_t *ent, usercmd_t *ucmd);
void button_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);

static sg_bot_t *TrainBotForEntity(const edict_t *entity)
{
	int slot;

	for (slot = 0; entity && slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == entity)
			return &sg_bots[slot];
	return NULL;
}

static int TrainSelected(int link_index)
{
	rune_t *rune = SG_Rune();

	return rune && rune->links && link_index >= 0 &&
	       link_index < rune->hdr.num_links &&
	       rune->links[link_index].action == RL_TRAIN;
}

static int TrainBinding(int link_index, int owned,
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
	       binding->mover_node && binding->destination_node &&
	       binding->egress_node && binding->entry_entity &&
	       binding->mover_entity && binding->destination_entity &&
	       binding->egress_entity && binding->link->action == RL_TRAIN &&
	       (binding->plan->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN ||
	        binding->plan->controller_kind ==
	            SG_MECHANISM_CONTROLLER_TRAIN_SHOOT);
}

static int TrainQ8(const float source[3], int16_t destination[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float value = source[axis] * 8.0f;

		if (!isfinite(value) || value < SHRT_MIN || value > SHRT_MAX ||
		    value != (float)(int16_t)value)
			return 0;
		destination[axis] = (int16_t)value;
	}
	return 1;
}

static int TrainWitness(int link_index, sg_train_gate_witness_t *witness)
{
	rune_t *rune = SG_Rune();
	sg_rune_mechanism_binding_t binding;
	vec3_t button_center;
	uint32_t mover_keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t mover_count = 0U;

	if (witness)
		memset(witness, 0, sizeof(*witness));
	if (!witness || !TrainBinding(link_index, 0, &binding) ||
	    !rune || !rune->seeds || binding.link->from < 0 ||
	    binding.link->to < 0 || binding.link->from >= rune->hdr.num_seeds ||
	    binding.link->to >= rune->hdr.num_seeds ||
	    !SG_RuneMechanismBindingMoverKeys(&binding, mover_keys,
	        &mover_count) || mover_count != 1U ||
	    mover_keys[0] != binding.mover_node->key ||
	    binding.plan->cooldown_ms == 0U ||
	    binding.plan->cooldown_ms > UINT16_MAX ||
	    !TrainQ8(rune->seeds[binding.link->from].origin,
	        witness->source_q8) ||
	    !TrainQ8(binding.link->anchor, witness->entry_q8) ||
	    !TrainQ8(binding.link->mechanism_anchor, witness->cross_q8) ||
	    !TrainQ8(rune->seeds[binding.link->to].origin,
	        witness->destination_q8))
		return 0;
	button_center[0] =
		(binding.entry_entity->absmin[0] + binding.entry_entity->absmax[0]) * 0.5f;
	button_center[1] =
		(binding.entry_entity->absmin[1] + binding.entry_entity->absmax[1]) * 0.5f;
	button_center[2] =
		(binding.entry_entity->absmin[2] + binding.entry_entity->absmax[2]) * 0.5f;
	if (!TrainQ8(button_center, witness->button_q8))
		return 0;
	witness->link_index = (uint32_t)link_index;
	witness->button_key = binding.entry_node->key;
	witness->train_key = binding.mover_node->key;
	witness->closed_corner_key = binding.destination_node->key;
	witness->open_corner_key = binding.egress_node->key;
	witness->activation = binding.plan->controller_kind ==
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT
		? SG_TRAIN_GATE_ACTIVATION_SHOOT : SG_TRAIN_GATE_ACTIVATION_TOUCH;
	witness->opening_bound_ms = (uint16_t)binding.plan->cooldown_ms;
	return 1;
}

static sg_train_gate_pose_t TrainPose(uint32_t train_key)
{
	sg_mech_train_gate_pose_t pose;

	if (!SG_MechCatalogTrainGatePose(train_key, &pose))
		return SG_TRAIN_GATE_POSE_INVALID;
	return (sg_train_gate_pose_t)pose;
}

static int TrainBodyClear(const edict_t *entity, uint32_t train_key)
{
	vec3_t mins;
	vec3_t maxs;

	return entity && SG_MechCatalogTrainGateSweep(train_key, mins, maxs) &&
	       (entity->absmax[0] <= mins[0] || entity->absmin[0] >= maxs[0] ||
	        entity->absmax[1] <= mins[1] || entity->absmin[1] >= maxs[1] ||
	        entity->absmax[2] <= mins[2] || entity->absmin[2] >= maxs[2]);
}

static int TrainObservation(sg_bot_t *bot,
	sg_train_gate_observation_t *observation)
{
	sg_rune_mechanism_binding_t binding;
	edict_t *entity;
	vec3_t destination;
	vec3_t entry;
	vec3_t cross;
	int axis;

	if (!bot || !(entity = bot->ent) || !entity->inuse || !entity->client ||
	    !observation)
		return 0;
	memset(observation, 0, sizeof(*observation));
	observation->alive = entity->deadflag == DEAD_NO && entity->health > 0;
	observation->supported = entity->groundentity != NULL;
	observation->dry = entity->waterlevel == 0;
	observation->binding_current = TrainBinding(
	    (int)bot->train_gate.live.witness.link_index,
	    bot->train_gate.guard_owned != 0U, &binding);
	observation->pose = TrainPose(bot->train_gate.live.witness.train_key);
	observation->body_clear = TrainBodyClear(entity,
	    bot->train_gate.live.witness.train_key);
	for (axis = 0; axis < 3; axis++)
	{
		entry[axis] = bot->train_gate.live.witness.entry_q8[axis] * 0.125f;
		cross[axis] = bot->train_gate.live.witness.cross_q8[axis] * 0.125f;
		destination[axis] =
		    bot->train_gate.live.witness.destination_q8[axis] * 0.125f;
	}
	observation->entry_arrived = observation->supported && observation->dry &&
	    SG_SupportedArrived(entity->s.origin, entry,
	        entity->groundentity != NULL, entity->watertype,
	        entity->waterlevel, NULL);
	observation->cross_arrived = observation->supported && observation->dry &&
	    SG_SupportedArrived(entity->s.origin, cross,
	        entity->groundentity != NULL, entity->watertype,
	        entity->waterlevel, NULL);
	observation->arrived = observation->supported && observation->dry &&
	    SG_SupportedArrived(entity->s.origin, destination,
	        entity->groundentity != NULL, entity->watertype,
	        entity->waterlevel, NULL);
	observation->button_touch_count = bot->train_gate.button_touch_count;
	observation->button_shot_count = bot->train_gate.button_shot_count;
	observation->target_dispatch_count =
	    bot->train_gate.target_dispatch_count;
	observation->train_use_count = bot->train_gate.train_use_count;
	if (bot->train_gate.live.witness.activation ==
	    SG_TRAIN_GATE_ACTIVATION_SHOOT && bot->train_gate.button_shot_count == 0U)
	{
		gitem_t *blaster = FindItem("Blaster");
		vec3_t aim;
		vec3_t view;
		vec3_t muzzle;
		vec3_t forward;
		vec3_t end;
		trace_t trace;

		observation->weapon_ready = blaster &&
			entity->client->pers.weapon == blaster &&
			entity->client->weaponstate == WEAPON_READY;
		for (axis = 0; axis < 3; axis++)
			aim[axis] = bot->train_gate.live.witness.button_q8[axis] * 0.125f;
		if (observation->weapon_ready &&
		    SG_BlasterAimAngles(entity->s.origin, entity->viewheight,
		        entity->client->pers.hand, aim, view, muzzle))
		{
			observation->aim_contact_current =
				(short)ANGLE2SHORT(entity->client->v_angle[PITCH]) ==
				    (short)ANGLE2SHORT(view[PITCH]) &&
				(short)ANGLE2SHORT(entity->client->v_angle[YAW]) ==
				    (short)ANGLE2SHORT(view[YAW]);
			AngleVectors(view, forward, NULL, NULL);
			VectorMA(muzzle, 8192.0f, forward, end);
			trace = gi.trace(muzzle, NULL, NULL, end, entity, MASK_SHOT);
			observation->line_of_fire_clear =
				!trace.startsolid && !trace.allsolid &&
				trace.ent == binding.entry_entity;
		}
	}
	return 1;
}

static int TrainActivationCount(const sg_bot_t *bot)
{
	if (!bot)
		return 0;
	return bot->train_gate.live.witness.activation ==
		SG_TRAIN_GATE_ACTIVATION_SHOOT
		? bot->train_gate.button_shot_count
		: bot->train_gate.button_touch_count;
}

static int TrainGuardAuthorize(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding)
{
	sg_mover_key_t key;

	if (!bot || !binding || !binding->mover_node || !binding->entry_node ||
	    binding->mover_node->key == 0U || binding->mover_node->key > UINT16_MAX)
		return 0;
	key = (sg_mover_key_t)binding->mover_node->key;
	return SG_CompoundGuardAuthorize(&bot->compound_guard,
	    SG_MOVER_LAW_TRAIN_GATE, &key, 1U,
	    (int)bot->train_gate.live.witness.link_index,
	    binding->entry_node->key) == SG_COMPOUND_GUARD_OK;
}

static int TrainBegin(sg_bot_t *bot, int link_index)
{
	sg_train_gate_witness_t witness;
	sg_train_gate_observation_t observation;
	sg_rune_mechanism_binding_t binding;
	sg_mover_key_t mover_key;

	if (!bot || !TrainWitness(link_index, &witness) ||
	    !TrainBinding(link_index, 0, &binding) ||
	    binding.mover_node->key == 0U || binding.mover_node->key > UINT16_MAX)
		return 0;
	memset(&bot->train_gate, 0, sizeof(bot->train_gate));
	bot->train_gate.live.witness = witness;
	if (!TrainObservation(bot, &observation) ||
	    observation.pose != SG_TRAIN_GATE_POSE_CLOSED ||
	    !observation.body_clear)
		goto fail;
	mover_key = (sg_mover_key_t)binding.mover_node->key;
	if (SG_CompoundGuardAcquireTrainGate(&bot->compound_guard, &mover_key,
	        1U, link_index, binding.entry_node->key) != SG_COMPOUND_GUARD_OK)
		goto fail;
	bot->train_gate.guard_owned = 1U;
	if (!TrainObservation(bot, &observation) ||
	    !SG_TrainGateLiveBegin(&bot->train_gate.live, &witness, &observation))
		goto guarded_fail;
	return 1;

guarded_fail:
	(void)SG_CompoundGuardReleaseProvedClear(&bot->compound_guard);
fail:
	memset(&bot->train_gate, 0, sizeof(bot->train_gate));
	return 0;
}

int SG_TrainGateGameOwns(const sg_bot_t *bot)
{
	return bot && bot->train_gate.guard_owned == 1U &&
	       bot->train_gate.live.phase >= SG_TRAIN_GATE_APPROACH &&
	       bot->train_gate.live.phase <= SG_TRAIN_GATE_FAILED;
}

void SG_TrainGateGameReset(sg_bot_t *bot)
{
	sg_compound_guard_result_t result;

	if (!bot)
		return;
	if (bot->train_gate.guard_owned)
	{
		result = SG_CompoundGuardValidate(&bot->compound_guard, NULL);
		if (result != SG_COMPOUND_GUARD_NO_LEASE &&
		    result != SG_COMPOUND_GUARD_NOT_ATTACHED)
			return;
	}
	memset(&bot->train_gate, 0, sizeof(bot->train_gate));
}

static int TrainFinish(sg_bot_t *bot)
{
	sg_train_gate_observation_t observation;
	int complete;

	if (!bot || (bot->train_gate.live.phase != SG_TRAIN_GATE_COMPLETE &&
	             bot->train_gate.live.phase != SG_TRAIN_GATE_FAILED))
		return 0;
	complete = bot->train_gate.live.phase == SG_TRAIN_GATE_COMPLETE;
	if (TrainObservation(bot, &observation) && observation.body_clear &&
	    (observation.pose == SG_TRAIN_GATE_POSE_CLOSED ||
	     observation.pose == SG_TRAIN_GATE_POSE_OPEN))
	{
		if (SG_CompoundGuardReleaseProvedClear(&bot->compound_guard) ==
		    SG_COMPOUND_GUARD_OK)
		{
			memset(&bot->train_gate, 0, sizeof(bot->train_gate));
			bot->commit_link = -1;
			return 1;
		}
	}
	(void)SG_CompoundGuardQuarantine(&bot->compound_guard);
	if (complete)
		bot->train_gate.live.phase = SG_TRAIN_GATE_FAILED;
	return 1;
}

int SG_TrainGateGameEmit(sg_bot_t *bot, int selected_link)
{
	edict_t *entity;
	int step;

	if (!bot || !(entity = bot->ent) || !entity->client)
		return 0;
	if (TrainFinish(bot))
		return 1;
	if (!SG_TrainGateGameOwns(bot))
	{
		if (!TrainSelected(selected_link))
			return 0;
		if (!TrainBegin(bot, selected_link))
		{
			bot->commit_link = -1;
			return 1;
		}
	}
	for (step = 0; step < SG_TRAIN_GATE_FRAME_STEPS; step++)
	{
		sg_train_gate_observation_t observation;
		sg_train_gate_command_t control;
		usercmd_t command;
		vec3_t target;
		int axis;

		if (!TrainObservation(bot, &observation))
		{
			bot->train_gate.live.phase = SG_TRAIN_GATE_FAILED;
			(void)TrainFinish(bot);
			return 1;
		}
		control = SG_TrainGateLiveStep(&bot->train_gate.live, &observation,
		    SG_TRAIN_GATE_STEP_MS);
		if (bot->train_gate.live.phase == SG_TRAIN_GATE_COMPLETE ||
		    bot->train_gate.live.phase == SG_TRAIN_GATE_FAILED)
		{
			(void)TrainFinish(bot);
			return 1;
		}
		memset(&command, 0, sizeof(command));
		command.msec = SG_TRAIN_GATE_STEP_MS;
		if (control == SG_TRAIN_GATE_COMMAND_TO_BUTTON ||
		    control == SG_TRAIN_GATE_COMMAND_TO_ENTRY ||
		    control == SG_TRAIN_GATE_COMMAND_TO_CROSS ||
		    control == SG_TRAIN_GATE_COMMAND_TO_EGRESS)
		{
			const int16_t *q8 = control == SG_TRAIN_GATE_COMMAND_TO_BUTTON
			    ? bot->train_gate.live.witness.button_q8
			    : (control == SG_TRAIN_GATE_COMMAND_TO_ENTRY
			          ? bot->train_gate.live.witness.entry_q8
			          : (control == SG_TRAIN_GATE_COMMAND_TO_CROSS
			                ? bot->train_gate.live.witness.cross_q8
			                : bot->train_gate.live.witness.destination_q8));

			for (axis = 0; axis < 3; axis++)
				target[axis] = q8[axis] * 0.125f;
			if (!SG_DeclaredCommand(entity->s.origin, target,
			        &entity->client->ps.pmove, &command))
			{
				bot->train_gate.live.phase = SG_TRAIN_GATE_FAILED;
				(void)TrainFinish(bot);
				return 1;
			}
		}
		else if (control == SG_TRAIN_GATE_COMMAND_EQUIP)
		{
			gitem_t *blaster = FindItem("Blaster");

			if (!blaster)
				bot->train_gate.live.phase = SG_TRAIN_GATE_FAILED;
			else if (entity->client->pers.weapon != blaster)
				entity->client->newweapon = blaster;
		}
		else if (control == SG_TRAIN_GATE_COMMAND_AIM_BUTTON ||
		         control == SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON)
		{
			vec3_t view;
			vec3_t muzzle;

			for (axis = 0; axis < 3; axis++)
				target[axis] =
					bot->train_gate.live.witness.button_q8[axis] * 0.125f;
			if (!SG_BlasterAimAngles(entity->s.origin, entity->viewheight,
			        entity->client->pers.hand, target, view, muzzle))
				bot->train_gate.live.phase = SG_TRAIN_GATE_FAILED;
			else
			{
				command.angles[PITCH] = (short)(ANGLE2SHORT(view[PITCH]) -
					entity->client->ps.pmove.delta_angles[PITCH]);
				command.angles[YAW] = (short)(ANGLE2SHORT(view[YAW]) -
					entity->client->ps.pmove.delta_angles[YAW]);
				command.angles[ROLL] = (short)(-
					entity->client->ps.pmove.delta_angles[ROLL]);
				if (control == SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON)
					command.buttons = BUTTON_ATTACK;
			}
		}
		if (bot->train_gate.live.phase == SG_TRAIN_GATE_FAILED)
		{
			(void)TrainFinish(bot);
			return 1;
		}
		ClientThink(entity, &command);
	}
	return 1;
}

static int TrainActiveEvent(sg_bot_t *bot, edict_t *button,
	sg_rune_mechanism_binding_t *binding)
{
	return bot && SG_TrainGateGameOwns(bot) && button &&
	       TrainBinding((int)bot->train_gate.live.witness.link_index, 1,
	           binding) && binding->entry_entity == button &&
	       TrainGuardAuthorize(bot, binding);
}

int SG_TrainGateGameAuthorizeButtonTouch(edict_t *button, edict_t *activator)
{
	sg_bot_t *bot = TrainBotForEntity(activator);
	sg_rune_mechanism_binding_t binding;

	if (!bot || !SG_TrainGateGameOwns(bot))
		return -1;
	if (!TrainActiveEvent(bot, button, &binding) ||
	    binding.plan->controller_kind != SG_MECHANISM_CONTROLLER_TRAIN ||
	    bot->train_gate.live.phase != SG_TRAIN_GATE_APPROACH ||
	    bot->train_gate.button_touch_count != 0U ||
	    button->moveinfo.state != SG_PLAT_STATE_BOTTOM)
		return 0;
	bot->train_gate.button_touch_count = 1U;
	return 1;
}

int SG_TrainGateGameAuthorizeButtonUse(edict_t *button, edict_t *activator)
{
	sg_bot_t *bot = TrainBotForEntity(activator);
	sg_rune_mechanism_binding_t binding;

	if (!bot || !SG_TrainGateGameOwns(bot))
		return -1;
	return TrainActiveEvent(bot, button, &binding) ? 0 : -1;
}

int SG_TrainGateGameAuthorizeButtonShot(edict_t *button, edict_t *inflictor,
	edict_t *attacker, int damage)
{
	sg_bot_t *bot = TrainBotForEntity(attacker);
	sg_rune_mechanism_binding_t binding;

	if (!bot || !SG_TrainGateGameOwns(bot))
		return -1;
	if (!button || !inflictor || !TrainBinding(
	        (int)bot->train_gate.live.witness.link_index, 0, &binding) ||
	    binding.plan->controller_kind !=
	        SG_MECHANISM_CONTROLLER_TRAIN_SHOOT ||
	    binding.entry_entity != button || !TrainGuardAuthorize(bot, &binding) ||
	    bot->train_gate.live.phase != SG_TRAIN_GATE_APPROACH ||
	    bot->train_gate.live.shot_requested != 1U ||
	    bot->train_gate.button_shot_count != 0U ||
	    button->moveinfo.state != SG_PLAT_STATE_BOTTOM ||
	    button->max_health != 1 || button->health > 0 ||
	    button->takedamage != DAMAGE_YES || button->die != button_killed ||
	    damage != 15 || inflictor->owner != attacker || !inflictor->classname ||
	    strcmp(inflictor->classname, "bolt") || inflictor->dmg != 15 ||
	    inflictor->spawnflags != 0)
		return 0;
	bot->train_gate.button_shot_count = 1U;
	return 1;
}

int SG_TrainGateGameAuthorizeButtonTargets(edict_t *button,
	edict_t *activator)
{
	sg_bot_t *bot = TrainBotForEntity(activator);
	sg_rune_mechanism_binding_t binding;

	if (!bot || !SG_TrainGateGameOwns(bot))
		return -1;
	return TrainActiveEvent(bot, button, &binding) &&
	       TrainActivationCount(bot) == 1 &&
	       bot->train_gate.target_dispatch_count == 0U ? 1 : 0;
}

typedef struct train_dispatch_s
{
	edict_t *source;
	edict_t *activator;
} train_dispatch_t;

static int TrainDispatchTarget(void *context, edict_t *target,
	uint32_t target_key, uint32_t target_ordinal)
{
	train_dispatch_t *dispatch = context;

	(void)target_key;
	(void)target_ordinal;
	if (!dispatch || !target || !target->use)
		return 0;
	target->use(target, dispatch->source, dispatch->activator);
	return 1;
}

int SG_TrainGateGameHandleTargets(edict_t *source, edict_t *activator)
{
	sg_bot_t *bot = TrainBotForEntity(activator);
	sg_rune_mechanism_binding_t binding;
	train_dispatch_t dispatch;

	if (!bot || !SG_TrainGateGameOwns(bot))
		return 0;
	if (!TrainActiveEvent(bot, source, &binding) ||
	    TrainActivationCount(bot) != 1 ||
	    bot->train_gate.target_dispatch_count != 0U ||
	    bot->train_gate.train_use_count != 0U)
		return 1;
	bot->train_gate.target_dispatch_count = 1U;
	dispatch.source = source;
	dispatch.activator = activator;
	if (!SG_RuneMechanismBindingDispatchTargets(&binding,
	        binding.entry_node->key, TrainDispatchTarget, &dispatch) ||
	    bot->train_gate.train_use_count != 1U)
		bot->train_gate.live.phase = SG_TRAIN_GATE_FAILED;
	return 1;
}

int SG_TrainGateGameAuthorizeTrainUse(edict_t *train, edict_t *source,
	edict_t *activator)
{
	sg_bot_t *bot = TrainBotForEntity(activator);
	sg_rune_mechanism_binding_t binding;
	sg_mover_key_t key;
	int entity_key;

	if (!train || train->s.number <= 0 || train->s.number > UINT16_MAX)
		return 0;
	entity_key = train->s.number;
	key = (sg_mover_key_t)entity_key;
	if (!bot || !SG_TrainGateGameOwns(bot))
	{
		sg_compound_guard_result_t fence =
		    SG_CompoundGuardDoorPusherFence(&key, 1U);

		return fence == SG_COMPOUND_GUARD_OK ? 0 : -1;
	}
	if (!TrainBinding((int)bot->train_gate.live.witness.link_index, 1,
	    &binding) || binding.mover_entity != train ||
	    binding.entry_entity != source || !TrainGuardAuthorize(bot, &binding) ||
	    TrainActivationCount(bot) != 1 ||
	    bot->train_gate.target_dispatch_count != 1U ||
	    bot->train_gate.train_use_count != 0U)
		return 0;
	bot->train_gate.train_use_count = 1U;
	return 1;
}
