/* Authenticated mechanism capabilities over complete configuration space. */
#ifndef SG_MECHANISM_CAPABILITY_H
#define SG_MECHANISM_CAPABILITY_H

#include <stdint.h>

#include "sg_bsp_completeness_proof.h"
#include "sg_bsp_entity_semantics.h"
#include "sg_configuration_semantics.h"
#include "sg_rune_mechanism_catalog.h"

#define SG_MECHANISM_CAPABILITY_INDEX_NONE UINT32_MAX

typedef enum sg_mechanism_capability_error_code_e
{
	SG_MECHANISM_CAPABILITY_ERROR_NONE = 0,
	SG_MECHANISM_CAPABILITY_ERROR_INVALID_ARGUMENT,
	SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH,
	SG_MECHANISM_CAPABILITY_ERROR_INCOMPLETE_CONFIGURATION,
	SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
	SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
	SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY,
	SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE,
	SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
	SG_MECHANISM_CAPABILITY_ERROR_TIMING,
	SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW,
	SG_MECHANISM_CAPABILITY_ERROR_OUT_OF_MEMORY
} sg_mechanism_capability_error_code_t;

typedef struct sg_mechanism_capability_error_s
{
	sg_mechanism_capability_error_code_t code;
	uint32_t source_index;
} sg_mechanism_capability_error_t;

typedef enum sg_mechanism_capability_kind_e
{
	SG_MECHANISM_CAPABILITY_DOOR_CROSSING = 0,
	SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION,
	SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION,
	SG_MECHANISM_CAPABILITY_DWELL,
	SG_MECHANISM_CAPABILITY_LIFT_RIDE,
	SG_MECHANISM_CAPABILITY_TRAIN_RIDE,
	SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING,
	SG_MECHANISM_CAPABILITY_PUSH,
	SG_MECHANISM_CAPABILITY_TELEPORT,
	SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE,
	SG_MECHANISM_CAPABILITY_RESET,
	SG_MECHANISM_CAPABILITY_KIND_COUNT
} sg_mechanism_capability_kind_t;

typedef enum sg_mechanism_state_e
{
	SG_MECHANISM_STATE_INACTIVE = 0,
	SG_MECHANISM_STATE_ACTIVATING,
	SG_MECHANISM_STATE_ACTIVE,
	SG_MECHANISM_STATE_DWELLING,
	SG_MECHANISM_STATE_RETURNING,
	SG_MECHANISM_STATE_RESET,
	SG_MECHANISM_STATE_INTERRUPTED,
	SG_MECHANISM_STATE_COUNT
} sg_mechanism_state_t;

typedef enum sg_mechanism_activation_e
{
	SG_MECHANISM_ACTIVATION_AUTOMATIC = 0,
	SG_MECHANISM_ACTIVATION_TOUCH,
	SG_MECHANISM_ACTIVATION_USE,
	SG_MECHANISM_ACTIVATION_DAMAGE,
	SG_MECHANISM_ACTIVATION_INVENTORY,
	SG_MECHANISM_ACTIVATION_COUNT
} sg_mechanism_activation_t;

typedef enum sg_mechanism_recovery_e
{
	SG_MECHANISM_RECOVERY_NONE = 0,
	SG_MECHANISM_RECOVERY_WAIT_FOR_RESET,
	SG_MECHANISM_RECOVERY_WAIT_FOR_CYCLE,
	SG_MECHANISM_RECOVERY_RELOCALIZE,
	SG_MECHANISM_RECOVERY_RETRY_ACTIVATION,
	SG_MECHANISM_RECOVERY_COUNT
} sg_mechanism_recovery_t;

typedef uint32_t sg_mechanism_host_trace_flags_t;
enum
{
	SG_MECHANISM_HOST_TRACE_ONE_SHOT = UINT32_C(1) << 0
};

#define SG_MECHANISM_HOST_TRACE_FLAGS_KNOWN SG_MECHANISM_HOST_TRACE_ONE_SHOT

/* One complete host observation. Absolute timestamps name the selected host
 * callbacks and movement frames; derived durations are never caller evidence. */
typedef struct sg_mechanism_host_trace_s
{
	uint64_t candidate_identity;
	uint64_t trace_identity;
	uint64_t source_set_identity;
	uint64_t bsp_content_id;
	uint64_t physics_abi_id;
	uint32_t controller_entity;
	uint32_t mechanism_entity;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t source_phase;
	uint32_t destination_phase;
	sg_mechanism_capability_kind_t kind;
	sg_mechanism_state_t source_state;
	sg_mechanism_state_t destination_state;
	sg_mechanism_activation_t activation;
	sg_mechanism_recovery_t recovery;
	sg_rune_vec3_t entry_witness;
	sg_rune_vec3_t exit_witness;
	sg_rune_vec3_t observed_displacement;
	sg_rune_vec3_t observed_velocity;
	sg_host_collision_scene_t inactive_scene;
	sg_host_collision_scene_t active_scene;
	sg_mech_execution_state_t source_execution;
	sg_mech_execution_state_t destination_execution;
	uint64_t mechanism_instance_id;
	uint64_t activation_time_ms;
	uint64_t active_time_ms;
	uint64_t exit_time_ms;
	uint64_t reset_time_ms;
	sg_mechanism_host_trace_flags_t flags;
} sg_mechanism_host_trace_t;

/* Configuration and entity construction produce this set independently of
 * host replay. The builder admits exactly one authenticated trace per row. */
typedef struct sg_mechanism_capability_candidate_s
{
	uint64_t candidate_identity;
	uint64_t source_set_identity;
	uint32_t controller_entity;
	uint32_t mechanism_entity;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t source_phase;
	uint32_t destination_phase;
	sg_mechanism_capability_kind_t kind;
	sg_mechanism_state_t source_state;
	sg_mechanism_state_t destination_state;
	sg_mechanism_activation_t activation;
	sg_mechanism_recovery_t recovery;
} sg_mechanism_capability_candidate_t;

typedef struct sg_mechanism_host_trace_catalog_s
{
	sg_rune_model_identity_t identity;
	const sg_mechanism_capability_candidate_t *candidates;
	uint32_t candidate_count;
	const sg_mechanism_host_trace_t *traces;
	uint32_t trace_count;
	uint64_t candidate_verifier_identity;
	uint64_t trace_verifier_identity;
} sg_mechanism_host_trace_catalog_t;

typedef uint32_t sg_mechanism_capability_flags_t;
enum
{
	SG_MECHANISM_CAPABILITY_CONDITIONAL = UINT32_C(1) << 0,
	SG_MECHANISM_CAPABILITY_MOVER_RELATIVE = UINT32_C(1) << 1,
	SG_MECHANISM_CAPABILITY_HOST_PROVEN = UINT32_C(1) << 2,
	SG_MECHANISM_CAPABILITY_ONE_SHOT = UINT32_C(1) << 3,
	SG_MECHANISM_CAPABILITY_RESETS = UINT32_C(1) << 4
};

/* Exact scheduled inputs retained until later kernel assembly. */
typedef struct sg_mechanism_kernel_parameters_s
{
	sg_rune_interval3_t displacement;
	sg_rune_interval_t speed;
	sg_rune_interval_t acceleration;
	sg_rune_interval_t vertical_acceleration;
	float gravity;
	float drag;
	uint64_t physics_abi_id;
	uint32_t duration_ms;
	uint32_t fixed_latency_ms;
	uint32_t dwell_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
	uint64_t total_ms;
} sg_mechanism_kernel_parameters_t;

typedef struct sg_mechanism_capability_fact_s
{
	uint32_t order;
	uint64_t trace_identity;
	sg_rune_mechanism_id_t controller_id;
	sg_rune_mechanism_id_t mechanism_id;
	uint32_t controller_entity;
	uint32_t mechanism_entity;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t source_phase;
	uint32_t destination_phase;
	uint32_t first_topology_edge;
	uint32_t topology_edge_count;
	sg_mechanism_capability_kind_t kind;
	sg_mechanism_state_t source_state;
	sg_mechanism_state_t destination_state;
	sg_mechanism_activation_t activation;
	sg_mechanism_recovery_t recovery;
	sg_rune_vec3_t entry_witness;
	sg_rune_vec3_t exit_witness;
	sg_rune_vec3_t observed_displacement;
	sg_rune_vec3_t observed_velocity;
	sg_rune_vec3_t mechanism_direction;
	sg_rune_vec3_t mechanism_origin;
	sg_rune_vec3_t mechanism_angles;
	sg_host_collision_transition_t inactive_transition;
	sg_host_collision_transition_t active_transition;
	sg_host_collision_transform_t inactive_mechanism_transform;
	sg_host_collision_transform_t active_mechanism_transform;
	sg_mech_execution_state_t source_execution;
	sg_mech_execution_state_t destination_execution;
	uint64_t mechanism_instance_id;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t travel_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
	uint64_t activation_time_ms;
	uint64_t active_time_ms;
	uint64_t exit_time_ms;
	uint64_t reset_time_ms;
	sg_mechanism_kernel_parameters_t parameters;
	sg_mechanism_capability_flags_t flags;
} sg_mechanism_capability_fact_t;

typedef struct sg_mechanism_topology_relation_s
{
	uint32_t controller_entity;
	uint32_t mechanism_entity;
	uint32_t first_edge;
	uint32_t edge_count;
} sg_mechanism_topology_relation_t;

typedef struct sg_mechanism_capability_set_s sg_mechanism_capability_set_t;
typedef struct sg_mechanism_capability_owner_s
	sg_mechanism_capability_owner_t;

/* Read-only content of an owner-issued capability result.  The handle remains
 * opaque: content identity describes the snapshot but is not issuance
 * authority and cannot be used to mint another accepted handle. */
typedef struct sg_mechanism_capability_view_s
{
	sg_rune_model_identity_t identity;
	uint64_t candidate_verifier_identity;
	uint64_t trace_verifier_identity;
	uint64_t content_identity;
	const sg_mechanism_capability_fact_t *facts;
	uint32_t fact_count;
	const uint32_t *topology_edges;
	uint32_t topology_edge_count;
	const sg_mechanism_topology_relation_t *topology_relations;
	uint32_t topology_relation_count;
	const uint32_t *mechanism_offsets;
	uint32_t mechanism_offset_count;
	const uint32_t *facts_by_trace;
	uint64_t topology_edge_visits;
} sg_mechanism_capability_view_t;

typedef struct sg_mechanism_capability_source_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *configuration_semantics;
	const sg_bsp_entity_semantics_t *entity_semantics;
	const sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	const sg_mechanism_host_trace_catalog_t *host_traces;
} sg_mechanism_capability_source_t;

typedef enum sg_mechanism_capability_audit_code_e
{
	SG_MECHANISM_CAPABILITY_AUDIT_OK = 0,
	SG_MECHANISM_CAPABILITY_AUDIT_INVALID_ARGUMENT,
	SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH,
	SG_MECHANISM_CAPABILITY_AUDIT_INVALID_INDEX,
	SG_MECHANISM_CAPABILITY_AUDIT_OMITTED_FACT,
	SG_MECHANISM_CAPABILITY_AUDIT_INVENTED_FACT,
	SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT,
	SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT,
	SG_MECHANISM_CAPABILITY_AUDIT_NONDETERMINISTIC_ORDER
} sg_mechanism_capability_audit_code_t;

typedef struct sg_mechanism_capability_audit_result_s
{
	sg_mechanism_capability_audit_code_t code;
	uint32_t record;
	uint32_t proved_facts;
	uint32_t omitted_facts;
	uint32_t invented_facts;
	uint64_t lookup_comparisons;
} sg_mechanism_capability_audit_result_t;

int SG_MechanismCapabilityOwnerCreate(
	sg_mechanism_capability_owner_t **owner_out);
void SG_MechanismCapabilityOwnerDestroy(
	sg_mechanism_capability_owner_t *owner);
int SG_MechanismCapabilityBuild(
	sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_source_t *source,
	sg_mechanism_capability_set_t **capabilities_out,
	sg_mechanism_capability_error_t *error_out);
int SG_MechanismCapabilityAudit(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_source_t *source,
	const sg_mechanism_capability_set_t *capabilities,
	sg_mechanism_capability_audit_result_t *result_out);
int SG_MechanismCapabilityRead(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_set_t *capabilities,
	const sg_mechanism_capability_view_t **view_out);
void SG_MechanismCapabilityDestroy(
	sg_mechanism_capability_owner_t *owner,
	sg_mechanism_capability_set_t *capabilities);
const char *SG_MechanismCapabilityErrorString(
	sg_mechanism_capability_error_code_t code);
const char *SG_MechanismCapabilityAuditCodeString(
	sg_mechanism_capability_audit_code_t code);

#endif
