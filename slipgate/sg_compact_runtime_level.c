#include "sg_compact_runtime_level.h"

#include "sg_bot_localization.h"

#include <stdlib.h>
#include <string.h>


static sg_compact_runtime_level_t *sg_compact_bot_provider_owner;
static uint64_t sg_compact_bot_provider_next_token = 1U;

static int ProviderOwnedBy(const sg_compact_runtime_level_t *runtime)
{
	return runtime && runtime->provider_token != 0U &&
		sg_compact_bot_provider_owner == runtime &&
		runtime->provider_token == sg_compact_bot_provider_next_token - 1U;
}

static int RuntimeShapeEmpty(const sg_compact_runtime_level_t *runtime)
{
	return runtime != NULL && runtime->active == 0U &&
		runtime->accepted_model == NULL && runtime->field_service == NULL &&
		runtime->localization.bound == 0U &&
		runtime->localization_scratch.candidates == NULL &&
		runtime->localization_scratch.candidate_capacity == 0U &&
		runtime->localization_scratch.candidate_count == 0U &&
		runtime->provider_token == 0U;
}

sg_compact_runtime_level_status_t SG_CompactRuntimeLevelInstall(
	sg_compact_runtime_level_t *runtime,
	const sg_rune_compact_model_t *accepted_model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *spatial_index,
	const sg_compact_localization_observation_owner_t *observation_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision)
{
	sg_compact_runtime_level_t candidate;
	sg_rune_compact_field_service_status_t service_status;
	sg_localization_status_t localization_status;
	int strategy_attempted = 0;
	int tactic_attempted = 0;

	if (!runtime || !accepted_model || !expected_identity || !spatial_index ||
		!observation_owner || !observation_owner->validate || !host_authority ||
		rune_identity == 0U || topology_revision == 0U)
		return SG_COMPACT_RUNTIME_LEVEL_INVALID_ARGUMENT;
	if (!RuntimeShapeEmpty(runtime) || sg_compact_bot_provider_owner != NULL)
		return SG_COMPACT_RUNTIME_LEVEL_ALREADY_ACTIVE;
	if (sg_compact_bot_provider_next_token == 0U ||
		sg_compact_bot_provider_next_token == UINT64_MAX ||
		accepted_model->cell_count == 0U ||
		accepted_model->cell_count > SG_RUNE_COMPACT_MAX_CELLS)
		return SG_COMPACT_RUNTIME_LEVEL_SCRATCH_REJECTED;

	memset(&candidate, 0, sizeof(candidate));
	candidate.localization_scratch.candidates = calloc(
		(size_t)accepted_model->cell_count, sizeof(uint32_t));
	if (!candidate.localization_scratch.candidates)
		return SG_COMPACT_RUNTIME_LEVEL_SCRATCH_REJECTED;
	candidate.localization_scratch.candidate_capacity = accepted_model->cell_count;
	service_status = SG_RuneCompactFieldServiceCreate(accepted_model,
		expected_identity, rune_identity, topology_revision,
		&candidate.field_service, NULL);
	if (service_status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		goto reject_field_service;
	localization_status = SG_CompactLocalizationBind(&candidate.localization,
		accepted_model, expected_identity, spatial_index, observation_owner,
		host_authority, rune_identity, topology_revision);
	if (localization_status != SG_LOCALIZATION_OK)
		goto reject_localization;
	/* SG_BotLocalizationProviderSet copies the binding and, at this same
	 * boundary, installs the compact CACO/belief provider. */
	if (!SG_BotLocalizationProviderSet(&candidate.localization))
		goto reject_localization_provider;
	strategy_attempted = 1;
	if (!SG_StrategyRuntimeCompactProviderInstall(candidate.field_service,
		SG_BotLocalizationStrategyObservationOwner()))
		goto reject_strategy_provider;
	tactic_attempted = 1;
	if (!SG_TacticRuntimeProviderInstall(accepted_model,
		candidate.field_service, &candidate.localization, rune_identity,
		topology_revision))
		goto reject_tactic_provider;

	candidate.accepted_model = accepted_model;
	candidate.rune_identity = rune_identity;
	candidate.model_generation = topology_revision;
	candidate.provider_token = sg_compact_bot_provider_next_token++;
	candidate.active = 1U;
	*runtime = candidate;
	sg_compact_bot_provider_owner = runtime;
	return SG_COMPACT_RUNTIME_LEVEL_OK;

reject_tactic_provider:
	if (tactic_attempted)
		SG_TacticRuntimeProviderClear(candidate.field_service);
reject_strategy_provider:
	if (strategy_attempted)
		SG_StrategyRuntimeCompactProviderClear(candidate.field_service);
	(void)SG_BotLocalizationProviderSet(NULL);
reject_localization_provider:
	SG_CompactLocalizationUnbind(&candidate.localization);
reject_localization:
	SG_RuneCompactFieldServiceDestroy(candidate.field_service);
	free(candidate.localization_scratch.candidates);
	return tactic_attempted ?
		SG_COMPACT_RUNTIME_LEVEL_TACTIC_PROVIDER_REJECTED : strategy_attempted ?
		SG_COMPACT_RUNTIME_LEVEL_STRATEGY_PROVIDER_REJECTED :
		(localization_status != SG_LOCALIZATION_OK ?
			SG_COMPACT_RUNTIME_LEVEL_LOCALIZATION_REJECTED :
			SG_COMPACT_RUNTIME_LEVEL_LOCALIZATION_PROVIDER_REJECTED);

reject_field_service:
	free(candidate.localization_scratch.candidates);
	return SG_COMPACT_RUNTIME_LEVEL_FIELD_SERVICE_REJECTED;
}

void SG_CompactRuntimeLevelClear(sg_compact_runtime_level_t *runtime)
{
	sg_rune_compact_field_service_t *service;

	if (!runtime)
		return;
	if (RuntimeShapeEmpty(runtime))
		return;
	if (runtime->provider_token != 0U && !ProviderOwnedBy(runtime))
	{
		memset(runtime, 0, sizeof(*runtime));
		return;
	}
	service = runtime->field_service;
	/* The provider and all strategy leases must stop referring to this service
	 * before the localization binding or model owner can be retired. */
	SG_TacticRuntimeProviderClear(service);
	SG_StrategyRuntimeCompactProviderClear(service);
	if (ProviderOwnedBy(runtime))
	{
		(void)SG_BotLocalizationProviderSet(NULL);
		sg_compact_bot_provider_owner = NULL;
	}
	SG_CompactLocalizationUnbind(&runtime->localization);
	SG_RuneCompactFieldServiceDestroy(service);
	free(runtime->localization_scratch.candidates);
	memset(runtime, 0, sizeof(*runtime));
}

int SG_CompactRuntimeLevelCurrent(
	const sg_compact_runtime_level_t *runtime)
{
	return runtime != NULL && runtime->active == 1U &&
		runtime->accepted_model != NULL &&
		runtime->field_service != NULL &&
		SG_RuneCompactFieldServiceModel(runtime->field_service) ==
			runtime->accepted_model &&
		SG_CompactLocalizationBindingCurrent(&runtime->localization) &&
		runtime->localization.model == runtime->accepted_model &&
		runtime->localization.rune_identity == runtime->rune_identity &&
		runtime->localization.topology_revision == runtime->model_generation &&
		SG_TacticRuntimeProviderCurrent(runtime->field_service) &&
		runtime->localization_scratch.candidates != NULL &&
		runtime->localization_scratch.candidate_capacity ==
			runtime->accepted_model->cell_count &&
		runtime->localization_scratch.candidate_count <=
			runtime->localization_scratch.candidate_capacity &&
		ProviderOwnedBy(runtime) &&
		SG_StrategyRuntimeCompactProviderInstalledFor(runtime->field_service);
}

const sg_rune_compact_field_service_t *SG_CompactRuntimeLevelFieldService(
	const sg_compact_runtime_level_t *runtime)
{
	return SG_CompactRuntimeLevelCurrent(runtime) ? runtime->field_service : NULL;
}


sg_localization_status_t SG_CompactRuntimeLevelObserve(
	sg_compact_runtime_level_t *runtime,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localized_state_t *state_out)
{
	if (!SG_CompactRuntimeLevelCurrent(runtime))
		return SG_LOCALIZATION_INVALID_BINDING;
	return SG_CompactLocalizationObserveWithScratch(&runtime->localization,
		sample, previous, &runtime->localization_scratch, state_out);
}

const char *SG_CompactRuntimeLevelStatusString(
	sg_compact_runtime_level_status_t status)
{
	static const char *const names[
		SG_COMPACT_RUNTIME_LEVEL_STATUS_COUNT] = {
		"ok",
		"invalid argument",
		"already active",
		"field service rejected",
		"localization rejected",
		"localization provider rejected",
		"strategy provider rejected",
		"tactic provider rejected",
		"scratch rejected"
	};

	return (uint32_t)status <
		(uint32_t)SG_COMPACT_RUNTIME_LEVEL_STATUS_COUNT ? names[status] :
		"unknown compact runtime level status";
}
