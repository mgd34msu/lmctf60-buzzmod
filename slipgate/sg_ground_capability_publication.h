#ifndef SG_GROUND_CAPABILITY_PUBLICATION_H
#define SG_GROUND_CAPABILITY_PUBLICATION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_ground_capability.h"
#include "sg_host_law_publication.h"
#include "sg_phase_catalog.h"

typedef struct sg_ground_capability_publication_s
	sg_ground_capability_publication_t;
typedef struct sg_ground_capability_publication_owner_s
	sg_ground_capability_publication_owner_t;

typedef enum sg_ground_capability_completeness_e
{
	SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED = 0,
	SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE,
	SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY
} sg_ground_capability_completeness_t;

typedef enum sg_ground_capability_audit_code_e
{
	SG_GROUND_CAPABILITY_AUDIT_OK = 0,
	SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT,
	SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH,
	SG_GROUND_CAPABILITY_AUDIT_ENGINE_AUTHORITY_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_SEMANTICS_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_PHASE_BINDING_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_PHASE_TRANSITION_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED,
	SG_GROUND_CAPABILITY_AUDIT_INVALID_FACT,
	SG_GROUND_CAPABILITY_AUDIT_DUPLICATE_FACT,
	SG_GROUND_CAPABILITY_AUDIT_NONCANONICAL_ORDER,
	SG_GROUND_CAPABILITY_AUDIT_OMITTED_FACT,
	SG_GROUND_CAPABILITY_AUDIT_INVENTED_FACT,
	SG_GROUND_CAPABILITY_AUDIT_FACT_DISAGREEMENT,
	SG_GROUND_CAPABILITY_AUDIT_COMPLETENESS_DISAGREEMENT,
	SG_GROUND_CAPABILITY_AUDIT_SOURCE_MUTATED,
	SG_GROUND_CAPABILITY_AUDIT_OVERFLOW,
	SG_GROUND_CAPABILITY_AUDIT_OUT_OF_MEMORY
} sg_ground_capability_audit_code_t;

/* The two opaque publications authenticate collision, Pmove, phases, and
 * transitions. Configuration and semantics remain the audited construction
 * products that those publications bind by exact model identity. */
typedef struct sg_ground_capability_publication_source_s
{
	const sg_host_law_publication_t *engine_authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_phase_catalog_publication_owner_t *phase_catalog_owner;
	const sg_phase_catalog_publication_t *phase_catalog;
} sg_ground_capability_publication_source_t;

typedef struct sg_ground_capability_audit_result_s
{
	sg_ground_capability_audit_code_t code;
	sg_ground_capability_completeness_t completeness;
	uint32_t record;
	uint32_t expected_facts;
	uint32_t candidate_facts;
	uint32_t matched_facts;
	uint32_t omitted_facts;
	uint32_t invented_facts;
	uint32_t duplicate_facts;
	uint32_t expected_by_kind[SG_GROUND_CAPABILITY_KIND_COUNT];
	uint32_t candidate_by_kind[SG_GROUND_CAPABILITY_KIND_COUNT];
	uint32_t phase_count;
	uint32_t binding_count;
	uint32_t phase_transition_count;
	uint32_t proved_portals;
	uint32_t proven_empty_portals;
	uint32_t proved_directions;
	uint32_t proven_empty_directions;
	uint64_t host_pmove_frames;
} sg_ground_capability_audit_result_t;

typedef struct sg_ground_capability_publication_fact_s
{
	uint32_t order;
	sg_rune_cell_ref_t source_cell;
	sg_rune_cell_ref_t destination_cell;
	uint64_t source_region_id;
	uint64_t destination_region_id;
	sg_rune_portal_ref_t portal;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_ground_capability_kind_t kind;
	sg_rune_vec3_t source_witness;
	sg_rune_vec3_t destination_witness;
	sg_rune_vec3_t initial_velocity;
	sg_rune_vec3_t observed_velocity;
	sg_rune_interval3_t displacement;
	sg_rune_interval_t duration_ms;
	float acceleration;
	float gravity;
	uint64_t physics_abi_id;
	sg_ground_capability_flags_t flags;
} sg_ground_capability_publication_fact_t;

typedef struct sg_ground_capability_publication_description_s
{
	sg_rune_model_identity_t identity;
	uint32_t engine_authority_version;
	uint32_t reserved;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	sg_host_engine_pmove_abi_t pmove_abi;
	uint64_t pmove_behavior_fingerprint;
	sg_ground_capability_completeness_t completeness;
	uint32_t cell_count;
	uint32_t portal_count;
	uint32_t semantic_region_count;
	uint32_t phase_count;
	uint32_t binding_count;
	uint32_t phase_transition_count;
	uint32_t fact_count;
	uint32_t fact_count_by_kind[SG_GROUND_CAPABILITY_KIND_COUNT];
	uint32_t proved_portals;
	uint32_t proven_empty_portals;
	uint32_t proved_directions;
	uint32_t proven_empty_directions;
	uint64_t host_pmove_frames;
} sg_ground_capability_publication_description_t;

int SG_GroundCapabilityFactBitsEqual(
	const sg_ground_capability_t *left,
	const sg_ground_capability_t *right);

#ifdef SG_GROUND_CAPABILITY_TESTING
int SG_GroundCapabilityPublicationTestCandidateStorageBytes(size_t count,
	size_t *bytes_out);
#endif

int SG_GroundCapabilityPublicationOwnerCreate(
	sg_ground_capability_publication_owner_t **owner_out);
void SG_GroundCapabilityPublicationOwnerDestroy(
	sg_ground_capability_publication_owner_t *owner);

int SG_GroundCapabilityAudit(
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_audit_result_t *result_out);

int SG_GroundCapabilityPublicationIssue(
	sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_source_t *source,
	const sg_ground_capability_set_t *candidate,
	sg_ground_capability_publication_t **publication_out,
	sg_ground_capability_audit_result_t *audit_out);

/* Offline construction callers use this entry point. It derives a candidate
 * from the accepted sources and then issues only after a second reconstruction
 * agrees exactly. */
int SG_GroundCapabilityPublicationBuild(
	sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_source_t *source,
	sg_ground_capability_publication_t **publication_out,
	sg_ground_capability_audit_result_t *audit_out);

int SG_GroundCapabilityPublicationDescribe(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication,
	sg_ground_capability_publication_description_t *description_out);
int SG_GroundCapabilityPublicationFact(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication, uint32_t index,
	sg_ground_capability_publication_fact_t *fact_out);
int SG_GroundCapabilityPublicationStorageOverlaps(
	const sg_ground_capability_publication_owner_t *owner,
	const sg_ground_capability_publication_t *publication,
	const void *address, size_t size);
void SG_GroundCapabilityPublicationDestroy(
	sg_ground_capability_publication_owner_t *owner,
	sg_ground_capability_publication_t *publication);

const char *SG_GroundCapabilityAuditCodeString(
	sg_ground_capability_audit_code_t code);

#endif /* SG_GROUND_CAPABILITY_PUBLICATION_H */
