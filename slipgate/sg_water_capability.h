/* Deterministic water-volume capability facts over audited configuration data. */
#ifndef SG_WATER_CAPABILITY_H
#define SG_WATER_CAPABILITY_H

#include <stdint.h>

#include "sg_configuration_semantics.h"
#include "sg_host_pmove.h"

#define SG_WATER_CAPABILITY_INDEX_NONE UINT32_MAX

typedef enum sg_water_capability_error_code_e
{
	SG_WATER_CAPABILITY_ERROR_NONE = 0,
	SG_WATER_CAPABILITY_ERROR_INVALID_ARGUMENT,
	SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE,
	SG_WATER_CAPABILITY_ERROR_INVALID_PHASE,
	SG_WATER_CAPABILITY_ERROR_NONFINITE,
	SG_WATER_CAPABILITY_ERROR_HOST_DISAGREEMENT,
	SG_WATER_CAPABILITY_ERROR_SOLVER,
	SG_WATER_CAPABILITY_ERROR_OVERFLOW,
	SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY
} sg_water_capability_error_code_t;

typedef struct sg_water_capability_error_s
{
	sg_water_capability_error_code_t code;
	uint32_t source_index;
} sg_water_capability_error_t;

/* This limit protects the serialized kernel representation. It does not
 * truncate capability discovery. */
typedef struct sg_water_capability_limits_s
{
	uint32_t max_facts;
} sg_water_capability_limits_t;

typedef struct sg_water_phase_binding_s
{
	uint64_t semantic_region_id;
	uint32_t phase;
	uint32_t reserved;
} sg_water_phase_binding_t;

typedef enum sg_water_capability_kind_e
{
	SG_WATER_CAPABILITY_DIRECTIONAL_SWIM = 0,
	SG_WATER_CAPABILITY_CURRENT,
	SG_WATER_CAPABILITY_SINK,
	SG_WATER_CAPABILITY_SURFACE,
	SG_WATER_CAPABILITY_ENTRY,
	SG_WATER_CAPABILITY_EXIT,
	SG_WATER_CAPABILITY_VOLUME_CROSSING,
	SG_WATER_CAPABILITY_KIND_COUNT
} sg_water_capability_kind_t;

typedef enum sg_water_direction_e
{
	SG_WATER_DIRECTION_POSITIVE_X = 0,
	SG_WATER_DIRECTION_NEGATIVE_X,
	SG_WATER_DIRECTION_POSITIVE_Y,
	SG_WATER_DIRECTION_NEGATIVE_Y,
	SG_WATER_DIRECTION_POSITIVE_Z,
	SG_WATER_DIRECTION_NEGATIVE_Z,
	SG_WATER_DIRECTION_BOUNDARY,
	SG_WATER_DIRECTION_COUNT
} sg_water_direction_t;

typedef uint32_t sg_water_capability_flags_t;
enum
{
	SG_WATER_CAPABILITY_DIRECTIONAL = UINT32_C(1) << 0,
	SG_WATER_CAPABILITY_CHANGES_MEDIUM = UINT32_C(1) << 1,
	SG_WATER_CAPABILITY_USES_CURRENT = UINT32_C(1) << 2,
	SG_WATER_CAPABILITY_CROSSES_PORTAL = UINT32_C(1) << 3,
	SG_WATER_CAPABILITY_HOST_PROVEN = UINT32_C(1) << 4
};

typedef struct sg_water_capability_fact_s
{
	/* Region-scoped facts cover the full audited convex semantic volume.
	 * Witnesses validate host law; they do not narrow that scope. */
	uint32_t order;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t source_phase;
	uint32_t destination_phase;
	uint32_t portal;
	sg_water_capability_kind_t kind;
	sg_water_direction_t direction;
	sg_rune_medium_t source_medium;
	sg_rune_medium_t destination_medium;
	sg_rune_contents_mask_t source_contents;
	sg_rune_contents_mask_t destination_contents;
	sg_rune_contents_mask_t current;
	uint8_t source_water_level;
	uint8_t destination_water_level;
	uint8_t reserved[2];
	sg_rune_vec3_t source_witness;
	sg_rune_vec3_t boundary_witness;
	sg_rune_vec3_t destination_witness;
	sg_rune_vec3_t direction_vector;
	sg_rune_vec3_t observed_displacement;
	sg_rune_vec3_t observed_velocity;
	sg_rune_kernel_parameters_t parameters;
	sg_water_capability_flags_t flags;
} sg_water_capability_fact_t;

typedef struct sg_water_capability_set_s
{
	sg_rune_model_identity_t identity;
	sg_water_capability_fact_t *facts;
	uint32_t fact_count;
	uint32_t wet_region_count;
	uint32_t boundary_count;
	uint64_t host_pmove_frames;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint32_t lattice_maximum_binary_shift;
} sg_water_capability_set_t;

void SG_WaterCapabilityDefaultLimits(
	sg_water_capability_limits_t *limits_out);
int SG_WaterCapabilityBuild(
	const sg_host_collision_authority_t *authority,
	sg_host_pmove_function_t host_pmove,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_phase_basis_t *phases, uint32_t phase_count,
	const sg_water_phase_binding_t *bindings, uint32_t binding_count,
	const sg_water_capability_limits_t *limits,
	sg_water_capability_set_t **capabilities_out,
	sg_water_capability_error_t *error_out);
void SG_WaterCapabilityDestroy(sg_water_capability_set_t *capabilities);
const char *SG_WaterCapabilityErrorString(
	sg_water_capability_error_code_t code);

#endif
