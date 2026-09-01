#include "../slipgate/sg_compact_runtime_level.h"
#include "../slipgate/sg_tactic_execution_owner_private.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sg_rune_compact_field_service_s
{
	uint8_t live;
	const sg_rune_compact_model_t *model;
};

struct sg_tactic_execution_owner_s
{
	uint8_t live;
};

static struct sg_rune_compact_field_service_s service_storage;
static int failures;
static int allow_bind = 1;
static int allow_localization_provider = 1;
static int allow_strategy_provider = 1;
static int allow_tactic_provider = 1;
static int allow_execution_owner = 1;
static int strategy_installed;
static int tactic_installed;
static struct sg_tactic_execution_owner_s execution_owner_storage;
static int event_count;
static int events[16];

struct sg_compact_localization_observation_s
{
	uint32_t token;
};

static struct sg_compact_localization_observation_s observation = { 1U };

static sg_localization_status_t ValidateObservation(void *context,
	const sg_host_law_runtime_authority_t *authority,
	const sg_compact_localization_observation_t *candidate,
	sg_compact_localization_observation_view_t *view_out)
{
	(void)context;
	(void)authority;
	(void)candidate;
	(void)view_out;
	return SG_LOCALIZATION_OK;
}

static const sg_compact_localization_observation_owner_t observation_owner = {
	NULL, ValidateObservation
};
static const sg_rune_compact_spatial_index_t *const spatial_index =
	(const sg_rune_compact_spatial_index_t *)(uintptr_t)1U;

enum
{
	EVENT_BIND = 1,
	EVENT_BOT_INSTALL,
	EVENT_STRATEGY_INSTALL,
	EVENT_TACTIC_INSTALL,
	EVENT_EXECUTION_INSTALL,
	EVENT_EXECUTION_CLEAR,
	EVENT_TACTIC_CLEAR,
	EVENT_STRATEGY_CLEAR,
	EVENT_BOT_CLEAR,
	EVENT_UNBIND,
	EVENT_SERVICE_DESTROY
};

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Event(int value)
{
	if (event_count < (int)(sizeof(events) / sizeof(events[0])))
		events[event_count++] = value;
}

static void ResetEvents(void)
{
	event_count = 0;
	memset(events, 0, sizeof(events));
}

sg_rune_compact_field_service_status_t SG_RuneCompactFieldServiceCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	uint64_t rune_identity, uint64_t topology_revision,
	sg_rune_compact_field_service_t **service_out,
	sg_rune_compact_error_t *model_error_out)
{
	(void)model_error_out;
	if (!model || !expected_identity || !service_out || rune_identity == 0U ||
		topology_revision == 0U)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	service_storage.live = 1U;
	service_storage.model = model;
	*service_out = &service_storage;
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

void SG_RuneCompactFieldServiceDestroy(
	sg_rune_compact_field_service_t *service)
{
	if (service != NULL)
	{
		Event(EVENT_SERVICE_DESTROY);
		service_storage.live = 0U;
		service_storage.model = NULL;
	}
}

const sg_rune_compact_model_t *SG_RuneCompactFieldServiceModel(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL && service_storage.live ? service_storage.model : NULL;
}

uint64_t SG_RuneCompactFieldServiceIdentity(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL && service_storage.live ? 1U : 0U;
}

uint64_t SG_RuneCompactFieldServiceGeneration(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL && service_storage.live ? 1U : 0U;
}

sg_localization_status_t SG_CompactLocalizationBind(
	sg_compact_localization_binding_t *binding,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *candidate_spatial_index,
	const sg_compact_localization_observation_owner_t *candidate_observation_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision)
{
	Event(EVENT_BIND);
	if (!allow_bind || !binding || !model || !expected_identity ||
		!candidate_spatial_index || !candidate_observation_owner ||
		!candidate_observation_owner->validate ||
		!host_authority || rune_identity == 0U || topology_revision == 0U)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	memset(binding, 0, sizeof(*binding));
	binding->model = model;
	binding->spatial_index = candidate_spatial_index;
	binding->identity = *expected_identity;
	binding->host_authority = *host_authority;
	binding->observation_owner = *candidate_observation_owner;
	binding->rune_identity = rune_identity;
	binding->topology_revision = topology_revision;
	binding->bound = 1U;
	return SG_LOCALIZATION_OK;
}

void SG_CompactLocalizationUnbind(
	sg_compact_localization_binding_t *binding)
{
	Event(EVENT_UNBIND);
	if (binding)
		memset(binding, 0, sizeof(*binding));
}

int SG_CompactLocalizationBindingCurrent(
	const sg_compact_localization_binding_t *binding)
{
	return binding != NULL && binding->bound == 1U;
}

sg_localization_status_t SG_CompactLocalizationObserveWithScratch(
	const sg_compact_localization_binding_t *candidate,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localization_scratch_t *scratch,
	sg_compact_localized_state_t *state_out)
{
	(void)previous;
	if (!SG_CompactLocalizationBindingCurrent(candidate) || !sample || !scratch ||
		!state_out || sample->observation != &observation ||
		!scratch->candidates || scratch->candidate_capacity == 0U)
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	memset(state_out, 0, sizeof(*state_out));
	state_out->model_stamp.identity = candidate->rune_identity;
	state_out->model_stamp.generation = candidate->topology_revision;
	state_out->model_stamp.frame_sequence = 9U;
	state_out->location.cell.value = 0U;
	state_out->valid = 1U;
	scratch->candidate_count = 1U;
	return SG_LOCALIZATION_OK;
}

int SG_BotLocalizationProviderSet(
	const sg_compact_localization_binding_t *binding)
{
	Event(binding != NULL ? EVENT_BOT_INSTALL : EVENT_BOT_CLEAR);
	return binding == NULL ? 1 : allow_localization_provider;
}

const struct sg_strategy_runtime_bot_observation_owner_s *
SG_BotLocalizationStrategyObservationOwner(void)
{
	return (const struct sg_strategy_runtime_bot_observation_owner_s *)
		(uintptr_t)1U;
}

int SG_StrategyRuntimeCompactProviderInstall(
	sg_rune_compact_field_service_t *service,
	const struct sg_strategy_runtime_bot_observation_owner_s *bot_observation)
{
	Event(EVENT_STRATEGY_INSTALL);
	if (!allow_strategy_provider || service == NULL || bot_observation == NULL)
		return 0;
	strategy_installed = 1;
	return 1;
}

void SG_StrategyRuntimeCompactProviderClear(
	sg_rune_compact_field_service_t *service)
{
	(void)service;
	Event(EVENT_STRATEGY_CLEAR);
	strategy_installed = 0;
}

int SG_StrategyRuntimeCompactProviderInstalledFor(
	const sg_rune_compact_field_service_t *service)
{
	return strategy_installed && service == &service_storage;
}

int SG_TacticRuntimeProviderInstall(const sg_rune_compact_model_t *model,
	sg_rune_compact_field_service_t *service,
	const sg_compact_localization_binding_t *localization,
	uint64_t rune_identity,
	uint64_t topology_revision)
{
	Event(EVENT_TACTIC_INSTALL);
	if (!allow_tactic_provider || model == NULL || service == NULL ||
		localization == NULL ||
		rune_identity == 0U || topology_revision == 0U)
		return 0;
	tactic_installed = 1;
	return 1;
}

void SG_TacticRuntimeProviderClear(sg_rune_compact_field_service_t *service)
{
	(void)service;
	Event(EVENT_TACTIC_CLEAR);
	tactic_installed = 0;
}

int SG_TacticRuntimeProviderCurrent(
	const sg_rune_compact_field_service_t *service)
{
	return tactic_installed && service == &service_storage;
}

sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCreate(
	sg_tactic_execution_owner_t **owner_out,
	sg_tactic_execution_diagnostic_t *diagnostic_out)
{
	Event(EVENT_EXECUTION_INSTALL);
	if (owner_out)
		*owner_out = NULL;
	if (diagnostic_out)
		memset(diagnostic_out, 0, sizeof(*diagnostic_out));
	if (!allow_execution_owner || !owner_out || !diagnostic_out)
		return SG_TACTIC_EXECUTION_OWNER_ALLOCATION_REJECTED;
	execution_owner_storage.live = 1U;
	*owner_out = &execution_owner_storage;
	return SG_TACTIC_EXECUTION_OWNER_OK;
}

void SG_TacticExecutionOwnerDestroy(sg_tactic_execution_owner_t *owner)
{
	if (owner)
	{
		Event(EVENT_EXECUTION_CLEAR);
		owner->live = 0U;
	}
}

int SG_TacticExecutionOwnerCurrent(
	const sg_tactic_execution_owner_t *owner)
{
	return owner == &execution_owner_storage && owner->live != 0U;
}

static void InitInputs(sg_rune_compact_model_t *model,
	sg_rune_compact_identity_t *identity,
	sg_host_law_runtime_authority_t *authority)
{
	memset(model, 0, sizeof(*model));
	memset(identity, 0, sizeof(*identity));
	memset(authority, 0, sizeof(*authority));
	model->identity.bsp_bytes = 1U;
	model->cells = (const sg_rune_compact_cell_t *)(uintptr_t)1U;
	model->cell_count = 1U;
	*identity = model->identity;
	authority->epoch = 1U;
}

static void CheckEvents(const int *expected, size_t count)
{
	size_t index;

	CHECK(event_count == (int)count);
	for (index = 0U; index < count && index < (size_t)event_count; index++)
		CHECK(events[index] == expected[index]);
}

static void TestSuccessAndTeardown(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity,
	const sg_host_law_runtime_authority_t *authority)
{
	sg_compact_runtime_level_t runtime = SG_COMPACT_RUNTIME_LEVEL_INITIALIZER;
	sg_compact_localization_sample_t sample;
	sg_compact_localized_state_t localized;
	const int install_events[] = { EVENT_BIND, EVENT_BOT_INSTALL,
		EVENT_STRATEGY_INSTALL, EVENT_TACTIC_INSTALL,
		EVENT_EXECUTION_INSTALL };
	const int clear_events[] = { EVENT_EXECUTION_CLEAR, EVENT_TACTIC_CLEAR,
		EVENT_STRATEGY_CLEAR,
		EVENT_BOT_CLEAR, EVENT_UNBIND, EVENT_SERVICE_DESTROY };

	allow_bind = 1;
	allow_localization_provider = 1;
	allow_strategy_provider = 1;
	allow_tactic_provider = 1;
	allow_execution_owner = 1;
	strategy_installed = 0;
	tactic_installed = 0;
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&runtime, model, identity, spatial_index,
		&observation_owner, authority,
		41U, 7U) == SG_COMPACT_RUNTIME_LEVEL_OK);
	CHECK(SG_CompactRuntimeLevelCurrent(&runtime));
	CHECK(runtime.accepted_model == model);
	CHECK(runtime.model_generation == 7U);
	CHECK(SG_CompactRuntimeLevelFieldService(&runtime) == &service_storage);
	CHECK(SG_CompactRuntimeLevelExecutionOwner(&runtime) ==
		&execution_owner_storage);
	memset(&sample, 0, sizeof(sample));
	sample.observation = &observation;
	CHECK(SG_CompactRuntimeLevelObserve(&runtime, &sample, NULL, &localized) ==
		SG_LOCALIZATION_OK);
	CHECK(localized.valid == 1U);
	CHECK(localized.model_stamp.frame_sequence == 9U);
	CHECK(runtime.localization_scratch.candidate_count == 1U);
	sample.observation = NULL;
	CHECK(SG_CompactRuntimeLevelObserve(&runtime, &sample, NULL, &localized) ==
		SG_LOCALIZATION_IDENTITY_MISMATCH);
	CheckEvents(install_events, sizeof(install_events) / sizeof(install_events[0]));
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&runtime, model, identity, spatial_index,
		&observation_owner, authority,
		41U, 7U) == SG_COMPACT_RUNTIME_LEVEL_ALREADY_ACTIVE);
	CHECK(event_count == 0);
	SG_CompactRuntimeLevelClear(&runtime);
	CheckEvents(clear_events, sizeof(clear_events) / sizeof(clear_events[0]));
	CHECK(!SG_CompactRuntimeLevelCurrent(&runtime));
	ResetEvents();
	SG_CompactRuntimeLevelClear(&runtime);
	CHECK(event_count == 0);
}

static void TestRollback(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity,
	const sg_host_law_runtime_authority_t *authority)
{
	sg_compact_runtime_level_t runtime = SG_COMPACT_RUNTIME_LEVEL_INITIALIZER;
	const int localization_events[] = { EVENT_BIND, EVENT_SERVICE_DESTROY };
	const int strategy_events[] = { EVENT_BIND, EVENT_BOT_INSTALL,
		EVENT_STRATEGY_INSTALL, EVENT_STRATEGY_CLEAR, EVENT_BOT_CLEAR,
		EVENT_UNBIND, EVENT_SERVICE_DESTROY };
	const int tactic_events[] = { EVENT_BIND, EVENT_BOT_INSTALL,
		EVENT_STRATEGY_INSTALL, EVENT_TACTIC_INSTALL, EVENT_TACTIC_CLEAR,
		EVENT_STRATEGY_CLEAR, EVENT_BOT_CLEAR, EVENT_UNBIND,
		EVENT_SERVICE_DESTROY };
	const int execution_events[] = { EVENT_BIND, EVENT_BOT_INSTALL,
		EVENT_STRATEGY_INSTALL, EVENT_TACTIC_INSTALL,
		EVENT_EXECUTION_INSTALL, EVENT_TACTIC_CLEAR,
		EVENT_STRATEGY_CLEAR, EVENT_BOT_CLEAR, EVENT_UNBIND,
		EVENT_SERVICE_DESTROY };

	allow_bind = 0;
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&runtime, model, identity, spatial_index,
		&observation_owner, authority,
		41U, 7U) == SG_COMPACT_RUNTIME_LEVEL_LOCALIZATION_REJECTED);
	CheckEvents(localization_events,
		sizeof(localization_events) / sizeof(localization_events[0]));
	CHECK(service_storage.live == 0U);

	allow_bind = 1;
	allow_localization_provider = 1;
	allow_strategy_provider = 0;
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&runtime, model, identity, spatial_index,
		&observation_owner, authority,
		41U, 7U) == SG_COMPACT_RUNTIME_LEVEL_STRATEGY_PROVIDER_REJECTED);
	CheckEvents(strategy_events,
		sizeof(strategy_events) / sizeof(strategy_events[0]));
	CHECK(service_storage.live == 0U);
	CHECK(!SG_CompactRuntimeLevelCurrent(&runtime));

	allow_strategy_provider = 1;
	allow_tactic_provider = 0;
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&runtime, model, identity, spatial_index,
		&observation_owner, authority,
		41U, 7U) == SG_COMPACT_RUNTIME_LEVEL_TACTIC_PROVIDER_REJECTED);
	CheckEvents(tactic_events,
		sizeof(tactic_events) / sizeof(tactic_events[0]));
	CHECK(service_storage.live == 0U);
	CHECK(!SG_CompactRuntimeLevelCurrent(&runtime));
	allow_tactic_provider = 1;

	allow_execution_owner = 0;
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&runtime, model, identity, spatial_index,
		&observation_owner, authority,
		41U, 7U) == SG_COMPACT_RUNTIME_LEVEL_EXECUTION_OWNER_REJECTED);
	CheckEvents(execution_events,
		sizeof(execution_events) / sizeof(execution_events[0]));
	CHECK(service_storage.live == 0U);
	CHECK(!SG_CompactRuntimeLevelCurrent(&runtime));
	allow_execution_owner = 1;
}

static void TestProviderOwnership(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity,
	const sg_host_law_runtime_authority_t *authority)
{
	sg_compact_runtime_level_t first = SG_COMPACT_RUNTIME_LEVEL_INITIALIZER;
	sg_compact_runtime_level_t stale;
	sg_compact_runtime_level_t second = SG_COMPACT_RUNTIME_LEVEL_INITIALIZER;
	uint64_t first_token;
	int events_before;

	allow_bind = 1;
	allow_localization_provider = 1;
	allow_strategy_provider = 1;
	allow_tactic_provider = 1;
	allow_execution_owner = 1;
	strategy_installed = 0;
	tactic_installed = 0;
	ResetEvents();
	CHECK(SG_CompactRuntimeLevelInstall(&first, model, identity, spatial_index,
		&observation_owner, authority, 41U, 7U) ==
		SG_COMPACT_RUNTIME_LEVEL_OK);
	first_token = first.provider_token;
	events_before = event_count;
	CHECK(SG_CompactRuntimeLevelInstall(&second, model, identity, spatial_index,
		&observation_owner, authority, 41U, 7U) ==
		SG_COMPACT_RUNTIME_LEVEL_ALREADY_ACTIVE);
	CHECK(event_count == events_before);
	stale = first;
	SG_CompactRuntimeLevelClear(&first);
	CHECK(SG_CompactRuntimeLevelInstall(&second, model, identity, spatial_index,
		&observation_owner, authority, 41U, 7U) ==
		SG_COMPACT_RUNTIME_LEVEL_OK);
	CHECK(second.provider_token != first_token);
	ResetEvents();
	SG_CompactRuntimeLevelClear(&stale);
	CHECK(event_count == 0);
	CHECK(SG_CompactRuntimeLevelCurrent(&second));
	SG_CompactRuntimeLevelClear(&second);
}

int main(void)
{
	sg_rune_compact_model_t model;
	sg_rune_compact_identity_t identity;
	sg_host_law_runtime_authority_t authority;

	InitInputs(&model, &identity, &authority);
	TestSuccessAndTeardown(&model, &identity, &authority);
	TestRollback(&model, &identity, &authority);
	TestProviderOwnership(&model, &identity, &authority);
	if (failures != 0)
	{
		fprintf(stderr, "%d compact runtime level tests failed\n", failures);
		return 1;
	}
	puts("sg_compact_runtime_level_test: PASS");
	return 0;
}
