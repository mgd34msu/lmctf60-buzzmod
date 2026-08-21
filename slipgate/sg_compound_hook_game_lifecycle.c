#include "../g_local.h"

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_compound_hook_game.h"

sg_compound_guard_result_t SG_CompoundHookGameOrphan(sg_bot_t *bot)
{
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_result_t result;

	if (!bot || !bot->compound_hook_live.guard_owned ||
	    !bot->compound_hook_live.local_owned ||
	    !SG_CompoundHookGameHost(bot, &host))
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = SG_CompoundHookLiveOrphan(&bot->compound_hook_live, &host);
	return result.outcome == SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED ?
	    SG_COMPOUND_GUARD_OK : SG_COMPOUND_GUARD_HOST_ERROR;
}
