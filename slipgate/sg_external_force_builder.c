#include "sg_external_force_builder.h"

#include <string.h>

#include "sg_host_law_owner.h"

int SG_ExternalForceProductionBuild(
	const sg_external_force_source_t *source_without_construction,
	sg_external_force_publication_t **publication_out,
	sg_external_force_audit_result_t *audit_out)
{
	sg_external_force_source_t bound_source;
	sg_host_law_construction_t *construction = NULL;
	sg_host_law_result_t construction_result;
	int issued;

	if (!source_without_construction ||
		source_without_construction->construction || !publication_out ||
		*publication_out || !audit_out ||
		!source_without_construction->collision_authority)
	{
		if (audit_out)
		{
			memset(audit_out, 0, sizeof(*audit_out));
			audit_out->code = SG_EXTERNAL_FORCE_AUDIT_INVALID_ARGUMENT;
			audit_out->record = UINT32_MAX;
		}
		return 0;
	}
	construction_result = SG_HostLawProductionConstructionIssue(
		source_without_construction->collision_authority, &construction);
	if (construction_result.status != SG_HOST_LAW_OK || !construction)
	{
		memset(audit_out, 0, sizeof(*audit_out));
		audit_out->code = SG_EXTERNAL_FORCE_AUDIT_AUTHORITY_REJECTED;
		audit_out->record = construction_result.element;
		return 0;
	}
	bound_source = *source_without_construction;
	bound_source.construction = construction;
	issued = SG_ExternalForcePublicationIssue(&bound_source, publication_out,
		audit_out);
	SG_HostLawConstructionDestroy(construction);
	return issued;
}
