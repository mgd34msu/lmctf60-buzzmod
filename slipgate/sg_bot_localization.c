#include "../g_local.h"
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_localization.h"

#include <limits.h>
#include <string.h>

#define SG_BOT_LOCALIZATION_NUMERIC_DRIFT 0.5f

static sg_compact_localization_binding_t sg_bot_localization_binding;

static void InvalidateState(sg_bot_t *bot);
static void Localize(sg_bot_t *bot,
	sg_localization_observation_kind_t kind,
	const sg_host_pmove_request_t *pmove_request,
	const sg_host_pmove_result_t *pmove_result,
	const sg_host_pmove_state_observation_t *live_state);

static const sg_compact_localization_binding_t *Provider(void)
{
	return SG_CompactLocalizationBindingCurrent(&sg_bot_localization_binding) ?
		&sg_bot_localization_binding : NULL;
}

static int SubjectEqual(const sg_localization_subject_t *left,
	const sg_localization_subject_t *right)
{
	return left && right && left->reserved == 0U && right->reserved == 0U &&
		left->client_id != UINT32_MAX && right->client_id != UINT32_MAX &&
		left->spawn_generation != 0U &&
		left->client_id == right->client_id &&
		left->spawn_generation == right->spawn_generation;
}

static int SubjectValid(const sg_localization_subject_t *subject)
{
	return subject && subject->reserved == 0U &&
		subject->client_id != UINT32_MAX &&
		subject->spawn_generation != 0U;
}

static sg_bot_t *BotForEntity(const edict_t *entity)
{
	int index;

	if (!entity)
		return NULL;
	for (index = 0; index < SG_MAXBOTS; index++)
		if (sg_bots[index].active && sg_bots[index].ent == entity)
			return &sg_bots[index];
	return NULL;
}

static void InvalidateState(sg_bot_t *bot)
{
	if (bot)
		SG_BotLocalizationInvalidate(bot);
}

int SG_BotLocalizationProviderSet(
	const sg_compact_localization_binding_t *binding)
{
	int index;

	for (index = 0; index < SG_MAXBOTS; index++)
		SG_BotLocalizationReset(&sg_bots[index]);
	SG_CompactLocalizationUnbind(&sg_bot_localization_binding);
	if (!binding)
		return 1;
	if (!SG_CompactLocalizationBindingCurrent(binding))
		return 0;
	sg_bot_localization_binding = *binding;
	return 1;
}

static uint64_t FrameSequence(void)
{
	if (level.framenum < 0 || (uint64_t)level.framenum == UINT64_MAX)
		return 0U;
	return (uint64_t)level.framenum + 1U;
}

static uint64_t SampleTime(const sg_compact_localization_binding_t *binding,
	uint64_t frame_sequence)
{
	uint32_t frame_ms;

	if (!binding || !frame_sequence)
		return 0U;
	frame_ms = binding->identity.physics.frame_ms;
	if (frame_ms == 0U || frame_sequence > UINT64_MAX / frame_ms)
		return 0U;
	return frame_sequence * frame_ms;
}

static int StateMatchesRequest(const sg_compact_localized_state_t *state,
	const sg_host_pmove_request_t *request, uint64_t sample_time,
	const sg_compact_localization_binding_t *binding)
{
	uint32_t frame_ms;

	if (!state || !request || !binding)
		return 0;
	frame_ms = binding->identity.physics.frame_ms;
	return sample_time >= frame_ms &&
		state->localized_at_ms == sample_time - frame_ms &&
		memcmp(&request->state, &state->host_state,
			sizeof(request->state)) == 0 &&
		memcmp(&request->previous_state, &state->host_state,
			sizeof(request->previous_state)) == 0;
}

static void Bootstrap(sg_bot_t *bot,
	const sg_compact_localization_binding_t *binding)
{
	sg_host_pmove_state_observation_t observation;
	sg_host_law_result_t result;

	if (!bot || !binding)
		return;
	memset(&observation, 0, sizeof(observation));
	result = SG_HostLawProductionSubjectState(&binding->host_authority,
		&bot->localization_subject, &observation);
	if (result.status != SG_HOST_LAW_OK)
	{
		InvalidateState(bot);
		return;
	}
	Localize(bot, SG_LOCALIZATION_OBSERVATION_NEW_SPAWN, NULL, NULL,
		&observation);
}

void SG_BotLocalizationFrameBegin(sg_bot_t *bot)
{
	const sg_compact_localization_binding_t *binding;
	sg_localization_subject_t subject;
	sg_host_law_result_t host_result;
	int same_subject;

	if (!bot)
		return;
	binding = Provider();
	if (!bot->active || !bot->ent || !bot->ent->inuse ||
		!bot->ent->client || !binding)
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	memset(&subject, 0, sizeof(subject));
	host_result = SG_HostLawProductionSubject(&binding->host_authority,
		(uint32_t)bot->ent->s.number, &subject);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	same_subject = SubjectEqual(&bot->localization_subject, &subject);
	if (!same_subject)
	{
		SG_BotLocalizationReset(bot);
		if (bot->ent->deadflag != DEAD_NO)
			return;
		bot->localization_subject = subject;
		bot->localization_event = SG_LOCALIZATION_OBSERVATION_NEW_SPAWN;
	}
	if (bot->localized_state.rune_identity != 0U &&
		!SG_CompactLocalizationStateCurrent(binding,
			&bot->localization_subject, &bot->localized_state))
	{
		InvalidateState(bot);
		bot->localization_event = SG_LOCALIZATION_OBSERVATION_PRESENT;
	}
	if (bot->ent->deadflag != DEAD_NO)
		return;
	if (!SG_BotLocalizationCurrent(bot) &&
		bot->localization_event == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN)
		Bootstrap(bot, binding);
}

static void Localize(sg_bot_t *bot,
	sg_localization_observation_kind_t kind,
	const sg_host_pmove_request_t *pmove_request,
	const sg_host_pmove_result_t *pmove_result,
	const sg_host_pmove_state_observation_t *live_state)
{
	const sg_compact_localization_binding_t *binding = Provider();
	sg_compact_localization_sample_t sample;
	sg_compact_localized_state_t previous;
	sg_compact_localized_state_t localized;
	const sg_compact_localized_state_t *previous_ptr;
	sg_localization_status_t status;
	uint64_t frame_sequence = FrameSequence();
	uint64_t sample_time = SampleTime(binding, frame_sequence);
	int had_previous;

	if (!bot || !binding || !frame_sequence || !sample_time ||
		!SubjectValid(&bot->localization_subject))
	{
		InvalidateState(bot);
		return;
	}
	previous = bot->localized_state;
	had_previous = SG_BotLocalizationCurrent(bot) != NULL;
	previous_ptr = had_previous ? &previous : NULL;
	if (kind == SG_LOCALIZATION_OBSERVATION_PRESENT && previous_ptr &&
		(!pmove_request || !StateMatchesRequest(previous_ptr, pmove_request,
			sample_time, binding)))
		previous_ptr = NULL;
	if (kind != SG_LOCALIZATION_OBSERVATION_PRESENT &&
		kind != SG_LOCALIZATION_OBSERVATION_TELEPORTED &&
		kind != SG_LOCALIZATION_OBSERVATION_NEW_SPAWN)
		previous_ptr = NULL;

	memset(&sample, 0, sizeof(sample));
	sample.authenticated = 1U;
	sample.kind = kind;
	sample.subject = bot->localization_subject;
	sample.frame_sequence = frame_sequence;
	sample.observed_at_ms = sample_time;
	sample.authenticated_at_ms = sample_time;
	sample.pmove_result = pmove_result;
	sample.state_observation = live_state;
	if (kind == SG_LOCALIZATION_OBSERVATION_PRESENT && previous_ptr)
		sample.maximum_recovery_distance = SG_BOT_LOCALIZATION_NUMERIC_DRIFT;
	if (kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN)
		sample.life_reset.authorized = 1U;
	status = SG_CompactLocalizationObserve(binding, &sample, previous_ptr,
		&localized);
	if (status != SG_LOCALIZATION_OK)
	{
		InvalidateState(bot);
		bot->localization_event = SG_LOCALIZATION_OBSERVATION_PRESENT;
		return;
	}
	bot->localized_state = localized;
	bot->localization_event = SG_LOCALIZATION_OBSERVATION_PRESENT;
}

void SG_BotLocalizationObservePmove(edict_t *entity,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_result_t *result)
{
	const sg_compact_localization_binding_t *binding = Provider();
	sg_bot_t *bot = BotForEntity(entity);
	sg_localization_observation_kind_t kind;

	if (!bot || !request || !result)
		return;
	if (entity->deadflag != DEAD_NO)
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	if (!binding)
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	if (request->command.msec != binding->identity.physics.frame_ms)
	{
		SG_BotLocalizationInvalidate(bot);
		return;
	}
	kind = bot->localization_event;
	if ((result->state.pm_flags & PMF_TIME_TELEPORT) != 0 &&
		(bot->localized_state.host_state.pm_flags & PMF_TIME_TELEPORT) == 0)
		kind = SG_LOCALIZATION_OBSERVATION_TELEPORTED;
	Localize(bot, kind, request, result, NULL);
}

void SG_BotLocalizationFrameEnd(sg_bot_t *bot)
{
	const sg_compact_localized_state_t *current;
	uint64_t frame_sequence = FrameSequence();

	if (!bot || !bot->active || !bot->ent || bot->ent->deadflag != DEAD_NO)
		return;
	current = SG_BotLocalizationCurrent(bot);
	if (current && current->frame_sequence != frame_sequence)
		InvalidateState(bot);
}
