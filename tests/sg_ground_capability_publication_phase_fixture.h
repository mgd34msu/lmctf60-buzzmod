#ifndef SG_GROUND_CAPABILITY_PUBLICATION_PHASE_FIXTURE_H
#define SG_GROUND_CAPABILITY_PUBLICATION_PHASE_FIXTURE_H

#include "../slipgate/sg_phase_catalog.h"

int SG_TestGroundPhasePublicationBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_phase_catalog_publication_owner_t **owner_out,
	sg_phase_catalog_publication_t **publication_out);

#endif
