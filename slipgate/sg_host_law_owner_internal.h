/* Owner-private bot hook consumer seams. */
#ifndef SG_HOST_LAW_OWNER_INTERNAL_H
#define SG_HOST_LAW_OWNER_INTERNAL_H

#include "sg_host_law_owner.h"
/* Runtime activation is intentionally absent until the downstream artifact
 * cutover can provide a genuinely opaque acceptance capability. */
sg_host_law_result_t SG_HostLawProductionHookTouch(uint32_t subject_index,
	uint32_t target_index, int32_t surface_flags, int attached, uint32_t frame,
	uint32_t last_damage_frame, sg_host_hook_step_t *step_out);
sg_host_law_result_t SG_HostLawProductionHookPullVelocity(
	uint32_t subject_index, const vec3_t start, const vec3_t bite,
	vec3_t velocity, int *rope_length_out);

#endif /* SG_HOST_LAW_OWNER_INTERNAL_H */
