/* sg_rune_proof.c -- scoped nominal gravity for RUNE generation. */
#include <math.h>
#include <string.h>

/* q_shared.h intentionally has no include guard and must precede sg_rune.h. */
#include "q_shared.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_rune_proof.h"

static short sg_rune_scoped_gravity = (short)RUNE_PROOF_GRAVITY;
static int sg_rune_proof_scope_active;

int SG_RuneFunkyGravityCompatible(const float *value)
{
	return value && isfinite(*value) &&
	       *value == (float)SG_RUNE_PROOF_FUNKY_GRAVITY_REQUIRED;
}

int SG_RuneProofHookLateralWindow(float horizontal, float rise)
{
	return isfinite(horizontal) && isfinite(rise) &&
	       horizontal >= 0.0f &&
	       horizontal <= SG_RUNE_PROOF_HOOK_LATERAL_MAX_HORIZONTAL &&
	       rise >= SG_RUNE_PROOF_HOOK_LATERAL_MIN_RISE &&
	       rise <= SG_RUNE_PROOF_HOOK_LATERAL_MAX_RISE;
}

#define SG_RUNE_PROOF_HOOK_RANKS 15
#define SG_RUNE_PROOF_HOOK_REACH_Q8 (768 * 8)
#define SG_RUNE_PROOF_HOOK_LOCAL_Q8 (192 * 8)
#define SG_RUNE_PROOF_HOOK_NEAR_Q8 (448 * 8)
#define SG_RUNE_PROOF_HOOK_MAX_RISE_Q8 (512 * 8)
#define SG_RUNE_PROOF_HOOK_MAX_FALL_Q8 (2048 * 8)
#define SG_RUNE_PROOF_HOOK_CLIMB_Q8 (32 * 8)

static int SG_RuneProofHookRank(const sg_rune_proof_hook_seed_t *from,
	const sg_rune_proof_hook_seed_t *to, size_t component_count)
{
	int64_t dx, dy, horizontal2;
	int32_t dz;
	int category, distance;

	if (!from || !to || from == to || from->component < 0 ||
	    to->component < 0 || (size_t)from->component >= component_count ||
	    (size_t)to->component >= component_count)
		return -1;
	dx = (int64_t)to->origin_q8[0] - from->origin_q8[0];
	dy = (int64_t)to->origin_q8[1] - from->origin_q8[1];
	dz = to->origin_q8[2] - from->origin_q8[2];
	horizontal2 = dx * dx + dy * dy;
	if (horizontal2 > (int64_t)SG_RUNE_PROOF_HOOK_REACH_Q8 *
	        SG_RUNE_PROOF_HOOK_REACH_Q8 ||
	    dz > SG_RUNE_PROOF_HOOK_MAX_RISE_Q8 ||
	    (dz < -SG_RUNE_PROOF_HOOK_MAX_RISE_Q8 &&
	     (!to->water || dz < -SG_RUNE_PROOF_HOOK_MAX_FALL_Q8)))
		return -1;
	if ((!from->water && !from->stable) ||
	    (from->water && (from->waterlevel < 2 || to->water ||
	                     dz <= 128 * 8)))
		return -1;
	if (horizontal2 <= (int64_t)SG_RUNE_PROOF_HOOK_LOCAL_Q8 *
	        SG_RUNE_PROOF_HOOK_LOCAL_Q8)
		distance = 0;
	else if (horizontal2 <= (int64_t)SG_RUNE_PROOF_HOOK_NEAR_Q8 *
	             SG_RUNE_PROOF_HOOK_NEAR_Q8)
		distance = 1;
	else
		distance = 2;
	if (from->component != to->component &&
	    (to->objective_mask & (uint8_t)~from->objective_mask) != 0)
		category = 0;
	else if (from->component != to->component &&
	         dz > SG_RUNE_PROOF_HOOK_CLIMB_Q8)
		category = 1;
	else if (from->component != to->component)
		category = 2;
	else if (dz > SG_RUNE_PROOF_HOOK_CLIMB_Q8)
		category = 3;
	else
		category = 4;
	return category * 3 + distance;
}

size_t SG_RuneProofSelectHookFrontier(
	const sg_rune_proof_hook_frontier_t *frontier)
{
	size_t selected = 0;
	int rank;

	if (!frontier || !frontier->seeds || frontier->seed_count == 0 ||
	    frontier->component_count == 0 || !frontier->component_trials ||
	    !frontier->source_trials || !frontier->source_cursor ||
	    !frontier->component_source_cursor || !frontier->output ||
	    frontier->output_capacity == 0 || frontier->global_limit == 0 ||
	    frontier->global_limit > SG_RUNE_PROOF_HOOK_FRONTIER_MAX ||
	    frontier->component_limit == 0 || frontier->source_limit == 0)
		return 0;
	memset(frontier->component_trials, 0,
	       frontier->component_count * sizeof(*frontier->component_trials));
	memset(frontier->source_trials, 0,
	       frontier->seed_count * sizeof(*frontier->source_trials));
	for (rank = 0; rank < SG_RUNE_PROOF_HOOK_RANKS; rank++)
	{
		int progressed;

		memset(frontier->source_cursor, 0,
		       frontier->seed_count * sizeof(*frontier->source_cursor));
		memset(frontier->component_source_cursor, 0,
		       frontier->component_count *
		           sizeof(*frontier->component_source_cursor));
		do
		{
			size_t component;

			progressed = 0;
			for (component = 0; component < frontier->component_count;
			     component++)
			{
				size_t offset;
				size_t first = frontier->component_source_cursor[component];

				if (frontier->component_trials[component] >=
				    frontier->component_limit)
					continue;
				for (offset = 0; offset < frontier->seed_count; offset++)
				{
					size_t from = (first + offset) % frontier->seed_count;
					size_t to;

					if ((size_t)frontier->seeds[from].component != component ||
					    frontier->source_trials[from] >= frontier->source_limit)
						continue;
					for (to = frontier->source_cursor[from];
					     to < frontier->seed_count; to++)
					{
						sg_rune_proof_hook_candidate_t *candidate;

						frontier->source_cursor[from] = to + 1;
						if (SG_RuneProofHookRank(&frontier->seeds[from],
						        &frontier->seeds[to],
						        frontier->component_count) != rank)
							continue;
						if (selected >= frontier->output_capacity ||
						    selected >= frontier->global_limit)
							return selected;
						candidate = &frontier->output[selected++];
						memset(candidate, 0, sizeof(*candidate));
						candidate->from = (int)from;
						candidate->to = (int)to;
						candidate->rank = (uint8_t)rank;
						frontier->component_trials[component]++;
						frontier->source_trials[from]++;
						frontier->component_source_cursor[component] =
						    (from + 1) % frontier->seed_count;
						progressed = 1;
						break;
					}
					if (to < frontier->seed_count)
						break;
				}
			}
		} while (progressed);
	}
	return selected;
}

int SG_RuneProofObjectiveRunCandidate(
	const sg_rune_proof_objective_run_seed_t *from,
	const sg_rune_proof_objective_run_seed_t *to,
	uint8_t objective_bit)
{
	int64_t dx, dy, dz, horizontal2;
	const int64_t minimum = SG_RUNE_PROOF_OBJECTIVE_RUN_MIN_HORIZONTAL_Q8;
	const int64_t maximum = SG_RUNE_PROOF_OBJECTIVE_RUN_MAX_HORIZONTAL_Q8;

	if (!from || !to || (objective_bit != 1U && objective_bit != 2U) ||
	    !(from->forward_mask & objective_bit) || from->component == to->component ||
	    !from->stable || !to->stable || from->waterlevel != 0U ||
	    to->waterlevel != 0U)
		return 0;
	dx = (int64_t)to->origin_q8[0] - from->origin_q8[0];
	dy = (int64_t)to->origin_q8[1] - from->origin_q8[1];
	dz = (int64_t)to->origin_q8[2] - from->origin_q8[2];
	horizontal2 = dx * dx + dy * dy;
	return horizontal2 > minimum * minimum &&
	       horizontal2 <= maximum * maximum &&
	       dz >= -SG_RUNE_PROOF_OBJECTIVE_RUN_MAX_VERTICAL_Q8 &&
	       dz <= SG_RUNE_PROOF_OBJECTIVE_RUN_MAX_VERTICAL_Q8;
}

int SG_RuneProofObjectiveRunReplayAccepted(int edge_seek, int airborne)
{
	return !edge_seek && !airborne;
}

int SG_RuneProofScopeBegin(float gravity)
{
	if (sg_rune_proof_scope_active || !isfinite(gravity) ||
	    gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
	    gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
	    (SG_RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED &&
	     gravity != (float)(short)gravity))
		return 0;
	sg_rune_scoped_gravity = (short)gravity;
	sg_rune_proof_scope_active = 1;
	return 1;
}

void SG_RuneProofScopeEnd(void)
{
	sg_rune_scoped_gravity = (short)RUNE_PROOF_GRAVITY;
	sg_rune_proof_scope_active = 0;
}

short SG_RuneProofGravity(void)
{
	return sg_rune_proof_scope_active
		? sg_rune_scoped_gravity : (short)RUNE_PROOF_GRAVITY;
}

int SG_RuneProofScopeActive(void)
{
	return sg_rune_proof_scope_active;
}
