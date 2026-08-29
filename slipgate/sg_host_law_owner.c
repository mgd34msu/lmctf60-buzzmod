#include "sg_host_law_owner.h"

#include <string.h>

static sg_host_law_publication_t *sg_host_law_production;

static sg_host_law_result_t HostUnavailable(void)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = SG_HOST_LAW_HOST_UNAVAILABLE;
	result.field = SG_HOST_LAW_FIELD_PMOVE_ABI;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	result.expected_bits = 1U;
	return result;
}

sg_host_law_result_t SG_HostLawProductionInstall(
	const sg_host_collision_authority_t *authority)
{
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationIssue(authority, &publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (sg_host_law_production)
		SG_HostLawPublicationDestroy(sg_host_law_production);
	sg_host_law_production = publication;
	return result;
}

void SG_HostLawProductionReset(void)
{
	if (!sg_host_law_production)
		return;
	SG_HostLawPublicationDestroy(sg_host_law_production);
	sg_host_law_production = NULL;
}

sg_host_law_result_t SG_HostLawProductionRevalidate(void)
{
	sg_host_law_result_t result;

	if (!sg_host_law_production)
		return HostUnavailable();
	result = SG_HostLawPublicationRevalidateProduction(sg_host_law_production);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostLawProductionReset();
		return result;
	}
	return result;
}

const sg_host_law_publication_t *SG_HostLawProductionPublication(void)
{
	return sg_host_law_production;
}
