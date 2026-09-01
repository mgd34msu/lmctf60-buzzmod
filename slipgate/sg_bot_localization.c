#include "../g_local.h"
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_localization.h"
#include "sg_belief_runtime.h"
#include "sg_strategy_runtime_bridge_private.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SG_BOT_LOCALIZATION_NUMERIC_DRIFT 0.5f

struct sg_compact_localization_observation_s
{
	uint64_t serial;
};

struct sg_strategy_runtime_bot_observation_s
{
	uint64_t serial;
};

typedef struct sg_bot_localization_observation_issuer_s
{
	struct sg_compact_localization_observation_s capability;
	sg_compact_localization_observation_view_t view;
	uint64_t serial;
	uint8_t active;
	uint8_t reserved[7];
} sg_bot_localization_observation_issuer_t;

typedef struct sg_bot_strategy_observation_issuer_s
{
	struct sg_strategy_runtime_bot_observation_s capability;
	sg_strategy_runtime_bot_observation_view_t view;
	sg_bot_t *bot;
	const sg_compact_localized_state_t *localized;
	uint64_t serial;
	uint8_t active;
	uint8_t reserved[7];
} sg_bot_strategy_observation_issuer_t;

typedef struct sg_bot_localization_provider_s
{
	sg_compact_localization_binding_t binding;
	sg_compact_localization_scratch_t scratch;
	uint32_t *belief_candidates;
	sg_rune_compact_cell_index_t *belief_index_candidates;
	sg_bot_localization_observation_issuer_t issuer;
	sg_bot_strategy_observation_issuer_t strategy_issuer;
} sg_bot_localization_provider_t;

static sg_bot_localization_provider_t sg_bot_localization_provider;

static void InvalidateState(sg_bot_t *bot);
static void Localize(sg_bot_t *bot,
	sg_localization_observation_kind_t kind,
	const sg_host_pmove_request_t *pmove_request,
	const sg_host_pmove_result_t *pmove_result,
	const sg_host_pmove_state_observation_t *live_state);

static sg_localization_status_t ValidateIssuedObservation(void *context,
	const sg_host_law_runtime_authority_t *authority,
	const sg_compact_localization_observation_t *observation,
	sg_compact_localization_observation_view_t *view_out)
{
	sg_bot_localization_observation_issuer_t *issuer = context;
	const struct sg_compact_localization_observation_s *capability = observation;

	if (!issuer || issuer != &sg_bot_localization_provider.issuer ||
		!authority || !capability || !view_out || issuer->active != 1U ||
		capability != &issuer->capability || capability->serial == 0U ||
		capability->serial != issuer->serial ||
		authority->epoch == 0U || authority->epoch !=
			issuer->view.host_authority_epoch ||
		authority->epoch_complement != ~authority->epoch)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	*view_out = issuer->view;
	issuer->active = 0U;
	return SG_LOCALIZATION_OK;
}

const sg_compact_localization_observation_owner_t *
SG_BotLocalizationObservationOwner(void)
{
	static const sg_compact_localization_observation_owner_t owner = {
		&sg_bot_localization_provider.issuer,
		ValidateIssuedObservation
	};

	return &owner;
}

static int CompactBeliefFloatToQ8(float value, int32_t *output)
{
	const double scaled = (double)value * 8.0;

	if (!output || !isfinite(value) || !isfinite(scaled) ||
		scaled < (double)INT32_MIN || scaled > (double)INT32_MAX ||
		floor(scaled) != scaled)
		return 0;
	*output = (int32_t)scaled;
	return 1;
}

static int CompactBeliefPointToQ8(const float position[3],
	sg_rune_q8_vec3_t *point)
{
	uint32_t axis;

	if (!position || !point)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!CompactBeliefFloatToQ8(position[axis], &point->value[axis]))
			return 0;
	return 1;
}

static int CompactBeliefProviderCurrent(void *context,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity, uint64_t rune_identity,
	uint64_t topology_revision, uint64_t generation)
{
	const sg_compact_localization_binding_t *binding =
		(const sg_compact_localization_binding_t *)context;

	return binding == &sg_bot_localization_provider.binding &&
		SG_CompactLocalizationBindingCurrent(binding) &&
		binding->model == model && model && identity == &model->identity &&
		binding->rune_identity == rune_identity &&
		binding->topology_revision == topology_revision &&
		binding->host_authority.epoch != 0U &&
		binding->host_authority.epoch == generation;
}

static int CompactBeliefLocate(void *context,
	const sg_rune_compact_model_t *model, const float position[3],
	sg_belief_runtime_cell_state_t *cell_out)
{
	const sg_compact_localization_binding_t *binding =
		(const sg_compact_localization_binding_t *)context;
	sg_rune_compact_spatial_error_t spatial_error;
	sg_rune_compact_location_t location;
	sg_rune_vec3_t world_point;
	uint32_t candidate_count = 0U;
	uint32_t candidate_index;
	sg_rune_q8_vec3_t point;

	if (!cell_out)
		return 0;
	memset(cell_out, 0, sizeof(*cell_out));
	cell_out->location.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	if (!model || !binding || !CompactBeliefProviderCurrent(context, model,
		&model->identity,
		binding->rune_identity, binding->topology_revision,
		binding->host_authority.epoch) || !binding->spatial_index ||
		!CompactBeliefPointToQ8(position, &point))
		return 0;
	if (!sg_bot_localization_provider.belief_candidates ||
		!sg_bot_localization_provider.belief_index_candidates)
		return 0;
	world_point.value[0] = position[0];
	world_point.value[1] = position[1];
	world_point.value[2] = position[2];
	memset(&spatial_error, 0, sizeof(spatial_error));
	if (!SG_RuneCompactSpatialIndexQueryCells(binding->spatial_index,
		&world_point, sg_bot_localization_provider.belief_candidates,
		model->cell_count, &candidate_count, &spatial_error) ||
		candidate_count == 0U)
		return 0;
	for (candidate_index = 0U; candidate_index < candidate_count;
		candidate_index++)
		sg_bot_localization_provider.belief_index_candidates[candidate_index].value =
			sg_bot_localization_provider.belief_candidates[candidate_index];
	memset(&location, 0, sizeof(location));
	location.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	if (SG_RuneCompactLocalizeIndexed(model, &point,
		sg_bot_localization_provider.belief_index_candidates,
		candidate_count, &location) != SG_RUNE_COMPACT_LOCALIZE_OK)
		return 0;
	cell_out->location = location;
	return 1;
}

static int CompactBeliefProviderInstall(
	const sg_compact_localization_binding_t *binding)
{
	sg_belief_runtime_provider_t provider;

	if (!binding || !SG_CompactLocalizationBindingCurrent(binding) ||
		binding->host_authority.epoch == 0U)
		return 0;
	memset(&provider, 0, sizeof(provider));
	provider.model = binding->model;
	provider.identity = &binding->model->identity;
	provider.rune_identity = binding->rune_identity;
	provider.topology_revision = binding->topology_revision;
	provider.generation = binding->host_authority.epoch;
	/* Preserve CACO's established eight-second live-belief lifetime.  Movement
	 * diffusion is intentionally zero until an authenticated live updater owns
	 * it; this provider only maps earned host observations to compact cells. */
	provider.policy.confidence_decay_ms =
		(uint64_t)(SG_BELIEF_STALE * 1000.0f);
	provider.policy.spread_growth_per_ms = 0.0f;
	provider.locate = CompactBeliefLocate;
	provider.current = CompactBeliefProviderCurrent;
	provider.context = &sg_bot_localization_provider.binding;
	return SG_CacoCompactBeliefProviderSet(&provider);
}

static const sg_compact_localization_binding_t *Provider(void)
{
	return SG_CompactLocalizationBindingCurrent(
		&sg_bot_localization_provider.binding) ?
		&sg_bot_localization_provider.binding : NULL;
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

static int StrategyObservationViewEqual(
	const sg_strategy_runtime_bot_observation_view_t *left,
	const sg_strategy_runtime_bot_observation_view_t *right)
{
	return left && right && left->subject.client_id == right->subject.client_id &&
		left->subject.reserved == right->subject.reserved &&
		left->subject.spawn_generation == right->subject.spawn_generation &&
		left->host_authority_epoch == right->host_authority_epoch &&
		left->frame_sequence == right->frame_sequence &&
		left->observed_at_ms == right->observed_at_ms &&
		left->hook_phase == right->hook_phase &&
		memcmp(&left->hook_length, &right->hook_length,
			sizeof(left->hook_length)) == 0 &&
		memcmp(&left->target_radius, &right->target_radius,
			sizeof(left->target_radius)) == 0;
}

static int StrategyObservationCapture(sg_bot_t *bot,
	const sg_compact_localized_state_t *localized,
	sg_strategy_runtime_bot_observation_view_t *view_out)
{
	const sg_compact_localization_binding_t *binding = Provider();
	sg_localization_subject_t live_subject;
	sg_host_law_result_t host_result;
	edict_t *entity;
	edict_t *hook;
	float target_radius = 0.0f;
	float hook_length = 0.0f;
	sg_host_hook_phase_t hook_phase;

	if (!bot || !localized || !view_out || !binding || !bot->active ||
		!(entity = bot->ent) || !entity->inuse || !entity->client ||
		BotForEntity(entity) != bot ||
		SG_BotLocalizationCurrent(bot) != localized ||
		!SubjectEqual(&bot->localization_subject, &localized->subject) ||
		!SG_CompactLocalizationStateCurrent(binding, &localized->subject,
			localized))
		return 0;
	memset(&live_subject, 0, sizeof(live_subject));
	host_result = SG_HostLawProductionSubject(&binding->host_authority,
		(uint32_t)entity->s.number, &live_subject);
	if (host_result.status != SG_HOST_LAW_OK ||
		!SubjectEqual(&live_subject, &localized->subject))
		return 0;
	hook = entity->client->hook;
	if (bot->hook_phase == 0 && entity->client->hookstate == 0 &&
		hook == NULL && bot->hook_entity == NULL)
		hook_phase = SG_HOST_HOOK_IDLE;
	else if (bot->hook_phase == 2 && entity->client->hookstate == 1 &&
		hook != NULL && hook->inuse && hook->owner == entity &&
		bot->hook_entity == hook && hook->hook_target == NULL)
		hook_phase = SG_HOST_HOOK_IN_FLIGHT;
	else if (bot->hook_phase == 2 && entity->client->hookstate == 2 &&
		hook != NULL && hook->inuse && hook->owner == entity &&
		bot->hook_entity == hook && hook->hook_target != NULL &&
		hook->hook_target->inuse && entity->client->hooklength >= 0)
	{
		float extent[3];

		hook_phase = SG_HOST_HOOK_ATTACHED;
		hook_length = (float)entity->client->hooklength;
		for (uint32_t axis = 0U; axis < 3U; axis++)
			extent[axis] = hook->hook_target->maxs[axis] -
				hook->hook_target->mins[axis];
		target_radius = 0.5f * sqrtf(extent[0] * extent[0] +
			extent[1] * extent[1] + extent[2] * extent[2]);
		if (!isfinite(target_radius) || target_radius < 0.0f)
			return 0;
	}
	else if (bot->hook_phase == 3 && entity->client->hookstate == 0 &&
		hook == NULL)
		hook_phase = SG_HOST_HOOK_COAST;
	else
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->subject = live_subject;
	view_out->host_authority_epoch = binding->host_authority.epoch;
	view_out->frame_sequence = localized->frame_sequence;
	view_out->observed_at_ms = localized->localized_at_ms;
	view_out->hook_phase = hook_phase;
	view_out->hook_length = hook_length;
	view_out->target_radius = target_radius;
	return view_out->host_authority_epoch != 0U;
}

static int ValidateStrategyObservation(void *context,
	const sg_strategy_runtime_bot_observation_t *observation,
	sg_strategy_runtime_bot_observation_view_t *view_out)
{
	sg_bot_strategy_observation_issuer_t *issuer = context;
	const struct sg_strategy_runtime_bot_observation_s *capability = observation;

	if (!issuer || issuer != &sg_bot_localization_provider.strategy_issuer ||
		!capability || !view_out || issuer->active != 1U ||
		capability != &issuer->capability || capability->serial == 0U ||
		capability->serial != issuer->serial)
		return 0;
	*view_out = issuer->view;
	issuer->active = 0U;
	return 1;
}

static int StrategyObservationCurrent(void *context,
	const sg_strategy_runtime_bot_observation_view_t *view)
{
	sg_bot_strategy_observation_issuer_t *issuer = context;
	sg_strategy_runtime_bot_observation_view_t current;

	if (!issuer || issuer != &sg_bot_localization_provider.strategy_issuer ||
		!view || !issuer->bot || !issuer->localized)
		return 0;
	if (!StrategyObservationCapture(issuer->bot, issuer->localized, &current))
		return 0;
	return StrategyObservationViewEqual(view, &current);
}

const sg_strategy_runtime_bot_observation_owner_t *
SG_BotLocalizationStrategyObservationOwner(void)
{
	static const sg_strategy_runtime_bot_observation_owner_t owner = {
		&sg_bot_localization_provider.strategy_issuer,
		ValidateStrategyObservation,
		StrategyObservationCurrent
	};

	return &owner;
}

const sg_strategy_runtime_bot_observation_t *
SG_BotLocalizationStrategyObservationIssue(sg_bot_t *bot,
	const sg_compact_localized_state_t *localized)
{
	sg_bot_strategy_observation_issuer_t *issuer =
		&sg_bot_localization_provider.strategy_issuer;
	uint64_t serial;

	issuer->active = 0U;
	if (!StrategyObservationCapture(bot, localized, &issuer->view) ||
		issuer->serial == UINT64_MAX)
	{
		memset(issuer, 0, sizeof(*issuer));
		return NULL;
	}
	serial = issuer->serial + 1U;
	if (serial == 0U)
	{
		memset(issuer, 0, sizeof(*issuer));
		return NULL;
	}
	issuer->serial = serial;
	issuer->capability.serial = serial;
	issuer->bot = bot;
	issuer->localized = localized;
	issuer->active = 1U;
	return &issuer->capability;
}

static void InvalidateState(sg_bot_t *bot)
{
	if (bot)
		SG_BotLocalizationInvalidate(bot);
}

int SG_BotLocalizationProviderSet(
	const sg_compact_localization_binding_t *binding)
{
	uint32_t *scratch_candidates = NULL;
	uint32_t *belief_candidates = NULL;
	sg_rune_compact_cell_index_t *belief_index_candidates = NULL;
	int index;

	/* CACO borrows this compact locator. Revoke its callback before the
	 * localization binding or level-owned artifact can disappear. */
	(void)SG_CacoCompactBeliefProviderSet(NULL);
	for (index = 0; index < SG_MAXBOTS; index++)
		SG_BotLocalizationReset(&sg_bots[index]);
	SG_CompactLocalizationUnbind(&sg_bot_localization_provider.binding);
	free(sg_bot_localization_provider.scratch.candidates);
	free(sg_bot_localization_provider.belief_candidates);
	free(sg_bot_localization_provider.belief_index_candidates);
	memset(&sg_bot_localization_provider, 0,
		sizeof(sg_bot_localization_provider));
	if (!binding)
		return 1;
	if (!SG_CompactLocalizationBindingCurrent(binding) || !binding->model ||
		binding->model->cell_count == 0U)
		return 0;
	scratch_candidates = calloc((size_t)binding->model->cell_count,
		sizeof(*scratch_candidates));
	belief_candidates = calloc((size_t)binding->model->cell_count,
		sizeof(*belief_candidates));
	belief_index_candidates = calloc((size_t)binding->model->cell_count,
		sizeof(*belief_index_candidates));
	if (!scratch_candidates || !belief_candidates || !belief_index_candidates)
	{
		free(scratch_candidates);
		free(belief_candidates);
		free(belief_index_candidates);
		return 0;
	}
	sg_bot_localization_provider.binding = *binding;
	sg_bot_localization_provider.scratch.candidates = scratch_candidates;
	sg_bot_localization_provider.scratch.candidate_capacity =
		binding->model->cell_count;
	sg_bot_localization_provider.belief_candidates = belief_candidates;
	sg_bot_localization_provider.belief_index_candidates =
		belief_index_candidates;
	if (CompactBeliefProviderInstall(&sg_bot_localization_provider.binding))
		return 1;
	SG_CompactLocalizationUnbind(&sg_bot_localization_provider.binding);
	free(sg_bot_localization_provider.scratch.candidates);
	free(sg_bot_localization_provider.belief_candidates);
	free(sg_bot_localization_provider.belief_index_candidates);
	memset(&sg_bot_localization_provider, 0,
		sizeof(sg_bot_localization_provider));
	return 0;
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
	sg_bot_localization_observation_issuer_t *issuer =
		&sg_bot_localization_provider.issuer;
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

	if (issuer->active == 1U || issuer->serial == UINT64_MAX)
	{
		InvalidateState(bot);
		return;
	}
	issuer->serial++;
	memset(&issuer->capability, 0, sizeof(issuer->capability));
	memset(&issuer->view, 0, sizeof(issuer->view));
	issuer->capability.serial = issuer->serial;
	issuer->view.kind = kind;
	issuer->view.subject = bot->localization_subject;
	issuer->view.host_authority_epoch = binding->host_authority.epoch;
	issuer->view.frame_sequence = frame_sequence;
	issuer->view.observed_at_ms = sample_time;
	issuer->view.model_stamp.identity = binding->rune_identity;
	issuer->view.model_stamp.generation = binding->topology_revision;
	issuer->view.model_stamp.frame_sequence = frame_sequence;
	issuer->view.pmove_result = pmove_result;
	issuer->view.state_observation = live_state;
	if (kind == SG_LOCALIZATION_OBSERVATION_PRESENT && previous_ptr)
		issuer->view.maximum_recovery_distance =
			SG_BOT_LOCALIZATION_NUMERIC_DRIFT;
	if (kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN && previous_ptr)
	{
		issuer->view.previous_subject = previous_ptr->subject;
		issuer->view.previous_frame_sequence = previous_ptr->frame_sequence;
		issuer->view.previous_observed_at_ms = previous_ptr->localized_at_ms;
	}
	issuer->active = 1U;
	memset(&sample, 0, sizeof(sample));
	sample.observation = &issuer->capability;
	status = SG_CompactLocalizationObserveWithScratch(binding, &sample,
		previous_ptr, &sg_bot_localization_provider.scratch, &localized);
	issuer->active = 0U;
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
