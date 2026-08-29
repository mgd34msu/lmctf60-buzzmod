#ifndef SG_BSP_ENTITY_SEMANTICS_AUDIT_INTERNAL_H
#define SG_BSP_ENTITY_SEMANTICS_AUDIT_INTERNAL_H

#include "sg_bsp_entity_semantics_publication.h"

int SG_BspEntitySemanticsAuditOwned(
	const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_t **owned_out,
	sg_bsp_entity_semantics_audit_result_t *result_out);

#endif
