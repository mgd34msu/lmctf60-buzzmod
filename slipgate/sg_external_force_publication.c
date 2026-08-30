#include "sg_external_force_publication.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"
#include "sg_configuration_lattice.h"
#include "sg_host_law_construction_offline.h"

#define SG_EXTERNAL_FORCE_MAGIC UINT64_C(0x455854464f524345)
#define SG_EXTERNAL_FORCE_CURRENT_MASK \
	(SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_90 | \
	 SG_HOST_CONTENTS_CURRENT_180 | SG_HOST_CONTENTS_CURRENT_270 | \
	 SG_HOST_CONTENTS_CURRENT_UP | SG_HOST_CONTENTS_CURRENT_DOWN)
#define SG_EXTERNAL_FORCE_GROUND_PROBE 0.25f
#define SG_EXTERNAL_FORCE_GROUND_NORMAL_Z 0.7f

typedef struct sg_external_force_build_s
{
	const sg_external_force_source_t *source;
	sg_bsp_entity_semantics_view_t entities;
	const sg_mechanism_capability_view_t *mechanisms;
	const sg_phase_catalog_view_t *phases;
	sg_host_law_view_t host;
	sg_external_force_fact_t *facts;
	uint32_t fact_count;
	uint32_t fact_capacity;
	sg_external_force_audit_code_t error;
	uint32_t record;
} sg_external_force_build_t;

struct sg_external_force_publication_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const sg_external_force_publication_t *self;
	size_t allocation_size;
	size_t allocation_size_inverse;
	sg_external_force_view_t view;
};

static int FloatBitsEqual(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int VecEqual(const sg_rune_vec3_t *left, const sg_rune_vec3_t *right)
{
	return FloatBitsEqual(left->value[0], right->value[0]) &&
		FloatBitsEqual(left->value[1], right->value[1]) &&
		FloatBitsEqual(left->value[2], right->value[2]);
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return VecEqual(&left->mins, &right->mins) &&
		VecEqual(&left->maxs, &right->maxs);
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return FloatBitsEqual(left->gravity, right->gravity) &&
		FloatBitsEqual(left->ground_acceleration,
			right->ground_acceleration) &&
		FloatBitsEqual(left->air_acceleration, right->air_acceleration) &&
		FloatBitsEqual(left->water_acceleration,
			right->water_acceleration) &&
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

static int EntityBindingEqual(
	const sg_bsp_entity_semantics_binding_t *left,
	const sg_bsp_entity_semantics_binding_t *right)
{
	return left->source_set_identity == right->source_set_identity &&
		memcmp(&left->source_identity, &right->source_identity,
			sizeof(left->source_identity)) == 0 &&
		memcmp(&left->schema_identity, &right->schema_identity,
			sizeof(left->schema_identity)) == 0;
}

static int StableEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return SG_RuneModelStableIdEqual(left, right);
}

static int StableCompare(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static int IntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return FloatBitsEqual(left->min_value, right->min_value) &&
		FloatBitsEqual(left->max_value, right->max_value);
}

static int Interval3Equal(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return IntervalEqual(&left->x, &right->x) &&
		IntervalEqual(&left->y, &right->y) &&
		IntervalEqual(&left->z, &right->z);
}

static int FactEqual(const sg_external_force_fact_t *left,
	const sg_external_force_fact_t *right)
{
	return left->kind == right->kind &&
		left->source_entity_ordinal == right->source_entity_ordinal &&
		left->mechanism_entity_index == right->mechanism_entity_index &&
		StableEqual(&left->mechanism.value, &right->mechanism.value) &&
		StableEqual(&left->source_cell.value, &right->source_cell.value) &&
		StableEqual(&left->destination_cell.value,
			&right->destination_cell.value) &&
		left->source_region_id == right->source_region_id &&
		left->destination_region_id == right->destination_region_id &&
		left->source_model_index == right->source_model_index &&
		left->source_leaf_index == right->source_leaf_index &&
		left->source_brush_index == right->source_brush_index &&
		left->source_brush_side_index == right->source_brush_side_index &&
		left->source_contents == right->source_contents &&
		VecEqual(&left->source_witness, &right->source_witness) &&
		VecEqual(&left->support_witness, &right->support_witness) &&
		VecEqual(&left->support_normal, &right->support_normal) &&
		FloatBitsEqual(left->support_distance, right->support_distance) &&
		VecEqual(&left->source_model_origin, &right->source_model_origin) &&
		VecEqual(&left->source_model_angles, &right->source_model_angles) &&
		StableEqual(&left->portal.value, &right->portal.value) &&
		StableEqual(&left->source_phase.value, &right->source_phase.value) &&
		StableEqual(&left->destination_phase.value,
			&right->destination_phase.value) &&
		Interval3Equal(&left->displacement, &right->displacement) &&
		VecEqual(&left->velocity, &right->velocity) &&
		VecEqual(&left->acceleration, &right->acceleration) &&
		FloatBitsEqual(left->gravity, right->gravity) &&
		left->delay_ms == right->delay_ms &&
		left->duration_ms == right->duration_ms &&
		left->dwell_ms == right->dwell_ms &&
		left->wait_ms == right->wait_ms &&
		left->reset_ms == right->reset_ms &&
		left->physics_abi_id == right->physics_abi_id &&
		left->flags == right->flags &&
		left->observation == right->observation &&
		memcmp(&left->input_state, &right->input_state,
			sizeof(left->input_state)) == 0 &&
		memcmp(&left->input_command, &right->input_command,
			sizeof(left->input_command)) == 0 &&
		left->input_grounded == right->input_grounded &&
		left->input_water_level == right->input_water_level &&
		left->input_support_model_index == right->input_support_model_index &&
		left->input_support_instance_id == right->input_support_instance_id &&
		memcmp(&left->output_state, &right->output_state,
			sizeof(left->output_state)) == 0 &&
		left->output_grounded == right->output_grounded &&
		left->output_water_level == right->output_water_level &&
		left->output_support_model_index == right->output_support_model_index &&
		left->output_support_instance_id == right->output_support_instance_id;
}

static int FactCompare(const void *left_value, const void *right_value)
{
	const sg_external_force_fact_t *left = left_value;
	const sg_external_force_fact_t *right = right_value;
	int comparison;

#define COMPARE_SCALAR(member) do { \
	if (left->member != right->member) \
		return left->member < right->member ? -1 : 1; \
} while (0)
	COMPARE_SCALAR(kind);
	comparison = StableCompare(&left->source_cell.value,
		&right->source_cell.value);
	if (comparison) return comparison;
	comparison = StableCompare(&left->destination_cell.value,
		&right->destination_cell.value);
	if (comparison) return comparison;
	COMPARE_SCALAR(source_region_id);
	COMPARE_SCALAR(destination_region_id);
	COMPARE_SCALAR(source_model_index);
	COMPARE_SCALAR(source_leaf_index);
	COMPARE_SCALAR(source_brush_index);
	COMPARE_SCALAR(source_brush_side_index);
	COMPARE_SCALAR(source_contents);
	comparison = StableCompare(&left->source_phase.value,
		&right->source_phase.value);
	if (comparison) return comparison;
	comparison = StableCompare(&left->destination_phase.value,
		&right->destination_phase.value);
	if (comparison) return comparison;
	COMPARE_SCALAR(source_entity_ordinal);
	COMPARE_SCALAR(mechanism_entity_index);
	comparison = StableCompare(&left->mechanism.value,
		&right->mechanism.value);
	if (comparison) return comparison;
	return memcmp(left, right, sizeof(*left));
#undef COMPARE_SCALAR
}

static void SetAudit(sg_external_force_audit_result_t *audit,
	sg_external_force_audit_code_t code, uint32_t record)
{
	audit->code = code;
	audit->record = record;
}

static int AppendFact(sg_external_force_build_t *build,
	const sg_external_force_fact_t *fact)
{
	sg_external_force_fact_t *replacement;
	uint32_t capacity;

	if (build->fact_count == UINT32_MAX)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_OVERFLOW;
		return 0;
	}
	if (build->fact_count == build->fact_capacity)
	{
		capacity = build->fact_capacity ? build->fact_capacity * 2U : 16U;
		if (capacity < build->fact_capacity)
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_OVERFLOW;
			return 0;
		}
		replacement = realloc(build->facts,
			(size_t)capacity * sizeof(*replacement));
		if (!replacement)
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_OUT_OF_MEMORY;
			return 0;
		}
		build->facts = replacement;
		build->fact_capacity = capacity;
	}
	build->facts[build->fact_count++] = *fact;
	return 1;
}

static int SourceIdentityAccepted(sg_external_force_build_t *build)
{
	const sg_external_force_source_t *source = build->source;
	const sg_rune_model_identity_t *identity =
		&source->collision_authority->identity;
	sg_bsp_completeness_result_t completeness;
	sg_configuration_audit_result_t configuration;
	sg_configuration_semantics_audit_result_t semantics;
	sg_host_law_construction_view_t construction;
	sg_host_law_result_t host_result;

	if (!IdentityEqual(identity, &source->configuration->identity) ||
		!IdentityEqual(identity, &source->configuration_semantics->identity) ||
		!IdentityEqual(identity, &build->mechanisms->identity) ||
		!IdentityEqual(identity, &build->phases->identity))
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_IDENTITY_MISMATCH;
		return 0;
	}
	if (memcmp(build->entities.binding.source_identity.bytes,
			build->host.bsp_identity.bytes,
			sizeof(build->entities.binding.source_identity.bytes)) != 0 ||
		memcmp(source->collision_authority->content_identity.bytes,
			build->host.bsp_identity.bytes,
			sizeof(build->host.bsp_identity.bytes)) != 0 ||
		build->entities.binding.source_set_identity !=
			identity->source_set_identity ||
		!FloatBitsEqual(build->entities.world.gravity,
			identity->physics.gravity) ||
		memcmp(&build->entities.binding.schema_identity,
			&SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID,
			sizeof(build->entities.binding.schema_identity)) != 0 ||
		build->host.version != SG_HOST_LAW_PUBLICATION_VERSION ||
		build->host.collision_law_id == 0U ||
		build->host.pmove_law_id == 0U ||
		build->host.gravity_law_id == 0U ||
		build->host.static_identity.physics_abi_id != identity->physics_abi_id ||
		!HullEqual(&build->host.static_identity.standing_hull,
			&identity->standing_hull) ||
		!HullEqual(&build->host.static_identity.crouching_hull,
			&identity->crouching_hull) ||
		!PhysicsEqual(&build->host.static_identity.physics, &identity->physics))
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_IDENTITY_MISMATCH;
		return 0;
	}
	host_result = SG_HostLawPublicationRevalidateProduction(
		source->engine_authority);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_AUTHORITY_REJECTED;
		return 0;
	}
	memset(&construction, 0, sizeof(construction));
	host_result = SG_HostLawConstructionRead(source->construction,
		&construction);
	if (host_result.status != SG_HOST_LAW_OK || construction.current != 1U ||
		memcmp(&construction.laws, &build->host, sizeof(build->host)) != 0 ||
		memcmp(construction.geometry.bsp_identity.bytes,
			source->collision_authority->content_identity.bytes,
			sizeof(construction.geometry.bsp_identity.bytes)) != 0 ||
		construction.geometry.node_count !=
			source->collision_authority->world->node_count ||
		construction.geometry.model_count !=
			source->collision_authority->world->model_count ||
		construction.geometry.brush_count !=
			source->collision_authority->world->brush_count ||
		construction.geometry.brush_side_count !=
			source->collision_authority->world->brush_side_count)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_AUTHORITY_REJECTED;
		return 0;
	}
	host_result = SG_HostLawConstructionCompletenessProve(
		source->construction, source->configuration, &completeness);
	if (host_result.status != SG_HOST_LAW_OK ||
		completeness.code != SG_BSP_COMPLETENESS_OK)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_CONFIGURATION_REJECTED;
		return 0;
	}
	host_result = SG_HostLawConstructionConfigurationAudit(
		source->construction, source->configuration, &configuration);
	if (host_result.status != SG_HOST_LAW_OK ||
		configuration.code != SG_CONFIGURATION_AUDIT_OK)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_CONFIGURATION_REJECTED;
		return 0;
	}
	host_result = SG_HostLawConstructionSemanticsAudit(source->construction,
		source->configuration, source->configuration_semantics, &semantics);
	if (host_result.status != SG_HOST_LAW_OK ||
		semantics.code != SG_CONFIGURATION_SEMANTICS_AUDIT_OK)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_CONFIGURATION_REJECTED;
		return 0;
	}
	return 1;
}

static int ReadSources(sg_external_force_build_t *build)
{
	const sg_external_force_source_t *source = build->source;
	sg_host_law_result_t host_result;

	if (!SG_BspEntitySemanticsPublicationRead(source->entity_semantics,
			&build->entities))
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_ENTITY_REJECTED;
		return 0;
	}
	if (!SG_MechanismCapabilityRead(source->mechanism_owner,
			source->mechanisms, &build->mechanisms))
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_MECHANISM_REJECTED;
		return 0;
	}
	if (!SG_PhaseCatalogPublicationRead(source->phase_owner, source->phases,
			&build->phases))
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_PHASE_REJECTED;
		return 0;
	}
	host_result = SG_HostLawPublicationRead(source->engine_authority,
		&build->host);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		build->error = SG_EXTERNAL_FORCE_AUDIT_AUTHORITY_REJECTED;
		return 0;
	}
	return SourceIdentityAccepted(build);
}

static int PhaseBindingFor(const sg_external_force_build_t *build,
	uint64_t region_id, uint32_t cell, const sg_rune_phase_ref_t *phase)
{
	uint32_t index;

	for (index = 0U; index < build->phases->binding_count; index++)
	{
		const sg_phase_catalog_binding_t *binding =
			&build->phases->bindings[index];

		if (binding->semantic_region_id == region_id &&
			binding->configuration_cell == cell &&
			StableEqual(&binding->phase.value, &phase->value))
			return 1;
	}
	return 0;
}

static uint32_t MechanismStateBit(sg_mechanism_state_t state)
{
	return state < SG_MECHANISM_STATE_COUNT ?
		UINT32_C(1) << (uint32_t)state : 0U;
}

static uint32_t MechanismTimingSpan(
	const sg_mechanism_capability_fact_t *mechanism)
{
	uint64_t total = (uint64_t)mechanism->delay_ms + mechanism->dwell_ms +
		mechanism->travel_ms + mechanism->wait_ms + mechanism->reset_ms;

	if (total == 0U)
		return 1U;
	return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static int IssueAppendMechanismFacts(sg_external_force_build_t *build)
{
	const sg_configuration_space_t *configuration =
		build->source->configuration;
	uint32_t index;

	for (index = 0U; index < build->phases->transition_count; index++)
	{
		const sg_phase_catalog_transition_evidence_t *evidence =
			&build->phases->transition_evidence[index];
		const sg_rune_phase_transition_t *transition =
			&build->phases->transitions[index];
		const sg_mechanism_capability_fact_t *mechanism;
		sg_external_force_fact_t fact;
		int include;

		if (evidence->origin !=
			SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING)
			continue;
		if (evidence->source_record >= build->mechanisms->fact_count ||
			evidence->source_cell >= configuration->cell_count ||
			evidence->destination_cell >= configuration->cell_count)
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
			build->record = index;
			return 0;
		}
		mechanism = &build->mechanisms->facts[evidence->source_record];
		if (!StableEqual(&evidence->mechanism.value,
				&mechanism->mechanism_id.value) ||
			evidence->provider_verifier_identity !=
				build->phases->mover_support_verifier_identity ||
			evidence->source_state_mask !=
				MechanismStateBit(mechanism->source_state) ||
			evidence->destination_state_mask !=
				MechanismStateBit(mechanism->destination_state) ||
			evidence->delay_ms != mechanism->delay_ms ||
			evidence->dwell_ms != mechanism->dwell_ms ||
			evidence->travel_ms != mechanism->travel_ms ||
			evidence->wait_ms != mechanism->wait_ms ||
			evidence->reset_ms != mechanism->reset_ms ||
			evidence->activation_time_ms != mechanism->activation_time_ms ||
			evidence->active_time_ms != mechanism->active_time_ms ||
			evidence->exit_time_ms != mechanism->exit_time_ms ||
			evidence->reset_time_ms != mechanism->reset_time_ms ||
			!StableEqual(&transition->cell.value,
				&configuration->cells[evidence->source_cell].id.value) ||
			!StableEqual(&transition->destination_cell.value,
				&configuration->cells[evidence->destination_cell].id.value) ||
			!IntervalEqual(&transition->duration_ms,
				&(sg_rune_interval_t){
					(float)MechanismTimingSpan(mechanism),
					(float)MechanismTimingSpan(mechanism) }))
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
			build->record = index;
			return 0;
		}
		include = mechanism->kind == SG_MECHANISM_CAPABILITY_PUSH ||
			(mechanism->flags & SG_MECHANISM_CAPABILITY_MOVER_RELATIVE) != 0U ||
			mechanism->kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
			mechanism->kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE ||
			mechanism->kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING;
		if (!include)
			continue;
		if (mechanism->source_region >=
				build->source->configuration_semantics->region_count ||
			mechanism->destination_region >=
				build->source->configuration_semantics->region_count ||
			build->source->configuration_semantics->regions[
				mechanism->source_region].id != evidence->source_region_id ||
			build->source->configuration_semantics->regions[
				mechanism->destination_region].id !=
				evidence->destination_region_id)
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
			build->record = index;
			return 0;
		}
		memset(&fact, 0, sizeof(fact));
		fact.source_entity_ordinal = UINT32_MAX;
		fact.mechanism_entity_index = mechanism->mechanism_entity;
		fact.kind = mechanism->kind == SG_MECHANISM_CAPABILITY_PUSH ?
			SG_EXTERNAL_FORCE_TRIGGER_PUSH :
			SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT;
		fact.mechanism.value = mechanism->mechanism_id.value;
		fact.source_cell = configuration->cells[evidence->source_cell].id;
		fact.destination_cell =
			configuration->cells[evidence->destination_cell].id;
		fact.source_region_id = evidence->source_region_id;
		fact.destination_region_id = evidence->destination_region_id;
		fact.source_model_index = UINT32_MAX;
		fact.source_leaf_index = UINT32_MAX;
		fact.source_brush_index = UINT32_MAX;
		fact.source_brush_side_index = UINT32_MAX;
		fact.portal = evidence->portal;
		fact.source_phase = transition->source_phase;
		fact.destination_phase = transition->destination_phase;
		fact.displacement = mechanism->parameters.displacement;
		fact.velocity = mechanism->observed_velocity;
		fact.acceleration.value[0] = mechanism->mechanism_direction.value[0] *
			mechanism->parameters.acceleration.max_value;
		fact.acceleration.value[1] = mechanism->mechanism_direction.value[1] *
			mechanism->parameters.acceleration.max_value;
		fact.acceleration.value[2] = mechanism->mechanism_direction.value[2] *
			mechanism->parameters.vertical_acceleration.max_value;
		fact.gravity = mechanism->parameters.gravity;
		fact.delay_ms = mechanism->delay_ms;
		fact.duration_ms = mechanism->travel_ms;
		fact.dwell_ms = mechanism->dwell_ms;
		fact.wait_ms = mechanism->wait_ms;
		fact.reset_ms = mechanism->reset_ms;
		fact.physics_abi_id = build->phases->identity.physics_abi_id;
		fact.flags = SG_EXTERNAL_FORCE_HOST_PROVEN;
		if (mechanism->flags & SG_MECHANISM_CAPABILITY_ONE_SHOT)
			fact.flags |= SG_EXTERNAL_FORCE_ONE_SHOT;
		if (mechanism->flags & SG_MECHANISM_CAPABILITY_CONDITIONAL)
			fact.flags |= SG_EXTERNAL_FORCE_CONDITIONAL;
		if (fact.kind == SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT)
			fact.flags |= SG_EXTERNAL_FORCE_MOVER_RELATIVE;
		if (!PhaseBindingFor(build, fact.source_region_id,
				evidence->source_cell, &fact.source_phase) ||
			!PhaseBindingFor(build, fact.destination_region_id,
				evidence->destination_cell, &fact.destination_phase) ||
			!AppendFact(build, &fact))
		{
			if (build->error == SG_EXTERNAL_FORCE_AUDIT_OK)
				build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
			build->record = index;
			return 0;
		}
	}
	return 1;
}

static int Q8Coordinate(float value, short *coordinate)
{
	float scaled = value * 8.0f;
	long rounded;

	if (!isfinite(value) || scaled < (float)SHRT_MIN ||
		scaled > (float)SHRT_MAX)
		return 0;
	rounded = lrintf(scaled);
	if (rounded < SHRT_MIN || rounded > SHRT_MAX ||
		(float)rounded != scaled)
		return 0;
	*coordinate = (short)rounded;
	return 1;
}

typedef struct sg_external_force_brush_ref_s
{
	uint32_t brush;
	uint32_t leaf;
} sg_external_force_brush_ref_t;

typedef struct sg_external_force_brush_refs_s
{
	sg_external_force_brush_ref_t *values;
	uint32_t count;
} sg_external_force_brush_refs_t;

static void IssueAngleAxis(const float angles[3], float axis[3][3])
{
	const float radians = 0.01745329251994329577f;
	float sy = sinf(angles[1] * radians);
	float cy = cosf(angles[1] * radians);
	float sp = sinf(angles[0] * radians);
	float cp = cosf(angles[0] * radians);
	float sr = sinf(angles[2] * radians);
	float cr = cosf(angles[2] * radians);

	axis[0][0] = cp * cy;
	axis[0][1] = cp * sy;
	axis[0][2] = -sp;
	axis[1][0] = sr * sp * cy - cr * sy;
	axis[1][1] = sr * sp * sy + cr * cy;
	axis[1][2] = sr * cp;
	axis[2][0] = cr * sp * cy + sr * sy;
	axis[2][1] = cr * sp * sy - sr * cy;
	axis[2][2] = cr * cp;
}

static void IssueWorldPlane(const float local_normal[3], float local_distance,
	const sg_host_collision_transform_t *transform, float normal[3],
	float *distance)
{
	float axis[3][3];
	uint32_t row, column;

	IssueAngleAxis(transform->angles, axis);
	for (column = 0U; column < 3U; column++)
	{
		normal[column] = 0.0f;
		for (row = 0U; row < 3U; row++)
			normal[column] += local_normal[row] * axis[row][column];
	}
	*distance = local_distance;
	for (column = 0U; column < 3U; column++)
		*distance += normal[column] * transform->origin[column];
}

/* Issuance discovers the exact brush set reachable from the submodel headnode
 * with an explicit, node-count-bounded stack.  The visited set handles legal
 * shared subtrees without imposing a work budget. */
static int IssueCollectModelBrushes(const sg_bsp_world_t *world,
	uint32_t model_index, sg_external_force_brush_refs_t *refs)
{
	int32_t *stack;
	uint8_t *visited_nodes;
	uint32_t *ref_index_by_brush;
	uint32_t stack_count = 0U;
	uint32_t brush_index;

	memset(refs, 0, sizeof(*refs));
	if (model_index >= world->model_count || world->node_count == UINT32_MAX)
		return 0;
	stack = calloc((size_t)world->node_count + 1U, sizeof(*stack));
	visited_nodes = calloc(world->node_count ? world->node_count : 1U, 1U);
	ref_index_by_brush = calloc(world->brush_count ? world->brush_count : 1U,
		sizeof(*ref_index_by_brush));
	refs->values = calloc(world->brush_count ? world->brush_count : 1U,
		sizeof(*refs->values));
	if (!stack || !visited_nodes || !ref_index_by_brush || !refs->values)
	{
		free(stack);
		free(visited_nodes);
		free(ref_index_by_brush);
		free(refs->values);
		memset(refs, 0, sizeof(*refs));
		return 0;
	}
	for (brush_index = 0U; brush_index < world->brush_count; brush_index++)
		ref_index_by_brush[brush_index] = UINT32_MAX;
	stack[stack_count++] = world->models[model_index].headnode;
	while (stack_count != 0U)
	{
		int32_t child = stack[--stack_count];

		if (child >= 0)
		{
			const sg_bsp_node_t *node;

			if ((uint32_t)child >= world->node_count)
				goto invalid;
			if (visited_nodes[(uint32_t)child])
				continue;
			visited_nodes[(uint32_t)child] = 1U;
			node = &world->nodes[(uint32_t)child];
			if (stack_count > world->node_count - 1U)
				goto invalid;
			stack[stack_count++] = node->children[1];
			stack[stack_count++] = node->children[0];
		}
		else
		{
			uint32_t leaf = (uint32_t)(-1 - child);
			const sg_bsp_leaf_t *record;
			uint32_t offset;

			if (leaf >= world->leaf_count)
				goto invalid;
			record = &world->leaves[leaf];
			if (record->first_leaf_brush > world->leaf_brush_count ||
				record->leaf_brush_count > world->leaf_brush_count -
					record->first_leaf_brush)
				goto invalid;
			for (offset = 0U; offset < record->leaf_brush_count; offset++)
			{
				uint32_t brush = world->leaf_brushes[
					record->first_leaf_brush + offset];

				if (brush >= world->brush_count)
					goto invalid;
				if (ref_index_by_brush[brush] == UINT32_MAX)
				{
					ref_index_by_brush[brush] = refs->count;
					refs->values[refs->count].brush = brush;
					refs->values[refs->count].leaf = leaf;
					refs->count++;
				}
				else if (leaf < refs->values[
					ref_index_by_brush[brush]].leaf)
					refs->values[ref_index_by_brush[brush]].leaf = leaf;
			}
		}
	}
	free(stack);
	free(visited_nodes);
	free(ref_index_by_brush);
	return 1;

invalid:
	free(stack);
	free(visited_nodes);
	free(ref_index_by_brush);
	free(refs->values);
	memset(refs, 0, sizeof(*refs));
	return 0;
}

static float IssueExpandedDistance(const sg_bsp_plane_t *plane,
	const sg_rune_hull_profile_t *hull)
{
	float corner[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		corner[axis] = plane->normal.value[axis] < 0.0f ?
			hull->maxs.value[axis] : hull->mins.value[axis];
	return plane->distance -
		(corner[0] * plane->normal.value[0] +
		 corner[1] * plane->normal.value[1] +
		 corner[2] * plane->normal.value[2]);
}

static int IssueSupportWitness(const sg_external_force_build_t *build,
	const sg_bsp_entity_semantic_t *entity,
	const sg_configuration_semantic_region_t *region, uint32_t brush_index,
	uint32_t support_side, sg_rune_vec3_t *witness)
{
	const sg_bsp_world_t *world = build->source->collision_authority->world;
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[region->cell];
	const sg_rune_hull_profile_t *hull = cell->stance == SG_RUNE_STANCE_STANDING ?
		&build->phases->identity.standing_hull :
		&build->phases->identity.crouching_hull;
	sg_configuration_lattice_halfspace_t *spaces;
	sg_host_collision_transform_t transform;
	uint32_t count = 0U, side_offset, axis;
	int32_t point[3];
	sg_configuration_lattice_stats_t stats;
	int solved;

	if (region->first_face > semantics->face_count ||
		region->face_count > semantics->face_count - region->first_face ||
		brush->first_side > world->brush_side_count ||
		brush->side_count > world->brush_side_count - brush->first_side ||
		region->face_count > UINT32_MAX - brush->side_count - 2U)
		return 0;
	spaces = calloc((size_t)region->face_count + brush->side_count + 2U,
		sizeof(*spaces));
	if (!spaces)
		return 0;
	for (axis = 0U; axis < region->face_count; axis++)
	{
		const sg_configuration_semantic_face_t *face =
			&semantics->faces[region->first_face + axis];

		memcpy(spaces[count].normal, face->normal, sizeof(face->normal));
		spaces[count].distance = face->distance;
		spaces[count].open = face->open != 0U;
		count++;
	}
	memset(&transform, 0, sizeof(transform));
	memcpy(transform.origin, entity->origin.value, sizeof(transform.origin));
	memcpy(transform.angles, entity->angles.value, sizeof(transform.angles));
	for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&world->brush_sides[brush->first_side + side_offset];
		const sg_bsp_plane_t *plane;
		float distance;

		if (side->plane >= world->plane_count)
		{
			free(spaces);
			return 0;
		}
		plane = &world->planes[side->plane];
		IssueWorldPlane(plane->normal.value,
			IssueExpandedDistance(plane, hull), &transform,
			spaces[count].normal, &distance);
		spaces[count].distance = distance;
		if (side_offset == support_side)
		{
			for (axis = 0U; axis < 3U; axis++)
				spaces[count].normal[axis] = -spaces[count].normal[axis];
			spaces[count].distance = -distance;
			spaces[count].open = 1;
		}
		else
			spaces[count].open = 1;
		count++;
		if (side_offset == support_side)
		{
			IssueWorldPlane(plane->normal.value,
				IssueExpandedDistance(plane, hull), &transform,
				spaces[count].normal, &distance);
			spaces[count].distance = distance +
				SG_EXTERNAL_FORCE_GROUND_PROBE * spaces[count].normal[2];
			count++;
		}
	}
	memset(&stats, 0, sizeof(stats));
	solved = SG_ConfigurationLatticeFind(spaces, count, NULL, point, &stats);
	free(spaces);
	if (solved != 1)
		return solved == 0 ? 2 : 0;
	for (axis = 0U; axis < 3U; axis++)
		witness->value[axis] = (float)point[axis] * 0.125f;
	return 1;
}

static int IssueAppendLocalFact(sg_external_force_build_t *build,
	sg_external_force_kind_t kind, uint32_t region_index,
	const sg_phase_catalog_binding_t *binding,
	sg_host_collision_contents_t currents, uint32_t leaf_index,
	sg_external_force_observation_t observation)
{
	const sg_configuration_semantic_region_t *region =
		&build->source->configuration_semantics->regions[region_index];
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[region->cell];
	sg_external_force_fact_t fact;
	sg_host_collision_pose_t pose;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error;
	uint32_t axis;
	float seconds = (float)build->phases->identity.physics.frame_ms * 0.001f;

	memset(&fact, 0, sizeof(fact));
	memset(&pose, 0, sizeof(pose));
	if (SG_HostLawConstructionClassifyPose(build->source->construction, NULL,
			region->interior_witness.value, cell->stance, &pose).status !=
			SG_HOST_LAW_OK || !pose.valid)
		return 0;
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	if (cell->stance == SG_RUNE_STANCE_CROUCHING)
		request.state.pm_flags |= PMF_DUCKED;
	for (axis = 0U; axis < 3U; axis++)
		if (!Q8Coordinate(region->interior_witness.value[axis],
				&request.state.origin[axis]))
			return 0;
	if (observation == SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)
	{
		request.command.forwardmove = SHRT_MAX;
		request.command.sidemove = SHRT_MAX;
	}
	else if (observation == SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)
	{
		float initial = build->phases->identity.physics.max_velocity - 1.0f;

		if (!Q8Coordinate(initial, &request.state.velocity[0]))
			return 0;
		request.command.forwardmove = SHRT_MAX;
	}
	request.previous_state = request.state;
	memset(&result, 0, sizeof(result));
	pmove_error = SG_HOST_PMOVE_ERROR_NONE;
	if (SG_HostLawConstructionPmove(build->source->construction, NULL,
			&request, &result, &pmove_error).status != SG_HOST_LAW_OK ||
		pmove_error != SG_HOST_PMOVE_ERROR_NONE)
		return 0;
	fact.kind = kind;
	fact.source_entity_ordinal = UINT32_MAX;
	fact.mechanism_entity_index = UINT32_MAX;
	fact.mechanism = SG_RUNE_MECHANISM_REF_NONE;
	fact.source_cell = cell->id;
	fact.destination_cell = cell->id;
	fact.source_region_id = region->id;
	fact.destination_region_id = region->id;
	fact.source_model_index = 0U;
	fact.source_leaf_index = leaf_index;
	fact.source_brush_index = UINT32_MAX;
	fact.source_brush_side_index = UINT32_MAX;
	fact.source_contents = currents;
	fact.source_witness = region->interior_witness;
	fact.support_witness.value[0] = pose.support.end[0];
	fact.support_witness.value[1] = pose.support.end[1];
	fact.support_witness.value[2] = pose.support.end[2];
	fact.support_normal.value[0] = pose.support.plane.normal[0];
	fact.support_normal.value[1] = pose.support.plane.normal[1];
	fact.support_normal.value[2] = pose.support.plane.normal[2];
	fact.support_distance = pose.support.plane.distance;
	fact.portal = SG_RUNE_PORTAL_REF_NONE;
	fact.source_phase = binding->phase;
	fact.destination_phase = binding->phase;
	for (axis = 0U; axis < 3U; axis++)
	{
		float input_velocity = (float)request.state.velocity[axis] * 0.125f;
		float delta = result.origin[axis] - region->interior_witness.value[axis];

		if (axis == 0U)
			fact.displacement.x.min_value = fact.displacement.x.max_value = delta;
		else if (axis == 1U)
			fact.displacement.y.min_value = fact.displacement.y.max_value = delta;
		else
			fact.displacement.z.min_value = fact.displacement.z.max_value = delta;
		fact.velocity.value[axis] = result.velocity[axis];
		fact.acceleration.value[axis] =
			(result.velocity[axis] - input_velocity) / seconds;
	}
	fact.gravity = result.gravity;
	fact.duration_ms = result.elapsed_ms;
	fact.physics_abi_id = build->phases->identity.physics_abi_id;
	fact.flags = SG_EXTERNAL_FORCE_HOST_PROVEN;
	fact.observation = observation;
	fact.input_state = request.state;
	fact.input_command = request.command;
	fact.input_grounded = pose.supported ? 1U : 0U;
	fact.input_water_level = (uint32_t)pose.water_level;
	fact.input_support_model_index = pose.support.model_index;
	fact.input_support_instance_id = pose.support.instance_id;
	fact.output_state = result.state;
	fact.output_grounded = result.grounded ? 1U : 0U;
	fact.output_water_level = (uint32_t)result.water_level;
	fact.output_support_model_index = result.support_model_index;
	fact.output_support_instance_id = result.support_instance_id;
	return AppendFact(build, &fact);
}

static int IssueAppendConveyorFact(sg_external_force_build_t *build,
	const sg_bsp_entity_semantic_t *entity,
	const sg_configuration_semantic_region_t *region,
	const sg_phase_catalog_binding_t *binding, uint32_t leaf_index,
	uint32_t brush_index, uint32_t side_index,
	const sg_rune_vec3_t *witness,
	sg_external_force_observation_t observation)
{
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[region->cell];
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	sg_host_collision_pose_t pose;
	sg_host_collision_contents_t point_contents;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error = SG_HOST_PMOVE_ERROR_NONE;
	sg_external_force_fact_t fact;
	uint32_t axis;
	float seconds = (float)build->phases->identity.physics.frame_ms * 0.001f;

	memset(&instance, 0, sizeof(instance));
	instance.instance_id = (uint64_t)entity->source_entity_ordinal + 1U;
	instance.model_index = entity->bsp_model;
	memcpy(instance.transform.origin, entity->origin.value,
		sizeof(instance.transform.origin));
	memcpy(instance.transform.angles, entity->angles.value,
		sizeof(instance.transform.angles));
	scene.instances = &instance;
	scene.instance_count = 1U;
	if (SG_HostLawConstructionPointContents(build->source->construction,
			&scene, witness->value, &point_contents).status != SG_HOST_LAW_OK ||
		(point_contents & SG_HOST_CONTENTS_SOLID) != 0U)
	{
		return 0;
	}
	memset(&pose, 0, sizeof(pose));
	if (SG_HostLawConstructionClassifyPose(build->source->construction, &scene,
			witness->value, cell->stance, &pose).status != SG_HOST_LAW_OK ||
		!pose.valid || !pose.supported || !pose.support_is_mover ||
		pose.support.model_index != entity->bsp_model ||
		pose.support.instance_id != instance.instance_id ||
		(pose.support.contents & SG_EXTERNAL_FORCE_CURRENT_MASK) !=
			((sg_host_collision_contents_t)build->source->collision_authority->
				world->brushes[brush_index].contents &
			 SG_EXTERNAL_FORCE_CURRENT_MASK))
	{
		return 0;
	}
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	if (cell->stance == SG_RUNE_STANCE_CROUCHING)
		request.state.pm_flags |= PMF_DUCKED;
	for (axis = 0U; axis < 3U; axis++)
		if (!Q8Coordinate(witness->value[axis], &request.state.origin[axis]))
		{
			return 0;
		}
	if (observation == SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)
	{
		request.command.forwardmove = SHRT_MAX;
		request.command.sidemove = SHRT_MAX;
	}
	else if (observation == SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)
	{
		if (!Q8Coordinate(build->phases->identity.physics.max_velocity - 1.0f,
				&request.state.velocity[0]))
			return 0;
		request.command.forwardmove = SHRT_MAX;
	}
	request.previous_state = request.state;
	memset(&result, 0, sizeof(result));
	if (SG_HostLawConstructionPmove(build->source->construction, &scene,
			&request, &result, &pmove_error).status != SG_HOST_LAW_OK ||
		pmove_error != SG_HOST_PMOVE_ERROR_NONE)
	{
		return 0;
	}
	memset(&fact, 0, sizeof(fact));
	fact.kind = SG_EXTERNAL_FORCE_CONVEYOR_CURRENT;
	fact.source_entity_ordinal = entity->source_entity_ordinal;
	fact.mechanism_entity_index = UINT32_MAX;
	fact.mechanism = SG_RUNE_MECHANISM_REF_NONE;
	fact.source_cell = cell->id;
	fact.destination_cell = cell->id;
	fact.source_region_id = region->id;
	fact.destination_region_id = region->id;
	fact.source_model_index = entity->bsp_model;
	fact.source_leaf_index = leaf_index;
	fact.source_brush_index = brush_index;
	fact.source_brush_side_index = side_index;
	fact.source_contents = pose.support.contents & SG_EXTERNAL_FORCE_CURRENT_MASK;
	fact.source_witness = *witness;
	memcpy(fact.source_model_origin.value, entity->origin.value,
		sizeof(fact.source_model_origin.value));
	memcpy(fact.source_model_angles.value, entity->angles.value,
		sizeof(fact.source_model_angles.value));
	for (axis = 0U; axis < 3U; axis++)
	{
		float input_velocity = (float)request.state.velocity[axis] * 0.125f;
		float delta = result.origin[axis] - witness->value[axis];

		fact.support_witness.value[axis] = pose.support.end[axis];
		fact.support_normal.value[axis] = pose.support.plane.normal[axis];
		fact.velocity.value[axis] = result.velocity[axis];
		fact.acceleration.value[axis] =
			(result.velocity[axis] - input_velocity) / seconds;
		if (axis == 0U)
			fact.displacement.x.min_value = fact.displacement.x.max_value = delta;
		else if (axis == 1U)
			fact.displacement.y.min_value = fact.displacement.y.max_value = delta;
		else
			fact.displacement.z.min_value = fact.displacement.z.max_value = delta;
	}
	fact.support_distance = pose.support.plane.distance;
	fact.portal = SG_RUNE_PORTAL_REF_NONE;
	fact.source_phase = binding->phase;
	fact.destination_phase = binding->phase;
	fact.gravity = result.gravity;
	fact.duration_ms = result.elapsed_ms;
	fact.physics_abi_id = build->phases->identity.physics_abi_id;
	fact.flags = SG_EXTERNAL_FORCE_HOST_PROVEN;
	if ((entity->flags & SG_BSP_ENTITY_USE_ACTIVATED) != 0U)
		fact.flags |= SG_EXTERNAL_FORCE_CONDITIONAL;
	fact.observation = observation;
	fact.input_state = request.state;
	fact.input_command = request.command;
	fact.input_grounded = pose.supported ? 1U : 0U;
	fact.input_water_level = (uint32_t)pose.water_level;
	fact.input_support_model_index = pose.support.model_index;
	fact.input_support_instance_id = pose.support.instance_id;
	fact.output_state = result.state;
	fact.output_grounded = result.grounded ? 1U : 0U;
	fact.output_water_level = (uint32_t)result.water_level;
	fact.output_support_model_index = result.support_model_index;
	fact.output_support_instance_id = result.support_instance_id;
	return AppendFact(build, &fact);
}

static int IssueAppendConveyorForces(sg_external_force_build_t *build)
{
	const sg_bsp_world_t *world = build->source->collision_authority->world;
	uint32_t entity_index;

	for (entity_index = 0U; entity_index < build->entities.entity_count;
		entity_index++)
	{
		const sg_bsp_entity_semantic_t *entity =
			&build->entities.entities[entity_index];
		sg_external_force_brush_refs_t refs;
		uint32_t region_index, brush_offset;

		if (entity->physics_kind != SG_BSP_ENTITY_PHYSICS_CONVEYOR)
			continue;
		if ((entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
			!IssueCollectModelBrushes(world, entity->bsp_model, &refs))
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
			build->record = entity_index;
			return 0;
		}
		for (region_index = 0U;
			region_index < build->source->configuration_semantics->region_count;
			region_index++)
		{
			const sg_configuration_semantic_region_t *region =
				&build->source->configuration_semantics->regions[region_index];

			for (brush_offset = 0U; brush_offset < refs.count; brush_offset++)
			{
				const sg_bsp_brush_t *brush =
					&world->brushes[refs.values[brush_offset].brush];
				uint32_t side_offset;

				if (((uint32_t)brush->contents & SG_HOST_CONTENTS_SOLID) == 0U ||
					((uint32_t)brush->contents &
					 SG_EXTERNAL_FORCE_CURRENT_MASK) == 0U)
					continue;
				for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
				{
					const sg_bsp_brush_side_t *side = &world->brush_sides[
						brush->first_side + side_offset];
					const sg_bsp_plane_t *plane = &world->planes[side->plane];
					sg_host_collision_transform_t transform;
					float world_normal[3], world_distance;
					sg_rune_vec3_t witness;
					int support;
					uint32_t binding_index;

					memset(&transform, 0, sizeof(transform));
					memcpy(transform.origin, entity->origin.value,
						sizeof(transform.origin));
					memcpy(transform.angles, entity->angles.value,
						sizeof(transform.angles));
					IssueWorldPlane(plane->normal.value, plane->distance,
						&transform, world_normal, &world_distance);
					(void)world_distance;
					if (world_normal[2] < SG_EXTERNAL_FORCE_GROUND_NORMAL_Z)
						continue;
					support = IssueSupportWitness(build, entity, region,
						refs.values[brush_offset].brush, side_offset, &witness);
					if (support == 0)
					{
						build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
						build->record = refs.values[brush_offset].brush;
						free(refs.values);
						return 0;
					}
					if (support == 2)
						continue;
					for (binding_index = 0U;
						binding_index < build->phases->binding_count;
						binding_index++)
					{
						const sg_phase_catalog_binding_t *binding =
							&build->phases->bindings[binding_index];

						if (binding->semantic_region_id != region->id ||
							binding->configuration_cell != region->cell)
							continue;
						if (!IssueAppendConveyorFact(build, entity, region,
								binding, refs.values[brush_offset].leaf,
								refs.values[brush_offset].brush, side_offset,
								&witness, SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL) ||
							!IssueAppendConveyorFact(build, entity, region,
								binding, refs.values[brush_offset].leaf,
								refs.values[brush_offset].brush, side_offset,
								&witness,
								SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED) ||
							!IssueAppendConveyorFact(build, entity, region,
								binding, refs.values[brush_offset].leaf,
								refs.values[brush_offset].brush, side_offset,
								&witness,
								SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP))
						{
							if (build->error == SG_EXTERNAL_FORCE_AUDIT_OK)
								build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
							if (build->record == UINT32_MAX)
								build->record = brush->first_side + side_offset;
							free(refs.values);
							return 0;
						}
					}
				}
			}
		}
		free(refs.values);
	}
	return 1;
}

static int IssueAppendRegionForces(sg_external_force_build_t *build)
{
	/* This family records the engine physics/collision authority's map gravity.
	 * trigger_gravity writes per-actor state in g_trigger.c and is outside the
	 * immutable static input.  The fixture keeps one present to prove it never
	 * becomes a publication source. */
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	const sg_host_collision_contents_t mask =
		SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_90 |
		SG_HOST_CONTENTS_CURRENT_180 | SG_HOST_CONTENTS_CURRENT_270 |
		SG_HOST_CONTENTS_CURRENT_UP | SG_HOST_CONTENTS_CURRENT_DOWN;
	uint32_t region_index;

	for (region_index = 0U; region_index < semantics->region_count;
		region_index++)
	{
		const sg_configuration_semantic_region_t *region =
			&semantics->regions[region_index];
		sg_host_collision_contents_t currents = region->water_type & mask;
		uint32_t binding_index;

		for (binding_index = 0U;
			binding_index < build->phases->binding_count; binding_index++)
		{
			const sg_phase_catalog_binding_t *binding =
				&build->phases->bindings[binding_index];

			if (binding->semantic_region_id != region->id ||
				binding->configuration_cell != region->cell)
				continue;
			if (!IssueAppendLocalFact(build, SG_EXTERNAL_FORCE_GRAVITY,
					region_index, binding, 0U, UINT32_MAX,
					SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL) ||
				(currents != 0U && region->water_level != 0U &&
				 !IssueAppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					region_index, binding, currents, region->sample_leaves[0],
					SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL)) ||
				(currents != 0U && region->water_level != 0U &&
				 !IssueAppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					region_index, binding, currents, region->sample_leaves[0],
					SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)) ||
				(currents != 0U && region->water_level != 0U &&
				 !IssueAppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					region_index, binding, currents, region->sample_leaves[0],
					SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)))
				return 0;
		}
	}
	return 1;
}

/* Audit reconstruction intentionally walks accepted authorities in the
 * opposite direction from issuance: mechanisms locate phase evidence and
 * phase bindings locate regions. */
static int AuditAppendMechanismFacts(sg_external_force_build_t *build)
{
	const sg_configuration_space_t *configuration =
		build->source->configuration;
	uint32_t mechanism_index;

	for (mechanism_index = 0U;
		mechanism_index < build->mechanisms->fact_count; mechanism_index++)
	{
		const sg_mechanism_capability_fact_t *mechanism =
			&build->mechanisms->facts[mechanism_index];
		uint32_t phase_index;
		int include = mechanism->kind == SG_MECHANISM_CAPABILITY_PUSH ||
			(mechanism->flags & SG_MECHANISM_CAPABILITY_MOVER_RELATIVE) != 0U ||
			mechanism->kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
			mechanism->kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE ||
			mechanism->kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING;

		if (!include)
			continue;
		for (phase_index = 0U; phase_index < build->phases->transition_count;
			phase_index++)
		{
			const sg_phase_catalog_transition_evidence_t *evidence =
				&build->phases->transition_evidence[phase_index];
			const sg_rune_phase_transition_t *transition =
				&build->phases->transitions[phase_index];
			sg_external_force_fact_t fact;

			if (evidence->origin !=
					SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING ||
				evidence->source_record != mechanism_index)
				continue;
			if (evidence->source_cell >= configuration->cell_count ||
				evidence->destination_cell >= configuration->cell_count ||
				mechanism->source_region >=
					build->source->configuration_semantics->region_count ||
				mechanism->destination_region >=
					build->source->configuration_semantics->region_count ||
				!StableEqual(&evidence->mechanism.value,
					&mechanism->mechanism_id.value) ||
				evidence->provider_verifier_identity !=
					build->phases->mover_support_verifier_identity ||
				evidence->source_state_mask !=
					MechanismStateBit(mechanism->source_state) ||
				evidence->destination_state_mask !=
					MechanismStateBit(mechanism->destination_state) ||
				evidence->delay_ms != mechanism->delay_ms ||
				evidence->dwell_ms != mechanism->dwell_ms ||
				evidence->travel_ms != mechanism->travel_ms ||
				evidence->wait_ms != mechanism->wait_ms ||
				evidence->reset_ms != mechanism->reset_ms ||
				build->source->configuration_semantics->regions[
					mechanism->source_region].id != evidence->source_region_id ||
				build->source->configuration_semantics->regions[
					mechanism->destination_region].id !=
					evidence->destination_region_id ||
				!StableEqual(&transition->cell.value,
					&configuration->cells[evidence->source_cell].id.value) ||
				!StableEqual(&transition->destination_cell.value,
					&configuration->cells[evidence->destination_cell].id.value))
			{
				build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
				build->record = phase_index;
				return 0;
			}
			memset(&fact, 0, sizeof(fact));
			fact.source_entity_ordinal = UINT32_MAX;
			fact.mechanism_entity_index = mechanism->mechanism_entity;
			fact.kind = mechanism->kind == SG_MECHANISM_CAPABILITY_PUSH ?
				SG_EXTERNAL_FORCE_TRIGGER_PUSH :
				SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT;
			fact.mechanism.value = mechanism->mechanism_id.value;
			fact.source_cell = configuration->cells[evidence->source_cell].id;
			fact.destination_cell =
				configuration->cells[evidence->destination_cell].id;
			fact.source_region_id = evidence->source_region_id;
			fact.destination_region_id = evidence->destination_region_id;
			fact.source_model_index = UINT32_MAX;
			fact.source_leaf_index = UINT32_MAX;
			fact.source_brush_index = UINT32_MAX;
			fact.source_brush_side_index = UINT32_MAX;
			fact.portal = evidence->portal;
			fact.source_phase = transition->source_phase;
			fact.destination_phase = transition->destination_phase;
			fact.displacement = mechanism->parameters.displacement;
			fact.velocity = mechanism->observed_velocity;
			fact.acceleration.value[0] =
				mechanism->mechanism_direction.value[0] *
				mechanism->parameters.acceleration.max_value;
			fact.acceleration.value[1] =
				mechanism->mechanism_direction.value[1] *
				mechanism->parameters.acceleration.max_value;
			fact.acceleration.value[2] =
				mechanism->mechanism_direction.value[2] *
				mechanism->parameters.vertical_acceleration.max_value;
			fact.gravity = mechanism->parameters.gravity;
			fact.delay_ms = mechanism->delay_ms;
			fact.duration_ms = mechanism->travel_ms;
			fact.dwell_ms = mechanism->dwell_ms;
			fact.wait_ms = mechanism->wait_ms;
			fact.reset_ms = mechanism->reset_ms;
			fact.physics_abi_id = build->phases->identity.physics_abi_id;
			fact.flags = SG_EXTERNAL_FORCE_HOST_PROVEN;
			if ((mechanism->flags & SG_MECHANISM_CAPABILITY_ONE_SHOT) != 0U)
				fact.flags |= SG_EXTERNAL_FORCE_ONE_SHOT;
			if ((mechanism->flags & SG_MECHANISM_CAPABILITY_CONDITIONAL) != 0U)
				fact.flags |= SG_EXTERNAL_FORCE_CONDITIONAL;
			if (fact.kind == SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT)
				fact.flags |= SG_EXTERNAL_FORCE_MOVER_RELATIVE;
			if (!PhaseBindingFor(build, fact.source_region_id,
					evidence->source_cell, &fact.source_phase) ||
				!PhaseBindingFor(build, fact.destination_region_id,
					evidence->destination_cell, &fact.destination_phase) ||
				!AppendFact(build, &fact))
				return 0;
		}
	}
	return 1;
}

static void AuditRotation(const float angles[3], float matrix[3][3])
{
	const float scale = 0.01745329251994329577f;
	float sin_yaw = sinf(angles[1] * scale);
	float cos_yaw = cosf(angles[1] * scale);
	float sin_pitch = sinf(angles[0] * scale);
	float cos_pitch = cosf(angles[0] * scale);
	float sin_roll = sinf(angles[2] * scale);
	float cos_roll = cosf(angles[2] * scale);

	matrix[0][0] = cos_pitch * cos_yaw;
	matrix[0][1] = cos_pitch * sin_yaw;
	matrix[0][2] = -sin_pitch;
	matrix[1][0] = sin_roll * sin_pitch * cos_yaw - cos_roll * sin_yaw;
	matrix[1][1] = sin_roll * sin_pitch * sin_yaw + cos_roll * cos_yaw;
	matrix[1][2] = sin_roll * cos_pitch;
	matrix[2][0] = cos_roll * sin_pitch * cos_yaw + sin_roll * sin_yaw;
	matrix[2][1] = cos_roll * sin_pitch * sin_yaw - sin_roll * cos_yaw;
	matrix[2][2] = cos_roll * cos_pitch;
}

static void AuditTransformPlane(const sg_bsp_plane_t *plane,
	float expanded_distance, const sg_bsp_entity_semantic_t *entity,
	float output_normal[3], float *output_distance)
{
	float matrix[3][3];
	uint32_t column;

	AuditRotation(entity->angles.value, matrix);
	for (column = 0U; column < 3U; column++)
		output_normal[column] = plane->normal.value[0] * matrix[0][column] +
			plane->normal.value[1] * matrix[1][column] +
			plane->normal.value[2] * matrix[2][column];
	*output_distance = expanded_distance +
		output_normal[0] * entity->origin.value[0] +
		output_normal[1] * entity->origin.value[1] +
		output_normal[2] * entity->origin.value[2];
}

/* Audit starts from each candidate brush and independently finds the minimum
 * reachable leaf that names it. */
static int AuditBrushReachable(const sg_bsp_world_t *world,
	uint32_t model_index, uint32_t sought_brush, uint32_t *leaf_out)
{
	int32_t *pending;
	uint8_t *seen;
	uint32_t pending_count = 0U;
	int result = 0;

	if (model_index >= world->model_count || sought_brush >= world->brush_count ||
		world->node_count == UINT32_MAX)
		return -1;
	pending = calloc((size_t)world->node_count + 1U, sizeof(*pending));
	seen = calloc(world->node_count ? world->node_count : 1U, 1U);
	if (!pending || !seen)
	{
		free(pending);
		free(seen);
		return -1;
	}
	*leaf_out = UINT32_MAX;
	pending[pending_count++] = world->models[model_index].headnode;
	while (pending_count != 0U)
	{
		int32_t item = pending[--pending_count];

		if (item >= 0)
		{
			const sg_bsp_node_t *node;

			if ((uint32_t)item >= world->node_count)
			{
				result = -1;
				break;
			}
			if (seen[(uint32_t)item])
				continue;
			seen[(uint32_t)item] = 1U;
			node = &world->nodes[(uint32_t)item];
			if (pending_count > world->node_count - 1U)
			{
				result = -1;
				break;
			}
			pending[pending_count++] = node->children[0];
			pending[pending_count++] = node->children[1];
		}
		else
		{
			uint32_t leaf = (uint32_t)(-1 - item);
			const sg_bsp_leaf_t *record;
			uint32_t offset;

			if (leaf >= world->leaf_count)
			{
				result = -1;
				break;
			}
			record = &world->leaves[leaf];
			if (record->first_leaf_brush > world->leaf_brush_count ||
				record->leaf_brush_count > world->leaf_brush_count -
					record->first_leaf_brush)
			{
				result = -1;
				break;
			}
			for (offset = 0U; offset < record->leaf_brush_count; offset++)
				if (world->leaf_brushes[record->first_leaf_brush + offset] ==
					sought_brush)
				{
					if (leaf < *leaf_out)
						*leaf_out = leaf;
					result = 1;
					break;
				}
		}
	}
	free(pending);
	free(seen);
	return result;
}

static float AuditHullPlaneDistance(const sg_bsp_plane_t *plane,
	const sg_rune_hull_profile_t *hull)
{
	float offset = 0.0f;
	uint32_t coordinate;

	for (coordinate = 0U; coordinate < 3U; coordinate++)
		offset += plane->normal.value[coordinate] *
			(plane->normal.value[coordinate] < 0.0f ?
			 hull->maxs.value[coordinate] : hull->mins.value[coordinate]);
	return plane->distance - offset;
}

static int AuditFindStandingOrigin(const sg_external_force_build_t *build,
	const sg_bsp_entity_semantic_t *entity,
	const sg_configuration_semantic_region_t *region, uint32_t brush_index,
	uint32_t top_side, sg_rune_vec3_t *origin_out)
{
	const sg_bsp_world_t *world = build->source->collision_authority->world;
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[region->cell];
	const sg_rune_hull_profile_t *hull = cell->stance == SG_RUNE_STANCE_CROUCHING ?
		&build->phases->identity.crouching_hull :
		&build->phases->identity.standing_hull;
	sg_configuration_lattice_halfspace_t *constraints;
	uint32_t constraint_count = 0U, side, face, coordinate;
	int32_t q8[3];
	sg_configuration_lattice_stats_t statistics;
	int status;

	if (brush->first_side > world->brush_side_count ||
		brush->side_count > world->brush_side_count - brush->first_side ||
		region->first_face > semantics->face_count ||
		region->face_count > semantics->face_count - region->first_face ||
		brush->side_count > UINT32_MAX - region->face_count - 2U)
		return 0;
	constraints = calloc((size_t)brush->side_count + region->face_count + 2U,
		sizeof(*constraints));
	if (!constraints)
		return 0;
	/* Audit deliberately starts from the selected support slab, then clips by
	 * the remaining brush faces and finally by the semantic region. */
	{
		const sg_bsp_plane_t *plane = &world->planes[world->brush_sides[
			brush->first_side + top_side].plane];
		float distance;

		AuditTransformPlane(plane, AuditHullPlaneDistance(plane, hull), entity,
			constraints[constraint_count].normal, &distance);
		constraints[constraint_count].distance = distance +
			SG_EXTERNAL_FORCE_GROUND_PROBE *
			constraints[constraint_count].normal[2];
		constraint_count++;
		for (coordinate = 0U; coordinate < 3U; coordinate++)
			constraints[constraint_count].normal[coordinate] =
				-constraints[0].normal[coordinate];
		constraints[constraint_count].distance = -distance;
		constraints[constraint_count].open = 1;
		constraint_count++;
	}
	for (side = 0U; side < brush->side_count; side++)
	{
		const sg_bsp_plane_t *plane;
		float distance;

		if (side == top_side)
			continue;
		plane = &world->planes[world->brush_sides[
			brush->first_side + side].plane];
		AuditTransformPlane(plane, AuditHullPlaneDistance(plane, hull), entity,
			constraints[constraint_count].normal, &distance);
		constraints[constraint_count].distance = distance;
		constraints[constraint_count].open = 1;
		constraint_count++;
	}
	for (face = 0U; face < region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *source =
			&semantics->faces[region->first_face + face];

		memcpy(constraints[constraint_count].normal, source->normal,
			sizeof(source->normal));
		constraints[constraint_count].distance = source->distance;
		constraints[constraint_count].open = source->open != 0U;
		constraint_count++;
	}
	memset(&statistics, 0, sizeof(statistics));
	status = SG_ConfigurationLatticeFind(constraints, constraint_count, NULL,
		q8, &statistics);
	free(constraints);
	if (status != 1)
		return status == 0 ? 2 : 0;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
		origin_out->value[coordinate] = (float)q8[coordinate] * 0.125f;
	return 1;
}

static int AuditRecordConveyorObservation(sg_external_force_build_t *build,
	const sg_bsp_entity_semantic_t *entity,
	const sg_configuration_semantic_region_t *region,
	const sg_phase_catalog_binding_t *binding, uint32_t leaf,
	uint32_t brush, uint32_t side, const sg_rune_vec3_t *origin,
	sg_external_force_observation_t observation)
{
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[binding->configuration_cell];
	const sg_bsp_brush_t *source_brush =
		&build->source->collision_authority->world->brushes[brush];
	sg_host_collision_instance_t mover;
	sg_host_collision_scene_t collision_scene;
	sg_host_collision_pose_t classification;
	sg_host_collision_contents_t at_origin = 0U;
	sg_host_pmove_request_t input;
	sg_host_pmove_result_t output;
	sg_host_pmove_error_t movement_error = SG_HOST_PMOVE_ERROR_NONE;
	sg_external_force_fact_t expected;
	uint32_t coordinate;
	float frame_seconds =
		(float)build->phases->identity.physics.frame_ms / 1000.0f;

	memset(&mover, 0, sizeof(mover));
	mover.instance_id = UINT64_C(1) + entity->source_entity_ordinal;
	mover.model_index = entity->bsp_model;
	memcpy(mover.transform.origin, entity->origin.value,
		sizeof(mover.transform.origin));
	memcpy(mover.transform.angles, entity->angles.value,
		sizeof(mover.transform.angles));
	collision_scene.instances = &mover;
	collision_scene.instance_count = 1U;
	memset(&classification, 0, sizeof(classification));
	if (SG_HostLawConstructionClassifyPose(build->source->construction,
			&collision_scene, origin->value, cell->stance,
			&classification).status != SG_HOST_LAW_OK ||
		SG_HostLawConstructionPointContents(build->source->construction,
			&collision_scene, origin->value, &at_origin).status != SG_HOST_LAW_OK ||
		(at_origin & SG_HOST_CONTENTS_SOLID) != 0U ||
		!classification.valid || !classification.supported ||
		!classification.support_is_mover ||
		classification.support.model_index != entity->bsp_model ||
		classification.support.instance_id != mover.instance_id ||
		(classification.support.contents & SG_EXTERNAL_FORCE_CURRENT_MASK) !=
			((uint32_t)source_brush->contents &
			 SG_EXTERNAL_FORCE_CURRENT_MASK))
		return 0;
	memset(&input, 0, sizeof(input));
	input.state.pm_type = PM_NORMAL;
	if (cell->stance == SG_RUNE_STANCE_CROUCHING)
		input.state.pm_flags = PMF_DUCKED;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
		if (!Q8Coordinate(origin->value[coordinate],
				&input.state.origin[coordinate]))
			return 0;
	if (observation == SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)
	{
		input.command.forwardmove = SHRT_MAX;
		input.command.sidemove = SHRT_MAX;
	}
	if (observation == SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)
	{
		if (!Q8Coordinate(build->phases->identity.physics.max_velocity - 1.0f,
				&input.state.velocity[0]))
			return 0;
		input.command.forwardmove = SHRT_MAX;
	}
	input.previous_state = input.state;
	memset(&output, 0, sizeof(output));
	if (SG_HostLawConstructionPmove(build->source->construction,
			&collision_scene, &input, &output, &movement_error).status !=
			SG_HOST_LAW_OK || movement_error != SG_HOST_PMOVE_ERROR_NONE)
		return 0;
	memset(&expected, 0, sizeof(expected));
	expected.kind = SG_EXTERNAL_FORCE_CONVEYOR_CURRENT;
	expected.source_entity_ordinal = entity->source_entity_ordinal;
	expected.mechanism_entity_index = UINT32_MAX;
	expected.mechanism = SG_RUNE_MECHANISM_REF_NONE;
	expected.source_cell = cell->id;
	expected.destination_cell = cell->id;
	expected.source_region_id = region->id;
	expected.destination_region_id = region->id;
	expected.source_model_index = entity->bsp_model;
	expected.source_leaf_index = leaf;
	expected.source_brush_index = brush;
	expected.source_brush_side_index = side;
	expected.source_contents = classification.support.contents &
		SG_EXTERNAL_FORCE_CURRENT_MASK;
	expected.source_witness = *origin;
	memcpy(expected.source_model_origin.value, entity->origin.value,
		sizeof(expected.source_model_origin.value));
	memcpy(expected.source_model_angles.value, entity->angles.value,
		sizeof(expected.source_model_angles.value));
	for (coordinate = 0U; coordinate < 3U; coordinate++)
	{
		float before = (float)input.state.velocity[coordinate] * 0.125f;
		float moved = output.origin[coordinate] - origin->value[coordinate];

		expected.support_witness.value[coordinate] =
			classification.support.end[coordinate];
		expected.support_normal.value[coordinate] =
			classification.support.plane.normal[coordinate];
		expected.velocity.value[coordinate] = output.velocity[coordinate];
		expected.acceleration.value[coordinate] =
			(output.velocity[coordinate] - before) / frame_seconds;
		if (coordinate == 0U)
			expected.displacement.x.min_value =
				expected.displacement.x.max_value = moved;
		else if (coordinate == 1U)
			expected.displacement.y.min_value =
				expected.displacement.y.max_value = moved;
		else
			expected.displacement.z.min_value =
				expected.displacement.z.max_value = moved;
	}
	expected.support_distance = classification.support.plane.distance;
	expected.portal = SG_RUNE_PORTAL_REF_NONE;
	expected.source_phase = binding->phase;
	expected.destination_phase = binding->phase;
	expected.gravity = output.gravity;
	expected.duration_ms = output.elapsed_ms;
	expected.physics_abi_id = build->phases->identity.physics_abi_id;
	expected.flags = SG_EXTERNAL_FORCE_HOST_PROVEN;
	if ((entity->flags & SG_BSP_ENTITY_USE_ACTIVATED) != 0U)
		expected.flags |= SG_EXTERNAL_FORCE_CONDITIONAL;
	expected.observation = observation;
	expected.input_state = input.state;
	expected.input_command = input.command;
	expected.input_grounded = classification.supported ? 1U : 0U;
	expected.input_water_level = (uint32_t)classification.water_level;
	expected.input_support_model_index = classification.support.model_index;
	expected.input_support_instance_id = classification.support.instance_id;
	expected.output_state = output.state;
	expected.output_grounded = output.grounded ? 1U : 0U;
	expected.output_water_level = (uint32_t)output.water_level;
	expected.output_support_model_index = output.support_model_index;
	expected.output_support_instance_id = output.support_instance_id;
	return AppendFact(build, &expected);
}

static int AuditAppendConveyorForces(sg_external_force_build_t *build)
{
	const sg_bsp_world_t *world = build->source->collision_authority->world;
	uint32_t binding_index;

	for (binding_index = 0U; binding_index < build->phases->binding_count;
		binding_index++)
	{
		const sg_phase_catalog_binding_t *binding =
			&build->phases->bindings[binding_index];
		const sg_configuration_semantic_region_t *region = NULL;
		uint32_t region_index, entity_index;

		for (region_index = 0U;
			region_index < build->source->configuration_semantics->region_count;
			region_index++)
			if (build->source->configuration_semantics->regions[region_index].id ==
					binding->semantic_region_id &&
				build->source->configuration_semantics->regions[region_index].cell ==
					binding->configuration_cell)
			{
				region = &build->source->configuration_semantics->regions[region_index];
				break;
			}
		if (!region)
			return 0;
		for (entity_index = build->entities.entity_count; entity_index-- > 0U; )
		{
			const sg_bsp_entity_semantic_t *entity =
				&build->entities.entities[entity_index];
			uint32_t brush;

			if (entity->physics_kind != SG_BSP_ENTITY_PHYSICS_CONVEYOR)
				continue;
			if ((entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
				entity->bsp_model >= world->model_count)
				return 0;
			for (brush = 0U; brush < world->brush_count; brush++)
			{
				const sg_bsp_brush_t *candidate = &world->brushes[brush];
				uint32_t leaf, side;
				int reachable;

				if (((uint32_t)candidate->contents & SG_HOST_CONTENTS_SOLID) == 0U ||
					((uint32_t)candidate->contents &
					 SG_EXTERNAL_FORCE_CURRENT_MASK) == 0U)
					continue;
				reachable = AuditBrushReachable(world, entity->bsp_model, brush,
					&leaf);
				if (reachable < 0)
					return 0;
				if (!reachable)
					continue;
				for (side = candidate->side_count; side-- > 0U; )
				{
					const sg_bsp_plane_t *plane = &world->planes[world->brush_sides[
						candidate->first_side + side].plane];
					float normal[3], distance;
					sg_rune_vec3_t standing_origin;
					int found;

					AuditTransformPlane(plane, plane->distance, entity, normal,
						&distance);
					(void)distance;
					if (normal[2] < SG_EXTERNAL_FORCE_GROUND_NORMAL_Z)
						continue;
					found = AuditFindStandingOrigin(build, entity, region, brush,
						side, &standing_origin);
					if (found == 0)
						return 0;
					if (found == 2)
						continue;
					if (!AuditRecordConveyorObservation(build, entity, region,
							binding, leaf, brush, side, &standing_origin,
							SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL) ||
						!AuditRecordConveyorObservation(build, entity, region,
							binding, leaf, brush, side, &standing_origin,
							SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED) ||
						!AuditRecordConveyorObservation(build, entity, region,
							binding, leaf, brush, side, &standing_origin,
							SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP))
						return 0;
				}
			}
		}
	}
	return 1;
}

static int AuditAppendLocalFact(sg_external_force_build_t *build,
	sg_external_force_kind_t kind,
	const sg_configuration_semantic_region_t *region,
	const sg_phase_catalog_binding_t *binding,
	sg_host_collision_contents_t contents, uint32_t leaf,
	sg_external_force_observation_t observation)
{
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[binding->configuration_cell];
	sg_external_force_fact_t expected;
	sg_host_collision_pose_t classified;
	sg_host_pmove_request_t input;
	sg_host_pmove_result_t output;
	sg_host_pmove_error_t error = SG_HOST_PMOVE_ERROR_NONE;
	uint32_t coordinate;
	float elapsed = (float)build->phases->identity.physics.frame_ms * 0.001f;

	memset(&expected, 0, sizeof(expected));
	memset(&classified, 0, sizeof(classified));
	if (SG_HostLawConstructionClassifyPose(build->source->construction, NULL,
			region->interior_witness.value, cell->stance, &classified).status !=
			SG_HOST_LAW_OK || !classified.valid)
		return 0;
	memset(&input, 0, sizeof(input));
	input.state.pm_type = PM_NORMAL;
	if (cell->stance == SG_RUNE_STANCE_CROUCHING)
		input.state.pm_flags = PMF_DUCKED;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
		if (!Q8Coordinate(region->interior_witness.value[coordinate],
				&input.state.origin[coordinate]))
			return 0;
	if (observation == SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)
	{
		input.command.forwardmove = SHRT_MAX;
		input.command.sidemove = SHRT_MAX;
	}
	else if (observation == SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)
	{
		if (!Q8Coordinate(build->phases->identity.physics.max_velocity - 1.0f,
				&input.state.velocity[0]))
			return 0;
		input.command.forwardmove = SHRT_MAX;
	}
	input.previous_state = input.state;
	memset(&output, 0, sizeof(output));
	if (SG_HostLawConstructionPmove(build->source->construction, NULL, &input,
			&output, &error).status != SG_HOST_LAW_OK ||
		error != SG_HOST_PMOVE_ERROR_NONE)
		return 0;
	expected.kind = kind;
	expected.source_entity_ordinal = UINT32_MAX;
	expected.mechanism_entity_index = UINT32_MAX;
	expected.mechanism = SG_RUNE_MECHANISM_REF_NONE;
	expected.source_cell = cell->id;
	expected.destination_cell = cell->id;
	expected.source_region_id = binding->semantic_region_id;
	expected.destination_region_id = binding->semantic_region_id;
	expected.source_model_index = 0U;
	expected.source_leaf_index = leaf;
	expected.source_brush_index = UINT32_MAX;
	expected.source_brush_side_index = UINT32_MAX;
	expected.source_contents = contents;
	expected.source_witness = region->interior_witness;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
	{
		float initial_velocity =
			(float)input.state.velocity[coordinate] * 0.125f;
		float displacement = output.origin[coordinate] -
			region->interior_witness.value[coordinate];

		expected.support_witness.value[coordinate] =
			classified.support.end[coordinate];
		expected.support_normal.value[coordinate] =
			classified.support.plane.normal[coordinate];
		expected.velocity.value[coordinate] = output.velocity[coordinate];
		expected.acceleration.value[coordinate] =
			(output.velocity[coordinate] - initial_velocity) / elapsed;
		if (coordinate == 0U)
			expected.displacement.x.min_value =
				expected.displacement.x.max_value = displacement;
		else if (coordinate == 1U)
			expected.displacement.y.min_value =
				expected.displacement.y.max_value = displacement;
		else
			expected.displacement.z.min_value =
				expected.displacement.z.max_value = displacement;
	}
	expected.support_distance = classified.support.plane.distance;
	expected.portal = SG_RUNE_PORTAL_REF_NONE;
	expected.source_phase = binding->phase;
	expected.destination_phase = binding->phase;
	expected.gravity = output.gravity;
	expected.duration_ms = output.elapsed_ms;
	expected.physics_abi_id = build->phases->identity.physics_abi_id;
	expected.flags = SG_EXTERNAL_FORCE_HOST_PROVEN;
	expected.observation = observation;
	expected.input_state = input.state;
	expected.input_command = input.command;
	expected.input_grounded = classified.supported ? 1U : 0U;
	expected.input_water_level = (uint32_t)classified.water_level;
	expected.input_support_model_index = classified.support.model_index;
	expected.input_support_instance_id = classified.support.instance_id;
	expected.output_state = output.state;
	expected.output_grounded = output.grounded ? 1U : 0U;
	expected.output_water_level = (uint32_t)output.water_level;
	expected.output_support_model_index = output.support_model_index;
	expected.output_support_instance_id = output.support_instance_id;
	return AppendFact(build, &expected);
}

static int AuditAppendRegionForces(sg_external_force_build_t *build)
{
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	uint32_t binding_index;

	for (binding_index = 0U; binding_index < build->phases->binding_count;
		binding_index++)
	{
		const sg_phase_catalog_binding_t *binding =
			&build->phases->bindings[binding_index];
		uint32_t region_index;
		int found = 0;

		for (region_index = 0U; region_index < semantics->region_count;
			region_index++)
		{
			const sg_configuration_semantic_region_t *region =
				&semantics->regions[region_index];
			sg_host_collision_contents_t currents;

			if (region->id != binding->semantic_region_id ||
				region->cell != binding->configuration_cell)
				continue;
			found = 1;
			currents = region->water_type & SG_EXTERNAL_FORCE_CURRENT_MASK;
			if (!AuditAppendLocalFact(build, SG_EXTERNAL_FORCE_GRAVITY,
					region, binding, 0U, UINT32_MAX,
					SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL) ||
				(currents != 0U && region->water_level != 0U &&
				 !AuditAppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					region, binding, currents, region->sample_leaves[0],
					SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL)) ||
				(currents != 0U && region->water_level != 0U &&
				 !AuditAppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					region, binding, currents, region->sample_leaves[0],
					SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)) ||
				(currents != 0U && region->water_level != 0U &&
				 !AuditAppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					region, binding, currents, region->sample_leaves[0],
					SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)))
				return 0;
			break;
		}
		if (!found)
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
			build->record = binding_index;
			return 0;
		}
	}
	return 1;
}

static int AuditReconstructFacts(const sg_external_force_source_t *source,
	sg_external_force_build_t *build)
{
	uint32_t index;

	memset(build, 0, sizeof(*build));
	build->source = source;
	build->error = SG_EXTERNAL_FORCE_AUDIT_OK;
	build->record = UINT32_MAX;
	if (!ReadSources(build) || !AuditAppendMechanismFacts(build) ||
		!AuditAppendRegionForces(build) || !AuditAppendConveyorForces(build))
		return 0;
	qsort(build->facts, build->fact_count, sizeof(*build->facts), FactCompare);
	for (index = 1U; index < build->fact_count; index++)
		if (FactEqual(&build->facts[index - 1U], &build->facts[index]))
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_DUPLICATE_FACT;
			build->record = index;
			return 0;
		}
	return 1;
}

static int IssueBuildFacts(const sg_external_force_source_t *source,
	sg_external_force_build_t *build)
{
	uint32_t index;

	memset(build, 0, sizeof(*build));
	build->source = source;
	build->error = SG_EXTERNAL_FORCE_AUDIT_OK;
	build->record = UINT32_MAX;
	if (!ReadSources(build) || !IssueAppendMechanismFacts(build) ||
		!IssueAppendRegionForces(build) || !IssueAppendConveyorForces(build))
		return 0;
	qsort(build->facts, build->fact_count, sizeof(*build->facts), FactCompare);
	for (index = 1U; index < build->fact_count; index++)
		if (FactEqual(&build->facts[index - 1U], &build->facts[index]))
		{
			build->error = SG_EXTERNAL_FORCE_AUDIT_DUPLICATE_FACT;
			build->record = index;
			return 0;
		}
	return 1;
}

static const sg_external_force_fact_t *PublicationFacts(
	const sg_external_force_publication_t *publication)
{
	return (const sg_external_force_fact_t *)(publication + 1);
}

static sg_external_force_completeness_t FactsCompleteness(
	const sg_external_force_fact_t *facts, uint32_t fact_count,
	sg_external_force_kind_t kind)
{
	uint32_t index;
	int found = 0;

	for (index = 0U; index < fact_count; index++)
		if (facts[index].kind == kind)
		{
			found = 1;
			if ((facts[index].flags & SG_EXTERNAL_FORCE_LAW_UNRESOLVED) != 0U)
				return SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED;
		}
	return found ? SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE :
		SG_EXTERNAL_FORCE_COMPLETENESS_PROVEN_EMPTY;
}

static sg_external_force_completeness_t OverallCompleteness(
	const sg_external_force_completeness_t by_kind[SG_EXTERNAL_FORCE_KIND_COUNT],
	uint32_t fact_count)
{
	uint32_t index;

	for (index = 0U; index < SG_EXTERNAL_FORCE_KIND_COUNT; index++)
		if (by_kind[index] == SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED)
			return SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED;
	return fact_count ? SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE :
		SG_EXTERNAL_FORCE_COMPLETENESS_PROVEN_EMPTY;
}

static uint64_t HashBytes(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static uint64_t PublicationContentIdentity(
	const sg_external_force_view_t *view,
	const sg_external_force_fact_t *facts)
{
	sg_external_force_view_t identity_view = *view;
	uint64_t hash = UINT64_C(14695981039346656037);

	identity_view.content_identity = 0U;
	hash = HashBytes(hash, &identity_view, sizeof(identity_view));
	hash = HashBytes(hash, facts,
		(size_t)view->fact_count * sizeof(*facts));
	return hash ? hash : UINT64_C(1);
}

static int PublicationHeaderValid(
	const sg_external_force_publication_t *publication)
{
	size_t fact_bytes;

	if (!publication || publication->magic != SG_EXTERNAL_FORCE_MAGIC ||
		publication->magic_inverse != ~SG_EXTERNAL_FORCE_MAGIC ||
		publication->self != publication ||
		publication->allocation_size_inverse != ~publication->allocation_size)
		return 0;
	fact_bytes = (size_t)publication->view.fact_count *
		sizeof(sg_external_force_fact_t);
	if (publication->view.fact_count != 0U &&
		fact_bytes / sizeof(sg_external_force_fact_t) !=
			(size_t)publication->view.fact_count)
		return 0;
	if (fact_bytes > SIZE_MAX - sizeof(*publication))
		return 0;
	return publication->magic == SG_EXTERNAL_FORCE_MAGIC &&
		publication->allocation_size == sizeof(*publication) + fact_bytes;
}

static int PublicationValid(const sg_external_force_publication_t *publication)
{
	uint64_t count = 0U;
	uint32_t observed[SG_EXTERNAL_FORCE_KIND_COUNT] = { 0U };
	uint32_t index;

	if (!PublicationHeaderValid(publication) ||
		publication->view.content_identity == 0U ||
		publication->view.content_identity != PublicationContentIdentity(
			&publication->view, PublicationFacts(publication)) ||
		publication->view.completeness != OverallCompleteness(
			publication->view.completeness_by_kind,
			publication->view.fact_count))
		return 0;
	for (index = 0U; index < publication->view.fact_count; index++)
	{
		const sg_external_force_fact_t *fact =
			&PublicationFacts(publication)[index];

		if (fact->kind < SG_EXTERNAL_FORCE_TRIGGER_PUSH ||
			fact->kind >= SG_EXTERNAL_FORCE_KIND_COUNT ||
			(fact->flags &
			 ~(sg_external_force_flags_t)SG_EXTERNAL_FORCE_FLAGS_KNOWN) != 0U ||
			fact->physics_abi_id != publication->view.identity.physics_abi_id)
			return 0;
		observed[fact->kind]++;
	}
	for (index = 0U; index < SG_EXTERNAL_FORCE_KIND_COUNT; index++)
	{
		count += publication->view.fact_count_by_kind[index];
		if (publication->view.fact_count_by_kind[index] != observed[index] ||
			publication->view.completeness_by_kind[index] !=
				FactsCompleteness(PublicationFacts(publication),
					publication->view.fact_count,
					(sg_external_force_kind_t)index))
			return 0;
	}
	return count == publication->view.fact_count;
}

static int ComparePublication(const sg_external_force_build_t *expected,
	const sg_external_force_publication_t *publication,
	sg_external_force_audit_result_t *audit)
{
	uint32_t expected_by_kind[SG_EXTERNAL_FORCE_KIND_COUNT] = { 0U };
	uint32_t index;

	audit->expected_facts = expected->fact_count;
	if (!PublicationValid(publication))
	{
		SetAudit(audit, SG_EXTERNAL_FORCE_AUDIT_STORAGE_DISAGREEMENT,
			UINT32_MAX);
		return 0;
	}
	for (index = 0U; index < expected->fact_count; index++)
		expected_by_kind[expected->facts[index].kind]++;
	if (!IdentityEqual(&publication->view.identity,
			&expected->phases->identity) ||
		!EntityBindingEqual(&publication->view.entity_binding,
			&expected->entities.binding) ||
		publication->view.mechanism_content_identity !=
			expected->mechanisms->content_identity ||
		publication->view.phase_verifier_identity !=
			expected->phases->mover_support_verifier_identity ||
		publication->view.pmove_behavior_fingerprint !=
			expected->host.pmove_behavior_fingerprint)
	{
		SetAudit(audit, SG_EXTERNAL_FORCE_AUDIT_METADATA_DISAGREEMENT,
			UINT32_MAX);
		return 0;
	}
	audit->observed_facts = publication->view.fact_count;
	for (index = 1U; index < publication->view.fact_count; index++)
	{
		const sg_external_force_fact_t *previous =
			&PublicationFacts(publication)[index - 1U];
		const sg_external_force_fact_t *current =
			&PublicationFacts(publication)[index];

		if (FactEqual(previous, current))
		{
			SetAudit(audit, SG_EXTERNAL_FORCE_AUDIT_DUPLICATE_FACT, index);
			return 0;
		}
		if (FactCompare(previous, current) > 0)
		{
			SetAudit(audit, SG_EXTERNAL_FORCE_AUDIT_NONDETERMINISTIC_ORDER,
				index);
			return 0;
		}
	}
	if (expected->fact_count != publication->view.fact_count)
	{
		SetAudit(audit,
			expected->fact_count > publication->view.fact_count ?
			SG_EXTERNAL_FORCE_AUDIT_OMITTED_FACT :
			SG_EXTERNAL_FORCE_AUDIT_INVENTED_FACT, UINT32_MAX);
		return 0;
	}
	for (index = 0U; index < SG_EXTERNAL_FORCE_KIND_COUNT; index++)
		if (publication->view.fact_count_by_kind[index] !=
				expected_by_kind[index] ||
			publication->view.completeness_by_kind[index] !=
				FactsCompleteness(expected->facts, expected->fact_count,
					(sg_external_force_kind_t)index))
		{
			SetAudit(audit,
				SG_EXTERNAL_FORCE_AUDIT_COMPLETENESS_DISAGREEMENT, index);
			return 0;
		}
	if (publication->view.completeness != OverallCompleteness(
			publication->view.completeness_by_kind,
			publication->view.fact_count))
	{
		SetAudit(audit, SG_EXTERNAL_FORCE_AUDIT_COMPLETENESS_DISAGREEMENT,
			UINT32_MAX);
		return 0;
	}
	for (index = 0U; index < expected->fact_count; index++)
		if (!FactEqual(&expected->facts[index],
				&PublicationFacts(publication)[index]))
		{
			SetAudit(audit, SG_EXTERNAL_FORCE_AUDIT_FACT_DISAGREEMENT, index);
			return 0;
		}
	return 1;
}

int SG_ExternalForcePublicationAudit(
	const sg_external_force_source_t *source,
	const sg_external_force_publication_t *publication,
	sg_external_force_audit_result_t *audit_out)
{
	sg_external_force_audit_result_t audit;
	sg_external_force_build_t expected;
	uint32_t index;

	memset(&audit, 0, sizeof(audit));
	audit.completeness = SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED;
	SetAudit(&audit, SG_EXTERNAL_FORCE_AUDIT_INVALID_ARGUMENT, UINT32_MAX);
	if (!audit_out)
		return 0;
	*audit_out = audit;
	if (!source || !publication || !source->collision_authority ||
		!source->collision_authority->world || !source->engine_authority ||
		!source->construction ||
		!source->entity_semantics || !source->configuration ||
		!source->configuration_semantics || !source->mechanism_owner ||
		!source->mechanisms || !source->phase_owner || !source->phases)
		return 0;
	if (!AuditReconstructFacts(source, &expected))
	{
		SetAudit(&audit, expected.error, expected.record);
		free(expected.facts);
		*audit_out = audit;
		return 0;
	}
	for (index = 0U; index < expected.fact_count; index++)
		audit.facts_by_kind[expected.facts[index].kind]++;
	for (index = 0U; index < SG_EXTERNAL_FORCE_KIND_COUNT; index++)
		audit.completeness_by_kind[index] = FactsCompleteness(expected.facts,
			expected.fact_count, (sg_external_force_kind_t)index);
	if (!ComparePublication(&expected, publication, &audit))
	{
		free(expected.facts);
		*audit_out = audit;
		return 0;
	}
	audit.completeness = OverallCompleteness(audit.completeness_by_kind,
		expected.fact_count);
	SetAudit(&audit, SG_EXTERNAL_FORCE_AUDIT_OK, UINT32_MAX);
	free(expected.facts);
	*audit_out = audit;
	return 1;
}

int SG_ExternalForcePublicationIssue(
	const sg_external_force_source_t *source,
	sg_external_force_publication_t **publication_out,
	sg_external_force_audit_result_t *audit_out)
{
	sg_external_force_build_t build;
	sg_external_force_publication_t *publication;
	size_t allocation_size;
	uint32_t index;

	if (!publication_out || *publication_out || !audit_out)
		return 0;
	*publication_out = NULL;
	if (!source || !source->collision_authority ||
		!source->collision_authority->world || !source->engine_authority ||
		!source->construction ||
		!source->entity_semantics || !source->configuration ||
		!source->configuration_semantics || !source->mechanism_owner ||
		!source->mechanisms || !source->phase_owner || !source->phases)
	{
		memset(audit_out, 0, sizeof(*audit_out));
		audit_out->code = SG_EXTERNAL_FORCE_AUDIT_INVALID_ARGUMENT;
		audit_out->record = UINT32_MAX;
		return 0;
	}
	if (!IssueBuildFacts(source, &build))
	{
		memset(audit_out, 0, sizeof(*audit_out));
		audit_out->code = build.error;
		audit_out->record = build.record;
		free(build.facts);
		return 0;
	}
	if (build.fact_count != 0U && sizeof(*build.facts) >
		(SIZE_MAX - sizeof(*publication)) / (size_t)build.fact_count)
	{
		free(build.facts);
		memset(audit_out, 0, sizeof(*audit_out));
		audit_out->code = SG_EXTERNAL_FORCE_AUDIT_OVERFLOW;
		audit_out->record = UINT32_MAX;
		return 0;
	}
	allocation_size = sizeof(*publication) +
		(size_t)build.fact_count * sizeof(*build.facts);
	publication = calloc(1U, allocation_size);
	if (!publication)
	{
		free(build.facts);
		memset(audit_out, 0, sizeof(*audit_out));
		audit_out->code = SG_EXTERNAL_FORCE_AUDIT_OUT_OF_MEMORY;
		audit_out->record = UINT32_MAX;
		return 0;
	}
	publication->magic = SG_EXTERNAL_FORCE_MAGIC;
	publication->magic_inverse = ~SG_EXTERNAL_FORCE_MAGIC;
	publication->self = publication;
	publication->allocation_size = allocation_size;
	publication->allocation_size_inverse = ~allocation_size;
	publication->view.identity = build.phases->identity;
	publication->view.entity_binding = build.entities.binding;
	publication->view.mechanism_content_identity =
		build.mechanisms->content_identity;
	publication->view.phase_verifier_identity =
		build.phases->mover_support_verifier_identity;
	publication->view.pmove_behavior_fingerprint =
		build.host.pmove_behavior_fingerprint;
	publication->view.fact_count = build.fact_count;
	if (build.fact_count)
		memcpy((void *)(publication + 1), build.facts,
			(size_t)build.fact_count * sizeof(*build.facts));
	for (index = 0U; index < build.fact_count; index++)
		publication->view.fact_count_by_kind[build.facts[index].kind]++;
	for (index = 0U; index < SG_EXTERNAL_FORCE_KIND_COUNT; index++)
		publication->view.completeness_by_kind[index] = FactsCompleteness(
			build.facts, build.fact_count, (sg_external_force_kind_t)index);
	publication->view.completeness = OverallCompleteness(
		publication->view.completeness_by_kind, build.fact_count);
	publication->view.content_identity = PublicationContentIdentity(
		&publication->view, PublicationFacts(publication));
	free(build.facts);
	if (!SG_ExternalForcePublicationAudit(source, publication, audit_out))
	{
		SG_ExternalForcePublicationDestroy(publication);
		return 0;
	}
	*publication_out = publication;
	return 1;
}

int SG_ExternalForcePublicationRead(
	const sg_external_force_publication_t *publication,
	sg_external_force_view_t *view_out)
{
	if (!view_out)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	if (!PublicationValid(publication))
		return 0;
	*view_out = publication->view;
	return 1;
}

int SG_ExternalForcePublicationFact(
	const sg_external_force_publication_t *publication, uint32_t index,
	sg_external_force_fact_t *fact_out)
{
	if (!fact_out)
		return 0;
	memset(fact_out, 0, sizeof(*fact_out));
	if (!PublicationValid(publication) || index >= publication->view.fact_count)
		return 0;
	*fact_out = PublicationFacts(publication)[index];
	return 1;
}

void SG_ExternalForcePublicationDestroy(
	sg_external_force_publication_t *publication)
{
	if (!PublicationHeaderValid(publication))
		return;
	publication->magic = 0U;
	publication->magic_inverse = 0U;
	publication->self = NULL;
	free(publication);
}

const char *SG_ExternalForceAuditCodeString(
	sg_external_force_audit_code_t code)
{
	static const char *const names[] = {
		"ok", "invalid argument", "authority rejected", "identity mismatch",
		"configuration rejected", "entity rejected", "mechanism rejected",
		"phase rejected", "invalid fact", "omitted fact", "invented fact",
		"duplicate fact", "fact disagreement", "nondeterministic order",
		"storage disagreement", "metadata disagreement",
		"completeness disagreement", "overflow", "out of memory"
	};

	if (code < 0 || (size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}
