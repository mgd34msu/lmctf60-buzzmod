#include <float.h>
#include <limits.h>

#include "q_shared.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_timed_vault_egress.h"

#define SG_TIMED_VAULT_WATER_LOCALIZE_MAX 192.0f

static qboolean SG_TimedVaultSourceCaptured(const vec3_t origin,
	const vec3_t velocity, const vec3_t source)
{
	vec3_t delta;

	VectorSubtract(source, origin, delta);
	return DotProduct(delta, delta) <=
	           SG_TIMED_VAULT_CAPTURE_DISTANCE *
	               SG_TIMED_VAULT_CAPTURE_DISTANCE &&
	       DotProduct(velocity, velocity) <=
	           SG_TIMED_VAULT_CAPTURE_SPEED * SG_TIMED_VAULT_CAPTURE_SPEED;
}

static qboolean SG_TimedVaultHopArrived(const vec3_t origin,
	const vec3_t destination)
{
	vec3_t delta;

	VectorSubtract(destination, origin, delta);
	return delta[0] * delta[0] + delta[1] * delta[1] <
	           SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
	       delta[2] > -SG_REPLAY_ARRIVE_Z && delta[2] < SG_REPLAY_ARRIVE_Z;
}

int SG_TimedVaultEgressBudgetMs(int controller_kind)
{
	return controller_kind == SG_MECHANISM_CONTROLLER_TIMED_VAULT
	    ? 9000 : 5000;
}

static qboolean SG_TimedVaultRouteScore(const rune_seed_t *seeds,
	int num_seeds, const int *next, int source, const vec3_t final_target,
	float *score_out)
{
	float score = 0.0f;
	int current = source;
	int steps = 0;

	if (!seeds || num_seeds <= 0 || !next || source < 0 ||
	    source >= num_seeds || !final_target || !score_out)
		return false;
	while (next[current] >= 0)
	{
		vec3_t delta;
		int hop = next[current];

		if ((seeds[current].flags & RSF_TOMBSTONE) || hop < 0 ||
		    hop >= num_seeds || hop == current ||
		    (seeds[hop].flags & RSF_TOMBSTONE) || ++steps > num_seeds)
			return false;
		VectorSubtract(seeds[hop].origin, seeds[current].origin, delta);
		score += sqrtf(DotProduct(delta, delta));
		current = hop;
	}
	if (seeds[current].flags & (RSF_WATER | RSF_TOMBSTONE))
		return false;
	{
		vec3_t delta;

		VectorSubtract(final_target, seeds[current].origin, delta);
		score += sqrtf(DotProduct(delta, delta));
	}
	*score_out = score;
	return true;
}

int SG_TimedVaultEgressSourceSelect(const rune_seed_t *seeds, int num_seeds,
	const int *next, const vec3_t origin, const vec3_t final_target,
	sg_timed_vault_source_reachable_fn reachable, void *context)
{
	float best = FLT_MAX;
	int best_seed = -1;
	int i;

	if (!seeds || num_seeds <= 0 || !next || !origin || !final_target ||
	    !reachable)
		return -1;
	for (i = 0; i < num_seeds; i++)
	{
		vec3_t delta;
		float distance, route_score;
		int hop = next[i];

		if (!(seeds[i].flags & RSF_WATER) ||
		    (seeds[i].flags & RSF_TOMBSTONE) || hop < 0 ||
		    hop >= num_seeds || hop == i ||
		    (seeds[hop].flags & RSF_TOMBSTONE))
			continue;
		VectorSubtract(seeds[i].origin, origin, delta);
		distance = DotProduct(delta, delta);
		if (distance > SG_TIMED_VAULT_WATER_LOCALIZE_MAX *
		        SG_TIMED_VAULT_WATER_LOCALIZE_MAX ||
		    !reachable(origin, seeds[i].origin, context) ||
		    !SG_TimedVaultRouteScore(seeds, num_seeds, next, i,
		        final_target, &route_score))
			continue;
		route_score += sqrtf(distance);
		if (route_score >= best)
			continue;
		best = route_score;
		best_seed = i;
	}
	return best_seed;
}

int SG_WaterEscapeIndexBuild(const rune_seed_t *seeds, int num_seeds,
	const rune_link_t *links, int num_links, int *next, int *dist,
	int *incoming, int *next_incoming, int *queue)
{
	int i, head = 0, tail = 0;

	if (!seeds || num_seeds <= 0 || !links || num_links < 0 || !next ||
	    !dist || !incoming || !next_incoming || !queue)
		return 0;
	for (i = 0; i < num_seeds; i++)
	{
		next[i] = -1;
		incoming[i] = -1;
		if (seeds[i].flags & RSF_TOMBSTONE)
			dist[i] = -1;
		else if (seeds[i].flags & RSF_WATER)
			dist[i] = INT_MAX;
		else
		{
			dist[i] = 0;
			queue[tail++] = i;
		}
	}
	for (i = 0; i < num_links; i++)
	{
		const rune_link_t *link = &links[i];

		next_incoming[i] = -1;
		if (link->from < 0 || link->from >= num_seeds || link->to < 0 ||
		    link->to >= num_seeds)
			return 0;
		if (link->action != RL_SWIM ||
		    !(seeds[link->from].flags & RSF_WATER) ||
		    (seeds[link->from].flags & RSF_TOMBSTONE) ||
		    (seeds[link->to].flags & RSF_TOMBSTONE))
			continue;
		next_incoming[i] = incoming[link->to];
		incoming[link->to] = i;
	}
	while (head < tail)
	{
		int to = queue[head++];

		for (i = incoming[to]; i >= 0; i = next_incoming[i])
		{
			const rune_link_t *link = &links[i];

			if (dist[link->from] != INT_MAX)
				continue;
			dist[link->from] = dist[to] + 1;
			next[link->from] = to;
			queue[tail++] = link->from;
		}
	}
	return 1;
}

static qboolean SG_TimedVaultHeapLess(const int *heap, int left, int right,
	const float *score)
{
	int left_seed = heap[left];
	int right_seed = heap[right];

	return score[left_seed] < score[right_seed] ||
	       (score[left_seed] == score[right_seed] && left_seed < right_seed);
}

static void SG_TimedVaultHeapSwap(int *heap, int *heap_pos, int left,
	int right)
{
	int seed = heap[left];

	heap[left] = heap[right];
	heap[right] = seed;
	heap_pos[heap[left]] = left;
	heap_pos[heap[right]] = right;
}

static void SG_TimedVaultHeapUp(int *heap, int *heap_pos, int position,
	const float *score)
{
	while (position > 0)
	{
		int parent = (position - 1) / 2;

		if (!SG_TimedVaultHeapLess(heap, position, parent, score))
			break;
		SG_TimedVaultHeapSwap(heap, heap_pos, position, parent);
		position = parent;
	}
}

static int SG_TimedVaultHeapPop(int *heap, int *heap_pos, int *count,
	const float *score)
{
	int result = heap[0];
	int position = 0;

	(*count)--;
	heap_pos[result] = -2;
	if (*count <= 0)
		return result;
	heap[0] = heap[*count];
	heap_pos[heap[0]] = 0;
	for (;;)
	{
		int left = position * 2 + 1;
		int right = left + 1;
		int best = position;

		if (left < *count && SG_TimedVaultHeapLess(heap, left, best, score))
			best = left;
		if (right < *count && SG_TimedVaultHeapLess(heap, right, best, score))
			best = right;
		if (best == position)
			break;
		SG_TimedVaultHeapSwap(heap, heap_pos, position, best);
		position = best;
	}
	return result;
}

int SG_WaterEscapeTargetIndexBuild(const rune_seed_t *seeds, int num_seeds,
	const rune_link_t *links, int num_links, const vec3_t final_target,
	int *next, float *score, int *incoming, int *next_incoming, int *heap,
	int *heap_pos)
{
	int count = 0;
	int i;

	if (!seeds || num_seeds <= 0 || !links || num_links < 0 ||
	    !final_target || !next || !score || !incoming || !next_incoming ||
	    !heap || !heap_pos)
		return 0;
	for (i = 0; i < num_seeds; i++)
	{
		next[i] = -1;
		incoming[i] = -1;
		heap_pos[i] = -1;
		if ((seeds[i].flags & RSF_TOMBSTONE) ||
		    (seeds[i].flags & RSF_WATER))
			score[i] = FLT_MAX;
		else
		{
			vec3_t delta;

			VectorSubtract(final_target, seeds[i].origin, delta);
			score[i] = sqrtf(DotProduct(delta, delta));
			heap[count] = i;
			heap_pos[i] = count;
			SG_TimedVaultHeapUp(heap, heap_pos, count++, score);
		}
	}
	for (i = 0; i < num_links; i++)
	{
		const rune_link_t *link = &links[i];

		next_incoming[i] = -1;
		if (link->from < 0 || link->from >= num_seeds || link->to < 0 ||
		    link->to >= num_seeds)
			return 0;
		if (link->action != RL_SWIM ||
		    !(seeds[link->from].flags & RSF_WATER) ||
		    (seeds[link->from].flags & RSF_TOMBSTONE) ||
		    (seeds[link->to].flags & RSF_TOMBSTONE))
			continue;
		next_incoming[i] = incoming[link->to];
		incoming[link->to] = i;
	}
	while (count > 0)
	{
		int to = SG_TimedVaultHeapPop(heap, heap_pos, &count, score);

		for (i = incoming[to]; i >= 0; i = next_incoming[i])
		{
			const rune_link_t *link = &links[i];
			vec3_t delta;
			float candidate;

			if (heap_pos[link->from] == -2)
				continue;
			VectorSubtract(seeds[to].origin, seeds[link->from].origin,
			    delta);
			candidate = score[to] + sqrtf(DotProduct(delta, delta));
			if (candidate > score[link->from] ||
			    (candidate == score[link->from] && next[link->from] >= 0 &&
			     to >= next[link->from]))
				continue;
			score[link->from] = candidate;
			next[link->from] = to;
			if (heap_pos[link->from] < 0)
			{
				heap[count] = link->from;
				heap_pos[link->from] = count;
				SG_TimedVaultHeapUp(heap, heap_pos, count++, score);
			}
			else
				SG_TimedVaultHeapUp(heap, heap_pos,
				    heap_pos[link->from], score);
		}
	}
	return 1;
}

qboolean SG_TimedVaultEgressTarget(const rune_seed_t *seeds, int num_seeds,
	const int *next, int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t final_target, vec3_t target_out)
{
	int route_seed = -1;

	return SG_TimedVaultEgressAdvance(seeds, num_seeds, next,
	    controller_kind, waterlevel, origin, final_target, &route_seed,
	    target_out);
}

qboolean SG_TimedVaultEgressAdvance(const rune_seed_t *seeds, int num_seeds,
	const int *next, int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t final_target, int *route_seed,
	vec3_t target_out)

{
	qboolean exact_capture;
	vec3_t velocity = { 0.0f, 0.0f, 0.0f };

	return SG_TimedVaultEgressAdvancePose(seeds, num_seeds, next,
	    controller_kind, waterlevel, origin, velocity, final_target,
	    route_seed, &exact_capture, target_out);
}

qboolean SG_TimedVaultEgressAdvancePose(const rune_seed_t *seeds,
	int num_seeds, const int *next, int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t velocity, const vec3_t final_target,
	int *route_seed, qboolean *exact_capture, vec3_t target_out)
{
	float best = FLT_MAX;
	int best_seed = -1;
	int hop, i, advances = 0;

	if (!origin || !velocity || !final_target || !route_seed ||
	    !exact_capture || !target_out || waterlevel < 0 || waterlevel > 3)
		return false;
	*exact_capture = false;
	if (controller_kind != SG_MECHANISM_CONTROLLER_TIMED_VAULT)
	{
		*route_seed = -1;
		VectorCopy(final_target, target_out);
		return true;
	}
	if (waterlevel < 2)
	{
		*route_seed = -1;
		VectorCopy(final_target, target_out);
		return true;
	}
	if (!seeds || num_seeds <= 0 || !next)
		return false;
	if (*route_seed < -1)
	{
		vec3_t delta;

		if (*route_seed < -num_seeds - 1)
			return false;
		best_seed = -*route_seed - 2;
		if (!(seeds[best_seed].flags & RSF_WATER) ||
		    (seeds[best_seed].flags & RSF_TOMBSTONE) ||
		    next[best_seed] < 0 || next[best_seed] >= num_seeds ||
		    next[best_seed] == best_seed ||
		    (seeds[next[best_seed]].flags & RSF_TOMBSTONE))
			return false;
		VectorSubtract(seeds[best_seed].origin, origin, delta);
		best = DotProduct(delta, delta);
		if (best > SG_TIMED_VAULT_WATER_LOCALIZE_MAX *
		        SG_TIMED_VAULT_WATER_LOCALIZE_MAX)
			return false;
		*exact_capture = true;
		if (!SG_TimedVaultSourceCaptured(origin, velocity,
		        seeds[best_seed].origin))
		{
			VectorCopy(seeds[best_seed].origin, target_out);
			return true;
		}
		*route_seed = best_seed;
	}
	if (*route_seed >= 0)
	{
		vec3_t from_delta, hop_delta;
		int current_hop;

		if (*route_seed >= num_seeds ||
		    (seeds[*route_seed].flags & RSF_TOMBSTONE) ||
		    (!(seeds[*route_seed].flags & RSF_WATER) && waterlevel >= 2))
			return false;
		current_hop = next[*route_seed];
		VectorSubtract(seeds[*route_seed].origin, origin, from_delta);
		if (current_hop >= 0 && current_hop < num_seeds &&
		    current_hop != *route_seed &&
		    !(seeds[current_hop].flags & RSF_TOMBSTONE))
			VectorSubtract(seeds[current_hop].origin, origin, hop_delta);
		else
			VectorCopy(from_delta, hop_delta);
		if ((current_hop == -1 &&
		     !(seeds[*route_seed].flags & RSF_WATER) &&
		     DotProduct(from_delta, from_delta) <=
		         SG_TIMED_VAULT_WATER_LOCALIZE_MAX *
		             SG_TIMED_VAULT_WATER_LOCALIZE_MAX) ||
		    (current_hop >= 0 && current_hop < num_seeds &&
		     current_hop != *route_seed &&
		     !(seeds[current_hop].flags & RSF_TOMBSTONE) &&
		     (DotProduct(from_delta, from_delta) <=
		        SG_TIMED_VAULT_WATER_LOCALIZE_MAX *
		            SG_TIMED_VAULT_WATER_LOCALIZE_MAX ||
		      DotProduct(hop_delta, hop_delta) <=
		        SG_TIMED_VAULT_WATER_LOCALIZE_MAX *
		            SG_TIMED_VAULT_WATER_LOCALIZE_MAX)))
		{
			best_seed = *route_seed;
			best = 0.0f;
		}
		else
			return false;
	}
	if (best_seed < 0)
		for (i = 0; i < num_seeds; i++)
	{
		vec3_t delta;
		float distance;
		int candidate_hop = next[i];

		if (!(seeds[i].flags & RSF_WATER) ||
		    (seeds[i].flags & RSF_TOMBSTONE) || candidate_hop < 0 ||
		    candidate_hop >= num_seeds ||
		    (seeds[candidate_hop].flags & RSF_TOMBSTONE))
			continue;
		VectorSubtract(seeds[i].origin, origin, delta);
		distance = DotProduct(delta, delta);
		if (distance < best)
		{
			best = distance;
			best_seed = i;
		}
	}
	if (best_seed < 0 ||
	    best > SG_TIMED_VAULT_WATER_LOCALIZE_MAX *
	        SG_TIMED_VAULT_WATER_LOCALIZE_MAX)
		return false;
	if (*route_seed == -1 &&
	    !SG_TimedVaultSourceCaptured(origin, velocity,
	        seeds[best_seed].origin))
	{
		*route_seed = -best_seed - 2;
		*exact_capture = true;
		VectorCopy(seeds[best_seed].origin, target_out);
		return true;
	}
	*route_seed = best_seed;
	hop = next[*route_seed];
	while (hop >= 0 && hop < num_seeds)
	{
		if ((seeds[hop].flags & RSF_TOMBSTONE) || hop == *route_seed)
			return false;
		*exact_capture = false;
		if (!(seeds[hop].flags & RSF_WATER) && waterlevel >= 2)
			break;
		if (!SG_TimedVaultHopArrived(origin, seeds[hop].origin))
			break;
		*route_seed = hop;
		hop = next[*route_seed];
		if (++advances > num_seeds)
			return false;
	}
	if (hop == -1 && !((*route_seed >= 0) &&
	                   !(seeds[*route_seed].flags & RSF_WATER)))
		return false;
	if (hop == -1)
	{
		VectorCopy(final_target, target_out);
		return true;
	}
	if (hop < 0 || hop >= num_seeds || hop == *route_seed ||
	    (seeds[hop].flags & RSF_TOMBSTONE))
		return false;
	if (!(seeds[hop].flags & RSF_WATER))
		*exact_capture = false;
	VectorCopy(seeds[hop].origin, target_out);
	return true;
}
