#ifndef SG_DEATH_BELIEF_H
#define SG_DEATH_BELIEF_H

#include <math.h>

/* A public obituary supplies identity and time, never a position.  Recover
 * the freshest position this team had already earned for that client and
 * reject future, stale, malformed, or graph-invalid records. */
static inline int SG_DeathBeliefSeed(const sg_belief_enemy_t *records,
	int count, int victim_client, float now, float stale_seconds,
	int num_seeds)
{
	float freshest = -1.0f;
	int index, seed = -1;

	if (!records || count <= 0 || victim_client < 0 ||
	    !isfinite(now) || !isfinite(stale_seconds) || stale_seconds < 0.0f ||
	    num_seeds <= 0)
		return -1;
	for (index = 0; index < count; index++)
	{
		const sg_belief_enemy_t *record = &records[index];

		if (record->client != victim_client || record->seed < 0 ||
		    record->seed >= num_seeds || !isfinite(record->seen_time) ||
		    record->seen_time > now ||
		    now - record->seen_time > stale_seconds)
			continue;
		if (record->seen_time > freshest)
		{
			freshest = record->seen_time;
			seed = record->seed;
		}
	}
	return seed;
}

#endif
