/* Live bot ownership of authenticated configuration/phase localization. */
#ifndef SG_BOT_LOCALIZATION_H
#define SG_BOT_LOCALIZATION_H

#include "sg_cell_phase_localization.h"

#include <limits.h>
#include <string.h>

struct edict_s;
struct sg_bot_s;

/* The upstream phase/model owner registers one borrowed runtime for the
 * current level and clears it before destroying any retained source. */
int SG_BotLocalizationProviderSet(const sg_cell_phase_runtime_t *runtime);

void SG_BotLocalizationFrameBegin(struct sg_bot_s *bot);
void SG_BotLocalizationFrameEnd(struct sg_bot_s *bot);

/* Called only from the authenticated bot branch of ClientThink with the exact
 * engine Pmove result, before the caller publishes that result to the body. */
void SG_BotLocalizationObservePmove(struct edict_s *entity,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_result_t *result);

static inline const sg_localized_player_state_t *SG_BotLocalizedStateCurrent(
	const sg_localization_subject_t *subject,
	const sg_localized_player_state_t *state)
{
	if (!subject || !state || subject->reserved != 0U ||
		subject->client_id == UINT32_MAX || subject->spawn_generation == 0U ||
		state->subject.reserved != 0U ||
		state->subject.client_id != subject->client_id ||
		state->subject.spawn_generation != subject->spawn_generation ||
		state->rune_identity == 0U || state->topology_revision == 0U ||
		state->frame_sequence == 0U ||
		!SG_DestinationPoseValid(&state->field_pose))
		return NULL;
	return state;
}

static inline int SG_BotLocalizedStateCell(
	const sg_localization_subject_t *subject,
	const sg_localized_player_state_t *state)
{
	state = SG_BotLocalizedStateCurrent(subject, state);
	return state && state->field_pose.phase.cell_id <= INT_MAX ?
		(int)state->field_pose.phase.cell_id : -1;
}

static inline void SG_BotLocalizedStateInvalidate(
	sg_localized_player_state_t *state,
	sg_localization_observation_kind_t *event)
{
	if (state)
		memset(state, 0, sizeof(*state));
	if (event)
		*event = SG_LOCALIZATION_OBSERVATION_PRESENT;
}

static inline void SG_BotLocalizedStateReset(
	sg_localization_subject_t *subject, sg_localized_player_state_t *state,
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

/* Temporary translation for legacy seed-indexed strategy/navigation callers.
 * The typed localized state remains the sole owner; this function stores no
 * nearest seed and fails closed unless its runtime cell is representable by
 * the currently installed legacy execution graph. */
#endif /* SG_BOT_LOCALIZATION_H */
