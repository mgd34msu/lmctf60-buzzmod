#ifndef SG_GROUND_CAPABILITY_H
#define SG_GROUND_CAPABILITY_H

#include <stddef.h>
#include <stdint.h>

#include "sg_configuration_semantics.h"
#include "sg_host_pmove.h"

#define SG_GROUND_CAPABILITY_INDEX_NONE UINT32_MAX

typedef enum sg_ground_capability_error_code_e
{
	SG_GROUND_CAPABILITY_ERROR_NONE = 0,
	SG_GROUND_CAPABILITY_ERROR_INVALID_ARGUMENT,
	SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE,
	SG_GROUND_CAPABILITY_ERROR_IDENTITY_MISMATCH,
	SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE,
	SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
	SG_GROUND_CAPABILITY_ERROR_OVERFLOW,
	SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY
} sg_ground_capability_error_code_t;

typedef struct sg_ground_capability_error_s
{
	sg_ground_capability_error_code_t code;
	uint32_t source_index;
} sg_ground_capability_error_t;

typedef struct sg_ground_capability_limits_s
{
	uint32_t max_capabilities;
} sg_ground_capability_limits_t;

typedef struct sg_ground_phase_binding_s
{
	uint32_t cell;
	uint32_t phase;
} sg_ground_phase_binding_t;

typedef enum sg_ground_capability_kind_e
{
	SG_GROUND_CAPABILITY_WALK = 0,
	SG_GROUND_CAPABILITY_CROUCH,
	SG_GROUND_CAPABILITY_RAMP,
	SG_GROUND_CAPABILITY_STEP,
	SG_GROUND_CAPABILITY_STANCE,
	SG_GROUND_CAPABILITY_JUMP_TAKEOFF,
	SG_GROUND_CAPABILITY_LANDING,
	SG_GROUND_CAPABILITY_DROP,
	SG_GROUND_CAPABILITY_KIND_COUNT
} sg_ground_capability_kind_t;

typedef uint32_t sg_ground_capability_flags_t;
enum
{
	SG_GROUND_CAPABILITY_DIRECTIONAL = UINT32_C(1) << 0,
	SG_GROUND_CAPABILITY_REQUIRES_SUPPORT = UINT32_C(1) << 1,
	SG_GROUND_CAPABILITY_CHANGES_STANCE = UINT32_C(1) << 2,
	SG_GROUND_CAPABILITY_CHANGES_SUPPORT = UINT32_C(1) << 3,
	SG_GROUND_CAPABILITY_VOID_ADJACENT = UINT32_C(1) << 4,
	SG_GROUND_CAPABILITY_PROVEN = UINT32_C(1) << 5
};

typedef struct sg_ground_capability_s
{
	uint32_t source_cell;
	uint32_t destination_cell;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t portal;
	uint32_t source_phase;
	uint32_t destination_phase;
	sg_ground_capability_kind_t kind;
	sg_rune_vec3_t source_witness;
	sg_rune_vec3_t destination_witness;
	sg_rune_interval3_t displacement;
	sg_rune_interval_t duration_ms;
	float acceleration;
	float gravity;
	uint64_t physics_abi_id;
	sg_ground_capability_flags_t flags;
} sg_ground_capability_t;

typedef struct sg_ground_capability_set_s
{
	sg_rune_model_identity_t identity;
	sg_ground_capability_t *capabilities;
	uint32_t capability_count;
	uint32_t proved_portals;
	uint32_t rejected_crossings;
	uint64_t pmove_frames;
} sg_ground_capability_set_t;

void SG_GroundCapabilityDefaultLimits(
	sg_ground_capability_limits_t *limits_out);
/* The complete-model integrator calls this only after the shared BSP and
 * configuration-semantics acceptance barrier has passed. */
int SG_GroundCapabilityBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_phase_basis_t *phases, size_t phase_count,
	const sg_ground_phase_binding_t *bindings, size_t binding_count,
	sg_host_pmove_function_t host_pmove,
	const sg_ground_capability_limits_t *limits,
	sg_ground_capability_set_t **set_out,
	sg_ground_capability_error_t *error_out);
void SG_GroundCapabilityDestroy(sg_ground_capability_set_t *set);
const char *SG_GroundCapabilityErrorString(
	sg_ground_capability_error_code_t code);

#endif
