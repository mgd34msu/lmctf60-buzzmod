#ifndef SG_RUNE_DYNAMICS_MODEL_H
#define SG_RUNE_DYNAMICS_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "sg_destination.h"

#define SG_RUNE_DYNAMICS_MODEL_VERSION UINT16_C(1)
#define SG_RUNE_STATE_DIMENSION_COUNT UINT8_C(7)
#define SG_RUNE_FIELD_COST_INFINITE UINT64_MAX
#define SG_RUNE_FIELD_NO_REGION UINT32_MAX

typedef struct sg_rune_dynamics_model_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_dynamics_model_id_t;

typedef struct sg_rune_state_vertex_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_state_vertex_id_t;
typedef sg_rune_state_vertex_id_t sg_rune_state_vertex_ref_t;

typedef struct sg_rune_state_simplex_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_state_simplex_id_t;
typedef sg_rune_state_simplex_id_t sg_rune_state_simplex_ref_t;

typedef struct sg_rune_control_fiber_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_control_fiber_id_t;
typedef sg_rune_control_fiber_id_t sg_rune_control_fiber_ref_t;

typedef struct sg_rune_response_patch_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_response_patch_id_t;
typedef sg_rune_response_patch_id_t sg_rune_response_patch_ref_t;

typedef struct sg_rune_boundary_transfer_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_boundary_transfer_id_t;
typedef sg_rune_boundary_transfer_id_t sg_rune_boundary_transfer_ref_t;

typedef struct sg_rune_control_domain_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_control_domain_ref_t;

typedef struct sg_rune_guard_condition_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_guard_condition_ref_t;

typedef struct sg_rune_dynamics_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_dynamics_proof_ref_t;

typedef struct sg_rune_field_region_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_field_region_id_t;

typedef struct sg_rune_field_hierarchy_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_field_hierarchy_id_t;

typedef struct sg_rune_field_error_contract_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_field_error_contract_id_t;

typedef struct sg_rune_state_vertex_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_vertex_span_t;

typedef struct sg_rune_state_chart_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_chart_span_t;

typedef struct sg_rune_state_simplex_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_simplex_span_t;

typedef struct sg_rune_state_domain_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_domain_span_t;

typedef struct sg_rune_control_fiber_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_control_fiber_span_t;

typedef struct sg_rune_response_patch_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_response_patch_span_t;

typedef struct sg_rune_boundary_transfer_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_boundary_transfer_span_t;

typedef struct sg_rune_field_region_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_field_region_span_t;

typedef enum sg_rune_state_mode_kind_e
{
	SG_RUNE_STATE_MODE_SUPPORTED = 0,
	SG_RUNE_STATE_MODE_WATER,
	SG_RUNE_STATE_MODE_AIRBORNE,
	SG_RUNE_STATE_MODE_HOOK_BOLT,
	SG_RUNE_STATE_MODE_HOOK_PULL,
	SG_RUNE_STATE_MODE_HOOK_COAST,
	SG_RUNE_STATE_MODE_MOVER_RELATIVE,
	SG_RUNE_STATE_MODE_KIND_COUNT
} sg_rune_state_mode_kind_t;

typedef struct sg_rune_supported_mode_s
{
	sg_rune_surface_ref_t support_surface;
} sg_rune_supported_mode_t;

typedef struct sg_rune_water_mode_s
{
	sg_rune_medium_t medium;
	sg_rune_contents_mask_t contents;
} sg_rune_water_mode_t;

typedef struct sg_rune_airborne_mode_s
{
	sg_rune_void_relation_t void_relation;
} sg_rune_airborne_mode_t;

typedef struct sg_rune_hook_bolt_mode_s
{
	sg_rune_affordance_ref_t visibility_relation;
} sg_rune_hook_bolt_mode_t;

typedef struct sg_rune_hook_pull_mode_s
{
	sg_rune_surface_ref_t anchor_surface;
} sg_rune_hook_pull_mode_t;

typedef struct sg_rune_hook_coast_mode_s
{
	sg_rune_void_relation_t void_relation;
} sg_rune_hook_coast_mode_t;

typedef struct sg_rune_mover_relative_mode_s
{
	sg_rune_mechanism_ref_t mover;
} sg_rune_mover_relative_mode_t;

typedef struct sg_rune_state_mode_s
{
	sg_rune_state_mode_kind_t kind;
	union
	{
		sg_rune_supported_mode_t supported;
		sg_rune_water_mode_t water;
		sg_rune_airborne_mode_t airborne;
		sg_rune_hook_bolt_mode_t hook_bolt;
		sg_rune_hook_pull_mode_t hook_pull;
		sg_rune_hook_coast_mode_t hook_coast;
		sg_rune_mover_relative_mode_t mover_relative;
	} value;
} sg_rune_state_mode_t;

typedef struct sg_rune_state_embedding_s
{
	sg_rune_interval3_t position;
	sg_rune_interval3_t velocity;
	sg_rune_interval_t elapsed_ms;
	uint8_t dimension_count;
	uint8_t reserved[3];
} sg_rune_state_embedding_t;

typedef struct sg_rune_state_vertex_s
{
	sg_rune_state_vertex_id_t id;
	sg_rune_state_chart_ref_t chart;
	sg_rune_vec3_t position;
	sg_rune_vec3_t velocity;
	float elapsed_ms;
} sg_rune_state_vertex_t;

typedef struct sg_rune_state_simplex_s
{
	sg_rune_state_simplex_id_t id;
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_vertex_span_t vertices;
} sg_rune_state_simplex_t;

typedef struct sg_rune_state_domain_s
{
	sg_rune_state_domain_id_t id;
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_simplex_span_t simplices;
} sg_rune_state_domain_t;

typedef struct sg_rune_state_chart_s
{
	sg_rune_state_chart_id_t id;
	sg_rune_cell_ref_t configuration_cell;
	sg_rune_state_mode_t mode;
	sg_rune_state_embedding_t embedding;
	sg_rune_state_vertex_span_t state_vertices;
	sg_rune_state_simplex_span_t simplices;
	sg_rune_state_domain_span_t state_domains;
	sg_rune_control_fiber_span_t control_fibers;
	sg_rune_response_patch_span_t response_patches;
	sg_rune_boundary_transfer_span_t boundary_transfers;
	sg_rune_dynamics_proof_ref_t coverage_proof;
} sg_rune_state_chart_t;

typedef struct sg_rune_control_fiber_s
{
	sg_rune_control_fiber_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_control_domain_ref_t domain;
	sg_rune_guard_condition_ref_t condition;
	sg_rune_dynamics_proof_ref_t coverage_proof;
} sg_rune_control_fiber_t;

typedef struct sg_rune_flow_enclosure_s
{
	sg_rune_interval3_t position;
	sg_rune_interval3_t velocity;
	sg_rune_interval_t elapsed_ms;
} sg_rune_flow_enclosure_t;

typedef struct sg_rune_cost_bounds_s
{
	uint64_t lower_us;
	uint64_t upper_us;
} sg_rune_cost_bounds_t;

typedef struct sg_rune_response_patch_s
{
	sg_rune_response_patch_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_state_simplex_ref_t source_simplex;
	sg_rune_control_fiber_span_t controls;
	sg_rune_flow_enclosure_t flow;
	sg_rune_cost_bounds_t running_cost;
	sg_rune_state_domain_span_t destination_domains;
	sg_rune_dynamics_proof_ref_t flow_proof;
} sg_rune_response_patch_t;

typedef struct sg_rune_boundary_transfer_s
{
	sg_rune_boundary_transfer_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_state_domain_ref_t source_domain;
	sg_rune_guard_condition_ref_t condition;
	sg_rune_state_chart_ref_t destination_chart;
	sg_rune_state_domain_ref_t destination_domain;
	sg_rune_flow_enclosure_t reset_enclosure;
	sg_rune_dynamics_proof_ref_t transfer_proof;
} sg_rune_boundary_transfer_t;

typedef struct sg_rune_field_region_s
{
	sg_rune_field_region_id_t id;
	uint32_t parent_region;
	uint32_t level;
	sg_rune_field_region_span_t children;
	sg_rune_state_chart_span_t charts;
	sg_rune_state_domain_span_t state_domains;
	sg_rune_response_patch_span_t response_patches;
	sg_rune_dynamics_proof_ref_t coverage_proof;
} sg_rune_field_region_t;

typedef struct sg_rune_field_region_hierarchy_s
{
	sg_rune_field_hierarchy_id_t id;
	const sg_rune_field_region_t *regions;
	size_t region_count;
	const uint32_t *children;
	size_t child_count;
	const uint32_t *chart_leaf_regions;
	const uint32_t *state_domain_leaf_regions;
	const uint32_t *response_patch_leaf_regions;
	size_t chart_count;
	size_t state_domain_count;
	size_t response_patch_count;
	sg_rune_dynamics_proof_ref_t hierarchy_proof;
} sg_rune_field_region_hierarchy_t;

typedef struct sg_rune_field_error_contract_s
{
	sg_rune_field_error_contract_id_t id;
	uint64_t cost_quantum_us;
	uint64_t maximum_value_width_us;
	uint64_t maximum_bellman_residual_us;
	sg_rune_interval3_t position_error;
	sg_rune_interval3_t velocity_error;
	sg_rune_interval_t time_error;
} sg_rune_field_error_contract_t;

typedef struct sg_rune_dynamics_model_s
{
	uint16_t version;
	uint16_t reserved;
	sg_rune_dynamics_model_id_t id;
	uint64_t rune_identity;
	uint64_t topology_revision;
	const sg_rune_state_vertex_t *state_vertices;
	size_t state_vertex_count;
	const sg_rune_state_chart_t *state_charts;
	size_t state_chart_count;
	const sg_rune_state_simplex_t *state_simplices;
	size_t state_simplex_count;
	const sg_rune_state_domain_t *state_domains;
	size_t state_domain_count;
	const sg_rune_control_fiber_t *control_fibers;
	size_t control_fiber_count;
	const sg_rune_response_patch_t *response_patches;
	size_t response_patch_count;
	const sg_rune_boundary_transfer_t *boundary_transfers;
	size_t boundary_transfer_count;
	sg_rune_field_region_hierarchy_t hierarchy;
	sg_rune_field_error_contract_t error_contract;
} sg_rune_dynamics_model_t;

typedef struct sg_field_service_s sg_field_service_t;

typedef struct sg_field_handle_s
{
	uint64_t service_identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t terminal_generation;
	uint64_t field_generation;
} sg_field_handle_t;

typedef struct sg_localized_field_state_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t pose_revision;
	uint64_t sampled_at_ms;
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_mode_t mode;
	sg_rune_vec3_t position;
	sg_rune_vec3_t velocity;
	float elapsed_ms;
} sg_localized_field_state_t;

typedef struct sg_field_environment_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t environment_revision;
	uint64_t sampled_at_ms;
	uint8_t authenticated;
	uint8_t reserved[7];
} sg_field_environment_t;

typedef struct sg_rune_field_descent_s
{
	sg_rune_control_fiber_ref_t control;
	uint64_t minimum_descent_us;
	sg_rune_cost_bounds_t endpoint_cost;
} sg_rune_field_descent_t;

typedef struct sg_rune_field_descent_span_s
{
	const sg_rune_field_descent_t *values;
	size_t count;
} sg_rune_field_descent_span_t;

typedef enum sg_field_guidance_kind_e
{
	SG_FIELD_GUIDANCE_TERMINAL = 0,
	SG_FIELD_GUIDANCE_DESCENT,
	SG_FIELD_GUIDANCE_UNREACHABLE,
	SG_FIELD_GUIDANCE_KIND_COUNT
} sg_field_guidance_kind_t;

typedef struct sg_field_terminal_guidance_s
{
	sg_rune_cost_bounds_t arrival_cost;
	uint64_t residual_bound_us;
} sg_field_terminal_guidance_t;

typedef struct sg_field_descent_guidance_s
{
	sg_rune_cost_bounds_t arrival_cost;
	uint64_t residual_bound_us;
	sg_rune_interval3_t spatial_subgradient;
	sg_rune_interval3_t velocity_subgradient;
	sg_rune_interval_t time_subgradient;
	sg_rune_field_descent_span_t controls;
} sg_field_descent_guidance_t;

typedef struct sg_field_unreachable_guidance_s
{
	sg_rune_cost_bounds_t arrival_cost;
} sg_field_unreachable_guidance_t;

typedef struct sg_field_guidance_s
{
	sg_field_handle_t field;
	uint64_t pose_revision;
	uint64_t sampled_at_ms;
	sg_field_guidance_kind_t kind;
	union
	{
		sg_field_terminal_guidance_t terminal;
		sg_field_descent_guidance_t descent;
		sg_field_unreachable_guidance_t unreachable;
	} value;
} sg_field_guidance_t;

typedef enum sg_field_status_e
{
	SG_FIELD_STATUS_OK = 0,
	SG_FIELD_STATUS_INVALID_ARGUMENT,
	SG_FIELD_STATUS_INVALID_MODEL,
	SG_FIELD_STATUS_IDENTITY_MISMATCH,
	SG_FIELD_STATUS_STALE,
	SG_FIELD_STATUS_UNREACHABLE,
	SG_FIELD_STATUS_PROOF_FAILED,
	SG_FIELD_STATUS_STORAGE,
	SG_FIELD_STATUS_COUNT
} sg_field_status_t;

int SG_RuneDynamicsModelIdValid(const sg_rune_dynamics_model_id_t *id);
int SG_RuneStateVertexIdValid(const sg_rune_state_vertex_id_t *id);
int SG_RuneStateChartIdValid(const sg_rune_state_chart_id_t *id);
int SG_RuneStateSimplexIdValid(const sg_rune_state_simplex_id_t *id);
int SG_RuneStateDomainIdValid(const sg_rune_state_domain_id_t *id);
int SG_RuneControlFiberIdValid(const sg_rune_control_fiber_id_t *id);
int SG_RuneResponsePatchIdValid(const sg_rune_response_patch_id_t *id);
int SG_RuneBoundaryTransferIdValid(
	const sg_rune_boundary_transfer_id_t *id);
int SG_RuneControlDomainRefValid(const sg_rune_control_domain_ref_t *ref);
int SG_RuneGuardConditionRefValid(const sg_rune_guard_condition_ref_t *ref);
int SG_RuneDynamicsProofRefValid(const sg_rune_dynamics_proof_ref_t *ref);
int SG_RuneFieldRegionIdValid(const sg_rune_field_region_id_t *id);
int SG_RuneFieldHierarchyIdValid(const sg_rune_field_hierarchy_id_t *id);
int SG_RuneFieldErrorContractIdValid(
	const sg_rune_field_error_contract_id_t *id);
int SG_RuneStateModeValid(const sg_rune_state_mode_t *mode);
int SG_RuneStateVertexShapeValid(const sg_rune_state_vertex_t *vertex);
int SG_RuneStateSimplexShapeValid(const sg_rune_state_simplex_t *simplex);
int SG_RuneStateDomainShapeValid(const sg_rune_state_domain_t *domain);
int SG_RuneStateChartShapeValid(const sg_rune_state_chart_t *chart);
int SG_RuneControlFiberShapeValid(const sg_rune_control_fiber_t *fiber);
int SG_RuneResponsePatchShapeValid(const sg_rune_response_patch_t *patch);
int SG_RuneBoundaryTransferShapeValid(
	const sg_rune_boundary_transfer_t *transfer);
int SG_RuneFieldRegionShapeValid(const sg_rune_field_region_t *region);
int SG_RuneFieldRegionHierarchyValid(
	const sg_rune_field_region_hierarchy_t *hierarchy);
int SG_RuneFieldErrorContractValid(
	const sg_rune_field_error_contract_t *contract);
int SG_RuneDynamicsModelValid(const sg_rune_dynamics_model_t *model,
	const sg_rune_runtime_snapshot_t *snapshot);
int SG_LocalizedFieldStateValid(const sg_localized_field_state_t *state);
int SG_FieldEnvironmentValid(const sg_field_environment_t *environment);
int SG_FieldHandleValid(const sg_field_handle_t *handle);
int SG_FieldGuidanceValid(const sg_field_guidance_t *guidance);

sg_field_status_t SG_FieldServiceCreate(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_dynamics_model_t *dynamics_model,
	sg_field_service_t **service_out);
void SG_FieldServiceDestroy(sg_field_service_t *service);
sg_field_status_t SG_FieldServiceResolve(sg_field_service_t *service,
	const sg_destination_terminal_t *terminal, uint64_t now_ms,
	sg_field_handle_t *handle_out);
sg_field_status_t SG_FieldServiceRefresh(sg_field_service_t *service,
	const sg_field_handle_t *previous,
	const sg_destination_terminal_t *terminal, uint64_t now_ms,
	sg_field_handle_t *handle_out);
sg_field_status_t SG_FieldServiceQuery(const sg_field_service_t *service,
	const sg_field_handle_t *handle,
	const sg_localized_field_state_t *state,
	const sg_field_environment_t *environment,
	sg_field_guidance_t *guidance_out);

#endif
