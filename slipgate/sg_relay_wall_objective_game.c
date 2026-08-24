#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_relay_wall_objective_game.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define RELAY_WALL_STEP_MS 25
#define RELAY_WALL_APPROACH_MS 3000

typedef struct relay_wall_pose_s
{
	edict_t *entity;
	int solid;
	int svflags;
} relay_wall_pose_t;

typedef struct relay_wall_game_context_s
{
	const sg_relay_wall_objective_game_request_t *request;
	const rune_mechanism_node_t *entry;
} relay_wall_game_context_t;


static int RelayWallGameApproach(const vec3_t source, edict_t *button,
	uint32_t dwell_ms, vec3_t anchor_out, uint32_t *arrival_ms_out);

static const rune_mechanism_node_t *RelayWallGameNode(
	const sg_mech_catalog_view_t *catalog, uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high = catalog ? catalog->num_nodes : 0U;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (catalog->nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	return catalog && catalog->nodes && low < catalog->num_nodes &&
	       catalog->nodes[low].key == key ? &catalog->nodes[low] : NULL;
}

static edict_t *RelayWallGameEntity(uint32_t key)
{
	return g_edicts && globals.edicts == g_edicts && key > 0U &&
	       key < (uint32_t)globals.num_edicts ? &g_edicts[key] : NULL;
}

static int RelayWallGameEligible(void *raw, uint32_t seed, int source)
{
	relay_wall_game_context_t *context = raw;
	const sg_relay_wall_objective_game_request_t *request = context->request;

	if (seed >= request->seed_count || !request->source_stable[seed] ||
	    request->source_waterlevel[seed] != 0 ||
	    request->source_watertype[seed] != 0)
		return 0;
	if (source && (!context->entry ||
	    !SG_RelayWallSourceContactElevation(context->entry,
	        request->seeds[seed].origin[2])))
		return 0;
	return source ? request->has_incoming(request->context, seed)
	              : request->has_outgoing(request->context, seed);
}


static int RelayWallGameDiscover(void *raw,
	const sg_mech_catalog_view_t *catalog, uint32_t entry_key,
	sg_relay_wall_plan_witness_t *witness_out)
{
	relay_wall_game_context_t *context = raw;
	int discovered;

	discovered = SG_RelayWallPlanDiscover(catalog, entry_key, witness_out);
	if (discovered)
	{
		context->entry = RelayWallGameNode(catalog, entry_key);
		return 1;
	}
	return discovered;
}

static int RelayWallGameCurrent(
	const sg_relay_wall_objective_game_request_t *request,
	const sg_relay_wall_plan_witness_t *witness)
{
	const rune_mechanism_node_t *entry;
	const rune_mechanism_node_t *wall;
	const rune_mechanism_node_t *immediate;
	const rune_mechanism_node_t *restore;

	entry = RelayWallGameNode(request->catalog, witness->entry_key);
	wall = RelayWallGameNode(request->catalog, witness->wall_key);
	immediate = RelayWallGameNode(request->catalog,
		witness->immediate_relay_key);
	restore = RelayWallGameNode(request->catalog, witness->restore_relay_key);
	return entry && wall && immediate && restore &&
	       SG_MechCatalogEntityExecutionMatches(entry->key, entry,
	           SG_MECHANISM_CONTROLLER_RELAY_DOOR) &&
	       SG_MechCatalogEntityExecutionMatches(wall->key, wall,
	           SG_MECHANISM_CONTROLLER_RELAY_DOOR) &&
	       SG_MechCatalogEntityExecutionMatches(immediate->key, immediate,
	           SG_MECHANISM_CONTROLLER_RELAY_DOOR) &&
	       SG_MechCatalogEntityExecutionMatches(restore->key, restore,
	           SG_MECHANISM_CONTROLLER_RELAY_DOOR);
}

static int RelayWallGameContact(edict_t *button, const vec3_t origin)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t center;
	vec3_t direction;
	vec3_t end;
	trace_t trace;
	float length;
	int axis;

	if (!button || !button->inuse || !button->classname ||
	    strcmp(button->classname, "func_button") != 0)
		return 0;
	for (axis = 0; axis < 3; axis++)
		center[axis] = 0.5f * (button->absmin[axis] + button->absmax[axis]);
	VectorSubtract(center, origin, direction);
	length = VectorLength(direction);
	if (!isfinite(length) || length <= 0.01f)
		return 0;
	VectorScale(direction, 4.0f / length, direction);
	VectorAdd(origin, direction, end);
	trace = sg_host.trace((vec_t *)origin, mins, maxs, end, NULL,
		MASK_PLAYERSOLID);
	return !trace.startsolid && !trace.allsolid && trace.fraction < 1.0f &&
	       trace.ent == button;
}

static void RelayWallGameCommand(const vec3_t origin, const vec3_t target,
	usercmd_t *command)
{
	static const float fan[] = { 0.0f, -35.0f, 35.0f, -75.0f, 75.0f };
	vec3_t delta;
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	float yaw;
	float chosen;
	float best = -1.0f;
	size_t index;

	VectorSubtract(target, origin, delta);
	yaw = atan2f(delta[1], delta[0]);
	chosen = yaw;
	for (index = 0U; index < sizeof(fan) / sizeof(fan[0]); index++)
	{
		vec3_t direction;
		vec3_t probe;
		trace_t trace;
		float candidate = yaw + fan[index] * (float)(M_PI / 180.0);
		float score;

		direction[0] = cosf(candidate);
		direction[1] = sinf(candidate);
		direction[2] = 0.0f;
		VectorMA((vec_t *)origin, 80.0f, direction, probe);
		probe[2] += 8.0f;
		trace = sg_host.trace((vec_t *)origin, mins, maxs, probe, NULL,
			MASK_PLAYERSOLID);
		score = trace.fraction - 0.06f * (float)index;
		if (score > best)
		{
			best = score;
			chosen = candidate;
		}
		if (index == 0U && trace.fraction >= 1.0f)
			break;
	}
	memset(command, 0, sizeof(*command));
	command->msec = RELAY_WALL_STEP_MS;
	command->angles[YAW] = ANGLE2SHORT(chosen * 180.0f / (float)M_PI);
	command->forwardmove = 400;
}

static int RelayWallGameApproach(const vec3_t source, edict_t *button,
	uint32_t dwell_ms, vec3_t anchor_out, uint32_t *arrival_ms_out)
{
	sg_phantom_t phantom;
	vec3_t button_center;
	int elapsed;
	int axis;

	if (dwell_ms == 0U || dwell_ms % RELAY_WALL_STEP_MS != 0U)
		return 0;
	for (axis = 0; axis < 3; axis++)
		button_center[axis] = 0.5f *
			(button->absmin[axis] + button->absmax[axis]);

	SG_OraclePlace(&phantom, (vec_t *)source);
	phantom.groundentity = true;
	for (elapsed = 0; elapsed < RELAY_WALL_APPROACH_MS;
	     elapsed += RELAY_WALL_STEP_MS)
	{
		usercmd_t command;

		if ((elapsed % 100) == 0 && phantom.groundentity &&
		    phantom.waterlevel == 0 &&
		    RelayWallGameContact(button, phantom.origin))
		{
			uint32_t dwell;

			for (dwell = 0U; dwell < dwell_ms;
			     dwell += RELAY_WALL_STEP_MS)
			{
				usercmd_t zero;

				memset(&zero, 0, sizeof(zero));
				zero.msec = RELAY_WALL_STEP_MS;
				if (!SG_OracleRunWorld(&phantom, &zero, 1))
					return 0;
			}
			VectorCopy(phantom.origin, anchor_out);
			*arrival_ms_out = (uint32_t)elapsed;
			return elapsed > 0;
		}
		RelayWallGameCommand(phantom.origin, button_center, &command);
		if (!SG_OracleRunWorld(&phantom, &command, 1))
			return 0;
	}
	return 0;
}

static int RelayWallSegmentBox(const vec3_t start, const vec3_t end,
	const vec3_t mins, const vec3_t maxs)
{
	double low = 0.0;
	double high = 1.0;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		double delta = (double)end[axis] - (double)start[axis];
		double first;
		double second;

		if (fabs(delta) < 1e-9)
		{
			if (start[axis] < mins[axis] || start[axis] > maxs[axis])
				return 0;
			continue;
		}
		first = ((double)mins[axis] - (double)start[axis]) / delta;
		second = ((double)maxs[axis] - (double)start[axis]) / delta;
		if (first > second)
		{
			double swap = first;

			first = second;
			second = swap;
		}
		if (first > low)
			low = first;
		if (second < high)
			high = second;
		if (low > high)
			return 0;
	}
	return 1;
}

static int RelayWallGameEgress(const vec3_t anchor, const vec3_t destination,
	const rune_mechanism_node_t *wall, uint32_t window_ms,
	uint32_t *arrival_ms_out)
{
	sg_phantom_t phantom;
	vec3_t closed_mins;
	vec3_t closed_maxs;
	int elapsed;
	int crossed = 0;
	int axis;

	if (!SG_RelayWallNodeBounds(wall, closed_mins, closed_maxs))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		closed_mins[axis] -= axis == 2 ? 32.0f : 16.0f;
		closed_maxs[axis] += axis == 2 ? 24.0f : 16.0f;
	}
	SG_OraclePlace(&phantom, (vec_t *)anchor);
	phantom.groundentity = true;
	for (elapsed = 0; elapsed < (int)window_ms; elapsed += RELAY_WALL_STEP_MS)
	{
		vec3_t before;
		vec3_t delta;
		usercmd_t command;

		VectorSubtract(destination, phantom.origin, delta);
		if ((elapsed % 100) == 0 && elapsed > 0 && crossed &&
		    delta[0] * delta[0] + delta[1] * delta[1] <= 48.0f * 48.0f &&
		    fabsf(delta[2]) < 72.0f && phantom.groundentity &&
		    phantom.waterlevel == 0 &&
		    !RelayWallSegmentBox(phantom.origin, phantom.origin,
		        closed_mins, closed_maxs))
		{
			*arrival_ms_out = (uint32_t)elapsed;
			return 1;
		}
		VectorCopy(phantom.origin, before);
		RelayWallGameCommand(phantom.origin, destination, &command);
		if (!SG_OracleRunWorld(&phantom, &command, 1))
			return 0;
		if (RelayWallSegmentBox(before, phantom.origin,
		        closed_mins, closed_maxs))
			crossed = 1;
	}
	return 0;
}

static int RelayWallGamePoseOpen(
	const sg_relay_wall_objective_game_request_t *request,
	const sg_relay_wall_plan_witness_t *witness, relay_wall_pose_t *poses,
	uint32_t *pose_count_out)
{
	uint32_t edge_index;
	uint32_t count = 0U;

	*pose_count_out = 0U;

	for (edge_index = 0U; edge_index < request->catalog->num_edges;
	     edge_index++)
	{
		const rune_mechanism_edge_t *edge =
			&request->catalog->edges[edge_index];
		const rune_mechanism_node_t *node;
		edict_t *entity;

		if (edge->from_key != witness->immediate_relay_key ||
		    edge->kind != SG_MECH_EDGE_TARGET)
			continue;
		node = RelayWallGameNode(request->catalog, edge->to_key);
		if (!node || (node->kind != SG_MECH_NODE_TOGGLE_WALL &&
		              node->kind != SG_MECH_NODE_TRIGGER_HURT))
			continue;
		if (count >= 2U || !(entity = RelayWallGameEntity(node->key)) ||
		    !SG_MechCatalogEntityExecutionMatches(node->key, node,
		        SG_MECHANISM_CONTROLLER_RELAY_DOOR))
			return 0;
		poses[count].entity = entity;
		poses[count].solid = entity->solid;
		poses[count].svflags = entity->svflags;
		entity->solid = SOLID_NOT;
		entity->svflags |= SVF_NOCLIENT;
		sg_host.linkentity(entity);
		count++;
		*pose_count_out = count;
	}
	return count > 0U;
}

static int RelayWallGameRestore(relay_wall_pose_t *poses, uint32_t count)
{
	while (count > 0U)
	{
		edict_t *entity;

		count--;
		entity = poses[count].entity;
		if (!entity || !entity->inuse)
			return 0;
		entity->solid = poses[count].solid;
		entity->svflags = poses[count].svflags;
		sg_host.linkentity(entity);
	}
	return 1;
}

static int RelayWallGameProve(void *raw,
	const sg_relay_wall_plan_witness_t *witness,
	uint32_t source, uint32_t destination,
	sg_relay_wall_objective_proof_t *proof_out)
{
	relay_wall_game_context_t *context = raw;
	const sg_relay_wall_objective_game_request_t *request = context->request;
	relay_wall_pose_t poses[2];
	edict_t *button;
	edict_t *wall;
	const rune_mechanism_node_t *wall_node;
	uint32_t pose_count = 0U;
	uint32_t approach_ms = 0U;
	uint32_t egress_ms = 0U;
	int opened = 0;
	int proved = 0;
	int outcome = 0;

	memset(poses, 0, sizeof(poses));
	if (source >= request->seed_count || destination >= request->seed_count ||
	    !RelayWallGameCurrent(request, witness) ||
	    !(button = RelayWallGameEntity(witness->entry_key)) ||
	    !(wall = RelayWallGameEntity(witness->wall_key)) ||
	    !(wall_node = RelayWallGameNode(request->catalog, witness->wall_key)))
		goto done;
	if (!RelayWallGameApproach(request->seeds[source].origin, button,
	        witness->touch_hold_ms, proof_out->anchor, &approach_ms))
		goto done;
	if (!RelayWallGamePoseOpen(request, witness, poses, &pose_count))
	{
		if (pose_count > 0U && !RelayWallGameRestore(poses, pose_count))
			return -1;
		goto done;
	}
	opened = 1;
	if (!RelayWallGameEgress(proof_out->anchor,
	        request->seeds[destination].origin, wall_node,
	        witness->active_window_ms, &egress_ms))
		goto done;
	if (approach_ms + witness->touch_hold_ms + egress_ms > RUNE_MAX_COST_MS)
		goto done;
	proof_out->cost_ms = approach_ms + witness->touch_hold_ms + egress_ms;
	proof_out->egress_ms = egress_ms;
	proof_out->sweep_clear_ms = (uint16_t)(((egress_ms + 99U) / 100U) * 100U);
	proved = 1;

done:
	if (opened && !RelayWallGameRestore(poses, pose_count))
		outcome = -1;
	else if (!RelayWallGameCurrent(request, witness))
		outcome = -1;
	else
		outcome = proved;
	return outcome;
}

static int RelayWallGamePublish(void *raw,
	const sg_relay_wall_plan_witness_t *witness,
	uint32_t source, uint32_t destination,
	const sg_relay_wall_objective_proof_t *proof)
{
	relay_wall_game_context_t *context = raw;
	const sg_relay_wall_objective_game_request_t *request = context->request;
	rune_link_t *link;
	sg_mechanism_plan_binding_t *binding;

	if (!witness || !proof || source >= request->seed_count ||
	    destination >= request->seed_count || proof->cost_ms == 0U ||
	    proof->cost_ms > RUNE_MAX_COST_MS ||
	    *request->link_count < 0 ||
	    *request->link_count >= request->link_capacity ||
	    *request->binding_count >= request->binding_capacity)
		return -1;
	link = &request->links[*request->link_count];
	binding = &request->bindings[*request->binding_count];
	memset(link, 0, sizeof(*link));
	memset(binding, 0, sizeof(*binding));
	link->from = (uint16_t)source;
	link->to = (uint16_t)destination;
	link->action = RL_BUTTON_DOOR;
	link->provenance = RL_DECLARED;
	link->cost_ms = (int16_t)proof->cost_ms;
	link->heading_slack = RUNE_DECLARED_CONTROL_MARKER;
	link->mechanism_plan = *request->binding_count;
	link->sweep_clear_ms = proof->sweep_clear_ms;
	link->mode = RLCM_PREOPEN;
	VectorCopy(proof->anchor, link->anchor);
	binding->entry_key = witness->entry_key;
	binding->mover_key = witness->wall_key;
	binding->destination_key = witness->immediate_relay_key;
	binding->egress_key = witness->restore_relay_key;
	binding->controller_kind = SG_MECHANISM_CONTROLLER_RELAY_DOOR;
	binding->expected_members = 1U;
	binding->cooldown_ms = witness->cooldown_ms;
	(*request->link_count)++;
	(*request->binding_count)++;
	return 1;
}

int SG_RelayWallObjectiveGameBridge(
	const sg_relay_wall_objective_game_request_t *request,
	sg_relay_wall_objective_report_t *report_out)
{
	relay_wall_game_context_t context;
	sg_relay_wall_objective_request_t objective;

	if (!request || !report_out || !request->catalog || !request->seeds ||
	    !request->components || !request->objective_masks ||
	    !request->source_stable || !request->source_waterlevel ||
	    !request->source_watertype || !request->links ||
	    !request->link_count || request->link_capacity <= 0 ||
	    !request->bindings || !request->binding_count ||
	    request->binding_capacity == 0U || !request->has_incoming ||
	    !request->has_outgoing)
		return -1;
	memset(&context, 0, sizeof(context));
	memset(&objective, 0, sizeof(objective));
	context.request = request;
	objective.catalog = request->catalog;
	objective.seeds = request->seeds;
	objective.seed_count = request->seed_count;
	objective.components = request->components;
	objective.objective_masks = request->objective_masks;
	objective.context = &context;
	objective.eligible = RelayWallGameEligible;
	objective.discover = RelayWallGameDiscover;
	objective.prove = RelayWallGameProve;
	objective.publish = RelayWallGamePublish;
	return SG_RelayWallObjectiveBridge(&objective, report_out);
}
