#include "sg_external_force_publication.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"
#include "sg_configuration_lattice.h"

#define SG_EXTERNAL_FORCE_MAGIC UINT64_C(0x455854464f524345)
#define SG_EXTERNAL_FORCE_CURRENT_MASK \
	(SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_90 | \
	 SG_HOST_CONTENTS_CURRENT_180 | SG_HOST_CONTENTS_CURRENT_270 | \
	 SG_HOST_CONTENTS_CURRENT_UP | SG_HOST_CONTENTS_CURRENT_DOWN)

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
		left->source_contents == right->source_contents &&
		VecEqual(&left->source_witness, &right->source_witness) &&
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
		left->flags == right->flags;
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
	if (!SG_BspCompletenessProve(source->collision_authority,
			source->configuration, &completeness) ||
		completeness.code != SG_BSP_COMPLETENESS_OK ||
		!SG_ConfigurationAudit(source->collision_authority,
			source->configuration, &configuration) ||
		configuration.code != SG_CONFIGURATION_AUDIT_OK ||
		!SG_ConfigurationSemanticsAudit(source->collision_authority,
			source->configuration, source->configuration_semantics,
			&semantics) ||
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

typedef struct sg_external_force_partition_s
{
	uint32_t leaf;
	sg_host_collision_contents_t contents;
	sg_rune_vec3_t witness;
} sg_external_force_partition_t;

typedef struct sg_external_force_partition_list_s
{
	sg_external_force_partition_t *values;
	uint32_t count;
	uint32_t capacity;
} sg_external_force_partition_list_t;

static void AngleAxis(const float angles[3], float axis[3][3])
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

static void WorldHalfspace(const float local_normal[3], float local_distance,
	const sg_host_collision_transform_t *transform,
	sg_configuration_lattice_halfspace_t *halfspace)
{
	float axis[3][3];
	uint32_t row;
	uint32_t column;

	AngleAxis(transform->angles, axis);
	for (column = 0U; column < 3U; column++)
	{
		halfspace->normal[column] = 0.0f;
		for (row = 0U; row < 3U; row++)
			halfspace->normal[column] +=
				local_normal[row] * axis[row][column];
	}
	halfspace->distance = local_distance;
	for (column = 0U; column < 3U; column++)
		halfspace->distance += halfspace->normal[column] *
			transform->origin[column];
	halfspace->open = 0;
}

static int AppendPartition(sg_external_force_partition_list_t *partitions,
	uint32_t leaf, sg_host_collision_contents_t contents,
	const int32_t point[3])
{
	sg_external_force_partition_t *replacement;
	uint32_t capacity;
	uint32_t axis;

	if (partitions->count == partitions->capacity)
	{
		capacity = partitions->capacity ? partitions->capacity * 2U : 4U;
		if (capacity < partitions->capacity)
			return 0;
		replacement = realloc(partitions->values,
			(size_t)capacity * sizeof(*replacement));
		if (!replacement)
			return 0;
		partitions->values = replacement;
		partitions->capacity = capacity;
	}
	partitions->values[partitions->count].leaf = leaf;
	partitions->values[partitions->count].contents = contents;
	for (axis = 0U; axis < 3U; axis++)
		partitions->values[partitions->count].witness.value[axis] =
			(float)point[axis] * 0.125f;
	partitions->count++;
	return 1;
}

static int TraverseCurrentPartitions(const sg_bsp_world_t *world,
	int32_t child, const sg_host_collision_transform_t *transform,
	sg_configuration_lattice_halfspace_t *halfspaces, uint32_t count,
	uint32_t capacity, sg_external_force_partition_list_t *partitions)
{
	if (child < 0)
	{
		uint32_t leaf = (uint32_t)(-1 - child);
		int32_t point[3];
		sg_configuration_lattice_stats_t stats;
		int feasible;

		if (leaf >= world->leaf_count ||
			(world->leaves[leaf].contents & SG_EXTERNAL_FORCE_CURRENT_MASK) == 0)
			return leaf < world->leaf_count;
		memset(&stats, 0, sizeof(stats));
		feasible = SG_ConfigurationLatticeFind(halfspaces, count, NULL,
			point, &stats);
		if (feasible < 0)
			return 0;
		return feasible == 0 || AppendPartition(partitions, leaf,
			world->leaves[leaf].contents & SG_EXTERNAL_FORCE_CURRENT_MASK,
			point);
	}
	if ((uint32_t)child >= world->node_count || count >= capacity)
		return 0;
	{
		const sg_bsp_node_t *node = &world->nodes[(uint32_t)child];
		const sg_bsp_plane_t *plane;
		sg_configuration_lattice_halfspace_t world_plane;
		uint32_t axis;

		if (node->plane >= world->plane_count)
			return 0;
		plane = &world->planes[node->plane];
		WorldHalfspace(plane->normal.value, plane->distance, transform,
			&world_plane);
		halfspaces[count] = world_plane;
		for (axis = 0U; axis < 3U; axis++)
			halfspaces[count].normal[axis] = -world_plane.normal[axis];
		halfspaces[count].distance = -world_plane.distance;
		halfspaces[count].open = 0;
		if (!TraverseCurrentPartitions(world, node->children[0], transform,
			halfspaces, count + 1U, capacity, partitions))
			return 0;
		halfspaces[count] = world_plane;
		halfspaces[count].open = 1;
		return TraverseCurrentPartitions(world, node->children[1], transform,
			halfspaces, count + 1U, capacity, partitions);
	}
}

static int FindCurrentPartitions(const sg_external_force_build_t *build,
	const sg_bsp_entity_semantic_t *entity, uint32_t region_index,
	sg_external_force_partition_list_t *partitions)
{
	const sg_bsp_world_t *world = build->source->collision_authority->world;
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	const sg_configuration_semantic_region_t *region =
		&semantics->regions[region_index];
	const sg_bsp_model_t *model;
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_host_collision_transform_t transform;
	uint32_t capacity;
	uint32_t count = 0U;
	uint32_t axis;
	int result;

	if (entity->bsp_model >= world->model_count ||
		region->first_face > semantics->face_count ||
		region->face_count > semantics->face_count - region->first_face ||
		region->face_count > UINT32_MAX - 6U ||
		world->node_count > UINT32_MAX - region->face_count - 6U)
		return 0;
	capacity = region->face_count + 6U + world->node_count;
	halfspaces = calloc(capacity ? capacity : 1U, sizeof(*halfspaces));
	if (!halfspaces)
		return 0;
	for (axis = 0U; axis < region->face_count; axis++)
	{
		const sg_configuration_semantic_face_t *face =
			&semantics->faces[region->first_face + axis];

		memcpy(halfspaces[count].normal, face->normal,
			sizeof(halfspaces[count].normal));
		halfspaces[count].distance = face->distance;
		halfspaces[count].open = 0;
		count++;
	}
	memset(&transform, 0, sizeof(transform));
	memcpy(transform.origin, entity->origin.value, sizeof(transform.origin));
	memcpy(transform.angles, entity->angles.value, sizeof(transform.angles));
	model = &world->models[entity->bsp_model];
	for (axis = 0U; axis < 3U; axis++)
	{
		float normal[3] = { 0.0f, 0.0f, 0.0f };

		normal[axis] = 1.0f;
		WorldHalfspace(normal, model->maxs.value[axis], &transform,
			&halfspaces[count++]);
		normal[axis] = -1.0f;
		WorldHalfspace(normal, -model->mins.value[axis], &transform,
			&halfspaces[count++]);
	}
	result = TraverseCurrentPartitions(world, model->headnode, &transform,
		halfspaces, count, capacity, partitions);
	free(halfspaces);
	if (result)
		for (axis = 0U; axis < partitions->count; axis++)
			if ((SG_HostCollisionPointContentsModel(
					build->source->collision_authority, entity->bsp_model,
					&transform, partitions->values[axis].witness.value) &
					SG_EXTERNAL_FORCE_CURRENT_MASK) !=
				partitions->values[axis].contents)
				return 0;
	return result;
}

static int AppendLocalFact(sg_external_force_build_t *build,
	sg_external_force_kind_t kind, uint32_t entity_ordinal,
	const sg_bsp_entity_semantic_t *entity, uint32_t region_index,
	const sg_phase_catalog_binding_t *binding,
	sg_host_collision_contents_t currents, uint32_t model_index,
	uint32_t leaf_index, const sg_rune_vec3_t *witness)
{
	const sg_configuration_semantic_region_t *region =
		&build->source->configuration_semantics->regions[region_index];
	const sg_configuration_cell_t *cell =
		&build->source->configuration->cells[region->cell];
	sg_external_force_fact_t fact;

	memset(&fact, 0, sizeof(fact));
	fact.kind = kind;
	fact.source_entity_ordinal = entity_ordinal;
	fact.mechanism_entity_index = UINT32_MAX;
	fact.mechanism = SG_RUNE_MECHANISM_REF_NONE;
	fact.source_cell = cell->id;
	fact.destination_cell = cell->id;
	fact.source_region_id = region->id;
	fact.destination_region_id = region->id;
	fact.source_model_index = model_index;
	fact.source_leaf_index = leaf_index;
	fact.source_contents = currents;
	fact.source_witness = witness ? *witness : region->interior_witness;
	if (entity)
	{
		memcpy(fact.source_model_origin.value, entity->origin.value,
			sizeof(fact.source_model_origin.value));
		memcpy(fact.source_model_angles.value, entity->angles.value,
			sizeof(fact.source_model_angles.value));
	}
	fact.portal = SG_RUNE_PORTAL_REF_NONE;
	fact.source_phase = binding->phase;
	fact.destination_phase = binding->phase;
	fact.duration_ms = build->phases->identity.physics.frame_ms;
	fact.physics_abi_id = build->phases->identity.physics_abi_id;
	/* The exact local law is a state-dependent Pmove observation.  Geometry
	 * establishes this obligation, but no value is published until the
	 * authenticated offline construction adapter supplies that observation. */
	fact.flags = SG_EXTERNAL_FORCE_LAW_UNRESOLVED;
	if (kind == SG_EXTERNAL_FORCE_CONVEYOR_CURRENT && entity &&
		(entity->flags & SG_BSP_ENTITY_USE_ACTIVATED) != 0U)
		fact.flags |= SG_EXTERNAL_FORCE_CONDITIONAL;
	return AppendFact(build, &fact);
}

static int IssueAppendRegionForces(sg_external_force_build_t *build)
{
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
			if (!AppendLocalFact(build, SG_EXTERNAL_FORCE_GRAVITY,
					UINT32_MAX, NULL, region_index, binding, 0U, 0U,
					UINT32_MAX, NULL) ||
				(currents != 0U && region->water_level != 0U &&
				 !AppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					UINT32_MAX, NULL, region_index, binding, currents, 0U,
					region->sample_leaves[0], NULL)))
				return 0;
		}
	}
	return 1;
}

static int IssueAppendEntityLocalForces(sg_external_force_build_t *build)
{
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	uint32_t entity_index;

	for (entity_index = 0U; entity_index < build->entities.entity_count;
		entity_index++)
	{
		const sg_bsp_entity_semantic_t *entity =
			&build->entities.entities[entity_index];
		uint32_t region_index;

		/* trigger_gravity mutates generic actor gravity, but this game's player
		 * Pmove authority admits only map gravity and the hook zero-gravity law.
		 * Actor state is outside this static publication. */
		if (entity->physics_kind != SG_BSP_ENTITY_PHYSICS_CONVEYOR)
			continue;
		for (region_index = 0U; region_index < semantics->region_count;
			region_index++)
		{
			const sg_configuration_semantic_region_t *region =
				&semantics->regions[region_index];
			uint32_t binding_index;
			sg_external_force_partition_list_t partitions;
			uint32_t partition_index;

			memset(&partitions, 0, sizeof(partitions));
			if (!FindCurrentPartitions(build, entity, region_index, &partitions))
			{
				free(partitions.values);
				build->error = SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT;
				build->record = entity_index;
				return 0;
			}
			for (binding_index = 0U;
				binding_index < build->phases->binding_count; binding_index++)
			{
				const sg_phase_catalog_binding_t *binding =
					&build->phases->bindings[binding_index];

				if (binding->semantic_region_id != region->id ||
					binding->configuration_cell != region->cell)
					continue;
				for (partition_index = 0U;
					partition_index < partitions.count; partition_index++)
					if (!AppendLocalFact(build,
						SG_EXTERNAL_FORCE_CONVEYOR_CURRENT,
						entity->source_entity_ordinal, entity, region_index,
						binding, partitions.values[partition_index].contents,
						entity->bsp_model,
						partitions.values[partition_index].leaf,
						&partitions.values[partition_index].witness))
					{
						free(partitions.values);
						return 0;
					}
			}
			free(partitions.values);
		}
	}
	return 1;
}

/* Audit reconstruction intentionally walks accepted authorities in the
 * opposite direction from issuance: mechanisms locate phase evidence,
 * bindings locate regions, and regions locate conveyor entities. */
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
			if (!AppendLocalFact(build, SG_EXTERNAL_FORCE_GRAVITY,
					UINT32_MAX, NULL, region_index, binding, 0U, 0U,
					UINT32_MAX, NULL) ||
				(currents != 0U && region->water_level != 0U &&
				 !AppendLocalFact(build, SG_EXTERNAL_FORCE_WATER_CURRENT,
					UINT32_MAX, NULL, region_index, binding, currents, 0U,
					region->sample_leaves[0], NULL)))
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

static int AuditAppendEntityLocalForces(sg_external_force_build_t *build)
{
	const sg_configuration_semantics_t *semantics =
		build->source->configuration_semantics;
	uint32_t region_index;

	for (region_index = 0U; region_index < semantics->region_count;
		region_index++)
	{
		const sg_configuration_semantic_region_t *region =
			&semantics->regions[region_index];
		uint32_t entity_index;

		for (entity_index = 0U; entity_index < build->entities.entity_count;
			entity_index++)
		{
			const sg_bsp_entity_semantic_t *entity =
				&build->entities.entities[entity_index];
			sg_external_force_partition_list_t partitions;
			uint32_t binding_index;

			if (entity->physics_kind != SG_BSP_ENTITY_PHYSICS_CONVEYOR)
				continue;
			memset(&partitions, 0, sizeof(partitions));
			if (!FindCurrentPartitions(build, entity, region_index, &partitions))
			{
				free(partitions.values);
				return 0;
			}
			for (binding_index = 0U;
				binding_index < build->phases->binding_count; binding_index++)
			{
				const sg_phase_catalog_binding_t *binding =
					&build->phases->bindings[binding_index];
				uint32_t partition_index;

				if (binding->semantic_region_id != region->id ||
					binding->configuration_cell != region->cell)
					continue;
				for (partition_index = 0U;
					partition_index < partitions.count; partition_index++)
					if (!AppendLocalFact(build,
						SG_EXTERNAL_FORCE_CONVEYOR_CURRENT,
						entity->source_entity_ordinal, entity, region_index,
						binding, partitions.values[partition_index].contents,
						entity->bsp_model,
						partitions.values[partition_index].leaf,
						&partitions.values[partition_index].witness))
					{
						free(partitions.values);
						return 0;
					}
			}
			free(partitions.values);
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
		!AuditAppendRegionForces(build) ||
		!AuditAppendEntityLocalForces(build))
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
		!IssueAppendRegionForces(build) ||
		!IssueAppendEntityLocalForces(build))
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
