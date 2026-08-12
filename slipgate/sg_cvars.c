/* sg_cvars.c -- registry body; see sg_cvars.h. */
#include "g_local.h"
#include "slipgate/sg_cvars.h"

sg_cvars_t sg_cv;

void SG_CvarsInit(void)
{
	static qboolean done;

	if (done)
		return;
	done = true;
#define X(f, n, d) sg_cv.f = gi.cvar(n, d, 0);
	SG_CVAR_LIST(X)
#undef X
}
