/* Production owner for the immutable host-law publication. */
#ifndef SG_HOST_LAW_OWNER_H
#define SG_HOST_LAW_OWNER_H

#include "sg_host_law_publication.h"

/* Install only after the production BSP/identity bridge has supplied the
 * exact authority.  A failed install leaves the previous owner untouched. */
sg_host_law_result_t SG_HostLawProductionInstall(
	const sg_host_collision_authority_t *authority);

/* Production level owner: obtain the committed level identity and load the
 * same map BSP used by the engine before publishing any consumer view. */
sg_host_law_result_t SG_HostLawProductionInstallLevel(const char *mapname);

/* Level teardown invalidates every borrowed BSP and its publication. */
void SG_HostLawProductionReset(void);

/* The frame owner calls this before consumers can use host laws.  With no
 * exact engine binding installed it returns HOST_UNAVAILABLE (fail closed). */
sg_host_law_result_t SG_HostLawProductionRevalidate(void);

const sg_host_law_publication_t *SG_HostLawProductionPublication(void);

/* Owner-issued read-only collision view.  The pointer is borrowed until the
 * next level reset; NULL is returned whenever the publication is absent or
 * its live identity has drifted. */
sg_host_law_result_t SG_HostLawProductionCollisionAuthority(
	const sg_host_collision_authority_t **authority_out);

#endif /* SG_HOST_LAW_OWNER_H */
