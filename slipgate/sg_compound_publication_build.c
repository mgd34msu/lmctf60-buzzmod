#include "../g_local.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "sg_compound_action_publication.h"
#include "sg_compound_publication.h"
#include "sg_compound_publication_internal.h"
#include "sg_util.h"

static int CompoundPublicationSize(size_t count, size_t item_size,
	int *size_out)
{
	if (!size_out || count == 0 || item_size == 0 ||
	    count > (size_t)INT_MAX / item_size)
		return 0;
	*size_out = (int)(count * item_size);
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

static void CompoundPublicationCheckpointHookSuffix(
	sg_compound_publication_checkpoint_t *checkpoint,
	const sg_compound_hook_proof_t *proof)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	checkpoint->pms = proof->suffix_pms;
	checkpoint->old_pms = proof->suffix_old_pms;
	checkpoint->grounded = proof->suffix_groundentity;
	checkpoint->watertype = proof->suffix_watertype;
	checkpoint->waterlevel = proof->suffix_waterlevel;
	checkpoint->old_frame_z = proof->suffix_old_frame_z;
}

static void CompoundPublicationCheckpointHookSource(
	sg_compound_publication_checkpoint_t *checkpoint,
	const sg_compound_hook_proof_t *proof)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	checkpoint->pms = proof->source_pms;
	checkpoint->old_pms = proof->source_old_pms;
	checkpoint->grounded = proof->source_groundentity;
	checkpoint->watertype = proof->source_watertype;
	checkpoint->waterlevel = proof->source_waterlevel;
	checkpoint->old_frame_z = proof->source_old_frame_z;
}

static int CompoundPublicationHookSourceMatches(
	const sg_compound_hook_proof_t *proof,
	const sg_compound_swim_source_t *prepared)
{
	return proof && prepared &&
	       memcmp(&proof->source_pms, &prepared->phantom.pms,
	              sizeof(proof->source_pms)) == 0 &&
	       memcmp(&proof->source_old_pms, &prepared->phantom.old_pms,
	              sizeof(proof->source_old_pms)) == 0 &&
	       proof->source_groundentity == prepared->phantom.groundentity &&
	       proof->source_watertype == prepared->phantom.watertype &&
	       proof->source_waterlevel == prepared->phantom.waterlevel &&
	       CompoundPublicationFloatBitsEqual(proof->source_old_frame_z,
	                                         prepared->old_frame_z) &&
	       CompoundPublicationVectorEqual(proof->source_origin,
	                                      prepared->phantom.origin) &&
	       CompoundPublicationVectorEqual(proof->source_velocity,
	                                      prepared->phantom.velocity);
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

static int CompoundPublicationHookProofValid(const rune_link_t *link,
	const sg_compound_hook_proof_t *hook)
{
	sg_compound_swim_proof_t timing;
	vec3_t decoded_view, muzzle, decoded_bite;

	if (!hook ||
	    !SG_HookControlDecode(hook->suffix_origin, 22.0f, RIGHT_HANDED,
	                          link->anchor, decoded_view, muzzle,
	                          decoded_bite))
		return 0;
	memset(&timing, 0, sizeof(timing));
	timing.touch_ms = hook->touch_ms;
	timing.touch_frame_end_ms = hook->touch_frame_end_ms;
	timing.mover_top_ms = hook->mover_top_ms;
	timing.suffix_start_ms = hook->suffix_start_ms;
	timing.arrival_ms = hook->arrival_ms;
	timing.sweep_clear_ms = hook->sweep_clear_ms;
	timing.total_cost_ms = hook->total_cost_ms;
	timing.exit_speed = hook->exit_speed;
	return CompoundPublicationProofValid(link, &timing) &&
	       CompoundPublicationVectorEqual(hook->control, link->anchor) &&
	       CompoundPublicationVectorEqual(hook->hook_spec.view_angles,
	                                      decoded_view) &&
	       CompoundPublicationVectorEqual(hook->hook_spec.bite,
	                                      decoded_bite);
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

sg_compound_publication_result_t SG_CompoundPublicationBuild(
	const rune_t *rune, sg_compound_publication_alloc_fn allocate,
	sg_compound_publication_free_fn deallocate,
	sg_compound_publication_t **publication_out)
{
	sg_compound_publication_t *publication = NULL;
	sg_compound_world_candidate_t *candidates = NULL;
	size_t compound_count = 0;
	size_t hook_count = 0;
	size_t binding_index = 0;
	int binding_bytes, mechanism_bytes, hook_proof_bytes, candidate_bytes;
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
		    rune->links[link_index].action == RL_DOOR_DROP ||
		    rune->links[link_index].action == RL_DOOR_HOOK)
		{
			compound_count++;
			if (rune->links[link_index].action == RL_DOOR_HOOK)
				hook_count++;
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
	if (hook_count)
	{
		if (!CompoundPublicationSize(compound_count,
		        sizeof(*publication->hook_proofs), &hook_proof_bytes))
			goto fail;
		publication->hook_proofs = allocate(hook_proof_bytes);
		if (!publication->hook_proofs)
		{
			result = CompoundPublicationResult(
				SG_COMPOUND_PUBLICATION_ALLOCATION, RLR_OK,
				SG_COMPOUND_PUBLICATION_INDEX_NONE);
			goto fail;
		}
		memset(publication->hook_proofs, 0, (size_t)hook_proof_bytes);
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
		sg_compound_hook_proof_t hook_proof;
		sg_phantom_t phantom;
		const float *source_origin;
		const float *source_velocity;
		const float *suffix_origin;
		const float *suffix_velocity;
		float source_old_frame_z;
		int touch_ms;
		int touch_frame_end_ms;
		int mover_top_ms;
		int suffix_start_ms;
		int arrival_ms;
		int sweep_clear_ms;
		int total_cost_ms;
		rune_reject_reason_t reason;
		int hint_index;
		int contact_found = 0;
		vec3_t canonical_hint;

		if (link->action != RL_DOOR_SWIM && link->action != RL_DOOR_DROP &&
		    link->action != RL_DOOR_HOOK)
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
		memset(&hook_proof, 0, sizeof(hook_proof));
		memset(canonical_hint, 0, sizeof(canonical_hint));
		if (link->action == RL_DOOR_SWIM || link->action == RL_DOOR_HOOK)
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
			if (link->action == RL_DOOR_SWIM ||
			    link->action == RL_DOOR_HOOK)
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
				source.old_frame_z, &proof, NULL, true, true);
		}
		else if (link->action == RL_DOOR_DROP)
			reason = SG_OracleCompoundDropPreopen(
				rune->seeds[link->from].origin, &resolved,
				link->mechanism_anchor, rune->seeds[link->to].origin,
				link->anchor, link->heading,
				(rune->seeds[link->to].flags & RSF_WATER) != 0,
				&drop_proof, true);
		else
		{
			phantom = source.phantom;
		reason = SG_OracleCompoundHookPreopen(
				&phantom, &resolved, link->mechanism_anchor,
				rune->seeds[link->to].origin, link->anchor,
				source.old_frame_z,
				&hook_proof, NULL, true, true);
		}
		if (reason != RLR_OK)
		{
			result.status = SG_COMPOUND_PUBLICATION_REPLAY;
			result.reason = reason;
			goto fail;
		}
		if ((link->action == RL_DOOR_SWIM &&
		     !CompoundPublicationProofValid(link, &proof)) ||
		    (link->action == RL_DOOR_DROP &&
		     !CompoundPublicationDropProofValid(link, &drop_proof)) ||
		    (link->action == RL_DOOR_HOOK &&
		     (!CompoundPublicationHookProofValid(link, &hook_proof) ||
		      !CompoundPublicationHookSourceMatches(&hook_proof, &source))))
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
		else if (link->action == RL_DOOR_DROP)
		{
			CompoundPublicationCheckpointDropSource(&binding->source,
			                                        &drop_proof);
			CompoundPublicationCheckpointDropSuffix(&binding->suffix,
			                                        &drop_proof);
		}
		else
		{
			CompoundPublicationCheckpointHookSource(&binding->source,
			                                        &hook_proof);
			CompoundPublicationCheckpointHookSuffix(&binding->suffix,
			                                        &hook_proof);
			binding->hook_proof.spec = hook_proof.hook_spec;
		}
		if (link->action == RL_DOOR_SWIM)
		{
			source_origin = source.phantom.origin;
			source_velocity = source.phantom.velocity;
			suffix_origin = proof.suffix_origin;
			suffix_velocity = proof.suffix_velocity;
			source_old_frame_z = 0.0f;
			touch_ms = proof.touch_ms;
			touch_frame_end_ms = proof.touch_frame_end_ms;
			mover_top_ms = proof.mover_top_ms;
			suffix_start_ms = proof.suffix_start_ms;
			arrival_ms = proof.arrival_ms;
			sweep_clear_ms = proof.sweep_clear_ms;
			total_cost_ms = proof.total_cost_ms;
		}
		else if (link->action == RL_DOOR_DROP)
		{
			source_origin = drop_proof.source_origin;
			source_velocity = drop_proof.source_velocity;
			suffix_origin = drop_proof.suffix_origin;
			suffix_velocity = drop_proof.suffix_velocity;
			source_old_frame_z = 0.0f;
			touch_ms = drop_proof.touch_ms;
			touch_frame_end_ms = drop_proof.touch_frame_end_ms;
			mover_top_ms = drop_proof.mover_top_ms;
			suffix_start_ms = drop_proof.suffix_start_ms;
			arrival_ms = drop_proof.arrival_ms;
			sweep_clear_ms = drop_proof.sweep_clear_ms;
			total_cost_ms = drop_proof.total_cost_ms;
		}
		else
		{
			source_origin = hook_proof.source_origin;
			source_velocity = hook_proof.source_velocity;
			suffix_origin = hook_proof.suffix_origin;
			suffix_velocity = hook_proof.suffix_velocity;
			source_old_frame_z = hook_proof.source_old_frame_z;
			touch_ms = hook_proof.touch_ms;
			touch_frame_end_ms = hook_proof.touch_frame_end_ms;
			mover_top_ms = hook_proof.mover_top_ms;
			suffix_start_ms = hook_proof.suffix_start_ms;
			arrival_ms = hook_proof.arrival_ms;
			sweep_clear_ms = hook_proof.sweep_clear_ms;
			total_cost_ms = hook_proof.total_cost_ms;
		}
		if (!CompoundPublicationCheckpointInternallyValid(&binding->source,
		                                                source_origin,
		                                                source_velocity) ||
		    !CompoundPublicationCheckpointInternallyValid(&binding->suffix,
		                                                suffix_origin,
		                                                suffix_velocity) ||
		    !CompoundPublicationFloatBitsEqual(binding->source.old_frame_z,
		                                       source_old_frame_z) ||
		    ((link->action == RL_DOOR_SWIM ||
		      link->action == RL_DOOR_HOOK) &&
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
		binding->touch_ms = touch_ms;
		binding->touch_frame_end_ms = touch_frame_end_ms;
		binding->mover_top_ms = mover_top_ms;
		binding->suffix_start_ms = suffix_start_ms;
		binding->arrival_ms = arrival_ms;
		binding->sweep_clear_ms = sweep_clear_ms;
		binding->total_cost_ms = total_cost_ms;
		if (link->action == RL_DOOR_HOOK)
		{
			sg_hook_replay_spec_t hook_spec;

			if (!SG_CompoundHookPublicationPlan(binding, &hook_spec))
			{
				result.status = SG_COMPOUND_PUBLICATION_MISMATCH;
				result.reason = RLR_BAD_CONTROL_POLICY;
				goto fail;
			}
			publication->hook_proofs[binding_index - 1] =
				binding->hook_proof;
		}
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
