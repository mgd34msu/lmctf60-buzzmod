/* Passive real-edict adapter for authenticated continuous station trains. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_train_station_game.h"
#include "slipgate/sg_train_station_plan.h"
#include "slipgate/sg_util.h"

#include <math.h>
#include <string.h>

#define SG_TRAIN_STATION_STEP_MS 25U
#define SG_TRAIN_STATION_FRAME_STEPS 4
#define SG_TRAIN_STATION_START_ON 1
#define SG_TRAIN_STATION_POSE_EPSILON 0.125f
#define SG_TRAIN_STATION_DIRECTION_SLOP 0.000001f
#define SG_TRAIN_STATION_FRAME_EPSILON 0.001f

void ClientThink(edict_t *ent, usercmd_t *ucmd);
void Move_Final(edict_t *ent);
void Move_Done(edict_t *ent);
void train_next(edict_t *ent);

static int StationSelected(int link_index)
{
	rune_t *rune = SG_Rune();
	const rune_mechanism_plan_t *plan;

	if (!rune || !rune->links || !rune->mechanism_plans ||
	    link_index < 0 || link_index >= rune->hdr.num_links ||
	    rune->links[link_index].action != RL_TRAIN ||
	    rune->links[link_index].mode != RLCM_RIDE ||
	    rune->links[link_index].mechanism_plan == RUNE_NO_MECHANISM_PLAN ||
	    rune->links[link_index].mechanism_plan >=
	        rune->artifact.num_mechanism_plans)
		return 0;
	plan = &rune->mechanism_plans[
		rune->links[link_index].mechanism_plan];
	return plan->controller_kind ==
		SG_MECHANISM_CONTROLLER_TRAIN_STATION;
}

static int StationBinding(uint32_t link_index,
	sg_rune_mechanism_binding_t *binding)
{
	rune_t *rune = SG_Rune();

	if (binding)
		memset(binding, 0, sizeof(*binding));
	return rune && binding && link_index < (uint32_t)rune->hdr.num_links &&
	       SG_RuneMechanismStationBindingCapture(rune, link_index, binding) &&
	       binding->rune == rune && binding->link && binding->plan &&
	       binding->entry_node && binding->mover_node &&
	       binding->destination_node && binding->egress_node &&
	       binding->entry_entity && binding->mover_entity &&
	       binding->destination_entity && binding->egress_entity &&
	       binding->link->action == RL_TRAIN &&
	       binding->link->mode == RLCM_RIDE &&
	       binding->plan->controller_kind ==
	           SG_MECHANISM_CONTROLLER_TRAIN_STATION &&
	       binding->plan->expected_members == 2U &&
	       binding->plan->cooldown_ms == 3000U;
}

static int StationRouteIndex(const sg_train_station_game_state_t *state,
	uint32_t key)
{
	unsigned int index;

	if (!state)
		return -1;
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
		if (state->route_keys[index] == key)
			return (int)index;
	return -1;
}

static int StationMoving(const edict_t *train)
{
	return train && (train->velocity[0] != 0.0f ||
		train->velocity[1] != 0.0f || train->velocity[2] != 0.0f);
}

static int StationTrainPoseCurrent(const edict_t *train, int moving,
	const edict_t *predecessor)
{
	vec3_t target;
	vec3_t segment;
	vec3_t progress;
	vec3_t residual;
	float segment_squared = 0.0f;
	float progress_dot = 0.0f;
	float velocity_dot = 0.0f;
	float velocity_squared = 0.0f;
	float residual_squared = 0.0f;
	float fraction;
	float fraction_slop;
	float speed;
	float expected_speed;
	float distance_to_end_squared = 0.0f;
	float distance_to_end;
	float scheduled_frames;
	float rounded_frames;
	int axis;

	if (!train || !train->target_ent || !train->target ||
	    !train->target_ent->target ||
	    Q_stricmp(train->target, train->target_ent->target) != 0 ||
	    !isfinite(train->moveinfo.wait) ||
	    !isfinite(train->target_ent->wait) ||
	    fabsf(train->moveinfo.wait - train->target_ent->wait) >
	        SG_TRAIN_STATION_FRAME_EPSILON)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		target[axis] = train->target_ent->s.origin[axis] -
			train->mins[axis];
		if (!isfinite(target[axis]) ||
		    fabsf((moving ? train->moveinfo.end_origin[axis]
		                  : train->s.origin[axis]) - target[axis]) >
		        SG_TRAIN_STATION_POSE_EPSILON)
			return 0;
	}
	if (!moving)
		return train->moveinfo.wait > 0.0f && train->think == train_next &&
		       isfinite(train->nextthink) && train->nextthink > level.time &&
		       train->nextthink - level.time <= train->moveinfo.wait +
		           SG_TRAIN_STATION_FRAME_EPSILON;
	if (!predecessor || !predecessor->inuse || predecessor->s.number <= 0)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		if (!isfinite(predecessor->s.origin[axis]) ||
		    fabsf(train->moveinfo.start_origin[axis] -
		        (predecessor->s.origin[axis] - train->mins[axis])) >
		        SG_TRAIN_STATION_POSE_EPSILON)
			return 0;
		segment[axis] = train->moveinfo.end_origin[axis] -
			train->moveinfo.start_origin[axis];
		progress[axis] = train->s.origin[axis] -
			train->moveinfo.start_origin[axis];
		if (!isfinite(segment[axis]) || !isfinite(progress[axis]) ||
		    !isfinite(train->velocity[axis]))
			return 0;
		segment_squared += segment[axis] * segment[axis];
		progress_dot += progress[axis] * segment[axis];
		velocity_dot += train->velocity[axis] * segment[axis];
		velocity_squared += train->velocity[axis] * train->velocity[axis];
		distance_to_end_squared +=
			(train->moveinfo.end_origin[axis] - train->s.origin[axis]) *
			(train->moveinfo.end_origin[axis] - train->s.origin[axis]);
	}
	if (segment_squared <= SG_TRAIN_STATION_POSE_EPSILON *
	        SG_TRAIN_STATION_POSE_EPSILON || velocity_dot <= 0.0f ||
	    velocity_squared <= 0.0f)
		return 0;
	speed = sqrtf(velocity_squared);
	distance_to_end = sqrtf(distance_to_end_squared);
	if (!isfinite(speed) || !isfinite(train->moveinfo.speed) ||
	    !isfinite(distance_to_end) || train->moveinfo.speed <= 0.0f ||
	    !isfinite(train->nextthink) || !isfinite(level.time))
		return 0;
	if (train->think == Move_Done)
	{
		if (!isfinite(train->moveinfo.remaining_distance) ||
		    train->moveinfo.remaining_distance <= 0.0f ||
		    train->moveinfo.remaining_distance >
		        train->moveinfo.speed * FRAMETIME +
		            SG_TRAIN_STATION_POSE_EPSILON)
			return 0;
		expected_speed = train->moveinfo.remaining_distance / FRAMETIME;
		if (fabsf(distance_to_end -
		        train->moveinfo.remaining_distance) >
		        SG_TRAIN_STATION_POSE_EPSILON ||
		    fabsf(train->nextthink - level.time - FRAMETIME) >
		        SG_TRAIN_STATION_FRAME_EPSILON)
			return 0;
	}
	else if (train->think == Move_Final)
	{
		expected_speed = train->moveinfo.speed;
		if (!isfinite(train->moveinfo.remaining_distance) ||
		    train->moveinfo.remaining_distance < 0.0f ||
		    train->moveinfo.remaining_distance >
		        train->moveinfo.speed * FRAMETIME +
		            SG_TRAIN_STATION_POSE_EPSILON)
			return 0;
		scheduled_frames = (train->nextthink - level.time) / FRAMETIME;
		rounded_frames = roundf(scheduled_frames);
		if (!isfinite(scheduled_frames) || rounded_frames < 1.0f ||
		    fabsf(scheduled_frames - rounded_frames) >
		        SG_TRAIN_STATION_FRAME_EPSILON ||
		    fabsf(distance_to_end -
		        (rounded_frames * train->moveinfo.speed * FRAMETIME +
		         train->moveinfo.remaining_distance)) >
		        SG_TRAIN_STATION_POSE_EPSILON)
			return 0;
	}
	else
		return 0;
	if (fabsf(speed - expected_speed) > SG_TRAIN_STATION_POSE_EPSILON)
		return 0;
	fraction = progress_dot / segment_squared;
	fraction_slop = SG_TRAIN_STATION_POSE_EPSILON /
		sqrtf(segment_squared);
	if (!isfinite(fraction) || !isfinite(fraction_slop) ||
	    fraction < -fraction_slop || fraction > 1.0f + fraction_slop)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		residual[axis] = progress[axis] - fraction * segment[axis];
		residual_squared += residual[axis] * residual[axis];
	}
	if (residual_squared > SG_TRAIN_STATION_POSE_EPSILON *
	        SG_TRAIN_STATION_POSE_EPSILON ||
	    velocity_dot * velocity_dot < velocity_squared * segment_squared *
	        (1.0f - SG_TRAIN_STATION_DIRECTION_SLOP))
		return 0;
	return 1;
}

static edict_t *StationRideTrain(
	const sg_train_station_game_state_t *state,
	const sg_rune_mechanism_binding_t *binding)
{
	if (!state || !binding)
		return NULL;
	if (state->ride_key == state->master_key)
		return binding->mover_entity;
	if (state->ride_key == state->member_key)
		return binding->egress_entity;
	return NULL;
}

static int StationTrainPairCurrent(
	const sg_train_station_game_state_t *state,
	const sg_rune_mechanism_binding_t *binding, uint32_t *corner_key_out,
	uint32_t *train_identity_out, int *moving_out)
{
	edict_t *master;
	edict_t *companion;
	uint32_t master_key;
	uint32_t companion_key;
	uint32_t master_generation;
	uint32_t member_generation;
	int master_index;
	int companion_index;
	int master_moving;
	int companion_moving;
	edict_t *ride;
	edict_t *master_predecessor;
	edict_t *companion_predecessor;
	uint32_t ride_generation;

	if (corner_key_out)
		*corner_key_out = state ? state->route_keys[0] : 0U;
	if (train_identity_out)
		*train_identity_out = 0U;
	if (moving_out)
		*moving_out = 0;
	if (!state || !binding || !corner_key_out || !train_identity_out ||
	    !moving_out || !(master = binding->mover_entity) ||
	    !(companion = binding->egress_entity) || !master->target_ent ||
	    !companion->target_ent ||
	    master->spawnflags != SG_TRAIN_STATION_START_ON ||
	    companion->spawnflags != SG_TRAIN_STATION_START_ON ||
	    master->movetype != MOVETYPE_PUSH ||
	    companion->movetype != MOVETYPE_PUSH || master->solid != SOLID_BSP ||
	    companion->solid != SOLID_BSP || master->teammaster != master ||
	    master->teamchain != companion || companion->teammaster != master ||
	    companion->teamchain != NULL ||
	    (master->flags & FL_TEAMSLAVE) != 0 ||
	    (companion->flags & FL_TEAMSLAVE) == 0 ||
	    !SG_MechCatalogEntityGeneration(master, &master_key,
	        &master_generation) ||
	    !SG_MechCatalogEntityGeneration(companion, &companion_key,
	        &member_generation) || master_key != state->master_key ||
	    companion_key != state->member_key ||
	    master_generation != state->master_generation ||
	    member_generation != state->member_generation ||
	    master->target_ent->s.number <= 0 ||
	    companion->target_ent->s.number <= 0)
		return 0;
	ride = StationRideTrain(state, binding);
	if (!ride)
		return 0;
	ride_generation = ride == master ? master_generation : member_generation;
	master_index = StationRouteIndex(state,
		(uint32_t)master->target_ent->s.number);
	companion_index = StationRouteIndex(state,
		(uint32_t)companion->target_ent->s.number);
	if (master_index < 0 || companion_index < 0)
		return 0;
	if (!g_edicts ||
	    state->route_keys[(master_index +
	        (int)SG_TRAIN_STATION_ROUTE_CORNERS - 1) %
	        (int)SG_TRAIN_STATION_ROUTE_CORNERS] >=
	        (uint32_t)globals.num_edicts ||
	    state->route_keys[(companion_index +
	        (int)SG_TRAIN_STATION_ROUTE_CORNERS - 1) %
	        (int)SG_TRAIN_STATION_ROUTE_CORNERS] >=
	        (uint32_t)globals.num_edicts)
		return 0;
	master_predecessor = &g_edicts[state->route_keys[(master_index +
		(int)SG_TRAIN_STATION_ROUTE_CORNERS - 1) %
		(int)SG_TRAIN_STATION_ROUTE_CORNERS]];
	companion_predecessor = &g_edicts[state->route_keys[(companion_index +
		(int)SG_TRAIN_STATION_ROUTE_CORNERS - 1) %
		(int)SG_TRAIN_STATION_ROUTE_CORNERS]];
	master_moving = StationMoving(master);
	companion_moving = StationMoving(companion);
	if (!StationTrainPoseCurrent(master, master_moving,
	        master_predecessor) ||
	    !StationTrainPoseCurrent(companion, companion_moving,
	        companion_predecessor))
		return 0;
	*corner_key_out = (uint32_t)ride->target_ent->s.number;
	*train_identity_out = ride_generation;
	*moving_out = ride == master ? master_moving : companion_moving;
	return 1;
}

static int StationBodyClear(const edict_t *body, const edict_t *train)
{
	return body && train &&
	       (body->absmax[0] <= train->absmin[0] ||
	        body->absmin[0] >= train->absmax[0] ||
	        body->absmax[1] <= train->absmin[1] ||
	        body->absmin[1] >= train->absmax[1] ||
	        body->absmax[2] <= train->absmin[2] ||
	        body->absmin[2] >= train->absmax[2]);
}

static void StationObservation(sg_bot_t *bot,
	sg_train_station_observation_t *observation)
{
	sg_train_station_game_state_t *state;
	sg_rune_mechanism_binding_t binding;
	edict_t *entity;
	uint32_t corner_key;
	uint32_t train_identity;
	int moving;
	int binding_current;
	int train_current;

	memset(observation, 0, sizeof(*observation));
	if (!bot || !(state = &bot->train_station) ||
	    !(entity = bot->ent))
		return;
	observation->frame = level.framenum >= 0
		? (uint32_t)level.framenum : 0U;
	observation->source_key = state->transaction.spec.source_key;
	observation->binding_identity =
		state->transaction.spec.binding_identity;
	observation->fanout_identity = state->transaction.spec.fanout_identity;
	observation->train_identity = state->transaction.spec.train_identity;
	observation->train_corner_key = state->route_keys[0];
	observation->alive = entity->inuse && entity->client &&
		entity->deadflag == DEAD_NO && entity->health > 0;
	observation->connected = entity->inuse && entity->client;
	binding_current = state->rune == SG_Rune() &&
		StationBinding(state->link_index, &binding) &&
		binding.rune == state->rune &&
		binding.mover_node->key == state->master_key &&
		binding.egress_node->key == state->member_key &&
		memcmp(binding.link->anchor, state->approach,
			sizeof(state->approach)) == 0 &&
		memcmp(binding.link->mechanism_anchor, state->boarding,
			sizeof(state->boarding)) == 0;
	observation->binding_current = binding_current != 0;
	if (!binding_current)
		return;
	train_current = StationTrainPairCurrent(state, &binding, &corner_key,
		&train_identity, &moving);
	if (!train_current)
	{
		observation->train_identity = 0U;
		return;
	}
	observation->train_identity = train_identity;
	observation->train_corner_key = corner_key;
	observation->train_moving = moving != 0;
	observation->body_aboard = SG_LiftRider(
		StationRideTrain(state, &binding), entity) != 0;
	observation->body_clear = StationBodyClear(entity,
		StationRideTrain(state, &binding)) != 0;
	observation->egress_arrived = !observation->body_aboard &&
		SG_SupportedArrived(entity->s.origin, state->destination,
		    entity->groundentity != NULL, entity->watertype,
		    entity->waterlevel, entity);
}

static void StationFail(sg_bot_t *bot, sg_train_station_reason_t reason)
{
	if (!bot)
		return;
	bot->train_station.transaction.phase = SG_TRAIN_STATION_FAILED;
	bot->train_station.transaction.reason = reason;
	bot->train_station.active = 0U;
	bot->commit_link = -1;
}

static int StationBoardingPathEnsure(sg_bot_t *bot)
{
	sg_train_station_game_state_t *state;
	sg_rune_mechanism_binding_t binding;
	edict_t *train;
	uint32_t corner_key;
	uint32_t train_identity;
	int moving;

	if (!bot || !(state = &bot->train_station))
		return 0;
	if (state->boarding_path_ready)
		return state->boarding_path.count > 0U &&
		       state->boarding_path.next < state->boarding_path.count;
	if (!StationBinding(state->link_index, &binding) ||
	    !StationTrainPairCurrent(state, &binding, &corner_key,
	        &train_identity, &moving) || moving ||
	    corner_key != state->route_keys[0] ||
	    !(train = StationRideTrain(state, &binding)) ||
	    !SG_TrainStationApproachPathBuild(state->approach, state->boarding,
	        &state->boarding_path))
		return 0;
	state->boarding_path_ready = 1U;
	return 1;
}

static int StationSamePoint(const float left[3], const float right[3])
{
	return left[0] == right[0] && left[1] == right[1] &&
	       left[2] == right[2];
}

static int StationBegin(sg_bot_t *bot, int link_index)
{
	sg_train_station_game_state_t *state;
	sg_rune_mechanism_binding_t binding;
	sg_train_station_plan_witness_t witness;
	sg_mech_catalog_view_t view;
	sg_train_station_spec_t spec;
	sg_train_station_observation_t observation;
	uint32_t destination_key;
	uint32_t companion_key;
	uint32_t mover_key;
	uint32_t mover_generation;
	uint32_t observed_companion_key;
	uint32_t companion_generation;
	uint32_t corner_key;
	uint32_t train_identity;
	uint32_t route_offset;
	uint32_t index;
	float entry_distance;
	float destination_distance;
	int moving;
	rune_t *rune = SG_Rune();

	if (!bot || !rune ||
	    rune->artifact.identity.server_frame_ms != 100U ||
	    !StationBinding((uint32_t)link_index, &binding) ||
	    !rune->seeds || binding.link->from < 0 || binding.link->to < 0 ||
	    binding.link->from >= rune->hdr.num_seeds ||
	    binding.link->to >= rune->hdr.num_seeds)
		return 0;
	memset(&view, 0, sizeof(view));
	view.nodes = rune->mechanism_nodes;
	view.num_nodes = rune->artifact.num_mechanism_nodes;
	view.edges = rune->mechanism_edges;
	view.num_edges = rune->artifact.num_inventory_edges;
	view.strings = rune->mechanism_strings;
	view.string_bytes = rune->artifact.string_bytes;
	if (!SG_TrainStationPlanDiscover(&view, binding.entry_node->key,
	        binding.mover_node->key, &destination_key, &companion_key,
	        &witness) || destination_key != binding.destination_node->key ||
	    companion_key != binding.egress_node->key ||
	    witness.route_count != SG_TRAIN_STATION_ROUTE_CORNERS ||
	    !SG_MechCatalogEntityGeneration(binding.mover_entity, &mover_key,
	        &mover_generation) ||
	    !SG_MechCatalogEntityGeneration(binding.egress_entity,
	        &observed_companion_key, &companion_generation) ||
	    mover_key != binding.mover_node->key ||
	    observed_companion_key != companion_key)
		return 0;
	state = &bot->train_station;
	memset(state, 0, sizeof(*state));
	state->rune = rune;
	state->link_index = (uint32_t)link_index;
	state->master_key = mover_key;
	state->member_key = companion_key;
	state->master_generation = mover_generation;
	state->member_generation = companion_generation;
	entry_distance = 0.0f;
	destination_distance = 0.0f;
	for (index = 0U; index < 3U; index++)
	{
		float entry_delta = rune->seeds[binding.link->from].origin[index] -
			binding.entry_entity->s.origin[index];
		float destination_delta =
			rune->seeds[binding.link->from].origin[index] -
			binding.destination_entity->s.origin[index];

		entry_distance += entry_delta * entry_delta;
		destination_distance += destination_delta * destination_delta;
	}
	if (!isfinite(entry_distance) || !isfinite(destination_distance) ||
	    entry_distance == destination_distance)
		goto fail;
	route_offset = destination_distance < entry_distance
		? SG_TRAIN_STATION_ROUTE_CORNERS / 2U : 0U;
	state->ride_key = route_offset == 0U ? mover_key : companion_key;
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
		state->route_keys[index] = witness.route_keys[(route_offset + index) %
			SG_TRAIN_STATION_ROUTE_CORNERS];
	VectorCopy(rune->seeds[binding.link->from].origin, state->source);
	VectorCopy(binding.link->anchor, state->approach);
	VectorCopy(binding.link->mechanism_anchor, state->boarding);
	VectorCopy(rune->seeds[binding.link->to].origin, state->destination);
	if (!isfinite(state->approach[0]) || !isfinite(state->approach[1]) ||
	    !isfinite(state->approach[2]) ||
	    StationSamePoint(state->approach, state->source) ||
	    StationSamePoint(state->approach, state->boarding) ||
	    !SG_TrainStationApproachPathBuild(state->source, state->approach,
	        &state->approach_path))
		goto fail;
	if (!StationTrainPairCurrent(state, &binding, &corner_key,
	        &train_identity, &moving) || !moving)
		goto fail;
	memset(&spec, 0, sizeof(spec));
	spec.source_key = state->route_keys[0];
	spec.binding_identity = rune->artifact.mechanism_contract_crc32;
	spec.fanout_identity = binding.plan->closure_crc32;
	spec.train_identity = route_offset == 0U
		? mover_generation : companion_generation;
	memcpy(spec.route_corner_keys, state->route_keys,
		sizeof(spec.route_corner_keys));
	spec.upper_station_key = state->route_keys[0];
	spec.lower_station_key = state->route_keys[
		SG_TRAIN_STATION_ROUTE_CORNERS / 2U];
	spec.upper_dwell_frames = SG_TRAIN_STATION_DWELL_FRAMES;
	spec.lower_dwell_frames = SG_TRAIN_STATION_DWELL_FRAMES;
	spec.route_corner_count = SG_TRAIN_STATION_ROUTE_CORNERS;
	spec.start_on = 1U;
	spec.boarding_station = SG_TRAIN_STATION_UPPER;
	state->transaction.spec = spec;
	StationObservation(bot, &observation);
	if (!SG_TrainStationTransactionBegin(&state->transaction, &spec,
	        &observation))
		goto fail;
	state->active = 1U;
	bot->commit_link = link_index;
	return 1;

fail:
	memset(state, 0, sizeof(*state));
	return 0;
}

int SG_TrainStationGameOwns(const sg_bot_t *bot)
{
	return bot && bot->train_station.active == 1U &&
	       bot->train_station.transaction.phase >=
	           SG_TRAIN_STATION_WAIT_SOURCE &&
	       bot->train_station.transaction.phase <= SG_TRAIN_STATION_FAILED;
}

void SG_TrainStationGameReset(sg_bot_t *bot)
{
	int owned;

	if (!bot)
		return;
	owned = bot->train_station.active == 1U &&
		bot->commit_link == (int)bot->train_station.link_index;
	memset(&bot->train_station, 0, sizeof(bot->train_station));
	if (owned)
		bot->commit_link = -1;
}

int SG_TrainStationGameEmit(sg_bot_t *bot, int selected_link)
{
	sg_train_station_observation_t observation;
	sg_train_station_command_t control;
	edict_t *entity;
	int step;

	if (!bot || !(entity = bot->ent) || !entity->client)
		return 0;
	if (!SG_TrainStationGameOwns(bot))
	{
		sg_rune_mechanism_binding_t binding;

		if (!StationSelected(selected_link))
			return 0;
		if (!StationBinding((uint32_t)selected_link, &binding))
		{
			bot->commit_link = -1;
			return 1;
		}
		if (!StationBegin(bot, selected_link))
		{
			bot->commit_link = -1;
			return 1;
		}
	}
	StationObservation(bot, &observation);
	if (!bot->train_station.approach_reached &&
	    SG_SupportedArrived(entity->s.origin, bot->train_station.approach,
	        entity->groundentity != NULL, entity->watertype,
	        entity->waterlevel, entity))
		bot->train_station.approach_reached = 1U;
	control = SG_TrainStationTransactionStep(
		&bot->train_station.transaction, &observation);
	if (bot->train_station.transaction.phase == SG_TRAIN_STATION_FAILED)
	{
		bot->train_station.active = 0U;
		bot->commit_link = -1;
		return 1;
	}
	if (bot->train_station.transaction.phase == SG_TRAIN_STATION_COMPLETE)
	{
		bot->train_station.active = 0U;
		bot->commit_link = -1;
		return 1;
	}
	for (step = 0; step < SG_TRAIN_STATION_FRAME_STEPS; step++)
	{
		usercmd_t command;
		vec3_t board_target;
		const float *target = NULL;

		memset(&command, 0, sizeof(command));
		command.msec = SG_TRAIN_STATION_STEP_MS;
		if (control == SG_TRAIN_STATION_COMMAND_WAIT)
		{
			if (!bot->train_station.approach_reached)
			{
				SG_TrainStationBoardPathNextTarget(entity->s.origin,
				    &bot->train_station.approach_path, board_target);
				target = board_target;
			}
			else
				target = bot->train_station.approach;
		}
		else if (control == SG_TRAIN_STATION_COMMAND_BOARD)
		{
			if (!bot->train_station.approach_reached)
			{
				SG_TrainStationBoardPathNextTarget(entity->s.origin,
				    &bot->train_station.approach_path, board_target);
				target = board_target;
			}
			else if (!StationBoardingPathEnsure(bot))
			{
				StationFail(bot, SG_TRAIN_STATION_REASON_INVALID);
				return 1;
			}
			else
			{
				SG_TrainStationBoardPathNextTarget(entity->s.origin,
				    &bot->train_station.boarding_path, board_target);
				target = board_target;
			}
		}
		else if (control == SG_TRAIN_STATION_COMMAND_EGRESS)
			target = bot->train_station.destination;
		else if (control != SG_TRAIN_STATION_COMMAND_HOLD)
		{
			StationFail(bot, SG_TRAIN_STATION_REASON_INVALID);
			return 1;
		}
		if (target && !SG_DeclaredCommand(entity->s.origin, target,
		        &entity->client->ps.pmove, &command))
		{
			StationFail(bot, SG_TRAIN_STATION_REASON_INVALID);
			return 1;
		}
		ClientThink(entity, &command);
	}
	return 1;
}
