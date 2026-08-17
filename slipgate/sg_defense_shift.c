/* Threat-responsive movement for the stand defender. */
#include "sg_defense_shift.h"

#include <float.h>
#include <math.h>

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
