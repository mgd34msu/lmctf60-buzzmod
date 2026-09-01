#include "../g_local.h"
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"
#include "../slipgate/sg_compact_localization.h"
#include "../slipgate/sg_belief_runtime.h"
#include "../slipgate/sg_strategy_runtime_bridge_private.h"

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
static sg_rune_compact_model_t compact_model;
static sg_rune_compact_cell_t compact_cell;
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
static sg_localization_status_t replay_status;
static sg_compact_localization_observation_view_t captured_view;
static int captured_pmove_result_present;
static const sg_host_pmove_result_t *captured_pmove_result;
static int captured_state_observation_present;
static sg_host_pmove_state_observation_t captured_state_observation;
static int captured_previous_present;
static sg_compact_localized_state_t captured_previous;
static sg_belief_runtime_provider_t captured_belief_provider;
static int compact_belief_provider_active;
static int compact_belief_provider_calls;

static sg_host_law_result_t HostResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

int SG_CacoCompactBeliefProviderSet(
	const sg_belief_runtime_provider_t *provider)
{
	compact_belief_provider_calls++;
	memset(&captured_belief_provider, 0, sizeof(captured_belief_provider));
	if (!provider)
	{
		compact_belief_provider_active = 0;
		return 1;
	}
	captured_belief_provider = *provider;
	compact_belief_provider_active = 1;
	return 1;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	(void)point;
	if (!model || !location_out)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = 0U;
	location_out->valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalizeIndexed(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	const sg_rune_compact_cell_index_t *candidate_cells,
	uint32_t candidate_count, sg_rune_compact_location_t *location_out)
{
	(void)candidate_cells;
	return candidate_count != 0U ? SG_RuneCompactLocalize(model, point,
		location_out) : SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
}

int SG_RuneCompactSpatialIndexQueryCells(
	const sg_rune_compact_spatial_index_t *index, const sg_rune_vec3_t *point,
	uint32_t *cells_out, uint32_t cell_capacity, uint32_t *cell_count_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	(void)point;
	(void)error_out;
	if (!index || !cells_out || cell_capacity == 0U || !cell_count_out)
		return 0;
	cells_out[0] = 0U;
	*cell_count_out = 1U;
	return 1;
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

sg_localization_status_t SG_CompactLocalizationObserveWithScratch(
	const sg_compact_localization_binding_t *candidate,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localization_scratch_t *scratch,
	sg_compact_localized_state_t *state_out)
{
	sg_localization_status_t validate_status;
	const pmove_state_t *host_state = NULL;
	const float *position = NULL;
	const float *velocity = NULL;

	if (!candidate || candidate->bound != 1U || !sample || !scratch ||
		!state_out || !candidate->observation_owner.validate)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	memset(&captured_view, 0, sizeof(captured_view));
	validate_status = candidate->observation_owner.validate(
		candidate->observation_owner.context, &candidate->host_authority,
		sample->observation, &captured_view);
	if (validate_status != SG_LOCALIZATION_OK)
		return validate_status;
	{
		sg_compact_localization_observation_view_t replay_view;

		memset(&replay_view, 0, sizeof(replay_view));
		replay_status = candidate->observation_owner.validate(
			candidate->observation_owner.context, &candidate->host_authority,
			sample->observation, &replay_view);
	}
	observe_calls++;
	captured_pmove_result_present = captured_view.pmove_result != NULL;
	captured_pmove_result = captured_view.pmove_result;
	captured_state_observation_present =
		captured_view.state_observation != NULL;
	if (captured_view.state_observation)
		captured_state_observation = *captured_view.state_observation;
	else
		memset(&captured_state_observation, 0,
			sizeof(captured_state_observation));
	captured_previous_present = previous != NULL;
	if (previous)
		captured_previous = *previous;
	else
		memset(&captured_previous, 0, sizeof(captured_previous));
	if (observe_status != SG_LOCALIZATION_OK)
		return observe_status;
	if (previous)
		*state_out = *previous;
	else
		memset(state_out, 0, sizeof(*state_out));
	state_out->subject = captured_view.subject;
	state_out->model_stamp = captured_view.model_stamp;
	state_out->rune_identity = candidate->rune_identity;
	state_out->topology_revision = candidate->topology_revision;
	state_out->frame_sequence = captured_view.frame_sequence;
	state_out->localized_at_ms = captured_view.observed_at_ms;
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
	state_out->recovery = captured_view.kind ==
		SG_LOCALIZATION_OBSERVATION_PRESENT &&
		previous ? SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY :
		SG_LOCALIZATION_RECOVERY_NONE;
	if (captured_view.pmove_result)
	{
		host_state = &captured_view.pmove_result->state;
		position = captured_view.pmove_result->origin;
		velocity = captured_view.pmove_result->velocity;
		state_out->stance =
			(captured_view.pmove_result->state.pm_flags & PMF_DUCKED) != 0 ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	}
	else if (captured_view.state_observation)
	{
		host_state = &captured_view.state_observation->state;
		position = captured_view.state_observation->origin;
		velocity = captured_view.state_observation->velocity;
		state_out->stance =
			(captured_view.state_observation->state.pm_flags & PMF_DUCKED) != 0 ?
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
	memset(&compact_model, 0, sizeof(compact_model));
	memset(&compact_cell, 0, sizeof(compact_cell));
	compact_cell.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	compact_model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	compact_model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	compact_model.cells = &compact_cell;
	compact_model.cell_count = 1U;
	memset(&entity, 0, sizeof(entity));
	memset(&client, 0, sizeof(client));
	memset(&captured_view, 0, sizeof(captured_view));
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
	replay_status = SG_LOCALIZATION_OK;
	compact_belief_provider_active = 0;
	compact_belief_provider_calls = 0;
	memset(&captured_belief_provider, 0, sizeof(captured_belief_provider));
	binding.bound = 1U;
	binding.model = &compact_model;
	binding.spatial_index =
		(const sg_rune_compact_spatial_index_t *)(uintptr_t)1U;
	binding.rune_identity = 11U;
	binding.topology_revision = 13U;
	binding.identity.physics.frame_ms = 100U;
	binding.host_authority.epoch = 5U;
	binding.host_authority.epoch_complement = ~UINT64_C(5);
	binding.observation_owner = *SG_BotLocalizationObservationOwner();
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
	const sg_strategy_runtime_bot_observation_owner_t *strategy_owner;
	const sg_strategy_runtime_bot_observation_t *strategy_observation;
	sg_strategy_runtime_bot_observation_view_t strategy_view;
	edict_t hook;
	edict_t hook_target;
	const sg_compact_localized_state_t *before;
	int calls_before;

	ResetFixture();
	/* Provider installation owns the reset of every bot slot. */
	sg_bots[1].localization_subject.client_id = 99U;
	sg_bots[1].localized_state.valid = 1U;
	CHECK(SG_BotLocalizationProviderSet(&binding));
	CHECK(compact_belief_provider_active);
	CHECK(captured_belief_provider.model == &compact_model);
	CHECK(captured_belief_provider.identity == &compact_model.identity);
	CHECK(captured_belief_provider.rune_identity == binding.rune_identity);
	CHECK(captured_belief_provider.topology_revision ==
		binding.topology_revision);
	CHECK(captured_belief_provider.generation == binding.host_authority.epoch);
	CHECK(captured_belief_provider.current != NULL);
	CHECK(sg_bots[1].localization_subject.client_id == 0U);
	CHECK(sg_bots[1].localized_state.valid == 0U);
	CHECK(SG_BotLocalizationCurrent(&sg_bots[0]) == NULL);

	/* The first sample is owner state, never a fabricated Pmove result. */
	level.framenum = 0;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(subject_state_calls == 1);
	CHECK(observe_calls == 1);
	CHECK(replay_status == SG_LOCALIZATION_UNAUTHENTICATED);
	CHECK(captured_view.kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN);
	CHECK(!captured_previous_present);
	CHECK(!captured_pmove_result_present);
	CHECK(captured_state_observation_present);
	CHECK(captured_state_observation.state.pm_type == PM_NORMAL);
	CHECK(captured_state_observation.origin[0] == entity.s.origin[0]);
	CHECK(captured_view.maximum_recovery_distance == 0.0f);
	CHECK(captured_view.maximum_temporary_absence_ms == 0U);
	CHECK(captured_view.previous_subject.client_id == 0U);
	CHECK(captured_view.previous_subject.spawn_generation == 0U);
	CHECK(captured_view.model_stamp.identity == binding.rune_identity);
	CHECK(captured_view.model_stamp.generation == binding.topology_revision);
	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL);
	CHECK(current->valid == 1U);
	CHECK(SG_BotLocalizationCell(&sg_bots[0]) == observed_cell);
	CHECK(current->recovery == SG_LOCALIZATION_RECOVERY_NONE);
	CHECK(current->position[0] == entity.s.origin[0]);

	/* The bot-only host observation is one-use and exact. IDLE is observed,
	 * not supplied by the bridge, and any shared-hook mismatch revokes it. */
	strategy_owner = SG_BotLocalizationStrategyObservationOwner();
	CHECK(strategy_owner != NULL && strategy_owner->validate != NULL &&
		strategy_owner->current != NULL);
	strategy_observation = SG_BotLocalizationStrategyObservationIssue(
		&sg_bots[0], current);
	CHECK(strategy_observation != NULL);
	memset(&strategy_view, 0, sizeof(strategy_view));
	CHECK(strategy_owner->validate(strategy_owner->context,
		strategy_observation, &strategy_view));
	CHECK(strategy_view.hook_phase == SG_HOST_HOOK_IDLE);
	CHECK(!strategy_owner->validate(strategy_owner->context,
		strategy_observation, &strategy_view));
	CHECK(strategy_owner->current(strategy_owner->context, &strategy_view));
	client.hookstate = 1;
	CHECK(!strategy_owner->current(strategy_owner->context, &strategy_view));
	client.hookstate = 0;

	memset(&hook, 0, sizeof(hook));
	memset(&hook_target, 0, sizeof(hook_target));
	hook.inuse = true;
	hook.owner = &entity;
	client.hook = &hook;
	client.hookstate = 1;
	sg_bots[0].hook_phase = 2;
	sg_bots[0].hook_entity = &hook;
	strategy_observation = SG_BotLocalizationStrategyObservationIssue(
		&sg_bots[0], current);
	CHECK(strategy_observation != NULL);
	CHECK(strategy_owner->validate(strategy_owner->context,
		strategy_observation, &strategy_view));
	CHECK(strategy_view.hook_phase == SG_HOST_HOOK_IN_FLIGHT);

	hook_target.inuse = true;
	hook_target.mins[0] = -8.0f;
	hook_target.mins[1] = -8.0f;
	hook_target.mins[2] = -8.0f;
	hook_target.maxs[0] = 8.0f;
	hook_target.maxs[1] = 8.0f;
	hook_target.maxs[2] = 8.0f;
	hook.hook_target = &hook_target;
	client.hookstate = 2;
	client.hooklength = 64;
	strategy_observation = SG_BotLocalizationStrategyObservationIssue(
		&sg_bots[0], current);
	CHECK(strategy_observation != NULL);
	CHECK(strategy_owner->validate(strategy_owner->context,
		strategy_observation, &strategy_view));
	CHECK(strategy_view.hook_phase == SG_HOST_HOOK_ATTACHED);
	CHECK(strategy_view.hook_length == 64.0f);
	CHECK(strategy_view.target_radius > 0.0f);

	client.hook = NULL;
	client.hookstate = 0;
	sg_bots[0].hook_phase = 3;
	strategy_observation = SG_BotLocalizationStrategyObservationIssue(
		&sg_bots[0], current);
	CHECK(strategy_observation != NULL);
	CHECK(strategy_owner->validate(strategy_owner->context,
		strategy_observation, &strategy_view));
	CHECK(strategy_view.hook_phase == SG_HOST_HOOK_COAST);
	/* hook_entity is a historical identity in COAST and is never dereferenced. */
	sg_bots[0].hook_phase = 0;
	sg_bots[0].hook_entity = NULL;

	/* A normal frame carries the exact Pmove result and prior compact state. */
	before = current;
	level.framenum = 1;
	SG_BotLocalizationFrameBegin(&sg_bots[0]);
	CHECK(subject_state_calls == 1);
	request = PmoveRequest(before);
	result = PmoveResult(PMF_DUCKED, 11.0f);
	SG_BotLocalizationObservePmove(&entity, &request, &result);
	CHECK(observe_calls == 2);
	CHECK(captured_view.kind == SG_LOCALIZATION_OBSERVATION_PRESENT);
	CHECK(captured_previous_present);
	CHECK(captured_previous.location.cell.value == before->location.cell.value);
	CHECK(captured_pmove_result_present);
	CHECK(captured_pmove_result == &result);
	CHECK(!captured_state_observation_present);
	CHECK(captured_view.maximum_recovery_distance ==
		SG_COMPACT_LOCALIZATION_MAX_RECOVERY_DISTANCE);
	CHECK(captured_view.maximum_temporary_absence_ms == 0U);
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
	CHECK(captured_view.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED);
	CHECK(captured_previous_present);
	CHECK(captured_previous.location.cell.value == before->location.cell.value);
	CHECK(captured_pmove_result_present);
	CHECK(captured_pmove_result == &result);
	CHECK(!captured_state_observation_present);
	CHECK(captured_view.maximum_recovery_distance == 0.0f);
	current = SG_BotLocalizationCurrent(&sg_bots[0]);
	CHECK(current != NULL);
	CHECK(SG_BotLocalizationCell(&sg_bots[0]) == observed_cell);
	CHECK(current->recovery == SG_LOCALIZATION_RECOVERY_NONE);

	/* Compact localization has no temporary-absence lifecycle. */
	calls_before = observe_calls;
	level.framenum = 3;
	SG_BotLocalizationFrameEnd(&sg_bots[0]);
	CHECK(observe_calls == calls_before);
	CHECK(captured_view.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED);
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
	CHECK(!compact_belief_provider_active);
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
