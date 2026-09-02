#include "../g_local.h"

#include "sg_bot_cvars.h"

sg_cvars_t sg_cv;

void SG_CvarsInit(void)
{
#define X(field, name, value) \
	if (!sg_cv.field) sg_cv.field = gi.cvar(name, value, 0);
	SG_CVAR_LIST(X)
#undef X
}
