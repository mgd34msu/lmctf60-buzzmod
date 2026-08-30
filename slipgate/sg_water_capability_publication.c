#include "sg_water_capability_publication.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "sg_authority_entropy.h"
#include "sg_configuration_lattice.h"

#define PUBLICATION_STATE UINT32_C(0x57435031)
#define PLANE_EPSILON 0.0001
#define BOUNDS_EPSILON 0.0001f
#define COMMAND_MAGNITUDE INT16_C(400)
#define HOST_CURRENT_MASK \
	(SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_90 | \
	 SG_HOST_CONTENTS_CURRENT_180 | SG_HOST_CONTENTS_CURRENT_270 | \
	 SG_HOST_CONTENTS_CURRENT_UP | SG_HOST_CONTENTS_CURRENT_DOWN)

struct sg_water_capability_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	const sg_water_capability_publication_t *self;
	sg_water_capability_publication_info_t info;
	sg_rune_phase_basis_t *phases;
	sg_water_capability_publication_binding_t *bindings;
	sg_rune_phase_transition_t *transitions;
	sg_phase_catalog_transition_evidence_t *transition_evidence;
	sg_water_capability_publication_fact_t *facts;
};

typedef struct sg_water_capability_publication_record_s
{
	sg_water_capability_publication_t *token;
	sg_water_capability_publication_t *payload;
	struct sg_water_capability_publication_record_s *next;
} sg_water_capability_publication_record_t;

struct sg_water_capability_publication_owner_s
{
	sg_water_capability_publication_record_t *live;
	uint32_t live_count;
	uintptr_t next_token;
};

typedef struct normalized_binding_s
{
	uint64_t semantic_region_id;
	uint32_t phase;
	sg_phase_mechanism_state_mask_t mechanism_state_mask;
} normalized_binding_t;

/* This internal view is filled only from accepted publications.  Keeping it
 * separate from the public issue input prevents the audit from accidentally
 * falling back to a caller's callback, authority, phase array, or identity. */
typedef struct normalized_source_s
{
	const sg_host_law_publication_t *host_laws;
	const sg_host_collision_authority_t *authority;
	sg_host_law_view_t host_law_view;
	uint64_t host_law_identity;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	const sg_rune_phase_transition_t *transitions;
	const sg_phase_catalog_transition_evidence_t *transition_evidence;
	uint32_t transition_count;
	normalized_binding_t *bindings;
	uint32_t binding_count;
} normalized_source_t;

typedef struct audit_s
{
	const sg_water_capability_issue_source_t *input;
	normalized_source_t normalized;
	const normalized_source_t *source;
	sg_water_capability_audit_result_t result;
	sg_water_capability_fact_t *facts;
	uint32_t fact_count;
	uint32_t fact_capacity;
	uint32_t *binding_offsets;
	uint32_t *cell_region_offsets;
} audit_t;

typedef struct face_ref_s
{
	uint32_t cell;
	uint32_t region;
	uint32_t face;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint8_t sample_index;
	uint8_t reversed;
} face_ref_t;

typedef struct sweep_event_s
{
	float coordinate;
	uint32_t reference;
	uint8_t starts;
} sweep_event_t;

typedef struct interval_node_s
{
	float low;
	float high;
	float subtree_high;
	uint32_t reference;
	uint32_t left;
	uint32_t right;
	uint32_t height;
} interval_node_t;

typedef struct boundary_key_s
{
	uint32_t first_region;
	uint32_t second_region;
	uint32_t portal;
} boundary_key_t;

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static int Finite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static void Copy3(float destination[3], const float source[3])
{
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
}

static float Dot3(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static void Cross3(const float left[3], const float right[3], float result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static double PlaneDistance(const float point[3], const float normal[3],
	float distance)
{
	double x = normal[0];
	double y = normal[1];
	double z = normal[2];
	double length = sqrt(x * x + y * y + z * z);

	return ((double)point[0] * x + (double)point[1] * y +
		(double)point[2] * z - (double)distance) / length;
}

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

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull || !Finite3(hull->mins.value) || !Finite3(hull->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int BoundsValid(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	if (!bounds || !Finite3(bounds->mins.value) ||
		!Finite3(bounds->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	const sg_rune_physics_parameters_t *physics;

	if (!identity || identity->bsp_content_id == 0U ||
		identity->bsp_content_id == UINT64_MAX ||
		identity->entity_semantics_id == 0U ||
		identity->entity_semantics_id == UINT64_MAX ||
		identity->physics_abi_id == 0U ||
		identity->physics_abi_id == UINT64_MAX ||
		identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX ||
		identity->schema_id == 0U || identity->schema_id == UINT64_MAX ||
		identity->producer_identity == 0U ||
		identity->producer_identity == UINT64_MAX ||
		!HullValid(&identity->standing_hull) ||
		!HullValid(&identity->crouching_hull))
		return 0;
	physics = &identity->physics;
	return isfinite(physics->gravity) && physics->gravity >= 0.0f &&
		isfinite(physics->ground_acceleration) &&
		physics->ground_acceleration >= 0.0f &&
		isfinite(physics->air_acceleration) &&
		physics->air_acceleration >= 0.0f &&
		isfinite(physics->water_acceleration) &&
		physics->water_acceleration >= 0.0f &&
		isfinite(physics->hook_acceleration) &&
		physics->hook_acceleration >= 0.0f &&
		isfinite(physics->external_acceleration) &&
		physics->external_acceleration >= 0.0f &&
		isfinite(physics->water_drag) && physics->water_drag >= 0.0f &&
		isfinite(physics->max_velocity) && physics->max_velocity > 0.0f &&
		physics->gravity <= (float)SHRT_MAX &&
		truncf(physics->gravity) == physics->gravity &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
		physics->substep_ms <= UCHAR_MAX &&
		physics->substep_ms <= physics->frame_ms &&
		physics->frame_ms % physics->substep_ms == 0U;
}

static void Fail(audit_t *audit, sg_water_capability_audit_code_t code,
	uint32_t source_record, uint32_t candidate_record)
{
	if (audit->result.code != SG_WATER_CAPABILITY_AUDIT_OK)
		return;
	audit->result.code = code;
	audit->result.source_record = source_record;
	audit->result.candidate_record = candidate_record;
}

static int CheckedAddU32(uint32_t *value, uint32_t amount)
{
	if (!value || amount > UINT32_MAX - *value)
		return 0;
	*value += amount;
	return 1;
}

static int CheckedMulU32(uint32_t left, uint32_t right, uint32_t *value_out)
{
	if (!value_out || (left != 0U && right > UINT32_MAX / left))
		return 0;
	*value_out = left * right;
	return 1;
}

static int CounterAdd(audit_t *audit, uint32_t *counter, uint32_t amount,
	uint32_t source_record)
{
	if (CheckedAddU32(counter, amount))
		return 1;
	Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, source_record, 0U);
	return 0;
}

#ifdef SG_WATER_CAPABILITY_PUBLICATION_TESTING
int SG_WaterCapabilityPublicationTestCounterAdd(uint32_t *value,
	uint32_t amount, sg_water_capability_audit_result_t *audit_out)
{
	audit_t audit;
	int success;

	if (!audit_out)
		return 0;
	memset(&audit, 0, sizeof(audit));
	audit.result.code = SG_WATER_CAPABILITY_AUDIT_OK;
	success = CounterAdd(&audit, value, amount, UINT32_MAX);
	*audit_out = audit.result;
	return success;
}
#endif

static uint32_t MechanismStateCount(
	sg_phase_mechanism_state_mask_t state_mask)
{
	uint32_t count = 0U;

	while (state_mask != 0U)
	{
		count += state_mask & 1U;
		state_mask >>= 1U;
	}
	return count;
}

static uint32_t BindingMultiplicity(const audit_t *audit, uint32_t binding)
{
	const normalized_binding_t *record = &audit->source->bindings[binding];

	return audit->source->phases[record->phase].reference_frame ==
		SG_RUNE_FRAME_MOVER_RELATIVE ?
		MechanismStateCount(record->mechanism_state_mask) : 1U;
}

static sg_rune_medium_t RegionMedium(
	const sg_configuration_semantic_region_t *region)
{
	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER)
		return SG_RUNE_MEDIUM_WATER;
	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_LAVA)
		return SG_RUNE_MEDIUM_LAVA;
	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SLIME)
		return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_DRY;
}

static int RegionValid(const audit_t *audit, uint32_t region_index)
{
	const sg_configuration_semantic_region_t *region =
		&audit->source->semantics->regions[region_index];
	sg_rune_medium_t medium = RegionMedium(region);
	uint32_t medium_flags = region->flags &
		(SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		 SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
		 SG_CONFIGURATION_SEMANTIC_REGION_SLIME);
	uint32_t face;

	if (region->cell >= audit->source->configuration->cell_count ||
		!BoundsValid(&region->bounds) ||
		!Finite3(region->interior_witness.value) || region->face_count < 4U ||
		region->first_face > audit->source->semantics->face_count ||
		region->face_count > audit->source->semantics->face_count -
			region->first_face ||
		(region_index != 0U &&
		 audit->source->semantics->regions[region_index - 1U].cell >
			region->cell) ||
		(region_index != 0U &&
		 audit->source->semantics->regions[region_index - 1U].id >=
			region->id))
		return 0;
	if ((medium == SG_RUNE_MEDIUM_DRY) != (region->water_level == 0U) ||
		region->water_level > 3U ||
		(medium_flags != 0U &&
		 (medium_flags & (medium_flags - 1U)) != 0U))
		return 0;
	if ((medium == SG_RUNE_MEDIUM_WATER) !=
		((region->water_type & SG_HOST_CONTENTS_WATER) != 0U) ||
		(medium == SG_RUNE_MEDIUM_LAVA) !=
		((region->water_type & SG_HOST_CONTENTS_LAVA) != 0U) ||
		(medium == SG_RUNE_MEDIUM_SLIME) !=
		((region->water_type & SG_HOST_CONTENTS_SLIME) != 0U))
		return 0;
	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *record =
			&audit->source->semantics->faces[face];

		if (!Finite3(record->normal) || !isfinite(record->distance) ||
			(record->normal[0] == 0.0f && record->normal[1] == 0.0f &&
			 record->normal[2] == 0.0f))
			return 0;
	}
	return 1;
}

static int RegionMatchesHost(const audit_t *audit, uint32_t region_index)
{
	const sg_configuration_semantic_region_t *region =
		&audit->source->semantics->regions[region_index];
	const sg_configuration_cell_t *cell =
		&audit->source->configuration->cells[region->cell];
	const sg_rune_hull_profile_t *hull =
		cell->stance == SG_RUNE_STANCE_CROUCHING ?
		&audit->source->authority->identity.crouching_hull :
		&audit->source->authority->identity.standing_hull;
	sg_host_collision_pose_t pose;
	int supported = (region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;

	return SG_HostCollisionClassifyPose(audit->source->authority, NULL,
		region->interior_witness.value, cell->stance, &pose) && pose.valid &&
		pose.stance == cell->stance && HullEqual(&pose.hull, hull) &&
		pose.gravity == audit->source->authority->identity.physics.gravity &&
		pose.physics_abi_id == audit->source->host_law_identity &&
		(pose.supported != 0) == supported &&
		pose.water_level == region->water_level &&
		pose.water_type == region->water_type;
}

static int PhaseMatchesRegion(const audit_t *audit,
	const sg_rune_phase_basis_t *phase,
	const sg_configuration_semantic_region_t *region,
	sg_phase_mechanism_state_mask_t mechanism_state_mask)
{
	const sg_configuration_cell_t *cell =
		&audit->source->configuration->cells[region->cell];
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_void_relation_t void_relation;

	if (region->water_level >= 2U)
	{
		motion = SG_RUNE_MOTION_SWIMMING;
		support = SG_RUNE_SUPPORT_NONE;
	}
	else if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED)
	{
		motion = SG_RUNE_MOTION_SUPPORTED;
		support = SG_RUNE_SUPPORT_SUPPORTED;
	}
	else
	{
		motion = SG_RUNE_MOTION_AIRBORNE;
		support = SG_RUNE_SUPPORT_NONE;
	}
	void_relation = region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT ?
		SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR;
	if (!SG_RuneModelPhaseValid(phase) ||
		phase->order.source_set_identity !=
			audit->source->authority->identity.source_set_identity ||
		phase->stance != cell->stance || phase->medium != RegionMedium(region) ||
		phase->void_relation != void_relation)
		return 0;
	if (phase->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE)
		return phase->motion == SG_RUNE_MOTION_SUPPORTED &&
			phase->support == SG_RUNE_SUPPORT_MOVER &&
			SG_RuneModelStableIdValid(&phase->mover.value) &&
			mechanism_state_mask != 0U &&
			(mechanism_state_mask &
				~(sg_phase_mechanism_state_mask_t)
					SG_PHASE_MECHANISM_STATE_KNOWN) == 0U;
	return mechanism_state_mask == 0U &&
		phase->reference_frame == SG_RUNE_FRAME_WORLD &&
		!SG_RuneModelStableIdValid(&phase->mover.value) &&
		phase->order.source_set_identity ==
			audit->source->authority->identity.source_set_identity &&
		phase->stance == cell->stance && phase->motion == motion &&
		phase->support == support && phase->medium == RegionMedium(region) &&
		phase->void_relation == void_relation;
}

static int PhaseIndexForReference(const sg_rune_phase_basis_t *phases,
	uint32_t phase_count, sg_rune_phase_ref_t reference,
	uint32_t *index_out)
{
	uint32_t index;

	if (!phases || !index_out || !SG_RuneModelStableIdValid(&reference.value))
		return 0;
	for (index = 0U; index < phase_count; index++)
		if (SG_RuneModelStableIdEqual(&phases[index].id.value,
			&reference.value))
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static int SemanticRegionForId(const sg_configuration_semantics_t *semantics,
	uint64_t id, uint32_t *region_out)
{
	uint32_t region;

	if (!semantics || !region_out)
		return 0;
	for (region = 0U; region < semantics->region_count; region++)
		if (semantics->regions[region].id == id)
		{
			*region_out = region;
			return 1;
		}
	return 0;
}

static int NormalizePhaseCatalog(audit_t *audit,
	const sg_phase_catalog_view_t *catalog)
{
	normalized_source_t *source = &audit->normalized;
	uint8_t *phase_bound = NULL;
	uint32_t binding;

	if (!catalog || catalog->completion != SG_PHASE_CATALOG_COMPLETE ||
		(catalog->transition_count == 0U ?
		 catalog->transition_completion != SG_PHASE_CATALOG_PROVEN_EMPTY :
		 catalog->transition_completion != SG_PHASE_CATALOG_COMPLETE) ||
		catalog->mover_support_verifier_identity == 0U ||
		catalog->mover_support_verifier_identity == UINT64_MAX ||
		catalog->phase_count == 0U || !catalog->phases ||
		catalog->binding_count == 0U || !catalog->bindings ||
		!AllocationFits(catalog->binding_count, sizeof(*source->bindings)) ||
		!AllocationFits(catalog->phase_count, sizeof(*phase_bound)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_PHASE_CATALOG, 0U, 0U);
		return 0;
	}
	source->bindings = calloc(catalog->binding_count, sizeof(*source->bindings));
	phase_bound = calloc(catalog->phase_count, sizeof(*phase_bound));
	if (!source->bindings || !phase_bound)
	{
		free(phase_bound);
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
		return 0;
	}
	source->phases = catalog->phases;
	source->phase_count = catalog->phase_count;
	source->transitions = catalog->transitions;
	source->transition_evidence = catalog->transition_evidence;
	source->transition_count = catalog->transition_count;
	source->binding_count = catalog->binding_count;
	for (binding = 0U; binding < catalog->binding_count; binding++)
	{
		const sg_phase_catalog_binding_t *record = &catalog->bindings[binding];
		uint32_t phase;
		uint32_t region;

		if (record->configuration_cell >= source->configuration->cell_count ||
			!PhaseIndexForReference(catalog->phases,
				catalog->phase_count, record->phase, &phase) ||
			!SemanticRegionForId(source->semantics,
				record->semantic_region_id, &region) ||
			 source->semantics->regions[region].cell !=
				record->configuration_cell)
		{
			free(phase_bound);
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNBOUND_PHASE, binding, 0U);
			return 0;
		}
		source->bindings[binding].semantic_region_id =
			record->semantic_region_id;
		source->bindings[binding].phase = phase;
		source->bindings[binding].mechanism_state_mask =
			record->mechanism_state_mask;
		phase_bound[phase] = 1U;
	}
	for (binding = 0U; binding < catalog->phase_count; binding++)
		if (!phase_bound[binding])
		{
			free(phase_bound);
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNBOUND_PHASE, binding, 0U);
			return 0;
		}
	free(phase_bound);
	return 1;
}

static int PreflightSource(audit_t *audit)
{
	const normalized_source_t *source = audit->source;
	const sg_configuration_space_t *configuration = source->configuration;
	int bsp_proved;
	int configuration_audited;
	int semantics_audited;

	/* Run every independent prerequisite before deciding whether to issue.  The
	 * individual results are then snapshotted with the publication, so a later
	 * reader can tell which exact proof was accepted. */
	bsp_proved = SG_BspCompletenessProve(source->authority, configuration,
		&audit->result.bsp_completeness);
	configuration_audited = SG_ConfigurationAudit(source->authority,
		configuration, &audit->result.configuration_audit);
	semantics_audited = SG_ConfigurationSemanticsAudit(source->authority,
		configuration, source->semantics, &audit->result.semantics_audit);
	if (!bsp_proved ||
		audit->result.bsp_completeness.code != SG_BSP_COMPLETENESS_OK ||
		audit->result.bsp_completeness.expected_cells !=
			configuration->cell_count ||
		audit->result.bsp_completeness.represented_cells !=
			configuration->cell_count ||
		audit->result.bsp_completeness.proved_cells !=
			configuration->cell_count ||
		audit->result.bsp_completeness.expected_portals !=
			configuration->portal_count ||
		audit->result.bsp_completeness.represented_portals !=
			configuration->portal_count ||
		audit->result.bsp_completeness.proved_portals !=
			configuration->portal_count ||
		audit->result.bsp_completeness.omitted_cells != 0U ||
		audit->result.bsp_completeness.invented_cells != 0U ||
		audit->result.bsp_completeness.omitted_portals != 0U ||
		audit->result.bsp_completeness.invented_portals != 0U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_BSP_COMPLETENESS,
			audit->result.bsp_completeness.record, 0U);
		return 0;
	}
	if (!configuration_audited ||
		audit->result.configuration_audit.code != SG_CONFIGURATION_AUDIT_OK ||
		audit->result.configuration_audit.proved_cells !=
			configuration->cell_count ||
		audit->result.configuration_audit.proved_portals !=
			configuration->portal_count ||
		audit->result.configuration_audit.omitted_cells != 0U ||
		audit->result.configuration_audit.omitted_portals != 0U ||
		audit->result.configuration_audit.invented_portals != 0U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_CONFIGURATION_AUDIT,
			audit->result.configuration_audit.record, 0U);
		return 0;
	}
	if (!semantics_audited ||
		audit->result.semantics_audit.code !=
			SG_CONFIGURATION_SEMANTICS_AUDIT_OK)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_SEMANTICS_AUDIT,
			audit->result.semantics_audit.record, 0U);
		return 0;
	}
	return 1;
}

static int SourceValid(audit_t *audit,
	const sg_water_capability_set_t *candidate)
{
	const sg_water_capability_issue_source_t *input = audit->input;
	normalized_source_t *source = &audit->normalized;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_phase_catalog_view_t *catalog = NULL;
	sg_host_law_result_t host_result;
	uint32_t region;

	if (!input || !candidate || !input->host_laws || !input->configuration ||
		!input->semantics || !input->phase_catalog_owner ||
		!input->phase_catalog)
		return 0;
	host_result = SG_HostLawPublicationRevalidateProduction(input->host_laws);
	if (host_result.status != SG_HOST_LAW_OK ||
		SG_HostLawPublicationRead(input->host_laws, &source->host_law_view).
			status != SG_HOST_LAW_OK ||
		SG_HostLawPublicationCollisionAuthority(input->host_laws,
			&source->authority).status != SG_HOST_LAW_OK || !source->authority ||
		!source->authority->world)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_LAW, 0U, 0U);
		return 0;
	}
	if (!SG_PhaseCatalogPublicationRead(input->phase_catalog_owner,
		input->phase_catalog, &catalog))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_PHASE_CATALOG, 0U, 0U);
		return 0;
	}
	source->host_laws = input->host_laws;
	source->host_law_identity = source->host_law_view.identity.physics_abi_id;
	source->configuration = input->configuration;
	source->semantics = input->semantics;
	configuration = source->configuration;
	semantics = source->semantics;
	if (!IdentityValid(&source->host_law_view.identity) ||
		source->host_law_identity == 0U ||
		!IdentityEqual(&source->authority->identity,
			&source->host_law_view.identity) ||
		!IdentityEqual(&source->authority->identity, &configuration->identity) ||
		!IdentityEqual(&source->authority->identity, &semantics->identity) ||
		!IdentityEqual(&source->authority->identity, &candidate->identity) ||
		!IdentityEqual(&source->authority->identity, &catalog->identity))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_SOURCE_IDENTITY, 0U, 0U);
		return 0;
	}
	if (configuration->cell_count == 0U || !configuration->cells ||
		(configuration->portal_count != 0U && !configuration->portals) ||
		(configuration->vertex_count != 0U && !configuration->vertices) ||
		semantics->region_count == 0U || !semantics->regions ||
		!semantics->faces ||
		(candidate->fact_count != 0U && !candidate->facts) ||
		candidate->fact_count > SG_RUNE_MODEL_MAX_KERNELS)
		return 0;
	if (!NormalizePhaseCatalog(audit, catalog))
		return 0;
	if (!PreflightSource(audit))
		return 0;
	for (region = 0U; region < semantics->region_count; region++)
		if (!RegionValid(audit, region) || !RegionMatchesHost(audit, region))
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
				region, 0U);
			return 0;
		}
	for (region = 0U; region < configuration->cell_count; region++)
		if (!SG_RuneModelStableIdValid(&configuration->cells[region].id.value))
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNSTABLE_REFERENCE,
				region, 0U);
			return 0;
		}
	for (region = 0U; region < configuration->portal_count; region++)
	{
		const sg_configuration_portal_t *portal = &configuration->portals[region];

		if (portal->from_cell >= configuration->cell_count ||
			portal->to_cell >= configuration->cell_count ||
			portal->from_cell == portal->to_cell || portal->vertex_count < 3U ||
			portal->first_vertex > configuration->vertex_count ||
			portal->vertex_count > configuration->vertex_count -
				portal->first_vertex || !Finite3(portal->plane.normal) ||
			!isfinite(portal->plane.distance) ||
			!SG_RuneModelStableIdValid(&portal->id.value) ||
			(portal->plane.normal[0] == 0.0f &&
			 portal->plane.normal[1] == 0.0f &&
			 portal->plane.normal[2] == 0.0f))
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNSTABLE_REFERENCE,
				region, 0U);
			return 0;
		}
	}
	for (region = 0U; region < configuration->vertex_count; region++)
		if (!Finite3(configuration->vertices[region].value)) return 0;
	if (SG_HostLawPublicationRevalidateProduction(input->host_laws).status !=
		SG_HOST_LAW_OK)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_LAW, 0U, 0U);
		return 0;
	}
	return 1;
}

static int PrepareIndexes(audit_t *audit)
{
	const normalized_source_t *source = audit->source;
	uint32_t region;
	uint32_t binding = 0U;
	uint32_t cell;
	uint32_t next_region = 0U;

	if (source->semantics->region_count == UINT32_MAX ||
		source->configuration->cell_count == UINT32_MAX ||
		!AllocationFits((size_t)source->semantics->region_count + 1U,
			sizeof(*audit->binding_offsets)) ||
		!AllocationFits((size_t)source->configuration->cell_count + 1U,
			sizeof(*audit->cell_region_offsets)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, 0U, 0U);
		return 0;
	}
	audit->binding_offsets = calloc(
		(size_t)source->semantics->region_count + 1U,
		sizeof(*audit->binding_offsets));
	audit->cell_region_offsets = calloc(
		(size_t)source->configuration->cell_count + 1U,
		sizeof(*audit->cell_region_offsets));
	if (!audit->binding_offsets || !audit->cell_region_offsets)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
		return 0;
	}
	for (region = 0U; region < source->semantics->region_count; region++)
	{
		uint32_t first = binding;

		audit->binding_offsets[region] = binding;
		while (binding < source->binding_count &&
			source->bindings[binding].semantic_region_id ==
				source->semantics->regions[region].id)
		{
			const normalized_binding_t *record =
				&source->bindings[binding];

			if (record->phase >= source->phase_count ||
				!PhaseMatchesRegion(audit, &source->phases[record->phase],
					&source->semantics->regions[region],
					record->mechanism_state_mask))
			{
				Fail(audit, SG_WATER_CAPABILITY_AUDIT_INVALID_PHASE,
					binding, 0U);
				return 0;
			}
			if (binding != first &&
				source->bindings[binding - 1U].phase >= record->phase)
			{
				Fail(audit,
					source->bindings[binding - 1U].phase == record->phase ?
					SG_WATER_CAPABILITY_AUDIT_DUPLICATE_BINDING :
					SG_WATER_CAPABILITY_AUDIT_INVALID_PHASE,
					binding, 0U);
				return 0;
			}
			binding++;
		}
		if (binding == first)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_OMITTED_BINDING,
				region, 0U);
			return 0;
		}
	}
	audit->binding_offsets[source->semantics->region_count] = binding;
	if (binding != source->binding_count)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_INVENTED_BINDING,
			binding, 0U);
		return 0;
	}
	for (cell = 0U; cell < source->configuration->cell_count; cell++)
	{
		audit->cell_region_offsets[cell] = next_region;
		while (next_region < source->semantics->region_count &&
			source->semantics->regions[next_region].cell == cell)
			next_region++;
		if (audit->cell_region_offsets[cell] == next_region)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_SOURCE_IDENTITY,
				cell, 0U);
			return 0;
		}
	}
	audit->cell_region_offsets[source->configuration->cell_count] = next_region;
	if (next_region != source->semantics->region_count)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_SOURCE_IDENTITY,
			next_region, 0U);
		return 0;
	}
	return 1;
}

static int GrowFacts(audit_t *audit)
{
	uint32_t required = audit->fact_count + 1U;
	uint32_t capacity;
	sg_water_capability_fact_t *facts;

	if (audit->fact_count == UINT32_MAX ||
		audit->fact_count >= SG_RUNE_MODEL_MAX_KERNELS)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW,
			audit->fact_count, 0U);
		return 0;
	}
	if (required <= audit->fact_capacity)
		return 1;
	capacity = audit->fact_capacity ? audit->fact_capacity : 64U;
	while (capacity < required)
	{
		if (capacity > UINT32_MAX / 2U)
		{
			capacity = UINT32_MAX;
			break;
		}
		capacity *= 2U;
	}
	if (capacity < required ||
		!AllocationFits((size_t)capacity, sizeof(*facts)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, required, 0U);
		return 0;
	}
	facts = realloc(audit->facts, (size_t)capacity * sizeof(*facts));
	if (!facts)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, required, 0U);
		return 0;
	}
	audit->facts = facts;
	audit->fact_capacity = capacity;
	return 1;
}

static int AppendFact(audit_t *audit, const sg_water_capability_fact_t *fact)
{
	if (!GrowFacts(audit))
		return 0;
	audit->facts[audit->fact_count++] = *fact;
	return 1;
}

static void AccountLattice(audit_t *audit,
	const sg_configuration_lattice_stats_t *stats)
{
	audit->result.lattice_solve_calls += stats->solve_calls;
	audit->result.lattice_constraints += stats->constraints;
	if (stats->maximum_binary_shift > audit->result.lattice_maximum_binary_shift)
		audit->result.lattice_maximum_binary_shift =
			stats->maximum_binary_shift;
}

static void DirectionVector(sg_water_direction_t direction, float result[3])
{
	memset(result, 0, 3U * sizeof(*result));
	if (direction == SG_WATER_DIRECTION_POSITIVE_X) result[0] = 1.0f;
	if (direction == SG_WATER_DIRECTION_NEGATIVE_X) result[0] = -1.0f;
	if (direction == SG_WATER_DIRECTION_POSITIVE_Y) result[1] = 1.0f;
	if (direction == SG_WATER_DIRECTION_NEGATIVE_Y) result[1] = -1.0f;
	if (direction == SG_WATER_DIRECTION_POSITIVE_Z) result[2] = 1.0f;
	if (direction == SG_WATER_DIRECTION_NEGATIVE_Z) result[2] = -1.0f;
}

static void CombinedCurrentVector(sg_rune_contents_mask_t currents,
	float result[3])
{
	memset(result, 0, 3U * sizeof(*result));
	if (currents & SG_RUNE_CONTENTS_CURRENT_0) result[0] += 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_90) result[1] += 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_180) result[0] -= 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_270) result[1] -= 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_UP) result[2] += 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_DOWN) result[2] -= 1.0f;
}

static int PointToPmove(const float point[3], short result[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float scaled = point[axis] * 8.0f;

		if (!isfinite(scaled) || scaled < (float)SHRT_MIN ||
			scaled > (float)SHRT_MAX || truncf(scaled) != scaled)
			return 0;
		result[axis] = (short)scaled;
	}
	return 1;
}

static int IntervalVelocityToPmove(const sg_rune_interval_t *interval,
	short *result)
{
	float lower;
	float upper;
	float selected;

	if (!interval || !result || !isfinite(interval->min_value) ||
		!isfinite(interval->max_value))
		return 0;
	lower = ceilf(interval->min_value * 8.0f);
	upper = floorf(interval->max_value * 8.0f);
	if (lower > upper || upper < (float)SHRT_MIN || lower > (float)SHRT_MAX)
		return 0;
	if (lower < (float)SHRT_MIN) lower = (float)SHRT_MIN;
	if (upper > (float)SHRT_MAX) upper = (float)SHRT_MAX;
	selected = lower > 0.0f ? lower : (upper < 0.0f ? upper : 0.0f);
	*result = (short)selected;
	return 1;
}

static int PointInRegion(const audit_t *audit, uint32_t region_index,
	const float point[3])
{
	const sg_configuration_semantic_region_t *region =
		&audit->source->semantics->regions[region_index];
	uint32_t face;

	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *record =
			&audit->source->semantics->faces[face];

		if (PlaneDistance(point, record->normal, record->distance) >
			PLANE_EPSILON)
			return 0;
	}
	return 1;
}

static int PhaseContainsVelocity(const sg_rune_phase_basis_t *phase,
	const float velocity[3])
{
	return velocity[0] >= phase->velocity.x.min_value &&
		velocity[0] <= phase->velocity.x.max_value &&
		velocity[1] >= phase->velocity.y.min_value &&
		velocity[1] <= phase->velocity.y.max_value &&
		velocity[2] >= phase->velocity.z.min_value &&
		velocity[2] <= phase->velocity.z.max_value;
}

static sg_rune_medium_t ResultMedium(const sg_host_pmove_result_t *result)
{
	if (result->water_level == 0) return SG_RUNE_MEDIUM_DRY;
	if (result->water_type & SG_HOST_CONTENTS_WATER)
		return SG_RUNE_MEDIUM_WATER;
	if (result->water_type & SG_HOST_CONTENTS_LAVA)
		return SG_RUNE_MEDIUM_LAVA;
	if (result->water_type & SG_HOST_CONTENTS_SLIME)
		return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_DRY;
}

static int ResultMatchesDestination(const audit_t *audit,
	uint32_t region_index, uint32_t phase_index,
	const sg_host_pmove_result_t *result)
{
	const sg_configuration_semantic_region_t *region =
		&audit->source->semantics->regions[region_index];
	const sg_rune_phase_basis_t *phase =
		audit->source->phases + phase_index;
	const sg_rune_hull_profile_t *hull;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	int region_supported;
	uint32_t axis;

	if (!PointInRegion(audit, region_index, result->origin))
		return 0;
	stance = result->state.pm_flags & PMF_DUCKED ?
		SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	hull = stance == SG_RUNE_STANCE_CROUCHING ?
		&audit->source->authority->identity.crouching_hull :
		&audit->source->authority->identity.standing_hull;
	for (axis = 0U; axis < 3U; axis++)
		if (result->mins[axis] != hull->mins.value[axis] ||
			result->maxs[axis] != hull->maxs.value[axis])
			return 0;
	region_supported = (region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;
	if ((result->grounded != 0) != region_supported ||
		region->water_level != (uint8_t)result->water_level ||
		region->water_type !=
			(sg_host_collision_contents_t)result->water_type ||
		result->support_model_index != 0U || result->support_instance_id != 0U)
		return 0;
	motion = result->water_level >= 2 ? SG_RUNE_MOTION_SWIMMING :
		(result->grounded ? SG_RUNE_MOTION_SUPPORTED :
			SG_RUNE_MOTION_AIRBORNE);
	support = motion == SG_RUNE_MOTION_SUPPORTED ?
		SG_RUNE_SUPPORT_SUPPORTED : SG_RUNE_SUPPORT_NONE;
	return phase->stance == stance && phase->motion == motion &&
		phase->support == support && phase->medium == ResultMedium(result) &&
		phase->void_relation == ((region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) ?
			SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR) &&
		phase->reference_frame == SG_RUNE_FRAME_WORLD &&
		PhaseContainsVelocity(phase, result->velocity);
}

static int ResolveDestination(audit_t *audit, uint32_t region,
	const sg_host_pmove_result_t *result, uint32_t *phase_out)
{
	uint32_t binding;
	uint32_t matches = 0U;
	uint32_t phase = 0U;

	for (binding = audit->binding_offsets[region];
		binding < audit->binding_offsets[region + 1U]; binding++)
	{
		uint32_t candidate = audit->source->bindings[binding].phase;

		if (!ResultMatchesDestination(audit, region, candidate, result))
			continue;
		phase = candidate;
		matches++;
	}
	if (matches == 0U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNRESOLVED_DESTINATION,
			region, 0U);
		return 0;
	}
	if (matches != 1U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_AMBIGUOUS_DESTINATION,
			region, 0U);
		return 0;
	}
	*phase_out = phase;
	return 1;
}

static void CommandForDirection(const float direction[3], usercmd_t *command)
{
	memset(command, 0, sizeof(*command));
	command->forwardmove =
		(short)(direction[0] * (float)COMMAND_MAGNITUDE);
	command->sidemove =
		(short)(-direction[1] * (float)COMMAND_MAGNITUDE);
	command->upmove = (short)(direction[2] * (float)COMMAND_MAGNITUDE);
}

static int Probe(audit_t *audit, uint32_t source_region,
	const float start[3], const float direction[3],
	sg_water_capability_fact_t *fact, sg_host_pmove_result_t *result_out)
{
	const sg_configuration_semantic_region_t *region =
		&audit->source->semantics->regions[source_region];
	const sg_configuration_cell_t *cell =
		audit->source->configuration->cells + region->cell;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;
	uint32_t axis;

	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	if (cell->stance == SG_RUNE_STANCE_CROUCHING)
		request.state.pm_flags |= PMF_DUCKED;
	if (!PointToPmove(start, request.state.origin) ||
		fact->source_phase >= audit->source->phase_count ||
		!IntervalVelocityToPmove(
			&audit->source->phases[fact->source_phase].velocity.x,
			&request.state.velocity[0]) ||
		!IntervalVelocityToPmove(
			&audit->source->phases[fact->source_phase].velocity.y,
			&request.state.velocity[1]) ||
		!IntervalVelocityToPmove(
			&audit->source->phases[fact->source_phase].velocity.z,
			&request.state.velocity[2]))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_INVALID_PHASE,
			fact->source_phase, 0U);
		return 0;
	}
	request.state.gravity =
		(short)audit->source->authority->identity.physics.gravity;
	request.previous_state = request.state;
	CommandForDirection(direction, &request.command);
	Copy3(fact->command_vector.value, direction);
	if (SG_HostLawPublicationPmove(audit->source->host_laws, NULL, &request,
		&result, &error).status != SG_HOST_LAW_OK ||
		result.physics_abi_id != audit->source->host_law_identity ||
		result.gravity != audit->source->authority->identity.physics.gravity ||
		result.elapsed_ms !=
			audit->source->authority->identity.physics.frame_ms ||
		result.state.pm_type != PM_NORMAL || !Finite3(result.origin) ||
		!Finite3(result.velocity) || !Finite3(result.mins) ||
		!Finite3(result.maxs) || result.water_level < 0 ||
		result.water_level > 3)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
			source_region, 0U);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		if (result.origin[axis] !=
				(float)result.state.origin[axis] * 0.125f ||
			result.velocity[axis] !=
				(float)result.state.velocity[axis] * 0.125f)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
				source_region, 0U);
			return 0;
		}
		fact->observed_displacement.value[axis] =
			result.origin[axis] - start[axis];
		fact->observed_velocity.value[axis] = result.velocity[axis];
		fact->source_velocity.value[axis] =
			(float)request.state.velocity[axis] * 0.125f;
	}
	fact->result_pm_flags = result.state.pm_flags;
	fact->result_support_model_index = result.support_model_index;
	fact->result_support_instance_id = result.support_instance_id;
	fact->result_water_type =
		(sg_host_collision_contents_t)result.water_type;
	fact->result_grounded = (uint8_t)(result.grounded != 0);
	fact->result_water_level = (uint8_t)result.water_level;
	fact->parameters.displacement.x.min_value =
		fact->observed_displacement.value[0];
	fact->parameters.displacement.x.max_value =
		fact->observed_displacement.value[0];
	fact->parameters.displacement.y.min_value =
		fact->observed_displacement.value[1];
	fact->parameters.displacement.y.max_value =
		fact->observed_displacement.value[1];
	fact->parameters.displacement.z.min_value =
		fact->observed_displacement.value[2];
	fact->parameters.displacement.z.max_value =
		fact->observed_displacement.value[2];
	fact->parameters.duration_ms.min_value = (float)result.elapsed_ms;
	fact->parameters.duration_ms.max_value = (float)result.elapsed_ms;
	fact->parameters.speed.min_value = 0.0f;
	fact->parameters.speed.max_value =
		audit->source->authority->identity.physics.max_velocity;
	fact->parameters.acceleration.min_value = 0.0f;
	switch (audit->source->phases[fact->source_phase].motion)
	{
	case SG_RUNE_MOTION_SUPPORTED:
		fact->parameters.acceleration.max_value =
			audit->source->authority->identity.physics.ground_acceleration;
		break;
	case SG_RUNE_MOTION_AIRBORNE:
		fact->parameters.acceleration.max_value =
			audit->source->authority->identity.physics.air_acceleration;
		break;
	case SG_RUNE_MOTION_SWIMMING:
		fact->parameters.acceleration.max_value =
			audit->source->authority->identity.physics.water_acceleration;
		break;
	case SG_RUNE_MOTION_COUNT:
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_INVALID_PHASE,
			fact->source_phase, 0U);
		return 0;
	}
	fact->parameters.vertical_acceleration = fact->parameters.acceleration;
	fact->parameters.gravity =
		audit->source->authority->identity.physics.gravity;
	fact->parameters.drag = fact->source_medium == SG_RUNE_MEDIUM_DRY ?
		0.0f : audit->source->authority->identity.physics.water_drag;
	fact->parameters.physics_abi_id = audit->source->host_law_identity;
	fact->parameters.fixed_latency_ms =
		audit->source->authority->identity.physics.frame_ms;
	if (fact->source_medium != fact->destination_medium ||
		fact->source_water_level != fact->destination_water_level)
		fact->flags |= SG_WATER_CAPABILITY_STRADDLES_FRAME_LAW;
	fact->flags |= SG_WATER_CAPABILITY_HOST_PROVEN;
	audit->result.host_pmove_frames++;
	if (result_out) *result_out = result;
	return 1;
}

static void FillRegionFact(const audit_t *audit, uint32_t region,
	uint32_t phase, sg_water_capability_kind_t kind,
	sg_water_direction_t direction, sg_water_capability_fact_t *fact)
{
	const sg_configuration_semantic_region_t *record =
		&audit->source->semantics->regions[region];

	memset(fact, 0, sizeof(*fact));
	fact->source_region = region;
	fact->destination_region = region;
	fact->source_phase = phase;
	fact->destination_phase = phase;
	fact->portal = SG_WATER_CAPABILITY_INDEX_NONE;
	fact->kind = kind;
	fact->direction = direction;
	fact->source_medium = RegionMedium(record);
	fact->destination_medium = fact->source_medium;
	fact->source_contents = SG_HostCollisionRuneContents(record->water_type);
	fact->destination_contents = fact->source_contents;
	fact->source_water_level = record->water_level;
	fact->destination_water_level = record->water_level;
	fact->source_witness = record->interior_witness;
	fact->boundary_witness = record->interior_witness;
	fact->destination_witness = record->interior_witness;
	DirectionVector(direction, fact->direction_vector.value);
	fact->flags = SG_WATER_CAPABILITY_DIRECTIONAL;
}

static int LocalObligation(audit_t *audit, uint32_t region,
	const float direction[3], sg_water_capability_fact_t *fact)
{
	sg_host_pmove_result_t result;
	uint32_t destination_phase;

	if (!CounterAdd(audit, &audit->result.obligation_count, 1U, region))
		return 0;
	if (!Probe(audit, region, fact->source_witness.value, direction, fact,
		&result))
		return 0;
	if (!PointInRegion(audit, region, result.origin))
	{
		if (!CounterAdd(audit, &audit->result.proved_empty_count, 1U, region))
			return 0;
		return 1;
	}
	if (!ResolveDestination(audit, region, &result, &destination_phase))
		return 0;
	fact->destination_phase = destination_phase;
	if (!AppendFact(audit, fact))
		return 0;
	audit->result.proved_fact_count++;
	return 1;
}

static int EnumerateLocalFacts(audit_t *audit)
{
	uint32_t region;

	for (region = 0U; region < audit->source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&audit->source->semantics->regions[region];
		sg_host_collision_pose_t pose;
		sg_host_collision_contents_t host_currents;
		sg_rune_contents_mask_t currents;
		uint32_t binding;

		if (record->water_level == 0U)
			continue;
		audit->result.wet_region_count++;
		if (!SG_HostCollisionClassifyPose(audit->source->authority, NULL,
			record->interior_witness.value,
			audit->source->configuration->cells[record->cell].stance, &pose) ||
			!pose.valid)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
				region, 0U);
			return 0;
		}
		host_currents = (pose.water_type | pose.support.contents) &
			HOST_CURRENT_MASK;
		currents = SG_HostCollisionRuneContents(host_currents) &
			SG_RUNE_CONTENTS_CURRENT_MASK;
		for (binding = audit->binding_offsets[region];
			binding < audit->binding_offsets[region + 1U]; binding++)
		{
			uint32_t phase = audit->source->bindings[binding].phase;
			uint32_t direction;
			sg_water_capability_fact_t fact;
			uint32_t mover_obligations = 0U;

			if (audit->source->phases[phase].reference_frame ==
				SG_RUNE_FRAME_MOVER_RELATIVE)
			{
				uint32_t state_obligations;

				if (record->water_level >= 2U)
					mover_obligations = 8U;
				if (currents != 0U && !CheckedAddU32(&mover_obligations, 1U))
				{
					Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, region, 0U);
					return 0;
				}
				if (!CheckedMulU32(mover_obligations,
						BindingMultiplicity(audit, binding), &state_obligations))
				{
					Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, region, 0U);
					return 0;
				}
				/* No accepted dynamic collision scene is present at this boundary.
				 * Account for every accepted mechanism state as explicitly empty
				 * instead of replaying a world-relative Pmove. */
				if (!CounterAdd(audit, &audit->result.obligation_count,
						state_obligations, region) ||
					!CounterAdd(audit, &audit->result.proved_empty_count,
						state_obligations, region))
					return 0;
				continue;
			}

			if (record->water_level >= 2U)
			{
				for (direction = SG_WATER_DIRECTION_POSITIVE_X;
					direction <= SG_WATER_DIRECTION_NEGATIVE_Z; direction++)
				{
					FillRegionFact(audit, region, phase,
						SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
						(sg_water_direction_t)direction, &fact);
					if (!LocalObligation(audit, region,
						fact.direction_vector.value, &fact))
						return 0;
				}
				FillRegionFact(audit, region, phase,
					SG_WATER_CAPABILITY_SINK,
					SG_WATER_DIRECTION_NEGATIVE_Z, &fact);
				memset(fact.direction_vector.value, 0,
					sizeof(fact.direction_vector.value));
				if (!LocalObligation(audit, region,
					fact.direction_vector.value, &fact))
					return 0;
				FillRegionFact(audit, region, phase,
					SG_WATER_CAPABILITY_SURFACE,
					SG_WATER_DIRECTION_POSITIVE_Z, &fact);
				if (!LocalObligation(audit, region,
					fact.direction_vector.value, &fact))
					return 0;
			}
			if (currents != 0U)
			{
				float command[3] = { 0.0f, 0.0f, 0.0f };

				FillRegionFact(audit, region, phase,
					SG_WATER_CAPABILITY_CURRENT,
					SG_WATER_DIRECTION_COMBINED, &fact);
				fact.current = currents;
				fact.flags |= SG_WATER_CAPABILITY_USES_CURRENT;
				CombinedCurrentVector(currents,
					fact.direction_vector.value);
				if (!LocalObligation(audit, region, command, &fact))
					return 0;
			}
		}
	}
	return 1;
}

static void CanonicalPlane(const float input[3], float input_distance,
	float normal[3], float *distance)
{
	uint32_t axis;
	uint32_t dominant = 0U;
	float scale;
	int flip;

	for (axis = 1U; axis < 3U; axis++)
		if (fabsf(input[axis]) > fabsf(input[dominant]))
			dominant = axis;
	scale = fabsf(input[dominant]);
	flip = input[dominant] < 0.0f;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = (flip ? -input[axis] : input[axis]) / scale;
	*distance = (flip ? -input_distance : input_distance) / scale;
}

static int PlanesCoplanar(const float left[3], float left_distance,
	const float right[3], float right_distance)
{
	float left_normal[3];
	float right_normal[3];
	float canonical_left;
	float canonical_right;

	CanonicalPlane(left, left_distance, left_normal, &canonical_left);
	CanonicalPlane(right, right_distance, right_normal, &canonical_right);
	return fabsf(left_normal[0] - right_normal[0]) <= PLANE_EPSILON &&
		fabsf(left_normal[1] - right_normal[1]) <= PLANE_EPSILON &&
		fabsf(left_normal[2] - right_normal[2]) <= PLANE_EPSILON &&
		fabsf(canonical_left - canonical_right) <= PLANE_EPSILON;
}

static int BoundsOverlap(const sg_rune_bounds_t *left,
	const sg_rune_bounds_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (fmaxf(left->mins.value[axis], right->mins.value[axis]) >
			fminf(left->maxs.value[axis], right->maxs.value[axis]) +
			BOUNDS_EPSILON)
			return 0;
	return 1;
}

static int SharedBoundaryWitness(audit_t *audit, uint32_t first_region,
	uint32_t first_face, uint32_t second_region, uint32_t second_face,
	uint32_t portal_index, float witness[3])
{
	const sg_configuration_semantic_region_t *regions[2] = {
		&audit->source->semantics->regions[first_region],
		&audit->source->semantics->regions[second_region]
	};
	uint32_t shared_faces[2] = { first_face, second_face };
	const sg_configuration_portal_t *portal =
		portal_index == SG_WATER_CAPABILITY_INDEX_NONE ? NULL :
		&audit->source->configuration->portals[portal_index];
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats;
	uint32_t constraint_count;
	uint32_t constraint = 0U;
	uint32_t side;
	int32_t point[3];
	int positive_margin;
	int solved;

	if (regions[0]->face_count > UINT32_MAX - regions[1]->face_count ||
		(portal && portal->vertex_count > UINT32_MAX -
			regions[0]->face_count - regions[1]->face_count))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, first_region, 0U);
		return -1;
	}
	constraint_count = regions[0]->face_count + regions[1]->face_count +
		(portal ? portal->vertex_count : 0U);
	if (!AllocationFits(constraint_count, sizeof(*halfspaces)) ||
		!AllocationFits(constraint_count, sizeof(*clearance)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, first_region, 0U);
		return -1;
	}
	halfspaces = calloc(constraint_count, sizeof(*halfspaces));
	clearance = calloc(constraint_count, sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY,
			first_region, 0U);
		return -1;
	}
	for (side = 0U; side < 2U; side++)
	{
		uint32_t face;

		for (face = regions[side]->first_face;
			face < regions[side]->first_face + regions[side]->face_count;
			face++)
		{
			const sg_configuration_semantic_face_t *record =
				&audit->source->semantics->faces[face];

			Copy3(halfspaces[constraint].normal, record->normal);
			halfspaces[constraint].distance = record->distance;
			clearance[constraint] = (uint8_t)(face != shared_faces[side]);
			constraint++;
		}
	}
	if (portal)
	{
		float center[3] = { 0.0f, 0.0f, 0.0f };
		uint32_t vertex;

		for (vertex = 0U; vertex < portal->vertex_count; vertex++)
			for (side = 0U; side < 3U; side++)
				center[side] += audit->source->configuration->vertices[
					portal->first_vertex + vertex].value[side];
		for (side = 0U; side < 3U; side++)
			center[side] /= (float)portal->vertex_count;
		for (vertex = 0U; vertex < portal->vertex_count; vertex++)
		{
			const float *a = audit->source->configuration->vertices[
				portal->first_vertex + vertex].value;
			const float *b = audit->source->configuration->vertices[
				portal->first_vertex + (vertex + 1U) %
					portal->vertex_count].value;
			float edge[3];
			float normal[3];
			float distance;

			for (side = 0U; side < 3U; side++) edge[side] = b[side] - a[side];
			Cross3(edge, portal->plane.normal, normal);
			distance = Dot3(a, normal);
			if (Dot3(center, normal) > distance)
			{
				for (side = 0U; side < 3U; side++) normal[side] = -normal[side];
				distance = -distance;
			}
			Copy3(halfspaces[constraint].normal, normal);
			halfspaces[constraint].distance = distance;
			clearance[constraint] = 1U;
			constraint++;
		}
	}
	memset(&stats, 0, sizeof(stats));
	solved = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count,
		audit->source->semantics->faces[first_face].normal,
		point, &positive_margin, &stats);
	free(halfspaces);
	free(clearance);
	AccountLattice(audit, &stats);
	if (solved < 0)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_DOMAIN_COVERAGE,
			first_region, 0U);
		return -1;
	}
	if (!solved || !positive_margin) return 0;
	for (side = 0U; side < 3U; side++)
		witness[side] = (float)point[side] * 0.125f;
	return 1;
}

static int RegionSideWitness(audit_t *audit, uint32_t region_index,
	uint32_t boundary_face, const float boundary_witness[3], float witness[3])
{
	const sg_configuration_semantic_region_t *region =
		&audit->source->semantics->regions[region_index];
	const sg_configuration_semantic_face_t *boundary =
		audit->source->semantics->faces + boundary_face;
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_configuration_lattice_stats_t stats;
	int32_t point[3];
	uint32_t local;
	uint32_t dominant = 0U;
	int solved;
	int direct = 1;

	for (local = 1U; local < 3U; local++)
		if (fabsf(boundary->normal[local]) >
			fabsf(boundary->normal[dominant])) dominant = local;
	Copy3(witness, boundary_witness);
	witness[dominant] += boundary->normal[dominant] > 0.0f ? -0.125f : 0.125f;
	for (local = 0U; local < region->face_count; local++)
	{
		uint32_t face = region->first_face + local;
		const sg_configuration_semantic_face_t *record =
			&audit->source->semantics->faces[face];

		if (PlaneDistance(witness, record->normal, record->distance) > 0.0 ||
			(face == boundary_face && PlaneDistance(witness, record->normal,
				record->distance) >= 0.0)) direct = 0;
	}
	if (direct) return 1;
	if (region->face_count > UINT32_MAX - 6U ||
		!AllocationFits((size_t)region->face_count + 6U,
			sizeof(*halfspaces)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, region_index, 0U);
		return -1;
	}
	halfspaces = calloc(region->face_count + 6U, sizeof(*halfspaces));
	if (!halfspaces)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY,
			region_index, 0U);
		return -1;
	}
	for (local = 0U; local < region->face_count; local++)
	{
		uint32_t face = region->first_face + local;
		const sg_configuration_semantic_face_t *record =
			&audit->source->semantics->faces[face];

		Copy3(halfspaces[local].normal, record->normal);
		halfspaces[local].distance = record->distance;
		halfspaces[local].open = face == boundary_face;
	}
	for (local = 0U; local < 3U; local++)
	{
		uint32_t upper = region->face_count + local * 2U;
		uint32_t lower = upper + 1U;

		halfspaces[upper].normal[local] = 1.0f;
		halfspaces[upper].distance = boundary_witness[local] + 0.125f;
		halfspaces[lower].normal[local] = -1.0f;
		halfspaces[lower].distance = -boundary_witness[local] + 0.125f;
	}
	memset(&stats, 0, sizeof(stats));
	solved = SG_ConfigurationLatticeFind(halfspaces,
		region->face_count + 6U, boundary->normal, point, &stats);
	free(halfspaces);
	AccountLattice(audit, &stats);
	if (solved < 0)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_DOMAIN_COVERAGE,
			region_index, 0U);
		return -1;
	}
	if (!solved) return 0;
	for (local = 0U; local < 3U; local++)
		witness[local] = (float)point[local] * 0.125f;
	if (!PointInRegion(audit, region_index, witness) ||
		PlaneDistance(witness, boundary->normal, boundary->distance) >= 0.0)
		return 0;
	return 1;
}

static sg_water_capability_kind_t BoundaryKind(
	const sg_configuration_semantic_region_t *source,
	const sg_configuration_semantic_region_t *destination)
{
	if (source->water_level == 0U && destination->water_level != 0U)
		return SG_WATER_CAPABILITY_ENTRY;
	if (source->water_level != 0U && destination->water_level == 0U)
		return SG_WATER_CAPABILITY_EXIT;
	return SG_WATER_CAPABILITY_VOLUME_CROSSING;
}

static int BoundaryDirection(audit_t *audit, uint32_t source_region,
	uint32_t destination_region, uint32_t portal,
	const float source_witness[3], const float boundary_witness[3],
	const float destination_witness[3])
{
	const sg_configuration_semantic_region_t *source =
		&audit->source->semantics->regions[source_region];
	const sg_configuration_semantic_region_t *destination =
		&audit->source->semantics->regions[destination_region];
	sg_host_collision_transition_t transition;
	float direction[3];
	float length;
	uint32_t axis;
	uint32_t binding;

	if (!SG_HostCollisionTransition(audit->source->authority, NULL,
		source_witness, destination_witness,
		audit->source->configuration->cells[source->cell].stance,
		&transition))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
			source_region, 0U);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
		direction[axis] = destination_witness[axis] - source_witness[axis];
	length = sqrtf(Dot3(direction, direction));
	if (!transition.clear || !isfinite(length) || length <= 0.0f)
	{
		uint32_t count = 0U;

		for (binding = audit->binding_offsets[source_region];
			binding < audit->binding_offsets[source_region + 1U]; binding++)
			if (!CheckedAddU32(&count, BindingMultiplicity(audit, binding)))
			{
				Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW,
					source_region, 0U);
				return 0;
			}

		if (!CounterAdd(audit, &audit->result.obligation_count, count,
				source_region) ||
			!CounterAdd(audit, &audit->result.proved_empty_count, count,
				source_region))
			return 0;
		return 1;
	}
	for (axis = 0U; axis < 3U; axis++) direction[axis] /= length;
	for (binding = audit->binding_offsets[source_region];
		binding < audit->binding_offsets[source_region + 1U]; binding++)
	{
		sg_water_capability_fact_t fact;
		sg_host_pmove_result_t result;
		uint32_t destination_phase;

		memset(&fact, 0, sizeof(fact));
		fact.source_region = source_region;
		fact.destination_region = destination_region;
		fact.source_phase = audit->source->bindings[binding].phase;
		fact.portal = portal;
		fact.kind = BoundaryKind(source, destination);
		fact.direction = SG_WATER_DIRECTION_BOUNDARY;
		fact.source_medium = RegionMedium(source);
		fact.destination_medium = RegionMedium(destination);
		fact.source_contents =
			SG_HostCollisionRuneContents(source->water_type);
		fact.destination_contents =
			SG_HostCollisionRuneContents(destination->water_type);
		fact.source_water_level = source->water_level;
		fact.destination_water_level = destination->water_level;
		Copy3(fact.source_witness.value, source_witness);
		Copy3(fact.boundary_witness.value, boundary_witness);
		Copy3(fact.destination_witness.value, destination_witness);
		Copy3(fact.direction_vector.value, direction);
		fact.flags = SG_WATER_CAPABILITY_DIRECTIONAL;
		if (fact.source_medium != fact.destination_medium)
			fact.flags |= SG_WATER_CAPABILITY_CHANGES_MEDIUM;
		if (portal != SG_WATER_CAPABILITY_INDEX_NONE)
			fact.flags |= SG_WATER_CAPABILITY_CROSSES_PORTAL;
		if (!CounterAdd(audit, &audit->result.obligation_count,
				BindingMultiplicity(audit, binding),
				source_region))
			return 0;
		if (audit->source->phases[fact.source_phase].reference_frame ==
			SG_RUNE_FRAME_MOVER_RELATIVE)
		{
			if (!CounterAdd(audit, &audit->result.proved_empty_count,
					BindingMultiplicity(audit, binding),
					source_region))
				return 0;
			continue;
		}
		if (!Probe(audit, source_region, source_witness, direction, &fact,
			&result)) return 0;
		if (!PointInRegion(audit, destination_region, result.origin))
		{
			if (!CounterAdd(audit, &audit->result.proved_empty_count, 1U,
					source_region))
				return 0;
			continue;
		}
		if (!ResolveDestination(audit, destination_region, &result,
			&destination_phase)) return 0;
		fact.destination_phase = destination_phase;
		if (!AppendFact(audit, &fact)) return 0;
		audit->result.proved_fact_count++;
	}
	return 1;
}

static int BoundaryPair(audit_t *audit, uint32_t first_region,
	uint32_t first_face, uint32_t second_region, uint32_t second_face,
	uint32_t portal)
{
	const sg_configuration_semantic_region_t *first =
		&audit->source->semantics->regions[first_region];
	const sg_configuration_semantic_region_t *second =
		&audit->source->semantics->regions[second_region];
	float boundary[3];
	float first_witness[3];
	float second_witness[3];
	int result;

	if ((first->water_level == 0U && second->water_level == 0U) ||
		!BoundsOverlap(&first->bounds, &second->bounds)) return 1;
	result = SharedBoundaryWitness(audit, first_region, first_face,
		second_region, second_face, portal, boundary);
	if (result <= 0) return result == 0;
	result = RegionSideWitness(audit, first_region, first_face, boundary,
		first_witness);
	if (result <= 0) return result == 0;
	result = RegionSideWitness(audit, second_region, second_face, boundary,
		second_witness);
	if (result <= 0) return result == 0;
	return BoundaryDirection(audit, first_region, second_region, portal,
		first_witness, boundary, second_witness) &&
		BoundaryDirection(audit, second_region, first_region, portal,
		second_witness, boundary, first_witness);
}

static int SameSemanticSource(const face_ref_t *left, const face_ref_t *right)
{
	return left->cell == right->cell &&
		left->source_kind == right->source_kind &&
		left->source_index == right->source_index &&
		left->source_variant == right->source_variant &&
		left->sample_index == right->sample_index;
}

static int SameRegionSide(const face_ref_t *left, const face_ref_t *right)
{
	return left->region == right->region && left->reversed == right->reversed;
}

static int FaceRefCompare(const void *left_value, const void *right_value)
{
	const face_ref_t *left = left_value;
	const face_ref_t *right = right_value;

#define COMPARE(member) do { \
	if (left->member < right->member) return -1; \
	if (left->member > right->member) return 1; \
} while (0)
	COMPARE(cell);
	COMPARE(source_kind);
	COMPARE(source_index);
	COMPARE(source_variant);
	COMPARE(sample_index);
	COMPARE(reversed);
	COMPARE(region);
	COMPARE(face);
#undef COMPARE
	return 0;
}

static int SweepEventCompare(const void *left_value, const void *right_value)
{
	const sweep_event_t *left = left_value;
	const sweep_event_t *right = right_value;

	if (left->coordinate < right->coordinate) return -1;
	if (left->coordinate > right->coordinate) return 1;
	if (left->starts > right->starts) return -1;
	if (left->starts < right->starts) return 1;
	if (left->reference < right->reference) return -1;
	if (left->reference > right->reference) return 1;
	return 0;
}

static uint32_t IntervalHeight(const interval_node_t *nodes, uint32_t node)
{
	return node == UINT32_MAX ? 0U : nodes[node].height;
}

static void UpdateIntervalNode(interval_node_t *nodes, uint32_t node)
{
	uint32_t left_height = IntervalHeight(nodes, nodes[node].left);
	uint32_t right_height = IntervalHeight(nodes, nodes[node].right);
	float high = nodes[node].high;

	if (nodes[node].left != UINT32_MAX)
		high = fmaxf(high, nodes[nodes[node].left].subtree_high);
	if (nodes[node].right != UINT32_MAX)
		high = fmaxf(high, nodes[nodes[node].right].subtree_high);
	nodes[node].height = 1U + (left_height > right_height ?
		left_height : right_height);
	nodes[node].subtree_high = high;
}

static int IntervalKeyCompare(const interval_node_t *left,
	const interval_node_t *right)
{
	if (left->low < right->low) return -1;
	if (left->low > right->low) return 1;
	if (left->high < right->high) return -1;
	if (left->high > right->high) return 1;
	if (left->reference < right->reference) return -1;
	if (left->reference > right->reference) return 1;
	return 0;
}

static uint32_t RotateIntervalLeft(interval_node_t *nodes, uint32_t root)
{
	uint32_t replacement = nodes[root].right;

	nodes[root].right = nodes[replacement].left;
	nodes[replacement].left = root;
	UpdateIntervalNode(nodes, root);
	UpdateIntervalNode(nodes, replacement);
	return replacement;
}

static uint32_t RotateIntervalRight(interval_node_t *nodes, uint32_t root)
{
	uint32_t replacement = nodes[root].left;

	nodes[root].left = nodes[replacement].right;
	nodes[replacement].right = root;
	UpdateIntervalNode(nodes, root);
	UpdateIntervalNode(nodes, replacement);
	return replacement;
}

static uint32_t BalanceIntervalNode(interval_node_t *nodes, uint32_t root)
{
	int balance;

	UpdateIntervalNode(nodes, root);
	balance = (int)IntervalHeight(nodes, nodes[root].left) -
		(int)IntervalHeight(nodes, nodes[root].right);
	if (balance > 1)
	{
		uint32_t left = nodes[root].left;

		if (IntervalHeight(nodes, nodes[left].right) >
			IntervalHeight(nodes, nodes[left].left))
			nodes[root].left = RotateIntervalLeft(nodes, left);
		return RotateIntervalRight(nodes, root);
	}
	if (balance < -1)
	{
		uint32_t right = nodes[root].right;

		if (IntervalHeight(nodes, nodes[right].left) >
			IntervalHeight(nodes, nodes[right].right))
			nodes[root].right = RotateIntervalRight(nodes, right);
		return RotateIntervalLeft(nodes, root);
	}
	return root;
}

static uint32_t InsertIntervalNode(interval_node_t *nodes, uint32_t root,
	uint32_t inserted)
{
	if (root == UINT32_MAX) return inserted;
	if (IntervalKeyCompare(&nodes[inserted], &nodes[root]) < 0)
		nodes[root].left = InsertIntervalNode(nodes, nodes[root].left, inserted);
	else
		nodes[root].right = InsertIntervalNode(nodes, nodes[root].right, inserted);
	return BalanceIntervalNode(nodes, root);
}

static uint32_t RemoveIntervalNode(interval_node_t *nodes, uint32_t root,
	const interval_node_t *removed)
{
	int comparison;

	if (root == UINT32_MAX) return root;
	comparison = IntervalKeyCompare(removed, &nodes[root]);
	if (comparison < 0)
		nodes[root].left = RemoveIntervalNode(nodes, nodes[root].left, removed);
	else if (comparison > 0)
		nodes[root].right = RemoveIntervalNode(nodes, nodes[root].right, removed);
	else if (nodes[root].left == UINT32_MAX || nodes[root].right == UINT32_MAX)
		return nodes[root].left != UINT32_MAX ?
			nodes[root].left : nodes[root].right;
	else
	{
		uint32_t successor = nodes[root].right;
		interval_node_t key;

		while (nodes[successor].left != UINT32_MAX)
			successor = nodes[successor].left;
		key = nodes[successor];
		nodes[root].low = key.low;
		nodes[root].high = key.high;
		nodes[root].reference = key.reference;
		nodes[root].right = RemoveIntervalNode(nodes, nodes[root].right, &key);
	}
	return BalanceIntervalNode(nodes, root);
}

static int PositiveBoundsOverlap(const audit_t *audit, const face_ref_t *first,
	const face_ref_t *second, uint32_t first_axis, uint32_t second_axis)
{
	const sg_rune_bounds_t *left =
		&audit->source->semantics->regions[first->region].bounds;
	const sg_rune_bounds_t *right =
		&audit->source->semantics->regions[second->region].bounds;

	return fmaxf(left->mins.value[first_axis],
		right->mins.value[first_axis]) <
		fminf(left->maxs.value[first_axis], right->maxs.value[first_axis]) &&
		fmaxf(left->mins.value[second_axis],
		right->mins.value[second_axis]) <
		fminf(left->maxs.value[second_axis], right->maxs.value[second_axis]);
}

static int QueryIntervalTree(audit_t *audit, const face_ref_t *references,
	const interval_node_t *nodes, uint32_t root, uint32_t query_reference,
	uint32_t sweep_axis, uint32_t interval_axis)
{
	const face_ref_t *query = &references[query_reference];
	const sg_rune_bounds_t *query_bounds =
		&audit->source->semantics->regions[query->region].bounds;
	float low = query_bounds->mins.value[interval_axis];
	float high = query_bounds->maxs.value[interval_axis];
	const interval_node_t *node;

	if (root == UINT32_MAX) return 1;
	node = &nodes[root];
	if (node->left != UINT32_MAX &&
		nodes[node->left].subtree_high > low &&
		!QueryIntervalTree(audit, references, nodes, node->left,
			query_reference, sweep_axis, interval_axis)) return 0;
	if (node->low < high && node->high > low)
	{
		const face_ref_t *candidate = &references[node->reference];

		if (candidate->region != query->region &&
			PositiveBoundsOverlap(audit, candidate, query, sweep_axis,
				interval_axis))
		{
			audit->result.same_cell_candidate_pairs++;
			if (!BoundaryPair(audit, candidate->region, candidate->face,
				query->region, query->face,
				SG_WATER_CAPABILITY_INDEX_NONE)) return 0;
		}
	}
	if (node->low < high &&
		!QueryIntervalTree(audit, references, nodes, node->right,
			query_reference, sweep_axis, interval_axis)) return 0;
	return 1;
}

static int EnumerateSameCellBoundaryGroup(audit_t *audit,
	const face_ref_t *references, uint32_t group_start, uint32_t group_end)
{
	sweep_event_t *events = NULL;
	interval_node_t *nodes = NULL;
	uint32_t roots[2] = { UINT32_MAX, UINT32_MAX };
	uint32_t unique_count = 0U;
	uint32_t event_count;
	uint32_t reference;
	uint32_t event;
	uint32_t dominant = 0U;
	uint32_t tangents[2];
	uint32_t sweep_axis;
	uint32_t interval_axis;
	float center_span[2];
	int result = 0;

	for (reference = group_start; reference < group_end; reference++)
		if (reference == group_start || !SameRegionSide(
			&references[reference - 1U], &references[reference])) unique_count++;
	if (unique_count > UINT32_MAX / 2U ||
		!AllocationFits((size_t)unique_count * 2U, sizeof(*events)) ||
		!AllocationFits((size_t)unique_count, sizeof(*nodes)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, unique_count, 0U);
		return 0;
	}
	events = malloc((size_t)unique_count * 2U * sizeof(*events));
	nodes = calloc(unique_count, sizeof(*nodes));
	if ((!events || !nodes) && unique_count != 0U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, group_start, 0U);
		goto done;
	}
	for (reference = 1U; reference < 3U; reference++)
		if (fabsf(audit->source->semantics->faces[
			references[group_start].face].normal[reference]) >
			fabsf(audit->source->semantics->faces[
				references[group_start].face].normal[dominant]))
			dominant = reference;
	tangents[0] = (dominant + 1U) % 3U;
	tangents[1] = (dominant + 2U) % 3U;
	for (reference = 0U; reference < 2U; reference++)
	{
		float minimum = FLT_MAX;
		float maximum = -FLT_MAX;
		uint32_t source;

		for (source = group_start; source < group_end; source++)
		{
			const sg_rune_bounds_t *bounds =
				&audit->source->semantics->regions[
					references[source].region].bounds;
			float center = (bounds->mins.value[tangents[reference]] +
				bounds->maxs.value[tangents[reference]]) * 0.5f;

			minimum = fminf(minimum, center);
			maximum = fmaxf(maximum, center);
		}
		center_span[reference] = maximum - minimum;
	}
	sweep_axis = center_span[1] > center_span[0] ? tangents[1] : tangents[0];
	interval_axis = sweep_axis == tangents[0] ? tangents[1] : tangents[0];
	event_count = 0U;
	for (reference = group_start; reference < group_end; reference++)
	{
		const sg_rune_bounds_t *bounds;
		uint32_t node;

		if (reference != group_start && SameRegionSide(
			&references[reference - 1U], &references[reference])) continue;
		node = event_count / 2U;
		bounds = &audit->source->semantics->regions[
			references[reference].region].bounds;
		nodes[node].low = bounds->mins.value[interval_axis];
		nodes[node].high = bounds->maxs.value[interval_axis];
		nodes[node].subtree_high = nodes[node].high;
		nodes[node].reference = reference;
		nodes[node].left = UINT32_MAX;
		nodes[node].right = UINT32_MAX;
		nodes[node].height = 1U;
		events[event_count].coordinate = bounds->mins.value[sweep_axis];
		events[event_count].reference = node;
		events[event_count++].starts = 1U;
		events[event_count].coordinate = bounds->maxs.value[sweep_axis];
		events[event_count].reference = node;
		events[event_count++].starts = 0U;
	}
	qsort(events, event_count, sizeof(*events), SweepEventCompare);
	for (event = 0U; event < event_count; event++)
	{
		uint32_t node = events[event].reference;
		uint32_t side = references[nodes[node].reference].reversed ? 1U : 0U;

		if (events[event].starts)
		{
			if (!QueryIntervalTree(audit, references, nodes, roots[1U - side],
				nodes[node].reference, sweep_axis, interval_axis)) goto done;
			roots[side] = InsertIntervalNode(nodes, roots[side], node);
		}
		else
			roots[side] = RemoveIntervalNode(nodes, roots[side], &nodes[node]);
	}
	result = 1;
done:
	free(events);
	free(nodes);
	return result;
}

static int EnumerateSameCellBoundaries(audit_t *audit)
{
	face_ref_t *references;
	uint32_t reference_count = 0U;
	uint32_t region;
	uint32_t group_start;

	if (!AllocationFits((size_t)audit->source->semantics->face_count,
		sizeof(*references)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW,
			audit->source->semantics->face_count, 0U);
		return 0;
	}
	references = malloc((size_t)audit->source->semantics->face_count *
		sizeof(*references));
	if (!references && audit->source->semantics->face_count != 0U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
		return 0;
	}
	for (region = 0U; region < audit->source->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&audit->source->semantics->regions[region];
		uint32_t face;

		for (face = record->first_face;
			face < record->first_face + record->face_count; face++)
		{
			const sg_configuration_semantic_face_t *source =
				&audit->source->semantics->faces[face];
			face_ref_t *destination;

			if (source->source_kind == SG_CONFIGURATION_SEMANTIC_PLANE_CELL)
				continue;
			destination = &references[reference_count++];
			destination->cell = record->cell;
			destination->region = region;
			destination->face = face;
			destination->source_kind = source->source_kind;
			destination->source_index = source->source_index;
			destination->source_variant = source->source_variant;
			destination->sample_index = source->sample_index;
			destination->reversed = source->reversed;
		}
	}
	qsort(references, reference_count, sizeof(*references), FaceRefCompare);
	for (group_start = 0U; group_start < reference_count; )
	{
		uint32_t group_end = group_start + 1U;

		while (group_end < reference_count && SameSemanticSource(
			&references[group_start], &references[group_end])) group_end++;
		if (!EnumerateSameCellBoundaryGroup(audit, references, group_start,
			group_end))
		{
			free(references);
			return 0;
		}
		group_start = group_end;
	}
	free(references);
	return 1;
}

static uint32_t FindPortalFace(const audit_t *audit, uint32_t region,
	const sg_configuration_portal_t *portal)
{
	const sg_configuration_semantic_region_t *record =
		&audit->source->semantics->regions[region];
	uint32_t face;

	for (face = record->first_face;
		face < record->first_face + record->face_count; face++)
	{
		const sg_configuration_semantic_face_t *semantic =
			&audit->source->semantics->faces[face];

		if (semantic->source_kind == SG_CONFIGURATION_SEMANTIC_PLANE_CELL &&
			PlanesCoplanar(semantic->normal, semantic->distance,
				portal->plane.normal, portal->plane.distance)) return face;
	}
	return SG_WATER_CAPABILITY_INDEX_NONE;
}

static int EnumeratePortalBoundaries(audit_t *audit)
{
	uint32_t portal_index;

	for (portal_index = 0U;
		portal_index < audit->source->configuration->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&audit->source->configuration->portals[portal_index];
		uint32_t first_region;

		if (portal->from_cell >= audit->source->configuration->cell_count ||
			portal->to_cell >= audit->source->configuration->cell_count ||
			portal->from_cell == portal->to_cell || portal->vertex_count < 3U ||
			portal->first_vertex > audit->source->configuration->vertex_count ||
			portal->vertex_count > audit->source->configuration->vertex_count -
				portal->first_vertex) return 0;
		for (first_region = audit->cell_region_offsets[portal->from_cell];
			first_region < audit->cell_region_offsets[portal->from_cell + 1U];
			first_region++)
		{
			uint32_t first_face = FindPortalFace(audit, first_region, portal);
			uint32_t second_region;

			if (first_face == SG_WATER_CAPABILITY_INDEX_NONE) continue;
			for (second_region = audit->cell_region_offsets[portal->to_cell];
				second_region <
					audit->cell_region_offsets[portal->to_cell + 1U];
				second_region++)
			{
				uint32_t second_face =
					FindPortalFace(audit, second_region, portal);

				if (second_face != SG_WATER_CAPABILITY_INDEX_NONE &&
					!BoundaryPair(audit, first_region, first_face,
						second_region, second_face, portal_index)) return 0;
			}
		}
	}
	return 1;
}

static float NormalFloat(float value)
{
	return value == 0.0f ? 0.0f : value;
}

static void NormalizeInterval(sg_rune_interval_t *interval)
{
	interval->min_value = NormalFloat(interval->min_value);
	interval->max_value = NormalFloat(interval->max_value);
}

static void NormalizeFact(sg_water_capability_fact_t *fact)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		fact->source_witness.value[axis] =
			NormalFloat(fact->source_witness.value[axis]);
		fact->boundary_witness.value[axis] =
			NormalFloat(fact->boundary_witness.value[axis]);
		fact->destination_witness.value[axis] =
			NormalFloat(fact->destination_witness.value[axis]);
		fact->direction_vector.value[axis] =
			NormalFloat(fact->direction_vector.value[axis]);
		fact->command_vector.value[axis] =
			NormalFloat(fact->command_vector.value[axis]);
		fact->source_velocity.value[axis] =
			NormalFloat(fact->source_velocity.value[axis]);
		fact->observed_displacement.value[axis] =
			NormalFloat(fact->observed_displacement.value[axis]);
		fact->observed_velocity.value[axis] =
			NormalFloat(fact->observed_velocity.value[axis]);
	}
	NormalizeInterval(&fact->parameters.displacement.x);
	NormalizeInterval(&fact->parameters.displacement.y);
	NormalizeInterval(&fact->parameters.displacement.z);
	NormalizeInterval(&fact->parameters.duration_ms);
	NormalizeInterval(&fact->parameters.speed);
	NormalizeInterval(&fact->parameters.acceleration);
	NormalizeInterval(&fact->parameters.vertical_acceleration);
	fact->parameters.gravity = NormalFloat(fact->parameters.gravity);
	fact->parameters.drag = NormalFloat(fact->parameters.drag);
}

static int FloatCompare(float left, float right)
{
	if (left < right) return -1;
	if (left > right) return 1;
	return 0;
}

static int VecCompare(const sg_rune_vec3_t *left, const sg_rune_vec3_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		int comparison = FloatCompare(left->value[axis], right->value[axis]);

		if (comparison != 0) return comparison;
	}
	return 0;
}

static int IntervalCompare(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	int comparison = FloatCompare(left->min_value, right->min_value);

	return comparison != 0 ? comparison :
		FloatCompare(left->max_value, right->max_value);
}

static int ParametersCompare(const sg_rune_kernel_parameters_t *left,
	const sg_rune_kernel_parameters_t *right)
{
	int comparison;

#define INTERVAL(member) do { \
	comparison = IntervalCompare(&left->member, &right->member); \
	if (comparison != 0) return comparison; \
} while (0)
	INTERVAL(displacement.x);
	INTERVAL(displacement.y);
	INTERVAL(displacement.z);
	INTERVAL(duration_ms);
	INTERVAL(speed);
	INTERVAL(acceleration);
	INTERVAL(vertical_acceleration);
#undef INTERVAL
	comparison = FloatCompare(left->gravity, right->gravity);
	if (comparison != 0) return comparison;
	comparison = FloatCompare(left->drag, right->drag);
	if (comparison != 0) return comparison;
	if (left->physics_abi_id < right->physics_abi_id) return -1;
	if (left->physics_abi_id > right->physics_abi_id) return 1;
	if (left->fixed_latency_ms < right->fixed_latency_ms) return -1;
	if (left->fixed_latency_ms > right->fixed_latency_ms) return 1;
	if (left->dwell_ms < right->dwell_ms) return -1;
	if (left->dwell_ms > right->dwell_ms) return 1;
	return 0;
}

static int FactCompare(const void *left_value, const void *right_value)
{
	const sg_water_capability_fact_t *left = left_value;
	const sg_water_capability_fact_t *right = right_value;
	int comparison;

#define MEMBER(member) do { \
	if (left->member < right->member) return -1; \
	if (left->member > right->member) return 1; \
} while (0)
	MEMBER(source_region);
	MEMBER(destination_region);
	MEMBER(source_phase);
	MEMBER(destination_phase);
	MEMBER(portal);
	MEMBER(kind);
	MEMBER(direction);
	MEMBER(source_medium);
	MEMBER(destination_medium);
	MEMBER(source_contents);
	MEMBER(destination_contents);
	MEMBER(current);
	MEMBER(source_water_level);
	MEMBER(destination_water_level);
#undef MEMBER
#define VECTOR(member) do { \
	comparison = VecCompare(&left->member, &right->member); \
	if (comparison != 0) return comparison; \
} while (0)
	VECTOR(source_witness);
	VECTOR(boundary_witness);
	VECTOR(destination_witness);
	VECTOR(direction_vector);
	VECTOR(command_vector);
	VECTOR(source_velocity);
	VECTOR(observed_displacement);
	VECTOR(observed_velocity);
#undef VECTOR
#define MEMBER(member) do { \
	if (left->member < right->member) return -1; \
	if (left->member > right->member) return 1; \
} while (0)
	MEMBER(result_pm_flags);
	MEMBER(result_support_model_index);
	MEMBER(result_support_instance_id);
	MEMBER(result_water_type);
	MEMBER(result_grounded);
	MEMBER(result_water_level);
#undef MEMBER
	comparison = ParametersCompare(&left->parameters, &right->parameters);
	if (comparison != 0) return comparison;
	if (left->flags < right->flags) return -1;
	if (left->flags > right->flags) return 1;
	return 0;
}

static int BoundaryKeyCompare(const void *left_value, const void *right_value)
{
	const boundary_key_t *left = left_value;
	const boundary_key_t *right = right_value;

	if (left->first_region < right->first_region) return -1;
	if (left->first_region > right->first_region) return 1;
	if (left->second_region < right->second_region) return -1;
	if (left->second_region > right->second_region) return 1;
	if (left->portal < right->portal) return -1;
	if (left->portal > right->portal) return 1;
	return 0;
}

static int IntervalValid(const sg_rune_interval_t *interval)
{
	return isfinite(interval->min_value) && isfinite(interval->max_value) &&
		interval->min_value <= interval->max_value;
}

static int Q8Vector(const sg_rune_vec3_t *vector)
{
	uint32_t axis;

	if (!Finite3(vector->value)) return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (truncf(vector->value[axis] * 8.0f) !=
			vector->value[axis] * 8.0f) return 0;
	return 1;
}

static int FactValid(const audit_t *audit,
	const sg_water_capability_fact_t *fact)
{
	const uint32_t allowed_flags = SG_WATER_CAPABILITY_DIRECTIONAL |
		SG_WATER_CAPABILITY_CHANGES_MEDIUM |
		SG_WATER_CAPABILITY_USES_CURRENT |
		SG_WATER_CAPABILITY_CROSSES_PORTAL |
		SG_WATER_CAPABILITY_HOST_PROVEN |
		SG_WATER_CAPABILITY_STRADDLES_FRAME_LAW;

	return fact->source_region < audit->source->semantics->region_count &&
		fact->destination_region < audit->source->semantics->region_count &&
		fact->source_phase < audit->source->phase_count &&
		fact->destination_phase < audit->source->phase_count &&
		(fact->portal == SG_WATER_CAPABILITY_INDEX_NONE ||
			fact->portal < audit->source->configuration->portal_count) &&
		fact->kind < SG_WATER_CAPABILITY_KIND_COUNT &&
		fact->direction < SG_WATER_DIRECTION_COUNT &&
		fact->source_medium < SG_RUNE_MEDIUM_COUNT &&
		fact->destination_medium < SG_RUNE_MEDIUM_COUNT &&
		fact->source_water_level <= 3U && fact->destination_water_level <= 3U &&
		fact->reserved[0] == 0U && fact->reserved[1] == 0U &&
		fact->result_reserved[0] == 0U && fact->result_reserved[1] == 0U &&
		(fact->flags & ~allowed_flags) == 0U &&
		Q8Vector(&fact->source_witness) &&
		Q8Vector(&fact->boundary_witness) &&
		Q8Vector(&fact->destination_witness) &&
		Finite3(fact->direction_vector.value) &&
		Finite3(fact->command_vector.value) &&
		Finite3(fact->source_velocity.value) &&
		Finite3(fact->observed_displacement.value) &&
		Finite3(fact->observed_velocity.value) &&
		fact->result_grounded <= 1U && fact->result_water_level <= 3U &&
		IntervalValid(&fact->parameters.displacement.x) &&
		IntervalValid(&fact->parameters.displacement.y) &&
		IntervalValid(&fact->parameters.displacement.z) &&
		IntervalValid(&fact->parameters.duration_ms) &&
		IntervalValid(&fact->parameters.speed) &&
		IntervalValid(&fact->parameters.acceleration) &&
		IntervalValid(&fact->parameters.vertical_acceleration) &&
		isfinite(fact->parameters.gravity) &&
		isfinite(fact->parameters.drag) &&
		fact->parameters.physics_abi_id == audit->source->host_law_identity;
}

static int FinalizeExpected(audit_t *audit)
{
	boundary_key_t *boundaries = NULL;
	uint32_t read;
	uint32_t write = 0U;
	uint32_t boundary_count = 0U;

	for (read = 0U; read < audit->fact_count; read++)
		NormalizeFact(&audit->facts[read]);
	if (audit->fact_count != 0U)
		qsort(audit->facts, audit->fact_count, sizeof(*audit->facts),
			FactCompare);
	for (read = 0U; read < audit->fact_count; read++)
		if (write == 0U || FactCompare(&audit->facts[write - 1U],
			&audit->facts[read]) != 0)
			audit->facts[write++] = audit->facts[read];
	audit->fact_count = write;
	for (read = 0U; read < audit->fact_count; read++)
		audit->facts[read].order = read;
	if (audit->fact_count != 0U)
	{
		if (!AllocationFits((size_t)audit->fact_count, sizeof(*boundaries)))
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW,
				audit->fact_count, 0U);
			return 0;
		}
		boundaries = calloc(audit->fact_count, sizeof(*boundaries));
		if (!boundaries)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
			return 0;
		}
	}
	for (read = 0U; read < audit->fact_count; read++)
	{
		const sg_water_capability_fact_t *fact = &audit->facts[read];

		if (fact->source_region == fact->destination_region ||
			(fact->kind != SG_WATER_CAPABILITY_ENTRY &&
			 fact->kind != SG_WATER_CAPABILITY_EXIT &&
			 fact->kind != SG_WATER_CAPABILITY_VOLUME_CROSSING)) continue;
		boundaries[boundary_count].first_region =
			fact->source_region < fact->destination_region ?
			fact->source_region : fact->destination_region;
		boundaries[boundary_count].second_region =
			fact->source_region < fact->destination_region ?
			fact->destination_region : fact->source_region;
		boundaries[boundary_count].portal = fact->portal;
		boundary_count++;
	}
	if (boundary_count != 0U)
		qsort(boundaries, boundary_count, sizeof(*boundaries), BoundaryKeyCompare);
	audit->result.boundary_count = 0U;
	for (read = 0U; read < boundary_count; read++)
		if (read == 0U || BoundaryKeyCompare(&boundaries[read - 1U],
			&boundaries[read]) != 0) audit->result.boundary_count++;
	free(boundaries);
	if (audit->result.wet_region_count != 0U && audit->fact_count == 0U)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_DOMAIN_COVERAGE, 0U, 0U);
		return 0;
	}
	return 1;
}

static int CompareCandidate(audit_t *audit,
	const sg_water_capability_set_t *candidate)
{
	sg_water_capability_fact_t *normalized = NULL;
	uint32_t fact;

	if (candidate->fact_count != 0U)
	{
		if (!AllocationFits(candidate->fact_count, sizeof(*normalized)))
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_OVERFLOW, 0U, 0U);
			return 0;
		}
		normalized = malloc((size_t)candidate->fact_count *
			sizeof(*normalized));
		if (!normalized)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
			return 0;
		}
	}
	for (fact = 0U; fact < candidate->fact_count; fact++)
	{
		normalized[fact] = candidate->facts[fact];
		if (!FactValid(audit, &normalized[fact]))
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_INVALID_CANDIDATE,
				0U, fact);
			free(normalized);
			return 0;
		}
		NormalizeFact(&normalized[fact]);
		if (normalized[fact].order != fact ||
			(fact != 0U && FactCompare(&normalized[fact - 1U],
				&normalized[fact]) >= 0))
		{
			Fail(audit, fact != 0U &&
				FactCompare(&normalized[fact - 1U], &normalized[fact]) == 0 ?
				SG_WATER_CAPABILITY_AUDIT_DUPLICATE_FACT :
				SG_WATER_CAPABILITY_AUDIT_NONCANONICAL_ORDER,
				0U, fact);
			free(normalized);
			return 0;
		}
	}
	if (candidate->fact_count != audit->fact_count)
	{
		Fail(audit, candidate->fact_count < audit->fact_count ?
			SG_WATER_CAPABILITY_AUDIT_OMITTED_FACT :
			SG_WATER_CAPABILITY_AUDIT_INVENTED_FACT,
			candidate->fact_count < audit->fact_count ? candidate->fact_count :
				audit->fact_count,
			candidate->fact_count < audit->fact_count ? candidate->fact_count :
				audit->fact_count);
		free(normalized);
		return 0;
	}
	for (fact = 0U; fact < audit->fact_count; fact++)
		if (FactCompare(&audit->facts[fact], &normalized[fact]) != 0)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_FACT_DISAGREEMENT,
				fact, fact);
			free(normalized);
			return 0;
		}
	if (candidate->wet_region_count != audit->result.wet_region_count ||
		candidate->boundary_count != audit->result.boundary_count ||
		candidate->host_pmove_frames != audit->result.host_pmove_frames ||
		candidate->lattice_solve_calls != audit->result.lattice_solve_calls ||
		candidate->lattice_constraints != audit->result.lattice_constraints ||
		candidate->same_cell_candidate_pairs !=
			audit->result.same_cell_candidate_pairs ||
		candidate->lattice_maximum_binary_shift !=
			audit->result.lattice_maximum_binary_shift)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT,
			0U, 0U);
		free(normalized);
		return 0;
	}
	free(normalized);
	return 1;
}

static int CopyPublishedFact(audit_t *audit,
	const sg_water_capability_fact_t *fact,
	sg_water_capability_publication_fact_t *published)
{
	const normalized_source_t *source = audit->source;
	const sg_configuration_semantic_region_t *source_region;
	const sg_configuration_semantic_region_t *destination_region;
	const sg_configuration_cell_t *source_cell;
	const sg_configuration_cell_t *destination_cell;
	sg_host_collision_pose_t source_pose;
	sg_host_collision_pose_t destination_pose;
	sg_host_collision_pose_t result_pose;
	float result_origin[3];
	uint32_t axis;

	if (!FactValid(audit, fact))
		return 0;
	source_region = &source->semantics->regions[fact->source_region];
	destination_region = &source->semantics->regions[fact->destination_region];
	source_cell = &source->configuration->cells[source_region->cell];
	destination_cell = &source->configuration->cells[destination_region->cell];
	if (!SG_RuneModelStableIdValid(&source_cell->id.value) ||
		!SG_RuneModelStableIdValid(&destination_cell->id.value) ||
		!SG_RuneModelStableIdValid(
			&source->phases[fact->source_phase].id.value) ||
		!SG_RuneModelStableIdValid(
			&source->phases[fact->destination_phase].id.value) ||
		(fact->portal != SG_WATER_CAPABILITY_INDEX_NONE &&
		 !SG_RuneModelStableIdValid(&source->configuration->portals[
			fact->portal].id.value)))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNSTABLE_REFERENCE,
			fact->order, 0U);
		return 0;
	}
	if (!SG_HostCollisionClassifyPose(source->authority, NULL,
		fact->source_witness.value, source_cell->stance, &source_pose) ||
		!SG_HostCollisionClassifyPose(source->authority, NULL,
		fact->destination_witness.value, destination_cell->stance,
		&destination_pose))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
			fact->order, 0U);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
		result_origin[axis] = fact->source_witness.value[axis] +
			fact->observed_displacement.value[axis];
	if (!SG_HostCollisionClassifyPose(source->authority, NULL, result_origin,
		(source->phases[fact->destination_phase].stance), &result_pose) ||
		!source_pose.valid || !destination_pose.valid || !result_pose.valid)
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
			fact->order, 0U);
		return 0;
	}
	memset(published, 0, sizeof(*published));
	published->order = fact->order;
	published->source_cell = source_cell->id;
	published->destination_cell = destination_cell->id;
	published->source_semantic_region_id = source_region->id;
	published->destination_semantic_region_id = destination_region->id;
	published->source_phase = source->phases[fact->source_phase].id;
	published->destination_phase = source->phases[fact->destination_phase].id;
	published->phase_transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	for (axis = 0U; fact->portal != SG_WATER_CAPABILITY_INDEX_NONE &&
		axis < source->transition_count; axis++)
	{
		const sg_rune_phase_transition_t *transition =
			&source->transitions[axis];
		const sg_phase_catalog_transition_evidence_t *evidence =
			&source->transition_evidence[axis];

		if (evidence->origin == SG_PHASE_CATALOG_TRANSITION_PORTAL &&
			evidence->source_region_id == source_region->id &&
			evidence->destination_region_id == destination_region->id &&
			SG_RuneModelStableIdEqual(&evidence->portal.value,
				&source->configuration->portals[fact->portal].id.value) &&
			SG_RuneModelStableIdEqual(&transition->cell.value,
			&source_cell->id.value) &&
			SG_RuneModelStableIdEqual(&transition->destination_cell.value,
				&destination_cell->id.value) &&
			SG_RuneModelStableIdEqual(&transition->source_phase.value,
				&published->source_phase.value) &&
			SG_RuneModelStableIdEqual(&transition->destination_phase.value,
				&published->destination_phase.value))
		{
			published->phase_transition = transition->id;
			break;
		}
	}
	if (fact->portal != SG_WATER_CAPABILITY_INDEX_NONE &&
		!SG_RuneModelStableIdValid(&published->phase_transition.value))
	{
		Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNRESOLVED_DESTINATION,
			fact->order, 0U);
		return 0;
	}
	published->portal = fact->portal == SG_WATER_CAPABILITY_INDEX_NONE ?
		SG_RUNE_PORTAL_REF_NONE : source->configuration->portals[fact->portal].id;
	published->kind = fact->kind;
	published->direction = fact->direction;
	published->source_medium = fact->source_medium;
	published->destination_medium = fact->destination_medium;
	published->source_contents = fact->source_contents;
	published->destination_contents = fact->destination_contents;
	published->current = fact->current;
	published->source_water_level = fact->source_water_level;
	published->destination_water_level = fact->destination_water_level;
	published->source_watertype = source_pose.water_type;
	published->destination_watertype = destination_pose.water_type;
	published->source_groundcontents = source_pose.support.contents;
	published->destination_groundcontents = destination_pose.support.contents;
	published->source_water_current = source_pose.water_type & HOST_CURRENT_MASK;
	published->source_ground_current = source_pose.support.contents &
		HOST_CURRENT_MASK;
	published->source_current = published->source_water_current |
		published->source_ground_current;
	published->destination_water_current = destination_pose.water_type &
		HOST_CURRENT_MASK;
	published->destination_ground_current = destination_pose.support.contents &
		HOST_CURRENT_MASK;
	published->destination_current = published->destination_water_current |
		published->destination_ground_current;
	/* Boundary witnesses may straddle a volume: preserve their full-hull map
	 * classifications alongside the independently proven Pmove result. */
	published->result_watertype = result_pose.water_type;
	published->result_groundcontents = result_pose.support.contents;
	published->result_water_current = result_pose.water_type & HOST_CURRENT_MASK;
	published->result_ground_current = result_pose.support.contents &
		HOST_CURRENT_MASK;
	published->result_current = published->result_water_current |
		published->result_ground_current;
	if (source_pose.water_level == 3U)
		published->source_environment |= SG_WATER_ENVIRONMENT_BREATH_LIMITED;
	if ((source_pose.water_type &
		(SG_HOST_CONTENTS_LAVA | SG_HOST_CONTENTS_SLIME)) != 0U)
		published->source_environment |= SG_WATER_ENVIRONMENT_HAZARDOUS;
	if (published->source_water_current != 0U)
		published->source_environment |= SG_WATER_ENVIRONMENT_WATER_CURRENT;
	if (published->source_ground_current != 0U)
		published->source_environment |= SG_WATER_ENVIRONMENT_GROUND_CURRENT;
	if (destination_pose.water_level == 3U)
		published->destination_environment |=
			SG_WATER_ENVIRONMENT_BREATH_LIMITED;
	if ((destination_pose.water_type &
		(SG_HOST_CONTENTS_LAVA | SG_HOST_CONTENTS_SLIME)) != 0U)
		published->destination_environment |= SG_WATER_ENVIRONMENT_HAZARDOUS;
	if (published->destination_water_current != 0U)
		published->destination_environment |=
			SG_WATER_ENVIRONMENT_WATER_CURRENT;
	if (published->destination_ground_current != 0U)
		published->destination_environment |=
			SG_WATER_ENVIRONMENT_GROUND_CURRENT;
	if (result_pose.water_level == 3U)
		published->result_environment |= SG_WATER_ENVIRONMENT_BREATH_LIMITED;
	if ((result_pose.water_type &
		(SG_HOST_CONTENTS_LAVA | SG_HOST_CONTENTS_SLIME)) != 0U)
		published->result_environment |= SG_WATER_ENVIRONMENT_HAZARDOUS;
	if (published->result_water_current != 0U)
		published->result_environment |= SG_WATER_ENVIRONMENT_WATER_CURRENT;
	if (published->result_ground_current != 0U)
		published->result_environment |= SG_WATER_ENVIRONMENT_GROUND_CURRENT;
	published->source_witness = fact->source_witness;
	published->boundary_witness = fact->boundary_witness;
	published->destination_witness = fact->destination_witness;
	published->direction_vector = fact->direction_vector;
	published->command_vector = fact->command_vector;
	published->source_velocity = fact->source_velocity;
	published->observed_displacement = fact->observed_displacement;
	published->observed_velocity = fact->observed_velocity;
	published->result_pm_flags = fact->result_pm_flags;
	published->result_support_model_index = fact->result_support_model_index;
	published->result_support_instance_id = fact->result_support_instance_id;
	published->result_water_type = fact->result_water_type;
	published->result_grounded = fact->result_grounded;
	published->result_water_level = fact->result_water_level;
	published->parameters = fact->parameters;
	published->flags = fact->flags;
	return 1;
}

static int PublicationValid(
	const sg_water_capability_publication_t *publication)
{
	const sg_water_capability_publication_info_t *info;

	if (!publication) return 0;
	info = &publication->info;
	return publication->state == PUBLICATION_STATE &&
		publication->state_inverse == ~PUBLICATION_STATE &&
		publication->self == publication &&
		IdentityValid(&info->identity) &&
		info->collision_law_id != 0U &&
		info->collision_law_id != UINT64_MAX &&
		info->pmove_law_id != 0U && info->pmove_law_id != UINT64_MAX &&
		info->gravity_law_id != 0U && info->gravity_law_id != UINT64_MAX &&
		info->host_law_identity != 0U &&
		info->host_law_identity != UINT64_MAX &&
		info->bsp_completeness.code == SG_BSP_COMPLETENESS_OK &&
		info->configuration_audit.code == SG_CONFIGURATION_AUDIT_OK &&
		info->semantics_audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_OK &&
		info->phase_count != 0U && publication->phases &&
		info->binding_count != 0U && publication->bindings &&
		(info->transition_count == 0U ||
		 (publication->transitions && publication->transition_evidence)) &&
		(info->fact_count == 0U || publication->facts) &&
		((info->state ==
				SG_WATER_CAPABILITY_PUBLICATION_COMPLETE &&
			info->fact_count != 0U) ||
		 (info->state ==
				SG_WATER_CAPABILITY_PUBLICATION_PROVEN_EMPTY &&
			info->fact_count == 0U && info->wet_region_count == 0U));
}

static sg_water_capability_publication_t *CreatePublication(audit_t *audit)
{
	const normalized_source_t *source = audit->source;
	sg_water_capability_publication_t *publication;
	uint32_t binding;
	uint32_t region = 0U;
	uint32_t fact;

	publication = calloc(1U, sizeof(*publication));
	if (!publication) return NULL;
	publication->phases = malloc((size_t)source->phase_count *
		sizeof(*publication->phases));
	publication->bindings = malloc((size_t)source->binding_count *
		sizeof(*publication->bindings));
	if (source->transition_count != 0U)
	{
		publication->transitions = malloc((size_t)source->transition_count *
			sizeof(*publication->transitions));
		publication->transition_evidence = malloc(
			(size_t)source->transition_count *
				sizeof(*publication->transition_evidence));
	}
	if (audit->fact_count != 0U)
		publication->facts = malloc((size_t)audit->fact_count *
			sizeof(*publication->facts));
	if (!publication->phases || !publication->bindings ||
		(source->transition_count != 0U &&
		 (!publication->transitions || !publication->transition_evidence)) ||
		(audit->fact_count != 0U && !publication->facts))
	{
		free(publication->phases);
		free(publication->bindings);
		free(publication->transitions);
		free(publication->transition_evidence);
		free(publication->facts);
		free(publication);
		return NULL;
	}
	memcpy(publication->phases, source->phases,
		(size_t)source->phase_count * sizeof(*publication->phases));
	if (source->transition_count != 0U)
	{
		memcpy(publication->transitions, source->transitions,
			(size_t)source->transition_count *
				sizeof(*publication->transitions));
		memcpy(publication->transition_evidence, source->transition_evidence,
			(size_t)source->transition_count *
				sizeof(*publication->transition_evidence));
	}
	for (binding = 0U; binding < source->binding_count; binding++)
	{
		while (region < source->semantics->region_count &&
			source->semantics->regions[region].id <
				source->bindings[binding].semantic_region_id)
			region++;
		if (region >= source->semantics->region_count ||
			source->semantics->regions[region].id !=
				source->bindings[binding].semantic_region_id ||
			source->bindings[binding].phase >= source->phase_count)
		{
			Fail(audit, SG_WATER_CAPABILITY_AUDIT_UNBOUND_PHASE, binding, 0U);
			free(publication->phases);
			free(publication->bindings);
			free(publication->transitions);
			free(publication->transition_evidence);
			free(publication->facts);
			free(publication);
			return NULL;
		}
		publication->bindings[binding].semantic_region_id =
			source->bindings[binding].semantic_region_id;
		publication->bindings[binding].cell = source->configuration->cells[
			source->semantics->regions[region].cell].id;
		publication->bindings[binding].phase = source->phases[
			source->bindings[binding].phase].id;
		publication->bindings[binding].mechanism_state_mask =
			source->bindings[binding].mechanism_state_mask;
	}
	for (fact = 0U; fact < audit->fact_count; fact++)
		if (!CopyPublishedFact(audit, &audit->facts[fact],
			&publication->facts[fact]))
		{
			free(publication->phases);
			free(publication->bindings);
			free(publication->transitions);
			free(publication->transition_evidence);
			free(publication->facts);
			free(publication);
			return NULL;
		}
	publication->info.state = audit->fact_count != 0U ?
		SG_WATER_CAPABILITY_PUBLICATION_COMPLETE :
		SG_WATER_CAPABILITY_PUBLICATION_PROVEN_EMPTY;
	publication->info.identity = source->authority->identity;
	publication->info.collision_law_id = source->host_law_view.collision_law_id;
	publication->info.pmove_law_id = source->host_law_view.pmove_law_id;
	publication->info.gravity_law_id = source->host_law_view.gravity_law_id;
	publication->info.host_law_identity = source->host_law_identity;
	publication->info.phase_count = source->phase_count;
	publication->info.binding_count = source->binding_count;
	publication->info.transition_count = source->transition_count;
	publication->info.wet_region_count = audit->result.wet_region_count;
	publication->info.boundary_count = audit->result.boundary_count;
	publication->info.obligation_count = audit->result.obligation_count;
	publication->info.fact_count = audit->fact_count;
	publication->info.proved_empty_count = audit->result.proved_empty_count;
	publication->info.host_pmove_frames = audit->result.host_pmove_frames;
	publication->info.lattice_solve_calls = audit->result.lattice_solve_calls;
	publication->info.lattice_constraints = audit->result.lattice_constraints;
	publication->info.same_cell_candidate_pairs =
		audit->result.same_cell_candidate_pairs;
	publication->info.lattice_maximum_binary_shift =
		audit->result.lattice_maximum_binary_shift;
	publication->info.bsp_completeness = audit->result.bsp_completeness;
	publication->info.configuration_audit = audit->result.configuration_audit;
	publication->info.semantics_audit = audit->result.semantics_audit;
	publication->state = PUBLICATION_STATE;
	publication->state_inverse = ~PUBLICATION_STATE;
	publication->self = publication;
	return publication;
}

static void ReleasePublicationPayload(
	sg_water_capability_publication_t *publication)
{
	if (!publication)
		return;
	publication->state = 0U;
	publication->state_inverse = 0U;
	publication->self = NULL;
	free(publication->phases);
	free(publication->bindings);
	free(publication->transitions);
	free(publication->transition_evidence);
	free(publication->facts);
	free(publication);
}

static sg_water_capability_publication_record_t *PublicationRecord(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication)
{
	sg_water_capability_publication_record_t *record;

	if (!owner || !publication)
		return NULL;
	for (record = owner->live; record; record = record->next)
		if (record->token == publication)
			return record;
	return NULL;
}

static int MintPublicationToken(
	sg_water_capability_publication_owner_t *owner,
	sg_water_capability_publication_t **token_out)
{
	if (!owner || !token_out || owner->next_token == 0U ||
		owner->next_token == UINTPTR_MAX)
		return 0;
	*token_out = (sg_water_capability_publication_t *)(uintptr_t)
		owner->next_token;
	owner->next_token++;
	return 1;
}

int SG_WaterCapabilityPublicationOwnerCreate(
	sg_water_capability_publication_owner_t **owner_out)
{
	sg_water_capability_publication_owner_t *owner;
	uint32_t attempt;

	if (!owner_out || *owner_out)
		return 0;
	owner = calloc(1U, sizeof(*owner));
	if (!owner)
		return 0;
	for (attempt = 0U; attempt < 32U; attempt++)
		if (SG_AuthorityEntropyFill(&owner->next_token,
			sizeof(owner->next_token)) && owner->next_token != 0U &&
			owner->next_token != UINTPTR_MAX)
		{
			*owner_out = owner;
			return 1;
		}
	free(owner);
	return 0;
}

int SG_WaterCapabilityPublicationIssue(
	sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_issue_source_t *source,
	const sg_water_capability_set_t *candidate,
	sg_water_capability_publication_t **publication_out,
	sg_water_capability_audit_result_t *audit_out)
{
	audit_t audit;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_publication_record_t *record = NULL;
	int success = 0;

	memset(&audit, 0, sizeof(audit));
	audit.input = source;
	audit.source = &audit.normalized;
	audit.result.code = SG_WATER_CAPABILITY_AUDIT_OK;
	audit.result.source_record = SG_WATER_CAPABILITY_INDEX_NONE;
	audit.result.candidate_record = SG_WATER_CAPABILITY_INDEX_NONE;
	if (!owner || !publication_out || *publication_out ||
		owner->live_count == UINT32_MAX)
	{
		audit.result.code = SG_WATER_CAPABILITY_AUDIT_INVALID_ARGUMENT;
		goto done;
	}
	if (!SourceValid(&audit, candidate))
	{
		if (audit.result.code == SG_WATER_CAPABILITY_AUDIT_OK)
			audit.result.code = SG_WATER_CAPABILITY_AUDIT_INVALID_ARGUMENT;
		goto done;
	}
	if (!PrepareIndexes(&audit) || !EnumerateLocalFacts(&audit) ||
		!EnumerateSameCellBoundaries(&audit) ||
		!EnumeratePortalBoundaries(&audit) || !FinalizeExpected(&audit) ||
		!CompareCandidate(&audit, candidate)) goto done;
	if (SG_HostLawPublicationRevalidateProduction(
		audit.source->host_laws).status != SG_HOST_LAW_OK)
	{
		Fail(&audit, SG_WATER_CAPABILITY_AUDIT_HOST_LAW, 0U, 0U);
		goto done;
	}
	publication = CreatePublication(&audit);
	if (!publication)
	{
		if (audit.result.code == SG_WATER_CAPABILITY_AUDIT_OK)
			Fail(&audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
		goto done;
	}
	record = calloc(1U, sizeof(*record));
	if (!record || !MintPublicationToken(owner, &record->token))
	{
		free(record);
		record = NULL;
		Fail(&audit, SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY, 0U, 0U);
		goto done;
	}
	record->payload = publication;
	record->next = owner->live;
	owner->live = record;
	owner->live_count++;
	*publication_out = record->token;
	publication = NULL;
	success = 1;
done:
	free(audit.binding_offsets);
	free(audit.cell_region_offsets);
	free(audit.facts);
	free(audit.normalized.bindings);
	ReleasePublicationPayload(publication);
	if (audit_out) *audit_out = audit.result;
	return success;
}

int SG_WaterCapabilityPublicationInfo(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication,
	sg_water_capability_publication_info_t *info_out)
{
	sg_water_capability_publication_record_t *record =
		PublicationRecord(owner, publication);

	if (!record || !PublicationValid(record->payload) || !info_out) return 0;
	*info_out = record->payload->info;
	return 1;
}

int SG_WaterCapabilityPublicationPhase(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_rune_phase_basis_t *phase_out)
{
	sg_water_capability_publication_record_t *record =
		PublicationRecord(owner, publication);

	if (!record || !PublicationValid(record->payload) || !phase_out ||
		index >= record->payload->info.phase_count) return 0;
	*phase_out = record->payload->phases[index];
	return 1;
}

int SG_WaterCapabilityPublicationBinding(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_water_capability_publication_binding_t *binding_out)
{
	sg_water_capability_publication_record_t *record =
		PublicationRecord(owner, publication);

	if (!record || !PublicationValid(record->payload) || !binding_out ||
		index >= record->payload->info.binding_count) return 0;
	*binding_out = record->payload->bindings[index];
	return 1;
}

int SG_WaterCapabilityPublicationTransition(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_rune_phase_transition_t *transition_out)
{
	sg_water_capability_publication_record_t *record =
		PublicationRecord(owner, publication);

	if (!record || !PublicationValid(record->payload) || !transition_out ||
		index >= record->payload->info.transition_count) return 0;
	*transition_out = record->payload->transitions[index];
	return 1;
}

int SG_WaterCapabilityPublicationTransitionEvidence(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_phase_catalog_transition_evidence_t *evidence_out)
{
	sg_water_capability_publication_record_t *record =
		PublicationRecord(owner, publication);

	if (!record || !PublicationValid(record->payload) || !evidence_out ||
		index >= record->payload->info.transition_count) return 0;
	*evidence_out = record->payload->transition_evidence[index];
	return 1;
}

int SG_WaterCapabilityPublicationFact(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_water_capability_publication_fact_t *fact_out)
{
	sg_water_capability_publication_record_t *record =
		PublicationRecord(owner, publication);

	if (!record || !PublicationValid(record->payload) || !fact_out ||
		index >= record->payload->info.fact_count) return 0;
	*fact_out = record->payload->facts[index];
	return 1;
}

void SG_WaterCapabilityPublicationDestroy(
	sg_water_capability_publication_owner_t *owner,
	sg_water_capability_publication_t *publication)
{
	sg_water_capability_publication_record_t **link;
	sg_water_capability_publication_record_t *record;

	if (!owner || !publication)
		return;
	for (link = &owner->live; *link && (*link)->token != publication;
		link = &(*link)->next)
		;
	record = *link;
	if (!record)
		return;
	*link = record->next;
	owner->live_count--;
	ReleasePublicationPayload(record->payload);
	free(record);
}

void SG_WaterCapabilityPublicationOwnerDestroy(
	sg_water_capability_publication_owner_t *owner)
{
	if (!owner)
		return;
	while (owner->live)
		SG_WaterCapabilityPublicationDestroy(owner, owner->live->token);
	free(owner);
}

const char *SG_WaterCapabilityAuditCodeString(
	sg_water_capability_audit_code_t code)
{
	static const char *const names[] = {
		"ok", "invalid argument", "source identity", "host law identity",
		"invalid phase", "omitted binding", "invented binding",
		"duplicate binding", "invalid candidate", "noncanonical order",
		"duplicate fact", "omitted fact", "invented fact",
		"fact disagreement", "host disagreement", "unresolved destination",
		"ambiguous destination", "domain coverage", "counter disagreement",
		"overflow", "out of memory", "host law",
		"BSP completeness", "configuration audit", "semantics audit",
		"phase catalog", "unbound phase", "unstable reference",
		"metric disagreement"
	};

	if (code < 0 || (size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}
