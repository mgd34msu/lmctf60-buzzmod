#ifndef SG_PHASE_CATALOG_INTERNAL_H
#define SG_PHASE_CATALOG_INTERNAL_H

#include "sg_phase_catalog.h"
#include "sg_mechanism_capability_internal.h"

#define SG_PHASE_CATALOG_MAGIC UINT64_C(0x50434154414c4f47)

#if defined(SG_PHASE_CATALOG_TESTING) && \
	defined(SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT)
#define SG_PHASE_CATALOG_TRANSITION_APPEND_LIMIT \
	SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT
#else
#define SG_PHASE_CATALOG_TRANSITION_APPEND_LIMIT \
	SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS
#endif

typedef struct sg_phase_catalog_transition_pair_s
{
	sg_rune_phase_transition_t transition;
	sg_phase_catalog_transition_evidence_t evidence;
} sg_phase_catalog_transition_pair_t;

typedef struct sg_phase_mover_support_provider_payload_s
{
	sg_rune_model_identity_t identity;
	sg_phase_catalog_completion_t completion;
	/* Canonical provenance of the accepted capability snapshot.  This is a
	 * value digest, never an address, so equivalent builds remain identical. */
	uint64_t accepted_capability_identity;
	uint64_t verifier_identity;
	sg_phase_mover_support_t *supports;
	uint32_t support_count;
	sg_mechanism_capability_fact_t *facts;
	uint32_t fact_count;
} sg_phase_mover_support_provider_payload_t;

typedef struct sg_phase_mover_support_provider_record_s
{
	sg_phase_mover_support_provider_t *token;
	sg_phase_mover_support_provider_payload_t *payload;
	sg_phase_mover_support_provider_view_t view;
	sg_phase_mover_support_t *view_supports;
	sg_mechanism_capability_fact_t *view_facts;
	struct sg_phase_mover_support_provider_record_s *next;
} sg_phase_mover_support_provider_record_t;

struct sg_phase_mover_support_provider_owner_s
{
	sg_phase_mover_support_provider_record_t *live;
	uint32_t live_count;
};

/* Pure derivation input for tests and offline checks.  This type carries no
 * provider owner or capability token, so it cannot be passed to any public
 * build, audit, or publication entry point. */
typedef struct sg_phase_catalog_non_authoritative_source_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	sg_phase_mover_support_t *supports;
	uint32_t support_count;
	sg_mechanism_capability_fact_t *facts;
	uint32_t fact_count;
	sg_phase_catalog_completion_t completion;
	uint64_t verifier_identity;
} sg_phase_catalog_non_authoritative_source_t;

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
	uint32_t *transition_pair_hash;
	uint32_t transition_pair_hash_capacity;
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
int SG_PhaseCatalogDeriveExpectedNonAuthoritative(
	const sg_phase_catalog_non_authoritative_source_t *source,
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
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider);
sg_phase_mover_support_provider_payload_t *
SG_PhaseMoverSupportProviderPayload(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider);

#define SG_PHASE_SOURCE_PROVIDER(source) \
	SG_PhaseMoverSupportProviderPayload((source)->mover_support_owner, \
		(source)->mover_support_provider)
#endif /* SG_PHASE_CATALOG_INTERNAL_H */
