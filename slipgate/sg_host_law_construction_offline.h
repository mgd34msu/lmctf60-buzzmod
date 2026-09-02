/* Offline-only audits over a sealed host-law construction snapshot. */
#ifndef SG_HOST_LAW_CONSTRUCTION_OFFLINE_H
#define SG_HOST_LAW_CONSTRUCTION_OFFLINE_H

#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_audit.h"
#include "sg_configuration_semantics.h"
#include "sg_host_law_publication.h"

/* These entry points intentionally live outside the game runtime object.
 * Each call copy-outs the authenticated BSP bytes, reparses an independent
 * temporary authority, verifies the downstream artifact's host-static terms,
 * and then invokes the accepted offline auditor. */
sg_host_law_result_t SG_HostLawConstructionConfigurationAudit(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	sg_configuration_audit_result_t *audit_out);sg_host_law_result_t SG_HostLawConstructionCompletenessProve(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	sg_bsp_completeness_result_t *proof_out);

#endif /* SG_HOST_LAW_CONSTRUCTION_OFFLINE_H */
