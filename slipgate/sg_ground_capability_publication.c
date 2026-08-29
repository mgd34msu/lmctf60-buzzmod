#include "sg_ground_capability_publication.h"

#include <stddef.h>
#include <string.h>

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"

extern void Pmove(pmove_t *pmove);

static int FloatBitsEqual(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int Vec3BitsEqual(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	return FloatBitsEqual(left->value[0], right->value[0]) &&
		FloatBitsEqual(left->value[1], right->value[1]) &&
		FloatBitsEqual(left->value[2], right->value[2]);
}

static int IntervalBitsEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return FloatBitsEqual(left->min_value, right->min_value) &&
		FloatBitsEqual(left->max_value, right->max_value);
}

static int Interval3BitsEqual(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return IntervalBitsEqual(&left->x, &right->x) &&
		IntervalBitsEqual(&left->y, &right->y) &&
		IntervalBitsEqual(&left->z, &right->z);
}

int SG_GroundCapabilityFactBitsEqual(
	const sg_ground_capability_t *left,
	const sg_ground_capability_t *right)
{
	return left && right && left->source_cell == right->source_cell &&
		left->destination_cell == right->destination_cell &&
		left->source_region == right->source_region &&
		left->destination_region == right->destination_region &&
		left->portal == right->portal &&
		left->source_phase == right->source_phase &&
		left->destination_phase == right->destination_phase &&
		left->kind == right->kind &&
		Vec3BitsEqual(&left->source_witness, &right->source_witness) &&
		Vec3BitsEqual(&left->destination_witness,
			&right->destination_witness) &&
		Vec3BitsEqual(&left->initial_velocity, &right->initial_velocity) &&
		Vec3BitsEqual(&left->observed_velocity, &right->observed_velocity) &&
		Interval3BitsEqual(&left->displacement, &right->displacement) &&
		IntervalBitsEqual(&left->duration_ms, &right->duration_ms) &&
		FloatBitsEqual(left->acceleration, right->acceleration) &&
		FloatBitsEqual(left->gravity, right->gravity) &&
		left->physics_abi_id == right->physics_abi_id &&
		left->flags == right->flags;
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return Vec3BitsEqual(&left->mins, &right->mins) &&
		Vec3BitsEqual(&left->maxs, &right->maxs);
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return FloatBitsEqual(left->gravity, right->gravity) &&
		FloatBitsEqual(left->ground_acceleration, right->ground_acceleration) &&
		FloatBitsEqual(left->air_acceleration, right->air_acceleration) &&
		FloatBitsEqual(left->water_acceleration, right->water_acceleration) &&
		FloatBitsEqual(left->hook_acceleration, right->hook_acceleration) &&
		FloatBitsEqual(left->external_acceleration,
			right->external_acceleration) &&
		FloatBitsEqual(left->water_drag, right->water_drag) &&
		FloatBitsEqual(left->max_velocity, right->max_velocity) &&
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

static void SetResult(sg_ground_capability_audit_result_t *result,
	sg_ground_capability_audit_code_t code)
{
	result->code = code;
	result->record = SG_GROUND_CAPABILITY_INDEX_NONE;
}

static int SourcePointersValid(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate)
{
	return source && candidate && source->authority &&
		source->authority->world && source->configuration &&
		source->semantics && source->host_pmove &&
		(candidate->capability_count == 0U || candidate->capabilities);
}

static int AuditAcceptedSources(
	const sg_ground_capability_publication_source_t *source,
	sg_ground_capability_audit_result_t *result)
{
	sg_bsp_completeness_result_t completeness;
	sg_configuration_audit_result_t configuration;
	sg_configuration_semantics_audit_result_t semantics;
	int bsp_ok;
	int configuration_ok;
	int semantics_ok;

	bsp_ok = SG_BspCompletenessProve(source->authority,
		source->configuration, &completeness) &&
		completeness.code == SG_BSP_COMPLETENESS_OK;
	configuration_ok = SG_ConfigurationAudit(source->authority,
		source->configuration, &configuration) &&
		configuration.code == SG_CONFIGURATION_AUDIT_OK;
	semantics_ok = SG_ConfigurationSemanticsAudit(source->authority,
		source->configuration, source->semantics, &semantics) &&
		semantics.code == SG_CONFIGURATION_SEMANTICS_AUDIT_OK;
	if (!bsp_ok || !configuration_ok)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED);
		return 0;
	}
	if (!semantics_ok)
	{
		SetResult(result, SG_GROUND_CAPABILITY_AUDIT_SEMANTICS_REJECTED);
		return 0;
	}
	return 1;
}

int SG_GroundCapabilityAudit(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_audit_result_t *result_out)
{
	sg_ground_capability_audit_result_t result;

	memset(&result, 0, sizeof(result));
	SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	result.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
	if (!result_out)
		return 0;
	*result_out = result;
	if (!SourcePointersValid(source, candidate))
		return 0;
	if (!IdentityEqual(&source->authority->identity,
			&source->configuration->identity) ||
		!IdentityEqual(&source->authority->identity,
			&source->semantics->identity))
	{
		SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
		goto fail;
	}
	if (!AuditAcceptedSources(source, &result))
		goto fail;
	if (source->host_pmove != Pmove || source->host_law_identity == 0U ||
		source->host_law_identity != source->authority->identity.physics_abi_id)
	{
		SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_HOST_LAW_MISMATCH);
		goto fail;
	}
	if (!IdentityEqual(&source->authority->identity, &candidate->identity))
	{
		SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
		goto fail;
	}
	result.candidate_facts = candidate->capability_count;
	if (!source->phase_catalog)
		SetResult(&result, SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REQUIRED);
	else
		SetResult(&result,
			SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_UNAVAILABLE);

fail:
	*result_out = result;
	return 0;
}

int SG_GroundCapabilityPublicationIssue(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_publication_t **publication_out,
	sg_ground_capability_audit_result_t *audit_out)
{
	sg_ground_capability_audit_result_t audit;

	memset(&audit, 0, sizeof(audit));
	SetResult(&audit, SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	audit.completeness = SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED;
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
	*audit_out = audit;
	return 0;
}

int SG_GroundCapabilityPublicationDescribe(
	const sg_ground_capability_publication_t *publication,
	sg_ground_capability_publication_description_t *description_out)
{
	(void)publication;
	(void)description_out;
	return 0;
}

int SG_GroundCapabilityPublicationFact(
	const sg_ground_capability_publication_t *publication, uint32_t index,
	sg_ground_capability_publication_fact_t *fact_out)
{
	(void)publication;
	(void)index;
	(void)fact_out;
	return 0;
}

void SG_GroundCapabilityPublicationDestroy(
	sg_ground_capability_publication_t *publication)
{
	(void)publication;
}

const char *SG_GroundCapabilityAuditCodeString(
	sg_ground_capability_audit_code_t code)
{
	static const char *const names[] = {
		"ok", "invalid argument", "identity mismatch", "host law mismatch",
		"configuration rejected", "semantics rejected",
		"phase catalog required", "phase catalog unavailable",
		"reconstruction rejected", "omitted fact", "invented fact",
		"fact disagreement", "completeness disagreement", "overflow",
		"out of memory"
	};

	if (code < 0 || (size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}
