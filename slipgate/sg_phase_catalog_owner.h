/* Owned phase catalog construction and publication. */
#ifndef SG_PHASE_CATALOG_OWNER_H
#define SG_PHASE_CATALOG_OWNER_H

#include "sg_phase_catalog.h"

/* This implementation consumes an accepted mechanism result together with
 * configuration semantics. It never accepts caller-supplied phase, support,
 * mechanism, or verifier rows. On success the publication owner retains every
 * authoritative byte until the handle is destroyed or the owner is torn
 * down. */
int SG_PhaseCatalogPublicationBuild(
	sg_phase_catalog_publication_owner_t *publication_owner,
	sg_mechanism_capability_owner_t *capability_owner,
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_mechanism_capability_set_t *accepted_capabilities,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_check_result_t *check_out);

#endif /* SG_PHASE_CATALOG_OWNER_H */
