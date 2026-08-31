#include "../g_local.h"
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"
#include "../slipgate/sg_compact_localization.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at line %d: %s\n", \
			__LINE__, #condition); \
		return 0; \
	} \
} while (0)

level_locals_t level;
sg_bot_t sg_bots[SG_MAXBOTS];

static sg_compact_localization_binding_t binding;
static edict_t entity;
static gclient_t client;
static uint64_t spawn_generation;
static int binding_current;
static int state_current;
static int subject_current;
static int subject_state_calls;
static int observe_calls;
static int observed_cell;
static sg_localization_status_t observe_status;
static sg_compact_localization_sample_t captured_sample;
static int captured_pmove_result_present;
static const sg_host_pmove_result_t *captured_pmove_result;
static int captured_state_observation_present;
static sg_host_pmove_state_observation_t captured_state_observation;
static int captured_previous_present;
static sg_compact_localized_state_t captured_previous;

static sg_host_law_result_t HostResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

/* These are deliberately narrow boundary stubs.  The bot lifecycle test
 * owns neither the compact model nor the host publication it borrows. */
int SG_CompactLocalizationBindingCurrent(
	const sg_compact_localization_binding_t *candidate)
{
	return binding_current && candidate && candidate->bound == 1U;
}

void SG_CompactLocalizationUnbind(sg_compact_localization_binding_t *candidate)
{
	if (candidate)
		memset(candidate, 0, sizeof(*candidate));
}

int SG_CompactLocalizationStateCurrent(
	const sg_compact_localization_binding_t *candidate,
	const sg_localization_subject_t *subject,
	const sg_compact_localized_state_t *state)
{
	return state_current && SG_CompactLocalizationBindingCurrent(candidate) &&
		subject_current && subject && state && state->valid == 1U &&
		state->subject.client_id == subject->client_id &&
		state->subject.spawn_generation == subject->spawn_generation;
}

sg_localization_status_t SG_CompactLocalizationObserve(
	const sg_compact_localization_binding_t *candidate,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localized_state_t *state_out)
{
	const pmove_state_t *host_state = NULL;
	const float *position = NULL;
	const float *velocity = NULL;

	if (!candidate || candidate->bound != 1U || !sample || !state_out)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	observe_calls++;
	captured_sample = *sample;
	captured_pmove_result_present = sample->pmove_result != NULL;
	captured_pmove_result = sample->pmove_result;
	captured_state_observation_present = sample->state_observation != NULL;
	if (sample->state_observation)
		captured_state_observation = *sample->state_observation;
	else
		memset(&captured_state_observation, 0,
			sizeof(captured_state_observation));
	captured_previous_present = previous != NULL;
	if (previous)
		captured_previous = *previous;
	else
		memset(&captured_previous, 0, sizeof(captured_previous));
	captured_sample.pmove_result = NULL;
	captured_sample.state_observation = NULL;
	if (observe_status != SG_LOCALIZATION_OK)
		return observe_status;
	if (previous)
		*state_out = *previous;
	else
		memset(state_out, 0, sizeof(*state_out));
	state_out->subject = sample->subject;
	state_out->rune_identity = candidate->rune_identity;
	state_out->topology_revision = candidate->topology_revision;
	state_out->frame_sequence = sample->frame_sequence;
	state_out->localized_at_ms = sample->observed_at_ms;
	state_out->location.cell.value = (uint32_t)observed_cell;
	state_out->location.valid_stances = SG_RUNE_STANCE_VALID_ALL;
	state_out->stance = SG_RUNE_STANCE_STANDING;
	state_out->motion = SG_RUNE_MOTION_SUPPORTED;
	state_out->support = SG_RUNE_SUPPORT_SUPPORTED;
	state_out->medium = SG_RUNE_MEDIUM_DRY;
	state_out->void_relation = SG_RUNE_VOID_CLEAR;
	state_out->reference_frame = SG_RUNE_FRAME_WORLD;
	state_out->support_model_index = SG_HOST_COLLISION_MODEL_WORLD;
	state_out->support_instance_id = 0U;
	state_out->water_level = 0U;
	state_out->water_type = 0;
	state_out->recovery = sample->kind == SG_LOCALIZATION_OBSERVATION_PRESENT &&
		previous ? SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY :
		SG_LOCALIZATION_RECOVERY_NONE;
	if (sample->pmove_result)
	{
		host_state = &sample->pmove_result->state;
		position = sample->pmove_result->origin;
		velocity = sample->pmove_result->velocity;
		state_out->stance =
			(sample->pmove_result->state.pm_flags & PMF_DUCKED) != 0 ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	}
	else if (sample->state_observation)
	{
		host_state = &sample->state_observation->state;
		position = sample->state_observation->origin;
		velocity = sample->state_observation->velocity;
		state_out->stance =
			(sample->state_observation->state.pm_flags & PMF_DUCKED) != 0 ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	}
	if (host_state)
		state_out->host_state = *host_state;
	if (position)
		memcpy(state_out->position, position, sizeof(state_out->position));
	if (velocity)
		memcpy(state_out->velocity, velocity, sizeof(state_out->velocity));
	state_out->valid = 1U;
	return SG_LOCALIZATION_OK;
}

/* Host subject reads are the only non-compact calls the bot lifecycle owns.
 * Their bodies model the owner issuing an exact subject and spawn snapshot. */
sg_host_law_result_t SG_HostLawProductionSubject(
	const sg_host_law_runtime_authority_t *host_authority,
	uint32_t subject_index, sg_host_law_subject_t *subject_out)
{
	(void)host_authority;
	if (!subject_current || !subject_out)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	memset(subject_out, 0, sizeof(*subject_out));
	subject_out->client_id = subject_index;
	subject_out->spawn_generation = spawn_generation;
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawProductionSubjectCurrent(
	const sg_host_law_runtime_authority_t *host_authority,
	const sg_host_law_subject_t *subject)
{
	(void)host_authority;
	if (!subject_current || !subject ||
		subject->spawn_generation != spawn_generation)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawProductionSubjectState(
	const sg_host_law_runtime_authority_t *host_authority,
	const sg_host_law_subject_t *subject,
	sg_host_pmove_state_observation_t *observation_out)
{
	(void)host_authority;
	if (!subject_current || !subject ||
		subject->spawn_generation != spawn_generation || !observation_out)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	memset(observation_out, 0, sizeof(*observation_out));
	observation_out->state = client.ps.pmove;
	memcpy(observation_out->origin, entity.s.origin,
		sizeof(observation_out->origin));
	memcpy(observation_out->velocity, entity.velocity,
		sizeof(observation_out->velocity));
	subject_state_calls++;
	return HostResult(SG_HOST_LAW_OK);
}

static sg_host_pmove_request_t PmoveRequest(
	const sg_compact_localized_state_t *previous)
{
	sg_host_pmove_request_t request;

	memset(&request, 0, sizeof(request));
	request.command.msec = 100U;
	if (previous)
	{
		request.state = previous->host_state;
		request.previous_state = previous->host_state;
	}
	return request;
}

static sg_host_pmove_result_t PmoveResult(unsigned flags, float x)
{
	sg_host_pmove_result_t result;

	memset(&result, 0, sizeof(result));
	result.state.pm_type = PM_NORMAL;
	result.state.pm_flags = (byte)flags;
	result.state.origin[0] = (int16_t)(x * 8.0f);
	result.state.gravity = 800;
	result.origin[0] = x;
	result.velocity[1] = x + 1.0f;
	result.grounded = 1;
	result.support_model_index = SG_HOST_COLLISION_MODEL_WORLD;
	result.evaluated_steps = 4U;
	result.elapsed_ms = 100U;
	result.gravity = 800.0f;
	return result;
}

static void ResetFixture(void)
{
	(void)SG_BotLocalizationProviderSet(NULL);
	memset(&level, 0, sizeof(level));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&binding, 0, sizeof(binding));
	memset(&entity, 0, sizeof(entity));
	memset(&client, 0, sizeof(client));
	memset(&captured_sample, 0, sizeof(captured_sample));
	memset(&captured_state_observation, 0,
		sizeof(captured_state_observation));
	memset(&captured_previous, 0, sizeof(captured_previous));
	captured_pmove_result_present = 0;
	captured_pmove_result = NULL;
	captured_state_observation_present = 0;
	captured_previous_present = 0;
	spawn_generation = 1U;
	binding_current = 1;
	state_current = 1;
	subject_current = 1;
	subject_state_calls = 0;
	observe_calls = 0;
	observed_cell = 3;
	observe_status = SG_LOCALIZATION_OK;
	binding.bound = 1U;
	binding.rune_identity = 11U;
	binding.topology_revision = 13U;
	binding.identity.physics.frame_ms = 100U;
	entity.inuse = true;
	entity.client = &client;
	entity.deadflag = DEAD_NO;
	entity.s.number = 7;
	client.ps.pmove.pm_type = PM_NORMAL;
	client.ps.pmove.origin[0] = 80;
	client.ps.pmove.gravity = 800;
	entity.s.origin[0] = 10.0f;
	entity.velocity[1] = 2.0f;
	sg_bots[0].active = true;
	sg_bots[0].ent = &entity;
}

static int TestCompactLifecycle(void)
{
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	const sg_compact_localized_state_t *current;
	const sg_compact_localized_state_t *before;
	int calls_before;

	ResetFixture();
	/* Provider installation owns the reset of every bot slot. */
	sg_bots[1].localization_subject.client_id = 99U;
	sg_bots[1].localized_state.valid = 1U;
	CHECK(SG_BotLocalizationProviderSet(&binding));
	CHECK(sg_bots[1].localization_subject.client_id == 0U);
	CHECK(sg_bots[1].localized_state.valid == 0U);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);

	/* The first sample is owner state, never a fabricated Pmove result. */
	level.framenum = 0;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(subject_state_calls == 1);
	CHECK(observe_calls == 1);
	CHECK(captured_sample.kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN);
	CHECK(!captured_previous_present);
	CHECK(!captured_pmove_result_present);
	CHECK(captured_state_observation_present);
	CHECK(captured_state_observation.state.pm_type == PM_NORMAL);
	CHECK(captured_state_observation.origin[0] == entity.s.origin[0]);
	CHECK(captured_sample.maximum_recovery_distance == 0.0f);
	CHECK(captured_sample.maximum_temporary_absence_ms == 0U);
	CHECK(captured_sample.life_reset.authorized == 1U);
	CHECK(captured_sample.life_reset.previous_subject.client_id == 0U);
	CHECK(captured_sample.life_reset.previous_subject.spawn_generation == 0U);
	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL);
	CHECK(current->valid == 1U);
	CHECK(SG_BotLocalizationCell(&sg_bots[0]) == observed_cell);
	CHECK(current->recovery == SG_LOCALIZATION_RECOVERY_NONE);
	CHECK(current->position[0] == entity.s.origin[0]);

	/* A normal frame carries the exact Pmove result and prior compact state. */
	before = current;
	level.framenum = 1;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(subject_state_calls == 1);
	request = PmoveRequest(before);
	result = PmoveResult(PMF_DUCKED, 11.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(observe_calls == 2);
	CHECK(captured_sample.kind == SG_LOCALIZATION_OBSERVATION_PRESENT);
	CHECK(captured_previous_present);
	CHECK(captured_previous.location.cell.value == before->location.cell.value);
	CHECK(captured_pmove_result_present);
	CHECK(captured_pmove_result == &result);
	CHECK(!captured_state_observation_present);
	CHECK(captured_sample.maximum_recovery_distance ==
		SG_COMPACT_LOCALIZATION_MAX_RECOVERY_DISTANCE);
	CHECK(captured_sample.maximum_temporary_absence_ms == 0U);
	CHECK(captured_sample.life_reset.authorized == 0U);
	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL && current->stance == SG_RUNE_STANCE_CROUCHING);
	CHECK(current->recovery == SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY);

	/* Teleport still consumes Pmove; it must not turn same-cell identity into
	 * numeric/exact recovery. */
	before = current;
	level.framenum = 2;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	request = PmoveRequest(before);
	result = PmoveResult(PMF_TIME_TELEPORT, 64.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(observe_calls == 3);
	CHECK(captured_sample.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED);
	CHECK(captured_previous_present);
	CHECK(captured_previous.location.cell.value == before->location.cell.value);
	CHECK(captured_pmove_result_present);
	CHECK(captured_pmove_result == &result);
	CHECK(!captured_state_observation_present);
	CHECK(captured_sample.maximum_recovery_distance == 0.0f);
	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL);
	CHECK(SG_BotLocalizationCell(&sg_bots[0]) == observed_cell);
	CHECK(current->recovery == SG_LOCALIZATION_RECOVERY_NONE);

	/* Compact localization has no temporary-absence lifecycle. */
	calls_before = observe_calls;
	level.framenum = 3;
	SG_BotLocalizationFrameEnd(&sg_bots[0]);
	CHECK(observe_calls == calls_before);
	CHECK(captured_sample.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);

	/* A revoked binding fails closed and resets retained bot state. */
	binding_current = 0;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);
	sg_bots[0].localized_state.valid = 1U;
	CHECK(!SG_BotLocalizationProviderSet(&binding));
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);

	/* Reinstall a current provider for the msec and teardown checks. */
	binding_current = 1;
	CHECK(SG_BotLocalizationProviderSet(&binding));
	level.framenum = 4;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) != NULL);
	calls_before = observe_calls;
	request = PmoveRequest(SG_BotLocalizationCurrent(&sg_bots[0]));
	request.command.msec = 25U;
	result = PmoveResult(0U, 20.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(observe_calls == calls_before);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);

	/* Uninstall clears both the borrowed provider and canonical state. */
	CHECK(SG_BotLocalizationProviderSet(NULL));
	CHECK(sg_bots[0].localization_subject.spawn_generation == 0U);
	CHECK(sg_bots[0].localized_state.valid == 0U);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);
	return 1;
}

int main(void)
{
	if (!TestCompactLifecycle())
		return 1;
	puts("bot compact localization lifecycle tests passed");
	return 0;
}
