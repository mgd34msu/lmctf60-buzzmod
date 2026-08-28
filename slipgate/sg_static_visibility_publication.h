/* Authenticated publication of one exact static-visibility source set. */
#ifndef SG_STATIC_VISIBILITY_PUBLICATION_H
#define SG_STATIC_VISIBILITY_PUBLICATION_H

#include <stdint.h>

#include "sg_static_visibility.h"

typedef struct sg_static_visibility_publication_s
	sg_static_visibility_publication_t;

int SG_StaticVisibilityPublicationIssue(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint64_t revision,
	sg_static_visibility_publication_t **publication_out);

int SG_StaticVisibilityPublicationRead(
	const sg_static_visibility_publication_t *publication,
	const sg_host_collision_authority_t **authority_out,
	const sg_configuration_space_t **configuration_out,
	const sg_configuration_semantics_t **semantics_out,
	const sg_static_visibility_t **visibility_out, uint64_t *revision_out);

void SG_StaticVisibilityPublicationDestroy(
	sg_static_visibility_publication_t *publication);

#endif /* SG_STATIC_VISIBILITY_PUBLICATION_H */
