/* sg_cvars.c -- registry body; see sg_cvars.h. */
#include "g_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"

sg_cvars_t sg_cv;

void SG_CvarsInit(void)
{
	SG_HooksInit();     /* cvar registration may be the first host touch */

	static qboolean done;

	if (done)
		return;
	done = true;
#define X(f, n, d) sg_cv.f = sg_host.cvar(n, d, 0);
	SG_CVAR_LIST(X)
#undef X
}
