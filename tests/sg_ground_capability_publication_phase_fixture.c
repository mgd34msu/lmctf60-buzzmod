#define main GroundPublicationMechanismFixtureMain
int GroundPublicationMechanismFixtureMain(void);
#include "sg_mechanism_capability_test.c"
#undef main

#include "sg_ground_capability_publication_phase_fixture.h"

#include "../slipgate/sg_phase_catalog_owner.h"

static void RetargetFixture(mechanism_fixture_t *fixture,
	const sg_rune_model_identity_t *identity)
{
	uint32_t index;

	fixture->authority.identity = *identity;
	fixture->configuration->identity = *identity;
	fixture->configuration_semantics->identity = *identity;
	fixture->catalog.identity = *identity;
	for (index = 0U; index < PHASE_COUNT; index++)
	{
		fixture->phases[index].time_quantum_ms = identity->physics.substep_ms;
		fixture->phases[index].time_horizon_ms = identity->physics.frame_ms * 10U;
	}
	for (index = 0U; index < TRACE_COUNT; index++)
	{
		fixture->traces[index].bsp_content_id = identity->bsp_content_id;
		fixture->traces[index].physics_abi_id = identity->physics_abi_id;
	}
}

int SG_TestGroundPhasePublicationBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_phase_catalog_publication_owner_t **owner_out,
	sg_phase_catalog_publication_t **publication_out)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *capabilities = NULL;
	sg_mechanism_capability_error_t capability_error;
	sg_phase_catalog_error_t phase_error;
	sg_phase_catalog_audit_result_t phase_audit;
	int ok = 0;

	if (!authority || !configuration || !semantics || !owner_out ||
		*owner_out || !publication_out || *publication_out)
		return 0;
	memset(&fixture, 0, sizeof(fixture));
	if (!FixtureInit(&fixture))
		return 0;
	RetargetFixture(&fixture, &authority->identity);
	if (!Build(&fixture, &capabilities, &capability_error) ||
		!SG_PhaseCatalogPublicationOwnerCreate(owner_out))
		goto done;
	if (!SG_PhaseCatalogPublicationBuild(*owner_out,
			fixture.capability_owner, authority, configuration, semantics,
			capabilities, publication_out, &phase_error, &phase_audit))
		goto done;
	ok = 1;

done:
	SG_MechanismCapabilityDestroy(fixture.capability_owner, capabilities);
	FixtureDestroy(&fixture);
	if (!ok)
	{
		SG_PhaseCatalogPublicationOwnerDestroy(*owner_out);
		*owner_out = NULL;
	}
	return ok;
}
