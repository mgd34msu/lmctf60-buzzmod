#include "sg_ground_capability_publication.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"

#define SG_GROUND_PUBLICATION_STATE UINT32_C(0x47504331)

extern void Pmove(pmove_t *pmove);

struct sg_ground_capability_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	const sg_ground_capability_publication_t *self;
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_publication_fact_t facts[];
};

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (left->mins.value[axis] != right->mins.value[axis] ||
			left->maxs.value[axis] != right->maxs.value[axis])
			return 0;
	return 1;
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return left->gravity == right->gravity &&
		left->ground_acceleration == right->ground_acceleration &&
		left->air_acceleration == right->air_acceleration &&
		left->water_acceleration == right->water_acceleration &&
		left->hook_acceleration == right->hook_acceleration &&
		left->external_acceleration == right->external_acceleration &&
		left->water_drag == right->water_drag &&
		left->max_velocity == right->max_velocity &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

static int IntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return left->min_value == right->min_value &&
		left->max_value == right->max_value;
}

static int Vec3Equal(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	return left->value[0] == right->value[0] &&
		left->value[1] == right->value[1] &&
		left->value[2] == right->value[2];
}

static int Interval3Equal(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return IntervalEqual(&left->x, &right->x) &&
		IntervalEqual(&left->y, &right->y) &&
		IntervalEqual(&left->z, &right->z);
}

static int FactEqual(const sg_ground_capability_t *left,
	const sg_ground_capability_t *right)
{
	return left->source_cell == right->source_cell &&
		left->destination_cell == right->destination_cell &&
		left->source_region == right->source_region &&
		left->destination_region == right->destination_region &&
		left->portal == right->portal &&
		left->source_phase == right->source_phase &&
		left->destination_phase == right->destination_phase &&
		left->kind == right->kind &&
		Vec3Equal(&left->source_witness, &right->source_witness) &&
		Vec3Equal(&left->destination_witness, &right->destination_witness) &&
		Vec3Equal(&left->initial_velocity, &right->initial_velocity) &&
		Vec3Equal(&left->observed_velocity, &right->observed_velocity) &&
		Interval3Equal(&left->displacement, &right->displacement) &&
		IntervalEqual(&left->duration_ms, &right->duration_ms) &&
		left->acceleration == right->acceleration &&
		left->gravity == right->gravity &&
		left->physics_abi_id == right->physics_abi_id &&
		left->flags == right->flags;
}

static void SetResult(sg_ground_capability_audit_result_t *result,
	sg_ground_capability_audit_code_t code, uint32_t record)
{
	result->code = code;
	result->record = record;
}

static int SourceValid(const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_audit_result_t *result)
{
	if (!source || !candidate || !source->authority ||
		!source->authority->world || !source->configuration ||
		!source->semantics || !source->phases || source->phase_count == 0U ||
		!source->bindings || source->binding_count == 0U ||
		!source->host_pmove || source->phase_count > UINT32_MAX ||
		source->binding_count > UINT32_MAX ||
		(candidate->capability_count != 0U && !candidate->capabilities))
		return 0;
	if (!IdentityEqual(&source->authority->identity,
			&source->configuration->identity) ||
		!IdentityEqual(&source->authority->identity,
			&source->semantics->identity) ||
		!IdentityEqual(&source->authority->identity, &candidate->identity))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (source->host_pmove != Pmove || source->host_law_identity == 0U ||
		source->host_law_identity !=
			source->authority->identity.physics_abi_id)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_HOST_LAW_MISMATCH,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	return 1;
}

static int AuditAcceptedSources(
	const sg_ground_capability_publication_source_t *source,
	sg_ground_capability_audit_result_t *result)
{
	sg_bsp_completeness_result_t completeness;
	sg_configuration_audit_result_t configuration;
	sg_configuration_semantics_audit_result_t semantics;

	if (source->configuration->certificate_node_count != 0U &&
		(!SG_BspCompletenessProve(source->authority, source->configuration,
			&completeness) || completeness.code != SG_BSP_COMPLETENESS_OK ||
		 !SG_ConfigurationAudit(source->authority, source->configuration,
			&configuration) || configuration.code != SG_CONFIGURATION_AUDIT_OK))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (source->semantics->boundary_count != 0U &&
		(!SG_ConfigurationSemanticsAudit(source->authority,
			source->configuration, source->semantics, &semantics) ||
		 semantics.code != SG_CONFIGURATION_SEMANTICS_AUDIT_OK))
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SEMANTICS_REJECTED,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	return 1;
}

static sg_ground_capability_audit_code_t ReconstructionCode(
	sg_ground_capability_error_code_t code)
{
	if (code == SG_GROUND_CAPABILITY_ERROR_OVERFLOW)
		return SG_GROUND_CAPABILITY_AUDIT_OVERFLOW;
	if (code == SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY)
		return SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY;
	return SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED;
}

static int CompletenessEqual(const sg_ground_capability_set_t *expected,
	const sg_ground_capability_set_t *candidate)
{
	return expected->proved_portals == candidate->proved_portals &&
		expected->rejected_crossings == candidate->rejected_crossings &&
		expected->proved_directions == candidate->proved_directions &&
		expected->rejected_directions == candidate->rejected_directions;
}

int SG_GroundCapabilityAudit(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_audit_result_t *result_out)
{
	sg_ground_capability_audit_result_t result;
	sg_ground_capability_set_t *expected = NULL;
	sg_ground_capability_error_t error;
	uint32_t index;

	memset(&result, 0, sizeof(result));
	result.code = SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT;
	result.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
	result.record = SG_GROUND_CAPABILITY_INDEX_NONE;
	if (!result_out)
		return 0;
	*result_out = result;
	if (!SourceValid(source, candidate, &result) ||
		!AuditAcceptedSources(source, &result))
		goto fail;
	result.candidate_facts = candidate->capability_count;
	if (!SG_GroundCapabilityBuild(source->authority, source->configuration,
		source->semantics, source->phases, source->phase_count,
		source->bindings, source->binding_count, source->host_pmove,
		&expected, &error))
	{
		SetResult(&result, ReconstructionCode(error.code), error.source_index);
		goto fail;
	}
	result.expected_facts = expected->capability_count;
	result.proved_portals = expected->proved_portals;
	result.proven_empty_portals = expected->rejected_crossings;
	result.proved_directions = expected->proved_directions;
	result.proven_empty_directions = expected->rejected_directions;
	result.host_pmove_frames = expected->pmove_frames;
	for (index = 0U; index < expected->capability_count; index++)
		result.expected_by_kind[expected->capabilities[index].kind]++;
	if (candidate->capability_count < expected->capability_count)
	{
		SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_OMITTED_FACT,
			candidate->capability_count);
		goto fail;
	}
	if (candidate->capability_count > expected->capability_count)
	{
		SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_INVENTED_FACT,
			expected->capability_count);
		goto fail;
	}
	for (index = 0U; index < candidate->capability_count; index++)
	{
		if (candidate->capabilities[index].kind >= 0 &&
			candidate->capabilities[index].kind < SG_GROUND_CAPABILITY_KIND_COUNT)
			result.candidate_by_kind[candidate->capabilities[index].kind]++;
	}
	for (index = 0U; index < expected->capability_count; index++)
	{
		if (!FactEqual(&expected->capabilities[index],
				&candidate->capabilities[index]))
		{
			SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_FACT_DISAGREEMENT,
				index);
			goto fail;
		}
		result.matched_facts++;
	}
	if (!CompletenessEqual(expected, candidate))
	{
		SetResult(&result,
			SG_GROUND_CAPABILITY_AUDIT_COMPLETENESS_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		goto fail;
	}
	result.completeness = expected->capability_count == 0U ?
		SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY :
		SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE;
	result.code = SG_GROUND_CAPABILITY_AUDIT_OK;
	SG_GroundCapabilityDestroy(expected);
	*result_out = result;
	return 1;

fail:
	SG_GroundCapabilityDestroy(expected);
	*result_out = result;
	return 0;
}

static void NormalizeFact(const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_t *input,
	sg_ground_capability_publication_fact_t *output)
{
	memset(output, 0, sizeof(*output));
	output->source_cell = source->configuration->cells[input->source_cell].id;
	output->destination_cell =
		source->configuration->cells[input->destination_cell].id;
	output->source_region_id = source->semantics->regions[input->source_region].id;
	output->destination_region_id =
		source->semantics->regions[input->destination_region].id;
	output->portal = input->portal == SG_GROUND_CAPABILITY_INDEX_NONE ?
		SG_RUNE_PORTAL_REF_NONE : source->configuration->portals[input->portal].id;
	output->source_phase = source->phases[input->source_phase].id;
	output->destination_phase = source->phases[input->destination_phase].id;
	output->kind = input->kind;
	output->source_witness = input->source_witness;
	output->destination_witness = input->destination_witness;
	output->initial_velocity = input->initial_velocity;
	output->observed_velocity = input->observed_velocity;
	output->displacement = input->displacement;
	output->duration_ms = input->duration_ms;
	output->acceleration = input->acceleration;
	output->gravity = input->gravity;
	output->physics_abi_id = input->physics_abi_id;
	output->flags = input->flags;
}

static int PublicationValid(
	const sg_ground_capability_publication_t *publication)
{
	return publication && publication->state == SG_GROUND_PUBLICATION_STATE &&
		publication->state_inverse == ~SG_GROUND_PUBLICATION_STATE &&
		publication->self == publication &&
		(publication->description.completeness ==
			SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE ||
		 publication->description.completeness ==
			SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY) &&
		((publication->description.fact_count == 0U) ==
		 (publication->description.completeness ==
			SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY));
}

int SG_GroundCapabilityPublicationIssue(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_publication_t **publication_out,
	sg_ground_capability_audit_result_t *audit_out)
{
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_publication_t *publication;
	size_t bytes;
	uint32_t index;

	memset(&audit, 0, sizeof(audit));
	audit.code = SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT;
	audit.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
	audit.record = SG_GROUND_CAPABILITY_INDEX_NONE;
	if (!publication_out || *publication_out || !audit_out)
	{
		if (audit_out)
			*audit_out = audit;
		return 0;
	}
	if (!SG_GroundCapabilityAudit(source, candidate, &audit))
	{
		*audit_out = audit;
		return 0;
	}
#if SIZE_MAX < UINT32_MAX
	if (candidate->capability_count >
		(SIZE_MAX - sizeof(*publication)) / sizeof(*publication->facts))
	{
		audit.code = SG_GROUND_CAPABILITY_AUDIT_OVERFLOW;
		*audit_out = audit;
		return 0;
	}
#endif
	bytes = sizeof(*publication) + (size_t)candidate->capability_count *
		sizeof(*publication->facts);
	publication = calloc(1, bytes);
	if (!publication)
	{
		audit.code = SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY;
		*audit_out = audit;
		return 0;
	}
	publication->description.identity = source->authority->identity;
	publication->description.host_law_identity = source->host_law_identity;
	publication->description.completeness = audit.completeness;
	publication->description.cell_count = source->configuration->cell_count;
	publication->description.portal_count = source->configuration->portal_count;
	publication->description.semantic_region_count =
		source->semantics->region_count;
	publication->description.phase_count = (uint32_t)source->phase_count;
	publication->description.binding_count = (uint32_t)source->binding_count;
	publication->description.fact_count = candidate->capability_count;
	publication->description.proved_portals = audit.proved_portals;
	publication->description.proven_empty_portals = audit.proven_empty_portals;
	publication->description.proved_directions = audit.proved_directions;
	publication->description.proven_empty_directions =
		audit.proven_empty_directions;
	for (index = 0U; index < candidate->capability_count; index++)
	{
		NormalizeFact(source, &candidate->capabilities[index],
			&publication->facts[index]);
		publication->description.fact_count_by_kind[
			candidate->capabilities[index].kind]++;
	}
	publication->self = publication;
	publication->state_inverse = ~SG_GROUND_PUBLICATION_STATE;
	publication->state = SG_GROUND_PUBLICATION_STATE;
	*publication_out = publication;
	*audit_out = audit;
	return 1;
}

int SG_GroundCapabilityPublicationDescribe(
	const sg_ground_capability_publication_t *publication,
	sg_ground_capability_publication_description_t *description_out)
{
	if (!PublicationValid(publication) || !description_out)
		return 0;
	*description_out = publication->description;
	return 1;
}

int SG_GroundCapabilityPublicationFact(
	const sg_ground_capability_publication_t *publication, uint32_t index,
	sg_ground_capability_publication_fact_t *fact_out)
{
	if (!PublicationValid(publication) || !fact_out ||
		index >= publication->description.fact_count)
		return 0;
	*fact_out = publication->facts[index];
	return 1;
}

void SG_GroundCapabilityPublicationDestroy(
	sg_ground_capability_publication_t *publication)
{
	if (!PublicationValid(publication))
		return;
	publication->state = 0U;
	publication->state_inverse = 0U;
	publication->self = NULL;
	free(publication);
}

const char *SG_GroundCapabilityAuditCodeString(
	sg_ground_capability_audit_code_t code)
{
	static const char *const names[] = {
		"ok", "invalid argument", "identity mismatch", "host law mismatch",
		"configuration rejected", "semantics rejected",
		"reconstruction rejected", "omitted fact", "invented fact",
		"fact disagreement", "completeness disagreement", "overflow",
		"out of memory"
	};

	if (code < 0 || (size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}
