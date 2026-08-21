#ifndef SG_COMBAT_ALERT_POLICY_H
#define SG_COMBAT_ALERT_POLICY_H

#include <math.h>

#include "sg_local.h"

#define SG_COMBAT_ALERT_FRESH_SECONDS 3.0f
#define SG_COMBAT_ALERT_MAX_RANGE 1200.0f

typedef struct sg_combat_alert_selection_s
{
	int client;
	float range;
	float seen_time;
} sg_combat_alert_selection_t;

static inline qboolean SG_CombatAlertSelect(
	const sg_belief_enemy_t *rows, int row_count, const rune_t *rune,
	const int *goal_field, int maxclients, const vec3_t origin, float now,
	sg_combat_alert_selection_t *out)
{
	sg_combat_alert_selection_t best = { .client = -1 };
	int index;

	if (!rows || row_count <= 0 || !rune || !rune->seeds ||
	    rune->hdr.num_seeds <= 0 || !goal_field || maxclients <= 0 ||
	    !origin || !isfinite(now) || !out)
		return false;
	for (index = 0; index < row_count; index++)
	{
		const sg_belief_enemy_t *row = &rows[index];
		float dx, dy, dz, range;

		if (row->client < 0 || row->client >= maxclients || row->seed < 0 ||
		    row->seed >= rune->hdr.num_seeds || !isfinite(row->seen_time) ||
		    row->seen_time < 0.0f || row->seen_time > now ||
		    now - row->seen_time >= SG_COMBAT_ALERT_FRESH_SECONDS ||
		    goal_field[row->seed] < 0 || goal_field[row->seed] >= SG_FIELD_INF)
			continue;
		dx = rune->seeds[row->seed].origin[0] - origin[0];
		dy = rune->seeds[row->seed].origin[1] - origin[1];
		dz = rune->seeds[row->seed].origin[2] - origin[2];
		range = sqrtf(dx * dx + dy * dy + dz * dz);
		if (!isfinite(range) || range < 0.0f ||
		    range >= SG_COMBAT_ALERT_MAX_RANGE)
			continue;
		if (best.client < 0 || range < best.range ||
		    (range == best.range && row->seen_time > best.seen_time) ||
		    (range == best.range && row->seen_time == best.seen_time &&
		     row->client < best.client))
		{
			best.client = row->client;
			best.range = range;
			best.seen_time = row->seen_time;
		}
	}
	if (best.client < 0)
		return false;
	*out = best;
	return true;
}

#endif
