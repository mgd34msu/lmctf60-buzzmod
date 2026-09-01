#include "sg_rune_compact_movement_fields.h"

#include "sg_rune_compact_builder_owner.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TESTING)
static uint64_t sg_portal_merge_steps;
#define SG_PORTAL_MERGE_STEP() (sg_portal_merge_steps++)
#else
#define SG_PORTAL_MERGE_STEP() ((void)0)
#endif

#define PROFILE_GROUND 0U
#define PROFILE_WATER 1U
#define PROFILE_AIR 2U
#define PROFILE_HOOK_BLOCKED 3U
#define PROFILE_HOOK_VISIBLE 4U
#define PROFILE_HOOK_CONDITIONAL 5U
#define PROFILE_EXTERNAL 6U
#define PROFILE_HOOK_FLIGHT 7U
#define PROFILE_HOOK_COAST 8U
#define PROFILE_HOOK_COAST_GROUNDED 9U
#define PROFILE_ANGULAR 10U
#define PROFILE_BASE_COUNT 11U
#define HOOK_STANCE_COUNT 2U
#define HOOK_VISIBILITY_CLASS_COUNT 3U
/* CTF_HookPullVelocity groups [0,1) and [1,11), then uses the stock integer
 * intervals through 120 (steps 10, 20, 40, 20, 20) plus saturation.  This is
 * a law partition, not a work budget. */
#define HOOK_LADDER_CLAUSE_COUNT 113U

/* Owner reads populate this private construction context.  It never crosses
 * the public boundary, so detached views cannot be supplied by a caller. */
typedef struct sg_rune_compact_movement_bound_input_s
{
	const sg_rune_compact_builder_t *builder;
	const sg_host_law_construction_t *host_owner;
	const sg_rune_compact_geometry_t *geometry_owner;
	const sg_rune_compact_response_partition_t *response_owner;
	const sg_rune_compact_mechanisms_t *mechanisms_owner;
	const sg_rune_compact_static_materializer_t *static_owner;
	const sg_host_collision_scene_t *collision_scene;
	const sg_rune_compact_geometry_view_t *geometry;
	const sg_rune_compact_cell_t *cells;
	uint32_t cell_count;
	const sg_rune_compact_facet_t *facets;
	uint32_t facet_count;
	const sg_rune_compact_portal_t *portals;
	uint32_t portal_count;
	const sg_rune_compact_incidence_t *incidences;
	uint32_t incidence_count;
	const sg_rune_q8_vec3_t *vertices;
	uint32_t vertex_count;
	const sg_rune_compact_static_t *static_data;
	const sg_rune_compact_response_partition_view_t *response_partition;
	const sg_rune_compact_mechanisms_view_t *mechanisms;
	const sg_configuration_semantics_t *configuration_semantics;
	const sg_bsp_entity_semantics_t *entity_semantics;
	const sg_static_visibility_t *visibility;
	const sg_host_collision_authority_t *collision_authority;
	const sg_host_law_view_t *host_law;
	uint8_t reserved[3];
} sg_rune_compact_movement_bound_input_t;

#define sg_rune_compact_movement_fields_input_t \
	sg_rune_compact_movement_bound_input_t

typedef struct piecewise_clause_spec_s
{
	sg_rune_analytic_scalar_bits_t lower;
	sg_rune_analytic_scalar_bits_t upper;
	uint32_t function_spec;
	sg_rune_analytic_interval_ownership_t ownership;
} piecewise_clause_spec_t;

typedef struct function_spec_s
{
	sg_rune_compact_analytic_form_t form;
	sg_rune_analytic_output_meaning_t output;
	sg_rune_analytic_input_dimension_t dimensions[
		SG_RUNE_ANALYTIC_MAX_INPUTS];
	uint32_t input_count;
	uint8_t degree;
	uint8_t reserved[3];
	sg_rune_analytic_scalar_bits_t bias;
	sg_rune_analytic_scalar_bits_t slopes[SG_RUNE_ANALYTIC_MAX_INPUTS];
	sg_rune_analytic_scalar_bits_t *coefficients;
	sg_rune_analytic_scalar_bits_t initial;
	sg_rune_analytic_scalar_bits_t first_derivative;
	sg_rune_analytic_scalar_bits_t half_second_derivative;
	uint32_t value_count;
	uint32_t piecewise_default_spec;
	uint32_t piecewise_selector_input;
	uint32_t piecewise_clause_count;
	piecewise_clause_spec_t *piecewise_clauses;
} function_spec_t;

typedef struct analytic_workspace_s
{
	function_spec_t *specs;
	uint32_t *spec_order;
	uint32_t *spec_to_function;
	sg_rune_analytic_function_t *functions;
	sg_rune_analytic_input_dimension_t *input_dimensions;
	sg_rune_analytic_affine_t *affines;
	sg_rune_analytic_scalar_bits_t *affine_slopes;
	sg_rune_analytic_polynomial_t *polynomials;
	sg_rune_analytic_scalar_bits_t *polynomial_coefficients;
	sg_rune_analytic_ballistic_t *ballistics;
	sg_rune_analytic_piecewise_t *piecewise;
	sg_rune_analytic_piecewise_clause_t *piecewise_clauses;
	uint32_t spec_count;
	uint32_t spec_capacity;
	uint32_t spec_order_capacity;
	uint32_t spec_to_function_capacity;
	uint32_t function_count;
	uint32_t function_capacity;
	uint32_t input_dimension_count;
	uint32_t input_dimension_capacity;
	uint32_t affine_count;
	uint32_t affine_capacity;
	uint32_t affine_slope_count;
	uint32_t affine_slope_capacity;
	uint32_t polynomial_count;
	uint32_t polynomial_capacity;
	uint32_t polynomial_coefficient_count;
	uint32_t polynomial_coefficient_capacity;
	uint32_t ballistic_count;
	uint32_t ballistic_capacity;
	uint32_t piecewise_count;
	uint32_t piecewise_capacity;
	uint32_t piecewise_clause_count;
	uint32_t piecewise_clause_capacity;
	int allocation_failed;
} analytic_workspace_t;

typedef struct profile_s
{
	uint32_t *functions;
	uint32_t function_count;
	uint32_t function_capacity;
	int allocation_failed;
} profile_t;

typedef struct cell_response_ref_s
{
	uint32_t cell;
	uint8_t stance;
	uint8_t visibility_class;
	uint8_t reserved[2];
	sg_rune_compact_response_ref_t ref;
} cell_response_ref_t;

typedef enum hook_visibility_class_e
{
	HOOK_VISIBILITY_BLOCKED = 0,
	HOOK_VISIBILITY_VISIBLE,
	HOOK_VISIBILITY_CONDITIONAL
} hook_visibility_class_t;

typedef struct index_workspace_s
{
	uint32_t *region_by_cell;
	uint32_t *cell_portal_counts;
	uint32_t *cell_portal_offsets;
	uint32_t *cell_portals;
	uint32_t *transition_profiles;
	uint32_t *authority_transition_static;
	uint32_t *water_profiles;
	uint32_t *partition_by_cell;
	sg_rune_compact_response_ref_t *hook_refs;
	uint32_t *hook_ref_offsets;
	uint32_t hook_ref_count;
} index_workspace_t;

struct sg_rune_compact_movement_fields_s
{
	sg_rune_movement_capability_t *capabilities;
	sg_rune_compact_movement_state_t *states;
	sg_rune_compact_movement_fiber_t *fibers;
	uint8_t *hook_release_grounded;
	sg_rune_compact_movement_hook_target_t *hook_targets;
	sg_rune_analytic_function_index_t *fiber_function_refs;
	sg_rune_analytic_function_t *owned_functions;
	sg_rune_analytic_input_dimension_t *owned_input_dimensions;
	sg_rune_analytic_affine_t *owned_affines;
	sg_rune_analytic_scalar_bits_t *owned_affine_slopes;
	sg_rune_analytic_polynomial_t *owned_polynomials;
	sg_rune_analytic_scalar_bits_t *owned_polynomial_coefficients;
	sg_rune_analytic_ballistic_t *owned_ballistics;
	sg_rune_analytic_piecewise_t *owned_piecewise;
	sg_rune_analytic_piecewise_clause_t *owned_piecewise_clauses;
	sg_rune_compact_movement_angular_schedule_t *angular_schedules;
	sg_rune_compact_identity_t identity;
	uint32_t capability_count;
	uint32_t state_count;
	uint32_t fiber_count;
	uint32_t hook_target_count;
	uint32_t fiber_function_ref_count;
	uint32_t angular_schedule_count;
	sg_rune_compact_analytic_t analytic;
	sg_host_engine_pmove_abi_t pmove_abi;
	uint64_t pmove_behavior_fingerprint;
	uint64_t host_level_generation;
	uint64_t physics_abi_id;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	uint64_t hook_law_id;
	uint64_t mechanism_law_id;
};

typedef struct emit_state_s
{
	const sg_rune_compact_movement_fields_input_t *input;
	const index_workspace_t *index;
	const profile_t *profiles;
	uint32_t profile_count;
	sg_rune_compact_movement_fields_t *output;
	uint32_t attachment_cursor;
	uint32_t fiber_cursor;
	uint32_t hook_target_cursor;
	uint32_t function_cursor;
	uint32_t phase_function_count;
	int emit;
} emit_state_t;

typedef enum movement_family_e
{
	MOVEMENT_FAMILY_GROUND = 0,
	MOVEMENT_FAMILY_WATER,
	MOVEMENT_FAMILY_AIR,
	MOVEMENT_FAMILY_HOOK,
	MOVEMENT_FAMILY_MOVER,
	MOVEMENT_FAMILY_EXTERNAL_FORCE,
	MOVEMENT_FAMILY_COUNT
} movement_family_t;

static void SetError(sg_rune_compact_movement_fields_error_t *error,
	sg_rune_compact_movement_fields_error_code_t code, uint32_t record,
	uint64_t expected, uint64_t observed)
{
	if (error != NULL) {
		error->code = code;
		error->record = record;
		error->expected = expected;
		error->observed = observed;
	}
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int BytesZero(const void *value, size_t size)
{
	const unsigned char *bytes = value;
	size_t index;

	if (value == NULL)
		return 0;
	for (index = 0U; index < size; index++)
		if (bytes[index] != 0U)
			return 0;
	return 1;
}

static sg_rune_analytic_scalar_bits_t Scalar(float value)
{
	sg_rune_analytic_scalar_bits_t result;

	result.bits = FloatBits(value);
	return result;
}

static int ScalarValid(float value)
{
	return isfinite(value) && !(value == 0.0f && signbit(value));
}

static int PositiveFinite(float value)
{
	return ScalarValid(value) && value > 0.0f;
}

static int NonnegativeFinite(float value)
{
	return ScalarValid(value) && value >= 0.0f;
}

static int FiniteVector(const float value[3])
{
	return value != NULL && ScalarValid(value[0]) && ScalarValid(value[1]) &&
		ScalarValid(value[2]);
}

static int BoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (bounds == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int ClosedBoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (bounds == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int FloatBoundsValid(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	if (bounds == NULL || !FiniteVector(bounds->mins.value) ||
		!FiniteVector(bounds->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (hull == NULL || !FiniteVector(hull->mins.value) ||
		!FiniteVector(hull->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int CompactHullValid(const sg_rune_compact_hull_t *hull)
{
	uint32_t axis;

	if (hull == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int Binary32Canonical(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static float CompactScalar(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int CompactIdentityValid(const sg_rune_compact_identity_t *identity)
{
	const uint32_t physics[] = {
		identity == NULL ? 0U : identity->physics.gravity_bits,
		identity == NULL ? 0U : identity->physics.ground_acceleration_bits,
		identity == NULL ? 0U : identity->physics.air_acceleration_bits,
		identity == NULL ? 0U : identity->physics.water_acceleration_bits,
		identity == NULL ? 0U : identity->physics.hook_acceleration_bits,
		identity == NULL ? 0U : identity->physics.external_acceleration_bits,
		identity == NULL ? 0U : identity->physics.water_drag_bits,
		identity == NULL ? 0U : identity->physics.max_velocity_bits
	};
	uint32_t index;
	int digest_present = 0;

	if (identity == NULL)
		return 0;
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		if (identity->bsp_sha256[index] != 0U)
			digest_present = 1;
	if (!digest_present || identity->bsp_bytes == 0U ||
		identity->entity_semantics_id == 0U || identity->physics_abi_id == 0U ||
		identity->collision_law_id == 0U || identity->pmove_law_id == 0U ||
		identity->gravity_law_id == 0U || identity->hook_law_id == 0U ||
		identity->mechanism_law_id == 0U || identity->weapon_law_id == 0U ||
		identity->construction_id == 0U || identity->schema_id == 0U ||
		identity->producer_identity == 0U ||
		identity->source_counts.model_count == 0U ||
		identity->source_counts.leaf_count == 0U ||
		identity->source_counts.area_count == 0U ||
		identity->source_counts.plane_count == 0U ||
		identity->source_counts.entity_count == 0U ||
		(identity->source_counts.brush_count == 0U) !=
			(identity->source_counts.brush_side_count == 0U) ||
		!CompactHullValid(&identity->standing_hull) ||
		!CompactHullValid(&identity->crouching_hull))
		return 0;
	for (index = 0U; index < sizeof(physics) / sizeof(physics[0]); index++)
		if (!Binary32Canonical(physics[index]) ||
			CompactScalar(physics[index]) < 0.0f)
			return 0;
	return Binary32Canonical(identity->physics.gravity_bits) &&
		CompactScalar(identity->physics.gravity_bits) > 0.0f &&
		Binary32Canonical(identity->physics.max_velocity_bits) &&
		CompactScalar(identity->physics.max_velocity_bits) > 0.0f &&
		identity->physics.frame_ms != 0U && identity->physics.substep_ms != 0U &&
		identity->physics.substep_ms <= identity->physics.frame_ms;
}

/* Do not compare an identity with memcmp: the leading digest is followed by
 * naturally aligned fields and callers are not required to initialise padding
 * bytes.  This is the local boundary check used for every borrowed sealed
 * view consumed by this constructor. */
static int CompactIdentityEqual(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	uint32_t axis;
	uint32_t index;

	if (!CompactIdentityValid(left) || !CompactIdentityValid(right))
		return 0;
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		if (left->bsp_sha256[index] != right->bsp_sha256[index])
			return 0;
	if (left->bsp_bytes != right->bsp_bytes ||
		left->bsp_checksum != right->bsp_checksum ||
		left->entity_crc32 != right->entity_crc32 ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->collision_law_id != right->collision_law_id ||
		left->pmove_law_id != right->pmove_law_id ||
		left->gravity_law_id != right->gravity_law_id ||
		left->hook_law_id != right->hook_law_id ||
		left->mechanism_law_id != right->mechanism_law_id ||
		left->weapon_law_id != right->weapon_law_id ||
		left->construction_id != right->construction_id ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity ||
		left->weapon_profile_catalog_id != right->weapon_profile_catalog_id ||
		left->source_counts.model_count != right->source_counts.model_count ||
		left->source_counts.leaf_count != right->source_counts.leaf_count ||
		left->source_counts.area_count != right->source_counts.area_count ||
		left->source_counts.plane_count != right->source_counts.plane_count ||
		left->source_counts.brush_count != right->source_counts.brush_count ||
		left->source_counts.brush_side_count !=
			right->source_counts.brush_side_count ||
		left->source_counts.entity_count != right->source_counts.entity_count ||
		left->physics.frame_ms != right->physics.frame_ms ||
		left->physics.substep_ms != right->physics.substep_ms)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->standing_hull.mins.value[axis] !=
				right->standing_hull.mins.value[axis] ||
			left->standing_hull.maxs.value[axis] !=
				right->standing_hull.maxs.value[axis] ||
			left->crouching_hull.mins.value[axis] !=
				right->crouching_hull.mins.value[axis] ||
			left->crouching_hull.maxs.value[axis] !=
				right->crouching_hull.maxs.value[axis])
			return 0;
	return left->physics.gravity_bits == right->physics.gravity_bits &&
		left->physics.ground_acceleration_bits ==
			right->physics.ground_acceleration_bits &&
		left->physics.air_acceleration_bits ==
			right->physics.air_acceleration_bits &&
		left->physics.water_acceleration_bits ==
			right->physics.water_acceleration_bits &&
		left->physics.hook_acceleration_bits ==
			right->physics.hook_acceleration_bits &&
		left->physics.external_acceleration_bits ==
			right->physics.external_acceleration_bits &&
		left->physics.water_drag_bits == right->physics.water_drag_bits &&
		left->physics.max_velocity_bits == right->physics.max_velocity_bits;
}

static int ModelIdentityValid(const sg_rune_model_identity_t *identity)
{
	return identity != NULL && identity->bsp_content_id != 0U &&
		identity->entity_semantics_id != 0U && identity->physics_abi_id != 0U &&
		identity->source_set_identity != 0U && identity->schema_id != 0U &&
		identity->producer_identity != 0U &&
		HullValid(&identity->standing_hull) &&
		HullValid(&identity->crouching_hull) &&
		PositiveFinite(identity->physics.gravity) &&
		NonnegativeFinite(identity->physics.ground_acceleration) &&
		NonnegativeFinite(identity->physics.air_acceleration) &&
		NonnegativeFinite(identity->physics.water_acceleration) &&
		NonnegativeFinite(identity->physics.hook_acceleration) &&
		NonnegativeFinite(identity->physics.external_acceleration) &&
		NonnegativeFinite(identity->physics.water_drag) &&
		PositiveFinite(identity->physics.max_velocity) &&
		identity->physics.frame_ms != 0U && identity->physics.substep_ms != 0U &&
		identity->physics.substep_ms <= identity->physics.frame_ms;
}

static int ModelIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	uint32_t axis;

	if (!ModelIdentityValid(left) || !ModelIdentityValid(right) ||
		left->bsp_content_id != right->bsp_content_id ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->source_set_identity != right->source_set_identity ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(left->standing_hull.mins.value[axis]) !=
				FloatBits(right->standing_hull.mins.value[axis]) ||
			FloatBits(left->standing_hull.maxs.value[axis]) !=
				FloatBits(right->standing_hull.maxs.value[axis]) ||
			FloatBits(left->crouching_hull.mins.value[axis]) !=
				FloatBits(right->crouching_hull.mins.value[axis]) ||
			FloatBits(left->crouching_hull.maxs.value[axis]) !=
				FloatBits(right->crouching_hull.maxs.value[axis]))
			return 0;
	return FloatBits(left->physics.gravity) == FloatBits(right->physics.gravity) &&
		FloatBits(left->physics.ground_acceleration) ==
			FloatBits(right->physics.ground_acceleration) &&
		FloatBits(left->physics.air_acceleration) ==
			FloatBits(right->physics.air_acceleration) &&
		FloatBits(left->physics.water_acceleration) ==
			FloatBits(right->physics.water_acceleration) &&
		FloatBits(left->physics.hook_acceleration) ==
			FloatBits(right->physics.hook_acceleration) &&
		FloatBits(left->physics.external_acceleration) ==
			FloatBits(right->physics.external_acceleration) &&
		FloatBits(left->physics.water_drag) ==
			FloatBits(right->physics.water_drag) &&
		FloatBits(left->physics.max_velocity) ==
			FloatBits(right->physics.max_velocity) &&
		left->physics.frame_ms == right->physics.frame_ms &&
		left->physics.substep_ms == right->physics.substep_ms;
}

static int HostIdentityMatchesCompact(const sg_host_static_identity_t *host,
	const sg_rune_compact_identity_t *compact)
{
	uint32_t axis;

	if (host == NULL || compact == NULL || host->bsp_bytes != compact->bsp_bytes ||
		host->engine_checksum != compact->bsp_checksum ||
		host->entity_crc32 != compact->entity_crc32 ||
		host->physics_abi_id != compact->physics_abi_id ||
		memcmp(host->bsp_identity.bytes, compact->bsp_sha256,
			SG_BSP_CONTENT_ID_BYTES) != 0)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(host->standing_hull.mins.value[axis]) !=
			FloatBits((float)compact->standing_hull.mins.value[axis] / 8.0f) ||
			FloatBits(host->standing_hull.maxs.value[axis]) !=
			FloatBits((float)compact->standing_hull.maxs.value[axis] / 8.0f) ||
			FloatBits(host->crouching_hull.mins.value[axis]) !=
			FloatBits((float)compact->crouching_hull.mins.value[axis] / 8.0f) ||
			FloatBits(host->crouching_hull.maxs.value[axis]) !=
			FloatBits((float)compact->crouching_hull.maxs.value[axis] / 8.0f))
			return 0;
	return FloatBits(host->physics.gravity) == compact->physics.gravity_bits &&
		FloatBits(host->physics.ground_acceleration) ==
			compact->physics.ground_acceleration_bits &&
		FloatBits(host->physics.air_acceleration) ==
			compact->physics.air_acceleration_bits &&
		FloatBits(host->physics.water_acceleration) ==
			compact->physics.water_acceleration_bits &&
		FloatBits(host->physics.hook_acceleration) ==
			compact->physics.hook_acceleration_bits &&
		FloatBits(host->physics.external_acceleration) ==
			compact->physics.external_acceleration_bits &&
		FloatBits(host->physics.water_drag) == compact->physics.water_drag_bits &&
		FloatBits(host->physics.max_velocity) ==
			compact->physics.max_velocity_bits &&
		host->physics.frame_ms == compact->physics.frame_ms &&
		host->physics.substep_ms == compact->physics.substep_ms;
}

static int CompactCellSourceValid(const sg_rune_compact_cell_source_t *source,
	const sg_rune_compact_source_counts_t *counts)
{
	return source != NULL && counts != NULL &&
		source->model < counts->model_count &&
		source->leaf < counts->leaf_count &&
		source->area < counts->area_count && source->cluster >= -1 &&
		source->split_ordinal != UINT32_MAX;
}

static int CompactFacetSourceValid(const sg_rune_compact_source_t *source,
	uint32_t facet_index, const sg_rune_compact_source_counts_t *counts)
{
	if (source == NULL || counts == NULL ||
		(uint32_t)source->kind >= (uint32_t)SG_RUNE_COMPACT_SOURCE_KIND_COUNT)
		return 0;
	switch (source->kind) {
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		return source->value.domain.axis < 3U &&
			source->value.domain.maximum_side < 2U;
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		return source->value.bsp_plane.model < counts->model_count &&
			source->value.bsp_plane.leaf < counts->leaf_count &&
			source->value.bsp_plane.plane < counts->plane_count;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		return source->value.brush_side.model < counts->model_count &&
			source->value.brush_side.brush < counts->brush_count &&
			source->value.brush_side.brush_side < counts->brush_side_count &&
			source->value.brush_side.plane < counts->plane_count;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		return source->value.split.parent_facet.value < facet_index &&
			source->value.split.ordinal != UINT32_MAX;
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int ValidateGeometryCellMapping(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error);

static int PmoveAbiEqual(const sg_host_engine_pmove_abi_t *left,
	const sg_host_engine_pmove_abi_t *right)
{
	return left != NULL && right != NULL && left->version == right->version &&
		left->game_api_version == right->game_api_version &&
		left->import_size == right->import_size &&
		left->pmove_offset == right->pmove_offset &&
		left->pmove_size == right->pmove_size &&
		left->state_size == right->state_size &&
		left->command_size == right->command_size &&
		left->fraction_bits == right->fraction_bits &&
		left->substep_ms == right->substep_ms &&
		left->identity == right->identity;
}

static int PmoveAbiValid(const sg_host_engine_pmove_abi_t *abi,
	const sg_rune_physics_parameters_t *physics)
{
	return abi != NULL && physics != NULL && abi->version != 0U &&
		abi->game_api_version != 0U && abi->import_size != 0U &&
		abi->pmove_offset != 0U && abi->pmove_size != 0U &&
		abi->state_size == (uint32_t)sizeof(pmove_state_t) &&
		abi->command_size == (uint32_t)sizeof(usercmd_t) &&
		abi->fraction_bits != 0U && abi->substep_ms == physics->substep_ms &&
		abi->identity != 0U;
}

static int HostStaticIdentityEqual(const sg_host_static_identity_t *left,
	const sg_host_static_identity_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL || left->bsp_bytes != right->bsp_bytes ||
		left->engine_checksum != right->engine_checksum ||
		left->entity_crc32 != right->entity_crc32 ||
		left->host_physics_epoch != right->host_physics_epoch ||
		left->reserved != right->reserved ||
		left->physics_abi_id != right->physics_abi_id ||
		memcmp(left->bsp_identity.bytes, right->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) != 0)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(left->standing_hull.mins.value[axis]) !=
				FloatBits(right->standing_hull.mins.value[axis]) ||
			FloatBits(left->standing_hull.maxs.value[axis]) !=
				FloatBits(right->standing_hull.maxs.value[axis]) ||
			FloatBits(left->crouching_hull.mins.value[axis]) !=
				FloatBits(right->crouching_hull.mins.value[axis]) ||
			FloatBits(left->crouching_hull.maxs.value[axis]) !=
				FloatBits(right->crouching_hull.maxs.value[axis]))
			return 0;
	return FloatBits(left->physics.gravity) == FloatBits(right->physics.gravity) &&
		FloatBits(left->physics.ground_acceleration) ==
			FloatBits(right->physics.ground_acceleration) &&
		FloatBits(left->physics.air_acceleration) ==
			FloatBits(right->physics.air_acceleration) &&
		FloatBits(left->physics.water_acceleration) ==
			FloatBits(right->physics.water_acceleration) &&
		FloatBits(left->physics.hook_acceleration) ==
			FloatBits(right->physics.hook_acceleration) &&
		FloatBits(left->physics.external_acceleration) ==
			FloatBits(right->physics.external_acceleration) &&
		FloatBits(left->physics.water_drag) ==
			FloatBits(right->physics.water_drag) &&
		FloatBits(left->physics.max_velocity) ==
			FloatBits(right->physics.max_velocity) &&
		left->physics.frame_ms == right->physics.frame_ms &&
		left->physics.substep_ms == right->physics.substep_ms;
}

static int HookLawEqual(const sg_host_hook_law_t *left,
	const sg_host_hook_law_t *right)
{
	return left != NULL && right != NULL && left->version == right->version &&
		left->trace_mask == right->trace_mask &&
		left->muzzle_forward_offset == right->muzzle_forward_offset &&
		left->muzzle_right_offset == right->muzzle_right_offset &&
		left->muzzle_view_offset == right->muzzle_view_offset &&
		left->fire_speed == right->fire_speed &&
		left->pull_speed == right->pull_speed &&
		left->initial_damage == right->initial_damage &&
		left->attached_damage == right->attached_damage &&
		left->projectile_health == right->projectile_health &&
		left->attached_cadence_frames == right->attached_cadence_frames &&
		FloatBits(left->trace_epsilon) == FloatBits(right->trace_epsilon) &&
		left->no_grapple_damage == right->no_grapple_damage &&
		left->identity == right->identity &&
		FloatBits(left->near_bite_distance) ==
			FloatBits(right->near_bite_distance) &&
		FloatBits(left->near_bite_gravity_zero_distance) ==
			FloatBits(right->near_bite_gravity_zero_distance);
}

static int MechanismLawEqual(const sg_host_mechanism_law_t *left,
	const sg_host_mechanism_law_t *right)
{
	return left != NULL && right != NULL && left->version == right->version &&
		left->frame_ms == right->frame_ms &&
		left->move_equation_id == right->move_equation_id &&
		left->acceleration_equation_id == right->acceleration_equation_id &&
		left->door_equation_id == right->door_equation_id &&
		left->platform_equation_id == right->platform_equation_id &&
		left->trigger_equation_id == right->trigger_equation_id &&
		left->train_equation_id == right->train_equation_id &&
		left->identity == right->identity &&
		left->door_default_wait_ms == right->door_default_wait_ms &&
		left->platform_top_dwell_ms == right->platform_top_dwell_ms &&
		left->platform_top_touch_delay_ms == right->platform_top_touch_delay_ms &&
		left->door_trigger_debounce_ms == right->door_trigger_debounce_ms &&
		left->door_message_debounce_ms == right->door_message_debounce_ms &&
		left->train_blocked_debounce_ms == right->train_blocked_debounce_ms &&
		left->trigger_default_wait_ms == right->trigger_default_wait_ms &&
		left->trigger_remove_delay_ms == right->trigger_remove_delay_ms &&
		left->frame_schedule_ms == right->frame_schedule_ms &&
		FloatBits(left->door_default_speed) ==
			FloatBits(right->door_default_speed) &&
		FloatBits(left->door_rotating_default_speed) ==
			FloatBits(right->door_rotating_default_speed) &&
		FloatBits(left->button_default_speed) ==
			FloatBits(right->button_default_speed) &&
		FloatBits(left->door_default_lip) ==
			FloatBits(right->door_default_lip) &&
		FloatBits(left->button_default_lip) ==
			FloatBits(right->button_default_lip) &&
		FloatBits(left->platform_default_lip) ==
			FloatBits(right->platform_default_lip) &&
		FloatBits(left->platform_default_speed) ==
			FloatBits(right->platform_default_speed) &&
		FloatBits(left->platform_default_accel) ==
			FloatBits(right->platform_default_accel) &&
		FloatBits(left->platform_default_decel) ==
			FloatBits(right->platform_default_decel) &&
		FloatBits(left->train_default_speed) ==
			FloatBits(right->train_default_speed) &&
		left->train_default_damage == right->train_default_damage;
}

static int HostLawEquivalent(const sg_host_law_view_t *left,
	const sg_host_law_view_t *right)
{
	return left != NULL && right != NULL &&
		left->version == right->version && left->reserved == right->reserved &&
		left->collision_law_id == right->collision_law_id &&
		left->pmove_law_id == right->pmove_law_id &&
		left->gravity_law_id == right->gravity_law_id &&
		left->hook_law_id == right->hook_law_id &&
		left->mechanism_law_id == right->mechanism_law_id &&
		left->bsp_bytes == right->bsp_bytes &&
		memcmp(left->bsp_identity.bytes, right->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		HostStaticIdentityEqual(&left->static_identity,
			&right->static_identity) && PmoveAbiEqual(&left->pmove_abi,
			&right->pmove_abi) &&
		left->pmove_behavior_fingerprint == right->pmove_behavior_fingerprint &&
		FloatBits(left->airaccelerate) == FloatBits(right->airaccelerate) &&
		FloatBits(left->maxvelocity) == FloatBits(right->maxvelocity) &&
		left->movement_flags == right->movement_flags &&
		left->physics_flags == right->physics_flags &&
		left->hook_fire_speed == right->hook_fire_speed &&
		left->hook_pull_speed == right->hook_pull_speed &&
		left->hook_initial_damage == right->hook_initial_damage &&
		left->hook_attached_damage == right->hook_attached_damage &&
		left->hook_health == right->hook_health &&
		HookLawEqual(&left->hook, &right->hook) &&
		MechanismLawEqual(&left->mechanism, &right->mechanism);
}

static int GeometryViewsEqual(const sg_rune_compact_geometry_view_t *left,
	const sg_rune_compact_geometry_view_t *right)
{
	return left != NULL && right != NULL &&
		CompactIdentityEqual(&left->identity, &right->identity) &&
		left->cells == right->cells && left->cell_count == right->cell_count &&
		left->facets == right->facets && left->facet_count == right->facet_count &&
		left->incidences == right->incidences &&
		left->incidence_count == right->incidence_count &&
		left->cell_incidences == right->cell_incidences &&
		left->cell_incidence_count == right->cell_incidence_count &&
		left->vertices == right->vertices && left->vertex_count == right->vertex_count &&
		left->portals == right->portals && left->portal_count == right->portal_count &&
		left->source_surfaces == right->source_surfaces &&
		left->source_surface_count == right->source_surface_count &&
		left->source_surface_vertices == right->source_surface_vertices &&
		left->source_surface_vertex_count == right->source_surface_vertex_count &&
		left->compact_cells_for_configuration_cell ==
			right->compact_cells_for_configuration_cell &&
		left->compact_cells_for_configuration_cell_count ==
			right->compact_cells_for_configuration_cell_count &&
		left->configuration_cell_compact_cells ==
			right->configuration_cell_compact_cells &&
		left->configuration_cell_compact_cell_count ==
			right->configuration_cell_compact_cell_count;
}

static int ResponseViewsEqual(
	const sg_rune_compact_response_partition_view_t *left,
	const sg_rune_compact_response_partition_view_t *right)
{
	return left != NULL && right != NULL &&
		CompactIdentityEqual(&left->identity, &right->identity) &&
		left->source_fragments == right->source_fragments &&
		left->source_fragment_count == right->source_fragment_count &&
		left->source_halfspaces == right->source_halfspaces &&
		left->source_halfspace_count == right->source_halfspace_count &&
		left->target_patches == right->target_patches &&
		left->target_patch_count == right->target_patch_count &&
		left->target_vertices == right->target_vertices &&
		left->target_vertex_count == right->target_vertex_count &&
		left->splits == right->splits && left->split_count == right->split_count &&
		left->response_pairs == right->response_pairs &&
		left->response_pair_count == right->response_pair_count &&
		left->candidate_groups == right->candidate_groups &&
		left->candidate_group_count == right->candidate_group_count &&
		left->source_endpoint_groups == right->source_endpoint_groups &&
		left->source_endpoint_group_count == right->source_endpoint_group_count &&
		left->source_endpoint_members == right->source_endpoint_members &&
		left->source_endpoint_member_count == right->source_endpoint_member_count &&
		left->target_endpoint_groups == right->target_endpoint_groups &&
		left->target_endpoint_group_count == right->target_endpoint_group_count &&
		left->target_endpoint_members == right->target_endpoint_members &&
		left->target_endpoint_member_count == right->target_endpoint_member_count &&
		left->static_occluder_count == right->static_occluder_count &&
		left->compact_source_surfaces == right->compact_source_surfaces &&
		left->compact_source_surface_count ==
			right->compact_source_surface_count &&
		left->compact_source_surface_vertices ==
			right->compact_source_surface_vertices &&
		left->compact_source_surface_vertex_count ==
			right->compact_source_surface_vertex_count &&
		memcmp(&left->seal, &right->seal, sizeof(left->seal)) == 0;
}

static int MechanismsViewsEqual(const sg_rune_compact_mechanisms_view_t *left,
	const sg_rune_compact_mechanisms_view_t *right)
{
	return left != NULL && right != NULL &&
		CompactIdentityEqual(&left->identity, &right->identity) &&
		left->mechanisms == right->mechanisms &&
		left->mechanism_count == right->mechanism_count &&
		left->controllers == right->controllers &&
		left->controller_count == right->controller_count &&
		left->topology_edges == right->topology_edges &&
		left->topology_edge_count == right->topology_edge_count &&
		left->transitions == right->transitions &&
		left->transition_count == right->transition_count;
}

static int StaticViewsEqual(const sg_rune_compact_static_t *left,
	const sg_rune_compact_static_t *right)
{
	return left != NULL && right != NULL &&
		left->mechanisms == right->mechanisms &&
		left->mechanism_count == right->mechanism_count &&
		left->mechanism_controllers == right->mechanism_controllers &&
		left->mechanism_controller_count == right->mechanism_controller_count &&
		left->mechanism_edges == right->mechanism_edges &&
		left->mechanism_edge_count == right->mechanism_edge_count &&
		left->transitions == right->transitions &&
		left->transition_count == right->transition_count &&
		left->landmarks == right->landmarks &&
		left->landmark_count == right->landmark_count &&
		left->landmark_cells == right->landmark_cells &&
		left->landmark_cell_count == right->landmark_cell_count &&
		left->facet_annotations == right->facet_annotations &&
		left->facet_annotation_count == right->facet_annotation_count &&
		left->portal_mechanisms == right->portal_mechanisms &&
		left->portal_mechanism_count == right->portal_mechanism_count;
}

/* Public construction accepts owners only.  Every flattened pointer consumed
 * below is issued into this stack-bound context by a fresh owner read; caller
 * snapshots are deliberately ignored. */
static int BindOwners(
	const struct sg_rune_compact_movement_fields_input_s *input,
	sg_rune_compact_movement_fields_input_t *bound,
	sg_rune_compact_builder_owner_view_t *builder_view,
	sg_rune_compact_geometry_view_t *geometry_view,
	sg_rune_compact_response_partition_view_t *response_view,
	sg_rune_compact_mechanisms_view_t *mechanisms_view,
	sg_host_law_construction_view_t *host_view,
	sg_rune_compact_static_t *static_view,
	sg_rune_compact_identity_t *static_identity,
	sg_rune_compact_movement_fields_error_t *error)
{
	sg_host_law_result_t host_result;

	if (input == NULL || bound == NULL || builder_view == NULL ||
		geometry_view == NULL || response_view == NULL ||
		mechanisms_view == NULL || host_view == NULL || static_view == NULL ||
		static_identity == NULL || input->builder == NULL ||
		input->host_owner == NULL || input->geometry_owner == NULL ||
		input->response_owner == NULL || input->mechanisms_owner == NULL ||
		input->static_owner == NULL || input->collision_scene == NULL ||
		!SG_RuneCompactBuilderOwnerRead(input->builder, builder_view) ||
		!SG_RuneCompactGeometryRead(input->geometry_owner, geometry_view) ||
		!SG_RuneCompactResponsePartitionRead(input->response_owner,
			response_view) ||
		!SG_RuneCompactMechanismsRead(input->mechanisms_owner,
			mechanisms_view) ||
		!SG_RuneCompactStaticMaterializerReadBound(input->static_owner,
			static_identity, static_view)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}
	host_result = SG_HostLawConstructionRead(input->host_owner, host_view);
	if (host_result.status != SG_HOST_LAW_OK || host_view->current == 0U ||
		host_view->level_generation == 0U || builder_view->host_law == NULL ||
		builder_view->semantics == NULL || builder_view->entity_semantics == NULL ||
		builder_view->visibility == NULL || builder_view->collision == NULL ||
		!CompactIdentityEqual(&builder_view->identity, &geometry_view->identity) ||
		!CompactIdentityEqual(&builder_view->identity, &response_view->identity) ||
		!CompactIdentityEqual(&builder_view->identity, &mechanisms_view->identity) ||
		!CompactIdentityEqual(&builder_view->identity, static_identity) ||
		!HostStaticIdentityEqual(&host_view->host_static_identity,
			&builder_view->host_law->static_identity) ||
		!HostLawEquivalent(&host_view->laws, builder_view->host_law)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}

	memset(bound, 0, sizeof(*bound));
	bound->builder = input->builder;
	bound->host_owner = input->host_owner;
	bound->geometry_owner = input->geometry_owner;
	bound->response_owner = input->response_owner;
	bound->mechanisms_owner = input->mechanisms_owner;
	bound->static_owner = input->static_owner;
	bound->collision_scene = input->collision_scene;
	bound->geometry = geometry_view;
	bound->cells = geometry_view->cells;
	bound->cell_count = geometry_view->cell_count;
	bound->facets = geometry_view->facets;
	bound->facet_count = geometry_view->facet_count;
	bound->portals = geometry_view->portals;
	bound->portal_count = geometry_view->portal_count;
	bound->incidences = geometry_view->incidences;
	bound->incidence_count = geometry_view->incidence_count;
	bound->vertices = geometry_view->vertices;
	bound->vertex_count = geometry_view->vertex_count;
	bound->static_data = static_view;
	bound->response_partition = response_view;
	bound->mechanisms = mechanisms_view;
	bound->configuration_semantics = builder_view->semantics;
	bound->entity_semantics = builder_view->entity_semantics;
	bound->visibility = builder_view->visibility;
	bound->collision_authority = builder_view->collision;
	bound->host_law = builder_view->host_law;
	return 1;
}

static int ValidateOwnerBindings(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	sg_rune_compact_builder_owner_view_t builder_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_response_partition_view_t response_view;
	sg_rune_compact_mechanisms_view_t mechanisms_view;
	sg_host_law_construction_view_t host_view;
	sg_host_law_result_t host_result;

	if (input == NULL || input->builder == NULL || input->host_owner == NULL ||
		input->geometry_owner == NULL || input->response_owner == NULL ||
		input->mechanisms_owner == NULL ||
		!SG_RuneCompactBuilderOwnerRead(input->builder, &builder_view) ||
		!SG_RuneCompactGeometryRead(input->geometry_owner, &geometry_view) ||
		!SG_RuneCompactResponsePartitionRead(input->response_owner,
			&response_view) ||
		!SG_RuneCompactMechanismsRead(input->mechanisms_owner,
			&mechanisms_view)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}
	host_result = SG_HostLawConstructionRead(input->host_owner, &host_view);
	if (host_result.status != SG_HOST_LAW_OK || host_view.current == 0U ||
		host_view.level_generation == 0U || input->geometry == NULL ||
		input->response_partition == NULL || input->mechanisms == NULL ||
		!GeometryViewsEqual(input->geometry, &geometry_view) ||
		!ResponseViewsEqual(input->response_partition, &response_view) ||
		!MechanismsViewsEqual(input->mechanisms, &mechanisms_view) ||
		!CompactIdentityEqual(&builder_view.identity, &input->geometry->identity) ||
		builder_view.host_law != input->host_law ||
		builder_view.semantics != input->configuration_semantics ||
		builder_view.entity_semantics != input->entity_semantics ||
		builder_view.visibility != input->visibility ||
		builder_view.collision != input->collision_authority ||
		!HostStaticIdentityEqual(&host_view.host_static_identity,
			&input->host_law->static_identity) ||
		!HostLawEquivalent(&host_view.laws, input->host_law) ||
		!PmoveAbiValid(&input->host_law->pmove_abi,
			&input->host_law->static_identity.physics) ||
		input->host_law->pmove_behavior_fingerprint == 0U ||
		input->host_law->pmove_behavior_fingerprint !=
			input->host_law->pmove_abi.identity) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}
	return 1;
}

static int ValidateIdentityBinding(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_compact_geometry_view_t *geometry;
	const sg_rune_compact_identity_t *identity;
	const sg_configuration_semantics_t *configuration;
	const sg_static_visibility_t *visibility;
	uint32_t index;

	if (input == NULL || input->geometry == NULL) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}
	geometry = input->geometry;
	identity = &geometry->identity;
	configuration = input->configuration_semantics;
	visibility = input->visibility;
	if (!CompactIdentityValid(identity) ||
		input->cells != geometry->cells || input->cell_count != geometry->cell_count ||
		input->facets != geometry->facets || input->facet_count != geometry->facet_count ||
		input->incidences != geometry->incidences ||
		input->incidence_count != geometry->incidence_count ||
		input->portals != geometry->portals || input->portal_count != geometry->portal_count ||
		input->cell_count == 0U || input->facet_count == 0U ||
		input->incidence_count == 0U ||
		geometry->cell_incidence_count == 0U ||
		geometry->cell_incidences == NULL || geometry->vertices == NULL ||
		geometry->vertex_count == 0U || !ValidateGeometryCellMapping(input, error)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY, 0U, 1U,
			0U);
		return 0;
	}
	if (configuration == NULL || visibility == NULL || input->host_law == NULL ||
		!ModelIdentityValid(&configuration->identity) ||
		!ModelIdentityValid(&visibility->identity) ||
		!ModelIdentityEqual(&configuration->identity, &visibility->identity) ||
		!HostIdentityMatchesCompact(&input->host_law->static_identity, identity) ||
		memcmp(input->host_law->bsp_identity.bytes,
			input->host_law->static_identity.bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) != 0 ||
		input->host_law->bsp_bytes != input->host_law->static_identity.bsp_bytes ||
		input->host_law->collision_law_id != identity->collision_law_id ||
		input->host_law->pmove_law_id != identity->pmove_law_id ||
		input->host_law->gravity_law_id != identity->gravity_law_id ||
		input->host_law->hook_law_id != identity->hook_law_id ||
		input->host_law->mechanism_law_id != identity->mechanism_law_id ||
		input->host_law->hook.identity != identity->hook_law_id ||
		input->host_law->mechanism.identity != identity->mechanism_law_id) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}
	for (index = 0U; index < input->cell_count; index++)
		if (!CompactCellSourceValid(&input->cells[index].source,
				&identity->source_counts)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY, index,
				identity->source_counts.model_count,
				input->cells[index].source.model);
			return 0;
		}
	for (index = 0U; index < input->facet_count; index++)
		if (!CompactFacetSourceValid(&input->facets[index].source, index,
				&identity->source_counts)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY, index, 1U,
				0U);
			return 0;
		}
	return 1;
}

static int StancesValid(sg_rune_stance_validity_t stances)
{
	return stances != 0U &&
		(stances & (sg_rune_stance_validity_t)~SG_RUNE_STANCE_VALID_ALL) == 0U;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

/* Geometry is allowed to split one configuration cell into several compact
 * cells (for example, the standing/crouching overlay).  Configuration and
 * visibility records continue to name the configuration cell, while all
 * movement attachments name the compact cell.  Keep that translation in one
 * place so no later stage silently assumes ordinal correspondence. */
static uint32_t ConfigurationCellCount(
	const sg_rune_compact_movement_fields_input_t *input)
{
	if (input == NULL || input->geometry == NULL)
		return 0U;
	return input->geometry->compact_cells_for_configuration_cell == NULL ?
		input->cell_count :
		input->geometry->compact_cells_for_configuration_cell_count;
}

static int ConfigurationCellSpan(
	const sg_rune_compact_movement_fields_input_t *input,
	uint32_t configuration_cell,
	const sg_rune_compact_geometry_cell_span_t **span_out)
{
	const sg_rune_compact_geometry_view_t *geometry;

	if (input == NULL || input->geometry == NULL || span_out == NULL)
		return 0;
	geometry = input->geometry;
	if (geometry->compact_cells_for_configuration_cell == NULL) {
		if (configuration_cell >= input->cell_count)
			return 0;
		*span_out = NULL;
		return 1;
	}
	if (configuration_cell >=
		geometry->compact_cells_for_configuration_cell_count)
		return 0;
	*span_out = &geometry->compact_cells_for_configuration_cell[
		configuration_cell];
	return SpanWithin((*span_out)->first, (*span_out)->count,
		geometry->configuration_cell_compact_cell_count) &&
		((*span_out)->count == 0U ||
			geometry->configuration_cell_compact_cells != NULL);
}

static int CompactCellMappedToConfigurationCell(
	const sg_rune_compact_movement_fields_input_t *input,
	uint32_t configuration_cell, uint32_t compact_cell)
{
	const sg_rune_compact_geometry_cell_span_t *span;
	uint32_t offset;

	if (input == NULL || compact_cell >= input->cell_count ||
		!ConfigurationCellSpan(input, configuration_cell, &span))
		return 0;
	if (span == NULL)
		return configuration_cell == compact_cell;
	for (offset = 0U; offset < span->count; offset++)
		if (input->geometry->configuration_cell_compact_cells[
			span->first + offset].value == compact_cell)
			return 1;
	return 0;
}

static int ValidateGeometryCellMapping(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_compact_geometry_view_t *geometry = input->geometry;
	uint32_t configuration_cell;

	if ((geometry->compact_cells_for_configuration_cell == NULL &&
			(geometry->compact_cells_for_configuration_cell_count != 0U ||
				geometry->configuration_cell_compact_cell_count != 0U ||
				geometry->configuration_cell_compact_cells != NULL)) ||
		(geometry->compact_cells_for_configuration_cell != NULL &&
			geometry->compact_cells_for_configuration_cell_count == 0U) ||
		(geometry->configuration_cell_compact_cells == NULL &&
			geometry->configuration_cell_compact_cell_count != 0U)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY, 0U, 1U,
			0U);
		return 0;
	}
	if (geometry->compact_cells_for_configuration_cell == NULL)
		return 1;
	for (configuration_cell = 0U;
		configuration_cell < geometry->compact_cells_for_configuration_cell_count;
		configuration_cell++) {
		const sg_rune_compact_geometry_cell_span_t *span =
			&geometry->compact_cells_for_configuration_cell[configuration_cell];
		uint32_t offset;

		if (!SpanWithin(span->first, span->count,
			geometry->configuration_cell_compact_cell_count)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				configuration_cell, 1U, span->count);
			return 0;
		}
		for (offset = 0U; offset < span->count; offset++) {
			const uint32_t reference = span->first + offset;
			const uint32_t compact_cell =
				geometry->configuration_cell_compact_cells[reference].value;

			if (compact_cell >= input->cell_count ||
				(offset != 0U && geometry->configuration_cell_compact_cells[
					reference - 1U].value >= compact_cell)) {
				SetError(error,
					SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
					configuration_cell, input->cell_count, compact_cell);
				return 0;
			}
		}
	}
	return 1;
}

static int AddU32(uint32_t left, uint32_t right, uint32_t *result)
{
	if (result == NULL || right > UINT32_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static int AddU64(uint64_t left, uint64_t right, uint64_t *result)
{
	if (result == NULL || right > UINT64_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static int AddSize(size_t count, size_t element_size, size_t *bytes_out)
{
	if (bytes_out == NULL ||
		(element_size != 0U && count > SIZE_MAX / element_size))
		return 0;
	*bytes_out = count * element_size;
	return 1;
}

static void *AllocateArray(uint32_t count, size_t element_size)
{
	size_t bytes;

	if (count == 0U || !AddSize((size_t)count, element_size, &bytes))
		return NULL;
	return calloc(1U, bytes);
}

/* Construction is deliberately not bounded by a private work-array cap.  A
 * map can add as many deduplicated analytic definitions as its accepted wire
 * counts permit; allocation failure is the only storage stop.  Growing with
 * calloc (rather than realloc) keeps the ownership and the fault-injection
 * path explicit. */
static int GrowArray(void **array, uint32_t *capacity, uint32_t needed,
	size_t element_size)
{
	uint32_t new_capacity;
	size_t bytes;
	void *replacement;

	if (array == NULL || capacity == NULL || element_size == 0U)
		return 0;
	if (needed <= *capacity)
		return 1;
	new_capacity = *capacity == 0U ? 8U : *capacity;
	while (new_capacity < needed) {
		if (new_capacity > UINT32_MAX / 2U) {
			new_capacity = needed;
			break;
		}
		new_capacity *= 2U;
	}
	if (!AddSize((size_t)new_capacity, element_size, &bytes))
		return 0;
	replacement = calloc(1U, bytes);
	if (replacement == NULL)
		return 0;
	if (*array != NULL && *capacity != 0U)
		memcpy(replacement, *array,
			(size_t)(*capacity) * element_size);
	free(*array);
	*array = replacement;
	*capacity = new_capacity;
	return 1;
}

static void ProfileDestroy(profile_t *profile)
{
	if (profile == NULL)
		return;
	free(profile->functions);
	memset(profile, 0, sizeof(*profile));
}

static void ProfilesDestroy(profile_t *profiles, uint32_t count)
{
	uint32_t index;

	if (profiles == NULL)
		return;
	for (index = 0U; index < count; index++)
		ProfileDestroy(&profiles[index]);
	free(profiles);
}

static int ProfileAppend(profile_t *profile, uint32_t function)
{
	uint32_t next_count;

	if (profile == NULL || !AddU32(profile->function_count, 1U,
		&next_count) || !GrowArray((void **)&profile->functions,
		&profile->function_capacity, next_count, sizeof(*profile->functions)))
	{
		if (profile != NULL)
			profile->allocation_failed = 1;
		return 0;
	}
	profile->functions[profile->function_count] = function;
	profile->function_count = next_count;
	return 1;
}

static int CanonicalizeProfile(const analytic_workspace_t *workspace,
	profile_t *profile)
{
	uint32_t index;

	if (workspace == NULL || profile == NULL)
		return 0;
	/* A lifecycle record is the phase/event/capability/outcome instance.  Its
	 * attached functions are the one value for each output meaning in that
	 * instance, so project them by meaning and reject duplicate meanings. */
	for (index = 0U; index < profile->function_count; index++)
		if (profile->functions[index] >= workspace->spec_count)
			return 0;
	for (index = 1U; index < profile->function_count; index++) {
		const uint32_t function = profile->functions[index];
		const sg_rune_analytic_output_meaning_t output =
			workspace->specs[function].output;
		uint32_t cursor = index;

		while (cursor != 0U && workspace->specs[
			profile->functions[cursor - 1U]].output > output) {
			profile->functions[cursor] = profile->functions[cursor - 1U];
			cursor--;
		}
		profile->functions[cursor] = function;
	}
	for (index = 1U; index < profile->function_count; index++)
		if (workspace->specs[profile->functions[index - 1U]].output ==
			workspace->specs[profile->functions[index]].output)
			return 0;
	return 1;
}

static int CanonicalizeProfiles(const analytic_workspace_t *workspace,
	profile_t *profiles, uint32_t profile_count)
{
	uint32_t profile;

	if (workspace == NULL || profiles == NULL)
		return 0;
	for (profile = 0U; profile < profile_count; profile++)
		if (!CanonicalizeProfile(workspace, &profiles[profile]))
			return 0;
	return 1;
}

static void WorkspaceDestroy(analytic_workspace_t *workspace)
{
	uint32_t index;

	if (workspace == NULL)
		return;
	for (index = 0U; index < workspace->spec_count; index++) {
		free(workspace->specs[index].coefficients);
		free(workspace->specs[index].piecewise_clauses);
	}
	free(workspace->specs);
	free(workspace->spec_order);
	free(workspace->spec_to_function);
	free(workspace->functions);
	free(workspace->input_dimensions);
	free(workspace->affines);
	free(workspace->affine_slopes);
	free(workspace->polynomials);
	free(workspace->polynomial_coefficients);
	free(workspace->ballistics);
	free(workspace->piecewise);
	free(workspace->piecewise_clauses);
	memset(workspace, 0, sizeof(*workspace));
}

static int WorkspaceGrow(analytic_workspace_t *workspace, void **array,
	uint32_t *capacity, uint32_t needed, size_t element_size)
{
	if (workspace == NULL || !GrowArray(array, capacity, needed, element_size)) {
		if (workspace != NULL)
			workspace->allocation_failed = 1;
		return 0;
	}
	return 1;
}

static int RegionFlagsValid(uint32_t flags)
{
	const uint32_t known = SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
		SG_CONFIGURATION_SEMANTIC_REGION_SLIME |
		SG_CONFIGURATION_SEMANTIC_REGION_HAZARD |
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT |
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;

	return (flags & ~known) == 0U;
}

static int GetPortalCells(
	const sg_rune_compact_movement_fields_input_t *input, uint32_t portal,
	uint32_t *negative_out, uint32_t *positive_out)
{
	const sg_rune_compact_portal_t *value;
	uint32_t negative;
	uint32_t positive;

	if (input == NULL || negative_out == NULL || positive_out == NULL ||
		portal >= input->portal_count)
		return 0;
	value = &input->portals[portal];
	if (value->negative_incidence.value >= input->incidence_count ||
		value->positive_incidence.value >= input->incidence_count)
		return 0;
	negative = input->incidences[value->negative_incidence.value].cell.value;
	positive = input->incidences[value->positive_incidence.value].cell.value;
	if (negative >= input->cell_count || positive >= input->cell_count ||
		negative == positive)
		return 0;
	*negative_out = negative;
	*positive_out = positive;
	return 1;
}

static int IsSupported(const sg_configuration_semantic_region_t *region)
{
	return region != NULL &&
		(region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;
}

static int IsWater(const sg_configuration_semantic_region_t *region,
	const sg_rune_compact_cell_t *cell)
{
	const sg_rune_compact_contents_mask_t contents =
		cell == NULL ? 0U : cell->contents;
	const uint32_t semantic_water = SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
		SG_CONFIGURATION_SEMANTIC_REGION_SLIME;
	const uint32_t compact_water = SG_RUNE_COMPACT_CONTENTS_WATER |
		SG_RUNE_COMPACT_CONTENTS_LAVA | SG_RUNE_COMPACT_CONTENTS_SLIME;

	return (region != NULL && (region->flags & semantic_water) != 0U) ||
		(contents & compact_water) != 0U;
}

static void IndexDestroy(index_workspace_t *index)
{
	if (index == NULL)
		return;
	free(index->region_by_cell);
	free(index->cell_portal_counts);
	free(index->cell_portal_offsets);
	free(index->cell_portals);
	free(index->transition_profiles);
	free(index->authority_transition_static);
	free(index->water_profiles);
	free(index->partition_by_cell);
	free(index->hook_refs);
	free(index->hook_ref_offsets);
	memset(index, 0, sizeof(*index));
}

static int PrefixOffsets(const uint32_t *counts, uint32_t count,
	uint32_t total, uint32_t **offsets_out, uint32_t **items_out)
{
	uint32_t *offsets;
	uint32_t *items;
	uint32_t index;

	if ((counts == NULL && count != 0U) || offsets_out == NULL ||
		items_out == NULL)
		return 0;
	offsets = AllocateArray(count + 1U, sizeof(*offsets));
	if (offsets == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (!AddU32(offsets[index], counts[index], &offsets[index + 1U])) {
			free(offsets);
			return 0;
		}
	if (offsets[count] != total) {
		free(offsets);
		return 0;
	}
	items = AllocateArray(total, sizeof(*items));
	if (total != 0U && items == NULL) {
		free(offsets);
		return 0;
	}
	*offsets_out = offsets;
	*items_out = items;
	return 1;
}

static int FloatBoundsContain(const sg_rune_bounds_t *bounds,
	const float point[3])
{
	uint32_t axis;

	if (bounds == NULL || point == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < bounds->mins.value[axis] ||
			point[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int Q8BoundsContainPoint(const sg_rune_q8_bounds_t *bounds,
	const float point[3])
{
	uint32_t axis;

	if (bounds == NULL || point == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < (float)bounds->mins.value[axis] * 0.125f ||
			point[axis] >= (float)bounds->maxs.value[axis] * 0.125f)
			return 0;
	return 1;
}

static int Q8BoundsOverlapFloat(const sg_rune_q8_bounds_t *left,
	const sg_rune_bounds_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if ((float)left->maxs.value[axis] * 0.125f <=
				right->mins.value[axis] ||
			right->maxs.value[axis] <=
			(float)left->mins.value[axis] * 0.125f)
			return 0;
	return 1;
}

static int Q8VectorInsideBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (point == NULL || !BoundsValid(bounds))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

/* Polygon vertices are boundary samples, so a vertex may legitimately lie on
 * the closed max edge of its quantized patch bounds.  Cell witnesses use the
 * half-open helper above; target-patch geometry uses this inclusive form. */
static int Q8VectorInsideClosedBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (point == NULL || !ClosedBoundsValid(bounds))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int ResponseTraceValid(const sg_host_collision_trace_t *trace)
{
	return trace != NULL && isfinite(trace->fraction) &&
		trace->fraction >= 0.0f && trace->fraction <= 1.0f &&
		FiniteVector(trace->end) && FiniteVector(trace->plane.normal) &&
		ScalarValid(trace->plane.distance);
}

static uint32_t ResponseEndpointGroupForMember(
	const sg_rune_compact_response_endpoint_group_t *groups,
	uint32_t group_count, const uint32_t *members, uint32_t member)
{
	uint32_t group;

	if (groups == NULL || members == NULL)
		return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	for (group = 0U; group < group_count; group++) {
		const uint32_t first = groups[group].first_member;
		const uint32_t count = groups[group].member_count;
		uint32_t cursor;

		for (cursor = first; cursor < first + count; cursor++)
			if (members[cursor] == member)
				return group;
	}
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

static int ValidateResponseEndpointGroups(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_endpoint_group_t *groups,
	uint32_t group_count, const uint32_t *members, uint32_t member_count,
	int source)
{
	uint32_t group;
	uint32_t cursor = 0U;
	uint32_t limit = source ? view->source_fragment_count :
		view->target_patch_count;
	uint32_t previous_cluster = 0U;
	uint32_t previous_area = 0U;

	(void)input;

	if (group_count != 0U && groups == NULL)
		return 0;
	if (member_count != 0U && members == NULL)
		return 0;
	for (group = 0U; group < group_count; group++) {
		const sg_rune_compact_response_endpoint_group_t *record =
			&groups[group];
		uint32_t member;

		if (record->first_member != cursor || record->member_count == 0U ||
			!SpanWithin(record->first_member, record->member_count,
				member_count) ||
			(group != 0U && (record->bsp_cluster < previous_cluster ||
				(record->bsp_cluster == previous_cluster &&
					record->bsp_area <= previous_area))))
			return 0;
		previous_cluster = record->bsp_cluster;
		previous_area = record->bsp_area;
		for (member = 0U; member < record->member_count; member++) {
			const uint32_t value = members[cursor + member];

			if (value >= limit ||
				(member != 0U && value <= members[cursor + member - 1U]))
				return 0;
			if (source) {
				const sg_rune_compact_response_fragment_t *fragment =
					&view->source_fragments[value];

				if (fragment->bsp_cluster != record->bsp_cluster ||
					fragment->bsp_area != record->bsp_area)
					return 0;
			} else {
				const sg_rune_compact_response_patch_t *patch =
					&view->target_patches[value];

				if ((patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U ||
					patch->bsp_cluster != record->bsp_cluster ||
					patch->bsp_area != record->bsp_area)
					return 0;
			}
		}
		cursor += record->member_count;
	}
	return cursor == member_count;
}

static const sg_rune_compact_response_candidate_group_t *
ResponseCandidateForGroups(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source_group, uint32_t target_group)
{
	uint32_t index;

	if (view == NULL)
		return NULL;
	for (index = 0U; index < view->candidate_group_count; index++) {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&view->candidate_groups[index];

		if (candidate->source_group == source_group &&
			candidate->target_group == target_group)
			return candidate;
	}
	return NULL;
}

static int ResponseCandidateValid(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_candidate_group_t *candidate)
{
	const sg_rune_compact_response_endpoint_group_t *source;
	const sg_rune_compact_response_endpoint_group_t *target;
	uint32_t expected_area_state;
	sg_rune_compact_static_visibility_reason_t expected_reason;

	if (view == NULL || candidate == NULL || candidate->source_group >=
		view->source_endpoint_group_count || candidate->target_group >=
		view->target_endpoint_group_count)
		return 0;
	source = &view->source_endpoint_groups[candidate->source_group];
	target = &view->target_endpoint_groups[candidate->target_group];
	expected_area_state = source->bsp_area !=
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE && target->bsp_area !=
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
		source->bsp_area != target->bsp_area;
	expected_reason = (target->flags &
		SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U ?
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL :
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	return candidate->classification ==
			SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL &&
		candidate->reason == expected_reason &&
		candidate->requires_exact_ray == 1U &&
		candidate->requires_area_state == expected_area_state &&
		candidate->reserved[0] == 0U && candidate->reserved[1] == 0U &&
		(candidate->relation_flags &
			(sg_rune_compact_static_relation_flags_t)
				~SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) == 0U &&
		((candidate->relation_flags &
			SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) != 0U) ==
			(expected_area_state != 0U);
}

static int ResponseCertificateValid(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_response_partition_view_t *view,
	const sg_static_visibility_class_t classification,
	const sg_static_visibility_reason_t reason, uint32_t first_hit,
	uint32_t requires_exact_ray, uint32_t requires_area_state,
	sg_rune_compact_response_certificate_t certificate,
	sg_rune_compact_static_relation_flags_t relation_flags,
	uint32_t source_group, uint32_t target_group)
{
	const sg_rune_compact_response_candidate_group_t *candidate;
	sg_rune_compact_static_relation_flags_t certificate_flag = 0U;

	if (input == NULL || view == NULL || source_group >=
		view->source_endpoint_group_count || target_group >=
		view->target_endpoint_group_count || (uint32_t)classification >
		(uint32_t)SG_STATIC_VISIBILITY_CONDITIONAL || (uint32_t)reason >
		(uint32_t)SG_STATIC_VISIBILITY_REASON_SKY || requires_exact_ray > 1U ||
		requires_area_state > 1U || (relation_flags & (sg_rune_compact_static_relation_flags_t)
		~SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN) != 0U ||
		(first_hit != SG_STATIC_VISIBILITY_INDEX_NONE &&
			first_hit >= input->visibility->occluder_count))
		return 0;
	candidate = ResponseCandidateForGroups(view, source_group, target_group);
	if (!ResponseCandidateValid(view, candidate) ||
		classification != (sg_static_visibility_class_t)
			candidate->classification ||
		reason != (sg_static_visibility_reason_t)candidate->reason ||
		requires_exact_ray != candidate->requires_exact_ray ||
		requires_area_state != candidate->requires_area_state)
		return 0;
	switch (certificate) {
	case SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT:
		certificate_flag = SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
		if (first_hit != SG_STATIC_VISIBILITY_INDEX_NONE)
			return 0;
		break;
	case SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT:
		certificate_flag =
			SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
		if (first_hit == SG_STATIC_VISIBILITY_INDEX_NONE || first_hit >=
			view->static_occluder_count)
			return 0;
		break;
	case SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY:
		if (first_hit != SG_STATIC_VISIBILITY_INDEX_NONE)
			return 0;
		break;
	case SG_RUNE_COMPACT_RESPONSE_CERTIFICATE_COUNT:
		return 0;
	}
	return relation_flags ==
		(candidate->relation_flags | certificate_flag);
}

static const sg_static_visibility_surface_t *FindVisibilitySurface(
	const sg_static_visibility_t *visibility, uint64_t surface_id)
{
	uint32_t index;

	if (visibility == NULL)
		return NULL;
	for (index = 0U; index < visibility->surface_count; index++)
		if (visibility->surfaces[index].id == surface_id)
			return &visibility->surfaces[index];
	return NULL;
}

static const sg_static_visibility_partition_t *FindVisibilityPartition(
	const sg_static_visibility_t *visibility, uint64_t partition_id)
{
	uint32_t index;

	if (visibility == NULL)
		return NULL;
	for (index = 0U; index < visibility->partition_count; index++)
		if (visibility->partitions[index].id == partition_id)
			return &visibility->partitions[index];
	return NULL;
}

static int ConfigurationHasHookableSurface(
	const sg_configuration_semantics_t *configuration)
{
	uint32_t index;

	if (configuration == NULL)
		return 0;
	for (index = 0U; index < configuration->hook_surface_count; index++)
		if ((configuration->hook_surfaces[index].flags &
			SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) != 0U &&
			(configuration->hook_surfaces[index].flags &
			SG_CONFIGURATION_HOOK_SURFACE_SKY) == 0U)
			return 1;
	return 0;
}

/* The response lane has already done the expensive first-hit/occluder work.
 * This boundary check verifies that its source volumes and target polygons
 * are still the exact geometry this constructor is consuming. */
static int ValidateResponsePartition(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_compact_response_partition_view_t *view =
		input->response_partition;
	uint32_t index;

	if (view == NULL || !SG_RuneCompactResponsePartitionSealValid(view) ||
		!CompactIdentityEqual(&view->identity,
		&input->geometry->identity) || view->seal.version !=
		SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION ||
		(view->seal.flags & SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED) !=
		SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED ||
		view->seal.source_fragment_count != view->source_fragment_count ||
		view->seal.target_patch_count != view->target_patch_count ||
		view->seal.split_count != view->split_count ||
		view->seal.response_pair_count != view->response_pair_count ||
		view->seal.unresolved_candidate_group_count !=
			view->candidate_group_count ||
		view->seal.source_endpoint_group_count !=
			view->source_endpoint_group_count ||
		view->seal.target_endpoint_group_count !=
			view->target_endpoint_group_count ||
		view->seal.source_endpoint_member_count !=
			view->source_endpoint_member_count ||
		view->seal.target_endpoint_member_count !=
			view->target_endpoint_member_count ||
		view->seal.static_occluder_count != view->static_occluder_count ||
		(view->source_fragment_count != 0U &&
			(view->source_fragments == NULL || view->source_halfspaces == NULL)) ||
		(view->target_patch_count != 0U &&
			(view->target_patches == NULL || view->target_vertices == NULL)) ||
		(view->split_count != 0U && view->splits == NULL) ||
		(view->response_pair_count != 0U && view->response_pairs == NULL) ||
		(view->candidate_group_count != 0U && view->candidate_groups == NULL) ||
		!ValidateResponseEndpointGroups(input, view, view->source_endpoint_groups,
			view->source_endpoint_group_count, view->source_endpoint_members,
			view->source_endpoint_member_count, 1) ||
		!ValidateResponseEndpointGroups(input, view, view->target_endpoint_groups,
			view->target_endpoint_group_count, view->target_endpoint_members,
			view->target_endpoint_member_count, 0)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, 0U, 1U,
			0U);
		return 0;
	}
	for (index = 0U; index < view->source_fragment_count; index++) {
		const sg_rune_compact_response_fragment_t *fragment =
			&view->source_fragments[index];
		const sg_static_visibility_partition_t *partition;

		if (fragment->parent_cell.value >= input->cell_count ||
			fragment->configuration_region >=
				input->configuration_semantics->region_count ||
			fragment->configuration_cell >= ConfigurationCellCount(input) ||
			!SpanWithin(fragment->boundary_incidences.first,
				fragment->boundary_incidences.count, input->incidence_count) ||
			!SpanWithin(fragment->first_halfspace, fragment->halfspace_count,
				view->source_halfspace_count) || !BoundsValid(&fragment->bounds) ||
			!Q8VectorInsideBounds(&fragment->witness, &fragment->bounds) ||
			fragment->bsp_leaf >= input->geometry->identity.source_counts.leaf_count ||
			fragment->bsp_area >= input->geometry->identity.source_counts.area_count ||
			!StancesValid(fragment->valid_stances) ||
			!CompactCellMappedToConfigurationCell(input,
				fragment->configuration_cell, fragment->parent_cell.value)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				input->cell_count, fragment->parent_cell.value);
			return 0;
		}
		partition = FindVisibilityPartition(input->visibility,
			fragment->static_partition_id);
		/* A source fragment is bound to the complete visibility partition,
		 * not merely to its configuration-cell ordinal.  The response lane's
		 * PVS/area/first-hit decisions are invalid if any of these source
		 * identities drifted between the two sealed inputs. */
		if (partition == NULL || fragment->static_partition_id != partition->id ||
			fragment->configuration_region != partition->configuration_region ||
			fragment->configuration_cell != partition->configuration_cell ||
			fragment->bsp_leaf != partition->bsp_leaf ||
			fragment->bsp_area != partition->bsp_area ||
			fragment->bsp_cluster != partition->bsp_cluster) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				partition == NULL ? 0U : partition->id,
				fragment->static_partition_id);
			return 0;
		}
	}
	for (index = 0U; index < view->target_patch_count; index++) {
		const sg_rune_compact_response_patch_t *patch =
			&view->target_patches[index];
		const sg_rune_compact_source_surface_t *source_surface = NULL;
		const sg_static_visibility_surface_t *surface =
			FindVisibilitySurface(input->visibility, patch->visibility_surface_id);
		const sg_static_visibility_partition_t *partition = NULL;
		const int is_sky = (patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U;
		const int is_moving =
			(patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U;
		const int has_parent_facet =
			patch->parent_facet.value != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;

		partition = FindVisibilityPartition(input->visibility,
			patch->static_partition_id);
		if (patch->source_surface < input->geometry->source_surface_count)
			source_surface = &input->geometry->source_surfaces[
				patch->source_surface];

		if (surface == NULL || source_surface == NULL ||
			patch->target_cell.value >= input->cell_count ||
			(has_parent_facet &&
				(patch->parent_facet.value >= input->facet_count ||
				 patch->boundary_incidences.first != input->facets[
					patch->parent_facet.value].incidences.first ||
				 patch->boundary_incidences.count != input->facets[
					patch->parent_facet.value].incidences.count)) ||
			(!has_parent_facet &&
				(patch->boundary_incidences.first != 0U ||
				 patch->boundary_incidences.count != 0U)) ||
			(!is_moving && !is_sky &&
				((has_parent_facet &&
					(patch->parent_facet.value >= input->facet_count ||
					 input->facets[patch->parent_facet.value].kind !=
						SG_RUNE_COMPACT_FACET_POLYGON)) ||
				 (patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE) == 0U)) ||
			(is_moving && (patch->model == SG_HOST_COLLISION_MODEL_WORLD ||
				has_parent_facet)) ||
			(!is_moving && patch->source_frame !=
				SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD) ||
			(is_moving && patch->source_frame !=
				SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) ||
			source_surface->source.model != patch->model ||
			source_surface->source.brush != patch->brush ||
			source_surface->source.brush_side != patch->brush_side ||
			source_surface->frame != patch->source_frame ||
			memcmp(&source_surface->plane, &patch->plane,
				sizeof(patch->plane)) != 0 ||
			(is_sky &&
				(patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE) != 0U) ||
			!SpanWithin(patch->boundary_incidences.first,
				patch->boundary_incidences.count, input->incidence_count) ||
			!SpanWithin(patch->first_vertex, patch->vertex_count,
				view->target_vertex_count) || patch->vertex_count < 3U ||
			!ClosedBoundsValid(&patch->bounds) ||
			patch->configuration_region >=
				input->configuration_semantics->region_count ||
			patch->configuration_cell >= ConfigurationCellCount(input) ||
			!CompactCellMappedToConfigurationCell(input,
				patch->configuration_cell, patch->target_cell.value) ||
			patch->visibility_surface_id != surface->id ||
			patch->model != surface->model || patch->brush != surface->brush ||
			patch->brush_side != surface->brush_side ||
			(patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) !=
				((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY) != 0U ?
				SG_RUNE_COMPACT_RESPONSE_PATCH_SKY : 0U) ||
			(patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE) !=
				((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) != 0U ?
				SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE : 0U) ||
			(is_moving &&
				(surface->flags & SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL) == 0U) ||
			(!is_moving &&
				(surface->flags & SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL) != 0U) ||
			patch->bsp_leaf >= input->geometry->identity.source_counts.leaf_count ||
			patch->bsp_area >= input->geometry->identity.source_counts.area_count ||
			partition == NULL || patch->static_partition_id != partition->id ||
			patch->configuration_region != partition->configuration_region ||
			patch->configuration_cell != partition->configuration_cell ||
			patch->bsp_leaf != partition->bsp_leaf ||
			patch->bsp_area != partition->bsp_area ||
			patch->bsp_cluster != partition->bsp_cluster) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				input->cell_count, patch->target_cell.value);
			return 0;
		}
		{
			uint32_t vertex;

			for (vertex = 0U; vertex < patch->vertex_count; vertex++)
				if (!Q8VectorInsideClosedBounds(&view->target_vertices[
					patch->first_vertex + vertex], &patch->bounds)) {
					SetError(error,
						SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
						index, input->cell_count, patch->target_cell.value);
					return 0;
				}
		}
	}
	for (index = 0U; index < view->response_pair_count; index++) {
		const sg_rune_compact_response_pair_t *pair =
			&view->response_pairs[index];
		const sg_rune_compact_response_fragment_t *fragment;
		const sg_rune_compact_response_patch_t *patch;

		if (pair->source_fragment >= view->source_fragment_count ||
			pair->target_patch >= view->target_patch_count ||
			(uint32_t)pair->classification >=
				(uint32_t)SG_STATIC_VISIBILITY_CONDITIONAL + 1U ||
			(uint32_t)pair->reason >=
				(uint32_t)SG_STATIC_VISIBILITY_REASON_SKY + 1U ||
			pair->requires_exact_ray > 1U || pair->requires_area_state > 1U ||
			(pair->relation_flags & (sg_rune_compact_static_relation_flags_t)
				~SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN) != 0U ||
			!StancesValid(pair->source_valid_stances) ||
			!StancesValid(pair->target_valid_stances) ||
			!ResponseCertificateValid(input, view, pair->classification,
				pair->reason, pair->first_hit_occluder,
				pair->requires_exact_ray, pair->requires_area_state,
				pair->certificate, pair->relation_flags,
				ResponseEndpointGroupForMember(view->source_endpoint_groups,
					view->source_endpoint_group_count,
					view->source_endpoint_members, pair->source_fragment),
				ResponseEndpointGroupForMember(view->target_endpoint_groups,
					view->target_endpoint_group_count,
					view->target_endpoint_members, pair->target_patch)) ||
			!ResponseTraceValid(&pair->trace)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				1U, 0U);
			return 0;
		}
		fragment = &view->source_fragments[pair->source_fragment];
		patch = &view->target_patches[pair->target_patch];
		if ((pair->source_valid_stances & fragment->valid_stances) !=
			pair->source_valid_stances ||
			(pair->target_valid_stances & input->cells[patch->target_cell.value].valid_stances) !=
			pair->target_valid_stances ||
			pair->reason == SG_STATIC_VISIBILITY_REASON_SKY) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				1U, 0U);
			return 0;
		}
	}
	for (index = 0U; index < view->candidate_group_count; index++) {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&view->candidate_groups[index];

		if ((index != 0U &&
			(candidate->source_group < view->candidate_groups[index - 1U].source_group ||
			(candidate->source_group == view->candidate_groups[index - 1U].source_group &&
				candidate->target_group <=
					view->candidate_groups[index - 1U].target_group))) ||
			!ResponseCandidateValid(view, candidate)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				1U, 0U);
			return 0;
		}
	}
	return 1;
}


static int CompareCellResponseRef(const void *left_pointer,
	const void *right_pointer)
{
	const cell_response_ref_t *left =
		(const cell_response_ref_t *)left_pointer;
	const cell_response_ref_t *right =
		(const cell_response_ref_t *)right_pointer;

	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	if (left->stance != right->stance)
		return left->stance < right->stance ? -1 : 1;
	if (left->visibility_class != right->visibility_class)
		return left->visibility_class < right->visibility_class ? -1 : 1;
	if (left->ref.kind != right->ref.kind)
		return left->ref.kind < right->ref.kind ? -1 : 1;
	if (left->ref.index != right->ref.index)
		return left->ref.index < right->ref.index ? -1 : 1;
	return 0;
}

static sg_rune_stance_validity_t HookStance(uint32_t stance)
{
	return stance == 0U ? SG_RUNE_STANCE_VALID_STANDING :
		SG_RUNE_STANCE_VALID_CROUCHING;
}

static uint32_t HookRefSlot(uint32_t cell, uint32_t stance,
	hook_visibility_class_t visibility_class)
{
	return ((cell * HOOK_STANCE_COUNT) + stance) *
		HOOK_VISIBILITY_CLASS_COUNT + (uint32_t)visibility_class;
}

static void AppendHookRefRecords(cell_response_ref_t *records,
	uint32_t *count, uint32_t cell, sg_rune_stance_validity_t stances,
	hook_visibility_class_t visibility_class,
	sg_rune_compact_response_ref_kind_t kind, uint32_t reference)
{
	uint32_t stance;

	for (stance = 0U; stance < HOOK_STANCE_COUNT; stance++) {
		if ((stances & HookStance(stance)) == 0U)
			continue;
		records[*count].cell = cell;
		records[*count].stance = (uint8_t)stance;
		records[*count].visibility_class = (uint8_t)visibility_class;
		records[*count].ref.kind = kind;
		records[*count].ref.index = reference;
		(*count)++;
	}
}

static int BuildHookRefs(
	const sg_rune_compact_movement_bound_input_t *input,
	index_workspace_t *index,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_compact_response_partition_view_t *view =
		input->response_partition;
	cell_response_ref_t *records;
	uint32_t capacity = view->response_pair_count;
	uint32_t count = 0U;
	uint32_t candidate_index;
	uint32_t pair_index;
	uint32_t cursor;

	for (candidate_index = 0U; candidate_index < view->candidate_group_count;
		candidate_index++) {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&view->candidate_groups[candidate_index];
		const sg_rune_compact_response_endpoint_group_t *source_group =
			&view->source_endpoint_groups[candidate->source_group];

		if (!AddU32(capacity, source_group->member_count, &capacity))
			goto limit;
	}
	if (!AddU32(capacity, capacity, &capacity))
		goto limit;
	records = AllocateArray(capacity, sizeof(*records));
	if (capacity != 0U && records == NULL) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, 0U, 0U);
		return 0;
	}
	for (candidate_index = 0U; candidate_index < view->candidate_group_count;
		candidate_index++) {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&view->candidate_groups[candidate_index];
		const sg_rune_compact_response_endpoint_group_t *source_group =
			&view->source_endpoint_groups[candidate->source_group];
		const sg_rune_compact_response_endpoint_group_t *target_group =
			&view->target_endpoint_groups[candidate->target_group];
		sg_rune_stance_validity_t target_stances = 0U;
		uint32_t source_offset;

		for (cursor = 0U; cursor < target_group->member_count; cursor++) {
			const sg_rune_compact_response_patch_t *patch =
				&view->target_patches[view->target_endpoint_members[
					target_group->first_member + cursor]];
			target_stances = (sg_rune_stance_validity_t)(target_stances |
				(patch->valid_stances &
					input->cells[patch->target_cell.value].valid_stances));
		}
		for (source_offset = 0U; source_offset < source_group->member_count;
			source_offset++) {
			const uint32_t fragment_index = view->source_endpoint_members[
				source_group->first_member + source_offset];
			const sg_rune_compact_response_fragment_t *fragment =
				&view->source_fragments[fragment_index];
			const uint32_t cell = fragment->parent_cell.value;
			const sg_rune_stance_validity_t stances =
				(sg_rune_stance_validity_t)(fragment->valid_stances &
					target_stances & input->cells[cell].valid_stances);

			AppendHookRefRecords(records, &count, cell, stances,
				HOOK_VISIBILITY_CONDITIONAL,
				SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP,
				candidate_index);
		}
	}
	for (pair_index = 0U; pair_index < view->response_pair_count;
		pair_index++) {
		const sg_rune_compact_response_pair_t *pair =
			&view->response_pairs[pair_index];
		const sg_rune_compact_response_fragment_t *fragment =
			&view->source_fragments[pair->source_fragment];
		const sg_rune_compact_response_patch_t *patch =
			&view->target_patches[pair->target_patch];
		const uint32_t cell = fragment->parent_cell.value;
		const sg_rune_stance_validity_t stances =
			(sg_rune_stance_validity_t)(pair->source_valid_stances &
				pair->target_valid_stances & fragment->valid_stances &
				patch->valid_stances & input->cells[cell].valid_stances &
				input->cells[patch->target_cell.value].valid_stances);
		hook_visibility_class_t visibility_class =
			HOOK_VISIBILITY_CONDITIONAL;

		if (pair->classification == SG_STATIC_VISIBILITY_OCCLUDED)
			visibility_class = HOOK_VISIBILITY_BLOCKED;
		else if (pair->classification == SG_STATIC_VISIBILITY_VISIBLE)
			visibility_class = HOOK_VISIBILITY_VISIBLE;
		AppendHookRefRecords(records, &count, cell, stances, visibility_class,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, pair_index);
	}
	if (count > 1U)
		qsort(records, count, sizeof(*records), CompareCellResponseRef);
	if (count > 1U) {
		uint32_t read;
		uint32_t write = 1U;

		for (read = 1U; read < count; read++)
			if (CompareCellResponseRef(&records[write - 1U],
				&records[read]) != 0)
				records[write++] = records[read];
		count = write;
	}
	index->hook_refs = AllocateArray(count, sizeof(*index->hook_refs));
	cursor = input->cell_count * HOOK_STANCE_COUNT *
		HOOK_VISIBILITY_CLASS_COUNT;
	index->hook_ref_offsets = AllocateArray(cursor + 1U,
		sizeof(*index->hook_ref_offsets));
	if ((count != 0U && index->hook_refs == NULL) ||
		index->hook_ref_offsets == NULL) {
		free(records);
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, 0U, 0U);
		return 0;
	}
	{
		const uint32_t slot_count = cursor;

		cursor = 0U;
		for (candidate_index = 0U; candidate_index < slot_count;
			candidate_index++) {
			index->hook_ref_offsets[candidate_index] = cursor;
			while (cursor < count && HookRefSlot(records[cursor].cell,
				records[cursor].stance,
				(hook_visibility_class_t)records[cursor].visibility_class) ==
				candidate_index) {
				index->hook_refs[cursor] = records[cursor].ref;
				cursor++;
			}
		}
		index->hook_ref_offsets[slot_count] = cursor;
	}
	index->hook_ref_count = count;
	free(records);
	if (count == 0U &&
		ConfigurationHasHookableSurface(input->configuration_semantics)) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
			0U, 1U, 0U);
		return 0;
	}
	return 1;

limit:
	SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_LIMIT_EXCEEDED,
		candidate_index, UINT32_MAX, capacity);
	return 0;
}

static int ValidateFacets(const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < input->facet_count; index++) {
		const sg_rune_compact_facet_t *facet = &input->facets[index];

		if ((uint32_t)facet->kind >=
			(uint32_t)SG_RUNE_COMPACT_FACET_KIND_COUNT ||
			!SpanWithin(facet->vertices.first, facet->vertices.count,
				input->geometry->vertex_count) ||
			!SpanWithin(facet->incidences.first, facet->incidences.count,
				input->incidence_count) ||
			(facet->kind == SG_RUNE_COMPACT_FACET_POLYGON &&
				facet->vertices.count < 3U) ||
			(facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY &&
				facet->vertices.count != 0U) ||
			(facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY &&
				facet->portal.value != SG_RUNE_COMPACT_INDEX_NONE) ||
			(facet->portal.value != SG_RUNE_COMPACT_INDEX_NONE &&
				facet->portal.value >= input->portal_count)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				index, SG_RUNE_COMPACT_FACET_KIND_COUNT,
				(uint64_t)facet->kind);
			return 0;
		}
	}
	return 1;
}

static int ValidateIncidencesAndPortals(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < input->incidence_count; index++) {
		const sg_rune_compact_incidence_t *incidence =
			&input->incidences[index];

		if (incidence->cell.value >= input->cell_count ||
			incidence->facet.value >= input->facet_count ||
			(uint32_t)incidence->side >=
				(uint32_t)SG_RUNE_FACET_SIDE_COUNT ||
			(uint32_t)incidence->boundary >=
				(uint32_t)SG_RUNE_BOUNDARY_OWNERSHIP_COUNT) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				index, input->cell_count, incidence->cell.value);
			return 0;
		}
	}
	for (index = 0U; index < input->portal_count; index++) {
		const sg_rune_compact_portal_t *portal = &input->portals[index];
		const sg_rune_compact_incidence_t *negative;
		const sg_rune_compact_incidence_t *positive;
		uint32_t negative_cell;
		uint32_t positive_cell;

		if (portal->negative_incidence.value >= input->incidence_count ||
			portal->positive_incidence.value >= input->incidence_count ||
			portal->facet.value >= input->facet_count ||
			portal->clearance_q8 == 0U ||
			(uint32_t)portal->direction >=
				(uint32_t)SG_RUNE_PORTAL_CONTINUITY_COUNT ||
			!StancesValid(portal->valid_stances)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				index, 1U, 0U);
			return 0;
		}
		if (input->facets[portal->facet.value].kind !=
			SG_RUNE_COMPACT_FACET_POLYGON ||
			input->facets[portal->facet.value].portal.value != index) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				index, SG_RUNE_COMPACT_FACET_POLYGON,
				input->facets[portal->facet.value].kind);
			return 0;
		}
		negative = &input->incidences[portal->negative_incidence.value];
		positive = &input->incidences[portal->positive_incidence.value];
		negative_cell = negative->cell.value;
		positive_cell = positive->cell.value;
		if (negative_cell == positive_cell ||
			negative->facet.value != portal->facet.value ||
			positive->facet.value != portal->facet.value ||
			(portal->valid_stances & input->cells[negative_cell].valid_stances) !=
				portal->valid_stances ||
			(portal->valid_stances & input->cells[positive_cell].valid_stances) !=
				portal->valid_stances) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				index, 0U, 1U);
			return 0;
		}
	}
	return 1;
}

static int ValidateConfiguration(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_configuration_semantics_t *configuration =
		input->configuration_semantics;
	const uint32_t configuration_cell_count = ConfigurationCellCount(input);
	uint32_t index;

	if (configuration_cell_count == 0U ||
		configuration_cell_count > SG_RUNE_COMPACT_MAX_CELLS ||
		configuration->region_count == 0U ||
		configuration->region_count > SG_RUNE_COMPACT_MAX_CELLS ||
		(configuration->region_count != 0U && configuration->regions == NULL) ||
	(configuration->face_count != 0U && configuration->faces == NULL) ||
	(configuration->vertex_count != 0U && configuration->vertices == NULL) ||
	(configuration->boundary_count != 0U && configuration->boundaries == NULL) ||
	(configuration->hook_surface_count != 0U &&
		configuration->hook_surfaces == NULL) ||
	(configuration->hook_vertex_count != 0U &&
		configuration->hook_vertices == NULL)) {
		SetError(error,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION, 0U,
			configuration_cell_count, configuration->region_count);
		return 0;
	}
	for (index = 0U; index < configuration->face_count; index++) {
		const sg_configuration_semantic_face_t *face =
			&configuration->faces[index];

		if ((face->kind != SG_CONFIGURATION_SEMANTIC_FACE_FACET &&
			face->kind != SG_CONFIGURATION_SEMANTIC_FACE_CONSTRAINT_ONLY) ||
			!FiniteVector(face->normal) || !ScalarValid(face->distance) ||
			!SpanWithin(face->first_vertex, face->vertex_count,
				configuration->vertex_count)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
				index, 0U, 1U);
			return 0;
		}
	}
	for (index = 0U; index < configuration->region_count; index++) {
		const sg_configuration_semantic_region_t *region =
			&configuration->regions[index];
		const sg_rune_compact_geometry_cell_span_t *span;
		uint32_t sample;

		if (region->cell >= configuration_cell_count ||
			!ConfigurationCellSpan(input, region->cell, &span) ||
			(span != NULL && span->count == 0U) ||
			!RegionFlagsValid(region->flags) ||
			!FloatBoundsValid(&region->bounds) ||
			!FiniteVector(region->interior_witness.value) ||
			!SpanWithin(region->first_face, region->face_count,
				configuration->face_count)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
				index, configuration_cell_count, region->cell);
			return 0;
		}
		for (sample = 0U; sample < 3U; sample++)
			if (region->sample_leaves[sample] >=
					input->geometry->identity.source_counts.leaf_count ||
				region->sample_areas[sample] >=
					input->geometry->identity.source_counts.area_count ||
				region->sample_clusters[sample] < -1) {
				SetError(error,
					SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
					index, input->geometry->identity.source_counts.leaf_count,
					region->sample_leaves[sample]);
				return 0;
			}
	}
	for (index = 0U; index < configuration->boundary_count; index++) {
		const sg_configuration_boundary_t *boundary =
			&configuration->boundaries[index];

		if (boundary->cell >= configuration_cell_count ||
			boundary->configuration_face >= configuration->face_count ||
			(boundary->brush != SG_CONFIGURATION_SEMANTICS_INDEX_NONE &&
				boundary->brush >=
					input->geometry->identity.source_counts.brush_count) ||
			(boundary->brush_side != SG_CONFIGURATION_SEMANTICS_INDEX_NONE &&
				boundary->brush_side >=
					input->geometry->identity.source_counts.brush_side_count) ||
			!FiniteVector(boundary->origin_normal) ||
			!ScalarValid(boundary->origin_distance) ||
			!FiniteVector(boundary->surface_normal) ||
			!ScalarValid(boundary->surface_distance)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
				index, 0U, 1U);
			return 0;
		}
	}
	for (index = 0U; index < configuration->hook_surface_count; index++) {
		const sg_configuration_hook_surface_t *surface =
			&configuration->hook_surfaces[index];
		const uint32_t known = SG_CONFIGURATION_HOOK_SURFACE_SKY |
			SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
			SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;

		if ((surface->model >= input->geometry->identity.source_counts.model_count) ||
			(surface->brush != SG_CONFIGURATION_SEMANTICS_INDEX_NONE &&
				surface->brush >=
					input->geometry->identity.source_counts.brush_count) ||
			(surface->brush_side != SG_CONFIGURATION_SEMANTICS_INDEX_NONE &&
				surface->brush_side >=
					input->geometry->identity.source_counts.brush_side_count) ||
			(surface->flags & ~known) != 0U ||
			(surface->flags & (SG_CONFIGURATION_HOOK_SURFACE_SKY |
				SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE)) ==
				(SG_CONFIGURATION_HOOK_SURFACE_SKY |
					SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) ||
			!FiniteVector(surface->normal) || !ScalarValid(surface->distance) ||
			!FloatBoundsValid(&surface->bounds) ||
			!SpanWithin(surface->first_vertex, surface->vertex_count,
				configuration->hook_vertex_count)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
				index, 0U, surface->flags);
			return 0;
		}
	}
	return 1;
}

static int PortalMechanismKindMatches(
	sg_rune_compact_portal_mechanism_kind_t binding,
	const sg_rune_compact_mechanism_t *mechanism)
{
	if (mechanism == NULL)
		return 0;
	switch (binding) {
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS:
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_DOOR ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_BUTTON ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_LIFT ||
			mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRAIN ||
			(mechanism->kind == SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
				(mechanism->flags &
					SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U);
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES:
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_TELEPORTS:
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_LAUNCHES:
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT:
		return 0;
	}
	return 0;
}

static int ValidateStaticData(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_compact_static_t *static_data = input->static_data;
	uint32_t index;
	uint32_t controller_cursor = 0U;
	uint32_t edge_cursor = 0U;
	uint32_t transition_cursor = 0U;
	uint32_t landmark_cell_cursor = 0U;

	if (static_data->mechanism_count > SG_RUNE_COMPACT_MAX_MECHANISMS ||
		static_data->mechanism_controller_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS ||
		static_data->mechanism_edge_count > SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ||
		static_data->transition_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS ||
		static_data->landmark_count > SG_RUNE_COMPACT_MAX_LANDMARKS ||
		static_data->landmark_cell_count > SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS ||
		static_data->facet_annotation_count > SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS ||
		static_data->portal_mechanism_count > SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS ||
		(static_data->mechanism_count != 0U && static_data->mechanisms == NULL) ||
		(static_data->mechanism_controller_count != 0U &&
			static_data->mechanism_controllers == NULL) ||
		(static_data->mechanism_edge_count != 0U &&
			static_data->mechanism_edges == NULL) ||
		(static_data->transition_count != 0U && static_data->transitions == NULL) ||
		(static_data->landmark_count != 0U && static_data->landmarks == NULL) ||
		(static_data->landmark_cell_count != 0U &&
			static_data->landmark_cells == NULL) ||
		(static_data->facet_annotation_count != 0U &&
			static_data->facet_annotations == NULL) ||
		(static_data->portal_mechanism_count != 0U &&
			static_data->portal_mechanisms == NULL)) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			0U, 0U, 1U);
		return 0;
	}
	if (static_data->transition_count != input->mechanisms->transition_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			0U, input->mechanisms->transition_count,
			static_data->transition_count);
		return 0;
	}
	for (index = 0U; index < static_data->mechanism_count; index++) {
		const sg_rune_compact_mechanism_t *mechanism =
			&static_data->mechanisms[index];

		if (mechanism->reserved[0] != 0U || mechanism->reserved[1] != 0U ||
			mechanism->reserved[2] != 0U ||
			mechanism->source.entity_ordinal >=
				input->geometry->identity.source_counts.entity_count ||
			mechanism->entry_cell.value >= input->cell_count ||
			mechanism->exit_cell.value >= input->cell_count ||
			!BoundsValid(&mechanism->bounds) ||
			(uint32_t)mechanism->kind >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_KIND_COUNT ||
			mechanism->activation_mask == 0U ||
			(mechanism->activation_mask &
				(sg_rune_compact_static_activation_mask_t)
				~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_KNOWN) != 0U ||
			(uint32_t)mechanism->initial_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->activated_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->reset_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->recovery >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_RECOVERY_COUNT ||
			((mechanism->activation_mask &
				(sg_rune_compact_static_activation_mask_t)
				~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U &&
				mechanism->activation_landmark.value ==
					SG_RUNE_COMPACT_INDEX_NONE) ||
			((mechanism->activation_mask &
				(sg_rune_compact_static_activation_mask_t)
				~SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) == 0U &&
				mechanism->activation_landmark.value !=
					SG_RUNE_COMPACT_INDEX_NONE) ||
			(mechanism->activation_landmark.value !=
				SG_RUNE_COMPACT_INDEX_NONE &&
				mechanism->activation_landmark.value >=
					static_data->landmark_count) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE) != 0U &&
				mechanism->health <= 0) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE) == 0U &&
				mechanism->health != 0) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY) != 0U &&
				mechanism->required_item == SG_BSP_ENTITY_STRING_NONE) ||
			((mechanism->activation_mask &
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY) == 0U &&
				mechanism->required_item != SG_BSP_ENTITY_STRING_NONE) ||
			(mechanism->flags & (sg_rune_compact_mechanism_flags_t)
				~SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN) != 0U ||
			((mechanism->flags &
				SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U &&
				(mechanism->kind != SG_RUNE_COMPACT_MECHANISM_ROTATOR ||
				 (mechanism->flags &
					SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE) == 0U)) ||
			mechanism->controllers.first >
				static_data->mechanism_controller_count ||
			mechanism->controllers.count >
				static_data->mechanism_controller_count -
					mechanism->controllers.first ||
			mechanism->topology.first > static_data->mechanism_edge_count ||
			mechanism->topology.count > static_data->mechanism_edge_count -
				mechanism->topology.first ||
			mechanism->transitions.first != transition_cursor ||
			!SpanWithin(mechanism->transitions.first,
				mechanism->transitions.count, static_data->transition_count) ||
			(mechanism->recovery ==
				SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET) !=
				(mechanism->reset_ms != 0U)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, 0U, 1U);
			return 0;
		}
		if (mechanism->controllers.first != controller_cursor ||
			!AddU32(controller_cursor, mechanism->controllers.count,
				&controller_cursor)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, controller_cursor, mechanism->controllers.first);
			return 0;
		}
		if (mechanism->topology.first != edge_cursor ||
			!AddU32(edge_cursor, mechanism->topology.count, &edge_cursor)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, edge_cursor, mechanism->topology.first);
			return 0;
		}
		if (!AddU32(transition_cursor, mechanism->transitions.count,
			&transition_cursor)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, UINT32_MAX, mechanism->transitions.count);
			return 0;
		}
	}
	if (transition_cursor != static_data->transition_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			static_data->transition_count, transition_cursor,
			static_data->transition_count);
		return 0;
	}
	for (index = 0U; index < static_data->transition_count; index++) {
		const sg_rune_compact_static_transition_t *transition =
			&static_data->transitions[index];

		if (transition->mechanism.value >= static_data->mechanism_count ||
			transition->entry_cell.value >= input->cell_count ||
			transition->exit_cell.value >= input->cell_count ||
			(uint32_t)transition->kind >=
				(uint32_t)SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT ||
			(uint32_t)transition->source_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)transition->destination_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			index < static_data->mechanisms[
				transition->mechanism.value].transitions.first ||
			index >= static_data->mechanisms[
				transition->mechanism.value].transitions.first +
				static_data->mechanisms[
					transition->mechanism.value].transitions.count) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, 1U, 0U);
			return 0;
		}
		if (transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH &&
			transition->value.push.gravity_bits !=
				input->geometry->identity.physics.gravity_bits) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, input->geometry->identity.physics.gravity_bits,
				transition->value.push.gravity_bits);
			return 0;
		}
	}
	if (controller_cursor != static_data->mechanism_controller_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			static_data->mechanism_controller_count, controller_cursor,
			static_data->mechanism_controller_count);
		return 0;
	}
	for (index = 0U; index < static_data->mechanism_controller_count; index++) {
		const sg_rune_compact_static_mechanism_controller_t *controller =
			&static_data->mechanism_controllers[index];

		if (controller->controller.entity_ordinal >=
				input->geometry->identity.source_counts.entity_count ||
			controller->spatiality >=
				SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
			(controller->spatiality ==
				SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
				(controller->activation_cell.value !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				 !BytesZero(&controller->activation_witness,
					sizeof(controller->activation_witness)) ||
				 !BytesZero(&controller->activation_bounds,
					sizeof(controller->activation_bounds))) :
				(controller->activation_cell.value >= input->cell_count ||
				 !Q8VectorInsideBounds(&controller->activation_witness,
					&controller->activation_bounds) ||
				 !Q8VectorInsideBounds(&controller->activation_witness,
					&input->cells[controller->activation_cell.value].bounds)))) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, 0U, 1U);
			return 0;
		}
	}
	if (edge_cursor != static_data->mechanism_edge_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			static_data->mechanism_edge_count, edge_cursor,
			static_data->mechanism_edge_count);
		return 0;
	}
	for (index = 0U; index < static_data->mechanism_edge_count; index++)
		if ((uint32_t)static_data->mechanism_edges[index].kind >=
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT,
				static_data->mechanism_edges[index].kind);
			return 0;
		}
	for (index = 0U; index < static_data->landmark_count; index++) {
		const sg_rune_compact_landmark_t *landmark =
			&static_data->landmarks[index];

		if ((uint32_t)landmark->kind >=
			(uint32_t)SG_RUNE_COMPACT_LANDMARK_KIND_COUNT ||
			!BoundsValid(&landmark->bounds) ||
			landmark->origin.value[0] < landmark->bounds.mins.value[0] ||
			landmark->origin.value[1] < landmark->bounds.mins.value[1] ||
			landmark->origin.value[2] < landmark->bounds.mins.value[2] ||
			landmark->origin.value[0] >= landmark->bounds.maxs.value[0] ||
			landmark->origin.value[1] >= landmark->bounds.maxs.value[1] ||
			landmark->origin.value[2] >= landmark->bounds.maxs.value[2] ||
			landmark->cells.first != landmark_cell_cursor ||
			!SpanWithin(landmark->cells.first, landmark->cells.count,
				static_data->landmark_cell_count) ||
			landmark->cells.count == 0U ||
			(landmark->mechanism.value != SG_RUNE_COMPACT_INDEX_NONE &&
				landmark->mechanism.value >= static_data->mechanism_count)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, landmark_cell_cursor, landmark->cells.first);
			return 0;
		}
		if (!AddU32(landmark_cell_cursor, landmark->cells.count,
			&landmark_cell_cursor)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, UINT32_MAX, landmark->cells.count);
			return 0;
		}
	}
	if (landmark_cell_cursor != static_data->landmark_cell_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			static_data->landmark_cell_count, landmark_cell_cursor,
			static_data->landmark_cell_count);
		return 0;
	}
	for (index = 0U; index < static_data->landmark_cell_count; index++)
		if (static_data->landmark_cells[index].value >= input->cell_count) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, input->cell_count,
				static_data->landmark_cells[index].value);
			return 0;
		}
	for (index = 0U; index < static_data->facet_annotation_count; index++) {
		const sg_rune_compact_facet_annotation_t *annotation =
			&static_data->facet_annotations[index];
		const uint16_t hookable = SG_RUNE_COMPACT_FACET_HOOKABLE;

		if (annotation->facet.value >= input->facet_count ||
			annotation->attributes == 0U ||
			(annotation->attributes & (sg_rune_compact_facet_attributes_t)
				~SG_RUNE_COMPACT_FACET_ATTRIBUTES_KNOWN) != 0U ||
			(annotation->hookable_stances & (sg_rune_stance_validity_t)
				~SG_RUNE_STANCE_VALID_ALL) != 0U ||
			((annotation->attributes & hookable) == 0U &&
				annotation->hookable_stances != 0U) ||
			((annotation->attributes & hookable) != 0U &&
				annotation->hookable_stances == 0U) ||
			((annotation->attributes & (hookable | SG_RUNE_COMPACT_FACET_SKY)) ==
				(hookable | SG_RUNE_COMPACT_FACET_SKY)) ||
			(input->facets[annotation->facet.value].kind ==
				SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY &&
				(annotation->attributes & hookable) != 0U) ||
			(index != 0U &&
			static_data->facet_annotations[index - 1U].facet.value >=
				annotation->facet.value)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, 0U, annotation->facet.value);
			return 0;
		}
	}
	for (index = 0U; index < static_data->portal_mechanism_count; index++) {
		const sg_rune_compact_portal_mechanism_t *binding =
			&static_data->portal_mechanisms[index];

		if (binding->reserved[0] != 0U || binding->reserved[1] != 0U ||
			binding->reserved[2] != 0U || binding->portal.value >= input->portal_count ||
			binding->mechanism.value >= static_data->mechanism_count ||
			(uint32_t)binding->kind >=
				(uint32_t)SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT ||
			/* A push or teleport is a non-portal authority transition.  The
			 * legacy portal table has no launch vector, gravity, flight, or
			 * destination, so accepting either binding would fabricate a
			 * transition and lose the authoritative fields below. */
			binding->kind != SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS ||
			!PortalMechanismKindMatches(binding->kind,
				&static_data->mechanisms[binding->mechanism.value]) ||
			(index != 0U &&
				(static_data->portal_mechanisms[index - 1U].mechanism.value >
					binding->mechanism.value ||
				(static_data->portal_mechanisms[index - 1U].mechanism.value ==
					binding->mechanism.value &&
					static_data->portal_mechanisms[index - 1U].portal.value >=
						binding->portal.value)))) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				index, 0U, binding->portal.value);
			return 0;
		}
	}
	return 1;
}

static const sg_bsp_entity_semantic_t *FindSemanticEntity(
	const sg_bsp_entity_semantics_t *semantics, uint32_t canonical_ordinal)
{
	uint32_t index;

	if (semantics == NULL)
		return NULL;
	for (index = 0U; index < semantics->entity_count; index++)
		if (semantics->entities[index].canonical_ordinal == canonical_ordinal)
			return &semantics->entities[index];
	return NULL;
}

static int FiniteAngularDoorAuthority(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_mechanism_authority_t *authority)
{
	const sg_bsp_entity_semantic_t *entity;

	if (input == NULL || input->entity_semantics == NULL || authority == NULL ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR)
		return 0;
	entity = FindSemanticEntity(input->entity_semantics,
		authority->source.entity_ordinal);
	return SG_BspEntitySemanticHasFiniteAngularDoor(entity);
}

static int ContinuousRotatorAuthority(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_mechanism_authority_t *authority)
{
	const sg_bsp_entity_semantic_t *entity;

	if (input == NULL || input->entity_semantics == NULL || authority == NULL ||
		authority->kind != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR)
		return 0;
	entity = FindSemanticEntity(input->entity_semantics,
		authority->source.entity_ordinal);
	return entity != NULL && entity->angular_mover.kind ==
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
}

static int MechanismTransitionKindMatches(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	sg_rune_compact_mechanism_transition_kind_t transition)
{
	if (authority == NULL)
		return 0;
	switch (transition) {
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		return authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
			FiniteAngularDoorAuthority(input, authority);
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		return authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		return authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		return authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
			authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN ||
			FiniteAngularDoorAuthority(input, authority) ||
			ContinuousRotatorAuthority(input, authority);
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

static int MechanismTransitionStatesValid(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	const sg_rune_compact_mechanism_transition_t *transition)
{
	if (authority == NULL || transition == NULL)
		return 0;
	if (transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT ||
		transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH)
		return transition->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			transition->destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
		transition->kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		(authority->activation & SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO) !=
			0U)
		return transition->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			transition->destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	if (transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		ContinuousRotatorAuthority(input, authority))
		return transition->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			transition->destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	return authority->initial_state != authority->activated_state &&
		transition->source_state == authority->initial_state &&
		transition->destination_state == authority->activated_state;
}


static int Binary32FiniteBits(uint32_t bits)
{
	return isfinite(CompactScalar(bits));
}

static int TransportPointMatches(
	const sg_rune_compact_movement_fields_input_t *input,
	uint32_t mover_entity_ordinal, const uint32_t origin_bits[3],
	const uint32_t axis_bits[3][3],
	const sg_rune_q8_vec3_t *local_q8, const uint32_t world_bits[3])
{
	sg_host_collision_world_transform_t transform;
	sg_rune_vec3_t world;
	sg_host_law_result_t result;
	uint32_t local_axis;
	uint32_t world_axis;

	if (input == NULL || input->builder == NULL || origin_bits == NULL ||
		axis_bits == NULL || local_q8 == NULL ||
		world_bits == NULL)
		return 0;
	memset(&transform, 0, sizeof(transform));
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		if (!Binary32Canonical(origin_bits[world_axis]) ||
			!Binary32Canonical(world_bits[world_axis]))
			return 0;
		transform.origin[world_axis] = CompactScalar(origin_bits[world_axis]);
		for (local_axis = 0U; local_axis < 3U; local_axis++) {
			if (!Binary32Canonical(axis_bits[local_axis][world_axis]))
				return 0;
			transform.axis[local_axis][world_axis] =
				CompactScalar(axis_bits[local_axis][world_axis]);
		}
	}
	result = SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(input->builder,
		mover_entity_ordinal, &transform, local_q8, &world);
	if (result.status != SG_HOST_LAW_OK)
		return 0;
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		if (FloatBits(world.value[world_axis]) != world_bits[world_axis])
			return 0;
	return 1;
}

static int TransportProvenanceValid(
	const sg_rune_compact_movement_fields_input_t *input,
	const sg_rune_compact_mechanism_authority_t *authority,
	const sg_rune_compact_mechanism_transport_t *transport)
{
	const sg_rune_compact_source_surface_t *surface;

	if (input == NULL || authority == NULL || transport == NULL ||
		transport->mover_model == SG_HOST_COLLISION_MODEL_WORLD ||
		transport->mover_model >=
			input->geometry->identity.source_counts.model_count ||
		transport->source_surface_ordinal >=
			input->geometry->source_surface_count)
		return 0;
	surface = &input->geometry->source_surfaces[
		transport->source_surface_ordinal];
	if (surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL ||
		surface->source.model != transport->mover_model ||
		surface->cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->parent_surface != SG_RUNE_COMPACT_INDEX_NONE ||
		surface->split_ordinal != 0U)
		return 0;
	return TransportPointMatches(input, authority->source.entity_ordinal,
		transport->source_mover_origin_bits,
		transport->source_mover_axis_bits,
		&transport->source_player_local,
		transport->source_player_world_bits) &&
		TransportPointMatches(input, authority->source.entity_ordinal,
			transport->source_mover_origin_bits,
			transport->source_mover_axis_bits,
			&transport->source_support_local,
			transport->source_support_world_bits) &&
		TransportPointMatches(input, authority->source.entity_ordinal,
			transport->destination_mover_origin_bits,
			transport->destination_mover_axis_bits,
			&transport->destination_player_local,
			transport->destination_player_world_bits) &&
		TransportPointMatches(input, authority->source.entity_ordinal,
			transport->destination_mover_origin_bits,
			transport->destination_mover_axis_bits,
			&transport->destination_support_local,
			transport->destination_support_world_bits);
}

static int ValidateMechanisms(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_compact_mechanisms_view_t *view = input->mechanisms;
	uint32_t index;

#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TESTING)
	sg_portal_merge_steps = 0U;
#endif

	if (view == NULL || !CompactIdentityEqual(&view->identity,
		&input->geometry->identity) ||
		(view->mechanism_count != 0U && view->mechanisms == NULL) ||
		(view->controller_count != 0U && view->controllers == NULL) ||
		(view->topology_edge_count != 0U && view->topology_edges == NULL) ||
		(view->transition_count != 0U && view->transitions == NULL))
		goto invalid;
	for (index = 0U; index < view->mechanism_count; index++) {
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[index];

		if ((uint32_t)authority->kind >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT ||
			authority->source.entity_ordinal >=
				input->geometry->identity.source_counts.entity_count ||
			(authority->activation &
				(sg_rune_compact_mechanism_activation_mask_t)
				~SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
			authority->activation_cell.value >= input->cell_count ||
			!Q8VectorInsideBounds(&authority->activation_witness,
				&authority->activation_bounds) ||
			!SpanWithin(authority->controllers.first,
				authority->controllers.count, view->controller_count) ||
			!SpanWithin(authority->topology.first, authority->topology.count,
				view->topology_edge_count) ||
			!SpanWithin(authority->transitions.first,
				authority->transitions.count, view->transition_count) ||
			(uint32_t)authority->initial_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			(uint32_t)authority->activated_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			(uint32_t)authority->reset_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			(authority->flags &
				(sg_rune_compact_mechanism_authority_flags_t)
				~SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN) != 0U)
			goto invalid_record;
	}
	for (index = 0U; index < view->controller_count; index++) {
		const sg_rune_compact_mechanism_controller_t *controller =
			&view->controllers[index];

		if (controller->mechanism >= view->mechanism_count ||
			controller->controller.entity_ordinal >=
				input->geometry->identity.source_counts.entity_count ||
			controller->topology_edge >= view->topology_edge_count ||
			controller->spatiality >=
				SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
			(controller->spatiality ==
				SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
				(controller->activation_cell.value !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				 !BytesZero(&controller->activation_witness,
					sizeof(controller->activation_witness)) ||
				 !BytesZero(&controller->activation_bounds,
					sizeof(controller->activation_bounds))) :
				(controller->activation_cell.value >= input->cell_count ||
				 !Q8VectorInsideBounds(&controller->activation_witness,
					&controller->activation_bounds))))
			goto invalid_record;
	}
	for (index = 0U; index < view->topology_edge_count; index++) {
		const sg_rune_compact_mechanism_topology_edge_t *edge =
			&view->topology_edges[index];

		if (edge->source.entity_ordinal >=
				input->geometry->identity.source_counts.entity_count ||
			edge->destination.entity_ordinal >=
				input->geometry->identity.source_counts.entity_count ||
			edge->kind < SG_MECH_EDGE_TARGET ||
			edge->kind > SG_MECH_EDGE_ROUTE_TARGET)
			goto invalid_record;
	}
	for (index = 0U; index < view->transition_count; index++) {
		const sg_rune_compact_mechanism_transition_t *transition =
			&view->transitions[index];
		const sg_rune_compact_mechanism_authority_t *authority;
		uint32_t axis;

		if (transition->mechanism >= view->mechanism_count ||
			(uint32_t)transition->kind >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT ||
			transition->entry_cell.value >= input->cell_count ||
			transition->exit_cell.value >= input->cell_count ||
			(uint32_t)transition->source_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			(uint32_t)transition->destination_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT)
			goto invalid_record;
		authority = &view->mechanisms[transition->mechanism];
		if (!MechanismTransitionKindMatches(input, authority,
			transition->kind) || index < authority->transitions.first ||
			index >= authority->transitions.first + authority->transitions.count ||
			!MechanismTransitionStatesValid(input, authority, transition))
			goto invalid_record;
		switch (transition->kind) {
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE: {
			const sg_rune_compact_mechanism_portal_state_t *state =
				&transition->value.portal_state;
			uint32_t negative;
			uint32_t positive;

			if (state->portal.value >= input->portal_count ||
				state->mover_model >=
					input->geometry->identity.source_counts.model_count ||
				state->source_blocked > 1U ||
				state->destination_blocked > 1U ||
				state->source_blocked == state->destination_blocked ||
				state->reserved[0] != 0U || state->reserved[1] != 0U ||
				state->delay_ms != authority->delay_ms ||
				state->dwell_ms != authority->dwell_ms ||
				state->pause_ms != authority->pause_ms ||
				state->travel_ms != authority->travel_ms ||
				state->recovery_ms != authority->recovery_ms ||
				transition->elapsed_ms == 0U ||
				!GetPortalCells(input, state->portal.value, &negative, &positive) ||
				!((transition->entry_cell.value == negative &&
					transition->exit_cell.value == positive) ||
				  (transition->entry_cell.value == positive &&
					transition->exit_cell.value == negative)))
				goto invalid_record;
			break;
		}
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT: {
			const sg_rune_compact_mechanism_teleport_t *teleport =
				&transition->value.teleport;

			if (transition->elapsed_ms != 0U || authority->delay_ms != 0U ||
				authority->dwell_ms != 0U || authority->pause_ms != 0U ||
				authority->travel_ms != 0U || authority->recovery_ms != 0U ||
				teleport->destination.entity_ordinal >=
					input->geometry->identity.source_counts.entity_count ||
				teleport->fanout_ordinal == SG_RUNE_COMPACT_INDEX_NONE ||
				!Q8VectorInsideBounds(&teleport->approach_witness,
					&input->cells[transition->entry_cell.value].bounds) ||
				!Q8VectorInsideBounds(&teleport->entry_witness,
					&input->cells[transition->entry_cell.value].bounds) ||
				!Q8VectorInsideBounds(&teleport->exit_witness,
					&input->cells[transition->exit_cell.value].bounds))
				goto invalid_record;
			for (axis = 0U; axis < 3U; axis++)
				if (teleport->arrival_velocity_bits[axis] != 0U)
					goto invalid_record;
			break;
		}
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH: {
			const sg_rune_compact_mechanism_push_t *push =
				&transition->value.push;

			if (push->flight_ms == 0U ||
				transition->elapsed_ms != (uint64_t)push->flight_ms ||
				!Q8VectorInsideBounds(&push->approach_witness,
					&input->cells[transition->entry_cell.value].bounds) ||
				!Q8VectorInsideBounds(&push->entry_witness,
					&input->cells[transition->entry_cell.value].bounds) ||
				!Q8VectorInsideBounds(&push->exit_witness,
					&input->cells[transition->exit_cell.value].bounds) ||
				!Binary32Canonical(push->gravity_bits) ||
				(push->gravity_bits & UINT32_C(0x80000000)) != 0U ||
				push->gravity_bits !=
					input->geometry->identity.physics.gravity_bits)
				goto invalid_record;
			for (axis = 0U; axis < 3U; axis++)
				if (!Binary32FiniteBits(push->launch_velocity_bits[axis]))
					goto invalid_record;
			break;
		}
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT: {
			const sg_rune_compact_mechanism_transport_t *transport =
				&transition->value.transport;

			if (!TransportProvenanceValid(input, authority, transport) ||
				(transport->source_endpoint.entity_ordinal !=
					SG_RUNE_COMPACT_INDEX_NONE &&
				 transport->source_endpoint.entity_ordinal >=
					input->geometry->identity.source_counts.entity_count) ||
				(transport->destination_endpoint.entity_ordinal !=
					SG_RUNE_COMPACT_INDEX_NONE &&
				 transport->destination_endpoint.entity_ordinal >=
					input->geometry->identity.source_counts.entity_count) ||
				(authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
				 (transport->source_endpoint.entity_ordinal ==
					SG_RUNE_COMPACT_INDEX_NONE ||
				  transport->destination_endpoint.entity_ordinal ==
					SG_RUNE_COMPACT_INDEX_NONE ||
				  transport->source_endpoint.entity_ordinal ==
					transport->destination_endpoint.entity_ordinal ||
				  transport->fanout_ordinal == SG_RUNE_COMPACT_INDEX_NONE)) ||
				(authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT &&
				 (transport->source_endpoint.entity_ordinal !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				  transport->destination_endpoint.entity_ordinal !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				  transport->fanout_ordinal != SG_RUNE_COMPACT_INDEX_NONE)) ||
				transition->elapsed_ms == 0U ||
				transport->swept_static_clear != 1U ||
				transport->start_supported != 1U ||
				transport->end_supported != 1U ||
				transport->stance >= SG_RUNE_STANCE_COUNT)
				goto invalid_record;
			break;
		}
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
			goto invalid_record;
		}
	}
	for (index = 0U; index < view->mechanism_count; index++) {
		const sg_rune_compact_mechanism_authority_t *authority =
			&view->mechanisms[index];
		uint32_t offset;

		for (offset = 0U; offset < authority->transitions.count; offset++)
			if (view->transitions[authority->transitions.first + offset].mechanism !=
				index)
				goto invalid_record;
	}
	/* Both projections are canonical by mechanism and then portal.  Merge them
	 * once so every BLOCKS root names exactly one typed portal transition. */
	{
		uint32_t binding_cursor = 0U;
		uint32_t transition_cursor = 0U;

		while (binding_cursor < input->static_data->portal_mechanism_count ||
			transition_cursor < input->static_data->transition_count) {
			const sg_rune_compact_portal_mechanism_t *binding;
			const sg_rune_compact_static_transition_t *transition;

			while (transition_cursor < input->static_data->transition_count &&
				input->static_data->transitions[transition_cursor].kind !=
					SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE) {
				SG_PORTAL_MERGE_STEP();
				transition_cursor++;
			}
			if (binding_cursor == input->static_data->portal_mechanism_count &&
				transition_cursor == input->static_data->transition_count)
				break;
			if (binding_cursor == input->static_data->portal_mechanism_count ||
				transition_cursor == input->static_data->transition_count)
				goto invalid_record;
			binding = &input->static_data->portal_mechanisms[binding_cursor];
			transition = &input->static_data->transitions[transition_cursor];
			SG_PORTAL_MERGE_STEP();
			if (binding->mechanism.value != transition->mechanism.value ||
				binding->portal.value !=
					transition->value.portal_state.portal.value)
				goto invalid_record;
			binding_cursor++;
			transition_cursor++;
		}
	}
	return 1;

invalid_record:
	SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
		index, 1U, 0U);
	return 0;
invalid:
	SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
		0U, 1U, 0U);
	return 0;
}

static int ValidateVisibility(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_static_visibility_t *visibility = input->visibility;
	const uint32_t configuration_cell_count = ConfigurationCellCount(input);
	uint8_t *partition_seen;
	uint32_t index;

	if ((visibility->partition_count != 0U && visibility->partitions == NULL) ||
		(visibility->area_count != 0U && visibility->area_components == NULL) ||
		(visibility->occluder_count != 0U && visibility->occluders == NULL) ||
		(visibility->surface_count != 0U && visibility->surfaces == NULL)) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
			0U, 0U, 1U);
		return 0;
	}
	partition_seen = calloc(input->configuration_semantics->region_count,
		sizeof(*partition_seen));
	if (partition_seen == NULL) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, input->configuration_semantics->region_count, 0U);
		return 0;
	}
	for (index = 0U; index < visibility->partition_count; index++) {
		const sg_static_visibility_partition_t *partition =
			&visibility->partitions[index];

		if (partition->configuration_region >=
			input->configuration_semantics->region_count ||
			partition->configuration_cell >= configuration_cell_count ||
			partition_seen[partition->configuration_region] != 0U ||
			partition->bsp_leaf >=
			input->geometry->identity.source_counts.leaf_count ||
			partition->bsp_area >=
			input->geometry->identity.source_counts.area_count ||
			(visibility->area_count != 0U &&
				partition->bsp_area >= visibility->area_count) ||
			input->configuration_semantics->regions[
				partition->configuration_region].cell !=
				partition->configuration_cell) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
				index, configuration_cell_count, partition->configuration_cell);
			free(partition_seen);
			return 0;
		}
		partition_seen[partition->configuration_region] = 1U;
	}
	if (visibility->partition_count !=
		input->configuration_semantics->region_count ||
		visibility->area_count !=
			input->geometry->identity.source_counts.area_count ||
		visibility->surface_count !=
			input->configuration_semantics->hook_surface_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
			0U, input->configuration_semantics->region_count,
			visibility->partition_count);
		free(partition_seen);
		return 0;
	}
	for (index = 0U; index < visibility->area_count; index++) {
		if (visibility->area_components[index] >= visibility->area_count ||
			visibility->area_components[index] >=
				input->geometry->identity.source_counts.area_count) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
				index, visibility->area_count,
				visibility->area_components[index]);
			free(partition_seen);
			return 0;
		}
	}
	for (index = 0U; index < visibility->occluder_count; index++)
		if (visibility->occluders[index].conditional > 1U) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
				index, 1U, visibility->occluders[index].conditional);
			free(partition_seen);
			return 0;
		}
	for (index = 0U; index < visibility->surface_count; index++) {
		const sg_static_visibility_surface_t *published =
			&visibility->surfaces[index];
		const sg_configuration_hook_surface_t *source =
			&input->configuration_semantics->hook_surfaces[index];

		if (published->semantic_surface != index || published->id != source->id ||
			published->model != source->model || published->brush != source->brush ||
			published->brush_side != source->brush_side ||
			published->flags != source->flags) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, index,
				index, published->semantic_surface);
			free(partition_seen);
			return 0;
		}
	}
	free(partition_seen);
	return 1;
}

static int SemanticRegionContainsPoint(
	const sg_configuration_semantics_t *configuration,
	const sg_configuration_semantic_region_t *region, const float point[3])
{
	uint32_t face;

	if (configuration == NULL || region == NULL || point == NULL ||
		!FloatBoundsContain(&region->bounds, point))
		return 0;
	for (face = 0U; face < region->face_count; face++)
		if (!SG_ConfigurationSemanticFaceContainsPoint(
			&configuration->faces[region->first_face + face], point))
			return 0;
	return 1;
}

static int SelectRegionForCompactCell(
	const sg_rune_compact_movement_fields_input_t *input, uint32_t compact_cell,
	uint32_t *region_out)
{
	const sg_configuration_semantics_t *configuration =
		input->configuration_semantics;
	const sg_rune_compact_cell_t *cell = &input->cells[compact_cell];
	float center[3];
	uint32_t best_region = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
	uint32_t best_score = 0U;
	uint32_t region_index;

	center[0] = ((float)cell->bounds.mins.value[0] +
		(float)cell->bounds.maxs.value[0]) * 0.0625f;
	center[1] = ((float)cell->bounds.mins.value[1] +
		(float)cell->bounds.maxs.value[1]) * 0.0625f;
	center[2] = ((float)cell->bounds.mins.value[2] +
		(float)cell->bounds.maxs.value[2]) * 0.0625f;
	for (region_index = 0U; region_index < configuration->region_count;
		region_index++) {
		const sg_configuration_semantic_region_t *region =
			&configuration->regions[region_index];
		uint32_t score = 0U;
		uint32_t sample;

		if (!CompactCellMappedToConfigurationCell(input, region->cell,
			compact_cell))
			continue;
		if (SemanticRegionContainsPoint(configuration, region, center))
			score += 64U;
		else if (Q8BoundsOverlapFloat(&cell->bounds, &region->bounds))
			score += 8U;
		if (Q8BoundsContainPoint(&cell->bounds,
			region->interior_witness.value))
			score += 64U;
		for (sample = 0U; sample < 3U; sample++) {
			if (region->sample_leaves[sample] == cell->source.leaf)
				score += sample == 0U ? 16U : 4U;
			if (region->sample_areas[sample] == cell->source.area)
				score += sample == 0U ? 8U : 2U;
			if (region->sample_clusters[sample] == cell->source.cluster)
				score += sample == 0U ? 4U : 1U;
		}
		if (best_region == SG_CONFIGURATION_SEMANTICS_INDEX_NONE ||
			score > best_score ||
			(score == best_score && region_index < best_region)) {
			best_region = region_index;
			best_score = score;
		}
	}
	if (best_region == SG_CONFIGURATION_SEMANTICS_INDEX_NONE)
		return 0;
	*region_out = best_region;
	return 1;
}

static int ValidateHostLaw(const sg_host_law_view_t *host,
	sg_rune_compact_movement_fields_error_t *error)
{
	const sg_rune_physics_parameters_t *physics;

	if (host->version != SG_HOST_LAW_PUBLICATION_VERSION ||
		host->reserved != 0U || host->collision_law_id == 0U ||
		host->pmove_law_id == 0U || host->gravity_law_id == 0U ||
		host->hook_law_id == 0U || host->mechanism_law_id == 0U ||
		host->static_identity.physics_abi_id == 0U ||
		!SG_HostHookLawValid(&host->hook) ||
		!SG_HostMechanismLawValid(&host->mechanism) ||
		host->hook_law_id != host->hook.identity ||
		host->mechanism_law_id != host->mechanism.identity ||
		host->static_identity.reserved != 0U ||
		!HullValid(&host->static_identity.standing_hull) ||
		!HullValid(&host->static_identity.crouching_hull) ||
		host->hook_fire_speed != host->hook.fire_speed ||
		host->hook_pull_speed != host->hook.pull_speed ||
		host->hook_initial_damage != host->hook.initial_damage ||
		host->hook_attached_damage != host->hook.attached_damage ||
		host->hook_health != host->hook.projectile_health ||
		!ScalarValid(host->airaccelerate) ||
		(host->airaccelerate != 0.0f && host->airaccelerate != 1.0f)) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW,
			0U, 1U, 0U);
		return 0;
	}
	physics = &host->static_identity.physics;
	if (!PositiveFinite(physics->gravity) ||
		!NonnegativeFinite(physics->ground_acceleration) ||
		!NonnegativeFinite(physics->air_acceleration) ||
		!NonnegativeFinite(physics->water_acceleration) ||
		!NonnegativeFinite(physics->hook_acceleration) ||
		!NonnegativeFinite(physics->external_acceleration) ||
		!NonnegativeFinite(physics->water_drag) ||
		FloatBits(physics->air_acceleration) != FloatBits(
			host->airaccelerate == 0.0f ?
				SG_HOST_ENGINE_AIR_ACCELERATION :
				SG_HOST_ENGINE_GROUND_ACCELERATION) ||
		!PositiveFinite(physics->max_velocity) || physics->frame_ms == 0U ||
		physics->substep_ms == 0U ||
		physics->frame_ms % physics->substep_ms != 0U ||
		host->mechanism.frame_ms != physics->frame_ms ||
		!PositiveFinite(host->maxvelocity) ||
		FloatBits(host->maxvelocity) != FloatBits(physics->max_velocity) ||
		host->hook_fire_speed == 0U || host->hook_pull_speed == 0U) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW,
			0U, FloatBits(physics->max_velocity),
			FloatBits(host->maxvelocity));
		return 0;
	}
	return 1;
}

static int ValidateInput(const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_error_t *error)
{
	uint32_t index;

	if (input == NULL || input->cells == NULL || input->facets == NULL ||
		input->incidences == NULL ||
		input->static_data == NULL || input->static_owner == NULL ||
		input->response_partition == NULL || input->mechanisms == NULL ||
		input->configuration_semantics == NULL ||
		input->visibility == NULL || input->host_law == NULL ||
		input->cell_count == 0U || input->facet_count == 0U ||
		input->incidence_count == 0U ||
		input->cell_count > SG_RUNE_COMPACT_MAX_CELLS ||
		input->facet_count > SG_RUNE_COMPACT_MAX_FACETS ||
		input->incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		input->portal_count > SG_RUNE_COMPACT_MAX_PORTALS ||
		input->reserved[0] != 0U || input->reserved[1] != 0U ||
		input->reserved[2] != 0U) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT,
			0U, 0U, 1U);
		return 0;
	}
	if (!ValidateOwnerBindings(input, error))
		return 0;
	if (input->portal_count != 0U && input->portals == NULL) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT,
			0U, 1U, 0U);
		return 0;
	}
	if (!ValidateIdentityBinding(input, error))
		return 0;
	for (index = 0U; index < input->cell_count; index++) {
		const sg_rune_compact_cell_t *cell = &input->cells[index];

		if (!BoundsValid(&cell->bounds) || !StancesValid(cell->valid_stances) ||
			(cell->contents & (sg_rune_compact_contents_mask_t)
				~SG_RUNE_COMPACT_CONTENTS_KNOWN) != 0U ||
			(cell->semantics & (sg_rune_compact_cell_semantics_t)
				~SG_RUNE_COMPACT_CELL_SEMANTICS_KNOWN) != 0U ||
			cell->reserved[0] != 0U || cell->reserved[1] != 0U ||
			cell->reserved[2] != 0U) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				index, 1U, 0U);
			return 0;
		}
	}
	return ValidateFacets(input, error) &&
		ValidateIncidencesAndPortals(input, error) &&
		ValidateConfiguration(input, error) && ValidateStaticData(input, error) &&
		ValidateVisibility(input, error) && ValidateResponsePartition(input,
			error) && ValidateMechanisms(input, error) &&
		ValidateHostLaw(input->host_law, error);
}

static int BuildIndexes(const sg_rune_compact_movement_fields_input_t *input,
	index_workspace_t *index,
	sg_rune_compact_movement_fields_error_t *error)
{
	uint32_t cell;
	uint32_t portal;
	uint32_t record;
	uint32_t *portal_cursors;
	uint8_t *static_transition_claimed;

	memset(index, 0, sizeof(*index));
	index->region_by_cell = AllocateArray(input->cell_count,
		sizeof(*index->region_by_cell));
	index->cell_portal_counts = AllocateArray(input->cell_count,
		sizeof(*index->cell_portal_counts));
	index->transition_profiles = AllocateArray(
		input->static_data->transition_count, sizeof(*index->transition_profiles));
	index->authority_transition_static = AllocateArray(
		input->mechanisms->transition_count,
		sizeof(*index->authority_transition_static));
	index->water_profiles = AllocateArray(input->cell_count,
		sizeof(*index->water_profiles));
	index->partition_by_cell = AllocateArray(input->cell_count,
		sizeof(*index->partition_by_cell));
	if (index->region_by_cell == NULL || index->cell_portal_counts == NULL ||
		index->water_profiles == NULL || index->partition_by_cell == NULL ||
		(input->static_data->transition_count != 0U &&
			index->transition_profiles == NULL) ||
		(input->mechanisms->transition_count != 0U &&
			index->authority_transition_static == NULL)) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, 0U, 0U);
		return 0;
	}
	for (cell = 0U; cell < input->cell_count; cell++)
	{
		index->region_by_cell[cell] = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
		index->partition_by_cell[cell] = SG_STATIC_VISIBILITY_INDEX_NONE;
		index->water_profiles[cell] = SG_STATIC_VISIBILITY_INDEX_NONE;
	}
	for (record = 0U; record < input->static_data->transition_count; record++)
		index->transition_profiles[record] = SG_STATIC_VISIBILITY_INDEX_NONE;
	if (input->static_data->transition_count !=
		input->mechanisms->transition_count) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
			0U, input->mechanisms->transition_count,
			input->static_data->transition_count);
		return 0;
	}
	static_transition_claimed = AllocateArray(
		input->static_data->transition_count,
		sizeof(*static_transition_claimed));
	if (input->static_data->transition_count != 0U &&
		static_transition_claimed == NULL) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, input->static_data->transition_count, 0U);
		return 0;
	}
	for (record = 0U; record < input->mechanisms->transition_count; record++) {
		uint32_t static_index;

		if (!SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
				input->static_owner, record, &static_index) ||
			static_index >= input->static_data->transition_count ||
			static_transition_claimed[static_index] != 0U) {
			free(static_transition_claimed);
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				record, 1U, 0U);
			return 0;
		}
		static_transition_claimed[static_index] = 1U;
		index->authority_transition_static[record] = static_index;
	}
	for (record = 0U; record < input->static_data->transition_count; record++)
		if (static_transition_claimed[record] == 0U) {
			free(static_transition_claimed);
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
				record, 1U, 0U);
			return 0;
		}
	free(static_transition_claimed);
	for (cell = 0U; cell < input->cell_count; cell++)
		if (!SelectRegionForCompactCell(input, cell,
			&index->region_by_cell[cell])) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
				cell, 1U, 0U);
			return 0;
		}
	/* Visibility partitions are semantic-region records.  Expand each
	 * configuration-cell span and retain only the partition selected for the
	 * particular compact cell; duplicate configuration-cell names are expected
	 * when stance/constraint regions share one source cell. */
	for (record = 0U; record < input->visibility->partition_count; record++) {
		const sg_static_visibility_partition_t *partition =
			&input->visibility->partitions[record];
		const sg_rune_compact_geometry_cell_span_t *span;
		uint32_t offset;

		if (!ConfigurationCellSpan(input, partition->configuration_cell, &span)) {
			SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
				record, ConfigurationCellCount(input),
				partition->configuration_cell);
			return 0;
		}
		if (span == NULL) {
			const uint32_t mapped = partition->configuration_cell;

			if (mapped >= input->cell_count ||
				index->region_by_cell[mapped] != partition->configuration_region)
				continue;
			if (index->partition_by_cell[mapped] !=
				SG_STATIC_VISIBILITY_INDEX_NONE) {
				SetError(error,
					SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
					mapped, SG_STATIC_VISIBILITY_INDEX_NONE, record);
				return 0;
			}
			index->partition_by_cell[mapped] = record;
			continue;
		}
		for (offset = 0U; offset < span->count; offset++) {
			const uint32_t mapped = input->geometry->
				configuration_cell_compact_cells[span->first + offset].value;

			if (mapped >= input->cell_count ||
				index->region_by_cell[mapped] != partition->configuration_region)
				continue;
			if (index->partition_by_cell[mapped] !=
				SG_STATIC_VISIBILITY_INDEX_NONE) {
				SetError(error,
					SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
					mapped, SG_STATIC_VISIBILITY_INDEX_NONE, record);
				return 0;
			}
			index->partition_by_cell[mapped] = record;
		}
	}
	for (cell = 0U; cell < input->cell_count; cell++)
		if (index->partition_by_cell[cell] == SG_STATIC_VISIBILITY_INDEX_NONE) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY, cell,
				1U, 0U);
			return 0;
		}
	for (portal = 0U; portal < input->portal_count; portal++) {
		uint32_t negative;
		uint32_t positive;

		if (!GetPortalCells(input, portal, &negative, &positive)) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				portal, 1U, 0U);
			return 0;
		}
		if (index->cell_portal_counts[negative] == UINT32_MAX ||
			index->cell_portal_counts[positive] == UINT32_MAX) {
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_LIMIT_EXCEEDED,
				portal, UINT32_MAX, 0U);
			return 0;
		}
		index->cell_portal_counts[negative]++;
		index->cell_portal_counts[positive]++;
	}
	if (input->portal_count > UINT32_MAX / 2U ||
		!PrefixOffsets(index->cell_portal_counts, input->cell_count,
			input->portal_count * 2U, &index->cell_portal_offsets,
			&index->cell_portals)) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, 0U, 0U);
		return 0;
	}
	portal_cursors = AllocateArray(input->cell_count, sizeof(*portal_cursors));
	if (portal_cursors == NULL) {
		free(portal_cursors);
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, 0U, 0U);
		return 0;
	}
	for (cell = 0U; cell < input->cell_count; cell++)
		portal_cursors[cell] = index->cell_portal_offsets[cell];
	for (portal = 0U; portal < input->portal_count; portal++) {
		uint32_t negative;
		uint32_t positive;

		if (!GetPortalCells(input, portal, &negative, &positive)) {
			free(portal_cursors);
			SetError(error,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
				portal, 1U, 0U);
			return 0;
		}
		index->cell_portals[portal_cursors[negative]++] = portal;
		index->cell_portals[portal_cursors[positive]++] = portal;
	}
	free(portal_cursors);

	return BuildHookRefs(input, index, error);
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareSpecs(const function_spec_t *left, const function_spec_t *right)
{
	uint32_t index;
	int comparison;

	comparison = CompareU32((uint32_t)left->form, (uint32_t)right->form);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->output,
			(uint32_t)right->output);
	if (comparison == 0)
		comparison = CompareU32(left->input_count, right->input_count);
	for (index = 0U; comparison == 0 && index < left->input_count; index++)
		comparison = CompareU32((uint32_t)left->dimensions[index],
			(uint32_t)right->dimensions[index]);
	if (comparison != 0)
		return comparison;
	if (left->form == SG_RUNE_COMPACT_ANALYTIC_AFFINE) {
		comparison = CompareU32(left->bias.bits, right->bias.bits);
		for (index = 0U; comparison == 0 && index < left->input_count; index++)
			comparison = CompareU32(left->slopes[index].bits,
				right->slopes[index].bits);
	} else if (left->form == SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL) {
		comparison = CompareU32((uint32_t)left->degree,
			(uint32_t)right->degree);
		if (comparison == 0)
			comparison = CompareU32(left->value_count, right->value_count);
		for (index = 0U; comparison == 0 && index < left->value_count;
			index++)
			comparison = CompareU32(left->coefficients[index].bits,
				right->coefficients[index].bits);
	} else if (left->form == SG_RUNE_COMPACT_ANALYTIC_BALLISTIC) {
		comparison = CompareU32(left->initial.bits, right->initial.bits);
		if (comparison == 0)
			comparison = CompareU32(left->first_derivative.bits,
				right->first_derivative.bits);
		if (comparison == 0)
			comparison = CompareU32(left->half_second_derivative.bits,
				right->half_second_derivative.bits);
	} else if (left->form == SG_RUNE_COMPACT_ANALYTIC_PIECEWISE) {
		comparison = CompareU32(left->piecewise_selector_input,
			right->piecewise_selector_input);
		if (comparison == 0)
			comparison = CompareU32(left->piecewise_default_spec,
				right->piecewise_default_spec);
		if (comparison == 0)
			comparison = CompareU32(left->piecewise_clause_count,
				right->piecewise_clause_count);
		for (index = 0U; comparison == 0 && index <
			left->piecewise_clause_count; index++) {
			const piecewise_clause_spec_t *left_clause =
				&left->piecewise_clauses[index];
			const piecewise_clause_spec_t *right_clause =
				&right->piecewise_clauses[index];

			comparison = CompareU32(left_clause->lower.bits,
				right_clause->lower.bits);
			if (comparison == 0)
				comparison = CompareU32(left_clause->upper.bits,
					right_clause->upper.bits);
			if (comparison == 0)
				comparison = CompareU32(left_clause->function_spec,
					right_clause->function_spec);
			if (comparison == 0)
				comparison = CompareU32((uint32_t)left_clause->ownership,
					(uint32_t)right_clause->ownership);
		}
	}
	return comparison;
}

static int SpecsEqual(const function_spec_t *left, const function_spec_t *right)
{
	return CompareSpecs(left, right) == 0;
}

static int DimensionsSorted(const sg_rune_analytic_input_dimension_t *dimensions,
	uint32_t count)
{
	uint32_t index;

	if (count == 0U || count > SG_RUNE_ANALYTIC_MAX_INPUTS)
		return 0;
	for (index = 0U; index < count; index++)
		if ((uint32_t)dimensions[index] >=
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT ||
			(index != 0U && dimensions[index - 1U] >= dimensions[index]))
			return 0;
	return 1;
}

static int AppendSpec(analytic_workspace_t *workspace,
	const function_spec_t *candidate, uint32_t *spec_out)
{
	uint32_t index;
	uint32_t next_count;

	for (index = 0U; index < workspace->spec_count; index++)
		if (SpecsEqual(&workspace->specs[index], candidate)) {
			free(candidate->coefficients);
			free(candidate->piecewise_clauses);
			*spec_out = index;
			return 1;
		}
	if (!AddU32(workspace->spec_count, 1U, &next_count) ||
		!WorkspaceGrow(workspace, (void **)&workspace->specs,
			&workspace->spec_capacity,
			next_count, sizeof(*workspace->specs))) {
		free(candidate->coefficients);
		free(candidate->piecewise_clauses);
		return 0;
	}
	workspace->specs[workspace->spec_count] = *candidate;
	*spec_out = workspace->spec_count;
	workspace->spec_count = next_count;
	return 1;
}

static int AppendAffineSpec(analytic_workspace_t *workspace,
	sg_rune_analytic_output_meaning_t output,
	const sg_rune_analytic_input_dimension_t *dimensions,
	uint32_t input_count, float bias, const float *slopes,
	uint32_t *spec_out)
{
	function_spec_t candidate;
	uint32_t index;

	if (workspace == NULL || dimensions == NULL || slopes == NULL ||
		spec_out == NULL || !DimensionsSorted(dimensions, input_count) ||
		!ScalarValid(bias) ||
		(uint32_t)output >= (uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT) {
		return 0;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	candidate.output = output;
	candidate.input_count = input_count;
	candidate.bias = Scalar(bias);
	for (index = 0U; index < input_count; index++) {
		if (!ScalarValid(slopes[index]))
			return 0;
		candidate.dimensions[index] = dimensions[index];
		candidate.slopes[index] = Scalar(slopes[index]);
	}
	return AppendSpec(workspace, &candidate, spec_out);
}

static int AppendPolynomialSpec(analytic_workspace_t *workspace,
	sg_rune_analytic_output_meaning_t output,
	const sg_rune_analytic_input_dimension_t *dimensions,
	uint32_t input_count, uint8_t degree,
	const float *coefficients, uint32_t coefficient_count,
	uint32_t *spec_out)
{
	function_spec_t candidate;
	uint32_t index;

	if (workspace == NULL || dimensions == NULL || coefficients == NULL ||
		spec_out == NULL || !DimensionsSorted(dimensions, input_count) ||
		degree < 2U || degree > SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_DEGREE ||
		coefficient_count != SG_RuneAnalyticPolynomialCoefficientCount(
			input_count, degree) ||
		(uint32_t)output >= (uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT) {
		return 0;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.form = SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL;
	candidate.output = output;
	candidate.input_count = input_count;
	candidate.degree = degree;
	candidate.value_count = coefficient_count;
	candidate.coefficients = AllocateArray(coefficient_count,
		sizeof(*candidate.coefficients));
	if (candidate.coefficients == NULL) {
		workspace->allocation_failed = 1;
		return 0;
	}
	for (index = 0U; index < input_count; index++)
		candidate.dimensions[index] = dimensions[index];
	for (index = 0U; index < coefficient_count; index++) {
		if (!ScalarValid(coefficients[index])) {
			free(candidate.coefficients);
			return 0;
		}
		candidate.coefficients[index] = Scalar(coefficients[index]);
	}
	return AppendSpec(workspace, &candidate, spec_out);
}

static int SpecsUseSameInputs(const function_spec_t *left,
	const function_spec_t *right)
{
	uint32_t index;

	if (left->input_count != right->input_count)
		return 0;
	for (index = 0U; index < left->input_count; index++)
		if (left->dimensions[index] != right->dimensions[index])
			return 0;
	return 1;
}

static int AppendPiecewiseSpec(analytic_workspace_t *workspace,
	sg_rune_analytic_output_meaning_t output,
	const sg_rune_analytic_input_dimension_t *dimensions, uint32_t input_count,
	uint32_t selector_input, uint32_t default_spec,
	const float *lowers, const float *uppers, const uint32_t *child_specs,
	const sg_rune_analytic_interval_ownership_t *ownership,
	uint32_t clause_count, uint32_t *spec_out)
{
	function_spec_t candidate;
	uint32_t index;

	if (workspace == NULL || dimensions == NULL || lowers == NULL ||
		uppers == NULL || child_specs == NULL || ownership == NULL ||
		spec_out == NULL || !DimensionsSorted(dimensions, input_count) ||
		selector_input >= input_count || clause_count == 0U ||
		default_spec >= workspace->spec_count ||
		(uint32_t)output >= (uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT) {
		return 0;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.form = SG_RUNE_COMPACT_ANALYTIC_PIECEWISE;
	candidate.output = output;
	candidate.input_count = input_count;
	candidate.piecewise_selector_input = selector_input;
	candidate.piecewise_default_spec = default_spec;
	candidate.piecewise_clause_count = clause_count;
	candidate.piecewise_clauses = AllocateArray(clause_count,
		sizeof(*candidate.piecewise_clauses));
	if (candidate.piecewise_clauses == NULL) {
		workspace->allocation_failed = 1;
		return 0;
	}
	for (index = 0U; index < input_count; index++)
		candidate.dimensions[index] = dimensions[index];
	if (workspace->specs[default_spec].form ==
			SG_RUNE_COMPACT_ANALYTIC_PIECEWISE ||
		workspace->specs[default_spec].output != output ||
			!SpecsUseSameInputs(&workspace->specs[default_spec], &candidate)) {
		free(candidate.piecewise_clauses);
		return 0;
	}
	for (index = 0U; index < clause_count; index++) {
		const uint32_t child = child_specs[index];

		if (!ScalarValid(lowers[index]) || !ScalarValid(uppers[index]) ||
			!(lowers[index] < uppers[index]) ||
			(uint32_t)ownership[index] >=
				(uint32_t)SG_RUNE_ANALYTIC_INTERVAL_OWNERSHIP_COUNT ||
			child >= workspace->spec_count ||
			workspace->specs[child].form ==
				SG_RUNE_COMPACT_ANALYTIC_PIECEWISE ||
			workspace->specs[child].output != output ||
				!SpecsUseSameInputs(&workspace->specs[child], &candidate)) {
				free(candidate.piecewise_clauses);
			return 0;
		}
		candidate.piecewise_clauses[index].lower = Scalar(lowers[index]);
		candidate.piecewise_clauses[index].upper = Scalar(uppers[index]);
		candidate.piecewise_clauses[index].function_spec = child;
		candidate.piecewise_clauses[index].ownership = ownership[index];
		if (index != 0U && lowers[index] < uppers[index - 1U]) {
			free(candidate.piecewise_clauses);
			return 0;
		}
	}
	return AppendSpec(workspace, &candidate, spec_out);
}

static int PolynomialTermIndex(uint32_t input_count, uint8_t degree,
	const uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS], uint32_t *index_out)
{
	uint8_t candidate[SG_RUNE_ANALYTIC_MAX_INPUTS];
	const uint32_t count = SG_RuneAnalyticPolynomialCoefficientCount(
		input_count, degree);
	uint32_t index;

	if (exponents == NULL || index_out == NULL || count == 0U)
		return 0;
	for (index = 0U; index < count; index++)
		if (SG_RuneAnalyticPolynomialExponentAt(input_count, degree, index,
			candidate, SG_RUNE_ANALYTIC_MAX_INPUTS) &&
			memcmp(candidate, exponents, input_count) == 0) {
			*index_out = index;
			return 1;
		}
	return 0;
}

static int SetPolynomialTerm(float *coefficients, uint32_t input_count,
	uint8_t degree, const uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS],
	float value)
{
	uint32_t index;

	if (coefficients == NULL || exponents == NULL || !ScalarValid(value) ||
		!PolynomialTermIndex(input_count, degree, exponents, &index))
	{
		return 0;
	}
	coefficients[index] = value;
	return 1;
}

static int AddProfileSpec(analytic_workspace_t *workspace, profile_t *profile,
	sg_rune_analytic_output_meaning_t output,
	const sg_rune_analytic_input_dimension_t *dimensions,
	uint32_t input_count, float bias, const float *slopes)
{
	uint32_t spec;

	if (!AppendAffineSpec(workspace, output, dimensions, input_count, bias,
		slopes, &spec) || !ProfileAppend(profile, spec))
		return 0;
	return 1;
}

static int AppendBallisticSpec(analytic_workspace_t *workspace,
	sg_rune_analytic_output_meaning_t output, float initial,
	float first_derivative, float half_second_derivative, uint32_t *spec_out)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS
	};
	function_spec_t candidate;

	if (workspace == NULL || spec_out == NULL || !ScalarValid(initial) ||
		!ScalarValid(first_derivative) || !ScalarValid(half_second_derivative) ||
		(uint32_t)output >= (uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT)
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.form = SG_RUNE_COMPACT_ANALYTIC_BALLISTIC;
	candidate.output = output;
	candidate.input_count = 1U;
	candidate.dimensions[0] = dimensions[0];
	candidate.initial = Scalar(initial);
	candidate.first_derivative = Scalar(first_derivative);
	candidate.half_second_derivative = Scalar(half_second_derivative);
	return AppendSpec(workspace, &candidate, spec_out);
}

static int AddBallisticProfile(analytic_workspace_t *workspace,
	profile_t *profile, sg_rune_analytic_output_meaning_t output,
	float initial, float first_derivative, float half_second_derivative)
{
	uint32_t spec;

	if (!AppendBallisticSpec(workspace, output, initial, first_derivative,
		half_second_derivative, &spec))
		return 0;
	return ProfileAppend(profile, spec);
}

static int AddCostAndTime(analytic_workspace_t *workspace, profile_t *profile,
	const sg_rune_analytic_input_dimension_t *dimensions, uint32_t input_count,
	const float *cost_slopes, const float *time_slopes, float cost_bias,
	float time_bias)
{
	return AddProfileSpec(workspace, profile, SG_RUNE_ANALYTIC_OUTPUT_COST,
		dimensions, input_count, cost_bias, cost_slopes) &&
		AddProfileSpec(workspace, profile,
			SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS, dimensions,
			input_count, time_bias, time_slopes);
}

static sg_rune_analytic_input_dimension_t VelocityDimension(uint32_t axis)
{
	return (sg_rune_analytic_input_dimension_t)(
		SG_RUNE_ANALYTIC_INPUT_VELOCITY_X + axis);
}

static sg_rune_analytic_input_dimension_t DirectionDimension(uint32_t axis)
{
	return (sg_rune_analytic_input_dimension_t)(
		SG_RUNE_ANALYTIC_INPUT_DIRECTION_X + axis);
}

static sg_rune_analytic_output_meaning_t AccelerationOutput(uint32_t axis)
{
	return (sg_rune_analytic_output_meaning_t)(
		SG_RUNE_ANALYTIC_OUTPUT_ACCELERATION_X + axis);
}

static int AddReachability(analytic_workspace_t *workspace, profile_t *profile,
	float margin)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_DISTANCE
	};
	static const float zero_slope[] = { 0.0f };

	return AddProfileSpec(workspace, profile,
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN, dimensions, 1U, margin,
		zero_slope);
}

/* CTF_HookPullVelocity truncates the Euclidean rope length to an integer
 * before applying its speed ladder.  The analytic contract has no floor
 * primitive, so represent each integer interval explicitly. */
static int BuildHookLadder(float pull_speed, float *lowers, float *uppers,
	float *speeds, sg_rune_analytic_interval_ownership_t *ownership)
{
	uint32_t cursor = 0U;
	uint32_t value;

	if (!PositiveFinite(pull_speed) || lowers == NULL || uppers == NULL ||
		speeds == NULL || ownership == NULL)
		return 0;
	lowers[cursor] = 0.0f;
	uppers[cursor] = 1.0f;
	/* CTF_HookPullVelocity truncates length before the ladder.  A positive
	 * direction is normalized even when the truncated length is zero. */
	speeds[cursor] = 1.0f;
	ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	lowers[cursor] = 1.0f;
	uppers[cursor] = 11.0f;
	speeds[cursor] = 1.0f;
	ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	for (value = 11U; value <= 20U; value++) {
		lowers[cursor] = (float)value;
		uppers[cursor] = (float)(value + 1U);
		speeds[cursor] = (float)value;
		ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	}
	for (value = 21U; value <= 40U; value++) {
		lowers[cursor] = (float)value;
		uppers[cursor] = (float)(value + 1U);
		speeds[cursor] = (float)value * 2.0f;
		ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	}
	for (value = 41U; value <= 80U; value++) {
		lowers[cursor] = (float)value;
		uppers[cursor] = (float)(value + 1U);
		speeds[cursor] = (float)value * 3.0f;
		ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	}
	for (value = 81U; value <= 100U; value++) {
		lowers[cursor] = (float)value;
		uppers[cursor] = (float)(value + 1U);
		speeds[cursor] = (float)value * 4.0f;
		ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	}
	for (value = 101U; value <= 120U; value++) {
		lowers[cursor] = (float)value;
		uppers[cursor] = (float)(value + 1U);
		speeds[cursor] = (float)value * 5.0f;
		ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	}
	lowers[cursor] = 121.0f;
	uppers[cursor] = FLT_MAX;
	speeds[cursor] = pull_speed;
	ownership[cursor++] = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
	return cursor == HOOK_LADDER_CLAUSE_COUNT;
}

static int AddHookLadderScalar(analytic_workspace_t *workspace,
	profile_t *profile, sg_rune_analytic_output_meaning_t output,
	float fire_speed, float pull_speed, float frame_seconds, int cost)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_DISTANCE,
		SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH
	};
	uint32_t children[HOOK_LADDER_CLAUSE_COUNT];
	float lowers[HOOK_LADDER_CLAUSE_COUNT];
	float uppers[HOOK_LADDER_CLAUSE_COUNT];
	float speeds[HOOK_LADDER_CLAUSE_COUNT];
	sg_rune_analytic_interval_ownership_t ownership[
		HOOK_LADDER_CLAUSE_COUNT];
	float slopes[2];
	uint32_t clause;
	uint32_t parent;
	int result;

	if (!PositiveFinite(fire_speed) || !PositiveFinite(pull_speed) ||
		!ScalarValid(frame_seconds) || !BuildHookLadder(pull_speed, lowers,
			uppers, speeds, ownership))
		return 0;
	for (clause = 0U; clause < HOOK_LADDER_CLAUSE_COUNT; clause++) {
		slopes[0] = 1.0f / fire_speed;
		slopes[1] = speeds[clause] > 0.0f ? 1.0f / speeds[clause] : 0.0f;
		if (!AppendAffineSpec(workspace, output, dimensions, 2U,
				cost ? 0.0f : frame_seconds, slopes,
				&children[clause])) {
			return 0;
		}
	}
	result = AppendPiecewiseSpec(workspace, output, dimensions, 2U, 1U,
			children[0U], lowers, uppers, children, ownership,
			HOOK_LADDER_CLAUSE_COUNT, &parent);
	if (!result)
		return 0;
	return ProfileAppend(profile, parent);
}

static int AddHookLadderPosition(analytic_workspace_t *workspace,
	profile_t *profile, uint32_t axis, float pull_speed)
{
	sg_rune_analytic_input_dimension_t dimensions[5];
	float coefficients[56];
	uint32_t children[HOOK_LADDER_CLAUSE_COUNT];
	float lowers[HOOK_LADDER_CLAUSE_COUNT];
	float uppers[HOOK_LADDER_CLAUSE_COUNT];
	float speeds[HOOK_LADDER_CLAUSE_COUNT];
	sg_rune_analytic_interval_ownership_t ownership[
		HOOK_LADDER_CLAUSE_COUNT];
	uint32_t clause;
	uint32_t default_spec;
	uint32_t parent;

	if (!PositiveFinite(pull_speed) || !BuildHookLadder(pull_speed, lowers,
		uppers, speeds, ownership))
		return 0;

	dimensions[0] = (sg_rune_analytic_input_dimension_t)(
		SG_RUNE_ANALYTIC_INPUT_WORLD_X + axis);
	dimensions[1] = VelocityDimension(axis);
	dimensions[2] = DirectionDimension(axis);
	dimensions[3] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	dimensions[4] = SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH;
	memset(coefficients, 0, sizeof(coefficients));
	{
		uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS] = { 0U };

		exponents[0] = 1U;
		if (!SetPolynomialTerm(coefficients, 5U, 3U, exponents, 1.0f))
			return 0;
	}
	if (!AppendPolynomialSpec(workspace,
		(sg_rune_analytic_output_meaning_t)(
			SG_RUNE_ANALYTIC_OUTPUT_POSITION_X + axis), dimensions, 5U,
		3U, coefficients, 56U, &default_spec))
		return 0;
	for (clause = 0U; clause < HOOK_LADDER_CLAUSE_COUNT; clause++) {
		uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS] = { 0U };
		const float speed = speeds[clause];

		memset(coefficients, 0, sizeof(coefficients));
		exponents[0] = 1U;
		if (!SetPolynomialTerm(coefficients, 5U, 3U, exponents, 1.0f))
			return 0;
		memset(exponents, 0, sizeof(exponents));
		exponents[2] = 1U;
		exponents[3] = 1U;
		if (speed != 0.0f &&
			!SetPolynomialTerm(coefficients, 5U, 3U, exponents,
				speed))
			return 0;
		if (!AppendPolynomialSpec(workspace,
			(sg_rune_analytic_output_meaning_t)(
				SG_RUNE_ANALYTIC_OUTPUT_POSITION_X + axis), dimensions, 5U,
			3U, coefficients, 56U, &children[clause]))
			return 0;
	}
	if (!AppendPiecewiseSpec(workspace,
		(sg_rune_analytic_output_meaning_t)(
			SG_RUNE_ANALYTIC_OUTPUT_POSITION_X + axis), dimensions, 5U, 4U,
		default_spec, lowers, uppers, children, ownership,
			HOOK_LADDER_CLAUSE_COUNT, &parent))
		return 0;
	return ProfileAppend(profile, parent);
}

static int AddHookLadderVelocity(analytic_workspace_t *workspace,
	profile_t *profile, uint32_t axis, float pull_speed)
{
	sg_rune_analytic_input_dimension_t dimensions[3];
	float coefficients[10];
	uint32_t children[HOOK_LADDER_CLAUSE_COUNT];
	float lowers[HOOK_LADDER_CLAUSE_COUNT];
	float uppers[HOOK_LADDER_CLAUSE_COUNT];
	float speeds[HOOK_LADDER_CLAUSE_COUNT];
	sg_rune_analytic_interval_ownership_t ownership[
		HOOK_LADDER_CLAUSE_COUNT];
	uint32_t clause;
	uint32_t default_spec;
	uint32_t parent;

	if (!PositiveFinite(pull_speed) || !BuildHookLadder(pull_speed, lowers,
		uppers, speeds, ownership))
		return 0;

	dimensions[0] = VelocityDimension(axis);
	dimensions[1] = DirectionDimension(axis);
	dimensions[2] = SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH;
	memset(coefficients, 0, sizeof(coefficients));
	{
		uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS] = { 0U };

		exponents[0] = 1U;
		if (!SetPolynomialTerm(coefficients, 3U, 2U, exponents, 1.0f))
			return 0;
	}
	if (!AppendPolynomialSpec(workspace,
		(sg_rune_analytic_output_meaning_t)(
			SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis), dimensions, 3U, 2U,
		coefficients, 10U, &default_spec))
		return 0;
	for (clause = 0U; clause < HOOK_LADDER_CLAUSE_COUNT; clause++) {
		uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS] = { 0U };
		const float speed = speeds[clause];

		memset(coefficients, 0, sizeof(coefficients));
		exponents[1] = 1U;
		if (speed != 0.0f &&
			!SetPolynomialTerm(coefficients, 3U, 2U, exponents,
				speed))
			return 0;
		if (!AppendPolynomialSpec(workspace,
			(sg_rune_analytic_output_meaning_t)(
				SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis), dimensions, 3U, 2U,
			coefficients, 10U, &children[clause]))
			return 0;
	}
	if (!AppendPiecewiseSpec(workspace,
		(sg_rune_analytic_output_meaning_t)(
			SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis), dimensions, 3U, 2U,
		default_spec, lowers, uppers, children, ownership,
		HOOK_LADDER_CLAUSE_COUNT, &parent))
		return 0;
	return ProfileAppend(profile, parent);
}

static int AddHookAcceleration(analytic_workspace_t *workspace, profile_t *profile,
	const sg_rune_physics_parameters_t *physics, float pull_speed)
{
	static const float zero_slopes[] = { 0.0f, 0.0f, 0.0f };
	sg_rune_analytic_input_dimension_t dimensions[3];
	uint32_t axis;

	if (workspace == NULL || profile == NULL || physics == NULL ||
		!NonnegativeFinite(physics->hook_acceleration) ||
		!PositiveFinite(physics->gravity) || !PositiveFinite(pull_speed))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		uint32_t spec;

		dimensions[0] = VelocityDimension(axis);
		dimensions[1] = DirectionDimension(axis);
		dimensions[2] = SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH;
		/* CTF_HookPullStep replaces the player velocity with the normalized
		 * rope-ladder vector.  It does not add hook acceleration to incoming
		 * velocity, so the exact attached acceleration field is zero. */
		if (!AppendAffineSpec(workspace, AccelerationOutput(axis), dimensions, 3U,
			0.0f, zero_slopes, &spec))
			return 0;
		if (!ProfileAppend(profile, spec))
			return 0;
	}
	return 1;
}

static int AddHookMotion(analytic_workspace_t *workspace, profile_t *profile,
	const sg_rune_physics_parameters_t *physics, float pull_speed)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!AddHookLadderPosition(workspace, profile, axis,
				pull_speed) || !AddHookLadderVelocity(workspace,
				profile, axis, pull_speed))
			return 0;
	return AddHookAcceleration(workspace, profile, physics, pull_speed);
}

static int BuildHookProfile(analytic_workspace_t *workspace, profile_t *profile,
	const sg_host_law_view_t *host, float margin)
{
	const sg_rune_physics_parameters_t *physics = &host->static_identity.physics;
	const float frame_seconds = (float)physics->frame_ms / 1000.0f;

	return AddHookLadderScalar(workspace, profile,
		SG_RUNE_ANALYTIC_OUTPUT_COST, (float)host->hook.fire_speed,
		(float)host->hook.pull_speed, frame_seconds, 1) &&
		AddHookLadderScalar(workspace, profile,
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS,
		(float)host->hook.fire_speed, (float)host->hook.pull_speed,
		frame_seconds, 0) && AddHookMotion(workspace, profile, physics,
			(float)host->hook.pull_speed) &&
		AddReachability(workspace, profile, margin);
}

static int AddHookFlightMotion(analytic_workspace_t *workspace,
	profile_t *profile, const sg_rune_physics_parameters_t *physics,
	float fire_speed)
{
	uint32_t axis;

	if (workspace == NULL || profile == NULL || physics == NULL ||
		!PositiveFinite(fire_speed) || !PositiveFinite(physics->gravity))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		sg_rune_analytic_input_dimension_t position_dimensions[3];
		sg_rune_analytic_input_dimension_t velocity_dimensions[2];
		float position_coefficients[10] = { 0.0f };
		float velocity_slopes[2];
		uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS] = { 0U };
		uint32_t spec;

		position_dimensions[0] = (sg_rune_analytic_input_dimension_t)(
			SG_RUNE_ANALYTIC_INPUT_WORLD_X + axis);
		position_dimensions[1] = (sg_rune_analytic_input_dimension_t)(
			SG_RUNE_ANALYTIC_INPUT_DIRECTION_X + axis);
		position_dimensions[2] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
		velocity_dimensions[0] = position_dimensions[1];
		velocity_dimensions[1] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
		exponents[0] = 1U;
		if (!SetPolynomialTerm(position_coefficients, 3U, 2U, exponents,
				1.0f))
			return 0;
		memset(exponents, 0, sizeof(exponents));
		exponents[1] = 1U;
		exponents[2] = 1U;
		if (!SetPolynomialTerm(position_coefficients, 3U, 2U, exponents,
			fire_speed))
			return 0;
		if (!AppendPolynomialSpec(workspace,
			(sg_rune_analytic_output_meaning_t)(
				SG_RUNE_ANALYTIC_OUTPUT_POSITION_X + axis), position_dimensions,
			3U, 2U, position_coefficients, 10U, &spec) ||
			!ProfileAppend(profile, spec))
			return 0;
		velocity_slopes[0] = fire_speed;
		velocity_slopes[1] = 0.0f;
		if (!AddProfileSpec(workspace, profile,
			(sg_rune_analytic_output_meaning_t)(
				SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis), velocity_dimensions,
			2U, 0.0f, velocity_slopes))
			return 0;
	}
	return 1;
}

static int BuildHookFlightProfile(analytic_workspace_t *workspace,
	profile_t *profile, const sg_host_law_view_t *host)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_DISTANCE
	};
	const sg_rune_physics_parameters_t *physics = &host->static_identity.physics;
	const float fire_speed = (float)host->hook.fire_speed;
	const float frame_seconds = (float)physics->frame_ms / 1000.0f;
	const float slope = 1.0f / fire_speed;

	return AddCostAndTime(workspace, profile, dimensions, 1U, &slope, &slope,
		0.0f, frame_seconds) && AddHookFlightMotion(workspace, profile,
		physics, fire_speed) && AddReachability(workspace, profile, 1.0f);
}

static int BuildHookCoastProfile(analytic_workspace_t *workspace,
	profile_t *profile, const sg_host_law_view_t *host, int grounded)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_DISTANCE
	};
	const float zero_slope = 0.0f;
	const float frame_seconds = host == NULL ? 0.0f :
		(float)host->static_identity.physics.frame_ms / 1000.0f;
	uint32_t axis;

	if (host == NULL || !PositiveFinite(frame_seconds) ||
		!AddCostAndTime(workspace, profile, dimensions, 1U, &zero_slope,
			&zero_slope, frame_seconds, frame_seconds))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		const sg_rune_analytic_input_dimension_t dimension =
			VelocityDimension(axis);
		const float slope = grounded && axis == 2U ? 0.0f : 1.0f;

		if (!AddProfileSpec(workspace, profile,
			(sg_rune_analytic_output_meaning_t)(
				SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis), &dimension, 1U,
			0.0f, &slope))
			return 0;
	}
	return AddReachability(workspace, profile, 1.0f);
}

static int TransitionBitsToVector(const uint32_t bits[3], float value[3])
{
	uint32_t axis;

	if (bits == NULL || value == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		value[axis] = CompactScalar(bits[axis]);
		if (!ScalarValid(value[axis]))
			return 0;
	}
	return 1;
}

static int BuildTransitionProfile(analytic_workspace_t *workspace,
	profile_t *profile,
	const sg_rune_compact_mechanism_t *mechanism,
	const sg_rune_compact_static_transition_t *transition)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_DISTANCE
	};
	float zero_slopes[] = { 0.0f };
	float entry[3];
	float exit[3];
	float launch[3];
	float gravity;
	float total_seconds;
	uint64_t travel_ms;
	uint64_t total_ms;
	int emit_endpoint_motion = 1;
	uint32_t axis;

	if (workspace == NULL || profile == NULL ||
		mechanism == NULL || transition == NULL)
		return 0;
	memset(entry, 0, sizeof(entry));
	memset(exit, 0, sizeof(exit));
	memset(launch, 0, sizeof(launch));
	if (transition->kind ==
		SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE) {
		emit_endpoint_motion = 0;
		gravity = 0.0f;
	} else if (transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH) {
		const sg_rune_compact_static_push_t *push = &transition->value.push;

		if (!TransitionBitsToVector(push->launch_velocity_bits, launch))
			return 0;
		for (axis = 0U; axis < 3U; axis++) {
			entry[axis] = (float)push->entry_witness.value[axis] * 0.125f;
			exit[axis] = (float)push->exit_witness.value[axis] * 0.125f;
		}
		gravity = CompactScalar(push->gravity_bits);
	} else if (transition->kind ==
		SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT) {
		const sg_rune_compact_static_teleport_t *teleport =
			&transition->value.teleport;

		if (!TransitionBitsToVector(teleport->arrival_velocity_bits, launch))
			return 0;
		for (axis = 0U; axis < 3U; axis++) {
			entry[axis] = (float)teleport->entry_witness.value[axis] * 0.125f;
			exit[axis] = (float)teleport->exit_witness.value[axis] * 0.125f;
		}
		gravity = 0.0f;
	} else {
		const sg_rune_compact_static_transport_t *transport =
			&transition->value.transport;

		for (axis = 0U; axis < 3U; axis++) {
			entry[axis] = CompactScalar(
				transport->source_player_world_bits[axis]);
			exit[axis] = CompactScalar(
				transport->destination_player_world_bits[axis]);
			launch[axis] = 0.0f;
		}
		gravity = 0.0f;
	}
	/* The transition's elapsed time is the builder-authenticated host schedule
	 * or flight duration.  All authenticated wait phases contribute to the
	 * directional cost as well as elapsed travel time. */
	travel_ms = transition->elapsed_ms;
	if (transition->kind ==
		SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE) {
		const sg_rune_compact_static_portal_state_t *state =
			&transition->value.portal_state;

		if (!AddU64(travel_ms, state->delay_ms, &total_ms) ||
			!AddU64(total_ms, state->dwell_ms, &total_ms) ||
			!AddU64(total_ms, state->pause_ms, &total_ms) ||
			!AddU64(total_ms, state->recovery_ms, &total_ms))
			return 0;
	} else if (!AddU64(travel_ms, mechanism->delay_ms, &total_ms) ||
		!AddU64(total_ms, mechanism->dwell_ms, &total_ms) ||
		!AddU64(total_ms, mechanism->wait_ms, &total_ms) ||
		!AddU64(total_ms, mechanism->reset_ms, &total_ms)) {
		return 0;
	}
	total_seconds = (float)total_ms / 1000.0f;
	if (!FiniteVector(entry) || !FiniteVector(exit) ||
		!NonnegativeFinite(gravity) || !NonnegativeFinite(total_seconds))
		return 0;
	if (!AddCostAndTime(workspace, profile, dimensions, 1U, zero_slopes,
		zero_slopes, total_seconds, total_seconds))
		return 0;
	if (!emit_endpoint_motion)
		return AddReachability(workspace, profile, 1.0f);
	if (transition->kind == SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH) {
		for (axis = 0U; axis < 3U; axis++)
			if (!AddBallisticProfile(workspace, profile,
				(sg_rune_analytic_output_meaning_t)(
					SG_RUNE_ANALYTIC_OUTPUT_POSITION_X + axis), entry[axis],
				launch[axis], axis == 2U ? -0.5f * gravity : 0.0f) ||
				!AddBallisticProfile(workspace, profile,
				(sg_rune_analytic_output_meaning_t)(
					SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis), launch[axis],
				axis == 2U ? -gravity : 0.0f, 0.0f))
				return 0;
	} else {
		for (axis = 0U; axis < 3U; axis++)
			if (!AddBallisticProfile(workspace, profile,
				(sg_rune_analytic_output_meaning_t)(
					SG_RUNE_ANALYTIC_OUTPUT_POSITION_X + axis), exit[axis],
				0.0f, 0.0f) ||
				!AddBallisticProfile(workspace, profile,
				(sg_rune_analytic_output_meaning_t)(
					SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X + axis),
				transition->kind ==
					SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT ?
					launch[axis] : 0.0f,
				0.0f, 0.0f))
				return 0;
	}
	return AddReachability(workspace, profile, 1.0f);
}

static int BuildProfiles(const sg_rune_compact_movement_fields_input_t *input,
	const index_workspace_t *index, profile_t *profiles,
	uint32_t profile_capacity, uint32_t *profile_count,
	analytic_workspace_t *workspace)
{
	static const sg_rune_analytic_input_dimension_t dimensions[] = {
		SG_RUNE_ANALYTIC_INPUT_DISTANCE
	};
	float zero_slopes[] = { 0.0f };
	const sg_host_law_view_t *host = input->host_law;
	const sg_rune_physics_parameters_t *physics =
		&host->static_identity.physics;
	const float frame_seconds =
		(float)physics->frame_ms / 1000.0f;
	uint32_t next_profile = PROFILE_BASE_COUNT;
	uint32_t cell;

	if (profiles == NULL || profile_count == NULL ||
		profile_capacity < PROFILE_BASE_COUNT)
		return 0;
	memset(profiles, 0, (size_t)profile_capacity * sizeof(*profiles));
	*profile_count = PROFILE_BASE_COUNT;

	if (!PositiveFinite(frame_seconds) ||
		!AddCostAndTime(workspace, &profiles[PROFILE_HOOK_BLOCKED], dimensions,
		1U, zero_slopes, zero_slopes, 0.0f, 0.0f) ||
		!AddReachability(workspace, &profiles[PROFILE_HOOK_BLOCKED], -1.0f) ||
		!AddCostAndTime(workspace, &profiles[PROFILE_ANGULAR], dimensions, 1U,
			zero_slopes, zero_slopes, frame_seconds, frame_seconds) ||
		!AddReachability(workspace, &profiles[PROFILE_ANGULAR], 1.0f)) {
		return 0;
	}
	if (!BuildHookFlightProfile(workspace,
		&profiles[PROFILE_HOOK_FLIGHT], host) || !BuildHookCoastProfile(workspace,
		&profiles[PROFILE_HOOK_COAST], host, 0) || !BuildHookCoastProfile(workspace,
		&profiles[PROFILE_HOOK_COAST_GROUNDED], host, 1)) {
		return 0;
	}
	if (!BuildHookProfile(workspace, &profiles[PROFILE_HOOK_VISIBLE], host, 1.0f))
		return 0;
	if (!BuildHookProfile(workspace, &profiles[PROFILE_HOOK_CONDITIONAL], host,
			-1.0f))
		return 0;
	for (cell = 0U; cell < input->static_data->transition_count; cell++) {
		const sg_rune_compact_static_transition_t *transition =
			&input->static_data->transitions[cell];
		const sg_rune_compact_mechanism_t *mechanism =
			&input->static_data->mechanisms[transition->mechanism.value];

		if (next_profile >= profile_capacity ||
			!BuildTransitionProfile(workspace, &profiles[next_profile], mechanism,
				transition))
			return 0;
		index->transition_profiles[cell] = next_profile++;
	}
	*profile_count = next_profile;
	return 1;
}


static int BuildAngularSchedules(
	const sg_rune_compact_movement_bound_input_t *input,
	sg_rune_compact_movement_fields_t *fields,
	sg_rune_compact_movement_fields_error_t *error)
{
	sg_rune_compact_builder_owner_view_t builder_view;
	uint32_t count = 0U;
	uint32_t mechanism;

	if (!SG_RuneCompactBuilderOwnerRead(input->builder, &builder_view) ||
		builder_view.entity_semantics == NULL)
		goto invalid;
	for (mechanism = 0U; mechanism < input->static_data->mechanism_count;
		mechanism++) {
		const sg_rune_compact_mechanism_t *static_mechanism =
			&input->static_data->mechanisms[mechanism];
		const sg_bsp_entity_angular_mover_t *mover =
			SG_BspEntitySemanticsAngularMover(builder_view.entity_semantics,
				static_mechanism->source.entity_ordinal);

		if (mover != NULL && mover->kind ==
			SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR &&
			!AddU32(count, 1U, &count))
			goto invalid;
	}
	fields->angular_schedules = AllocateArray(count,
		sizeof(*fields->angular_schedules));
	if (count != 0U && fields->angular_schedules == NULL) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, 0U, 0U);
		return 0;
	}
	for (mechanism = 0U; mechanism < input->static_data->mechanism_count;
		mechanism++) {
		const sg_rune_compact_mechanism_t *static_mechanism =
			&input->static_data->mechanisms[mechanism];
		const sg_bsp_entity_angular_mover_t *mover =
			SG_BspEntitySemanticsAngularMover(builder_view.entity_semantics,
				static_mechanism->source.entity_ordinal);
		const sg_bsp_entity_semantic_t *entity;
		const sg_bsp_entity_continuous_angular_schedule_t *schedule;
		sg_rune_compact_movement_angular_schedule_t *record;
		uint32_t axis;

		if (mover == NULL || mover->kind !=
			SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR)
			continue;
		entity = FindSemanticEntity(builder_view.entity_semantics,
			static_mechanism->source.entity_ordinal);
		if (static_mechanism->kind != SG_RUNE_COMPACT_MECHANISM_ROTATOR ||
			entity == NULL || entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
			entity->bsp_model >=
				input->geometry->identity.source_counts.model_count)
			goto invalid_record;
		schedule = &mover->schedule.continuous_rotator;
		if (!PositiveFinite(schedule->speed) || schedule->frame_ms == 0U ||
			schedule->frame_ms != input->host_law->mechanism.frame_ms)
			goto invalid_record;
		record = &fields->angular_schedules[fields->angular_schedule_count++];
		memset(record, 0, sizeof(*record));
		record->static_mechanism.value = mechanism;
		if (!SG_RuneCompactStaticMaterializerStaticMechanismAuthorityIndex(
				input->static_owner, mechanism,
				&record->authority_mechanism.value) ||
			record->authority_mechanism.value >=
				input->mechanisms->mechanism_count)
			goto invalid_record;
		record->source_entity = static_mechanism->source.entity_ordinal;
		record->mover_model = entity->bsp_model;
		record->flags = mover->flags;
		for (axis = 0U; axis < 3U; axis++) {
			const float values[] = {
				schedule->initial_angles.value[axis],
				schedule->axis.value[axis],
				schedule->angular_velocity.value[axis],
				schedule->frame_angular_delta.value[axis]
			};

			if (!ScalarValid(values[0]) || !ScalarValid(values[1]) ||
				!ScalarValid(values[2]) || !ScalarValid(values[3]))
				goto invalid_record;
			record->initial_angles_bits[axis] = FloatBits(values[0]);
			record->axis_bits[axis] = FloatBits(values[1]);
			record->angular_velocity_bits[axis] = FloatBits(values[2]);
			record->frame_angular_delta_bits[axis] = FloatBits(values[3]);
		}
		record->speed_bits = FloatBits(schedule->speed);
		record->frame_ms = schedule->frame_ms;
	}
	return fields->angular_schedule_count == count;

invalid_record:
	SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW,
		mechanism, 1U, 0U);
	return 0;
invalid:
	SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW,
		0U, 1U, 0U);
	return 0;
}

static uint32_t HookProfileForClass(hook_visibility_class_t visibility_class)
{
	switch (visibility_class) {
	case HOOK_VISIBILITY_VISIBLE:
		return PROFILE_HOOK_VISIBLE;
	case HOOK_VISIBILITY_CONDITIONAL:
		return PROFILE_HOOK_CONDITIONAL;
	case HOOK_VISIBILITY_BLOCKED:
		return PROFILE_HOOK_BLOCKED;
	}
	return PROFILE_HOOK_BLOCKED;
}

static int MaterializeAnalytic(analytic_workspace_t *workspace,
	sg_rune_compact_analytic_t *analytic)
{
	uint32_t index;
	uint32_t input_cursor = 0U;
	uint32_t next_count;
	uint32_t next_function_count;

	if (workspace == NULL || analytic == NULL || workspace->spec_count == 0U ||
		!WorkspaceGrow(workspace, (void **)&workspace->spec_order,
			&workspace->spec_order_capacity, workspace->spec_count,
			sizeof(*workspace->spec_order)) ||
		!WorkspaceGrow(workspace, (void **)&workspace->spec_to_function,
			&workspace->spec_to_function_capacity, workspace->spec_count,
			sizeof(*workspace->spec_to_function))) {
		return 0;
	}
	for (index = 0U; index < workspace->spec_count; index++)
		workspace->spec_order[index] = index;
	for (index = 1U; index < workspace->spec_count; index++) {
		const uint32_t candidate = workspace->spec_order[index];
		uint32_t cursor = index;

		while (cursor != 0U && CompareSpecs(
			&workspace->specs[candidate],
			&workspace->specs[workspace->spec_order[cursor - 1U]]) < 0) {
			workspace->spec_order[cursor] = workspace->spec_order[cursor - 1U];
			cursor--;
		}
		workspace->spec_order[cursor] = candidate;
	}
	workspace->function_count = 0U;
	workspace->input_dimension_count = 0U;
	workspace->affine_count = 0U;
	workspace->affine_slope_count = 0U;
	workspace->polynomial_count = 0U;
	workspace->polynomial_coefficient_count = 0U;
	workspace->ballistic_count = 0U;
	workspace->piecewise_count = 0U;
	workspace->piecewise_clause_count = 0U;
	for (index = 0U; index < workspace->spec_count; index++)
		workspace->spec_to_function[index] = SG_RUNE_COMPACT_INDEX_NONE;
	for (index = 0U; index < workspace->spec_count; index++) {
		const uint32_t spec_index = workspace->spec_order[index];
		const function_spec_t *spec = &workspace->specs[spec_index];
		sg_rune_analytic_function_t *function;
		uint32_t value_index;

		if (!AddU32(input_cursor, spec->input_count, &input_cursor) ||
			!AddU32(workspace->function_count, 1U, &next_function_count) ||
			!WorkspaceGrow(workspace, (void **)&workspace->functions,
				&workspace->function_capacity, next_function_count,
				sizeof(*workspace->functions)) ||
			!WorkspaceGrow(workspace, (void **)&workspace->input_dimensions,
				&workspace->input_dimension_capacity, input_cursor,
				sizeof(*workspace->input_dimensions))) {
			return 0;
		}
		function = &workspace->functions[workspace->function_count];
		memset(function, 0, sizeof(*function));
		function->inputs.first = input_cursor - spec->input_count;
		function->inputs.count = spec->input_count;
		function->output = spec->output;
		function->form = spec->form;
		for (value_index = 0U; value_index < spec->input_count; value_index++)
			workspace->input_dimensions[function->inputs.first + value_index] =
				spec->dimensions[value_index];
		switch (spec->form) {
		case SG_RUNE_COMPACT_ANALYTIC_AFFINE:
		{
			uint32_t previous_slopes = workspace->affine_slope_count;

			if (!AddU32(workspace->affine_count, 1U, &next_count) ||
				!AddU32(workspace->affine_slope_count, spec->input_count,
					&workspace->affine_slope_count) ||
				!WorkspaceGrow(workspace, (void **)&workspace->affines,
					&workspace->affine_capacity, next_count,
					sizeof(*workspace->affines)) ||
				!WorkspaceGrow(workspace, (void **)&workspace->affine_slopes,
					&workspace->affine_slope_capacity,
					workspace->affine_slope_count,
					sizeof(*workspace->affine_slopes))) {
				return 0;
			}
			function->definition = workspace->affine_count;
			workspace->affines[workspace->affine_count].bias = spec->bias;
			workspace->affines[workspace->affine_count].slopes.first =
				previous_slopes;
			workspace->affines[workspace->affine_count].slopes.count =
				spec->input_count;
			for (value_index = 0U; value_index < spec->input_count; value_index++)
				workspace->affine_slopes[previous_slopes + value_index] =
					spec->slopes[value_index];
			workspace->affine_count++;
			break;
		}
		case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL:
		{
			uint32_t previous_coefficients =
				workspace->polynomial_coefficient_count;

			if (!AddU32(workspace->polynomial_count, 1U, &next_count) ||
				!AddU32(workspace->polynomial_coefficient_count,
					spec->value_count,
					&workspace->polynomial_coefficient_count) ||
				!WorkspaceGrow(workspace, (void **)&workspace->polynomials,
					&workspace->polynomial_capacity, next_count,
					sizeof(*workspace->polynomials)) ||
				!WorkspaceGrow(workspace, (void **)&workspace->polynomial_coefficients,
					&workspace->polynomial_coefficient_capacity,
					workspace->polynomial_coefficient_count,
					sizeof(*workspace->polynomial_coefficients))) {
				return 0;
			}
			function->definition = workspace->polynomial_count;
			workspace->polynomials[workspace->polynomial_count].degree =
				spec->degree;
			workspace->polynomials[workspace->polynomial_count].coefficients.first =
				previous_coefficients;
			workspace->polynomials[workspace->polynomial_count].coefficients.count =
				spec->value_count;
			for (value_index = 0U; value_index < spec->value_count; value_index++)
				workspace->polynomial_coefficients[previous_coefficients +
					value_index] = spec->coefficients[value_index];
			workspace->polynomial_count++;
			break;
		}
		case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
				if (!AddU32(workspace->ballistic_count, 1U, &next_count) ||
					!WorkspaceGrow(workspace, (void **)&workspace->ballistics,
						&workspace->ballistic_capacity, next_count,
						sizeof(*workspace->ballistics))) {
					return 0;
				}
			function->definition = workspace->ballistic_count;
			workspace->ballistics[workspace->ballistic_count].initial =
				spec->initial;
			workspace->ballistics[workspace->ballistic_count].first_derivative =
				spec->first_derivative;
			workspace->ballistics[workspace->ballistic_count].half_second_derivative =
				spec->half_second_derivative;
			workspace->ballistic_count++;
			break;
		case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE: {
			uint32_t clause;
			uint32_t previous_clauses = workspace->piecewise_clause_count;

			if (!AddU32(workspace->piecewise_count, 1U, &next_count) ||
				!AddU32(workspace->piecewise_clause_count,
					spec->piecewise_clause_count,
					&workspace->piecewise_clause_count) ||
				!WorkspaceGrow(workspace, (void **)&workspace->piecewise,
					&workspace->piecewise_capacity, next_count,
					sizeof(*workspace->piecewise)) ||
				!WorkspaceGrow(workspace, (void **)&workspace->piecewise_clauses,
					&workspace->piecewise_clause_capacity,
					workspace->piecewise_clause_count,
					sizeof(*workspace->piecewise_clauses)) ||
				spec->piecewise_default_spec >= workspace->spec_count ||
					workspace->spec_to_function[spec->piecewise_default_spec] ==
						SG_RUNE_COMPACT_INDEX_NONE) {
					return 0;
				}
			function->definition = workspace->piecewise_count;
			workspace->piecewise[workspace->piecewise_count].selector_input =
				spec->piecewise_selector_input;
			workspace->piecewise[workspace->piecewise_count].default_function.value =
				workspace->spec_to_function[spec->piecewise_default_spec];
			workspace->piecewise[workspace->piecewise_count].clauses.first =
				previous_clauses;
			workspace->piecewise[workspace->piecewise_count].clauses.count =
				spec->piecewise_clause_count;
			for (clause = 0U; clause < spec->piecewise_clause_count; clause++) {
				const piecewise_clause_spec_t *source =
					&spec->piecewise_clauses[clause];
				sg_rune_analytic_piecewise_clause_t *destination =
					&workspace->piecewise_clauses[
						previous_clauses + clause];

					if (source->function_spec >= workspace->spec_count ||
						workspace->spec_to_function[source->function_spec] ==
							SG_RUNE_COMPACT_INDEX_NONE) {
						return 0;
					}
				destination->lower = source->lower;
				destination->upper = source->upper;
				destination->function.value =
					workspace->spec_to_function[source->function_spec];
				destination->ownership = source->ownership;
			}
			workspace->piecewise_count++;
			break;
		}
		case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
		case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
			return 0;
		}
		workspace->spec_to_function[spec_index] = workspace->function_count;
		workspace->function_count = next_function_count;
	}
	memset(analytic, 0, sizeof(*analytic));
	analytic->version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	analytic->functions = workspace->functions;
	analytic->function_count = workspace->function_count;
	analytic->input_dimensions = workspace->input_dimensions;
	analytic->input_dimension_count = input_cursor;
	analytic->affines = workspace->affines;
	analytic->affine_count = workspace->affine_count;
	analytic->affine_slopes = workspace->affine_slopes;
	analytic->affine_slope_count = workspace->affine_slope_count;
	analytic->polynomials = workspace->polynomials;
	analytic->polynomial_count = workspace->polynomial_count;
	analytic->polynomial_coefficients = workspace->polynomial_coefficients;
	analytic->polynomial_coefficient_count =
		workspace->polynomial_coefficient_count;
	analytic->ballistics = workspace->ballistics;
	analytic->ballistic_count = workspace->ballistic_count;
	analytic->piecewise = workspace->piecewise;
	analytic->piecewise_count = workspace->piecewise_count;
	analytic->piecewise_clauses = workspace->piecewise_clauses;
	analytic->piecewise_clause_count = workspace->piecewise_clause_count;
	return 1;
}

static void RemapProfiles(profile_t *profiles, uint32_t profile_count,
	const analytic_workspace_t *workspace)
{
	uint32_t profile;
	uint32_t reference;

	for (profile = 0U; profile < profile_count; profile++)
		for (reference = 0U; reference < profiles[profile].function_count;
			reference++)
			profiles[profile].functions[reference] =
				workspace->spec_to_function[
					profiles[profile].functions[reference]];
}


static const sg_configuration_semantic_region_t *RegionForCell(
	const sg_rune_compact_movement_fields_input_t *input,
	const index_workspace_t *index, uint32_t cell)
{
	return &input->configuration_semantics->regions[index->region_by_cell[cell]];
}

static sg_rune_movement_fiber_kind_t CapabilityFiberKind(
	sg_rune_movement_capability_kind_t kind)
{
	if (kind >= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		kind <= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH)
		return SG_RUNE_MOVEMENT_FIBER_HOOK;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE)
		return SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
	return SG_RUNE_MOVEMENT_FIBER_PMOVE;
}

static sg_rune_movement_state_variables_t CapabilityStateVariables(
	sg_rune_movement_capability_kind_t kind)
{
	sg_rune_movement_state_variables_t variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_TIME;

	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_WALK ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_CROUCH ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_RAMP ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_JUMP ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_DROP ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		variables |= SG_RUNE_MOVEMENT_STATE_WATER |
			SG_RUNE_MOVEMENT_STATE_CURRENT;
	if (kind >= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		kind <= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH)
		variables |= SG_RUNE_MOVEMENT_STATE_HOOK;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		variables |= SG_RUNE_MOVEMENT_STATE_MOVER;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE)
		variables |= SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE;
	return variables;
}

static int AddField(emit_state_t *state, uint32_t cell, uint32_t boundary,
	sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t source_stances,
	sg_rune_stance_validity_t destination_stances, uint32_t profile)
{
	const profile_t *selected;
	uint32_t next_attachment;
	uint32_t next_fiber;
	uint32_t next_function;
	uint32_t reference;

	if (profile >= state->profile_count || !StancesValid(source_stances) ||
		!StancesValid(destination_stances) ||
		kind >= SG_RUNE_MOVEMENT_CAPABILITY_KIND_COUNT)
		return 0;
	selected = &state->profiles[profile];
	if (!AddU32(state->attachment_cursor, 1U, &next_attachment) ||
		!AddU32(state->fiber_cursor, 1U, &next_fiber) ||
		!AddU32(state->function_cursor, selected->function_count,
			&next_function) ||
		next_attachment > SG_RUNE_COMPACT_MOVEMENT_MAX_FIELDS ||
		next_fiber > SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS ||
		next_function > SG_RUNE_COMPACT_MOVEMENT_MAX_ANALYTIC_REFERENCES)
		return 0;
	if (state->emit != 0) {
		sg_rune_movement_capability_t *attachment =
			&state->output->capabilities[state->attachment_cursor];
		sg_rune_compact_movement_fiber_t *fiber =
			&state->output->fibers[state->fiber_cursor];

		memset(attachment, 0, sizeof(*attachment));
		memset(fiber, 0, sizeof(*fiber));
		attachment->cell.value = cell;
		attachment->boundary_portal.value = boundary;
		attachment->kind = kind;
		attachment->source_stances = source_stances;
		attachment->destination_stances = destination_stances;
		attachment->fibers.first = state->fiber_cursor;
		attachment->fibers.count = 1U;
		fiber->capability.value = state->attachment_cursor;
		fiber->kind = CapabilityFiberKind(kind);
		fiber->state_variables = CapabilityStateVariables(kind);
		fiber->source_state.value = 0U;
		fiber->destination_state.value = 0U;
		fiber->functions.first = state->function_cursor;
		fiber->functions.count = selected->function_count;
		fiber->mechanism_transition.value = SG_RUNE_COMPACT_INDEX_NONE;
		fiber->angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
		fiber->controller_action_controller.value =
			SG_RUNE_COMPACT_INDEX_NONE;
		fiber->controller_action_target.value = SG_RUNE_COMPACT_INDEX_NONE;
		for (reference = 0U; reference < selected->function_count; reference++)
			state->output->fiber_function_refs[
				state->function_cursor + reference].value =
				selected->functions[reference];
	}
	state->attachment_cursor = next_attachment;
	state->fiber_cursor = next_fiber;
	state->function_cursor = next_function;
	return 1;
}

static int AddFieldIndexed(emit_state_t *state, uint32_t cell,
	uint32_t boundary, sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t source_stances,
	sg_rune_stance_validity_t destination_stances, uint32_t profile,
	uint32_t *field_index_out)
{
	if (state == NULL)
		return 0;
	if (field_index_out != NULL)
		*field_index_out = state->attachment_cursor;
	return AddField(state, cell, boundary, kind, source_stances,
		destination_stances, profile);
}

static int BindMechanismTransition(emit_state_t *state,
	uint32_t transition_index, uint32_t capability_index)
{
	uint32_t fiber_index;

	if (state == NULL || transition_index >=
		state->input->mechanisms->transition_count)
		return 0;
	if (state->emit != 0) {
		if (state->output == NULL || capability_index >=
			state->output->capability_count)
			return 0;
		fiber_index = state->output->capabilities[capability_index].fibers.first;
		if (fiber_index >= state->output->fiber_count)
			return 0;
		state->output->fibers[fiber_index].kind =
			SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
		state->output->fibers[fiber_index].mechanism_transition.value =
			transition_index;
		if (state->input->mechanisms->transitions[transition_index].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
			state->output->fibers[fiber_index].state_variables =
				SG_RUNE_MOVEMENT_STATE_POSITION |
				SG_RUNE_MOVEMENT_STATE_VELOCITY |
				SG_RUNE_MOVEMENT_STATE_STANCE |
				SG_RUNE_MOVEMENT_STATE_TIME |
				SG_RUNE_MOVEMENT_STATE_SUPPORT |
				SG_RUNE_MOVEMENT_STATE_WATER;
	}
	return 1;
}

static int BindControllerAction(emit_state_t *state, uint32_t controller_index,
	uint32_t capability_index)
{
	const sg_rune_compact_mechanism_controller_t *controller;
	uint32_t fiber_index;
	uint32_t transition_index;

	if (state == NULL || controller_index >=
		state->input->mechanisms->controller_count)
		return 0;
	controller = &state->input->mechanisms->controllers[controller_index];
	if (controller->mechanism >= state->input->mechanisms->mechanism_count)
		return 0;
	if (state->emit == 0)
		return 1;
	if (state->output == NULL || capability_index >=
		state->output->capability_count)
		return 0;
	fiber_index = state->output->capabilities[capability_index].fibers.first;
	if (fiber_index >= state->output->fiber_count ||
		state->output->capabilities[capability_index].kind !=
			SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		return 0;
	transition_index =
		state->output->fibers[fiber_index].mechanism_transition.value;
	if (transition_index >= state->input->mechanisms->transition_count ||
		state->input->mechanisms->transitions[transition_index].mechanism !=
			controller->mechanism)
		return 0;
	state->output->fibers[fiber_index].controller_action_controller.value =
		controller_index;
	state->output->fibers[fiber_index].controller_action_target.value =
		controller->mechanism;
	return 1;
}

static int AppendProfileFunctionSpan(emit_state_t *state, uint32_t profile,
	sg_rune_analytic_function_span_t *span_out)
{
	const profile_t *selected;
	uint32_t next_function;
	uint32_t reference;

	if (state == NULL || profile >= state->profile_count)
		return 0;
	selected = &state->profiles[profile];
	if (!AddU32(state->function_cursor, selected->function_count,
		&next_function) || next_function >
		SG_RUNE_COMPACT_MOVEMENT_MAX_ANALYTIC_REFERENCES)
		return 0;
	if (span_out != NULL) {
		span_out->first = state->function_cursor;
		span_out->count = selected->function_count;
	}
	if (state->emit != 0)
		for (reference = 0U; reference < selected->function_count; reference++)
			state->output->fiber_function_refs[
				state->function_cursor + reference].value =
				selected->functions[reference];
	state->function_cursor = next_function;
	return 1;
}

static int StaticHookTargetKind(const emit_state_t *state,
	uint32_t reference_index, sg_host_hook_target_kind_t *kind_out)
{
	const sg_rune_compact_response_partition_view_t *view;
	const sg_rune_compact_response_ref_t *reference;
	uint32_t first_patch;
	uint32_t patch_count;
	uint32_t offset;
	int moving = -1;

	if (state == NULL || state->input == NULL || state->index == NULL ||
		kind_out == NULL || reference_index >= state->index->hook_ref_count)
		return 0;
	view = state->input->response_partition;
	reference = &state->index->hook_refs[reference_index];
	if (view == NULL)
		return 0;
	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) {
		const sg_rune_compact_response_pair_t *pair;

		if (reference->index >= view->response_pair_count)
			return 0;
		pair = &view->response_pairs[reference->index];
		if (pair->target_patch >= view->target_patch_count)
			return 0;
		first_patch = pair->target_patch;
		patch_count = 1U;
	} else if (reference->kind ==
		SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP) {
		const sg_rune_compact_response_candidate_group_t *candidate;
		const sg_rune_compact_response_endpoint_group_t *target_group;

		if (reference->index >= view->candidate_group_count)
			return 0;
		candidate = &view->candidate_groups[reference->index];
		if (candidate->target_group >= view->target_endpoint_group_count)
			return 0;
		target_group = &view->target_endpoint_groups[candidate->target_group];
		first_patch = target_group->first_member;
		patch_count = target_group->member_count;
		if (patch_count == 0U || !SpanWithin(first_patch, patch_count,
			view->target_endpoint_member_count))
			return 0;
	} else {
		return 0;
	}
	for (offset = 0U; offset < patch_count; offset++) {
		uint32_t patch_index = first_patch + offset;
		const sg_rune_compact_response_patch_t *patch;
		int patch_moving;

		if (reference->kind ==
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP)
			patch_index = view->target_endpoint_members[patch_index];
		if (patch_index >= view->target_patch_count)
			return 0;
		patch = &view->target_patches[patch_index];
		patch_moving = patch->model != SG_HOST_COLLISION_MODEL_WORLD;
		if (moving != -1 && moving != patch_moving)
			return 0;
		moving = patch_moving;
	}
	*kind_out = moving != 0 ? SG_HOST_HOOK_TARGET_FUNC :
		SG_HOST_HOOK_TARGET_WORLD;
	return 1;
}

static int InitializeHookTarget(emit_state_t *state,
	sg_rune_compact_movement_hook_target_t *record, uint32_t fiber,
	sg_rune_stance_validity_t stance, uint32_t reference_index,
	hook_visibility_class_t visibility_class, int generic)
{
	static const sg_host_hook_target_kind_t generic_kinds[] = {
		SG_HOST_HOOK_TARGET_PLAYER,
		SG_HOST_HOOK_TARGET_BODYQUE,
		SG_HOST_HOOK_TARGET_FUNC,
		SG_HOST_HOOK_TARGET_INFO_FLAG
	};

	memset(record, 0, sizeof(*record));
	record->fiber.value = fiber;
	record->source_stances = stance;
	record->target_stances = stance;
	if (generic != 0) {
		record->target_kind = generic_kinds[reference_index];
		record->provenance =
			SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC;
		record->response.kind = SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT;
		record->response.index = SG_RUNE_COMPACT_INDEX_NONE;
		record->visibility_class =
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL;
	} else {
		record->response = state->index->hook_refs[reference_index];
		if (!StaticHookTargetKind(state, reference_index,
			&record->target_kind))
			return 0;
		record->provenance =
			SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
		record->visibility_class =
			(sg_rune_movement_hook_target_class_t)visibility_class;
	}
	return 1;
}

static int EmitHookCapability(emit_state_t *state, uint32_t cell,
	uint32_t boundary, sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t stance, uint32_t profile,
	uint32_t reference_first, uint32_t reference_count,
	hook_visibility_class_t visibility_class, int generic)
{
	uint32_t capability;
	uint32_t target;
	uint32_t phase_functions = 0U;
	const uint32_t body_profile = visibility_class == HOOK_VISIBILITY_VISIBLE ?
		PROFILE_HOOK_VISIBLE : visibility_class == HOOK_VISIBILITY_CONDITIONAL ?
			PROFILE_HOOK_CONDITIONAL : PROFILE_HOOK_BLOCKED;
	const uint32_t phase_profiles[6] = {
		PROFILE_HOOK_FLIGHT, body_profile, body_profile, PROFILE_HOOK_COAST,
		PROFILE_HOOK_COAST, PROFILE_HOOK_FLIGHT
	};
	uint32_t phase;

	if (reference_count == 0U || !AddFieldIndexed(state, cell, boundary, kind,
		stance, stance, profile, &capability) ||
		reference_count > UINT32_MAX - state->hook_target_cursor ||
		state->hook_target_cursor + reference_count >
		SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS)
		return 0;
	for (phase = 0U; phase < 6U; phase++)
		if (!AddU32(phase_functions,
			state->profiles[phase_profiles[phase]].function_count,
			&phase_functions))
			return 0;
	if (reference_count != 0U &&
		phase_functions > UINT32_MAX / reference_count)
		return 0;
	if (!AddU32(state->phase_function_count,
		phase_functions * reference_count, &state->phase_function_count))
		return 0;
	if (state->emit != 0) {
		const uint32_t fiber =
			state->output->capabilities[capability].fibers.first;

		state->output->fibers[fiber].hook_targets.first =
			state->hook_target_cursor;
		state->output->fibers[fiber].hook_targets.count = reference_count;
		for (target = 0U; target < reference_count; target++) {
			sg_rune_compact_movement_hook_target_t *record =
				&state->output->hook_targets[
					state->hook_target_cursor + target];

			if (!InitializeHookTarget(state, record, fiber, stance,
				generic != 0 ? target : reference_first + target,
				visibility_class, generic))
				return 0;
		}
	}
	state->hook_target_cursor += reference_count;
	return 1;
}

static int EmitHookReleaseCapability(emit_state_t *state, uint32_t cell,
	uint32_t boundary, sg_rune_stance_validity_t stance,
	uint32_t reference_first, uint32_t reference_count,
	hook_visibility_class_t visibility_class, int generic)
{
	static const uint32_t direct_profiles[4] = {
		PROFILE_HOOK_COAST, PROFILE_HOOK_COAST_GROUNDED,
		PROFILE_HOOK_COAST, PROFILE_HOOK_COAST_GROUNDED
	};
	const uint32_t capability_index = state->attachment_cursor;
	const uint32_t first_fiber = state->fiber_cursor;
	const uint32_t body_profile = generic != 0 ? PROFILE_HOOK_CONDITIONAL :
		HookProfileForClass(visibility_class);
	const uint32_t phase_profiles[6] = {
		PROFILE_HOOK_FLIGHT, body_profile, body_profile, PROFILE_HOOK_COAST,
		PROFILE_HOOK_COAST, PROFILE_HOOK_FLIGHT
	};
	uint32_t next_capability;
	uint32_t next_fiber;
	uint32_t next_function = state->function_cursor;
	uint32_t phase_functions = 0U;
	uint32_t variant;
	uint32_t phase;

	if (reference_count == 0U ||
		!AddU32(state->attachment_cursor, 1U, &next_capability) ||
		!AddU32(state->fiber_cursor, 4U, &next_fiber) ||
		next_capability > SG_RUNE_COMPACT_MOVEMENT_MAX_FIELDS ||
		next_fiber > SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS)
		return 0;
	for (variant = 0U; variant < 4U; variant++)
		if (!AddU32(next_function,
			state->profiles[direct_profiles[variant]].function_count,
			&next_function))
			return 0;
	if (next_function > SG_RUNE_COMPACT_MOVEMENT_MAX_ANALYTIC_REFERENCES)
		return 0;
	for (phase = 0U; phase < 6U; phase++)
		if (!AddU32(phase_functions,
			state->profiles[phase_profiles[phase]].function_count,
			&phase_functions))
			return 0;
	if (reference_count > UINT32_MAX / 2U ||
		state->hook_target_cursor > UINT32_MAX - reference_count * 2U ||
		state->hook_target_cursor + reference_count * 2U >
			SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS ||
		phase_functions > UINT32_MAX / (reference_count * 2U) ||
		!AddU32(state->phase_function_count,
			phase_functions * reference_count * 2U,
			&state->phase_function_count))
		return 0;
	if (state->emit != 0) {
		sg_rune_movement_capability_t *capability =
			&state->output->capabilities[capability_index];

		memset(capability, 0, sizeof(*capability));
		capability->cell.value = cell;
		capability->boundary_portal.value = boundary;
		capability->kind = SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE;
		capability->source_stances = stance;
		capability->destination_stances = stance;
		capability->fibers.first = first_fiber;
		capability->fibers.count = 4U;
	}
	for (variant = 0U; variant < 4U; variant++) {
		const profile_t *selected = &state->profiles[direct_profiles[variant]];
		const uint32_t fiber_index = state->fiber_cursor;
		const uint32_t target_count = variant < 2U ? 0U : reference_count;
		uint32_t reference;

		if (state->emit != 0) {
			sg_rune_compact_movement_fiber_t *fiber =
				&state->output->fibers[fiber_index];

			memset(fiber, 0, sizeof(*fiber));
			fiber->capability.value = capability_index;
			fiber->kind = SG_RUNE_MOVEMENT_FIBER_HOOK;
			fiber->state_variables = CapabilityStateVariables(
				SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE);
			fiber->functions.first = state->function_cursor;
			fiber->functions.count = selected->function_count;
			fiber->hook_targets.first = state->hook_target_cursor;
			fiber->hook_targets.count = target_count;
			fiber->mechanism_transition.value = SG_RUNE_COMPACT_INDEX_NONE;
			fiber->angular_schedule = SG_RUNE_COMPACT_INDEX_NONE;
			fiber->controller_action_controller.value =
				SG_RUNE_COMPACT_INDEX_NONE;
			fiber->controller_action_target.value =
				SG_RUNE_COMPACT_INDEX_NONE;
			state->output->hook_release_grounded[fiber_index] =
				(uint8_t)((variant & 1U) != 0U);
			for (reference = 0U; reference < selected->function_count;
				reference++)
				state->output->fiber_function_refs[
					state->function_cursor + reference].value =
					selected->functions[reference];
			for (reference = 0U; reference < target_count; reference++) {
				sg_rune_compact_movement_hook_target_t *target =
					&state->output->hook_targets[
						state->hook_target_cursor + reference];

				if (!InitializeHookTarget(state, target, fiber_index, stance,
					generic != 0 ? reference : reference_first + reference,
					visibility_class, generic))
					return 0;
			}
		}
		state->function_cursor += selected->function_count;
		state->fiber_cursor++;
		state->hook_target_cursor += target_count;
	}
	state->attachment_cursor = next_capability;
	return state->fiber_cursor == next_fiber &&
		state->function_cursor == next_function;
}

static int EmitHookField(emit_state_t *state, uint32_t cell, uint32_t boundary,
	sg_rune_stance_validity_t stances)
{
	uint32_t kind;

	for (kind = (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;
		kind <= (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH; kind++) {
		uint32_t stance;

		for (stance = 0U; stance < HOOK_STANCE_COUNT; stance++) {
			const sg_rune_stance_validity_t exact_stance = HookStance(stance);
			uint32_t visibility;

			if ((stances & exact_stance) == 0U)
				continue;
			if (kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST) {
				uint32_t capability_index;

				if (!AddFieldIndexed(state, cell, boundary,
						SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST,
						exact_stance, exact_stance, PROFILE_HOOK_COAST,
						&capability_index))
					return 0;
				if (state->emit != 0) {
					const uint32_t fiber = state->output->capabilities[
						capability_index].fibers.first;

					state->output->fibers[fiber].hook_targets.first =
						state->hook_target_cursor;
				}
				continue;
			}
			if (kind != SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST) {
				if (kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) {
					if (!EmitHookReleaseCapability(state, cell, boundary,
							exact_stance, 0U, 4U,
							HOOK_VISIBILITY_CONDITIONAL, 1))
						return 0;
				} else if (!EmitHookCapability(state, cell, boundary,
						(sg_rune_movement_capability_kind_t)kind,
						exact_stance,
						kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT ||
						kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH ?
							PROFILE_HOOK_FLIGHT : PROFILE_HOOK_CONDITIONAL,
						0U, 4U, HOOK_VISIBILITY_CONDITIONAL, 1)) {
					return 0;
				}
			}
			for (visibility = 0U; visibility < HOOK_VISIBILITY_CLASS_COUNT;
				visibility++) {
				const hook_visibility_class_t visibility_class =
					(hook_visibility_class_t)visibility;
				const uint32_t slot = HookRefSlot(cell, stance,
					visibility_class);
				const uint32_t reference_first =
					state->index->hook_ref_offsets[slot];
				const uint32_t reference_count =
					state->index->hook_ref_offsets[slot + 1U] - reference_first;
				const uint32_t target_profile =
					HookProfileForClass(visibility_class);
				const uint32_t profile =
					kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT ||
					kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH ?
						PROFILE_HOOK_FLIGHT :
					kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE ?
						PROFILE_HOOK_COAST :
					kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST ?
						PROFILE_HOOK_COAST : target_profile;

				if (reference_count == 0U)
					continue;
				if (kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) {
					if (!EmitHookReleaseCapability(state, cell, boundary,
							exact_stance, reference_first, reference_count,
							visibility_class, 0))
						return 0;
				} else if (!EmitHookCapability(state, cell, boundary,
					(sg_rune_movement_capability_kind_t)kind, exact_stance,
					profile, reference_first, reference_count,
					visibility_class, 0)) {
					return 0;
				}
			}
		}
	}
	return 1;
}

static int EmitDirectTransitionFields(emit_state_t *state, uint32_t cell,
	movement_family_t family)
{
	uint32_t stance;

	for (stance = 0U; stance < HOOK_STANCE_COUNT; stance++) {
		const sg_rune_stance_validity_t exact = HookStance(stance);
		uint32_t transition_index;

		for (transition_index = 0U;
			transition_index < state->input->mechanisms->transition_count;
			transition_index++) {
			const sg_rune_compact_mechanism_transition_t *transition =
				&state->input->mechanisms->transitions[transition_index];
			const uint32_t static_index =
				state->index->authority_transition_static[transition_index];
			const movement_family_t transition_family =
				transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH ?
				MOVEMENT_FAMILY_EXTERNAL_FORCE : MOVEMENT_FAMILY_MOVER;
			sg_rune_stance_validity_t transition_stances =
				state->input->cells[cell].valid_stances;
			uint32_t field_index;

			if (transition->kind ==
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
				transition_family != family ||
				transition->entry_cell.value != cell)
				continue;
			/* One continuous-rotator frame certifies a particular binary32
			 * source transform and phase.  The field schema has no phase key, so
			 * replaying that single frame as an always-available capability would
			 * overclaim later active phases.  Keep the authenticated authority
			 * fact, but publish no reusable movement fiber for it. */
			if (transition->kind ==
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
				transition->mechanism <
					state->input->mechanisms->mechanism_count &&
				ContinuousRotatorAuthority(state->input,
					&state->input->mechanisms->mechanisms[
						transition->mechanism]))
				continue;
			if (transition->kind ==
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
				transition_stances = (sg_rune_stance_validity_t)(
					transition_stances & (SG_RUNE_STANCE_VALID_STANDING <<
						transition->value.transport.stance));
			if ((transition_stances & exact) == 0U)
				continue;
			if (state->index->transition_profiles[static_index] ==
					SG_STATIC_VISIBILITY_INDEX_NONE ||
				!AddFieldIndexed(state, cell, SG_RUNE_COMPACT_INDEX_NONE,
					family == MOVEMENT_FAMILY_EXTERNAL_FORCE ?
						SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE :
						SG_RUNE_MOVEMENT_CAPABILITY_MOVER,
					exact, exact,
					state->index->transition_profiles[static_index],
					&field_index) ||
				!BindMechanismTransition(state, transition_index, field_index))
				return 0;
		}
	}
	return 1;
}

static int EmitControllerFields(emit_state_t *state, uint32_t cell)
{
	uint32_t controller_index;

	for (controller_index = 0U;
		controller_index < state->input->mechanisms->controller_count;
		controller_index++) {
		const sg_rune_compact_mechanism_controller_t *controller =
			&state->input->mechanisms->controllers[controller_index];
		uint32_t stance;

		if (controller->spatiality !=
				SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL ||
			controller->activation_cell.value != cell)
			continue;
		for (stance = 0U; stance < HOOK_STANCE_COUNT; stance++) {
			const sg_rune_stance_validity_t exact = HookStance(stance);
		uint32_t transition_index;

			if ((state->input->cells[cell].valid_stances & exact) == 0U)
				continue;
		for (transition_index = 0U;
			transition_index < state->input->mechanisms->transition_count;
			transition_index++) {
			const sg_rune_compact_mechanism_transition_t *transition =
				&state->input->mechanisms->transitions[transition_index];
			const uint32_t static_index =
				state->index->authority_transition_static[transition_index];
			uint32_t field_index;

			if (transition->mechanism != controller->mechanism)
				continue;
			if (state->index->transition_profiles[static_index] ==
					SG_STATIC_VISIBILITY_INDEX_NONE ||
				!AddFieldIndexed(state, cell, SG_RUNE_COMPACT_INDEX_NONE,
					SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION, exact, exact,
					state->index->transition_profiles[static_index],
					&field_index) ||
				!BindMechanismTransition(state, transition_index, field_index) ||
				!BindControllerAction(state, controller_index, field_index))
				return 0;
		}
		}
	}
	return 1;
}

static int EmitInteriorFields(emit_state_t *state, uint32_t cell)
{
	const sg_rune_stance_validity_t stances = state->input->cells[cell].valid_stances;

	return EmitHookField(state, cell, SG_RUNE_COMPACT_INDEX_NONE, stances) &&
		EmitDirectTransitionFields(state, cell, MOVEMENT_FAMILY_MOVER) &&
		EmitDirectTransitionFields(state, cell, MOVEMENT_FAMILY_EXTERNAL_FORCE) &&
		EmitControllerFields(state, cell);
}

static int EmitBoundaryFields(emit_state_t *state, uint32_t cell,
	uint32_t portal)
{
	uint32_t negative;
	uint32_t positive;

	if (!GetPortalCells(state->input, portal, &negative, &positive))
		return 0;
	(void)state;
	(void)cell;
	(void)negative;
	(void)positive;
	return 1;
}

static int EmitAllFields(emit_state_t *state)
{
	uint32_t cell;

	for (cell = 0U; cell < state->input->cell_count; cell++) {
		const index_workspace_t *index = state->index;
		uint32_t local;

		for (local = index->cell_portal_offsets[cell];
			local < index->cell_portal_offsets[cell + 1U]; local++)
			if (!EmitBoundaryFields(state, cell, index->cell_portals[local]))
				return 0;
		if (!EmitInteriorFields(state, cell))
			return 0;
	}
	return 1;
}

static int EmitHookTargetFunctions(emit_state_t *state)
{
	uint32_t target;

	if (state == NULL || state->emit == 0 || state->output == NULL)
		return 0;
	for (target = 0U; target < state->output->hook_target_count; target++) {
		sg_rune_compact_movement_hook_target_t *record =
			&state->output->hook_targets[target];
		const sg_rune_compact_movement_fiber_t *fiber =
			&state->output->fibers[record->fiber.value];
		const sg_rune_movement_capability_t *capability =
			&state->output->capabilities[fiber->capability.value];
		const int release_grounded =
			capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE &&
			state->output->hook_release_grounded[record->fiber.value] != 0U;
		const uint32_t body_profile = record->visibility_class ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_VISIBLE ? PROFILE_HOOK_VISIBLE :
			record->visibility_class ==
				SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL ?
					PROFILE_HOOK_CONDITIONAL : PROFILE_HOOK_BLOCKED;

		if (!AppendProfileFunctionSpan(state, PROFILE_HOOK_FLIGHT,
				&record->functions.bolt) ||
			!AppendProfileFunctionSpan(state, body_profile,
				&record->functions.body) ||
			!AppendProfileFunctionSpan(state, body_profile,
				&record->functions.pull) ||
			!AppendProfileFunctionSpan(state, release_grounded != 0 ?
				PROFILE_HOOK_COAST_GROUNDED : PROFILE_HOOK_COAST,
				&record->functions.release) ||
			!AppendProfileFunctionSpan(state, PROFILE_HOOK_COAST,
				&record->functions.coast) ||
			!AppendProfileFunctionSpan(state, PROFILE_HOOK_FLIGHT,
				&record->functions.relaunch))
			return 0;
	}
	return 1;
}

static int CountFields(const sg_rune_compact_movement_fields_input_t *input,
	const index_workspace_t *index, const profile_t *profiles,
	uint32_t profile_count,
	uint32_t *capability_count, uint32_t *fiber_count,
	uint32_t *hook_target_count, uint32_t *function_ref_count)
{
	emit_state_t state;

	memset(&state, 0, sizeof(state));
	state.input = input;
	state.index = index;
	state.profiles = profiles;
	state.profile_count = profile_count;
	if (!EmitAllFields(&state) || capability_count == NULL ||
		fiber_count == NULL || hook_target_count == NULL ||
		function_ref_count == NULL)
		return 0;
	*capability_count = state.attachment_cursor;
	*fiber_count = state.fiber_cursor;
	*hook_target_count = state.hook_target_cursor;
	if (!AddU32(state.function_cursor, state.phase_function_count,
		function_ref_count))
		return 0;
	return *capability_count != 0U && *fiber_count != 0U &&
		*function_ref_count != 0U;
}

static int CompareMovementState(const void *left_pointer,
	const void *right_pointer)
{
	const sg_rune_compact_movement_state_t *left = left_pointer;
	const sg_rune_compact_movement_state_t *right = right_pointer;

#define STATE_COMPARE(field) \
	do { \
		if ((uint32_t)left->field != (uint32_t)right->field) \
			return (uint32_t)left->field < (uint32_t)right->field ? -1 : 1; \
	} while (0)
	STATE_COMPARE(stance);
	STATE_COMPARE(support);
	STATE_COMPARE(water);
	STATE_COMPARE(hook_phase);
	STATE_COMPARE(flags);
	STATE_COMPARE(mover_mechanism);
#undef STATE_COMPARE
	return 0;
}

static int CapabilityState(const sg_rune_compact_movement_bound_input_t *input,
	const index_workspace_t *index,
	const sg_rune_compact_movement_fields_t *fields,
	const sg_rune_movement_capability_t *capability,
	const sg_rune_compact_movement_fiber_t *fiber, int destination,
	sg_rune_compact_movement_state_t *state)
{
	uint32_t cell = capability->cell.value;
	uint32_t stance = capability->source_stances;
	uint32_t mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	const sg_configuration_semantic_region_t *region;

	if (destination != 0) {
		uint32_t negative;
		uint32_t positive;

		stance = capability->destination_stances;
		if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION &&
			capability->kind !=
				SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) {
			if (fiber->mechanism_transition.value >=
				input->mechanisms->transition_count)
				return 0;
			cell = input->mechanisms->transitions[
				fiber->mechanism_transition.value].exit_cell.value;
		} else if (capability->boundary_portal.value !=
			SG_RUNE_COMPACT_INDEX_NONE) {
			if (!GetPortalCells(input, capability->boundary_portal.value,
				&negative, &positive))
				return 0;
			cell = cell == negative ? positive : negative;
		}
	}
	if ((stance != SG_RUNE_STANCE_VALID_STANDING &&
		stance != SG_RUNE_STANCE_VALID_CROUCHING) || cell >= input->cell_count)
		return 0;
	region = RegionForCell(input, index, cell);
	memset(state, 0, sizeof(*state));
	state->stance = (sg_rune_stance_validity_t)stance;
	state->support = IsSupported(region) ? SG_RUNE_MOVEMENT_SUPPORT_STATIC :
		SG_RUNE_MOVEMENT_SUPPORT_NONE;
	state->water = IsWater(region, &input->cells[cell]) ?
		SG_RUNE_MOVEMENT_WATER_SUBMERGED : SG_RUNE_MOVEMENT_WATER_DRY;
	state->hook_phase = SG_HOST_HOOK_IDLE;
	state->flags = state->support == SG_RUNE_MOVEMENT_SUPPORT_NONE ?
		SG_RUNE_MOVEMENT_STATE_AIRBORNE : 0U;
	state->mover_mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	if (capability->kind >= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		capability->kind <= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH) {
		static const sg_host_hook_phase_t source_phases[6] = {
			SG_HOST_HOOK_IDLE, SG_HOST_HOOK_IN_FLIGHT,
			SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_ATTACHED,
			SG_HOST_HOOK_COAST, SG_HOST_HOOK_COAST
		};
		static const sg_host_hook_phase_t destination_phases[6] = {
			SG_HOST_HOOK_IN_FLIGHT, SG_HOST_HOOK_ATTACHED,
			SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_COAST,
			SG_HOST_HOOK_COAST, SG_HOST_HOOK_IN_FLIGHT
		};
		const uint32_t phase = (uint32_t)capability->kind -
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;

		state->hook_phase = destination != 0 ? destination_phases[phase] :
			source_phases[phase];
		if (capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) {
			const uint32_t fiber_index = (uint32_t)(fiber - fields->fibers);
			const uint32_t variant = fiber_index - capability->fibers.first;
			const int grounded = fiber_index < fields->fiber_count &&
				fields->hook_release_grounded[fiber_index] != 0U;

			if (variant >= capability->fibers.count || variant >= 4U)
				return 0;
			state->hook_phase = destination != 0 ? SG_HOST_HOOK_COAST :
				variant < 2U ? SG_HOST_HOOK_IN_FLIGHT : SG_HOST_HOOK_ATTACHED;
			state->support = grounded ? SG_RUNE_MOVEMENT_SUPPORT_STATIC :
				SG_RUNE_MOVEMENT_SUPPORT_NONE;
			if (grounded)
				state->flags &=
					(sg_rune_movement_state_flags_t)
					~(sg_rune_movement_state_flags_t)
						SG_RUNE_MOVEMENT_STATE_AIRBORNE;
			else
				state->flags |= SG_RUNE_MOVEMENT_STATE_AIRBORNE;
		}
	}
	if (capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER) {
		if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION) {
			const uint32_t authority_transition =
				fiber->mechanism_transition.value;
			const sg_rune_compact_mechanism_transition_t *transition;
			uint32_t static_transition;

			if (authority_transition >= input->mechanisms->transition_count)
				return 0;
			transition = &input->mechanisms->transitions[authority_transition];
			if (transition->kind ==
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
				return state->mover_mechanism == SG_RUNE_COMPACT_INDEX_NONE &&
					(state->flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U;
			if (transition->kind !=
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
				return 0;
			static_transition =
				index->authority_transition_static[authority_transition];
			if (static_transition >= input->static_data->transition_count)
				return 0;
			/* State and live mechanism snapshots use authority-mechanism
			 * indexes.  The static projection only validates the independently
			 * canonical transition; its mechanism index may differ after fanout
			 * and must never escape into the authority state domain. */
			mechanism = transition->mechanism;
			if (mechanism >= input->mechanisms->mechanism_count)
				return 0;
		}
		else if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER) {
			const sg_rune_compact_movement_angular_schedule_t *schedule =
				&fields->angular_schedules[fiber->angular_schedule];
			mechanism = schedule->authority_mechanism.value;
			if (schedule->static_mechanism.value >=
					input->static_data->mechanism_count ||
				mechanism >= input->mechanisms->mechanism_count)
				return 0;
		}
		state->support = SG_RUNE_MOVEMENT_SUPPORT_MOVER;
		state->flags = (state->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) |
			SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
		state->mover_mechanism = mechanism;
	} else if (capability->kind ==
		SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE) {
		state->flags |= SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE;
	}
	return state->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE ||
		capability->kind != SG_RUNE_MOVEMENT_CAPABILITY_MOVER;
}

static int BuildMovementStates(
	const sg_rune_compact_movement_bound_input_t *input,
	const index_workspace_t *index, sg_rune_compact_movement_fields_t *fields,
	sg_rune_compact_movement_fields_error_t *error)
{
	sg_rune_compact_movement_state_t *candidates;
	uint32_t candidate_count;
	uint32_t fiber;
	uint32_t state_count;

	if (fields->fiber_count > UINT32_MAX / 2U)
		return 0;
	candidate_count = fields->fiber_count * 2U;
	candidates = AllocateArray(candidate_count, sizeof(*candidates));
	if (candidate_count != 0U && candidates == NULL) {
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, candidate_count, 0U);
		return 0;
	}
	for (fiber = 0U; fiber < fields->fiber_count; fiber++) {
		const sg_rune_compact_movement_fiber_t *record = &fields->fibers[fiber];
		const sg_rune_movement_capability_t *capability =
			&fields->capabilities[record->capability.value];

		if (!CapabilityState(input, index, fields, capability, record, 0,
			&candidates[fiber * 2U]) ||
			!CapabilityState(input, index, fields, capability, record, 1,
				&candidates[fiber * 2U + 1U])) {
			free(candidates);
			return 0;
		}
	}
	if (candidate_count > 1U)
		qsort(candidates, candidate_count, sizeof(*candidates),
			CompareMovementState);
	state_count = candidate_count == 0U ? 0U : 1U;
	for (fiber = 1U; fiber < candidate_count; fiber++)
		if (CompareMovementState(&candidates[state_count - 1U],
			&candidates[fiber]) != 0)
			candidates[state_count++] = candidates[fiber];
	fields->states = AllocateArray(state_count, sizeof(*fields->states));
	if (state_count != 0U && fields->states == NULL) {
		free(candidates);
		SetError(error, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
			0U, state_count, 0U);
		return 0;
	}
	memcpy(fields->states, candidates,
		(size_t)state_count * sizeof(*fields->states));
	fields->state_count = state_count;
	for (fiber = 0U; fiber < fields->fiber_count; fiber++) {
		sg_rune_compact_movement_fiber_t *record = &fields->fibers[fiber];
		const sg_rune_movement_capability_t *capability =
			&fields->capabilities[record->capability.value];
		sg_rune_compact_movement_state_t source;
		sg_rune_compact_movement_state_t destination;
		sg_rune_compact_movement_state_t *found;

		if (!CapabilityState(input, index, fields, capability, record, 0,
			&source) || !CapabilityState(input, index, fields, capability, record,
				1, &destination)) {
			free(candidates);
			return 0;
		}
		found = bsearch(&source, fields->states, state_count,
			sizeof(*fields->states), CompareMovementState);
		if (found == NULL) {
			free(candidates);
			return 0;
		}
		record->source_state.value = (uint32_t)(found - fields->states);
		found = bsearch(&destination, fields->states, state_count,
			sizeof(*fields->states), CompareMovementState);
		if (found == NULL) {
			free(candidates);
			return 0;
		}
		record->destination_state.value = (uint32_t)(found - fields->states);
	}
	free(candidates);
	return state_count != 0U;
}

static int CopyAnalytic(const sg_rune_compact_analytic_t *source,
	const analytic_workspace_t *workspace,
	sg_rune_compact_movement_fields_t *owner)
{
	sg_rune_compact_analytic_t *target = &owner->analytic;

	memset(target, 0, sizeof(*target));
	target->version = source->version;
	target->reserved = source->reserved;
	target->function_count = source->function_count;
	target->input_dimension_count = source->input_dimension_count;
	target->affine_count = source->affine_count;
	target->affine_slope_count = source->affine_slope_count;
	target->polynomial_count = source->polynomial_count;
	target->polynomial_coefficient_count =
		source->polynomial_coefficient_count;
	target->ballistic_count = source->ballistic_count;
	target->piecewise_count = source->piecewise_count;
	target->piecewise_clause_count = source->piecewise_clause_count;
	if (target->function_count != 0U) {
		owner->owned_functions = AllocateArray(target->function_count,
			sizeof(*target->functions));
		if (owner->owned_functions == NULL)
			return 0;
		target->functions = owner->owned_functions;
		memcpy(owner->owned_functions, source->functions,
			(size_t)target->function_count * sizeof(*target->functions));
	}
	if (target->input_dimension_count != 0U) {
		owner->owned_input_dimensions = AllocateArray(
			target->input_dimension_count,
			sizeof(*target->input_dimensions));
		if (owner->owned_input_dimensions == NULL)
			return 0;
		target->input_dimensions = owner->owned_input_dimensions;
		memcpy(owner->owned_input_dimensions, source->input_dimensions,
			(size_t)target->input_dimension_count *
				sizeof(*target->input_dimensions));
	}
	if (target->affine_count != 0U) {
		owner->owned_affines = AllocateArray(target->affine_count,
			sizeof(*target->affines));
		if (owner->owned_affines == NULL)
			return 0;
		target->affines = owner->owned_affines;
		memcpy(owner->owned_affines, source->affines,
			(size_t)target->affine_count * sizeof(*target->affines));
	}
	if (target->affine_slope_count != 0U) {
		owner->owned_affine_slopes = AllocateArray(
			target->affine_slope_count,
			sizeof(*target->affine_slopes));
		if (owner->owned_affine_slopes == NULL)
			return 0;
		target->affine_slopes = owner->owned_affine_slopes;
		memcpy(owner->owned_affine_slopes, source->affine_slopes,
			(size_t)target->affine_slope_count *
				sizeof(*target->affine_slopes));
	}
	if (target->polynomial_count != 0U) {
		owner->owned_polynomials = AllocateArray(target->polynomial_count,
			sizeof(*target->polynomials));
		if (owner->owned_polynomials == NULL)
			return 0;
		target->polynomials = owner->owned_polynomials;
		memcpy(owner->owned_polynomials, source->polynomials,
			(size_t)target->polynomial_count * sizeof(*target->polynomials));
	}
	if (target->polynomial_coefficient_count != 0U) {
		owner->owned_polynomial_coefficients =
			AllocateArray(target->polynomial_coefficient_count,
				sizeof(*target->polynomial_coefficients));
		if (owner->owned_polynomial_coefficients == NULL)
			return 0;
		target->polynomial_coefficients = owner->owned_polynomial_coefficients;
		memcpy(owner->owned_polynomial_coefficients,
			source->polynomial_coefficients,
			(size_t)target->polynomial_coefficient_count *
				sizeof(*target->polynomial_coefficients));
	}
	if (target->ballistic_count != 0U) {
		owner->owned_ballistics = AllocateArray(target->ballistic_count,
			sizeof(*target->ballistics));
		if (owner->owned_ballistics == NULL)
			return 0;
		target->ballistics = owner->owned_ballistics;
		memcpy(owner->owned_ballistics, source->ballistics,
			(size_t)target->ballistic_count * sizeof(*target->ballistics));
	}
	if (target->piecewise_count != 0U) {
		owner->owned_piecewise = AllocateArray(target->piecewise_count,
			sizeof(*target->piecewise));
		if (owner->owned_piecewise == NULL)
			return 0;
		target->piecewise = owner->owned_piecewise;
		memcpy(owner->owned_piecewise, source->piecewise,
			(size_t)target->piecewise_count * sizeof(*target->piecewise));
	}
	if (target->piecewise_clause_count != 0U) {
		owner->owned_piecewise_clauses = AllocateArray(
			target->piecewise_clause_count,
			sizeof(*target->piecewise_clauses));
		if (owner->owned_piecewise_clauses == NULL)
			return 0;
		target->piecewise_clauses = owner->owned_piecewise_clauses;
		memcpy(owner->owned_piecewise_clauses, source->piecewise_clauses,
			(size_t)target->piecewise_clause_count *
				sizeof(*target->piecewise_clauses));
	}
	(void)workspace;
	return 1;
}

static void DestroyAnalytic(sg_rune_compact_movement_fields_t *fields)
{
	if (fields == NULL)
		return;
	free(fields->owned_functions);
	free(fields->owned_input_dimensions);
	free(fields->owned_affines);
	free(fields->owned_affine_slopes);
	free(fields->owned_polynomials);
	free(fields->owned_polynomial_coefficients);
	free(fields->owned_ballistics);
	free(fields->owned_piecewise);
	free(fields->owned_piecewise_clauses);
	memset(&fields->analytic, 0, sizeof(fields->analytic));
}

#undef sg_rune_compact_movement_fields_input_t

int SG_RuneCompactMovementFieldsBuild(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_t **fields_out,
	sg_rune_compact_movement_fields_error_t *error_out)
{
	index_workspace_t index;
	analytic_workspace_t workspace;
	sg_rune_compact_movement_bound_input_t bound_input;
	sg_rune_compact_static_t bound_static;
	sg_rune_compact_identity_t bound_static_identity;
	sg_rune_compact_builder_owner_view_t bound_builder_view;
	sg_rune_compact_geometry_view_t bound_geometry_view;
	sg_rune_compact_response_partition_view_t bound_response_view;
	sg_rune_compact_mechanisms_view_t bound_mechanisms_view;
	const sg_rune_compact_movement_bound_input_t *construction_input;
	profile_t *profiles = NULL;
	sg_rune_compact_analytic_t workspace_analytic;
	sg_rune_compact_movement_fields_t *fields;
	sg_rune_analytic_error_t analytic_error;
	emit_state_t emit;
	uint32_t capability_count;
	uint32_t fiber_count;
	uint32_t hook_target_count;
	uint32_t function_ref_count;
	uint32_t profile_capacity;
	uint32_t profile_count;
	uint32_t profile_index;
	sg_host_law_construction_view_t host_construction;
	int workspace_allocation_failed = 0;
	int profile_allocation_failed = 0;

	SetError(error_out, SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_NONE, 0U, 0U,
		0U);
	if (fields_out == NULL) {
		SetError(error_out,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT, 0U, 1U,
			0U);
		return 0;
	}
	if (!BindOwners(input, &bound_input, &bound_builder_view,
		&bound_geometry_view, &bound_response_view, &bound_mechanisms_view,
		&host_construction, &bound_static, &bound_static_identity, error_out))
		return 0;
	construction_input = &bound_input;
	if (!ValidateInput(construction_input, error_out))
		return 0;
	memset(&index, 0, sizeof(index));
	memset(&workspace, 0, sizeof(workspace));
	memset(&workspace_analytic, 0, sizeof(workspace_analytic));
	memset(&analytic_error, 0, sizeof(analytic_error));
	if (!BuildIndexes(construction_input, &index, error_out)) {
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		return 0;
	}
	if (!AddU32(PROFILE_BASE_COUNT,
			construction_input->static_data->transition_count,
			&profile_capacity)) {
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SetError(error_out,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_LIMIT_EXCEEDED, 0U,
			UINT32_MAX, construction_input->static_data->transition_count);
		return 0;
	}
	profiles = AllocateArray(profile_capacity, sizeof(*profiles));
	if (profiles == NULL) {
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SetError(error_out,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY, 0U, 0U,
			0U);
		return 0;
	}
	if (!BuildProfiles(construction_input, &index, profiles, profile_capacity,
		&profile_count, &workspace) ||
		!CanonicalizeProfiles(&workspace, profiles, profile_count) ||
		!MaterializeAnalytic(&workspace, &workspace_analytic) ||
		!SG_RuneCompactAnalyticValidate(&workspace_analytic, &analytic_error)) {
		workspace_allocation_failed = workspace.allocation_failed;
		for (profile_index = 0U; profile_index < profile_capacity;
			profile_index++)
			if (profiles[profile_index].allocation_failed != 0)
				profile_allocation_failed = 1;
		ProfilesDestroy(profiles, profile_capacity);
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SetError(error_out, workspace_allocation_failed != 0 ||
			profile_allocation_failed ?
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY :
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_ANALYTIC_CONTRACT,
			analytic_error.record, 0U, analytic_error.code);
		return 0;
	}
	RemapProfiles(profiles, profile_count, &workspace);
	if (!CountFields(construction_input, &index, profiles, profile_count,
		&capability_count, &fiber_count, &hook_target_count,
		&function_ref_count)) {
		ProfilesDestroy(profiles, profile_capacity);
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SetError(error_out,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_LIMIT_EXCEEDED, 0U, 1U,
			0U);
		return 0;
	}
	fields = calloc(1U, sizeof(*fields));
	if (fields == NULL) {
		ProfilesDestroy(profiles, profile_capacity);
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SetError(error_out,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY, 0U, 0U,
			0U);
		return 0;
	}
	fields->capabilities = AllocateArray(capability_count,
		sizeof(*fields->capabilities));
	fields->fibers = AllocateArray(fiber_count, sizeof(*fields->fibers));
	fields->hook_release_grounded = AllocateArray(fiber_count,
		sizeof(*fields->hook_release_grounded));
	fields->hook_targets = AllocateArray(hook_target_count,
		sizeof(*fields->hook_targets));
	fields->fiber_function_refs = AllocateArray(function_ref_count,
		sizeof(*fields->fiber_function_refs));
	if (fields->capabilities == NULL || fields->fibers == NULL ||
		fields->hook_release_grounded == NULL ||
		fields->fiber_function_refs == NULL ||
		(hook_target_count != 0U && fields->hook_targets == NULL) ||
		!CopyAnalytic(&workspace_analytic, &workspace, fields)) {
		ProfilesDestroy(profiles, profile_capacity);
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SG_RuneCompactMovementFieldsDestroy(fields);
		SetError(error_out,
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY, 0U, 0U,
			0U);
		return 0;
	}
	fields->capability_count = capability_count;
	fields->fiber_count = fiber_count;
	fields->hook_target_count = hook_target_count;
	fields->fiber_function_ref_count = function_ref_count;
	fields->pmove_abi = construction_input->host_law->pmove_abi;
	fields->pmove_behavior_fingerprint =
		construction_input->host_law->pmove_behavior_fingerprint;
	fields->host_level_generation = host_construction.level_generation;
	fields->physics_abi_id = construction_input->host_law->static_identity.physics_abi_id;
	fields->collision_law_id = construction_input->host_law->collision_law_id;
	fields->pmove_law_id = construction_input->host_law->pmove_law_id;
	fields->gravity_law_id = construction_input->host_law->gravity_law_id;
	fields->hook_law_id = construction_input->host_law->hook_law_id;
	fields->mechanism_law_id = construction_input->host_law->mechanism_law_id;
	fields->identity = construction_input->geometry->identity;
	{
		const int angular_ok = BuildAngularSchedules(construction_input, fields,
			error_out);

		if (!angular_ok) {
		ProfilesDestroy(profiles, profile_capacity);
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SG_RuneCompactMovementFieldsDestroy(fields);
		return 0;
		}
	}
	memset(&emit, 0, sizeof(emit));
	emit.input = construction_input;
	emit.index = &index;
	emit.profiles = profiles;
	emit.profile_count = profile_count;
	emit.output = fields;
	emit.emit = 1;
	{
		const int emit_ok = EmitAllFields(&emit) &&
			emit.function_cursor <= function_ref_count &&
			EmitHookTargetFunctions(&emit) &&
			BuildMovementStates(construction_input, &index, fields, error_out);

		if (!emit_ok || emit.attachment_cursor != capability_count ||
			emit.fiber_cursor != fiber_count ||
			emit.hook_target_cursor != hook_target_count ||
			emit.function_cursor != function_ref_count) {
		ProfilesDestroy(profiles, profile_capacity);
		IndexDestroy(&index);
		WorkspaceDestroy(&workspace);
		SG_RuneCompactMovementFieldsDestroy(fields);
			if (error_out == NULL ||
				error_out->code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_NONE)
				SetError(error_out,
					SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_ANALYTIC_CONTRACT, 0U,
					function_ref_count, emit.function_cursor);
		return 0;
		}
	}
	ProfilesDestroy(profiles, profile_capacity);
	IndexDestroy(&index);
	WorkspaceDestroy(&workspace);
	{
		sg_host_law_construction_view_t final_host_view = { 0 };
		sg_rune_compact_static_t final_static_view;
		sg_rune_compact_identity_t final_static_identity;

		if (!ValidateOwnerBindings(construction_input, error_out) ||
			SG_HostLawConstructionRead(construction_input->host_owner,
				&final_host_view).status != SG_HOST_LAW_OK ||
			final_host_view.current == 0U ||
			final_host_view.level_generation != host_construction.level_generation ||
			!PmoveAbiEqual(&final_host_view.laws.pmove_abi,
				&host_construction.laws.pmove_abi) ||
			final_host_view.laws.pmove_behavior_fingerprint !=
				host_construction.laws.pmove_behavior_fingerprint ||
			!SG_RuneCompactStaticMaterializerReadBound(
				construction_input->static_owner, &final_static_identity,
				&final_static_view) ||
			!CompactIdentityEqual(&bound_static_identity,
				&final_static_identity) ||
			!StaticViewsEqual(&bound_static, &final_static_view)) {
			SG_RuneCompactMovementFieldsDestroy(fields);
			SetError(error_out,
				SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW, 0U,
				host_construction.level_generation,
				final_host_view.level_generation);
			return 0;
		}
	}
	*fields_out = fields;
	return 1;
}

int SG_RuneCompactMovementFieldsRead(
	const sg_rune_compact_movement_fields_t *fields,
	sg_rune_compact_movement_fields_view_t *view_out)
{
	if (fields == NULL || view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = fields->identity;
	view_out->capabilities = fields->capabilities;
	view_out->capability_count = fields->capability_count;
	view_out->states = fields->states;
	view_out->state_count = fields->state_count;
	view_out->fibers = fields->fibers;
	view_out->fiber_count = fields->fiber_count;
	view_out->hook_targets = fields->hook_targets;
	view_out->hook_target_count = fields->hook_target_count;
	view_out->fiber_function_refs = fields->fiber_function_refs;
	view_out->fiber_function_ref_count = fields->fiber_function_ref_count;
	view_out->analytic = fields->analytic;
	view_out->angular_schedules = fields->angular_schedules;
	view_out->angular_schedule_count = fields->angular_schedule_count;
	view_out->pmove_abi = fields->pmove_abi;
	view_out->pmove_behavior_fingerprint = fields->pmove_behavior_fingerprint;
	view_out->host_level_generation = fields->host_level_generation;
	view_out->physics_abi_id = fields->physics_abi_id;
	view_out->collision_law_id = fields->collision_law_id;
	view_out->pmove_law_id = fields->pmove_law_id;
	view_out->gravity_law_id = fields->gravity_law_id;
	view_out->hook_law_id = fields->hook_law_id;
	view_out->mechanism_law_id = fields->mechanism_law_id;
	return 1;
}

int SG_RuneCompactMovementFieldsReadBound(
	const sg_rune_compact_movement_fields_t *fields,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_movement_fields_view_t *view_out)
{
	if (fields == NULL || identity_out == NULL || view_out == NULL ||
		!SG_RuneCompactMovementFieldsRead(fields, view_out))
		return 0;
	*identity_out = fields->identity;
	view_out->identity = *identity_out;
	return 1;
}

int SG_RuneCompactMovementAngularInitial(
	const sg_rune_compact_movement_angular_schedule_t *schedule,
	float angles_out[3])
{
	uint32_t axis;

	if (schedule == NULL || angles_out == NULL || schedule->frame_ms == 0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		angles_out[axis] = CompactScalar(schedule->initial_angles_bits[axis]);
		if (!ScalarValid(angles_out[axis]))
			return 0;
	}
	return 1;
}

int SG_RuneCompactMovementAngularFrame(
	const sg_rune_compact_movement_angular_schedule_t *schedule,
	const float current_angles[3], int active, int frame_succeeded,
	float angles_out[3])
{
	uint32_t axis;

	if (schedule == NULL || current_angles == NULL || angles_out == NULL ||
		(active != 0 && active != 1) ||
		(frame_succeeded != 0 && frame_succeeded != 1) ||
		schedule->frame_ms == 0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		const float delta = CompactScalar(
			schedule->frame_angular_delta_bits[axis]);
		float next;

		if (!ScalarValid(current_angles[axis]) || !ScalarValid(delta))
			return 0;
		next = active != 0 && frame_succeeded != 0 ?
			current_angles[axis] + delta : current_angles[axis];
		if (!ScalarValid(next))
			return 0;
		angles_out[axis] = next;
	}
	return 1;
}

void SG_RuneCompactMovementFieldsDestroy(
	sg_rune_compact_movement_fields_t *fields)
{
	if (fields == NULL)
		return;
	free(fields->capabilities);
	free(fields->states);
	free(fields->fibers);
	free(fields->hook_release_grounded);
	free(fields->hook_targets);
	free(fields->fiber_function_refs);
	free(fields->angular_schedules);
	DestroyAnalytic(fields);
	free(fields);
}

#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TESTING)
uint64_t SG_RuneCompactMovementFieldsTestPortalMergeSteps(void)
{
	return sg_portal_merge_steps;
}
#endif

const char *SG_RuneCompactMovementFieldsErrorString(
	sg_rune_compact_movement_fields_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY:
		return "invalid geometry";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION:
		return "invalid configuration";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW:
		return "invalid host law";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY:
		return "invalid visibility";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA:
		return "invalid static data";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_ANALYTIC_CONTRACT:
		return "analytic contract";
	case SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_CODE_COUNT:
		break;
	}
	return "unknown movement-field error";
}
