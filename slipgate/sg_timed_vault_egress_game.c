#include <limits.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_timed_vault_egress.h"

typedef struct sg_timed_vault_egress_scope_s
{
	const rune_seed_t *seeds;
	const rune_link_t *links;
	int num_seeds;
	int num_links;
	int *next;
	int *dist;
	float *score;
	int *incoming;
	int *next_incoming;
	int *queue;
	int *heap_pos;
} sg_timed_vault_egress_scope_t;

static sg_timed_vault_egress_scope_t sg_timed_vault_egress_scope;

static qboolean SG_TimedVaultSourceReachable(const vec3_t origin,
	const vec3_t source, void *context)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;

	(void)context;
	trace = sg_host.trace((vec_t *)origin, mins, maxs, (vec_t *)source,
	    NULL, MASK_PLAYERSOLID & ~CONTENTS_MONSTER);
	return !trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f;
}

void SG_TimedVaultEgressScopeEnd(void)
{
	if (sg_timed_vault_egress_scope.queue)
		sg_host.level_free(sg_timed_vault_egress_scope.queue);
	if (sg_timed_vault_egress_scope.heap_pos)
		sg_host.level_free(sg_timed_vault_egress_scope.heap_pos);
	if (sg_timed_vault_egress_scope.next_incoming)
		sg_host.level_free(sg_timed_vault_egress_scope.next_incoming);
	if (sg_timed_vault_egress_scope.incoming)
		sg_host.level_free(sg_timed_vault_egress_scope.incoming);
	if (sg_timed_vault_egress_scope.dist)
		sg_host.level_free(sg_timed_vault_egress_scope.dist);
	if (sg_timed_vault_egress_scope.score)
		sg_host.level_free(sg_timed_vault_egress_scope.score);
	if (sg_timed_vault_egress_scope.next)
		sg_host.level_free(sg_timed_vault_egress_scope.next);
	memset(&sg_timed_vault_egress_scope, 0,
	    sizeof(sg_timed_vault_egress_scope));
}

qboolean SG_TimedVaultEgressScopeBegin(const rune_seed_t *seeds,
	int num_seeds, const rune_link_t *links, int num_links)
{
	size_t seed_bytes;
	size_t score_bytes;
	size_t link_bytes;

	if (sg_timed_vault_egress_scope.seeds || !seeds || num_seeds <= 0 ||
	    !links || num_links < 0)
		return false;
	seed_bytes = sizeof(int) * (size_t)num_seeds;
	score_bytes = sizeof(float) * (size_t)num_seeds;
	link_bytes = sizeof(int) * (size_t)(num_links > 0 ? num_links : 1);
	if (seed_bytes > INT_MAX || score_bytes > INT_MAX ||
	    link_bytes > INT_MAX)
		return false;
	sg_timed_vault_egress_scope.next = sg_host.level_alloc((int)seed_bytes);
	sg_timed_vault_egress_scope.dist = sg_host.level_alloc((int)seed_bytes);
	sg_timed_vault_egress_scope.score =
	    sg_host.level_alloc((int)score_bytes);
	sg_timed_vault_egress_scope.incoming =
	    sg_host.level_alloc((int)seed_bytes);
	sg_timed_vault_egress_scope.next_incoming =
	    sg_host.level_alloc((int)link_bytes);
	sg_timed_vault_egress_scope.queue = sg_host.level_alloc((int)seed_bytes);
	sg_timed_vault_egress_scope.heap_pos =
	    sg_host.level_alloc((int)seed_bytes);
	if (!sg_timed_vault_egress_scope.next ||
	    !sg_timed_vault_egress_scope.dist ||
	    !sg_timed_vault_egress_scope.score ||
	    !sg_timed_vault_egress_scope.incoming ||
	    !sg_timed_vault_egress_scope.next_incoming ||
	    !sg_timed_vault_egress_scope.queue ||
	    !sg_timed_vault_egress_scope.heap_pos ||
	    !SG_WaterEscapeIndexBuild(seeds, num_seeds, links, num_links,
	        sg_timed_vault_egress_scope.next,
	        sg_timed_vault_egress_scope.dist,
	        sg_timed_vault_egress_scope.incoming,
	        sg_timed_vault_egress_scope.next_incoming,
	        sg_timed_vault_egress_scope.queue))
	{
		SG_TimedVaultEgressScopeEnd();
		return false;
	}
	sg_timed_vault_egress_scope.seeds = seeds;
	sg_timed_vault_egress_scope.links = links;
	sg_timed_vault_egress_scope.num_seeds = num_seeds;
	sg_timed_vault_egress_scope.num_links = num_links;
	return true;
}

qboolean SG_TimedVaultEgressScopeTarget(int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t velocity, const vec3_t final_target,
	int *route_seed, qboolean *exact_capture, vec3_t target_out)
{
	if (controller_kind != SG_MECHANISM_CONTROLLER_TIMED_VAULT ||
	    (waterlevel < 2 && route_seed && *route_seed == -1))
		return SG_TimedVaultEgressAdvancePose(NULL, 0, NULL, controller_kind,
		    waterlevel, origin, velocity, final_target, route_seed,
		    exact_capture, target_out);
	if (route_seed && *route_seed == -1)
	{
		int source;

		if (!SG_WaterEscapeTargetIndexBuild(
		        sg_timed_vault_egress_scope.seeds,
		        sg_timed_vault_egress_scope.num_seeds,
		        sg_timed_vault_egress_scope.links,
		        sg_timed_vault_egress_scope.num_links, final_target,
		        sg_timed_vault_egress_scope.next,
		        sg_timed_vault_egress_scope.score,
		        sg_timed_vault_egress_scope.incoming,
		        sg_timed_vault_egress_scope.next_incoming,
		        sg_timed_vault_egress_scope.queue,
		        sg_timed_vault_egress_scope.heap_pos))
			return false;
		source = SG_TimedVaultEgressSourceSelect(
		    sg_timed_vault_egress_scope.seeds,
		    sg_timed_vault_egress_scope.num_seeds,
		    sg_timed_vault_egress_scope.next, origin, final_target,
		    SG_TimedVaultSourceReachable, NULL);

		if (source < 0)
			return false;
		*route_seed = -source - 2;
	}
	return SG_TimedVaultEgressAdvancePose(sg_timed_vault_egress_scope.seeds,
	    sg_timed_vault_egress_scope.num_seeds,
	    sg_timed_vault_egress_scope.next, controller_kind, waterlevel,
	    origin, velocity, final_target, route_seed, exact_capture, target_out);
}
