/* Engine-neutral ownership seam for the live bot localization provider. */
#ifndef SG_BOT_LOCALIZATION_OWNER_H
#define SG_BOT_LOCALIZATION_OWNER_H

#include "sg_compact_localization.h"

struct sg_strategy_runtime_bot_observation_owner_s;

int SG_BotLocalizationProviderSet(
	const sg_compact_localization_binding_t *binding);

const sg_compact_localization_observation_owner_t *
SG_BotLocalizationObservationOwner(void);

const struct sg_strategy_runtime_bot_observation_owner_s *
SG_BotLocalizationStrategyObservationOwner(void);

#endif
