/* Production-owner construction seam.  Runtime consumers include only the
 * public read/evaluate interface and cannot issue or retire publications. */
#ifndef SG_HOST_LAW_PUBLICATION_PRIVATE_H
#define SG_HOST_LAW_PUBLICATION_PRIVATE_H

#include "sg_host_law_publication.h"

sg_host_law_result_t SG_HostLawPublicationOwnerIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_publication_t **publication_out);
sg_host_law_result_t SG_HostLawPublicationOwnerIssueStatic(
	const sg_host_static_identity_t *identity,
	sg_host_law_publication_t **publication_out);
void SG_HostLawPublicationOwnerDestroy(
	sg_host_law_publication_t *publication);
sg_host_law_result_t SG_HostLawPublicationOwnerHookTouch(
	const sg_host_law_publication_t *publication, uint32_t target_index,
	int32_t surface_flags, int attached, uint32_t frame,
	uint32_t last_damage_frame, sg_host_hook_step_t *step_out);

#endif /* SG_HOST_LAW_PUBLICATION_PRIVATE_H */
