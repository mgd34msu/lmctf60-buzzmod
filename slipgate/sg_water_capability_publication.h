#ifndef SG_WATER_CAPABILITY_PUBLICATION_H
#define SG_WATER_CAPABILITY_PUBLICATION_H

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"
#include "sg_host_law_publication.h"
#include "sg_phase_catalog.h"
#include "sg_water_capability.h"

typedef struct sg_water_capability_publication_s
	sg_water_capability_publication_t;
typedef struct sg_water_capability_publication_owner_s
	sg_water_capability_publication_owner_t;

typedef enum sg_water_capability_publication_state_e
{
	SG_WATER_CAPABILITY_PUBLICATION_COMPLETE = 0,
	SG_WATER_CAPABILITY_PUBLICATION_PROVEN_EMPTY
} sg_water_capability_publication_state_t;

typedef enum sg_water_capability_audit_code_e
{
	SG_WATER_CAPABILITY_AUDIT_OK = 0,
	SG_WATER_CAPABILITY_AUDIT_INVALID_ARGUMENT,
	SG_WATER_CAPABILITY_AUDIT_SOURCE_IDENTITY,
	SG_WATER_CAPABILITY_AUDIT_HOST_LAW_IDENTITY,
	SG_WATER_CAPABILITY_AUDIT_INVALID_PHASE,
	SG_WATER_CAPABILITY_AUDIT_OMITTED_BINDING,
	SG_WATER_CAPABILITY_AUDIT_INVENTED_BINDING,
	SG_WATER_CAPABILITY_AUDIT_DUPLICATE_BINDING,
	SG_WATER_CAPABILITY_AUDIT_INVALID_CANDIDATE,
	SG_WATER_CAPABILITY_AUDIT_NONCANONICAL_ORDER,
	SG_WATER_CAPABILITY_AUDIT_DUPLICATE_FACT,
	SG_WATER_CAPABILITY_AUDIT_OMITTED_FACT,
	SG_WATER_CAPABILITY_AUDIT_INVENTED_FACT,
	SG_WATER_CAPABILITY_AUDIT_FACT_DISAGREEMENT,
	SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT,
	SG_WATER_CAPABILITY_AUDIT_UNRESOLVED_DESTINATION,
	SG_WATER_CAPABILITY_AUDIT_AMBIGUOUS_DESTINATION,
	SG_WATER_CAPABILITY_AUDIT_DOMAIN_COVERAGE,
	SG_WATER_CAPABILITY_AUDIT_COUNTER_DISAGREEMENT,
	SG_WATER_CAPABILITY_AUDIT_OVERFLOW,
	SG_WATER_CAPABILITY_AUDIT_OUT_OF_MEMORY,
	SG_WATER_CAPABILITY_AUDIT_HOST_LAW,
	SG_WATER_CAPABILITY_AUDIT_BSP_COMPLETENESS,
	SG_WATER_CAPABILITY_AUDIT_CONFIGURATION_AUDIT,
	SG_WATER_CAPABILITY_AUDIT_SEMANTICS_AUDIT,
	SG_WATER_CAPABILITY_AUDIT_PHASE_CATALOG,
	SG_WATER_CAPABILITY_AUDIT_UNBOUND_PHASE,
	SG_WATER_CAPABILITY_AUDIT_UNSTABLE_REFERENCE,
	SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT
} sg_water_capability_audit_code_t;

typedef struct sg_water_capability_audit_result_s
{
	sg_water_capability_audit_code_t code;
	uint32_t source_record;
	uint32_t candidate_record;
	uint32_t obligation_count;
	uint32_t proved_fact_count;
	uint32_t proved_empty_count;
	uint32_t wet_region_count;
	uint32_t boundary_count;
	uint64_t host_pmove_frames;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint64_t same_cell_candidate_pairs;
	uint32_t lattice_maximum_binary_shift;
	sg_bsp_completeness_result_t bsp_completeness;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_audit_result_t semantics_audit;
} sg_water_capability_audit_result_t;

typedef struct sg_water_capability_issue_source_s
{
	/* The opaque construction owns the accepted BSP parse, collision, and
	 * arbitrary offline Pmove. No authority, callback, or BSP storage crosses
	 * this boundary. */
	const sg_host_law_construction_t *construction;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	/* Only an immutable, complete phase-catalog publication can bind phases. */
	const sg_phase_catalog_publication_owner_t *phase_catalog_owner;
	const sg_phase_catalog_publication_t *phase_catalog;
} sg_water_capability_issue_source_t;

/* These snapshot references never expose transient source-array indexes. */
typedef struct sg_water_capability_publication_binding_s
{
	uint64_t semantic_region_id;
	sg_rune_cell_ref_t cell;
	sg_rune_phase_ref_t phase;
	sg_phase_mechanism_state_mask_t mechanism_state_mask;
} sg_water_capability_publication_binding_t;

typedef struct sg_water_capability_publication_fact_s
{
	uint32_t order;
	sg_rune_cell_ref_t source_cell;
	sg_rune_cell_ref_t destination_cell;
	uint64_t source_semantic_region_id;
	uint64_t destination_semantic_region_id;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_rune_phase_transition_ref_t phase_transition;
	sg_rune_portal_ref_t portal;
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
	/* Exact host-law classifications, including current bits and support
	 * ground contents, make water and ground physics independently auditable. */
	sg_host_collision_contents_t source_watertype;
	sg_host_collision_contents_t destination_watertype;
	sg_host_collision_contents_t source_groundcontents;
	sg_host_collision_contents_t destination_groundcontents;
	/* Current fields retain both contributing engine classifications.  The
	 * combined fields are their exact bitwise unions. */
	sg_host_collision_contents_t source_water_current;
	sg_host_collision_contents_t source_ground_current;
	sg_host_collision_contents_t source_current;
	sg_host_collision_contents_t destination_water_current;
	sg_host_collision_contents_t destination_ground_current;
	sg_host_collision_contents_t destination_current;
	sg_host_collision_contents_t result_watertype;
	sg_host_collision_contents_t result_groundcontents;
	sg_host_collision_contents_t result_water_current;
	sg_host_collision_contents_t result_ground_current;
	sg_host_collision_contents_t result_current;
	uint32_t source_environment;
	uint32_t destination_environment;
	uint32_t result_environment;
	sg_rune_vec3_t source_witness;
	sg_rune_vec3_t boundary_witness;
	sg_rune_vec3_t destination_witness;
	sg_rune_vec3_t direction_vector;
	sg_rune_vec3_t command_vector;
	sg_rune_vec3_t source_velocity;
	sg_rune_vec3_t observed_displacement;
	sg_rune_vec3_t observed_velocity;
	uint32_t result_pm_flags;
	uint32_t result_support_model_index;
	uint64_t result_support_instance_id;
	sg_host_collision_contents_t result_water_type;
	uint8_t result_grounded;
	uint8_t result_water_level;
	uint8_t result_reserved[2];
	sg_rune_kernel_parameters_t parameters;
	sg_water_capability_flags_t flags;
} sg_water_capability_publication_fact_t;

typedef struct sg_water_capability_publication_info_s
{
	sg_water_capability_publication_state_t state;
	sg_rune_model_identity_t identity;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	uint64_t host_law_identity;
	uint32_t phase_count;
	uint32_t binding_count;
	uint32_t transition_count;
	uint32_t wet_region_count;
	uint32_t boundary_count;
	uint32_t obligation_count;
	uint32_t fact_count;
	uint32_t proved_empty_count;
	uint64_t host_pmove_frames;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint64_t same_cell_candidate_pairs;
	uint32_t lattice_maximum_binary_shift;
	sg_bsp_completeness_result_t bsp_completeness;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_audit_result_t semantics_audit;
} sg_water_capability_publication_info_t;

typedef uint32_t sg_water_environment_flags_t;
enum
{
	SG_WATER_ENVIRONMENT_BREATH_LIMITED = UINT32_C(1) << 0,
	SG_WATER_ENVIRONMENT_HAZARDOUS = UINT32_C(1) << 1,
	SG_WATER_ENVIRONMENT_WATER_CURRENT = UINT32_C(1) << 2,
	SG_WATER_ENVIRONMENT_GROUND_CURRENT = UINT32_C(1) << 3
};

int SG_WaterCapabilityPublicationOwnerCreate(
	sg_water_capability_publication_owner_t **owner_out);
void SG_WaterCapabilityPublicationOwnerDestroy(
	sg_water_capability_publication_owner_t *owner);

int SG_WaterCapabilityPublicationIssue(
	sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_issue_source_t *source,
	const sg_water_capability_set_t *candidate,
	sg_water_capability_publication_t **publication_out,
	sg_water_capability_audit_result_t *audit_out);
int SG_WaterCapabilityPublicationInfo(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication,
	sg_water_capability_publication_info_t *info_out);
int SG_WaterCapabilityPublicationPhase(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_rune_phase_basis_t *phase_out);
int SG_WaterCapabilityPublicationBinding(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_water_capability_publication_binding_t *binding_out);
int SG_WaterCapabilityPublicationTransition(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_rune_phase_transition_t *transition_out);
int SG_WaterCapabilityPublicationTransitionEvidence(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_phase_catalog_transition_evidence_t *evidence_out);
int SG_WaterCapabilityPublicationFact(
	const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication, uint32_t index,
	sg_water_capability_publication_fact_t *fact_out);
void SG_WaterCapabilityPublicationDestroy(
	sg_water_capability_publication_owner_t *owner,
	sg_water_capability_publication_t *publication);
const char *SG_WaterCapabilityAuditCodeString(
	sg_water_capability_audit_code_t code);

#ifdef SG_WATER_CAPABILITY_PUBLICATION_TESTING
int SG_WaterCapabilityPublicationTestCounterAdd(uint32_t *value,
	uint32_t amount, sg_water_capability_audit_result_t *audit_out);
#endif

#endif
