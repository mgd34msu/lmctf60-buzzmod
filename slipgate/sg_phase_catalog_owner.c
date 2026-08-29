#include "sg_phase_catalog_owner.h"

#include "sg_phase_catalog_internal.h"

#include <string.h>

int SG_PhaseCatalogPublicationBuild(
	sg_mechanism_capability_owner_t *capability_owner,
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_mechanism_capability_set_t *accepted_capabilities,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_error_t *error_out,
	sg_phase_catalog_audit_result_t *audit_out)
{
	sg_phase_mover_support_provider_owner_t *provider_owner = NULL;
	sg_phase_mover_support_provider_t *provider = NULL;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_source_t source;
	sg_phase_catalog_error_t provider_error;
	sg_phase_catalog_error_t catalog_error;
	sg_phase_catalog_audit_result_t audit;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (audit_out)
		memset(audit_out, 0, sizeof(*audit_out));
	if (!capability_owner || !publication_out || *publication_out || !authority ||
		!semantics || !accepted_capabilities)
	{
		SG_PhaseCatalogSetError(error_out,
			!publication_out ? SG_PHASE_CATALOG_ERROR_INVALID_ARGUMENT :
			SG_PHASE_CATALOG_ERROR_INVALID_SOURCE, 0U);
		if (audit_out)
			audit_out->code = SG_PHASE_CATALOG_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	*publication_out = NULL;
	memset(&provider_error, 0, sizeof(provider_error));
	if (!SG_PhaseMoverSupportProviderOwnerCreate(&provider_owner) ||
		!SG_PhaseMoverSupportProviderBuild(provider_owner, capability_owner,
		semantics, accepted_capabilities,
		&provider, &provider_error))
	{
		if (error_out)
			*error_out = provider_error;
		SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
		return 0;
	}
	memset(&source, 0, sizeof(source));
	source.authority = authority;
	source.configuration = configuration;
	source.semantics = semantics;
	source.mover_support_owner = provider_owner;
	source.mover_support_provider = provider;
	memset(&catalog_error, 0, sizeof(catalog_error));
	if (!SG_PhaseCatalogBuild(&source, &catalog, &catalog_error))
	{
		if (error_out)
			*error_out = catalog_error;
		SG_PhaseMoverSupportProviderDestroy(provider_owner, provider);
		SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
		return 0;
	}
	memset(&audit, 0, sizeof(audit));
	if (!SG_PhaseCatalogPublicationIssue(&source, catalog, publication_out,
		&audit))
	{
		if (audit_out)
			*audit_out = audit;
		SG_PhaseCatalogDestroy(catalog);
		SG_PhaseMoverSupportProviderDestroy(provider_owner, provider);
		SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
		if (error_out)
		{
			error_out->code = SG_PHASE_CATALOG_ERROR_AUDIT_REJECTED;
			error_out->source_index = audit.record;
		}
		return 0;
	}
	if (audit_out)
		*audit_out = audit;
	/* Issue copied all catalog bytes into the immutable publication. */
	SG_PhaseCatalogDestroy(catalog);
	SG_PhaseMoverSupportProviderDestroy(provider_owner, provider);
	SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
	return 1;
}
