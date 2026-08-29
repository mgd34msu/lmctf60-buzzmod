/* Authenticated phase basis catalog over an accepted configuration space. */
#ifndef SG_PHASE_CATALOG_H
#define SG_PHASE_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "sg_configuration_semantics.h"

#define SG_PHASE_CATALOG_INDEX_NONE UINT32_MAX
#define SG_PHASE_CATALOG_MAX_BINDINGS SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS

/* A source completion value is evidence about the complete mover-support
 * enumeration.  It is not a permission for the caller to manufacture phase
 * records: Build and Audit derive every record from the source again. */
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

/* A mover-support row is produced by the independent mechanism/static
 * authority.  The state mask records the states for which the same support
 * relation was observed; phase basis records intentionally remain state
 * independent. */
typedef struct sg_phase_mover_support_s
{
	uint64_t semantic_region_id;
	sg_rune_mechanism_ref_t mechanism;
	sg_phase_mechanism_state_mask_t mechanism_state_mask;
} sg_phase_mover_support_t;

typedef struct sg_phase_catalog_source_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	sg_phase_mover_support_t *mover_supports;
	uint32_t mover_support_count;
	sg_phase_catalog_completion_t mover_support_completion;
	/* Nonzero identity issued by the independent mover-support verifier. */
	uint64_t mover_support_verifier_identity;
} sg_phase_catalog_source_t;

/* One semantic-region relation.  The stable phase reference, rather than a
 * transient array index, is the authority crossing the publication boundary. */
typedef struct sg_phase_catalog_binding_s
{
	uint64_t semantic_region_id;
	uint32_t configuration_cell;
	sg_rune_phase_ref_t phase;
	uint32_t reserved;
} sg_phase_catalog_binding_t;

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
	SG_PHASE_CATALOG_ERROR_AUDIT_REJECTED,
	SG_PHASE_CATALOG_ERROR_ERROR_COUNT
} sg_phase_catalog_error_code_t;

typedef struct sg_phase_catalog_error_s
{
	sg_phase_catalog_error_code_t code;
	uint32_t source_index;
} sg_phase_catalog_error_t;

typedef enum sg_phase_catalog_audit_code_e
{
	SG_PHASE_CATALOG_AUDIT_OK_COMPLETE = 0,
	SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY,
	SG_PHASE_CATALOG_AUDIT_INVALID_ARGUMENT,
	SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT,
	SG_PHASE_CATALOG_AUDIT_SOURCE_MISMATCH,
	SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE,
	SG_PHASE_CATALOG_AUDIT_INVENTED_PHASE,
	SG_PHASE_CATALOG_AUDIT_DUPLICATE_PHASE,
	SG_PHASE_CATALOG_AUDIT_DUPLICATE_BINDING,
	SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING,
	SG_PHASE_CATALOG_AUDIT_OMITTED_BINDING,
	SG_PHASE_CATALOG_AUDIT_INVENTED_BINDING,
	SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT,
	SG_PHASE_CATALOG_AUDIT_PHASE_DISAGREEMENT,
	SG_PHASE_CATALOG_AUDIT_INVALID_SOURCE,
	SG_PHASE_CATALOG_AUDIT_COMPLETION_DISAGREEMENT,
	SG_PHASE_CATALOG_AUDIT_NONDETERMINISTIC_ORDER,
	SG_PHASE_CATALOG_AUDIT_CODE_COUNT
} sg_phase_catalog_audit_code_t;

typedef struct sg_phase_catalog_audit_result_s
{
	sg_phase_catalog_audit_code_t code;
	uint32_t record;
	uint32_t proved_phases;
	uint32_t omitted_phases;
	uint32_t invented_phases;
	uint32_t proved_bindings;
	uint32_t omitted_bindings;
	uint32_t invented_bindings;
} sg_phase_catalog_audit_result_t;

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
} sg_phase_catalog_t;

int SG_PhaseCatalogBuild(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_t **catalog_out, sg_phase_catalog_error_t *error_out);
int SG_PhaseCatalogAudit(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_audit_result_t *result_out);
int SG_PhaseCatalogBindingsForRegion(const sg_phase_catalog_t *catalog,
	uint64_t semantic_region_id, const sg_phase_catalog_binding_t **bindings_out,
	uint32_t *binding_count_out);
void SG_PhaseCatalogDestroy(sg_phase_catalog_t *catalog);
const char *SG_PhaseCatalogErrorString(sg_phase_catalog_error_code_t code);
const char *SG_PhaseCatalogAuditCodeString(sg_phase_catalog_audit_code_t code);

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
} sg_phase_catalog_view_t;

typedef struct sg_phase_catalog_publication_s sg_phase_catalog_publication_t;

int SG_PhaseCatalogPublicationIssue(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_audit_result_t *audit_out);
int SG_PhaseCatalogPublicationRead(
	const sg_phase_catalog_publication_t *publication,
	const sg_phase_catalog_view_t **view_out);
int SG_PhaseCatalogPublicationStorageOverlaps(
	const sg_phase_catalog_publication_t *publication,
	const void *address, size_t size);
void SG_PhaseCatalogPublicationDestroy(
	sg_phase_catalog_publication_t *publication);

#endif /* SG_PHASE_CATALOG_H */
