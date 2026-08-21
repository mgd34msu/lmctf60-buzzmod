/* sg_compound_publication.c -- atomic loader replay bindings for compounds. */
#include "../g_local.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "sg_compound_publication.h"
#include "sg_local.h"

struct sg_compound_publication_s
{
	size_t binding_count;
	size_t mechanism_count;
	sg_compound_publication_binding_t *bindings;
	sg_compound_world_preopen_t *mechanisms;
	sg_compound_publication_free_fn deallocate;
};

_Static_assert(sizeof(short) == sizeof(uint16_t),
	"compound angle bias requires protocol-width signed shorts");

static sg_compound_publication_result_t CompoundPublicationResult(
	sg_compound_publication_status_t status, rune_reject_reason_t reason,
	uint32_t link_index)
{
	sg_compound_publication_result_t result;

	result.status = status;
	result.reason = reason;
	result.link_index = link_index;
	return result;
}

static int CompoundPublicationSize(size_t count, size_t item_size,
	int *size_out)
{
	if (!size_out || count == 0 || item_size == 0 ||
	    count > (size_t)INT_MAX / item_size)
		return 0;
	*size_out = (int)(count * item_size);
	return 1;
}

static int CompoundPublicationFloatBitsEqual(float first, float second)
{
	unsigned char first_bits[sizeof(first)];
	unsigned char second_bits[sizeof(second)];

	memcpy(first_bits, &first, sizeof(first_bits));
	memcpy(second_bits, &second, sizeof(second_bits));
	return memcmp(first_bits, second_bits, sizeof(first_bits)) == 0;
}

static int CompoundPublicationVectorEqual(const float first[3],
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

static int CompoundPublicationRuneShapeValid(const rune_t *rune)
{
	return rune && rune->hdr.num_seeds > 0 &&
	       rune->hdr.num_seeds <= RUNE_MAX_SEEDS &&
	       rune->hdr.num_links >= 0 &&
	       rune->hdr.num_links <= RUNE_MAX_LINKS && rune->seeds &&
	       (rune->hdr.num_links == 0 || rune->links);
}

static int CompoundPublicationNativeLinkValid(const rune_t *rune,
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
	return (link->action == RL_DOOR_SWIM || link->action == RL_DOOR_DROP) &&
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
	         CompoundPublicationWorldLattice3(link->anchor)));
}

static int CompoundPublicationMechanismEqual(
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

static void CompoundPublicationCheckpointSource(
	sg_compound_publication_checkpoint_t *checkpoint,
	const sg_compound_swim_source_t *source)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	checkpoint->pms = source->phantom.pms;
	checkpoint->old_pms = source->phantom.old_pms;
	checkpoint->grounded = source->phantom.groundentity;
	checkpoint->watertype = source->phantom.watertype;
	checkpoint->waterlevel = source->phantom.waterlevel;
	checkpoint->old_frame_z = source->old_frame_z;
}

static void CompoundPublicationCheckpointSuffix(
	sg_compound_publication_checkpoint_t *checkpoint,
	const sg_compound_swim_proof_t *proof)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	checkpoint->pms = proof->suffix_pms;
	checkpoint->old_pms = proof->suffix_old_pms;
	checkpoint->grounded = proof->suffix_groundentity;
	checkpoint->watertype = proof->suffix_watertype;
	checkpoint->waterlevel = proof->suffix_waterlevel;
	checkpoint->old_frame_z = proof->suffix_old_frame_z;
}

static void CompoundPublicationCheckpointDropSource(
	sg_compound_publication_checkpoint_t *checkpoint,
	const sg_compound_drop_proof_t *proof)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	checkpoint->pms = proof->source_pms;
	checkpoint->old_pms = proof->source_old_pms;
	checkpoint->grounded = proof->source_groundentity;
	checkpoint->watertype = proof->source_watertype;
	checkpoint->waterlevel = proof->source_waterlevel;
	checkpoint->old_frame_z = proof->source_old_frame_z;
}

static void CompoundPublicationCheckpointDropSuffix(
	sg_compound_publication_checkpoint_t *checkpoint,
	const sg_compound_drop_proof_t *proof)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	checkpoint->pms = proof->suffix_pms;
	checkpoint->old_pms = proof->suffix_old_pms;
	checkpoint->grounded = proof->suffix_groundentity;
	checkpoint->watertype = proof->suffix_watertype;
	checkpoint->waterlevel = proof->suffix_waterlevel;
	checkpoint->old_frame_z = proof->suffix_old_frame_z;
}

static int CompoundPublicationCheckpointInternallyValid(
	const sg_compound_publication_checkpoint_t *checkpoint,
	const float origin[3], const float velocity[3])
{
	int axis;

	if (!checkpoint || !origin || !velocity ||
	    (checkpoint->grounded != false && checkpoint->grounded != true) ||
	    checkpoint->waterlevel < 0 || checkpoint->waterlevel > 3 ||
	    !isfinite(checkpoint->old_frame_z))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(origin[axis]) || !isfinite(velocity[axis]) ||
		    origin[axis] != checkpoint->pms.origin[axis] * 0.125f ||
		    velocity[axis] != checkpoint->pms.velocity[axis] * 0.125f)
			return 0;
	return 1;
}

static int CompoundPublicationProofValid(const rune_link_t *link,
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

static int CompoundPublicationDropProofValid(const rune_link_t *link,
	const sg_compound_drop_proof_t *drop)
{
	sg_compound_swim_proof_t timing;

	if (!drop)
		return 0;
	memset(&timing, 0, sizeof(timing));
	timing.touch_ms = drop->touch_ms;
	timing.touch_frame_end_ms = drop->touch_frame_end_ms;
	timing.mover_top_ms = drop->mover_top_ms;
	timing.suffix_start_ms = drop->suffix_start_ms;
	timing.arrival_ms = drop->arrival_ms;
	timing.sweep_clear_ms = drop->sweep_clear_ms;
	timing.total_cost_ms = drop->total_cost_ms;
	timing.exit_speed = drop->exit_speed;
	return CompoundPublicationProofValid(link, &timing) &&
	       drop->heading == link->heading;
}

static int CompoundPublicationBindingMatchesRune(
	const rune_t *rune,
	const sg_compound_publication_binding_t *binding)
{
	const rune_link_t *link;

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
	       CompoundPublicationBindingTimingValid(binding);
}

static const sg_compound_world_candidate_t *
CompoundPublicationFindCandidate(
	const sg_compound_world_candidate_t *candidates, int candidate_count,
	const sg_compound_world_preopen_t *resolved)
{
	const sg_compound_world_candidate_t *found = NULL;
	int index;

	if (!candidates || candidate_count <= 0 || !resolved)
		return NULL;
	for (index = 0; index < candidate_count; index++)
		if (CompoundPublicationMechanismEqual(&candidates[index].resolved,
		                                      resolved))
		{
			if (found)
				return NULL;
			found = &candidates[index];
		}
	return found;
}

static int CompoundPublicationMechanismIndex(
	sg_compound_publication_t *publication,
	const sg_compound_world_preopen_t *resolved, uint32_t *index_out)
{
	size_t index;

	if (!publication || !resolved || !index_out)
		return 0;
	for (index = 0; index < publication->mechanism_count; index++)
		if (publication->mechanisms[index].trigger_key ==
		        resolved->trigger_key &&
		    publication->mechanisms[index].mover_key == resolved->mover_key)
		{
			if (!CompoundPublicationMechanismEqual(
			    &publication->mechanisms[index], resolved))
				return 0;
			*index_out = (uint32_t)index;
			return 1;
		}
	publication->mechanisms[publication->mechanism_count] = *resolved;
	*index_out = (uint32_t)publication->mechanism_count++;
	return 1;
}

void SG_CompoundPublicationDestroy(sg_compound_publication_t *publication)
{
	sg_compound_publication_free_fn deallocate;

	if (!publication)
		return;
	deallocate = publication->deallocate;
	if (publication->mechanisms)
		deallocate(publication->mechanisms);
	if (publication->bindings)
		deallocate(publication->bindings);
	deallocate(publication);
}

sg_compound_publication_result_t SG_CompoundPublicationBuild(
	const rune_t *rune, sg_compound_publication_alloc_fn allocate,
	sg_compound_publication_free_fn deallocate,
	sg_compound_publication_t **publication_out)
{
	sg_compound_publication_t *publication = NULL;
	sg_compound_world_candidate_t *candidates = NULL;
	size_t compound_count = 0;
	size_t binding_index = 0;
	int binding_bytes, mechanism_bytes, candidate_bytes;
	int candidate_count = 0;
	int candidate_capacity = 0;
	int link_index;
	rune_reject_reason_t enumeration_reason;
	sg_compound_publication_result_t result = CompoundPublicationResult(
		SG_COMPOUND_PUBLICATION_INVALID, RLR_BAD_CONTROL_POLICY,
		SG_COMPOUND_PUBLICATION_INDEX_NONE);

	if (!publication_out)
		return result;
	if (rune && rune->compound_publication)
		return result;
	*publication_out = NULL;
	if (!CompoundPublicationRuneShapeValid(rune) || !allocate || !deallocate)
		return result;
	for (link_index = 0; link_index < rune->hdr.num_links; link_index++)
		if (rune->links[link_index].action == RL_DOOR_SWIM ||
		    rune->links[link_index].action == RL_DOOR_DROP)
		{
			compound_count++;
			if (!CompoundPublicationNativeLinkValid(
			        rune, &rune->links[link_index]))
				return CompoundPublicationResult(
					SG_COMPOUND_PUBLICATION_INVALID,
					RLR_BAD_CONTROL_POLICY, (uint32_t)link_index);
		}
	if (compound_count == 0)
		return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_OK,
		                                  RLR_OK,
		                                  SG_COMPOUND_PUBLICATION_INDEX_NONE);
	enumeration_reason = SG_CompoundWorldEnumeratePreopen(NULL, 0,
	                                                    &candidate_count);
	if (enumeration_reason != RLR_OK || candidate_count <= 0 ||
	    !CompoundPublicationSize((size_t)candidate_count,
	        sizeof(sg_compound_world_candidate_t), &candidate_bytes))
		return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_MECHANISM,
		                                  enumeration_reason == RLR_OK
		                                      ? RLR_MECHANISM_UNRESOLVED
		                                      : enumeration_reason,
		                                  0);
	candidates = allocate(candidate_bytes);
	if (!candidates)
		return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_ALLOCATION,
		                                  RLR_OK,
		                                  SG_COMPOUND_PUBLICATION_INDEX_NONE);
	memset(candidates, 0, (size_t)candidate_bytes);
	candidate_capacity = candidate_count;
	enumeration_reason = SG_CompoundWorldEnumeratePreopen(
		candidates, candidate_capacity, &candidate_count);
	if (enumeration_reason != RLR_OK || candidate_count <= 0 ||
	    candidate_count != candidate_capacity)
	{
		result = CompoundPublicationResult(SG_COMPOUND_PUBLICATION_MECHANISM,
		                                   enumeration_reason == RLR_OK
		                                       ? RLR_MECHANISM_UNRESOLVED
		                                       : enumeration_reason,
		                                   0);
		goto fail;
	}
	if (!CompoundPublicationSize(compound_count,
	        sizeof(sg_compound_publication_binding_t), &binding_bytes) ||
	    !CompoundPublicationSize(compound_count,
	        sizeof(sg_compound_world_preopen_t), &mechanism_bytes))
		goto fail;
	publication = allocate((int)sizeof(*publication));
	if (!publication)
	{
		result = CompoundPublicationResult(SG_COMPOUND_PUBLICATION_ALLOCATION,
		                                   RLR_OK,
		                                   SG_COMPOUND_PUBLICATION_INDEX_NONE);
		goto fail;
	}
	memset(publication, 0, sizeof(*publication));
	publication->deallocate = deallocate;
	publication->bindings = allocate(binding_bytes);
	if (!publication->bindings)
	{
		result = CompoundPublicationResult(SG_COMPOUND_PUBLICATION_ALLOCATION,
		                                   RLR_OK,
		                                   SG_COMPOUND_PUBLICATION_INDEX_NONE);
		goto fail;
	}
	publication->mechanisms = allocate(mechanism_bytes);
	if (!publication->mechanisms)
	{
		result = CompoundPublicationResult(SG_COMPOUND_PUBLICATION_ALLOCATION,
		                                   RLR_OK,
		                                   SG_COMPOUND_PUBLICATION_INDEX_NONE);
		goto fail;
	}
	memset(publication->bindings, 0, (size_t)binding_bytes);
	memset(publication->mechanisms, 0, (size_t)mechanism_bytes);
	publication->binding_count = compound_count;

	for (link_index = 0; link_index < rune->hdr.num_links; link_index++)
	{
		const rune_link_t *link = &rune->links[link_index];
		sg_compound_publication_binding_t *binding;
		sg_compound_world_preopen_t resolved;
		const sg_compound_world_candidate_t *candidate;
		sg_compound_swim_source_t source;
		sg_compound_swim_proof_t proof;
		sg_compound_drop_proof_t drop_proof;
		sg_phantom_t phantom;
		rune_reject_reason_t reason;
		int hint_index;
		int contact_found = 0;
		vec3_t canonical_hint;

		if (link->action != RL_DOOR_SWIM && link->action != RL_DOOR_DROP)
			continue;
		result.link_index = (uint32_t)link_index;
		if (binding_index >= compound_count ||
		    !CompoundPublicationNativeLinkValid(rune, link))
		{
			result.status = SG_COMPOUND_PUBLICATION_INVALID;
			goto fail;
		}
		memset(&resolved, 0, sizeof(resolved));
		reason = SG_CompoundWorldResolvePreopen(link->mechanism_anchor,
		                                          &resolved);
		if (reason != RLR_OK)
		{
			result.status = SG_COMPOUND_PUBLICATION_MECHANISM;
			result.reason = reason;
			goto fail;
		}
		candidate = CompoundPublicationFindCandidate(candidates,
		                                             candidate_count,
		                                             &resolved);
		if (!candidate || candidate->hint_count <= 0 ||
		    candidate->hint_count > SG_COMPOUND_WORLD_PREOPEN_HINT_MAX)
		{
			result.status = SG_COMPOUND_PUBLICATION_MECHANISM;
			result.reason = RLR_MECHANISM_UNRESOLVED;
			goto fail;
		}
		memset(&source, 0, sizeof(source));
		memset(&drop_proof, 0, sizeof(drop_proof));
		memset(canonical_hint, 0, sizeof(canonical_hint));
		if (link->action == RL_DOOR_SWIM)
		{
			reason = SG_OracleCompoundSwimPrepareSource(
				rune->seeds[link->from].origin, &candidate->resolved, 0.0f,
				&source, NULL, true, true);
			if (reason != RLR_OK)
			{
				result.status = SG_COMPOUND_PUBLICATION_SOURCE;
				result.reason = reason;
				goto fail;
			}
		}
		for (hint_index = 0; hint_index < candidate->hint_count;
		     hint_index++)
		{
			vec3_t discovered;

			memset(discovered, 0, sizeof(discovered));
			if (link->action == RL_DOOR_SWIM)
				reason = SG_OracleCompoundSwimDiscoverContact(
					&source, &candidate->resolved,
					candidate->hints[hint_index], discovered, NULL, true, true);
			else
				reason = SG_OracleCompoundDropDiscoverContact(
					rune->seeds[link->from].origin, &candidate->resolved,
					candidate->hints[hint_index], discovered, true);
			if (reason == RLR_OK &&
			    CompoundPublicationVectorEqual(discovered,
			                                   link->mechanism_anchor))
			{
				contact_found = 1;
				memcpy(canonical_hint, candidate->hints[hint_index],
				       sizeof(canonical_hint));
				break;
			}
		}
		if (!contact_found)
		{
			result.status = SG_COMPOUND_PUBLICATION_MECHANISM;
			result.reason = RLR_BAD_MECHANISM_ANCHOR;
			goto fail;
		}
		memset(&proof, 0, sizeof(proof));
		if (link->action == RL_DOOR_SWIM)
		{
			phantom = source.phantom;
			reason = SG_OracleCompoundSwimPreopen(
				&phantom, &resolved, link->mechanism_anchor,
				rune->seeds[link->to].origin,
				(rune->seeds[link->to].flags & RSF_WATER) != 0,
				source.old_frame_z, &proof, NULL, NULL, true, true);
		}
		else
			reason = SG_OracleCompoundDropPreopen(
				rune->seeds[link->from].origin, &resolved,
				link->mechanism_anchor, rune->seeds[link->to].origin,
				link->anchor, link->heading,
				(rune->seeds[link->to].flags & RSF_WATER) != 0,
				&drop_proof, true);
		if (reason != RLR_OK)
		{
			result.status = SG_COMPOUND_PUBLICATION_REPLAY;
			result.reason = reason;
			goto fail;
		}
		if ((link->action == RL_DOOR_SWIM &&
		     !CompoundPublicationProofValid(link, &proof)) ||
		    (link->action == RL_DOOR_DROP &&
		     !CompoundPublicationDropProofValid(link, &drop_proof)))
		{
			result.status = SG_COMPOUND_PUBLICATION_MISMATCH;
			result.reason = RLR_COST_MISMATCH;
			goto fail;
		}
		binding = &publication->bindings[binding_index++];
		binding->link_index = (uint32_t)link_index;
		binding->link = *link;
		binding->source_seed = rune->seeds[link->from];
		binding->destination_seed = rune->seeds[link->to];
		memcpy(binding->canonical_hint, canonical_hint,
		       sizeof(binding->canonical_hint));
		if (link->action == RL_DOOR_SWIM)
		{
			CompoundPublicationCheckpointSource(&binding->source, &source);
			CompoundPublicationCheckpointSuffix(&binding->suffix, &proof);
		}
		else
		{
			CompoundPublicationCheckpointDropSource(&binding->source,
			                                        &drop_proof);
			CompoundPublicationCheckpointDropSuffix(&binding->suffix,
			                                        &drop_proof);
		}
		if (!CompoundPublicationCheckpointInternallyValid(&binding->source,
		        link->action == RL_DOOR_SWIM ? source.phantom.origin :
		                                       drop_proof.source_origin,
		        link->action == RL_DOOR_SWIM ? source.phantom.velocity :
		                                       drop_proof.source_velocity) ||
		    !CompoundPublicationCheckpointInternallyValid(&binding->suffix,
		        link->action == RL_DOOR_SWIM ? proof.suffix_origin :
		                                       drop_proof.suffix_origin,
		        link->action == RL_DOOR_SWIM ? proof.suffix_velocity :
		                                       drop_proof.suffix_velocity) ||
		    !CompoundPublicationFloatBitsEqual(binding->source.old_frame_z,
		                                       0.0f) ||
		    (link->action == RL_DOOR_SWIM &&
		     (source.phantom.waterlevel < 2 ||
		      !(source.phantom.watertype & CONTENTS_WATER) ||
		      (source.phantom.watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))) ||
		    (link->action == RL_DOOR_DROP &&
		     (drop_proof.source_waterlevel != 0 ||
		      !drop_proof.source_groundentity)) ||
		    !CompoundPublicationMechanismIndex(publication, &resolved,
		                                       &binding->mechanism_index))
		{
			result.status = SG_COMPOUND_PUBLICATION_MISMATCH;
			result.reason = RLR_MECHANISM_UNRESOLVED;
			goto fail;
		}
		binding->touch_ms = link->action == RL_DOOR_SWIM ? proof.touch_ms :
		                                                    drop_proof.touch_ms;
		binding->touch_frame_end_ms = link->action == RL_DOOR_SWIM ?
		    proof.touch_frame_end_ms : drop_proof.touch_frame_end_ms;
		binding->mover_top_ms = link->action == RL_DOOR_SWIM ?
		    proof.mover_top_ms : drop_proof.mover_top_ms;
		binding->suffix_start_ms = link->action == RL_DOOR_SWIM ?
		    proof.suffix_start_ms : drop_proof.suffix_start_ms;
		binding->arrival_ms = link->action == RL_DOOR_SWIM ? proof.arrival_ms :
		                                                      drop_proof.arrival_ms;
		binding->sweep_clear_ms = link->action == RL_DOOR_SWIM ?
		    proof.sweep_clear_ms : drop_proof.sweep_clear_ms;
		binding->total_cost_ms = link->action == RL_DOOR_SWIM ?
		    proof.total_cost_ms : drop_proof.total_cost_ms;
	}
	if (binding_index != compound_count)
	{
		result.status = SG_COMPOUND_PUBLICATION_INVALID;
		result.reason = RLR_BAD_CONTROL_POLICY;
		result.link_index = SG_COMPOUND_PUBLICATION_INDEX_NONE;
		goto fail;
	}
	deallocate(candidates);
	candidates = NULL;
	*publication_out = publication;
	return CompoundPublicationResult(SG_COMPOUND_PUBLICATION_OK, RLR_OK,
	                                 SG_COMPOUND_PUBLICATION_INDEX_NONE);

fail:
	SG_CompoundPublicationDestroy(publication);
	if (candidates)
		deallocate(candidates);
	return result;
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
	        rune, &publication->bindings[low]))
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
			    rune->links[index].action == RL_DOOR_DROP)
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
		    rune->links[index].action == RL_DOOR_DROP)
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
