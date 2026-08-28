/* Link seam for compiling the selected Yamagi Pmove source as a focused test. */
#include "common/header/common.h"
#include "client/header/client.h"
#include "client/sound/header/local.h"

client_state_t cl;
qboolean snd_is_underwater;

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(const char *format, ...)
{
	(void)format;
}
