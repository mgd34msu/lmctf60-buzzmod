/* Immutable publication of static map forces that affect player motion. */
#ifndef SG_EXTERNAL_FORCE_PUBLICATION_H
#define SG_EXTERNAL_FORCE_PUBLICATION_H

#include <stdint.h>

#include "sg_bsp_entity_semantics_publication.h"
#include "sg_host_law_publication.h"
#include "sg_phase_catalog.h"

typedef struct sg_external_force_publication_s
	sg_external_force_publication_t;

typedef enum sg_external_force_kind_e
{
	SG_EXTERNAL_FORCE_TRIGGER_PUSH = 0,
	SG_EXTERNAL_FORCE_WATER_CURRENT,
	SG_EXTERNAL_FORCE_CONVEYOR_CURRENT,
	SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT,
	SG_EXTERNAL_FORCE_GRAVITY,
	SG_EXTERNAL_FORCE_KIND_COUNT
} sg_external_force_kind_t;

typedef uint32_t sg_external_force_flags_t;
enum
{
	SG_EXTERNAL_FORCE_ONE_SHOT = UINT32_C(1) << 0,
	SG_EXTERNAL_FORCE_CONDITIONAL = UINT32_C(1) << 1,
	SG_EXTERNAL_FORCE_MOVER_RELATIVE = UINT32_C(1) << 2,
	SG_EXTERNAL_FORCE_HOST_PROVEN = UINT32_C(1) << 3,
	/* The spatial obligation is complete, but its state-dependent Pmove
	 * observation has not yet been authenticated. */
	SG_EXTERNAL_FORCE_LAW_UNRESOLVED = UINT32_C(1) << 4
};

#define SG_EXTERNAL_FORCE_FLAGS_KNOWN \
	(SG_EXTERNAL_FORCE_ONE_SHOT | SG_EXTERNAL_FORCE_CONDITIONAL | \
	 SG_EXTERNAL_FORCE_MOVER_RELATIVE | SG_EXTERNAL_FORCE_HOST_PROVEN | \
	 SG_EXTERNAL_FORCE_LAW_UNRESOLVED)

typedef struct sg_external_force_fact_s
{
	sg_external_force_kind_t kind;
	uint32_t source_entity_ordinal;
	uint32_t mechanism_entity_index;
	sg_rune_mechanism_ref_t mechanism;
	sg_rune_cell_ref_t source_cell;
	sg_rune_cell_ref_t destination_cell;
	uint64_t source_region_id;
	uint64_t destination_region_id;
	uint32_t source_model_index;
	uint32_t source_leaf_index;
	sg_host_collision_contents_t source_contents;
	sg_rune_vec3_t source_witness;
	sg_rune_vec3_t source_model_origin;
	sg_rune_vec3_t source_model_angles;
	sg_rune_portal_ref_t portal;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_rune_interval3_t displacement;
	sg_rune_vec3_t velocity;
	sg_rune_vec3_t acceleration;
	float gravity;
	uint32_t delay_ms;
	uint32_t duration_ms;
	uint32_t dwell_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
	uint64_t physics_abi_id;
	sg_external_force_flags_t flags;
} sg_external_force_fact_t;

typedef enum sg_external_force_completeness_e
{
	SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED = 0,
	SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE,
	SG_EXTERNAL_FORCE_COMPLETENESS_PROVEN_EMPTY
} sg_external_force_completeness_t;

typedef struct sg_external_force_source_s
{
	const sg_host_collision_authority_t *collision_authority;
	const sg_host_law_publication_t *engine_authority;
	const sg_bsp_entity_semantics_publication_t *entity_semantics;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *configuration_semantics;
	const sg_mechanism_capability_owner_t *mechanism_owner;
	const sg_mechanism_capability_set_t *mechanisms;
	const sg_phase_catalog_publication_owner_t *phase_owner;
	const sg_phase_catalog_publication_t *phases;
} sg_external_force_source_t;

typedef enum sg_external_force_audit_code_e
{
	SG_EXTERNAL_FORCE_AUDIT_OK = 0,
	SG_EXTERNAL_FORCE_AUDIT_INVALID_ARGUMENT,
	SG_EXTERNAL_FORCE_AUDIT_AUTHORITY_REJECTED,
	SG_EXTERNAL_FORCE_AUDIT_IDENTITY_MISMATCH,
	SG_EXTERNAL_FORCE_AUDIT_CONFIGURATION_REJECTED,
	SG_EXTERNAL_FORCE_AUDIT_ENTITY_REJECTED,
	SG_EXTERNAL_FORCE_AUDIT_MECHANISM_REJECTED,
	SG_EXTERNAL_FORCE_AUDIT_PHASE_REJECTED,
	SG_EXTERNAL_FORCE_AUDIT_INVALID_FACT,
	SG_EXTERNAL_FORCE_AUDIT_OMITTED_FACT,
	SG_EXTERNAL_FORCE_AUDIT_INVENTED_FACT,
	SG_EXTERNAL_FORCE_AUDIT_DUPLICATE_FACT,
	SG_EXTERNAL_FORCE_AUDIT_FACT_DISAGREEMENT,
	SG_EXTERNAL_FORCE_AUDIT_NONDETERMINISTIC_ORDER,
	SG_EXTERNAL_FORCE_AUDIT_STORAGE_DISAGREEMENT,
	SG_EXTERNAL_FORCE_AUDIT_METADATA_DISAGREEMENT,
	SG_EXTERNAL_FORCE_AUDIT_COMPLETENESS_DISAGREEMENT,
	SG_EXTERNAL_FORCE_AUDIT_OVERFLOW,
	SG_EXTERNAL_FORCE_AUDIT_OUT_OF_MEMORY
} sg_external_force_audit_code_t;

typedef struct sg_external_force_audit_result_s
{
	sg_external_force_audit_code_t code;
	sg_external_force_completeness_t completeness;
	uint32_t record;
	uint32_t expected_facts;
	uint32_t observed_facts;
	uint32_t facts_by_kind[SG_EXTERNAL_FORCE_KIND_COUNT];
	sg_external_force_completeness_t
		completeness_by_kind[SG_EXTERNAL_FORCE_KIND_COUNT];
} sg_external_force_audit_result_t;

typedef struct sg_external_force_view_s
{
	sg_rune_model_identity_t identity;
	sg_bsp_entity_semantics_binding_t entity_binding;
	uint64_t mechanism_content_identity;
	uint64_t phase_verifier_identity;
	uint64_t pmove_behavior_fingerprint;
	uint64_t content_identity;
	sg_external_force_completeness_t completeness;
	uint32_t fact_count;
	uint32_t fact_count_by_kind[SG_EXTERNAL_FORCE_KIND_COUNT];
	sg_external_force_completeness_t
		completeness_by_kind[SG_EXTERNAL_FORCE_KIND_COUNT];
} sg_external_force_view_t;

/* Issue is the offline production builder. It accepts only owner-issued
 * upstream publications and copies every result into its own allocation. */
int SG_ExternalForcePublicationIssue(
	const sg_external_force_source_t *source,
	sg_external_force_publication_t **publication_out,
	sg_external_force_audit_result_t *audit_out);
int SG_ExternalForcePublicationAudit(
	const sg_external_force_source_t *source,
	const sg_external_force_publication_t *publication,
	sg_external_force_audit_result_t *audit_out);
int SG_ExternalForcePublicationRead(
	const sg_external_force_publication_t *publication,
	sg_external_force_view_t *view_out);
int SG_ExternalForcePublicationFact(
	const sg_external_force_publication_t *publication, uint32_t index,
	sg_external_force_fact_t *fact_out);
void SG_ExternalForcePublicationDestroy(
	sg_external_force_publication_t *publication);
const char *SG_ExternalForceAuditCodeString(
	sg_external_force_audit_code_t code);

#endif /* SG_EXTERNAL_FORCE_PUBLICATION_H */
