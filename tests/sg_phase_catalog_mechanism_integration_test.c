int SG_MechanismCapabilityFixtureMain(void);
#define main SG_MechanismCapabilityFixtureMain
#include "sg_mechanism_capability_test.c"
#undef main

#include "slipgate/sg_phase_catalog.h"

static int phase_integration_failures;

#define CHECK_PHASE_INTEGRATION(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		phase_integration_failures++; \
	} \
} while (0)

int main(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *capabilities = NULL;
	sg_mechanism_capability_error_t capability_error;
	sg_phase_mover_support_provider_t *provider = NULL;
	sg_phase_catalog_error_t provider_error;
	sg_phase_catalog_source_t phase_source;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t phase_error;
	sg_phase_catalog_audit_result_t audit;
	const sg_phase_mover_support_provider_view_t *provider_view = NULL;
	sg_mechanism_capability_set_t forged;
	sg_phase_mover_support_provider_t *forged_provider = NULL;
	sg_phase_catalog_error_t forged_error;
	uint32_t index;
	uint32_t state_mask = 0U;
	uint32_t binding_index = UINT32_MAX;

	CHECK_PHASE_INTEGRATION(FixtureInit(&fixture));
	CHECK_PHASE_INTEGRATION(Build(&fixture, &capabilities, &capability_error));
	CHECK_PHASE_INTEGRATION(capabilities != NULL);
	if (!capabilities)
		return 1;
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilitySetAccepted(capabilities));
	forged = *capabilities;
	forged.self = &forged;
	forged.seal_digest = SG_MechanismCapabilitySetDigest(&forged);
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, &forged, &forged_provider,
		&forged_error));
	CHECK_PHASE_INTEGRATION(forged_provider == NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, capabilities, &provider,
		&provider_error));
	CHECK_PHASE_INTEGRATION(provider != NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider,
		&provider_view));
	CHECK_PHASE_INTEGRATION(provider_view != NULL &&
		provider_view->fact_count == capabilities->fact_count &&
		provider_view->support_count != 0U);
	if (provider_view)
		for (index = 0U; index < provider_view->support_count; index++)
			state_mask |= provider_view->supports[index].mechanism_state_mask;
	CHECK_PHASE_INTEGRATION((state_mask & SG_PHASE_MECHANISM_STATE_RETURNING) !=
		0U);
	memset(&phase_source, 0, sizeof(phase_source));
	phase_source.authority = &fixture.authority;
	phase_source.configuration = fixture.configuration;
	phase_source.semantics = fixture.configuration_semantics;
	phase_source.mover_support_provider = provider;
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogBuild(&phase_source, &catalog,
		&phase_error));
	CHECK_PHASE_INTEGRATION(catalog != NULL);
	if (catalog)
	{
		CHECK_PHASE_INTEGRATION(provider_view != NULL);
		if (provider_view)
			CHECK_PHASE_INTEGRATION(catalog->mover_support_verifier_identity ==
				provider_view->verifier_identity);
		CHECK_PHASE_INTEGRATION(catalog->transition_count != 0U);
		CHECK_PHASE_INTEGRATION(SG_PhaseCatalogAudit(&phase_source, catalog,
			&audit));
		CHECK_PHASE_INTEGRATION(audit.code == SG_PHASE_CATALOG_AUDIT_OK_COMPLETE);
		for (index = 0U; index < catalog->binding_count; index++)
			if (catalog->bindings[index].mechanism_state_mask != 0U)
			{
				binding_index = index;
				break;
			}
		CHECK_PHASE_INTEGRATION(binding_index != UINT32_MAX);
		if (binding_index != UINT32_MAX)
		{
			catalog->bindings[binding_index].mechanism_state_mask =
				SG_PHASE_MECHANISM_STATE_RETURNING;
			CHECK_PHASE_INTEGRATION(!SG_PhaseCatalogAudit(&phase_source,
				catalog, &audit));
			CHECK_PHASE_INTEGRATION(audit.code ==
				SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT);
		}
		SG_PhaseCatalogDestroy(catalog);
	}
	SG_PhaseMoverSupportProviderDestroy(provider);
	SG_MechanismCapabilityDestroy(capabilities);
	FixtureDestroy(&fixture);
	if (phase_integration_failures)
	{
		fprintf(stderr, "%d phase mechanism integration checks failed\n",
			phase_integration_failures);
		return 1;
	}
	puts("phase mechanism integration checks passed");
	return 0;
}
