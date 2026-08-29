int SG_MechanismCapabilityFixtureMain(void);
#define main SG_MechanismCapabilityFixtureMain
#include "sg_mechanism_capability_test.c"
#undef main

#include "slipgate/sg_phase_catalog_internal.h"
#include "slipgate/sg_phase_catalog_owner.h"

static int phase_integration_failures;

#define CHECK_PHASE_INTEGRATION(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		phase_integration_failures++; \
	} \
} while (0)

static void TestOwnerCyclesBoundLiveStorage(mechanism_fixture_t *fixture,
	const sg_mechanism_capability_set_t *accepted_capabilities)
{
	uint32_t cycle;

	for (cycle = 0U; cycle < 32U; cycle++)
	{
		sg_mechanism_capability_owner_t *capability_owner = NULL;
		sg_mechanism_capability_set_t *capabilities = NULL;
		sg_mechanism_capability_error_t capability_error;
		const sg_mechanism_capability_view_t *capability_view = NULL;
		sg_phase_mover_support_provider_owner_t *provider_owner = NULL;
		sg_phase_mover_support_provider_t *provider = NULL;
		sg_phase_catalog_error_t provider_error;
		const sg_phase_mover_support_provider_view_t *provider_view = NULL;

		CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityOwnerCreate(
			&capability_owner));
		CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityBuild(capability_owner,
			&fixture->source, &capabilities, &capability_error));
		CHECK_PHASE_INTEGRATION(capability_owner != NULL &&
			capability_owner->live_count == 1U);
		CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(capability_owner,
			capabilities, &capability_view));
		SG_MechanismCapabilityDestroy(capability_owner, capabilities);
		CHECK_PHASE_INTEGRATION(capability_owner != NULL &&
			capability_owner->live_count == 0U && capability_owner->live == NULL);
		SG_MechanismCapabilityOwnerDestroy(capability_owner);

		CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderOwnerCreate(
			&provider_owner));
		CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
			provider_owner, fixture->capability_owner,
			fixture->configuration_semantics, accepted_capabilities,
			&provider, &provider_error));
		CHECK_PHASE_INTEGRATION(provider_owner != NULL &&
			provider_owner->live_count == 1U);
		CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider_owner,
			provider, &provider_view));
		SG_PhaseMoverSupportProviderDestroy(provider_owner, provider);
		CHECK_PHASE_INTEGRATION(provider_owner != NULL &&
			provider_owner->live_count == 0U && provider_owner->live == NULL);
		SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
	}
}

static void TestOwnerTeardownRejectsPriorToken(mechanism_fixture_t *fixture,
	const sg_mechanism_capability_set_t *accepted_capabilities)
{
	sg_mechanism_capability_owner_t *first_owner = NULL;
	sg_mechanism_capability_owner_t *second_owner = NULL;
	sg_mechanism_capability_set_t *first = NULL;
	sg_mechanism_capability_set_t *second = NULL;
	const sg_mechanism_capability_view_t *view = NULL;
	sg_mechanism_capability_error_t error;
	sg_phase_mover_support_provider_owner_t *provider_owner = NULL;
	sg_phase_mover_support_provider_owner_t *first_provider_owner = NULL;
	sg_phase_mover_support_provider_owner_t *second_provider_owner = NULL;
	sg_phase_mover_support_provider_t *provider = NULL;
	sg_phase_mover_support_provider_t *first_provider = NULL;
	sg_phase_mover_support_provider_t *second_provider = NULL;
	const sg_phase_mover_support_provider_view_t *provider_view = NULL;
	sg_phase_catalog_error_t provider_error;

	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityOwnerCreate(&first_owner));
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityBuild(first_owner,
		&fixture->source, &first, &error));
	SG_MechanismCapabilityOwnerDestroy(first_owner);
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityOwnerCreate(&second_owner));
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityBuild(second_owner,
		&fixture->source, &second, &error));
	CHECK_PHASE_INTEGRATION(first != second);
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityRead(second_owner, first,
		&view));
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(second_owner, second,
		&view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderOwnerCreate(
		&provider_owner));
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderBuild(provider_owner,
		second_owner, fixture->configuration_semantics, first, &provider,
		&provider_error));
	CHECK_PHASE_INTEGRATION(provider == NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(provider_owner,
		second_owner, fixture->configuration_semantics, second, &provider,
		&provider_error));
	SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
	SG_MechanismCapabilityOwnerDestroy(second_owner);

	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderOwnerCreate(
		&first_provider_owner));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		first_provider_owner, fixture->capability_owner,
		fixture->configuration_semantics, accepted_capabilities,
		&first_provider, &provider_error));
	SG_PhaseMoverSupportProviderOwnerDestroy(first_provider_owner);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderOwnerCreate(
		&second_provider_owner));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(
		second_provider_owner, fixture->capability_owner,
		fixture->configuration_semantics, accepted_capabilities,
		&second_provider, &provider_error));
	CHECK_PHASE_INTEGRATION(first_provider != second_provider);
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderRead(
		second_provider_owner, first_provider, &provider_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(
		second_provider_owner, second_provider, &provider_view));
	SG_PhaseMoverSupportProviderOwnerDestroy(second_provider_owner);
}

int main(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_owner_t *cross_capability_owner = NULL;
	sg_phase_mover_support_provider_owner_t *provider_owner = NULL;
	sg_phase_mover_support_provider_owner_t *cross_provider_owner = NULL;
	sg_mechanism_capability_set_t *capabilities = NULL;
	sg_mechanism_capability_set_t *equivalent_capabilities = NULL;
	sg_mechanism_capability_set_t *replacement_capabilities = NULL;
	const sg_mechanism_capability_set_t *stale_capabilities = NULL;
	const sg_mechanism_capability_view_t *capability_view = NULL;
	const sg_mechanism_capability_view_t *equivalent_capability_view = NULL;
	const sg_mechanism_capability_view_t *rejected_capability_view = NULL;
	sg_mechanism_capability_payload_t *equivalent_payload = NULL;
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
	const sg_phase_mover_support_provider_view_t *rejected_provider_view = NULL;
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
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityOwnerCreate(
		&cross_capability_owner));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderOwnerCreate(
		&provider_owner));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderOwnerCreate(
		&cross_provider_owner));
	CHECK_PHASE_INTEGRATION(Build(&fixture, &capabilities, &capability_error));
	CHECK_PHASE_INTEGRATION(capabilities != NULL);
	if (!capabilities)
		return 1;
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(fixture.capability_owner,
		capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(Build(&fixture, &equivalent_capabilities,
		&capability_error));
	equivalent_payload = SG_MechanismCapabilityOwnerPayload(
		fixture.capability_owner, equivalent_capabilities);
	CHECK_PHASE_INTEGRATION(equivalent_payload != NULL);
	if (equivalent_payload)
	{
		equivalent_payload->topology_edge_visits += UINT64_C(37);
		equivalent_payload->view.topology_edge_visits =
			equivalent_payload->topology_edge_visits;
	}
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(fixture.capability_owner,
		equivalent_capabilities,
		&equivalent_capability_view));
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityRead(cross_capability_owner,
		capabilities, &rejected_capability_view));
	CHECK_PHASE_INTEGRATION(capability_view && equivalent_capability_view &&
		capability_view->topology_edge_visits !=
			equivalent_capability_view->topology_edge_visits &&
		capability_view->content_identity ==
			equivalent_capability_view->content_identity);
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderBuild(provider_owner,
		fixture.capability_owner, fixture.configuration_semantics, forged,
		&forged_provider,
		&forged_error));
	CHECK_PHASE_INTEGRATION(forged_provider == NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(provider_owner,
		fixture.capability_owner, fixture.configuration_semantics, capabilities,
		&provider,
		&provider_error));
	CHECK_PHASE_INTEGRATION(provider != NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider_owner,
		provider,
		&provider_view));
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderRead(
		cross_provider_owner, provider, &rejected_provider_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(provider_owner,
		fixture.capability_owner, fixture.configuration_semantics,
		equivalent_capabilities, &provider_again,
		&provider_error));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider_owner,
		provider_again,
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
	phase_source.mover_support_owner = provider_owner;
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
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationBuild(
		fixture.capability_owner, &fixture.authority,
		fixture.configuration, fixture.configuration_semantics, capabilities,
		&publication, &publication_error, &publication_audit));
	CHECK_PHASE_INTEGRATION(publication != NULL);
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationRead(publication,
		&publication_view));
	CHECK_PHASE_INTEGRATION(publication_view != NULL &&
		publication_view->phase_count != 0U &&
		publication_view->transition_count != 0U);
	CHECK_PHASE_INTEGRATION(SG_PhaseCatalogPublicationBuild(
		fixture.capability_owner, &fixture.authority,
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
	SG_PhaseMoverSupportProviderDestroy(provider_owner, provider);
	provider = NULL;
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderRead(provider_owner,
		stale_provider,
		&provider_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(provider_owner,
		fixture.capability_owner, fixture.configuration_semantics,
		equivalent_capabilities,
		&replacement_provider, &provider_error));
	CHECK_PHASE_INTEGRATION(replacement_provider != stale_provider);
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderRead(provider_owner,
		stale_provider,
		&provider_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderRead(provider_owner,
		replacement_provider, &provider_view));
	SG_PhaseMoverSupportProviderDestroy(provider_owner, replacement_provider);
	replacement_provider = NULL;
	SG_PhaseMoverSupportProviderDestroy(provider_owner, provider_again);
	stale_capabilities = capabilities;
	SG_MechanismCapabilityDestroy(fixture.capability_owner, capabilities);
	capabilities = NULL;
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityRead(
		fixture.capability_owner, stale_capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityAudit(
		fixture.capability_owner, &fixture.source, stale_capabilities,
		&(sg_mechanism_capability_audit_result_t){ 0 }));
	CHECK_PHASE_INTEGRATION(!SG_PhaseMoverSupportProviderBuild(provider_owner,
		fixture.capability_owner, fixture.configuration_semantics,
		stale_capabilities, &forged_provider,
		&forged_error));
	CHECK_PHASE_INTEGRATION(forged_provider == NULL);
	CHECK_PHASE_INTEGRATION(Build(&fixture, &replacement_capabilities,
		&capability_error));
	CHECK_PHASE_INTEGRATION(replacement_capabilities != stale_capabilities);
	CHECK_PHASE_INTEGRATION(!SG_MechanismCapabilityRead(
		fixture.capability_owner, stale_capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(SG_MechanismCapabilityRead(
		fixture.capability_owner, replacement_capabilities,
		&capability_view));
	CHECK_PHASE_INTEGRATION(SG_PhaseMoverSupportProviderBuild(provider_owner,
		fixture.capability_owner, fixture.configuration_semantics,
		replacement_capabilities,
		&replacement_provider, &provider_error));
	CHECK_PHASE_INTEGRATION(replacement_provider != NULL);
	TestOwnerCyclesBoundLiveStorage(&fixture, equivalent_capabilities);
	TestOwnerTeardownRejectsPriorToken(&fixture, equivalent_capabilities);
	SG_PhaseMoverSupportProviderDestroy(provider_owner, replacement_provider);
	SG_MechanismCapabilityDestroy(fixture.capability_owner,
		replacement_capabilities);
	SG_MechanismCapabilityDestroy(fixture.capability_owner,
		equivalent_capabilities);
	SG_PhaseMoverSupportProviderOwnerDestroy(cross_provider_owner);
	SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
	SG_MechanismCapabilityOwnerDestroy(cross_capability_owner);
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
