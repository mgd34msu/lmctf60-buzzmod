#include "sg_static_visibility_publication.h"

#include <stdlib.h>

#include "sg_configuration_audit.h"

#define SG_STATIC_VISIBILITY_PUBLICATION_STATE UINT32_C(0x53565031)

struct sg_static_visibility_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	const sg_static_visibility_publication_t *self;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;
	uint64_t revision;
};

static int PublicationValid(
	const sg_static_visibility_publication_t *publication)
{
	return publication &&
		publication->state == SG_STATIC_VISIBILITY_PUBLICATION_STATE &&
		publication->state_inverse ==
			~SG_STATIC_VISIBILITY_PUBLICATION_STATE &&
		publication->self == publication && publication->authority &&
		publication->configuration && publication->semantics &&
		publication->visibility && publication->revision != 0U;
}

int SG_StaticVisibilityPublicationIssue(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint64_t revision,
	sg_static_visibility_publication_t **publication_out)
{
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_audit_result_t semantics_audit;
	sg_static_visibility_audit_result_t visibility_audit;
	sg_static_visibility_publication_t *publication;

	if (!authority || !configuration || !semantics || !visibility ||
		revision == 0U || !publication_out || *publication_out)
		return 0;
	if (!SG_ConfigurationAudit(authority, configuration,
			&configuration_audit) ||
		!SG_ConfigurationSemanticsAudit(authority, configuration, semantics,
			&semantics_audit) ||
		!SG_StaticVisibilityAudit(authority, configuration, semantics,
			visibility, &visibility_audit))
		return 0;
	publication = malloc(sizeof(*publication));
	if (!publication)
		return 0;
	publication->state = SG_STATIC_VISIBILITY_PUBLICATION_STATE;
	publication->state_inverse = ~SG_STATIC_VISIBILITY_PUBLICATION_STATE;
	publication->self = publication;
	publication->authority = authority;
	publication->configuration = configuration;
	publication->semantics = semantics;
	publication->visibility = visibility;
	publication->revision = revision;
	*publication_out = publication;
	return 1;
}

int SG_StaticVisibilityPublicationRead(
	const sg_static_visibility_publication_t *publication,
	const sg_host_collision_authority_t **authority_out,
	const sg_configuration_space_t **configuration_out,
	const sg_configuration_semantics_t **semantics_out,
	const sg_static_visibility_t **visibility_out, uint64_t *revision_out)
{
	if (!PublicationValid(publication) || !authority_out ||
		!configuration_out || !semantics_out || !visibility_out ||
		!revision_out)
		return 0;
	*authority_out = publication->authority;
	*configuration_out = publication->configuration;
	*semantics_out = publication->semantics;
	*visibility_out = publication->visibility;
	*revision_out = publication->revision;
	return 1;
}

void SG_StaticVisibilityPublicationDestroy(
	sg_static_visibility_publication_t *publication)
{
	if (!PublicationValid(publication))
		return;
	publication->state = 0U;
	publication->state_inverse = 0U;
	publication->self = NULL;
	free(publication);
}
