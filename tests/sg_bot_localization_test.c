#include "../g_local.h"
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"

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

static rune_t rune;
static sg_host_collision_authority_t authority;
static sg_cell_phase_locator_t locator;
static sg_cell_phase_runtime_t runtime;
static edict_t entity;
static gclient_t client;
static uint64_t spawn_generation = 1U;
static int runtime_current = 1;
static int state_current = 1;
static sg_localization_observation_t captured_observation;
static sg_localization_environment_t captured_environment;
static sg_localization_request_t captured_request;
static int captured_previous;

rune_t *SG_Rune(void)
{
	return &rune;
}

int SG_CellPhaseRuntimeCurrent(const sg_cell_phase_runtime_t *candidate)
{
	return runtime_current && candidate == &runtime;
}

int SG_CellPhaseLocalizedStateCurrent(const sg_cell_phase_runtime_t *candidate,
	const sg_localization_subject_t *subject,
	const sg_localized_player_state_t *state)
{
	return state_current && SG_CellPhaseRuntimeCurrent(candidate) && subject &&
		state && state->subject.client_id == subject->client_id &&
		state->subject.spawn_generation == subject->spawn_generation;
}

sg_host_law_result_t SG_HostLawProductionSubject(
	const sg_host_law_runtime_authority_t *host_authority, uint32_t client_id,
	sg_localization_subject_t *subject_out)
{
	sg_host_law_result_t result;

	(void)host_authority;
	memset(&result, 0, sizeof(result));
	result.status = SG_HOST_LAW_OK;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	memset(subject_out, 0, sizeof(*subject_out));
	subject_out->client_id = client_id;
	subject_out->spawn_generation = spawn_generation;
	return result;
}

int SG_CellPhaseLocalize(const sg_cell_phase_runtime_t *candidate,
	const sg_localization_request_t *request,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment,
	sg_localized_player_state_t *state_out,
	sg_localization_status_t *status_out)
{
	CHECK(candidate == &runtime);
	captured_observation = *observation;
	captured_environment = *environment;
	captured_request = *request;
	captured_previous = request->previous != NULL;
	if (request->previous)
		*state_out = *request->previous;
	else
		memset(state_out, 0, sizeof(*state_out));
	state_out->subject = observation->subject;
	state_out->rune_identity = observation->rune_identity;
	state_out->topology_revision = observation->topology_revision;
	state_out->frame_sequence = observation->frame_sequence;
	state_out->localized_at_ms = observation->observed_at_ms;
	state_out->field_pose.phase.phase_id = 5U;
	state_out->field_pose.phase.cell_id = 3U;
	state_out->field_pose.region_id = 2U;
	state_out->field_pose.sample_time_ms = observation->observed_at_ms;
	if (observation->kind !=
		SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT)
	{
		memcpy(state_out->field_pose.position, observation->position,
			sizeof(state_out->field_pose.position));
		memcpy(state_out->field_pose.velocity, observation->velocity,
			sizeof(state_out->field_pose.velocity));
		state_out->host_state = observation->host_state;
	}
	state_out->host_state_valid = 1U;
	state_out->stance = observation->stance;
	if (status_out)
		*status_out = SG_LOCALIZATION_OK;
	return 1;
}

static sg_host_pmove_request_t PmoveRequest(
	const sg_localized_player_state_t *previous)
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
	result.state.pm_flags = (byte)flags;
	result.origin[0] = x;
	result.velocity[1] = x + 1.0f;
	return result;
}

static int TestLifeAndMotion(void)
{
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	const sg_localized_player_state_t *current;

	memset(&entity, 0, sizeof(entity));
	memset(&client, 0, sizeof(client));
	entity.inuse = true;
	entity.client = &client;
	entity.deadflag = DEAD_NO;
	entity.s.number = 7;
	sg_bots[0].active = true;
	sg_bots[0].ent = &entity;
	CHECK(SG_BotLocalizationProviderSet(&runtime));

	level.framenum = 0;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(sg_bots[0].localization_event ==
		SG_LOCALIZATION_OBSERVATION_NEW_SPAWN);
	request = PmoveRequest(NULL);
	result = PmoveResult(0U, 10.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(captured_observation.kind ==
		SG_LOCALIZATION_OBSERVATION_NEW_SPAWN);
	CHECK(!captured_previous && captured_request.maximum_recovery_distance == 0.0f);
	CHECK(captured_request.maximum_temporary_absence_ms == 0U);
	CHECK(captured_observation.position[0] == result.origin[0]);
	CHECK(SG_BotLocalizationCell(&sg_bots[0]) == 3);

	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL);
	level.framenum = 1;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	request = PmoveRequest(current);
	result = PmoveResult(PMF_DUCKED, 11.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(captured_observation.kind == SG_LOCALIZATION_OBSERVATION_PRESENT);
	CHECK(captured_observation.stance == SG_RUNE_STANCE_CROUCHING);
	CHECK(captured_previous && captured_environment.pmove_request == &request);
	CHECK(captured_request.maximum_recovery_distance == 0.5f);
	CHECK(captured_request.maximum_temporary_absence_ms == 0U);

	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL);
	level.framenum = 2;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	request = PmoveRequest(current);
	result = PmoveResult(PMF_TIME_TELEPORT, 64.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(captured_observation.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED);
	CHECK(!captured_previous && captured_request.maximum_recovery_distance == 0.0f);

	level.framenum = 3;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	SG_BotLocalizationFrameEnd(&sg_bots[0]);
	CHECK(captured_observation.kind ==
		SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT);
	CHECK(captured_previous &&
		captured_request.maximum_temporary_absence_ms == 100U);
	return 1;
}

static int TestInvalidationAndSlotReuse(void)
{
	edict_t *bot_entity = sg_bots[0].ent;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;

	spawn_generation++;
	level.framenum++;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);
	CHECK(sg_bots[0].localization_subject.spawn_generation == spawn_generation);
	CHECK(sg_bots[0].localization_event ==
		SG_LOCALIZATION_OBSERVATION_NEW_SPAWN);
	request = PmoveRequest(NULL);
	result = PmoveResult(0U, 20.0f);
	SG_BotLocalizationObservePmove(bot_entity, &request, &result);
	CHECK(!captured_previous);

	state_current = 0;
	level.framenum++;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);
	state_current = 1;

	request.command.msec = 25U;
	SG_BotLocalizationObservePmove(bot_entity, &request, &result);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);

	bot_entity->deadflag = DEAD_DEAD;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(sg_bots[0].localization_subject.spawn_generation == 0U);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);
	bot_entity->deadflag = DEAD_NO;

	runtime_current = 0;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(sg_bots[0].localization_subject.spawn_generation == 0U);
	runtime_current = 1;
	CHECK(SG_BotLocalizationProviderSet(NULL));
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);
	return 1;
}

int main(void)
{
	memset(&rune, 0, sizeof(rune));
	rune.hdr.num_seeds = 8;
	memset(&authority, 0, sizeof(authority));
	authority.identity.physics.frame_ms = 100U;
	memset(&locator, 0, sizeof(locator));
	locator.authority = &authority;
	locator.runtime_cell_count = 8U;
	memset(&runtime, 0, sizeof(runtime));
	runtime.locator = &locator;
	runtime.rune_identity = 11U;
	runtime.topology_revision = 13U;
	if (!TestLifeAndMotion() || !TestInvalidationAndSlotReuse())
		return 1;
	puts("bot localization lifecycle tests passed");
	return 0;
}
