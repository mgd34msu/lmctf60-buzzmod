int SG_MechanismCapabilityFixtureMain(void);
#define main SG_MechanismCapabilityFixtureMain
#include "sg_mechanism_capability_test.c"
#undef main

#include "slipgate/sg_phase_catalog.h"
#include "slipgate/sg_phase_catalog_owner.h"

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
	sg_mechanism_capability_set_t *equivalent_capabilities = NULL;
	sg_mechanism_capability_set_t *replacement_capabilities = NULL;
	const sg_mechanism_capability_set_t *stale_capabilities = NULL;
	const sg_mechanism_capability_view_t *capability_view = NULL;
	const sg_mechanism_capability_view_t *equivalent_capability_view = NULL;
	sg_mechanism_capability_error_t capability_error;
	sg_phase_mover_support_provider_t *provider = NULL;
	sg_phase_mover_support_provider_t *provider_again = NULL;
	sg_phase_mover_support_provider_t *replacement_provider = NULL;
	const sg_phase_mover_support_provider_t *stale_provider = NULL;
	sg_phase_catalog_error_t provider_error;
	sg_phase_catalog_source_t phase_source;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t phase_error;
	sg_phase_catalog_audit_result_t audit;
	const sg_phase_mover_support_provider_view_t *provider_view = NULL;
	const sg_phase_mover_support_provider_view_t *provider_view_again = NULL;
	sg_phase_catalog_publication_t *publication = NULL;
	sg_phase_catalog_publication_t *publication_again = NULL;
	const sg_phase_catalog_view_t *publication_view = NULL;
	const sg_phase_catalog_view_t *publication_view_again = NULL;
	sg_phase_catalog_error_t publication_error;
	sg_phase_catalog_audit_result_t publication_audit;
	sg_phase_catalog_error_t publication_again_error;
	sg_phase_catalog_audit_result_t publication_again_audit;
	const sg_mechanism_capability_set_t *forged =
		(const sg_mechanism_capability_set_t *)(uintptr_t)UINT32_C(1);
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
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(Build(&fixture, &equivalent_capabilities,
		&capability_error));
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(equivalent_capabilities,
		&equivalent_capability_view));
	CHECK_PHASE_INTEGRATION(capability_view && equivalent_capability_view &&
		capability_view->content_identity ==
			equivalent_capability_view->content_identity);
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, forged, &forged_provider,
		&forged_error));
	CHECK_PHASE_INTEGRATION(forged_provider == NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, capabilities, &provider,
		&provider_error));
	CHECK_PHASE_INTEGRATION(provider != NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider,
		&provider_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, equivalent_capabilities, &provider_again,
		&provider_error));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider_again,
		&provider_view_again));
	CHECK_PHASE_INTEGRATION(provider_view != NULL && capability_view != NULL &&
		provider_view->fact_count == capability_view->fact_count &&
		provider_view->support_count != 0U);
	CHECK_PHASE_INTEGRATION(provider_view_again != NULL && provider_view != NULL &&
		provider_view_again->verifier_identity == provider_view->verifier_identity &&
		provider_view_again->support_count == provider_view->support_count &&
		provider_view_again->fact_count == provider_view->fact_count &&
		memcmp(provider_view_again->supports, provider_view->supports,
			(size_t)provider_view->support_count * sizeof(*provider_view->supports)) == 0 &&
		memcmp(provider_view_again->facts, provider_view->facts,
			(size_t)provider_view->fact_count * sizeof(*provider_view->facts)) == 0);
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
			uint32_t original_mask =
				catalog->bindings[binding_index].mechanism_state_mask;
			catalog->bindings[binding_index].mechanism_state_mask = original_mask ^
				SG_PHASE_MECHANISM_STATE_RETURNING;
			if (catalog->bindings[binding_index].mechanism_state_mask == 0U)
				catalog->bindings[binding_index].mechanism_state_mask =
					original_mask ^ SG_PHASE_MECHANISM_STATE_INACTIVE;
			CHECK_PHASE_INTEGRATION(!SG_PhaseCatalogAudit(&phase_source,
				catalog, &audit));
			CHECK_PHASE_INTEGRATION(audit.code ==
				SG_PHASE_CATALOG_AUDIT_BINDING_DISAGREEMENT);
			catalog->bindings[binding_index].mechanism_state_mask = original_mask;
		}
		for (index = 0U; index < catalog->transition_count; index++)
			if (catalog->transition_evidence[index].origin ==
				SG_PHASE_CATALOG_TRANSITION_MECHANISM_STATE_TIMING)
			{
				catalog->transition_evidence[index].delay_ms++;
				CHECK_PHASE_INTEGRATION(!SG_PhaseCatalogAudit(&phase_source,
					catalog, &audit));
				CHECK_PHASE_INTEGRATION(audit.code ==
					SG_PHASE_CATALOG_AUDIT_TRANSITION_DISAGREEMENT);
				break;
			}
		SG_PhaseCatalogDestroy(catalog);
	}
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationBuild(&fixture.authority,
		fixture.configuration, fixture.configuration_semantics, capabilities,
		&publication, &publication_error, &publication_audit));
	CHECK_PHASE_INTEGRATION(publication != NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationRead(publication,
		&publication_view));
	CHECK_PHASE_INTEGRATION(publication_view != NULL &&
		publication_view->phase_count != 0U &&
		publication_view->transition_count != 0U);
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationBuild(&fixture.authority,
		fixture.configuration, fixture.configuration_semantics, capabilities,
		&publication_again, &publication_again_error,
		&publication_again_audit));
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationRead(publication_again,
		&publication_view_again));
	CHECK_PHASE_INTEGRATION(publication_view != NULL &&
		publication_view_again != NULL &&
		memcmp(&publication_view_again->identity, &publication_view->identity,
			sizeof(publication_view->identity)) == 0 &&
		publication_view_again->completion == publication_view->completion &&
		publication_view_again->transition_completion ==
			publication_view->transition_completion &&
		publication_view_again->mover_support_verifier_identity ==
			publication_view->mover_support_verifier_identity &&
		publication_view_again->phase_count == publication_view->phase_count &&
		publication_view_again->binding_count == publication_view->binding_count &&
		publication_view_again->transition_count ==
			publication_view->transition_count);
	if (publication_view && publication_view_again &&
		publication_view_again->phase_count == publication_view->phase_count &&
		publication_view_again->binding_count == publication_view->binding_count &&
		publication_view_again->transition_count ==
			publication_view->transition_count)
	{
		if (publication_view->phase_count != 0U)
			CHECK_PHASE_INTEGRATION(memcmp(publication_view_again->phases,
				publication_view->phases, (size_t)publication_view->phase_count *
					sizeof(*publication_view->phases)) == 0);
		if (publication_view->binding_count != 0U)
			CHECK_PHASE_INTEGRATION(memcmp(publication_view_again->bindings,
				publication_view->bindings, (size_t)publication_view->binding_count *
					sizeof(*publication_view->bindings)) == 0);
		if (publication_view->transition_count != 0U)
		{
			CHECK_PHASE_INTEGRATION(memcmp(publication_view_again->transitions,
				publication_view->transitions, (size_t)publication_view->transition_count *
					sizeof(*publication_view->transitions)) == 0);
			CHECK_PHASE_INTEGRATION(memcmp(publication_view_again->transition_evidence,
				publication_view->transition_evidence, (size_t)publication_view->transition_count *
					sizeof(*publication_view->transition_evidence)) == 0);
		}
	}
	SG_PhaseCatalogPublicationDestroy(publication);
	SG_PhaseCatalogPublicationDestroy(publication_again);
	stale_provider = provider;
	SG_PhaseMoverSupportProviderDestroy(provider);
	provider = NULL;
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderRead(stale_provider,
		&provider_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, equivalent_capabilities,
		&replacement_provider, &provider_error));
	CHECK_PHASE_INTEGRATION(replacement_provider != stale_provider);
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderRead(stale_provider,
		&provider_view));
	SG_PhaseMoverSupportProviderDestroy(replacement_provider);
	replacement_provider = NULL;
	SG_PhaseMoverSupportProviderDestroy(provider_again);
	stale_capabilities = capabilities;
	SG_MechanismCapabilityDestroy(capabilities);
	capabilities = NULL;
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityRead(stale_capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, stale_capabilities, &forged_provider,
		&forged_error));
	CHECK_PHASE_INTEGRATION(forged_provider == NULL);
	CHECK_PHASE_INTEGRATION(Build(&fixture, &replacement_capabilities,
		&capability_error));
	CHECK_PHASE_INTEGRATION(replacement_capabilities != stale_capabilities);
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityRead(stale_capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(replacement_capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		fixture.configuration_semantics, replacement_capabilities,
		&replacement_provider, &provider_error));
	CHECK_PHASE_INTEGRATION(replacement_provider != NULL);
	SG_PhaseMoverSupportProviderDestroy(replacement_provider);
	SG_MechanismCapabilityDestroy(replacement_capabilities);
	SG_MechanismCapabilityDestroy(equivalent_capabilities);
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
