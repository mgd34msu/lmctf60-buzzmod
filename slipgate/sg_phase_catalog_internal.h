#ifndef SG_PHASE_CATALOG_INTERNAL_H
#define SG_PHASE_CATALOG_INTERNAL_H

#include "sg_phase_catalog.h"

#define SG_PHASE_MOVER_SUPPORT_PROVIDER_MAGIC UINT64_C(0x50524f5649443031)

typedef struct sg_phase_catalog_transition_pair_s
{
	sg_rune_phase_transition_t transition;
	sg_phase_catalog_transition_evidence_t evidence;
} sg_phase_catalog_transition_pair_t;

struct sg_phase_mover_support_provider_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const struct sg_phase_mover_support_provider_s *self;
	sg_rune_model_identity_t identity;
	sg_phase_catalog_completion_t completion;
	/* Canonical provenance of the accepted capability snapshot.  This is a
	 * value digest, never an address, so equivalent builds remain identical. */
	uint64_t accepted_capability_digest;
	uint64_t verifier_identity;
	sg_phase_mover_support_t *supports;
	uint32_t support_count;
	sg_mechanism_capability_fact_t *facts;
	uint32_t fact_count;
	sg_phase_mover_support_provider_view_t view;
};

typedef struct sg_phase_catalog_expected_s
{
	sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;
	sg_rune_phase_transition_t *transitions;
	sg_phase_catalog_transition_evidence_t *transition_evidence;
	uint32_t transition_count;
	sg_phase_catalog_transition_pair_t *transition_pairs;
	uint32_t transition_pair_count;
	uint32_t transition_pair_capacity;
	sg_phase_catalog_completion_t completion;
	sg_phase_catalog_completion_t transition_completion;
	uint64_t mover_support_verifier_identity;
	uint32_t phase_capacity;
	uint32_t binding_capacity;
	uint32_t transition_capacity;
	uint32_t *phase_hash;
	uint32_t phase_hash_capacity;
	uint32_t *phase_neutral_hash;
	uint32_t phase_neutral_hash_capacity;
	/* Construction-time provenance for every static phase. */
	uint32_t *phase_region_by_phase;
	uint32_t phase_region_capacity;
} sg_phase_catalog_expected_t;

void SG_PhaseCatalogSetError(sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_error_code_t code, uint32_t source_index);
int SG_PhaseCatalogSourceValidate(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_error_t *error_out);
int SG_PhaseCatalogBuildExpected(const sg_phase_catalog_source_t *source,
	sg_phase_catalog_expected_t *expected,
	sg_phase_catalog_error_t *error_out);
void SG_PhaseCatalogExpectedDestroy(sg_phase_catalog_expected_t *expected);
int SG_PhaseCatalogHeaderValid(const sg_phase_catalog_t *catalog);
int SG_PhaseCatalogPhaseEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right);
int SG_PhaseCatalogBindingEqual(const sg_phase_catalog_binding_t *left,
	const sg_phase_catalog_binding_t *right);
int SG_PhaseCatalogIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right);
int SG_PhaseMoverSupportProviderHeaderValid(
	const sg_phase_mover_support_provider_t *provider);
/* Test/producer fixture for an explicitly empty accepted provider.  It is
 * intentionally outside the public catalog-consumer API; production callers
 * use SG_PhaseMoverSupportProviderBuild with an accepted capability set. */
int SG_PhaseMoverSupportProviderBuildEmpty(
	const sg_rune_model_identity_t *identity,
	sg_phase_mover_support_provider_t **provider_out,
	sg_phase_catalog_error_t *error_out);

#endif /* SG_PHASE_CATALOG_INTERNAL_H */
