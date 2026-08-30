#include "../g_local.h"
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_localization.h"
#include "sg_host_engine_pmove.h"
#include "sg_route_dither.h"

#include <limits.h>
#include <string.h>

#define SG_BOT_LOCALIZATION_REPLAY_SUBSTEPS 8U
#define SG_BOT_LOCALIZATION_REPLAY_TRACES 4096U
#define SG_BOT_LOCALIZATION_NUMERIC_DRIFT 0.5f

/* These arrays cover the exact selected-engine frame/substep contract and its
 * audited trace-callback bound.  They are deliberately compile-time products:
 * a host ABI change cannot silently shrink the live localization workspace. */
_Static_assert(SG_BOT_LOCALIZATION_REPLAY_SUBSTEPS >=
	SG_HOST_ENGINE_FRAME_MS / SG_HOST_ENGINE_PMOVE_SUBSTEP_MS,
	"bot localization replay substeps cover one engine frame");
_Static_assert(SG_BOT_LOCALIZATION_REPLAY_TRACES >=
	SG_HOST_ENGINE_PMOVE_REPLAY_TRACE_LIMIT,
	"bot localization replay traces cover one engine frame");

static const sg_cell_phase_runtime_t *sg_bot_localization_runtime;
static sg_host_pmove_substep_t
	sg_bot_localization_substeps[SG_BOT_LOCALIZATION_REPLAY_SUBSTEPS];
static sg_host_pmove_trace_t
	sg_bot_localization_traces[SG_BOT_LOCALIZATION_REPLAY_TRACES];

static void InvalidateState(sg_bot_t *bot);
static void Localize(sg_bot_t *bot,
	sg_localization_observation_kind_t kind,
	const sg_host_pmove_request_t *pmove_request,
	const sg_host_pmove_result_t *pmove_result,
	const sg_host_pmove_state_observation_t *live_state);

static void Bootstrap(sg_bot_t *bot)
{
	sg_host_pmove_state_observation_t observation;
	sg_host_law_result_t result;

	memset(&observation, 0, sizeof(observation));
	result = SG_HostLawProductionSubjectState(
		&sg_bot_localization_runtime->host_authority,
		&bot->localization_subject, &observation);
	if (result.status != SG_HOST_LAW_OK)
	{
		InvalidateState(bot);
		return;
	}
	Localize(bot, bot->localization_event, NULL, NULL, &observation);
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

int SG_BotLocalizationProviderSet(const sg_cell_phase_runtime_t *runtime)
{
	int index;

	for (index = 0; index < SG_MAXBOTS; index++)
		SG_BotLocalizationReset(&sg_bots[index]);
	sg_bot_localization_runtime = NULL;
	if (!runtime)
		return 1;
	if (!SG_CellPhaseRuntimeCurrent(runtime))
		return 0;
	if (!SG_Rune() || SG_Rune()->hdr.num_seeds <= 0 ||
		runtime->locator->runtime_cell_count !=
			(uint32_t)SG_Rune()->hdr.num_seeds)
		return 0;
	sg_bot_localization_runtime = runtime;
	return 1;
}

void SG_BotLocalizationFrameBegin(sg_bot_t *bot)
{
	sg_localization_subject_t subject;
	sg_host_law_result_t host_result;
	int same_subject;

	if (!bot)
		return;
	if (!bot->active || !bot->ent || !bot->ent->inuse ||
		!bot->ent->client ||
		!sg_bot_localization_runtime ||
		!SG_CellPhaseRuntimeCurrent(sg_bot_localization_runtime))
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	memset(&subject, 0, sizeof(subject));
	host_result = SG_HostLawProductionSubject(
		&sg_bot_localization_runtime->host_authority,
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
		!SG_CellPhaseLocalizedStateCurrent(sg_bot_localization_runtime,
			&bot->localization_subject, &bot->localized_state))
	{
		InvalidateState(bot);
		bot->localization_event = SG_LOCALIZATION_OBSERVATION_PRESENT;
	}
	/* The terminal state remains current through Think_Dead's one death event.
	 * That consumer resets the life immediately after learning from the cell. */
	if (bot->ent->deadflag != DEAD_NO)
		return;
	if (!SG_BotLocalizationCurrent(bot))
		Bootstrap(bot);
}

static uint64_t FrameSequence(void)
{
	if (level.framenum < 0 || (uint64_t)level.framenum == UINT64_MAX)
		return 0U;
	return (uint64_t)level.framenum + 1U;
}

static uint64_t SampleTime(uint64_t frame_sequence)
{
	uint32_t frame_ms;

	if (!frame_sequence || !sg_bot_localization_runtime ||
		!sg_bot_localization_runtime->locator ||
		!sg_bot_localization_runtime->locator->authority)
		return 0U;
	frame_ms = sg_bot_localization_runtime->locator->authority->
		identity.physics.frame_ms;
	if (frame_ms == 0U || frame_sequence > UINT64_MAX / frame_ms)
		return 0U;
	return frame_sequence * frame_ms;
}

static int StateMatchesRequest(const sg_localized_player_state_t *state,
	const sg_host_pmove_request_t *request, uint64_t sample_time)
{
	uint32_t frame_ms;

	if (!state || !request || !sg_bot_localization_runtime ||
		!sg_bot_localization_runtime->locator ||
		!sg_bot_localization_runtime->locator->authority)
		return 0;
	frame_ms = sg_bot_localization_runtime->locator->authority->
		identity.physics.frame_ms;
	return sample_time >= frame_ms &&
		state->field_pose.sample_time_ms == sample_time - frame_ms &&
		memcmp(&request->state, &state->host_state,
			sizeof(request->state)) == 0 &&
		memcmp(&request->previous_state, &state->host_state,
			sizeof(request->previous_state)) == 0;
}

static void RecordTransition(sg_bot_t *bot,
	const sg_localized_player_state_t *previous,
	const sg_localized_player_state_t *current)
{
	uint32_t from;
	uint32_t to;

	if (!bot || !previous || !current)
		return;
	from = previous->field_pose.phase.cell_id;
	to = current->field_pose.phase.cell_id;
	if (from == to || from > INT_MAX || to > INT_MAX)
		return;
	bot->prev_seed = (int)from;
	bot->prev_seed_time = level.time;
	bot->dither_salt = SG_RouteDitherNext(bot->dither_salt,
		(int)from, (int)to);
}

static void Localize(sg_bot_t *bot,
	sg_localization_observation_kind_t kind,
	const sg_host_pmove_request_t *pmove_request,
	const sg_host_pmove_result_t *pmove_result,
	const sg_host_pmove_state_observation_t *live_state)
{
	sg_localization_observation_t observation;
	sg_localization_environment_t environment;
	sg_localization_request_t request;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t localized;
	const sg_localized_player_state_t *previous_ptr;
	sg_localization_status_t status;
	uint64_t frame_sequence = FrameSequence();
	uint64_t sample_time = SampleTime(frame_sequence);
	int had_previous;

	if (!bot || !sg_bot_localization_runtime || !frame_sequence ||
		!sample_time || !SubjectValid(&bot->localization_subject))
	{
		InvalidateState(bot);
		return;
	}
	previous = bot->localized_state;
	had_previous = SG_BotLocalizationCurrent(bot) != NULL;
	previous_ptr = had_previous ? &previous : NULL;
	if (kind == SG_LOCALIZATION_OBSERVATION_PRESENT && previous_ptr &&
		pmove_request &&
		!StateMatchesRequest(previous_ptr, pmove_request, sample_time))
		previous_ptr = NULL;
	if (kind != SG_LOCALIZATION_OBSERVATION_PRESENT &&
		kind != SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT)
		previous_ptr = NULL;

	memset(&observation, 0, sizeof(observation));
	observation.authenticated = 1U;
	observation.kind = kind;
	if (pmove_result)
		observation.stance =
			(pmove_result->state.pm_flags & PMF_DUCKED) ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	else if (live_state)
		observation.stance =
			(live_state->state.pm_flags & PMF_DUCKED) ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	else
		observation.stance = previous_ptr ? previous_ptr->stance :
			SG_RUNE_STANCE_STANDING;
	observation.subject = bot->localization_subject;
	observation.rune_identity = sg_bot_localization_runtime->rune_identity;
	observation.topology_revision =
		sg_bot_localization_runtime->topology_revision;
	observation.frame_sequence = frame_sequence;
	observation.observed_at_ms = sample_time;
	observation.authenticated_at_ms = sample_time;
	if (pmove_result)
	{
		memcpy(observation.position, pmove_result->origin,
			sizeof(observation.position));
		memcpy(observation.velocity, pmove_result->velocity,
			sizeof(observation.velocity));
		observation.host_state = pmove_result->state;
	}
	else if (live_state)
	{
		memcpy(observation.position, live_state->origin,
			sizeof(observation.position));
		memcpy(observation.velocity, live_state->velocity,
			sizeof(observation.velocity));
		observation.host_state = live_state->state;
	}

	memset(&environment, 0, sizeof(environment));
	environment.authenticated = 1U;
	environment.rune_identity = observation.rune_identity;
	environment.topology_revision = observation.topology_revision;
	environment.frame_sequence = frame_sequence;
	environment.sampled_at_ms = sample_time;
	environment.authenticated_at_ms = sample_time;
	if (previous_ptr && kind == SG_LOCALIZATION_OBSERVATION_PRESENT)
	{
		environment.pmove_request = pmove_request;
		environment.replay_substeps = sg_bot_localization_substeps;
		environment.replay_substep_capacity =
			SG_BOT_LOCALIZATION_REPLAY_SUBSTEPS;
		environment.replay_traces = sg_bot_localization_traces;
		environment.replay_trace_capacity =
			SG_BOT_LOCALIZATION_REPLAY_TRACES;
	}

	memset(&request, 0, sizeof(request));
	request.expected_subject = bot->localization_subject;
	request.now_ms = sample_time;
	request.minimum_frame_sequence = frame_sequence;
	request.max_observation_age_ms = 0U;
	request.previous = previous_ptr;
	if (previous_ptr && kind == SG_LOCALIZATION_OBSERVATION_PRESENT)
		request.maximum_recovery_distance =
			SG_BOT_LOCALIZATION_NUMERIC_DRIFT;
	else if (previous_ptr &&
		kind == SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT)
		request.maximum_temporary_absence_ms =
			sg_bot_localization_runtime->locator->authority->
			identity.physics.frame_ms;
	if (!SG_CellPhaseLocalize(sg_bot_localization_runtime, &request,
			&observation, &environment, &localized, &status))
	{
		InvalidateState(bot);
		bot->localization_event = SG_LOCALIZATION_OBSERVATION_PRESENT;
		return;
	}
	if (previous_ptr)
		RecordTransition(bot, previous_ptr, &localized);
	bot->localized_state = localized;
	bot->localization_event = SG_LOCALIZATION_OBSERVATION_PRESENT;
}

void SG_BotLocalizationObservePmove(edict_t *entity,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_result_t *result)
{
	sg_bot_t *bot = BotForEntity(entity);
	sg_localization_observation_kind_t kind;

	if (!bot || !request || !result)
		return;
	if (entity->deadflag != DEAD_NO)
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	if (!sg_bot_localization_runtime ||
		!SG_CellPhaseRuntimeCurrent(sg_bot_localization_runtime))
	{
		SG_BotLocalizationReset(bot);
		return;
	}
	if (request->command.msec !=
		sg_bot_localization_runtime->locator->authority->
			identity.physics.frame_ms)
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
	const sg_localized_player_state_t *current;
	uint64_t frame_sequence = FrameSequence();

	if (!bot || !bot->active || !bot->ent || bot->ent->deadflag != DEAD_NO)
		return;
	current = SG_BotLocalizationCurrent(bot);
	if (!current || current->frame_sequence == frame_sequence)
		return;
	Localize(bot, SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT,
		NULL, NULL, NULL);
}
