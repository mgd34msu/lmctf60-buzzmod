/* sg_compound_publication.c -- atomic loader replay bindings for compounds. */
#include "../g_local.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "sg_compound_publication.h"
#include "sg_compound_publication_internal.h"
#include "sg_compound_action_publication.h"
#include "sg_local.h"


_Static_assert(sizeof(short) == sizeof(uint16_t),
	"compound angle bias requires protocol-width signed shorts");

sg_compound_publication_result_t CompoundPublicationResult(
	sg_compound_publication_status_t status, rune_reject_reason_t reason,
	uint32_t link_index)
{
	sg_compound_publication_result_t result;

	result.status = status;
	result.reason = reason;
	result.link_index = link_index;
	return result;
}

int CompoundPublicationFloatBitsEqual(float first, float second)
{
	unsigned char first_bits[sizeof(first)];
	unsigned char second_bits[sizeof(second)];

	memcpy(first_bits, &first, sizeof(first_bits));
	memcpy(second_bits, &second, sizeof(second_bits));
	return memcmp(first_bits, second_bits, sizeof(first_bits)) == 0;
}

int CompoundPublicationVectorEqual(const float first[3],
	const float second[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		if (!CompoundPublicationFloatBitsEqual(first[axis], second[axis]))
			return 0;
	return 1;
}

static int CompoundPublicationLinkEqual(const rune_link_t *first,
	const rune_link_t *second)
{
	return first && second && first->from == second->from &&
	       first->to == second->to && first->action == second->action &&
	       first->provenance == second->provenance &&
	       first->min_speed == second->min_speed &&
	       first->heading == second->heading &&
	       first->heading_slack == second->heading_slack &&
	       first->exit_speed == second->exit_speed &&
	       first->cost_ms == second->cost_ms &&
	       CompoundPublicationVectorEqual(first->anchor, second->anchor) &&
	       CompoundPublicationVectorEqual(first->mechanism_anchor,
	                                      second->mechanism_anchor) &&
	       first->sweep_clear_ms == second->sweep_clear_ms &&
	       first->mode == second->mode;
}

static int CompoundPublicationSeedEqual(const rune_seed_t *first,
	const rune_seed_t *second)
{
	return first && second &&
	       CompoundPublicationVectorEqual(first->origin, second->origin) &&
	       first->area_hint == second->area_hint &&
	       first->flags == second->flags;
}

static int CompoundPublicationPositiveZero3(const float value[3])
{
	int axis;

	if (!value)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (value[axis] != 0.0f || signbit(value[axis]))
			return 0;
	return 1;
}

static int CompoundPublicationFinite3(const float value[3])
{
	int axis;

	if (!value)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(value[axis]))
			return 0;
	return 1;
}

static int CompoundPublicationControlAngleValid(float angle)
{
	return isfinite(angle) && angle >= -180.0f && angle < 180.0f &&
	       SHORT2ANGLE((short)ANGLE2SHORT(angle)) == angle;
}

static int CompoundPublicationWorldRange3(const float value[3])
{
	int axis;

	if (!value)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(value[axis]) ||
		    value[axis] < (float)SG_RUNE_PROOF_WORLD_FIXED_MIN /
		                      SG_RUNE_PROOF_WORLD_FIXED_SCALE ||
		    value[axis] > (float)SG_RUNE_PROOF_WORLD_FIXED_MAX /
		                      SG_RUNE_PROOF_WORLD_FIXED_SCALE)
			return 0;
	return 1;
}

static int CompoundPublicationWorldLattice3(const float value[3])
{
	int axis;

	if (!CompoundPublicationWorldRange3(value))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled;

		if (value[axis] == 0.0f && signbit(value[axis]))
			return 0;
		scaled = value[axis] * SG_RUNE_PROOF_WORLD_FIXED_SCALE;
		if (scaled != (float)(short)scaled)
			return 0;
	}
	return 1;
}

int CompoundPublicationRuneShapeValid(const rune_t *rune)
{
	return rune && rune->hdr.num_seeds > 0 &&
	       rune->hdr.num_seeds <= RUNE_MAX_SEEDS &&
	       rune->hdr.num_links >= 0 &&
	       rune->hdr.num_links <= RUNE_MAX_LINKS && rune->seeds &&
	       (rune->hdr.num_links == 0 || rune->links);
}

int CompoundPublicationNativeLinkValid(const rune_t *rune,
	const rune_link_t *link)
{
	const rune_seed_t *source;
	const rune_seed_t *destination;

	if (!CompoundPublicationRuneShapeValid(rune) || !link ||
	    link->from < 0 || link->from >= rune->hdr.num_seeds ||
	    link->to < 0 || link->to >= rune->hdr.num_seeds ||
	    link->from == link->to)
		return 0;
	source = &rune->seeds[link->from];
	destination = &rune->seeds[link->to];
	return (link->action == RL_DOOR_SWIM || link->action == RL_DOOR_DROP ||
	        link->action == RL_DOOR_HOOK) &&
	       link->provenance == RL_CONTRACTED &&
	       link->mode == RLCM_PREOPEN &&
	       link->min_speed == 0 &&
	       link->cost_ms >= RUNE_MIN_COST_MS &&
	       link->cost_ms <= RUNE_MAX_COST_MS &&
	       link->cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       link->sweep_clear_ms > 0 &&
	       link->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       link->sweep_clear_ms <= (unsigned short)link->cost_ms &&
	       (source->flags & ~(RSF_WATER | RSF_TOMBSTONE)) == 0 &&
	       (destination->flags & ~(RSF_WATER | RSF_TOMBSTONE)) == 0 &&
	       (source->flags & RSF_TOMBSTONE) == 0 &&
	       (destination->flags & RSF_TOMBSTONE) == 0 &&
	       CompoundPublicationWorldLattice3(source->origin) &&
	       CompoundPublicationWorldRange3(destination->origin) &&
	       CompoundPublicationWorldLattice3(link->mechanism_anchor) &&
	       ((link->action == RL_DOOR_SWIM &&
	         (source->flags & RSF_WATER) != 0 &&
	         link->heading == 0 && link->heading_slack == 0 &&
	         CompoundPublicationPositiveZero3(link->anchor)) ||
	        (link->action == RL_DOOR_DROP &&
	         (source->flags & RSF_WATER) == 0 &&
	         link->heading_slack == SG_RUNE_PROOF_DROP_CONTROL_MARKER &&
	         CompoundPublicationWorldLattice3(link->anchor)) ||
	        (link->action == RL_DOOR_HOOK &&
	         (source->flags & RSF_WATER) != 0 &&
	         (destination->flags & RSF_WATER) == 0 && link->heading == 0 &&
	         link->heading_slack ==
	             SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER &&
	         CompoundPublicationFinite3(link->anchor) &&
	         link->anchor[PITCH] >= -89.0f &&
	         link->anchor[PITCH] <= 89.0f &&
	         link->anchor[ROLL] >= 1.0f &&
	         link->anchor[ROLL] <= RUNE_HOOK_MAX_RAY &&
	         CompoundPublicationControlAngleValid(link->anchor[PITCH]) &&
	         CompoundPublicationControlAngleValid(link->anchor[YAW])));
}

int CompoundPublicationMechanismEqual(
	const sg_compound_world_preopen_t *first,
	const sg_compound_world_preopen_t *second)
{
	return first && second && first->trigger == second->trigger &&
	       first->member == second->member &&
	       CompoundPublicationVectorEqual(first->bottom_origin,
	                                      second->bottom_origin) &&
	       CompoundPublicationVectorEqual(first->top_origin,
	                                      second->top_origin) &&
	       CompoundPublicationVectorEqual(first->member_mins,
	                                      second->member_mins) &&
	       CompoundPublicationVectorEqual(first->member_maxs,
	                                      second->member_maxs) &&
	       CompoundPublicationVectorEqual(first->fixed_angles,
	                                      second->fixed_angles) &&
	       CompoundPublicationFloatBitsEqual(first->speed, second->speed) &&
	       CompoundPublicationFloatBitsEqual(first->wait, second->wait) &&
	       CompoundPublicationFloatBitsEqual(first->inert_effect_delay,
	                                        second->inert_effect_delay) &&
	       first->trigger_key == second->trigger_key &&
	       first->mover_key == second->mover_key &&
	       first->axis == second->axis;
}

static int CompoundPublicationPmoveEqualExceptAngles(
	const pmove_state_t *first, const pmove_state_t *second)
{
	int axis;

	if (!first || !second || first->pm_type != second->pm_type ||
	    first->pm_flags != second->pm_flags ||
	    first->pm_time != second->pm_time ||
	    first->gravity != second->gravity)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (first->origin[axis] != second->origin[axis] ||
		    first->velocity[axis] != second->velocity[axis])
			return 0;
	return 1;
}

static short CompoundPublicationAngleDifference(short live, short expected)
{
	uint16_t difference = (uint16_t)live - (uint16_t)expected;
	short result;

	memcpy(&result, &difference, sizeof(result));
	return result;
}

static int CompoundPublicationCheckpointMetaEqual(
	const sg_compound_publication_checkpoint_t *first,
	const sg_compound_publication_checkpoint_t *second)
{
	return first && second && first->grounded == second->grounded &&
	       first->watertype == second->watertype &&
	       first->waterlevel == second->waterlevel &&
	       CompoundPublicationFloatBitsEqual(first->old_frame_z,
	                                        second->old_frame_z);
}

int SG_CompoundPublicationCaptureAngleBias(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	sg_compound_publication_angle_bias_t *bias_out)
{
	sg_compound_publication_angle_bias_t candidate;
	int axis;

	if (!expected || !live || !bias_out ||
	    !CompoundPublicationPmoveEqualExceptAngles(&expected->pms,
	                                              &live->pms) ||
	    !CompoundPublicationPmoveEqualExceptAngles(&expected->old_pms,
	                                              &live->old_pms) ||
	    !CompoundPublicationCheckpointMetaEqual(expected, live))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		candidate.axis[axis] = CompoundPublicationAngleDifference(
			live->pms.delta_angles[axis],
			expected->pms.delta_angles[axis]);
		if (candidate.axis[axis] != CompoundPublicationAngleDifference(
		    live->old_pms.delta_angles[axis],
		    expected->old_pms.delta_angles[axis]))
			return 0;
	}
	*bias_out = candidate;
	return 1;
}

int SG_CompoundPublicationCheckpointMatches(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	const sg_compound_publication_angle_bias_t *bias)
{
	int axis;

	if (!expected || !live || !bias ||
	    !CompoundPublicationPmoveEqualExceptAngles(&expected->pms,
	                                              &live->pms) ||
	    !CompoundPublicationPmoveEqualExceptAngles(&expected->old_pms,
	                                              &live->old_pms) ||
	    !CompoundPublicationCheckpointMetaEqual(expected, live))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (CompoundPublicationAngleDifference(
		        live->pms.delta_angles[axis],
		        expected->pms.delta_angles[axis]) != bias->axis[axis] ||
		    CompoundPublicationAngleDifference(
		        live->old_pms.delta_angles[axis],
		        expected->old_pms.delta_angles[axis]) != bias->axis[axis])
			return 0;
	return 1;
}

int CompoundPublicationProofValid(const rune_link_t *link,
	const sg_compound_swim_proof_t *proof)
{
	long long touch_frame_end;
	long long total;

	if (!link || !proof || proof->touch_ms <= 0 ||
	    proof->touch_ms > RUNE_MAX_COST_MS ||
	    proof->touch_ms % SG_RUNE_PROOF_PMOVE_SUBSTEP_MS != 0 ||
	    proof->touch_frame_end_ms <= 0 ||
	    proof->touch_frame_end_ms > RUNE_MAX_COST_MS ||
	    proof->touch_frame_end_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    proof->mover_top_ms < 2 * SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    proof->mover_top_ms > RUNE_MAX_COST_MS ||
	    proof->mover_top_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    proof->suffix_start_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    proof->suffix_start_ms > RUNE_MAX_COST_MS ||
	    proof->suffix_start_ms !=
	        proof->mover_top_ms - SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    proof->arrival_ms <= 0 ||
	    proof->arrival_ms > RUNE_MAX_COST_MS ||
	    proof->arrival_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    proof->sweep_clear_ms <= 0 ||
	    proof->sweep_clear_ms > RUNE_MAX_COST_MS ||
	    proof->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    proof->sweep_clear_ms > proof->arrival_ms ||
	    proof->total_cost_ms < RUNE_MIN_COST_MS ||
	    proof->total_cost_ms > RUNE_MAX_COST_MS ||
	    proof->total_cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0)
		return 0;
	touch_frame_end =
		((long long)proof->touch_ms +
		 SG_RUNE_PROOF_SERVER_FRAME_MS - 1LL) /
		SG_RUNE_PROOF_SERVER_FRAME_MS * SG_RUNE_PROOF_SERVER_FRAME_MS;
	total = (long long)proof->touch_frame_end_ms +
	        proof->suffix_start_ms + proof->arrival_ms;
	return touch_frame_end == proof->touch_frame_end_ms &&
	       total == proof->total_cost_ms &&
	       proof->total_cost_ms == link->cost_ms &&
	       proof->exit_speed == link->exit_speed &&
	       proof->sweep_clear_ms == link->sweep_clear_ms;
}

static int CompoundPublicationBindingTimingValid(
	const sg_compound_publication_binding_t *binding)
{
	sg_compound_swim_proof_t proof;

	if (!binding)
		return 0;
	memset(&proof, 0, sizeof(proof));
	proof.touch_ms = binding->touch_ms;
	proof.touch_frame_end_ms = binding->touch_frame_end_ms;
	proof.mover_top_ms = binding->mover_top_ms;
	proof.suffix_start_ms = binding->suffix_start_ms;
	proof.arrival_ms = binding->arrival_ms;
	proof.sweep_clear_ms = binding->sweep_clear_ms;
	proof.total_cost_ms = binding->total_cost_ms;
	proof.exit_speed = binding->link.exit_speed;
	return CompoundPublicationProofValid(&binding->link, &proof);
}

static int CompoundPublicationBindingMatchesRune(
	const rune_t *rune,
	const sg_compound_publication_binding_t *binding)
{
	const rune_link_t *link;
	sg_hook_replay_spec_t hook_spec;

	if (!CompoundPublicationRuneShapeValid(rune) || !binding ||
	    binding->link_index >= (uint32_t)rune->hdr.num_links)
		return 0;
	link = &rune->links[binding->link_index];
	return CompoundPublicationNativeLinkValid(rune, link) &&
	       CompoundPublicationLinkEqual(&binding->link, link) &&
	       CompoundPublicationSeedEqual(&binding->source_seed,
	                                    &rune->seeds[link->from]) &&
	       CompoundPublicationSeedEqual(&binding->destination_seed,
	                                    &rune->seeds[link->to]) &&
	       binding->source.pms.origin[0] ==
	           (short)(binding->source_seed.origin[0] * 8.0f) &&
	       binding->source.pms.origin[1] ==
	           (short)(binding->source_seed.origin[1] * 8.0f) &&
	       binding->source.pms.origin[2] ==
	           (short)(binding->source_seed.origin[2] * 8.0f) &&
	       CompoundPublicationBindingTimingValid(binding) &&
	       (link->action != RL_DOOR_HOOK ||
	        SG_CompoundHookPublicationPlan(binding, &hook_spec));
}

void SG_CompoundPublicationDestroy(sg_compound_publication_t *publication)
{
	sg_compound_publication_free_fn deallocate;

	if (!publication)
		return;
	deallocate = publication->deallocate;
	if (publication->mechanisms)
		deallocate(publication->mechanisms);
	if (publication->hook_proofs)
		deallocate(publication->hook_proofs);
	if (publication->bindings)
		deallocate(publication->bindings);
	deallocate(publication);
}

size_t SG_CompoundPublicationCount(const rune_t *rune)
{
	return CompoundPublicationRuneShapeValid(rune) &&
	       rune->compound_publication
	    ? rune->compound_publication->binding_count : 0;
}

const sg_compound_publication_binding_t *SG_CompoundPublicationBinding(
	const rune_t *rune, uint32_t link_index)
{
	const sg_compound_publication_t *publication;
	size_t low, high;

	if (!CompoundPublicationRuneShapeValid(rune) ||
	    !rune->compound_publication || rune->hdr.num_links == 0 ||
	    link_index >= (uint32_t)rune->hdr.num_links)
		return NULL;
	publication = rune->compound_publication;
	low = 0;
	high = publication->binding_count;
	while (low < high)
	{
		size_t middle = low + (high - low) / 2;
		uint32_t candidate = publication->bindings[middle].link_index;

		if (candidate < link_index)
			low = middle + 1;
		else
			high = middle;
	}
	if (low >= publication->binding_count ||
	    publication->bindings[low].link_index != link_index ||
	    !CompoundPublicationBindingMatchesRune(
	        rune, &publication->bindings[low]) ||
	    (publication->bindings[low].link.action == RL_DOOR_HOOK &&
	     (!publication->hook_proofs ||
	      memcmp(&publication->bindings[low].hook_proof,
	             &publication->hook_proofs[low],
	             sizeof(publication->bindings[low].hook_proof)) != 0)))
		return NULL;
	return &publication->bindings[low];
}

const sg_compound_world_preopen_t *SG_CompoundPublicationMechanism(
	const rune_t *rune,
	const sg_compound_publication_binding_t *binding)
{
	const sg_compound_publication_t *publication;
	const sg_compound_publication_binding_t *checked;
	size_t index;

	if (!CompoundPublicationRuneShapeValid(rune) ||
	    !rune->compound_publication || !binding)
		return NULL;
	publication = rune->compound_publication;
	for (index = 0; index < publication->binding_count; index++)
		if (&publication->bindings[index] == binding)
			break;
	if (index == publication->binding_count)
		return NULL;
	checked = SG_CompoundPublicationBinding(rune, binding->link_index);
	if (checked != binding ||
	    binding->mechanism_index >= publication->mechanism_count)
		return NULL;
	return &publication->mechanisms[binding->mechanism_index];
}

sg_compound_publication_result_t SG_CompoundPublicationRevalidate(
	const rune_t *rune)
{
	const sg_compound_publication_t *publication;
	size_t index;
	size_t compound_count = 0;
	int candidate_count = 0;
	rune_reject_reason_t reason;

	if (!CompoundPublicationRuneShapeValid(rune))
		return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_INVALID,
		                                  RLR_BAD_CONTROL_POLICY,
		                                  SG_COMPOUND_PUBLICATION_INDEX_NONE);
	if (!rune->compound_publication)
	{
		for (index = 0; index < (size_t)rune->hdr.num_links; index++)
			if (rune->links[index].action == RL_DOOR_SWIM ||
			    rune->links[index].action == RL_DOOR_DROP ||
			    rune->links[index].action == RL_DOOR_HOOK)
				return CompoundPublicationResult(
					SG_COMPOUND_PUBLICATION_INVALID,
					RLR_BAD_CONTROL_POLICY, (uint32_t)index);
		return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_OK,
		                                  RLR_OK,
		                                  SG_COMPOUND_PUBLICATION_INDEX_NONE);
	}
	publication = rune->compound_publication;
	for (index = 0; index < (size_t)rune->hdr.num_links; index++)
		if (rune->links[index].action == RL_DOOR_SWIM ||
		    rune->links[index].action == RL_DOOR_DROP ||
		    rune->links[index].action == RL_DOOR_HOOK)
			compound_count++;
	if (compound_count != publication->binding_count)
		return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_MISMATCH,
		                                  RLR_BAD_CONTROL_POLICY,
		                                  SG_COMPOUND_PUBLICATION_INDEX_NONE);
	reason = SG_CompoundWorldEnumeratePreopen(NULL, 0, &candidate_count);
	if (reason != RLR_OK || candidate_count < 0 ||
	    (size_t)candidate_count < publication->mechanism_count)
		return CompoundPublicationResult(
			SG_COMPOUND_PUBLICATION_WORLD_DRIFT,
			reason == RLR_OK ? RLR_MECHANISM_UNRESOLVED : reason,
			SG_COMPOUND_PUBLICATION_INDEX_NONE);
	for (index = 0; index < publication->binding_count; index++)
	{
		const sg_compound_publication_binding_t *binding =
			&publication->bindings[index];
		const sg_compound_world_preopen_t *stored;
		sg_compound_world_preopen_t current;
		struct edict_s *member = NULL;
		rune_reject_reason_t resolve_reason;

		if ((index > 0 && binding->link_index <=
		                  publication->bindings[index - 1].link_index) ||
		    !CompoundPublicationBindingMatchesRune(rune, binding) ||
		    (binding->link.action == RL_DOOR_HOOK &&
		     (!publication->hook_proofs ||
		      memcmp(&binding->hook_proof,
		             &publication->hook_proofs[index],
		             sizeof(binding->hook_proof)) != 0)) ||
		    binding->mechanism_index >= publication->mechanism_count)
			return CompoundPublicationResult(
				SG_COMPOUND_PUBLICATION_MISMATCH, RLR_BAD_CONTROL_POLICY,
				binding->link_index);
		stored = &publication->mechanisms[binding->mechanism_index];
		if (!SG_CompoundWorldResolvedMember(stored, &member) ||
		    member != stored->member ||
		    !SG_CompoundWorldPreopenHintMatches(
		        stored, binding->canonical_hint))
			return CompoundPublicationResult(
				SG_COMPOUND_PUBLICATION_WORLD_DRIFT,
				RLR_MECHANISM_UNRESOLVED, binding->link_index);
		memset(&current, 0, sizeof(current));
		resolve_reason = SG_CompoundWorldResolvePreopen(
			rune->links[binding->link_index].mechanism_anchor, &current);
		if (resolve_reason != RLR_OK ||
		    !CompoundPublicationMechanismEqual(stored, &current))
			return CompoundPublicationResult(
				SG_COMPOUND_PUBLICATION_WORLD_DRIFT,
				resolve_reason == RLR_OK ? RLR_MECHANISM_UNRESOLVED :
				resolve_reason,
				binding->link_index);
	}
	return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_OK, RLR_OK,
	                                 SG_COMPOUND_PUBLICATION_INDEX_NONE);
}

const char *SG_CompoundPublicationStatusName(
	sg_compound_publication_status_t status)
{
	switch (status)
	{
	case SG_COMPOUND_PUBLICATION_OK: return "ok";
	case SG_COMPOUND_PUBLICATION_INVALID: return "invalid";
	case SG_COMPOUND_PUBLICATION_ALLOCATION: return "allocation";
	case SG_COMPOUND_PUBLICATION_MECHANISM: return "mechanism";
	case SG_COMPOUND_PUBLICATION_SOURCE: return "source";
	case SG_COMPOUND_PUBLICATION_REPLAY: return "replay";
	case SG_COMPOUND_PUBLICATION_MISMATCH: return "mismatch";
	case SG_COMPOUND_PUBLICATION_WORLD_DRIFT: return "world-drift";
	default: return "unknown";
	}
}
