#include "../g_local.h"
#include "sg_hooks.h"
#include "sg_local.h"
#include "sg_train_station_candidate_game.h"
#include "sg_train_station_board_path.h"

#include <limits.h>
#include <stdlib.h>

typedef struct station_dry_candidate_s
{
	int dry_index;
	int approach;
	float distance2;
} station_dry_candidate_t;

typedef struct station_pose_s
{
	edict_t *train;
	vec3_t origin;
	vec3_t old_origin;
	vec3_t velocity;
	vec3_t avelocity;
	int solid;
	int linkcount;
} station_pose_t;

static sg_train_station_candidate_game_diagnostics_t station_diagnostics;

const sg_train_station_candidate_game_diagnostics_t *
SG_TrainStationCandidateGameLastDiagnostics(void)
{
	return &station_diagnostics;
}

static void StationSetPose(edict_t *train, edict_t *corner)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		train->s.origin[axis] = corner->s.origin[axis] - train->mins[axis];
	VectorCopy(train->s.origin, train->s.old_origin);
	VectorClear(train->velocity);
	VectorClear(train->avelocity);
	train->solid = SOLID_BSP;
	sg_host.linkentity(train);
}

static void StationPoseBegin(edict_t *train, edict_t *corner,
	station_pose_t *saved)
{
	memset(saved, 0, sizeof(*saved));
	saved->train = train;
	VectorCopy(train->s.origin, saved->origin);
	VectorCopy(train->s.old_origin, saved->old_origin);
	VectorCopy(train->velocity, saved->velocity);
	VectorCopy(train->avelocity, saved->avelocity);
	saved->solid = train->solid;
	saved->linkcount = train->linkcount;
	StationSetPose(train, corner);
}

static void StationPoseEnd(station_pose_t *saved)
{
	edict_t *train = saved->train;

	VectorCopy(saved->origin, train->s.origin);
	VectorCopy(saved->old_origin, train->s.old_origin);
	VectorCopy(saved->velocity, train->velocity);
	VectorCopy(saved->avelocity, train->avelocity);
	train->solid = saved->solid;
	sg_host.linkentity(train);
	train->linkcount = saved->linkcount;
}

static qboolean StationDryLinkValid(
	const sg_train_station_candidate_game_request_t *request,
	const rune_link_t *dry)
{
	int source = dry->from;
	int approach = dry->to;

	return dry->action == RL_RUN && dry->provenance == RL_PROVEN &&
	       dry->mechanism_plan == RUNE_NO_MECHANISM_PLAN && dry->cost_ms > 0 &&
	       dry->anchor[0] == 0.0f && dry->anchor[1] == 0.0f &&
	       dry->anchor[2] == 0.0f && source >= 0 && approach >= 0 &&
	       source != approach && source < request->num_seeds &&
	       approach < request->num_seeds && request->source_stable[source] &&
	       request->source_stable[approach] &&
	       request->source_waterlevel[source] == 0 &&
	       request->source_waterlevel[approach] == 0 &&
	       request->has_incoming(request->context, approach);
}

static qboolean StationApproachDistance2(const vec3_t origin,
	const edict_t *train, float *distance2)
{
	float delta[3];
	int axis;

	for (axis = 0; axis < 2; axis++)
	{
		float low = train->absmin[axis] - 15.0f;
		float high = train->absmax[axis] + 15.0f;

		delta[axis] = origin[axis] < low ? low - origin[axis] :
			origin[axis] > high ? origin[axis] - high : 0.0f;
	}
	delta[2] = origin[2] - 24.0f - train->absmax[2];
	*distance2 = DotProduct(delta, delta);
	return delta[0] * delta[0] + delta[1] * delta[1] <= 640.0f * 640.0f &&
	       fabsf(delta[2]) <= 64.0f;
}

static qboolean StationCandidateBefore(const station_dry_candidate_t *left,
	const station_dry_candidate_t *right)
{
	return left->distance2 < right->distance2 ||
	       (left->distance2 == right->distance2 &&
	        (left->approach < right->approach ||
	         (left->approach == right->approach &&
	          left->dry_index < right->dry_index)));
}

static int StationCandidateCompare(const void *left_raw,
	const void *right_raw)
{
	const station_dry_candidate_t *left = left_raw;
	const station_dry_candidate_t *right = right_raw;

	return StationCandidateBefore(left, right) ? -1 :
		StationCandidateBefore(right, left) ? 1 : 0;
}

static qboolean StationBoardStage(const vec3_t source, const edict_t *train,
	vec3_t stage)
{
	sg_train_station_board_path_t path;
	vec3_t interior;
	float bounds[4];
	unsigned stage_index;
	int nearest = 0;
	int side;

	if (!source || !train || !stage)
		return false;
	interior[0] = source[0] < train->absmin[0] + 16.125f
		? train->absmin[0] + 16.125f
		: source[0] > train->absmax[0] - 16.125f
			? train->absmax[0] - 16.125f : source[0];
	interior[1] = source[1] < train->absmin[1] + 16.125f
		? train->absmin[1] + 16.125f
		: source[1] > train->absmax[1] - 16.125f
			? train->absmax[1] - 16.125f : source[1];
	interior[2] = source[2];
	if (SG_TrainStationBoardPathBuildCanonical(source, train->absmin,
	        train->absmax, interior, &path) && path.count >= 2U)
	{
		stage_index = path.count - 2U;
		VectorCopy(path.points[stage_index], stage);
	}
	else
	{
		bounds[0] = train->absmin[0] - 16.125f;
		bounds[1] = train->absmax[0] + 16.125f;
		bounds[2] = train->absmin[1] - 16.125f;
		bounds[3] = train->absmax[1] + 16.125f;
		for (side = 1; side < 4; side++)
			if (fabsf(source[side / 2] - bounds[side]) <
			    fabsf(source[nearest / 2] - bounds[nearest]))
				nearest = side;
		VectorCopy(source, stage);
		stage[nearest / 2] = bounds[nearest];
	}
	return stage[0] != source[0] || stage[1] != source[1] ||
	       stage[2] != source[2];
}

static int StationDirectionGenerate(
	const sg_train_station_candidate_game_request_t *request,
	const sg_train_station_candidate_t *candidate, uint32_t direction_index)
{
	const sg_train_station_direction_t *direction =
		&candidate->directions[direction_index];
	edict_t *train;
	edict_t *source_corner;
	station_pose_t saved;
	uint32_t route_offset = direction_index == 0U ? 0U :
		SG_TRAIN_STATION_ROUTE_CORNERS / 2U;
	int best_source = -1;
	int best_destination = -1;
	int best_egress_ms = 0;
	int best_cost = INT_MAX;
	vec3_t best_anchor = { 0.0f, 0.0f, 0.0f };
	vec3_t best_board = { 0.0f, 0.0f, 0.0f };
	int *best_dry_by_approach;
	station_dry_candidate_t *ranked;
	int ranked_count = 0;
	int dry_index;
	int approach;

	station_diagnostics.directions++;

	if (direction->ride_train_key >= (uint32_t)globals.num_edicts ||
	    direction->source_station_key >= (uint32_t)globals.num_edicts)
		return 0;
	train = &g_edicts[direction->ride_train_key];
	source_corner = &g_edicts[direction->source_station_key];
	if (!train->inuse || !source_corner->inuse)
		return 0;
	if (request->num_seeds > INT_MAX / (int)sizeof(*best_dry_by_approach) ||
	    request->num_seeds > INT_MAX / (int)sizeof(*ranked))
		return -1;
	StationPoseBegin(train, source_corner, &saved);
	best_dry_by_approach = sg_host.level_alloc(
		(int)(sizeof(*best_dry_by_approach) * (size_t)request->num_seeds));
	if (!best_dry_by_approach)
	{
		StationPoseEnd(&saved);
		return -1;
	}
	ranked = sg_host.level_alloc(
		(int)(sizeof(*ranked) * (size_t)request->num_seeds));
	if (!ranked)
	{
		sg_host.level_free(best_dry_by_approach);
		StationPoseEnd(&saved);
		return -1;
	}
	for (approach = 0; approach < request->num_seeds; approach++)
		best_dry_by_approach[approach] = -1;
	for (dry_index = 0; dry_index < request->num_links; dry_index++)
	{
		const rune_link_t *dry = &request->links[dry_index];
		int prior;

		if (!StationDryLinkValid(request, dry))
			continue;
		station_diagnostics.source_candidates++;
		prior = best_dry_by_approach[dry->to];
		if (prior < 0 || dry->cost_ms < request->links[prior].cost_ms ||
		    (dry->cost_ms == request->links[prior].cost_ms &&
		     (dry->from < request->links[prior].from ||
		      (dry->from == request->links[prior].from && dry_index < prior))))
			best_dry_by_approach[dry->to] = dry_index;
	}
	/* The dry-link index is a finite, caller-owned map of every eligible
	 * boarding approach.  Rank the complete map for a canonical traversal,
	 * without truncating it to an arbitrary proof frontier. */
	for (approach = 0; approach < request->num_seeds; approach++)
	{
		station_dry_candidate_t candidate;
		if (best_dry_by_approach[approach] < 0)
			continue;
		candidate.dry_index = best_dry_by_approach[approach];
		candidate.approach = approach;
		if (!StationApproachDistance2(request->seeds[approach].origin, train,
		        &candidate.distance2))
			continue;
		ranked[ranked_count++] = candidate;
	}
	sg_host.level_free(best_dry_by_approach);
	qsort(ranked, (size_t)ranked_count, sizeof(*ranked),
		StationCandidateCompare);
	for (int rank = 0; rank < ranked_count; rank++)
	{
		int board_approach = ranked[rank].approach;
		vec3_t stage;
		vec3_t board;
		vec3_t carried;
		int approach_ms;
		int destination;
		uint32_t step;
		qboolean carry_ok = true;

		if (!StationBoardStage(request->seeds[board_approach].origin, train,
		        stage))
			continue;
		StationSetPose(train, source_corner);
		station_diagnostics.board_attempts++;
		if (!SG_OracleTrainStationBoard(request->seeds[board_approach].origin,
		        stage, train,
		        direction->source_dwell_ms, &approach_ms, board))
			continue;
		station_diagnostics.board_successes++;
		VectorCopy(board, carried);
		for (step = 0U; step < SG_TRAIN_STATION_ROUTE_CORNERS / 2U; step++)
		{
			uint32_t from_key = candidate->witness.route_keys[
				(route_offset + step) % SG_TRAIN_STATION_ROUTE_CORNERS];
			uint32_t to_key = candidate->witness.route_keys[
				(route_offset + step + 1U) %
				SG_TRAIN_STATION_ROUTE_CORNERS];
			vec3_t next;

			if (from_key >= (uint32_t)globals.num_edicts ||
			    to_key >= (uint32_t)globals.num_edicts)
			{
				carry_ok = false;
				break;
			}
			StationSetPose(train, &g_edicts[from_key]);
			station_diagnostics.carry_attempts++;
			if (!SG_OracleTrainStationCarry(carried, &g_edicts[from_key],
			        &g_edicts[to_key], train, next))
			{
				carry_ok = false;
				break;
			}
			station_diagnostics.carry_successes++;
			StationSetPose(train, &g_edicts[to_key]);
			VectorCopy(next, carried);
		}
		if (!carry_ok)
			continue;
		for (destination = 0; destination < request->num_seeds; destination++)
		{
			vec3_t egress_delta;
			int egress_ms;
			int cost = RUNE_MAX_COST_MS;

			if (destination == board_approach ||
			    !request->source_stable[destination] ||
			    request->source_waterlevel[destination] != 0 ||
			    !request->has_outgoing(request->context, destination))
				continue;
			VectorSubtract(request->seeds[destination].origin, carried,
				egress_delta);
			if (egress_delta[0] * egress_delta[0] +
			        egress_delta[1] * egress_delta[1] > 640.0f * 640.0f ||
			    fabsf(egress_delta[2]) > 256.0f)
				continue;
			station_diagnostics.egress_attempts++;
			if (!SG_OracleTrainRideEgress(carried,
			        request->seeds[destination].origin, train, &egress_ms))
				continue;
			station_diagnostics.egress_successes++;
			if (cost >= best_cost)
				continue;
			best_source = board_approach;
			best_destination = destination;
			best_egress_ms = egress_ms;
			best_cost = cost;
			VectorCopy(stage, best_anchor);
			VectorCopy(board, best_board);
		}
		if (best_source >= 0)
			break;
	}
	sg_host.level_free(ranked);
	StationPoseEnd(&saved);
	if (best_source >= 0 && best_destination >= 0)
	{
		if (*request->num_bindings >= request->binding_capacity)
			return -1;
		rune_link_t *link = request->append(request->context, best_source,
			best_destination, best_cost);
		sg_mechanism_plan_binding_t *binding;

		if (!link)
			return -1;
		VectorCopy(best_anchor, link->anchor);
		VectorCopy(best_board, link->mechanism_anchor);
		link->sweep_clear_ms = (uint16_t)best_egress_ms;
		link->mode = RLCM_RIDE;
		binding = &request->bindings[*request->num_bindings];
		*binding = candidate->binding;
		link->mechanism_plan = (*request->num_bindings)++;
		station_diagnostics.appended++;
		return 1;
	}
	return 0;
}

int SG_TrainStationCandidateGameGenerate(
	const sg_train_station_candidate_game_request_t *request)
{
	sg_train_station_candidate_t *candidates;
	uint32_t candidate_count;
	uint32_t candidate_index;
	int added = 0;

	memset(&station_diagnostics, 0, sizeof(station_diagnostics));

	if (!request || !request->catalog || !request->seeds ||
	    request->num_seeds <= 0 || !request->links ||
	    request->num_links <= 0 || !request->source_stable ||
	    !request->source_waterlevel || !request->has_incoming ||
	    !request->has_outgoing || !request->append || !request->bindings ||
	    !request->num_bindings || request->binding_capacity == 0U)
		return -1;
	candidate_count = SG_TrainStationCandidatesDiscover(request->catalog,
		NULL, 0U);
	station_diagnostics.candidates = candidate_count;
	if (candidate_count == 0U)
		return 0;
	candidates = sg_host.level_alloc(sizeof(*candidates) * candidate_count);
	if (!candidates || SG_TrainStationCandidatesDiscover(request->catalog,
	        candidates, candidate_count) != candidate_count)
	{
		if (candidates)
			sg_host.level_free(candidates);
		return -1;
	}
	for (candidate_index = 0U; candidate_index < candidate_count;
	     candidate_index++)
	{
		uint32_t direction;

		for (direction = 0U; direction < SG_TRAIN_STATION_DIRECTIONS;
		     direction++)
		{
			int result = StationDirectionGenerate(request,
				&candidates[candidate_index], direction);

			if (result < 0)
			{
				sg_host.level_free(candidates);
				return -1;
			}
			added += result;
		}
	}
	sg_host.level_free(candidates);
	return added;
}
