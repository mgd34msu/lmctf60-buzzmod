/* Independent BSP/host completeness audit for sg_configuration_space. */
#ifndef SG_CONFIGURATION_AUDIT_H
#define SG_CONFIGURATION_AUDIT_H

#include "sg_configuration_space.h"

typedef enum sg_configuration_audit_code_e
{
	SG_CONFIGURATION_AUDIT_OK = 0,
	SG_CONFIGURATION_AUDIT_INVALID_ARGUMENT,
	SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE,
	SG_CONFIGURATION_AUDIT_OMITTED_CELL,
	SG_CONFIGURATION_AUDIT_INVALID_CELL,
	SG_CONFIGURATION_AUDIT_OMITTED_PORTAL,
	SG_CONFIGURATION_AUDIT_INVENTED_PORTAL,
	SG_CONFIGURATION_AUDIT_HOST_CELL_DISAGREEMENT,
	SG_CONFIGURATION_AUDIT_HOST_PORTAL_DISAGREEMENT,
	SG_CONFIGURATION_AUDIT_OVERFLOW,
	SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY
} sg_configuration_audit_code_t;

typedef struct sg_configuration_audit_result_s
{
	sg_configuration_audit_code_t code;
	uint32_t record;
	uint32_t proved_cells;
	uint32_t proved_portals;
	uint32_t omitted_cells;
	uint32_t omitted_portals;
	uint32_t invented_portals;
	uint64_t boundary_witnesses;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint32_t lattice_maximum_binary_shift;
} sg_configuration_audit_result_t;

int SG_ConfigurationAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_configuration_audit_result_t *result_out);
const char *SG_ConfigurationAuditCodeString(sg_configuration_audit_code_t code);

#endif
