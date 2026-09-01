/* Live bot ownership of authenticated configuration/phase localization. */
#ifndef SG_BOT_LOCALIZATION_H
#define SG_BOT_LOCALIZATION_H

/* g_local.h defines world as a convenience macro.  The compact spatial API
 * also has a parameter named world, so hide the game macro while reading the
 * reusable compact headers and restore it for gameplay callers. */
#ifdef world
#define SG_BOT_LOCALIZATION_RESTORE_WORLD_MACRO 1
#undef world
#endif
#include "sg_bot_localization_owner.h"
#ifdef SG_BOT_LOCALIZATION_RESTORE_WORLD_MACRO
#define world (&g_edicts[0])
#undef SG_BOT_LOCALIZATION_RESTORE_WORLD_MACRO
#endif

#include <limits.h>
#include <string.h>

struct edict_s;
struct sg_bot_s;
struct sg_strategy_runtime_bot_observation_s;

/* The compact artifact owner installs one accepted borrowed binding for this
 * level and clears it before replacing or destroying the model it borrows.
 * Install copies the binding, not the model. A NULL binding uninstalls it. */
/* Bot-only host observation authority for strategy/tactic field queries.
 * Issue returns a one-use opaque capability for the exact current localized
 * subject and frame. Human clients never enter this owner. */
const struct sg_strategy_runtime_bot_observation_s *
SG_BotLocalizationStrategyObservationIssue(struct sg_bot_s *bot,
	const sg_compact_localized_state_t *localized);

void SG_BotLocalizationFrameBegin(struct sg_bot_s *bot);
void SG_BotLocalizationFrameEnd(struct sg_bot_s *bot);

/* Called only from the authenticated bot branch of ClientThink with the exact
 * engine Pmove result, before the caller publishes that result to the body. */
void SG_BotLocalizationObservePmove(struct edict_s *entity,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_result_t *result);

static inline const sg_compact_localized_state_t *SG_BotLocalizedStateCurrent(
	const sg_localization_subject_t *subject,
	const sg_compact_localized_state_t *state)
{
	if (!subject || !state || subject->reserved != 0U ||
		subject->client_id == UINT32_MAX || subject->spawn_generation == 0U ||
		state->subject.reserved != 0U ||
		state->subject.client_id != subject->client_id ||
		state->subject.spawn_generation != subject->spawn_generation ||
		state->rune_identity == 0U || state->topology_revision == 0U ||
		state->frame_sequence == 0U || state->localized_at_ms == 0U ||
		state->valid != 1U ||
		state->location.cell.value == SG_RUNE_COMPACT_INDEX_NONE)
		return NULL;
	return state;
}

static inline int SG_BotLocalizedStateCell(
	const sg_localization_subject_t *subject,
	const sg_compact_localized_state_t *state)
{
	state = SG_BotLocalizedStateCurrent(subject, state);
	return state && state->location.cell.value <= INT_MAX ?
		(int)state->location.cell.value : -1;
}

static inline void SG_BotLocalizedStateInvalidate(
	sg_compact_localized_state_t *state,
	sg_localization_observation_kind_t *event)
{
	if (state)
	{
		memset(state, 0, sizeof(*state));
		state->location.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	}
	if (event)
		*event = SG_LOCALIZATION_OBSERVATION_PRESENT;
}

static inline void SG_BotLocalizedStateReset(
	sg_localization_subject_t *subject, sg_compact_localized_state_t *state,
	sg_localization_observation_kind_t *event)
{
	if (subject)
		memset(subject, 0, sizeof(*subject));
	SG_BotLocalizedStateInvalidate(state, event);
}

/* These field-only adapters deliberately expand at a call site where sg_bot_s
 * is complete. They add no second current-position storage or link-time test
 * dependency. */
#define SG_BotLocalizationCurrent(bot) \
	SG_BotLocalizedStateCurrent(&(bot)->localization_subject, \
		&(bot)->localized_state)
#define SG_BotLocalizationCell(bot) \
	SG_BotLocalizedStateCell(&(bot)->localization_subject, \
		&(bot)->localized_state)
#define SG_BotLocalizationInvalidate(bot) \
	SG_BotLocalizedStateInvalidate(&(bot)->localized_state, \
		&(bot)->localization_event)
#define SG_BotLocalizationReset(bot) \
	SG_BotLocalizedStateReset(&(bot)->localization_subject, \
		&(bot)->localized_state, &(bot)->localization_event)

/* These macros expose the authenticated compact cell. They never consult a
 * seed, link, or nearest-point fallback. */
#endif /* SG_BOT_LOCALIZATION_H */
