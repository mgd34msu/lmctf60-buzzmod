#ifndef SG_BOT_LOCALIZATION_FIXTURE_H
#define SG_BOT_LOCALIZATION_FIXTURE_H

#include "../../slipgate/sg_bot.h"

static inline void SG_TestBotLocalizationCellSet(sg_bot_t *bot, int cell)
{
	SG_BotLocalizationReset(bot);
	if (cell < 0)
		return;
	bot->localization_subject.client_id = 1U;
	bot->localization_subject.spawn_generation = 1U;
	bot->localized_state.subject = bot->localization_subject;
	bot->localized_state.rune_identity = 1U;
	bot->localized_state.topology_revision = 1U;
	bot->localized_state.frame_sequence = 1U;
	bot->localized_state.localized_at_ms = 1U;
	bot->localized_state.location.cell.value = (uint32_t)cell;
	bot->localized_state.valid = 1U;
}

#endif
