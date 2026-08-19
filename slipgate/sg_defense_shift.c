/* Threat-responsive movement for the stand defender. */
#include "sg_defense_shift.h"

#include <float.h>
#include <math.h>

uint32_t SG_DefensePatrolRandomNext(uint32_t state)
{
	if (state == 0)
		state = UINT32_C(0x6d2b79f5);
	state ^= state >> 16;
	state *= UINT32_C(0x7feb352d);
	state ^= state >> 15;
	state *= UINT32_C(0x846ca68b);
	state ^= state >> 16;
	return state ? state : UINT32_C(0x27d4eb2d);
}

uint32_t SG_DefensePatrolRandomInitial(uint64_t instance_token,
	unsigned client_slot)
{
	uint32_t state = (uint32_t)instance_token ^
	    (uint32_t)(instance_token >> 32) ^
	    (client_slot + 1u) * UINT32_C(0x9e3779b9);

	return SG_DefensePatrolRandomNext(state);
}

float SG_DefensePatrolDwell(uint32_t draw)
{
	return 2.0f + (float)(draw % 4001u) * 0.001f;
}

float SG_DefensePatrolThrottle(float configured)
{
	if (!isfinite(configured) || configured <= 0.0f)
		return 0.0f;
	if (configured < 0.35f)
		return 0.35f;
	if (configured > 0.75f)
		return 0.75f;
	return configured;
}

int SG_DefensePatrolFinishLeg(int current_seed, int *target_seed)
{
	if (!target_seed || current_seed < 0 || *target_seed < 0 ||
	    current_seed != *target_seed)
		return 0;
	*target_seed = -1;
	return 1;
}

int SG_DefensePatrolRetireIfInactive(int active, int *patrol_link,
	int *target_seed, int *commit_link)
{
	int owned_link;

	if (active || !patrol_link || *patrol_link < 0)
		return 0;
	owned_link = *patrol_link;
	if (commit_link && *commit_link == owned_link)
		*commit_link = -1;
	*patrol_link = -1;
	if (target_seed)
		*target_seed = -1;
	return 1;
}

int SG_DefensePatrolChoose(const sg_defense_patrol_candidate_t *candidates,
	size_t candidate_count, int max_goal_ms, int previous_seed,
	unsigned draw, int *seed_out)
{
	size_t index, eligible = 0, pick;
	int selected = -1;

	if (seed_out)
		*seed_out = -1;
	if (!candidates || candidate_count == 0 || max_goal_ms < 0)
		return -1;
	for (index = 0; index < candidate_count; index++)
		if (candidates[index].is_run && candidates[index].link_index >= 0 &&
		    candidates[index].seed_index >= 0 &&
		    candidates[index].goal_ms >= 0 &&
		    candidates[index].goal_ms < max_goal_ms)
			eligible++;
	if (eligible == 0)
		return -1;

	pick = (size_t)(draw % (unsigned)eligible);
	for (index = 0; index < candidate_count; index++)
	{
		const sg_defense_patrol_candidate_t *candidate = &candidates[index];

		if (!candidate->is_run || candidate->link_index < 0 ||
		    candidate->seed_index < 0 || candidate->goal_ms < 0 ||
		    candidate->goal_ms >= max_goal_ms)
			continue;
		if (pick-- == 0)
		{
			selected = (int)index;
			break;
		}
	}
	if (selected < 0)
		return -1;

	/* A deliberate circuit does not immediately reverse when another admitted
	 * post-band road exists.  Scan from the drawn choice so different visits
	 * still choose different non-reversing legs. */
	if (eligible > 1 && candidates[selected].seed_index == previous_seed)
		for (index = 1; index < candidate_count; index++)
		{
			size_t alternate = ((size_t)selected + index) % candidate_count;
			const sg_defense_patrol_candidate_t *candidate =
			    &candidates[alternate];

			if (candidate->is_run && candidate->link_index >= 0 &&
			    candidate->seed_index >= 0 && candidate->goal_ms >= 0 &&
			    candidate->goal_ms < max_goal_ms &&
			    candidate->seed_index != previous_seed)
			{
				selected = (int)alternate;
				break;
			}
		}

	if (seed_out)
		*seed_out = candidates[selected].seed_index;
	return candidates[selected].link_index;
}

int SG_DefenseShiftChoose(const sg_defense_shift_request_t *request,
	const sg_defense_shift_candidate_t *candidates, size_t candidate_count,
	int *seed_out)
{
	float threat_length;
	float maximum_distance_squared;
	float best_score = -FLT_MAX;
	int best_link = -1;
	int best_seed = -1;
	size_t index;

	if (seed_out)
		*seed_out = -1;
	if (!request || !candidates || candidate_count == 0 ||
	    !isfinite(request->threat_x) || !isfinite(request->threat_y) ||
	    !isfinite(request->max_distance) || request->max_distance <= 0.0f ||
	    request->max_goal_ms < 0)
		return -1;
	threat_length = hypotf(request->threat_x, request->threat_y);
	if (!isfinite(threat_length) || threat_length < 1.0f)
		return -1;
	maximum_distance_squared = request->max_distance * request->max_distance;
	if (!isfinite(maximum_distance_squared))
		return -1;

	for (index = 0; index < candidate_count; index++)
	{
		const sg_defense_shift_candidate_t *candidate = &candidates[index];
		float distance_squared;
		float distance;
		float forward;
		float lateral;
		float score;

		if (candidate->link_index < 0 || candidate->seed_index < 0 ||
		    candidate->goal_ms < 0 ||
		    candidate->goal_ms > request->max_goal_ms ||
		    !isfinite(candidate->delta_x) ||
		    !isfinite(candidate->delta_y) ||
		    !isfinite(candidate->delta_z))
			continue;
		distance_squared = candidate->delta_x * candidate->delta_x +
		    candidate->delta_y * candidate->delta_y +
		    candidate->delta_z * candidate->delta_z;
		if (!isfinite(distance_squared) || distance_squared < 24.0f * 24.0f ||
		    distance_squared > maximum_distance_squared)
			continue;
		distance = sqrtf(distance_squared);
		forward = (candidate->delta_x * request->threat_x +
		    candidate->delta_y * request->threat_y) / threat_length;
		lateral = fabsf(candidate->delta_x * request->threat_y -
		    candidate->delta_y * request->threat_x) / threat_length;

		/* This is a guarded lateral adjustment, not a miniature charge or
		 * retreat. Reject roads whose primary motion follows the attack line. */
		if (lateral < distance * 0.45f || fabsf(forward) > distance * 0.60f)
			continue;
		score = lateral * 4.0f - fabsf(forward) -
		    (float)candidate->goal_ms * 0.02f;
		if (candidate->seed_index == request->previous_seed)
			score -= 200.0f;
		if (score > best_score ||
		    (score == best_score && candidate->link_index < best_link))
		{
			best_score = score;
			best_link = candidate->link_index;
			best_seed = candidate->seed_index;
		}
	}

	if (seed_out)
		*seed_out = best_seed;
	return best_link;
}

int SG_DefenseShiftRetireIfInvalid(int shift_link, int link_ready,
	int *commit_link)
{
	if (shift_link < 0 || link_ready)
		return 0;
	if (commit_link && *commit_link == shift_link)
		*commit_link = -1;
	return 1;
}

int SG_DefenseCombatChoose(const sg_defense_combat_request_t *request,
	sg_defense_combat_move_t *move_out)
{
	float line_x, line_y, line_length;
	float radial_x, radial_y, radial_length;
	float ring, correction = 0.0f;
	float x, y, length;
	int sign;

	if (move_out)
	{
		move_out->x = 0.0f;
		move_out->y = 0.0f;
		move_out->tangent_sign = 0;
	}
	if (!request || !move_out || !request->enabled || !request->hold_post ||
	    !request->defend_stand || !request->own_flag_home ||
	    !request->engaged || !request->live_enemy ||
	    !request->identity_valid || !request->movement_clear ||
	    !isfinite(request->self_x) || !isfinite(request->self_y) ||
	    !isfinite(request->stand_x) || !isfinite(request->stand_y) ||
	    !isfinite(request->enemy_x) || !isfinite(request->enemy_y) ||
	    !isfinite(request->camp_scale))
		return 0;

	line_x = request->enemy_x - request->self_x;
	line_y = request->enemy_y - request->self_y;
	line_length = hypotf(line_x, line_y);
	if (line_length < 1.0f || !isfinite(line_length))
		return 0;
	radial_x = request->self_x - request->stand_x;
	radial_y = request->self_y - request->stand_y;
	radial_length = hypotf(radial_x, radial_y);
	if (radial_length < 1.0f || radial_length > 128.0f ||
	    !isfinite(radial_length))
		return 0;
	radial_x /= radial_length;
	radial_y /= radial_length;
	ring = 72.0f * request->camp_scale;
	if (ring < 68.0f)
		ring = 68.0f;
	if (ring > 76.0f)
		ring = 76.0f;
	if (radial_length < 52.0f)
		correction = (52.0f - radial_length) / 24.0f;
	else if (radial_length > ring + 12.0f)
		correction = -(radial_length - (ring + 12.0f)) / 40.0f;
	if (correction > 0.75f)
		correction = 0.75f;
	if (correction < -0.75f)
		correction = -0.75f;
	if (request->preferred_tangent_sign == -1 ||
	    request->preferred_tangent_sign == 1)
		sign = request->preferred_tangent_sign;
	else
		sign = ((request->identity + request->phase) & 1) ? 1 : -1;
	x = (float)sign * -line_y / line_length + correction * radial_x;
	y = (float)sign * line_x / line_length + correction * radial_y;
	length = hypotf(x, y);
	if (length < 0.01f || !isfinite(length))
		return 0;
	move_out->x = x / length;
	move_out->y = y / length;
	move_out->tangent_sign = sign;
	return 1;
}

int SG_DefenseCombatProbeAllowed(const sg_defense_combat_probe_t *probe)
{
	if (!probe || !probe->body_clear || !probe->player_clear ||
	    !probe->floor_clear || !isfinite(probe->stand_distance) ||
	    !isfinite(probe->vertical_step) || probe->stand_distance < 48.0f ||
	    probe->stand_distance > 128.0f || fabsf(probe->vertical_step) > 24.0f)
		return 0;
	return 1;
}
