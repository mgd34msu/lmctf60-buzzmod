/* Owner-private bot hook consumer seams. */
#ifndef SG_HOST_LAW_OWNER_INTERNAL_H
#define SG_HOST_LAW_OWNER_INTERNAL_H

#include "sg_host_law_owner.h"
sg_host_law_result_t SG_HostLawProductionHookTouch(uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_step_t *step_out);
sg_host_law_result_t SG_HostLawProductionHookPullVelocity(
	uint32_t subject_index, uint32_t hook_index, vec3_t velocity,
	int *rope_length_out);

#endif /* SG_HOST_LAW_OWNER_INTERNAL_H */
