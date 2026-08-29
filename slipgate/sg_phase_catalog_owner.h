/* Production barrier for issuing an immutable phase catalog publication. */
#ifndef SG_PHASE_CATALOG_OWNER_H
#define SG_PHASE_CATALOG_OWNER_H

#include "sg_phase_catalog.h"

/* The owner consumes the already accepted mechanism result at the same
 * construction barrier as configuration semantics.  It never accepts caller
 * supplied phase, support, mechanism, or verifier rows.  On success the
 * returned publication owns every byte required by its view and no longer
 * borrows the source/provider objects. */
int SG_PhaseCatalogPublicationBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_mechanism_capability_set_t *accepted_capabilities,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_audit_result_t *audit_out);

#endif /* SG_PHASE_CATALOG_OWNER_H */
