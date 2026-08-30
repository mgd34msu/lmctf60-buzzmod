/* Offline owner for constructing an external-force publication. */
#ifndef SG_EXTERNAL_FORCE_BUILDER_H
#define SG_EXTERNAL_FORCE_BUILDER_H

#include "sg_external_force_publication.h"

/* The caller supplies accepted downstream publications and the caller-owned
 * collision parse, but no construction handle.  This boundary obtains the
 * current production construction, consumes it synchronously, and destroys it
 * before returning.  It is offline-only and must not enter the game DLL. */
int SG_ExternalForceProductionBuild(
	const sg_external_force_source_t *source_without_construction,
	sg_external_force_publication_t **publication_out,
	sg_external_force_audit_result_t *audit_out);

#endif /* SG_EXTERNAL_FORCE_BUILDER_H */
