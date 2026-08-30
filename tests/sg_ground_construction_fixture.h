#ifndef SG_GROUND_CONSTRUCTION_FIXTURE_H
#define SG_GROUND_CONSTRUCTION_FIXTURE_H

#include "../slipgate/sg_host_law_publication.h"

sg_host_law_construction_t *SG_TestGroundConstructionCreate(
	const sg_host_collision_authority_t *authority,
	sg_host_pmove_function_t pmove);
void SG_TestGroundConstructionDestroy(
	sg_host_law_construction_t *construction);

#endif /* SG_GROUND_CONSTRUCTION_FIXTURE_H */
