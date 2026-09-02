#include "sg_rune_field.h"

#define SG_RUNE_STEP_PUSH 16.0f   /* units past a portal the step aims */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_locate.h"

#define STANCE_CHANGE_COST 0.05f

static float Horizontal(const float a[3], const float b[3])
{
	const float dx = a[0] - b[0], dy = a[1] - b[1];

	return sqrtf(dx * dx + dy * dy);
}

static void Q8ToFloat(const sg_rune_cx_vec3_t *q8, float out[3])
{
	out[0] = (float)q8->value[0] / (float)SG_RUNE_CX_Q8_ONE;
	out[1] = (float)q8->value[1] / (float)SG_RUNE_CX_Q8_ONE;
	out[2] = (float)q8->value[2] / (float)SG_RUNE_CX_Q8_ONE;
}

/* What one crossing costs under its profile.  Contact profiles cost by
 * distance; air profiles cost by the time in the air, estimated from the
 * vertical difference under the map's gravity and the horizontal run at the
 * engine's speed.  Every crossing costs at least a floor, so a chain of
 * tiny cells is never free. */
#define EDGE_COST_FLOOR 0.02f

static float AirTime(const sg_rune_law_t *law, uint8_t kind, float horizontal,
	float dz)
{
	const float gravity = law->gravity > 0.0f ? law->gravity : 800.0f;
	float launch = 0.0f, time;

	if (kind == SG_RUNE_MOVE_JUMP)
		launch = law->jump_velocity;
	else if (kind == SG_RUNE_MOVE_ROCKET_JUMP)
		launch = law->jump_velocity + 700.0f;
	if (launch > 0.0f)
	{
		const float rise = launch * launch / (2.0f * gravity);
		const float remaining = rise - dz;

		time = launch / gravity +
			(remaining > 0.0f ? sqrtf(2.0f * remaining / gravity) : 0.0f);
	}
	else
		time = dz < 0.0f ? sqrtf(-2.0f * dz / gravity) : 0.0f;
	if (time < horizontal / law->max_velocity)
		time = horizontal / law->max_velocity;
	return time;
}

static float EdgeCost(const sg_rune_artifact_t *artifact, uint32_t capability,
	float horizontal, float dz)
{
	const sg_rune_move_table_t *move = &artifact->movement;
	const sg_rune_move_capability_t *record = &move->capabilities[capability];
	const sg_rune_move_profile_t *profile = &move->profiles[record->profile];
	float inputs[SG_RUNE_FN_INPUT_COUNT];
	float cost = 0.0f, reach = 1.0f;

	memset(inputs, 0, sizeof(inputs));
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = horizontal;
	inputs[SG_RUNE_FN_INPUT_TIME_SECONDS] = record->seconds > 0.0f ?
		record->seconds : AirTime(&artifact->law, record->kind, horizontal, dz);
	if (profile->reachability != SG_RUNE_FN_INDEX_NONE &&
		(!SG_RuneFnEvaluate(&move->analytic, profile->reachability, inputs,
			&reach) || !(reach > 0.0f)))
		return INFINITY;
	if (profile->cost == SG_RUNE_FN_INDEX_NONE ||
		!SG_RuneFnEvaluate(&move->analytic, profile->cost, inputs, &cost) ||
		!(cost >= 0.0f))
		return INFINITY;
	return cost < EDGE_COST_FLOOR ? EDGE_COST_FLOOR : cost;
}

int SG_RuneRouterBuild(sg_rune_router_t *router,
	const sg_rune_artifact_t *artifact)
{
	const sg_rune_cx_view_t *cx;
	const sg_rune_move_table_t *move;
	uint32_t cell, portal, capability, axis;
	uint32_t *fill = NULL;

	if (!router)
		return 0;
	memset(router, 0, sizeof(*router));
	if (!artifact)
		return 0;
	cx = &artifact->complex;
	move = &artifact->movement;
	router->artifact = artifact;
	router->cell_center = malloc((size_t)(cx->cell_count ? cx->cell_count : 1U) *
		3U * sizeof(float));
	router->portal_center = malloc((size_t)(cx->portal_count ?
		cx->portal_count : 1U) * 3U * sizeof(float));
	router->arrival_first = calloc((size_t)cx->cell_count + 1U,
		sizeof(*router->arrival_first));
	router->arrivals = malloc((size_t)(move->capability_count ?
		move->capability_count : 1U) * sizeof(*router->arrivals));
	router->departure_first = calloc((size_t)cx->cell_count + 1U,
		sizeof(*router->departure_first));
	router->departures = malloc((size_t)(move->capability_count ?
		move->capability_count : 1U) * sizeof(*router->departures));
	router->edge_cost = malloc((size_t)(move->capability_count ?
		move->capability_count : 1U) * sizeof(float));
	router->destination = malloc((size_t)(move->capability_count ?
		move->capability_count : 1U) * sizeof(uint32_t));
	fill = malloc((size_t)cx->cell_count * sizeof(*fill) + 1U);
	if (!router->cell_center || !router->portal_center ||
		!router->arrival_first || !router->arrivals || !router->departure_first ||
		!router->departures || !router->edge_cost ||
		!router->destination || !fill)
	{
		free(fill);
		SG_RuneRouterFree(router);
		return 0;
	}
	/* A cell's reference point is the middle of its floor (its bottom):
	 * bodies stand on floors, and a tall cell's height is air. */
	for (cell = 0U; cell < cx->cell_count; cell++)
	{
		for (axis = 0U; axis < 2U; axis++)
			router->cell_center[cell * 3U + axis] =
				(float)((double)cx->cells[cell].bounds.mins.value[axis] +
					(double)cx->cells[cell].bounds.maxs.value[axis]) /
				(2.0f * (float)SG_RUNE_CX_Q8_ONE);
		router->cell_center[cell * 3U + 2U] =
			(float)cx->cells[cell].bounds.mins.value[2] /
			(float)SG_RUNE_CX_Q8_ONE;
	}
	/* A portal's aim point is its foot: the middle of its lowest edge, one
	 * unit up.  A wall opening is crossed at the floor; a floor opening is
	 * flat anyway. */
	for (portal = 0U; portal < cx->portal_count; portal++)
		Q8ToFloat(&cx->portals[portal].foot, &router->portal_center[portal * 3U]);
	/* Count arrivals per destination cell, prefix-sum, fill. */
	for (capability = 0U; capability < move->capability_count; capability++)
	{
		const sg_rune_move_capability_t *record =
			&move->capabilities[capability];
		uint32_t destination = record->destination;
		float horizontal, dz;

		router->destination[capability] = destination;
		router->arrival_first[destination + 1U]++;
		if (record->portal == SG_RUNE_CX_INDEX_NONE)
			horizontal = Horizontal(&router->cell_center[record->cell * 3U],
				&router->cell_center[destination * 3U]);
		else
			horizontal = Horizontal(&router->cell_center[record->cell * 3U],
					&router->portal_center[record->portal * 3U]) +
				Horizontal(&router->portal_center[record->portal * 3U],
					&router->cell_center[destination * 3U]);
		dz = router->cell_center[destination * 3U + 2U] -
			router->cell_center[record->cell * 3U + 2U];
		router->edge_cost[capability] = EdgeCost(artifact, capability,
			horizontal, dz);
		if (record->mechanism != SG_RUNE_CX_INDEX_NONE &&
			record->mechanism < artifact->mechanisms.record_count)
		{
			const sg_rune_mech_t *mechanism =
				&artifact->mechanisms.records[record->mechanism];

			if (mechanism->kind == SG_RUNE_MECH_DOOR)
			{
				if (mechanism->activation == SG_RUNE_MECH_ACTIVATE_NONE)
					router->edge_cost[capability] = INFINITY;
				else if (mechanism->activation != SG_RUNE_MECH_ACTIVATE_TOUCH)
					router->edge_cost[capability] += 3.0f;
				else
					router->edge_cost[capability] += 0.5f;
			}
		}
	}
	for (cell = 0U; cell < cx->cell_count; cell++)
		router->arrival_first[cell + 1U] += router->arrival_first[cell];
	memcpy(fill, router->arrival_first, (size_t)cx->cell_count * sizeof(*fill));
	for (capability = 0U; capability < move->capability_count; capability++)
		router->arrivals[fill[router->destination[capability]]++] = capability;
	/* And by source cell. */
	for (capability = 0U; capability < move->capability_count; capability++)
		router->departure_first[move->capabilities[capability].cell + 1U]++;
	for (cell = 0U; cell < cx->cell_count; cell++)
		router->departure_first[cell + 1U] += router->departure_first[cell];
	memcpy(fill, router->departure_first, (size_t)cx->cell_count * sizeof(*fill));
	for (capability = 0U; capability < move->capability_count; capability++)
		router->departures[fill[move->capabilities[capability].cell]++] = capability;
	free(fill);
	return 1;
}

void SG_RuneRouterFree(sg_rune_router_t *router)
{
	if (!router)
		return;
	free(router->cell_center);
	free(router->portal_center);
	free(router->arrival_first);
	free(router->arrivals);
	free(router->departure_first);
	free(router->departures);
	free(router->edge_cost);
	free(router->destination);
	memset(router, 0, sizeof(*router));
}

/* ---- binary heap of (cost, state), lazy deletion --------------------------- */

typedef struct heap_item_s
{
	float cost;
	uint32_t state;
} heap_item_t;

typedef struct heap_s
{
	heap_item_t *items;
	uint32_t count, capacity;
} heap_t;

static int HeapPush(heap_t *heap, float cost, uint32_t state)
{
	uint32_t index;

	if (heap->count == heap->capacity)
	{
		uint32_t next = heap->capacity ? heap->capacity * 2U : 1024U;
		heap_item_t *grown = realloc(heap->items, (size_t)next * sizeof(*grown));

		if (!grown)
			return 0;
		heap->items = grown;
		heap->capacity = next;
	}
	index = heap->count++;
	while (index > 0U)
	{
		uint32_t parent = (index - 1U) / 2U;

		if (heap->items[parent].cost <= cost)
			break;
		heap->items[index] = heap->items[parent];
		index = parent;
	}
	heap->items[index].cost = cost;
	heap->items[index].state = state;
	return 1;
}

static int HeapPop(heap_t *heap, heap_item_t *out)
{
	heap_item_t last;
	uint32_t index = 0U;

	if (heap->count == 0U)
		return 0;
	*out = heap->items[0];
	last = heap->items[--heap->count];
	while (1)
	{
		uint32_t left = index * 2U + 1U, right = left + 1U, child;

		if (left >= heap->count)
			break;
		child = right < heap->count &&
			heap->items[right].cost < heap->items[left].cost ? right : left;
		if (heap->items[child].cost >= last.cost)
			break;
		heap->items[index] = heap->items[child];
		index = child;
	}
	if (heap->count)
		heap->items[index] = last;
	return 1;
}

int SG_RuneFieldBuild(sg_rune_field_t *field, const sg_rune_router_t *router,
	uint32_t destination_cell)
{
	return SG_RuneFieldBuildWeighted(field, router, destination_cell, NULL);
}

int SG_RuneFieldBuildWeighted(sg_rune_field_t *field,
	const sg_rune_router_t *router, uint32_t destination_cell,
	const float *cell_surcharge)
{
	const sg_rune_artifact_t *artifact;
	const sg_rune_cx_view_t *cx;
	const sg_rune_move_table_t *move;
	uint32_t state_count, state, stance;
	heap_t heap;
	heap_item_t item;

	if (!field || !router || !router->artifact)
		return 0;
	artifact = router->artifact;
	cx = &artifact->complex;
	move = &artifact->movement;
	if (destination_cell >= cx->cell_count)
		return 0;
	state_count = cx->cell_count * 2U;
	if (field->state_count != state_count)
	{
		SG_RuneFieldFree(field);
		field->cost = malloc((size_t)state_count * sizeof(float));
		field->next = malloc((size_t)state_count * sizeof(uint32_t));
		field->next_crouching = malloc((size_t)state_count);
		if (!field->cost || !field->next || !field->next_crouching)
		{
			SG_RuneFieldFree(field);
			return 0;
		}
		field->state_count = state_count;
	}
	field->destination_cell = destination_cell;
	field->settled = 0U;
	for (state = 0U; state < state_count; state++)
	{
		field->cost[state] = INFINITY;
		field->next[state] = SG_RUNE_CX_INDEX_NONE;
		field->next_crouching[state] = 0U;
	}
	memset(&heap, 0, sizeof(heap));
	for (stance = 0U; stance < 2U; stance++)
	{
		sg_rune_cx_stances_t needed = stance ? SG_RUNE_CX_STANCE_CROUCHING :
			SG_RUNE_CX_STANCE_STANDING;

		if ((cx->cells[destination_cell].valid_stances & needed) == 0U)
			continue;
		state = SG_RUNE_FIELD_STATE(destination_cell, stance);
		field->cost[state] = 0.0f;
		if (!HeapPush(&heap, 0.0f, state))
		{
			free(heap.items);
			return 0;
		}
	}
	while (HeapPop(&heap, &item))
	{
		uint32_t cell = item.state / 2U;
		uint32_t crouching = item.state & 1U;
		uint8_t here = crouching ? SG_RUNE_MOVE_CROUCHING : SG_RUNE_MOVE_STANDING;
		uint32_t slot;

		if (item.cost > field->cost[item.state])
			continue;   /* stale */
		field->settled++;
		for (slot = router->arrival_first[cell];
			slot < router->arrival_first[cell + 1U]; slot++)
		{
			uint32_t capability = router->arrivals[slot];
			const sg_rune_move_capability_t *record =
				&move->capabilities[capability];
			float edge = router->edge_cost[capability];
			uint32_t source_stance;

			if (!(edge < INFINITY) ||
				(record->destination_stances & here) == 0U)
				continue;
			for (source_stance = 0U; source_stance < 2U; source_stance++)
			{
				uint8_t from = source_stance ? SG_RUNE_MOVE_CROUCHING :
					SG_RUNE_MOVE_STANDING;
				uint32_t from_state;
				float candidate;

				if ((record->source_stances & from) == 0U)
					continue;
				from_state = SG_RUNE_FIELD_STATE(record->cell, source_stance);
				candidate = item.cost + edge +
					(source_stance != crouching ? STANCE_CHANGE_COST : 0.0f) +
					(cell_surcharge ? cell_surcharge[cell] : 0.0f);
				if (candidate < field->cost[from_state])
				{
					field->cost[from_state] = candidate;
					field->next[from_state] = capability;
					field->next_crouching[from_state] = (uint8_t)crouching;
					if (!HeapPush(&heap, candidate, from_state))
					{
						free(heap.items);
						return 0;
					}
				}
			}
		}
	}
	free(heap.items);
	return 1;
}

void SG_RuneFieldFree(sg_rune_field_t *field)
{
	if (!field)
		return;
	free(field->cost);
	free(field->next);
	free(field->next_crouching);
	memset(field, 0, sizeof(*field));
}

static void Fill(const sg_rune_router_t *router, uint32_t capability,
	uint8_t crouching_next, sg_rune_step_t *step_out);
static void Lookahead(const sg_rune_router_t *router,
	const sg_rune_field_t *field, sg_rune_step_t *step_out);

int SG_RuneStepSelect(const sg_rune_router_t *router,
	const sg_rune_field_t *field, uint32_t cell, int crouching,
	const float point[3], sg_rune_step_t *step_out)
{
	uint32_t state, capability;

	if (!step_out)
		return 0;
	memset(step_out, 0, sizeof(*step_out));
	step_out->kind = SG_RUNE_STEP_HOLD;
	step_out->cell = cell;
	step_out->portal = SG_RUNE_CX_INDEX_NONE;
	step_out->capability = SG_RUNE_CX_INDEX_NONE;
	step_out->crouching_now = crouching ? 1U : 0U;
	step_out->cost_to_go = INFINITY;
	if (!router || !router->artifact || !field || !field->cost ||
		cell >= router->artifact->complex.cell_count ||
		field->state_count != router->artifact->complex.cell_count * 2U)
		return 0;
	state = SG_RUNE_FIELD_STATE(cell, crouching);
	step_out->cost_to_go = field->cost[state];
	if (cell == field->destination_cell)
	{
		step_out->kind = SG_RUNE_STEP_ARRIVED;
		step_out->crouching_next = step_out->crouching_now;
		if (point)
			memcpy(step_out->target, point, sizeof(step_out->target));
		else
			memcpy(step_out->target, &router->cell_center[cell * 3U],
				sizeof(step_out->target));
		step_out->cost_to_go = 0.0f;
		return 1;
	}
	capability = field->next[state];
	if (capability == SG_RUNE_CX_INDEX_NONE)
	{
		/* The other stance may have a way: report it so the body can change
		 * stance in place. */
		uint32_t other = SG_RUNE_FIELD_STATE(cell, !crouching);

		if (field->next[other] != SG_RUNE_CX_INDEX_NONE)
		{
			step_out->kind = SG_RUNE_STEP_CROSS;
			step_out->capability = field->next[other];
			step_out->portal = router->artifact->movement.capabilities[
				step_out->capability].portal;
			step_out->move_kind = router->artifact->movement.capabilities[
				step_out->capability].kind;
			step_out->crouching_next = (uint8_t)(crouching ? 0U : 1U);
			memcpy(step_out->target, &router->portal_center[step_out->portal * 3U],
				sizeof(step_out->target));
			step_out->cost_to_go = field->cost[other];
			return 1;
		}
		step_out->kind = SG_RUNE_STEP_UNREACHABLE;
		return 1;
	}
	Fill(router, capability, field->next_crouching[state], step_out);
	Lookahead(router, field, step_out);
	return 1;
}

/* The step through one capability: its portal's foot pushed into the cell
 * beyond, or a mechanism's or hook's landing. */
/* A contact crossing whose next step, from the cell beyond, is a launch
 * (a jump, drop, rocket jump or rope) or nothing at all is walked to its
 * target, not through it: a body at full speed covers a body length a
 * frame and overshoots a small cell at a floor's edge before it decides
 * again.  The launch then lines itself up from a standing start. */
static void Lookahead(const sg_rune_router_t *router,
	const sg_rune_field_t *field, sg_rune_step_t *step_out)
{
	uint32_t beyond, state, next;
	uint8_t kind;

	step_out->ease = 0U;
	if (step_out->kind != SG_RUNE_STEP_CROSS ||
		step_out->capability == SG_RUNE_CX_INDEX_NONE)
		return;
	kind = step_out->move_kind;
	if (kind != SG_RUNE_MOVE_WALK && kind != SG_RUNE_MOVE_CROUCH &&
		kind != SG_RUNE_MOVE_RAMP && kind != SG_RUNE_MOVE_SWIM)
		return;
	beyond = router->destination[step_out->capability];
	if (beyond >= router->artifact->complex.cell_count ||
		beyond == field->destination_cell)
		return;
	state = SG_RUNE_FIELD_STATE(beyond, step_out->crouching_next);
	next = field->next[state];
	if (next == SG_RUNE_CX_INDEX_NONE)
	{
		step_out->ease = 1U;
		return;
	}
	kind = router->artifact->movement.capabilities[next].kind;
	if (kind == SG_RUNE_MOVE_JUMP || kind == SG_RUNE_MOVE_DROP ||
		kind == SG_RUNE_MOVE_ROCKET_JUMP || kind == SG_RUNE_MOVE_HOOK)
		step_out->ease = 1U;
}

static void Fill(const sg_rune_router_t *router, uint32_t capability,
	uint8_t crouching_next, sg_rune_step_t *step_out)
{
	const sg_rune_move_capability_t *record =
		&router->artifact->movement.capabilities[capability];

	step_out->kind = SG_RUNE_STEP_CROSS;
	step_out->capability = capability;
	step_out->portal = record->portal;
	step_out->move_kind = record->kind;
	step_out->crouching_next = crouching_next;
	step_out->run_up_present = 0U;
	step_out->launch_present = 0U;
	if (record->kind == SG_RUNE_MOVE_JUMP || record->kind == SG_RUNE_MOVE_DROP ||
		record->kind == SG_RUNE_MOVE_ROCKET_JUMP || record->kind == SG_RUNE_MOVE_HOOK)
	{
		/* Where the record was traced from: the cell's middle. */
		memcpy(step_out->run_up, &router->cell_center[record->cell * 3U],
			sizeof(step_out->run_up));
		step_out->run_up[2] += 24.0f;
		step_out->run_up_present = 1U;
		if (record->kind != SG_RUNE_MOVE_HOOK)
		{
			memcpy(step_out->launch, record->launch_velocity, sizeof(step_out->launch));
			step_out->launch_present = 1U;
		}
	}
	if (step_out->portal == SG_RUNE_CX_INDEX_NONE)
	{
		/* A mechanism or hook crossing: the target is the destination's
		 * floor; a hook also carries its bite point. */
		memcpy(step_out->target, &router->cell_center[
			router->destination[capability] * 3U], sizeof(step_out->target));
		step_out->target[2] += 24.0f;
		if (step_out->move_kind == SG_RUNE_MOVE_HOOK)
		{
			memcpy(step_out->hook_point,
				router->artifact->movement.capabilities[capability].anchor,
				sizeof(step_out->hook_point));
			step_out->hook_point_present = 1U;
			step_out->hook_release_distance =
				router->artifact->movement.capabilities[capability].parameter;
		}
	}
	else
	{
		/* The portal's centre, pushed a little into the cell beyond so the
		 * body actually crosses instead of stopping on the boundary. */
		const float *portal = &router->portal_center[step_out->portal * 3U];
		const float *beyond = &router->cell_center[router->destination[capability] * 3U];
		float dx = beyond[0] - portal[0], dy = beyond[1] - portal[1];
		float flat = sqrtf(dx * dx + dy * dy);

		memcpy(step_out->target, portal, sizeof(step_out->target));
		if (flat > 1e-3f)
		{
			float push = flat < SG_RUNE_STEP_PUSH ? flat : SG_RUNE_STEP_PUSH;

			step_out->target[0] += dx / flat * push;
			step_out->target[1] += dy / flat * push;
		}
	}

}

const char *SG_RuneStepKindString(sg_rune_step_kind_t kind)
{
	switch (kind)
	{
	case SG_RUNE_STEP_HOLD: return "hold";
	case SG_RUNE_STEP_ARRIVED: return "arrived";
	case SG_RUNE_STEP_CROSS: return "cross";
	case SG_RUNE_STEP_UNREACHABLE: return "unreachable";
	default: return "unknown";
	}
}

uint32_t SG_RuneFieldNearestReachable(const sg_rune_router_t *router,
	const sg_rune_locator_t *locator, const sg_rune_field_t *field,
	const float origin[3], float radius, float point_out[3])
{
	uint32_t best = SG_RUNE_CX_INDEX_NONE;
	float best_distance = radius;
	int32_t low[3], high[3], x, y, z;
	uint32_t axis;

	if (!router || !locator || !field || !field->cost || !origin ||
		router->artifact != locator->artifact)
		return SG_RUNE_CX_INDEX_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		float reach = axis == 2U ? 64.0f : radius;
		int32_t lo = (int32_t)((origin[axis] - reach) * (float)SG_RUNE_CX_Q8_ONE);
		int32_t hi = (int32_t)((origin[axis] + reach) * (float)SG_RUNE_CX_Q8_ONE);

		low[axis] = (lo - locator->origin_q8[axis]) / locator->bucket_q8;
		high[axis] = (hi - locator->origin_q8[axis]) / locator->bucket_q8;
		if (low[axis] < 0)
			low[axis] = 0;
		if (high[axis] >= (int32_t)locator->dims[axis])
			high[axis] = (int32_t)locator->dims[axis] - 1;
		if (low[axis] > high[axis])
			return SG_RUNE_CX_INDEX_NONE;
	}
	for (z = low[2]; z <= high[2]; z++)
		for (y = low[1]; y <= high[1]; y++)
			for (x = low[0]; x <= high[0]; x++)
			{
				uint32_t bucket = ((uint32_t)z * locator->dims[1] + (uint32_t)y) *
					locator->dims[0] + (uint32_t)x;
				uint32_t slot;

				for (slot = locator->first[bucket]; slot < locator->first[bucket + 1U];
					slot++)
				{
					uint32_t cell = locator->entries[slot];
					const float *center = &router->cell_center[cell * 3U];
					float dx = center[0] - origin[0], dy = center[1] - origin[1];
					float dz = center[2] - origin[2];
					float distance = sqrtf(dx * dx + dy * dy);

					if (!(router->artifact->complex.cells[cell].semantics &
							SG_RUNE_CX_CELL_SUPPORTED) ||
						fabsf(dz) > 64.0f || distance >= best_distance ||
						!(field->cost[SG_RUNE_FIELD_STATE(cell, 0)] < INFINITY))
						continue;
					best_distance = distance;
					best = cell;
				}
			}
	if (best != SG_RUNE_CX_INDEX_NONE && point_out)
	{
		point_out[0] = router->cell_center[best * 3U];
		point_out[1] = router->cell_center[best * 3U + 1U];
		point_out[2] = router->cell_center[best * 3U + 2U] + 24.0f;
	}
	return best;
}

static int Avoided(uint32_t capability, const uint32_t *avoid, uint32_t avoid_count)
{
	uint32_t i;

	for (i = 0U; i < avoid_count; i++)
		if (avoid[i] == capability)
			return 1;
	return 0;
}

int SG_RuneStepSelectAvoiding(const sg_rune_router_t *router,
	const sg_rune_field_t *field, uint32_t cell, int crouching,
	const float point[3], const uint32_t *avoid, uint32_t avoid_count,
	sg_rune_step_t *step_out)
{
	const sg_rune_move_table_t *move;
	uint8_t here;
	uint32_t slot, best = SG_RUNE_CX_INDEX_NONE;
	uint8_t best_crouching = 0U;
	float best_cost = INFINITY;

	if (!SG_RuneStepSelect(router, field, cell, crouching, point, step_out))
		return 0;
	if (step_out->kind != SG_RUNE_STEP_CROSS || avoid_count == 0U ||
		!Avoided(step_out->capability, avoid, avoid_count))
		return 1;
	move = &router->artifact->movement;
	here = crouching ? SG_RUNE_MOVE_CROUCHING : SG_RUNE_MOVE_STANDING;
	for (slot = router->departure_first[cell]; slot < router->departure_first[cell + 1U];
		slot++)
	{
		uint32_t capability = router->departures[slot];
		const sg_rune_move_capability_t *record = &move->capabilities[capability];
		float edge = router->edge_cost[capability];
		int arrive;

		if (Avoided(capability, avoid, avoid_count) || !(record->source_stances & here) ||
			!isfinite(edge))
			continue;
		for (arrive = 0; arrive < 2; arrive++)
		{
			uint8_t stance = arrive ? SG_RUNE_MOVE_CROUCHING : SG_RUNE_MOVE_STANDING;
			float cost;

			if (!(record->destination_stances & stance))
				continue;
			cost = edge + field->cost[SG_RUNE_FIELD_STATE(record->destination, arrive)];
			if (cost < best_cost)
			{
				best_cost = cost;
				best = capability;
				best_crouching = (uint8_t)arrive;
			}
		}
	}
	if (best == SG_RUNE_CX_INDEX_NONE)
	{
		step_out->kind = SG_RUNE_STEP_UNREACHABLE;
		step_out->capability = SG_RUNE_CX_INDEX_NONE;
		step_out->portal = SG_RUNE_CX_INDEX_NONE;
		return 1;
	}
	Fill(router, best, best_crouching, step_out);
	Lookahead(router, field, step_out);
	step_out->cost_to_go = best_cost;
	return 1;
}
