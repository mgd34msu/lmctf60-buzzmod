#ifndef SG_PHASE_CATALOG_INTERNAL_H
#define SG_PHASE_CATALOG_INTERNAL_H

#include "sg_phase_catalog.h"

typedef struct sg_phase_catalog_expected_s
{
	sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;
	sg_phase_catalog_completion_t completion;
	uint64_t mover_support_verifier_identity;
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

#endif /* SG_PHASE_CATALOG_INTERNAL_H */
