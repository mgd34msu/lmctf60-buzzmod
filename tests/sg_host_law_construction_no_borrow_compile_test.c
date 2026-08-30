#include "slipgate/sg_host_law_publication.h"

int main(void)
{
	sg_host_law_construction_view_t view = { 0 };

#ifdef SG_HOST_LAW_ATTEMPT_CONSTRUCTION_MUTATION
	/* This branch must not compile.  A construction view has no authority,
	 * world, source-byte, or collision-array pointer to cast away. */
	view.collision->world->planes[0].distance = 1.0f;
#else
	/* Pointer-free metadata is an ordinary caller-owned copy. */
	view.geometry.plane_count = 1U;
	view.host_static_identity.physics.gravity = 800.0f;
#endif
	return view.geometry.plane_count == 0U;
}
