/* Authenticated phase basis catalog over an accepted configuration space. */
#ifndef SG_PHASE_CATALOG_H
#define SG_PHASE_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "sg_configuration_semantics.h"
#include "sg_mechanism_capability.h"

#define SG_PHASE_CATALOG_INDEX_NONE UINT32_MAX
#define SG_PHASE_CATALOG_MAX_BINDINGS SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS

/* A source completion value is evidence about the complete mover-support
 * enumeration.  It is not a permission for the caller to manufacture phase
 * records: Build derives every record from the source. */
typedef enum sg_phase_catalog_completion_e
{
	SG_PHASE_CATALOG_INCOMPLETE = 0,
	SG_PHASE_CATALOG_COMPLETE,
	SG_PHASE_CATALOG_PROVEN_EMPTY,
	SG_PHASE_CATALOG_COMPLETION_COUNT
} sg_phase_catalog_completion_t;

typedef uint32_t sg_phase_mechanism_state_mask_t;
enum
{
	SG_PHASE_MECHANISM_STATE_INACTIVE = UINT32_C(1) << 0,
	SG_PHASE_MECHANISM_STATE_ACTIVATING = UINT32_C(1) << 1,
	SG_PHASE_MECHANISM_STATE_ACTIVE = UINT32_C(1) << 2,
	SG_PHASE_MECHANISM_STATE_DWELLING = UINT32_C(1) << 3,
	SG_PHASE_MECHANISM_STATE_RETURNING = UINT32_C(1) << 4,
	SG_PHASE_MECHANISM_STATE_RESET = UINT32_C(1) << 5,
	SG_PHASE_MECHANISM_STATE_INTERRUPTED = UINT32_C(1) << 6
};

#define SG_PHASE_MECHANISM_STATE_KNOWN \
	(SG_PHASE_MECHANISM_STATE_INACTIVE | SG_PHASE_MECHANISM_STATE_ACTIVATING | \
	 SG_PHASE_MECHANISM_STATE_ACTIVE | SG_PHASE_MECHANISM_STATE_DWELLING | \
	 SG_PHASE_MECHANISM_STATE_RETURNING | SG_PHASE_MECHANISM_STATE_RESET | \
	 SG_PHASE_MECHANISM_STATE_INTERRUPTED)

/* A mover-support row is retained only inside an issued provider snapshot.
 * Callers cannot supply mechanism IDs or verifier tokens to the catalog. */
typedef struct sg_phase_mover_support_s
{
	uint64_t semantic_region_id;
	sg_rune_mechanism_ref_t mechanism;
	sg_phase_mechanism_state_mask_t mechanism_state_mask;
} sg_phase_mover_support_t;

typedef struct sg_phase_mover_support_provider_s
	sg_phase_mover_support_provider_t;
typedef struct sg_phase_mover_support_provider_owner_s
	sg_phase_mover_support_provider_owner_t;

typedef struct sg_phase_mover_support_provider_view_s
{
	sg_rune_model_identity_t identity;
	sg_phase_catalog_completion_t completion;
	uint64_t verifier_identity;
	const sg_phase_mover_support_t *supports;
	uint32_t support_count;
	const sg_mechanism_capability_fact_t *facts;
	uint32_t fact_count;
} sg_phase_mover_support_provider_view_t;

typedef struct sg_phase_catalog_source_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	/* This object is issued from an accepted immutable mechanism result. */
	const sg_phase_mover_support_provider_owner_t *mover_support_owner;
	const sg_phase_mover_support_provider_t *mover_support_provider;
} sg_phase_catalog_source_t;

/* One semantic-region relation.  The stable phase reference, rather than a
 * transient array index, is the authority crossing the publication boundary. */
typedef struct sg_phase_catalog_binding_s
{
	uint64_t semantic_region_id;
	uint32_t configuration_cell;
	sg_rune_phase_ref_t phase;
	sg_phase_mechanism_state_mask_t mechanism_state_mask;
} sg_phase_catalog_binding_t;

typedef enum sg_phase_catalog_transition_origin_e
{
	SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP = 0,
	SG_PHASE_CATALOG_TRANSITION_PORTAL,
	SG_PHASE_CATALOG_TRANSITION_SUPPORT_CHANGE,
	SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING,
	SG_PHASE_CATALOG_TRANSITION_ORIGIN_COUNT
} sg_phase_catalog_transition_origin_t;

/* The RUNE transition is the canonical record.  Evidence keeps the
 * configuration/provider record that justified it, including every state
 * mask and timing input used by mechanism transitions. */
typedef struct sg_phase_catalog_transition_evidence_s
{
	sg_phase_catalog_transition_origin_t origin;
	uint32_t source_record;
	uint32_t destination_record;
	uint32_t source_cell;
	uint32_t destination_cell;
	uint64_t source_region_id;
	uint64_t destination_region_id;
	sg_rune_portal_ref_t portal;
	sg_rune_mechanism_ref_t mechanism;
	sg_phase_mechanism_state_mask_t source_state_mask;
	sg_phase_mechanism_state_mask_t destination_state_mask;
	uint64_t provider_verifier_identity;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t travel_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
	/* PORTAL is geometric adjacency and has no movement travel time.  This
	 * authenticated field is therefore required to remain exactly zero. */
	uint32_t portal_duration_ms;
	uint64_t activation_time_ms;
	uint64_t active_time_ms;
	uint64_t exit_time_ms;
	uint64_t reset_time_ms;
} sg_phase_catalog_transition_evidence_t;

typedef enum sg_phase_catalog_error_code_e
{
	SG_PHASE_CATALOG_ERROR_NONE = 0,
	SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT,
	SG_PHASE_CATALOG_ERROR_IDENTITY_MISMATCH,
	SG_PHASE_CATALOG_ERROR_INVALID_SOURCE,
	SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE,
	SG_PHASE_CATALOG_ERROR_INVALID_PHASE,
	SG_PHASE_CATALOG_ERROR_INVALID_BINDING,
	SG_PHASE_CATALOG_ERROR_DUPLICATE_SOURCE,
	SG_PHASE_CATALOG_ERROR_OVERFLOW,
	SG_PHASE_CATALOG_ERROR_OUT_OF_MEMORY,
	SG_PHASE_CATALOG_ERROR_CHECK_REJECTED,
	SG_PHASE_CATALOG_ERROR_ERROR_COUNT
} sg_phase_catalog_error_code_t;

typedef struct sg_phase_catalog_error_s
{
	sg_phase_catalog_error_code_t code;
	uint32_t source_index;
} sg_phase_catalog_error_t;

typedef enum sg_phase_catalog_check_code_e
{
	SG_PHASE_CATALOG_CHECK_OK_COMPLETE = 0,
	SG_PHASE_CATALOG_CHECK_INVALID_ARGUMENT,
	SG_PHASE_CATALOG_CHECK_STORAGE_INVALID,
	SG_PHASE_CATALOG_CHECK_SOURCE_MISMATCH,
	SG_PHASE_CATALOG_CHECK_DUPLICATE_PHASE,
	SG_PHASE_CATALOG_CHECK_DUPLICATE_BINDING,
	SG_PHASE_CATALOG_CHECK_DUPLICATE_TRANSITION,
	SG_PHASE_CATALOG_CHECK_NONDETERMINISTIC_ORDER,
	SG_PHASE_CATALOG_CHECK_CODE_COUNT
} sg_phase_catalog_check_code_t;

typedef struct sg_phase_catalog_check_result_s
{
	sg_phase_catalog_check_code_t code;
	uint32_t record;
} sg_phase_catalog_check_result_t;

/* The build result remains inspectable for audit diagnostics.  Callers must
 * not treat this mutable construction object as published state; use the
 * publication API for an owned immutable snapshot. */
typedef struct sg_phase_catalog_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const struct sg_phase_catalog_s *self;
	sg_rune_model_identity_t identity;
	sg_phase_catalog_completion_t completion;
	sg_phase_catalog_completion_t transition_completion;
	uint64_t mover_support_verifier_identity;
	sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	uint32_t phase_capacity;
	sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;
	uint32_t binding_capacity;
	sg_rune_phase_transition_t *transitions;
	sg_phase_catalog_transition_evidence_t *transition_evidence;
	uint32_t transition_count;
	uint32_t transition_capacity;
} sg_phase_catalog_t;

int SG_PhaseCatalogBuild(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_t **catalog_out, sg_phase_catalog_error_t *error_out);
/* Linear structural acceptance: header, storage shape, binding identity,
 * deterministic phase order, and duplicate phases, bindings, and transitions.
 * It reads the published catalog only and never re-derives the model. */
int SG_PhaseCatalogValidate(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_check_result_t *result_out);
int SG_PhaseCatalogBindingsForRegion(const sg_phase_catalog_t *catalog,
	uint64_t semantic_region_id, const sg_phase_catalog_binding_t **bindings_out,
	uint32_t *binding_count_out);
void SG_PhaseCatalogDestroy(sg_phase_catalog_t *catalog);
const char *SG_PhaseCatalogErrorString(sg_phase_catalog_error_code_t code);

typedef struct sg_phase_catalog_view_s
{
	sg_rune_model_identity_t identity;
	sg_phase_catalog_completion_t completion;
	sg_phase_catalog_completion_t transition_completion;
	uint64_t mover_support_verifier_identity;
	const sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	const sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;
	const sg_rune_phase_transition_t *transitions;
	const sg_phase_catalog_transition_evidence_t *transition_evidence;
	uint32_t transition_count;
} sg_phase_catalog_view_t;

typedef struct sg_phase_catalog_publication_s sg_phase_catalog_publication_t;
typedef struct sg_phase_catalog_publication_owner_s
	sg_phase_catalog_publication_owner_t;

int SG_PhaseCatalogPublicationOwnerCreate(
	sg_phase_catalog_publication_owner_t **owner_out);
void SG_PhaseCatalogPublicationOwnerDestroy(
	sg_phase_catalog_publication_owner_t *owner);
int SG_PhaseCatalogPublicationIssue(
	sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_check_result_t *check_out);
int SG_PhaseCatalogPublicationRead(
	const sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_publication_t *publication,
	const sg_phase_catalog_view_t **view_out);
int SG_PhaseCatalogPublicationStorageOverlaps(
	const sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_publication_t *publication,
	const void *address, size_t size);
void SG_PhaseCatalogPublicationDestroy(
	sg_phase_catalog_publication_owner_t *owner,
	sg_phase_catalog_publication_t *publication);

/* Issue an immutable mover-support snapshot only from a sealed capability
 * result.  The provider derives support IDs, masks, and its verifier identity
 * from the accepted facts; none are caller-shaped inputs. */
int SG_PhaseMoverSupportProviderOwnerCreate(
	sg_phase_mover_support_provider_owner_t **owner_out);
void SG_PhaseMoverSupportProviderOwnerDestroy(
	sg_phase_mover_support_provider_owner_t *owner);
int SG_PhaseMoverSupportProviderBuild(
	sg_phase_mover_support_provider_owner_t *owner,
	const sg_mechanism_capability_owner_t *capability_owner,
	const sg_configuration_semantics_t *semantics,
	const sg_mechanism_capability_set_t *accepted_capabilities,
	sg_phase_mover_support_provider_t **provider_out,
	sg_phase_catalog_error_t *error_out);
int SG_PhaseMoverSupportProviderRead(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider,
	const sg_phase_mover_support_provider_view_t **view_out);
void SG_PhaseMoverSupportProviderDestroy(
	sg_phase_mover_support_provider_owner_t *owner,
	sg_phase_mover_support_provider_t *provider);

#endif /* SG_PHASE_CATALOG_H */
