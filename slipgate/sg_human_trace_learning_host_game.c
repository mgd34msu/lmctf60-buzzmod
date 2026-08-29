/* Engine-owned v3 evidence derivation and post-match runtime consumption. */
#include "sg_human_trace_learning_host_game.h"
#include "sg_human_trace_learning_game_private.h"
#include "sg_human_trace_learning_store.h"
#include "sg_human_trace.h"
#define SG_HUMAN_TRACE_LEARNING_SPOOL_INTERNAL 1
#include "sg_human_trace_learning_spool_private.h"
#undef SG_HUMAN_TRACE_LEARNING_SPOOL_INTERNAL

#ifdef SG_HUMAN_TRACE_LEARNING_TEST
#include "sg_human_trace_learning_host_game_test.h"
#endif

#include "../g_local.h"
#include "sg_identity.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct sg_human_trace_learning_host_published_runtime_s
{
	sg_human_trace_learning_runtime_t *runtime;
	const sg_rune_runtime_snapshot_t *snapshot;
	sg_level_identity_t level_identity;
	void *evidence_context;
	sg_human_trace_learning_hook_kernel_locator_fn locate_hook_kernel;
	sg_human_trace_learning_store_t *store;
} sg_human_trace_learning_host_published_runtime_t;

typedef enum learning_host_source_kind_e
{
	LEARNING_HOST_SOURCE_HOOK_COST = 0
} learning_host_source_kind_t;

static sg_human_trace_learning_host_published_runtime_t
	sg_human_trace_learning_host_published_runtime;
static sg_human_trace_learning_store_t sg_human_trace_learning_host_store;
static uint8_t sg_human_trace_learning_host_runtime_published;
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
static uint64_t sg_human_trace_learning_host_test_event_visits;
static uint32_t sg_human_trace_learning_host_test_apply_client[MAX_CLIENTS * 4U];
static uint64_t sg_human_trace_learning_host_test_apply_generation[MAX_CLIENTS * 4U];
static size_t sg_human_trace_learning_host_test_apply_order_count;
#endif

static uint32_t LearningHostFloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int LearningHostCompletionToTrace(
	const sg_human_trace_completion_t *completion,
	sg_human_trace_learning_trace_v3_auth_t *trace_out)
{
	if (!completion || !trace_out)
		return 0;
	memset(trace_out, 0, sizeof(*trace_out));
	memcpy(trace_out->terminal_sha256.bytes, completion->terminal_sha256,
		sizeof(trace_out->terminal_sha256.bytes));
	trace_out->session = completion->session;
	trace_out->segment = completion->segment;
	trace_out->continuation = completion->continuation;
	memcpy(trace_out->mapname, completion->mapname,
		sizeof(trace_out->mapname));
	trace_out->bsp_checksum = completion->bsp_checksum;
	trace_out->entity_crc32 = completion->entity_crc32;
	trace_out->host_physics_id = completion->host_physics_id;
	trace_out->gravity_bits = completion->gravity_bits;
	trace_out->airaccelerate_bits = completion->airaccelerate_bits;
	trace_out->maxvelocity_bits = completion->maxvelocity_bits;
	trace_out->pmove_substep_ms = completion->pmove_substep_ms;
	trace_out->server_frame_ms = completion->server_frame_ms;
	trace_out->physics_flags = completion->physics_flags;
	trace_out->module_revision = completion->module_revision;
	memcpy(trace_out->module_version, completion->module_version,
		sizeof(trace_out->module_version));
	trace_out->end_order = completion->end_order;
	trace_out->end_frame = completion->end_frame;
	trace_out->end_level_time_bits = completion->end_level_time_bits;
	return SG_HumanTraceLearningTraceV3AuthValid(trace_out);
}

static int LearningHostTraceCurrent(
	const sg_human_trace_learning_trace_v3_auth_t *trace)
{
	sg_level_identity_t identity;
	cvar_t *airaccelerate;
	uint32_t flags;

	if (!SG_HumanTraceLearningTraceV3AuthValid(trace) ||
		!level.mapname[0] || strncmp(level.mapname, trace->mapname,
			sizeof(trace->mapname)) != 0 ||
		SG_LevelIdentitySnapshot(trace->mapname, &identity) != SG_IDENTITY_OK ||
		identity.bsp_checksum != trace->bsp_checksum ||
		identity.entity_crc32 != trace->entity_crc32 ||
		identity.host_physics_id != trace->host_physics_id || !gi.cvar ||
		!sv_gravity || !sv_maxvelocity || !want_funky_gravity ||
		trace->module_revision != LMCTF_REVISION ||
		strncmp(trace->module_version, LMCTF_VERSION,
			sizeof(trace->module_version)) != 0 ||
		FRAMETIME * 1000.0f != (float)SG_HUMAN_TRACE_LEARNING_SERVER_FRAME_MS)
		return 0;
	airaccelerate = gi.cvar("sv_airaccelerate", "0", 0);
	if (!airaccelerate)
		return 0;
	flags = want_funky_gravity->value != 0.0f ? UINT32_C(1) : 0U;
	return LearningHostFloatBits(sv_gravity->value) == trace->gravity_bits &&
		LearningHostFloatBits(airaccelerate->value) ==
			trace->airaccelerate_bits &&
		LearningHostFloatBits(sv_maxvelocity->value) ==
			trace->maxvelocity_bits && flags == trace->physics_flags;
}

static int LearningHostRuntimeCurrent(sg_human_trace_learning_runtime_t *runtime,
	const sg_rune_runtime_snapshot_t *snapshot)
{
	const sg_human_trace_learning_parameters_t *parameters;

	if (!runtime || !snapshot || !SG_RuneRuntimeSnapshotValid(snapshot) ||
		!(parameters = runtime->parameters) ||
		!SG_HumanTraceLearningParametersValid(parameters) ||
		parameters->domain.snapshot != snapshot ||
		!SG_HumanTraceLearningWorkspaceValid(parameters, &runtime->workspace) ||
		!runtime->playthroughs || runtime->playthrough_capacity == 0U ||
		runtime->next_transaction_id == 0U)
		return 0;
	return parameters->domain.identity.rune_identity == snapshot->identity &&
		parameters->domain.identity.topology_revision ==
			snapshot->topology_revision &&
		parameters->domain.identity.bsp_identity ==
			snapshot->model->identity.bsp_content_id &&
		parameters->domain.identity.physics_identity ==
			snapshot->model->identity.physics_abi_id;
}

static int LearningHostPublishedLevelMatches(
	const sg_level_identity_t *published,
	const sg_level_identity_t *current)
{
	return published && current &&
		published->bsp_checksum == current->bsp_checksum &&
		published->entity_crc32 == current->entity_crc32 &&
		published->host_physics_id == current->host_physics_id &&
		strncmp(published->mapname, current->mapname,
			sizeof(published->mapname)) == 0;
}

static int LearningHostPublishedRuntime(
	sg_human_trace_learning_host_published_runtime_t *published_out)
{
	if (published_out)
		memset(published_out, 0, sizeof(*published_out));
	if (!published_out || sg_human_trace_learning_host_runtime_published != 1U ||
		!LearningHostRuntimeCurrent(sg_human_trace_learning_host_published_runtime.runtime,
			sg_human_trace_learning_host_published_runtime.snapshot))
		return 0;
	*published_out = sg_human_trace_learning_host_published_runtime;
	return 1;
}

static int LearningHostPublishedMatchesTrace(
	const sg_human_trace_learning_host_published_runtime_t *published,
	const sg_human_trace_learning_trace_v3_auth_t *trace)
{
	sg_level_identity_t trace_identity;

	if (!published || !trace || !SG_HumanTraceLearningTraceV3AuthValid(trace) ||
		!LearningHostRuntimeCurrent(published->runtime, published->snapshot) ||
		!published->locate_hook_kernel)
		return 0;
	memset(&trace_identity, 0, sizeof(trace_identity));
	memcpy(trace_identity.mapname, trace->mapname,
		sizeof(trace_identity.mapname));
	trace_identity.bsp_checksum = trace->bsp_checksum;
	trace_identity.entity_crc32 = trace->entity_crc32;
	trace_identity.host_physics_id = trace->host_physics_id;
	return LearningHostPublishedLevelMatches(&published->level_identity,
		&trace_identity);
}

static int LearningHostPublishedForTrace(
	sg_human_trace_learning_host_published_runtime_t *published_out,
	const sg_human_trace_learning_trace_v3_auth_t *trace)
{
	if (!published_out || !LearningHostPublishedRuntime(published_out))
		return 0;
	return LearningHostPublishedMatchesTrace(published_out, trace);
}

typedef struct learning_host_source_record_s
{
	learning_host_source_kind_t kind;
	sg_human_trace_v3_event_t first;
	sg_human_trace_v3_event_t last;
	sg_human_trace_v3_event_t hook_fire;
	uint64_t effective_cost_us;
} learning_host_source_record_t;

typedef struct learning_host_scope_state_s
{
	sg_human_trace_v3_event_t last_step;
	sg_human_trace_v3_event_t hook_fire;
	sg_human_trace_v3_event_t hook_attach;
	sg_human_trace_v3_event_t hook_terminal;
	uint64_t attached_hook_event;
	uint8_t have_last_step;
	uint8_t have_hook_fire;
	uint8_t hook_attached;
	uint8_t have_hook_terminal;
	uint8_t saw_step_after_attach;
} learning_host_scope_state_t;

typedef int (*learning_host_source_sink_fn)(void *context,
	const learning_host_source_record_t *source);

typedef struct learning_host_capture_context_s
{
	/* Derives records for one accepted client/spawn scope. */
	sg_human_trace_learning_trace_scope_t scope;
	learning_host_scope_state_t state;
	learning_host_source_sink_fn sink;
	void *sink_context;
	uint64_t record_count;
} learning_host_capture_context_t;

typedef struct learning_host_accepted_v3_capability_s
{
	const sg_human_trace_v3_scope_acceptance_t *acceptance;
	const sg_human_trace_v3_spool_ref_t *spool;
	sg_human_trace_learning_trace_scope_t scope;
	sg_human_trace_learning_runtime_t *runtime;
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_human_trace_learning_record_t *records;
	uint64_t record_count;
	uint64_t issuance;
} learning_host_accepted_v3_capability_t;

static learning_host_accepted_v3_capability_t
	sg_human_trace_learning_active_capability;
static uint8_t sg_human_trace_learning_capability_active;
static uint64_t sg_human_trace_learning_capability_issuance;

static int LearningHostSameScope(
	const sg_human_trace_learning_trace_scope_t *scope,
	const sg_human_trace_v3_event_t *event)
{
	return scope && event && scope->client_id == event->client_id &&
		scope->spawn_generation == event->spawn_generation;
}

static void LearningHostClearAttempt(learning_host_scope_state_t *state)
{
	if (!state)
		return;
	state->have_hook_fire = 0U;
	state->hook_attached = 0U;
	state->have_hook_terminal = 0U;
	state->saw_step_after_attach = 0U;
	state->attached_hook_event = 0U;
	memset(&state->hook_fire, 0, sizeof(state->hook_fire));
	memset(&state->hook_attach, 0, sizeof(state->hook_attach));
	memset(&state->hook_terminal, 0, sizeof(state->hook_terminal));
}

static int LearningHostTrajectoryElapsedUs(
	const sg_human_trace_v3_event_t *attach,
	const sg_human_trace_v3_event_t *landing,
	const sg_human_trace_learning_trace_v3_auth_t *trace,
	uint64_t *elapsed_us_out)
{
	uint64_t frames;
	uint64_t elapsed_ms;
	uint64_t elapsed_us;

	if (!attach || !landing || !trace || !elapsed_us_out ||
		attach->order >= landing->order || attach->frame >= landing->frame ||
		trace->server_frame_ms == 0U)
		return 0;
	frames = (uint64_t)landing->frame - (uint64_t)attach->frame;
	if (frames > UINT64_MAX / (uint64_t)trace->server_frame_ms)
		return 0;
	elapsed_ms = frames * (uint64_t)trace->server_frame_ms;
	if (elapsed_ms > UINT64_MAX / UINT64_C(1000))
		return 0;
	elapsed_us = elapsed_ms * UINT64_C(1000);
	if (!SG_HumanTraceLearningEffectiveCostValid(elapsed_us))
		return 0;
	*elapsed_us_out = elapsed_us;
	return 1;
}

static int LearningHostCompleteTraversal(
	learning_host_capture_context_t *context,
	const sg_human_trace_v3_event_t *landing)
{
	learning_host_scope_state_t *state;
	learning_host_source_record_t source;
	uint64_t elapsed_us;

	if (!context || !landing)
		return 0;
	state = &context->state;
	if (!state->have_hook_fire || !state->hook_attached ||
		!state->have_hook_terminal || !state->saw_step_after_attach ||
		!landing->grounded || landing->order <= state->hook_terminal.order ||
		landing->frame < state->hook_terminal.frame ||
		landing->command < state->hook_terminal.after_command)
		return 1;
	if (!LearningHostTrajectoryElapsedUs(&state->hook_attach, landing,
		&context->scope.trace, &elapsed_us))
	{
		LearningHostClearAttempt(state);
		return 1;
	}
	if (context->record_count == UINT64_MAX)
		return 0;
	memset(&source, 0, sizeof(source));
	source.kind = LEARNING_HOST_SOURCE_HOOK_COST;
	source.first = state->hook_attach;
	source.last = *landing;
	source.hook_fire = state->hook_fire;
	source.effective_cost_us = elapsed_us;
	if (context->sink && !context->sink(context->sink_context, &source))
		return 0;
	context->record_count++;
	LearningHostClearAttempt(state);
	return 1;
}

static int LearningHostCaptureEvent(void *opaque,
	const sg_human_trace_v3_event_t *event)
{
	learning_host_capture_context_t *context = opaque;
	learning_host_scope_state_t *state;

	if (!context || !event || event->order == 0U ||
		event->order >= context->scope.trace.end_order ||
		event->frame > context->scope.trace.end_frame || event->client_id == 0U ||
		event->spawn_generation == 0U)
		return 0;
	if (!LearningHostSameScope(&context->scope, event))
		return 1;
	state = &context->state;
	switch (event->kind)
	{
	case SG_HUMAN_TRACE_V3_EVENT_STEP:
		state->last_step = *event;
		state->have_last_step = 1U;
		if (state->hook_attached && event->order > state->hook_attach.order)
		{
			if (!state->have_hook_terminal)
				state->saw_step_after_attach = 1U;
		}
		if (state->have_hook_terminal && event->grounded &&
			!LearningHostCompleteTraversal(context, event))
			return 0;
		return 1;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE:
		if (!state->have_last_step ||
			event->after_command != state->last_step.command)
			return 1;
		LearningHostClearAttempt(state);
		state->hook_fire = *event;
		state->have_hook_fire = 1U;
		return 1;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH:
		if (!state->have_hook_fire || !state->have_last_step ||
			event->after_command < state->hook_fire.after_command ||
			event->after_command > state->last_step.command ||
			event->hook_entity != state->hook_fire.hook_entity ||
			event->hook_event <= state->hook_fire.hook_event)
			return 1;
		state->hook_attach = *event;
		state->attached_hook_event = event->hook_event;
		state->hook_attached = 1U;
		state->have_hook_terminal = 0U;
		state->saw_step_after_attach = 0U;
		return 1;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_RELEASE:
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET:
		if (!state->hook_attached || state->have_hook_terminal ||
			!state->saw_step_after_attach || !state->have_last_step ||
			event->after_command < state->hook_attach.after_command ||
			event->after_command > state->last_step.command ||
			event->hook_entity != state->hook_attach.hook_entity ||
			event->hook_event <= state->attached_hook_event)
			return 1;
		state->hook_terminal = *event;
		state->have_hook_terminal = 1U;
		return 1;
	case SG_HUMAN_TRACE_V3_EVENT_KIND_COUNT:
	default:
		return 0;
	}
}

static int LearningHostKernelMatches(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_human_trace_learning_kernel_key_t *key)
{
	uint32_t index;

	if (!snapshot || !snapshot->model || !SG_HumanTraceLearningKernelKeyValid(key))
		return 0;
	for (index = 0U; index < snapshot->model->kernel_count; index++)
	{
		const sg_rune_capability_kernel_t *candidate =
			&snapshot->model->kernels[index];

		if (candidate->family == SG_RUNE_CAPABILITY_HOOK_TRAJECTORY &&
			SG_RuneModelStableIdEqual(&candidate->id.value, &key->kernel.value))
			return 1;
	}
	return 0;
}

static int LearningHostCapturedAt(const sg_human_trace_v3_event_t *event,
	const sg_human_trace_learning_trace_v3_auth_t *trace, uint64_t *captured_at_out)
{
	uint64_t frame_ms;

	if (!event || !trace || !captured_at_out || event->order == 0U)
		return 0;
	frame_ms = (uint64_t)event->frame * (uint64_t)trace->server_frame_ms;
	if (UINT64_MAX - frame_ms < event->order)
		return 0;
	*captured_at_out = frame_ms + event->order;
	return 1;
}

static int LearningHostSourceRecord(
	const sg_human_trace_learning_host_published_runtime_t *published,
	const sg_human_trace_learning_trace_scope_t *scope,
	const learning_host_source_record_t *source,
	sg_human_trace_learning_record_t *record_out)
{
	const sg_human_trace_learning_parameters_t *parameters;
	uint64_t captured_at;
	sg_human_trace_learning_kernel_key_t key;

	if (!published || !scope || !source || !record_out ||
		!LearningHostSameScope(scope, &source->first) ||
		!LearningHostSameScope(scope, &source->last) ||
		!LearningHostSameScope(scope, &source->hook_fire) ||
		!LearningHostCapturedAt(&source->first, &scope->trace, &captured_at))
		return 0;
	parameters = published->runtime->parameters;
	memset(record_out, 0, sizeof(*record_out));
	record_out->first_frame = source->first.frame;
	record_out->last_frame = source->last.frame;
	record_out->first_order = source->first.order;
	record_out->last_order = source->last.order;
	record_out->update.evidence.evidence_id = source->first.order;
	record_out->update.evidence.identity = parameters->domain.identity;
	record_out->update.evidence.trace = scope->trace.terminal_sha256;
	record_out->update.evidence.captured_at_ms = captured_at;
	if (source->kind != LEARNING_HOST_SOURCE_HOOK_COST ||
		source->first.kind != SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH ||
		source->last.kind != SG_HUMAN_TRACE_V3_EVENT_STEP ||
		!source->last.grounded || source->first.order >= source->last.order ||
		source->first.frame >= source->last.frame ||
		source->hook_fire.kind != SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE ||
		source->hook_fire.order >= source->first.order ||
		source->hook_fire.frame > source->first.frame ||
		!SG_HumanTraceLearningEffectiveCostValid(source->effective_cost_us) ||
		!published->locate_hook_kernel ||
		!published->locate_hook_kernel(published->evidence_context,
			published->snapshot, &source->hook_fire, &source->first, &key) ||
		!LearningHostKernelMatches(published->snapshot, &key))
		return 0;
	record_out->update.kind = SG_HUMAN_TRACE_LEARNING_UPDATE_COST;
	record_out->update.key = key;
	record_out->update.effective_cost_us = source->effective_cost_us;
	return SG_HumanTraceLearningUpdateValid(&record_out->update);
}

static int LearningHostRuntimeApplyValid(
	const sg_human_trace_learning_runtime_t *runtime)
{
	return runtime && SG_HumanTraceLearningParametersValid(runtime->parameters) &&
		SG_HumanTraceLearningWorkspaceValid(runtime->parameters, &runtime->workspace) &&
		runtime->playthroughs && runtime->playthrough_capacity != 0U &&
		runtime->next_transaction_id != 0U;
}

typedef struct learning_host_scope_apply_context_s
{
	const sg_human_trace_learning_host_published_runtime_t *published;
	const sg_human_trace_learning_trace_scope_t *scope;
	sg_human_trace_learning_parameters_t candidate;
	uint64_t next_transaction_id;
	uint64_t record_count;
	uint64_t last_order;
	uint32_t last_frame;
} learning_host_scope_apply_context_t;

typedef enum learning_host_scope_apply_result_e
{
	LEARNING_HOST_SCOPE_REJECTED = 0,
	LEARNING_HOST_SCOPE_EMPTY,
	LEARNING_HOST_SCOPE_COMMITTED
} learning_host_scope_apply_result_t;

typedef struct learning_host_staged_records_s
{
	const sg_human_trace_learning_host_published_runtime_t *published;
	const sg_human_trace_learning_trace_scope_t *scope;
	sg_human_trace_learning_record_t *records;
	uint64_t record_count;
	uint64_t record_capacity;
} learning_host_staged_records_t;

static int LearningHostStageSourceRecord(void *opaque,
	const learning_host_source_record_t *source)
{
	learning_host_staged_records_t *stage = opaque;
	sg_human_trace_learning_record_t record;

	if (!stage || !source || !stage->published || !stage->scope ||
		!LearningHostSourceRecord(stage->published, stage->scope, source,
			&record) || stage->record_count == UINT64_MAX)
		return 0;
	if (stage->record_count == stage->record_capacity)
	{
		sg_human_trace_learning_record_t *grown;
		uint64_t capacity = stage->record_capacity ?
			stage->record_capacity * UINT64_C(2) : UINT64_C(8);

		if (capacity < stage->record_capacity || capacity >
			(uint64_t)(SIZE_MAX / sizeof(*grown)))
			return 0;
		grown = realloc(stage->records, (size_t)capacity * sizeof(*grown));
		if (!grown)
			return 0;
		stage->records = grown;
		stage->record_capacity = capacity;
	}
	stage->records[stage->record_count] = record;
	stage->record_count++;
	return 1;
}

static int LearningHostApplyStagedRecord(
	learning_host_scope_apply_context_t *context,
	const sg_human_trace_learning_record_t *record)
{
	sg_human_trace_learning_transaction_t transaction;

	if (!context || !record || !context->scope ||
		!SG_HumanTraceLearningRecordValid(context->scope, record) ||
		(context->record_count != 0U &&
			(record->first_order <= context->last_order ||
			 record->first_frame < context->last_frame)) ||
		context->next_transaction_id == UINT64_MAX ||
		!SG_HumanTraceLearningTransactionBegin(&context->candidate,
			&record->update, context->next_transaction_id, &transaction) ||
		!SG_HumanTraceLearningApplyUpdate(&context->candidate, &record->update,
			&transaction) || !SG_HumanTraceLearningTransactionCommit(
			&context->candidate, &transaction))
		return 0;
	context->record_count++;
	context->last_order = record->last_order;
	context->last_frame = record->last_frame;
	context->next_transaction_id++;
	return 1;
}

static int LearningHostIssueAcceptedV3Capability(
	const sg_human_trace_learning_host_published_runtime_t *published,
	const sg_human_trace_v3_scope_acceptance_t *acceptance,
	const sg_human_trace_v3_spool_ref_t *spool,
	const sg_human_trace_learning_trace_scope_t *scope,
	const sg_human_trace_learning_record_t *records,
	uint64_t record_count)
{
	sg_human_trace_learning_trace_v3_auth_t trace;
	const sg_human_trace_v3_spool_ref_t *accepted_spool;
	uint32_t accepted_client;
	uint64_t accepted_generation;

	if (!published || !acceptance || !spool || !scope ||
		(record_count != 0U && !records) ||
		sg_human_trace_learning_capability_active != 0U ||
		!SG_HumanTraceAcceptedV3ScopeView(acceptance, &accepted_spool,
			&accepted_client, &accepted_generation) || accepted_spool != spool ||
		accepted_client != scope->client_id ||
		accepted_generation != scope->spawn_generation ||
		!LearningHostCompletionToTrace(&spool->completion, &trace) ||
		!SG_HumanTraceLearningTraceV3AuthEqual(&trace, &scope->trace) ||
		!LearningHostPublishedMatchesTrace(published, &scope->trace) ||
		!LearningHostRuntimeCurrent(published->runtime, published->snapshot) ||
		sg_human_trace_learning_capability_issuance == UINT64_MAX)
		return 0;
	memset(&sg_human_trace_learning_active_capability, 0,
		sizeof(sg_human_trace_learning_active_capability));
	sg_human_trace_learning_capability_issuance++;
	sg_human_trace_learning_active_capability.acceptance = acceptance;
	sg_human_trace_learning_active_capability.spool = spool;
	sg_human_trace_learning_active_capability.scope = *scope;
	sg_human_trace_learning_active_capability.runtime = published->runtime;
	sg_human_trace_learning_active_capability.snapshot = published->snapshot;
	sg_human_trace_learning_active_capability.records = records;
	sg_human_trace_learning_active_capability.record_count = record_count;
	sg_human_trace_learning_active_capability.issuance =
		sg_human_trace_learning_capability_issuance;
	sg_human_trace_learning_capability_active = 1U;
	return 1;
}

static void LearningHostRevokeAcceptedV3Capability(void)
{
	memset(&sg_human_trace_learning_active_capability, 0,
		sizeof(sg_human_trace_learning_active_capability));
	sg_human_trace_learning_capability_active = 0U;
}

static learning_host_scope_apply_result_t LearningHostApplyAcceptedV3Capability(
	const sg_human_trace_learning_host_published_runtime_t *published)
{
	const learning_host_accepted_v3_capability_t *capability =
		&sg_human_trace_learning_active_capability;
	learning_host_scope_apply_context_t context;
	uint64_t index;

	if (!published || sg_human_trace_learning_capability_active != 1U ||
		capability->issuance == 0U || capability->issuance !=
			sg_human_trace_learning_capability_issuance || !capability->spool ||
		!SG_HumanTraceAcceptedV3ScopeView(capability->acceptance, NULL, NULL,
			NULL) ||
		!LearningHostRuntimeCurrent(capability->runtime, capability->snapshot) ||
		capability->runtime != published->runtime ||
		capability->snapshot != published->snapshot ||
		!LearningHostPublishedMatchesTrace(published, &capability->scope.trace))
		return LEARNING_HOST_SCOPE_REJECTED;
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
	if (sg_human_trace_learning_host_test_apply_order_count <
		MAX_CLIENTS * 4U)
	{
		size_t applied = sg_human_trace_learning_host_test_apply_order_count++;

		sg_human_trace_learning_host_test_apply_client[applied] =
			capability->scope.client_id;
		sg_human_trace_learning_host_test_apply_generation[applied] =
			capability->scope.spawn_generation;
	}
#endif
	if (!published->store || SG_HumanTraceLearningStoreScopeConsumed(
		published->store, &capability->scope))
		return LEARNING_HOST_SCOPE_EMPTY;
	/* Even an empty authenticated life is incorporated in the same durable
	 * state image used to suppress its replay. */
	if (capability->record_count == 0U)
		return SG_HumanTraceLearningStoreCommitScope(published->store,
			capability->runtime->parameters,
			capability->runtime->next_transaction_id, 0U, &capability->scope)
			? LEARNING_HOST_SCOPE_EMPTY : LEARNING_HOST_SCOPE_REJECTED;
	if (!LearningHostRuntimeApplyValid(capability->runtime))
		return LEARNING_HOST_SCOPE_REJECTED;
	memset(&context, 0, sizeof(context));
	if (!SG_HumanTraceLearningParametersClone(capability->runtime->parameters,
		&capability->runtime->workspace, &context.candidate))
		return LEARNING_HOST_SCOPE_REJECTED;
	context.published = published;
	context.scope = &capability->scope;
	context.next_transaction_id = capability->runtime->next_transaction_id;
	if (!capability->records)
		return LEARNING_HOST_SCOPE_REJECTED;
	for (index = 0U; index < capability->record_count; index++)
	{
		if (!LearningHostApplyStagedRecord(&context,
			&capability->records[index]))
			return LEARNING_HOST_SCOPE_REJECTED;
	}
	if (context.record_count != capability->record_count)
		return LEARNING_HOST_SCOPE_REJECTED;
	if (!SG_HumanTraceLearningStoreCommitScope(published->store,
		&context.candidate, context.next_transaction_id, context.record_count,
		&capability->scope))
		return LEARNING_HOST_SCOPE_REJECTED;
	if (!SG_HumanTraceLearningParametersReplace(capability->runtime->parameters,
		&context.candidate))
	{
		if (!SG_HumanTraceLearningStoreRestore(published->store,
			capability->runtime->parameters,
			&capability->runtime->next_transaction_id))
			return LEARNING_HOST_SCOPE_REJECTED;
	}
	else
		capability->runtime->next_transaction_id = context.next_transaction_id;
	return LEARNING_HOST_SCOPE_COMMITTED;
}

static int LearningHostReportAdd(uint64_t *value, uint64_t amount)
{
	if (!value)
		return 1;
	if (UINT64_MAX - *value < amount)
		return 0;
	*value += amount;
	return 1;
}

typedef struct learning_host_stream_scope_s
{
	const sg_human_trace_v3_scope_acceptance_t *acceptance;
	learning_host_capture_context_t capture;
	learning_host_staged_records_t staged;
	uint8_t consumed;
} learning_host_stream_scope_t;

typedef struct learning_host_stream_context_s
{
	const sg_human_trace_learning_host_published_runtime_t *published;
	const sg_human_trace_v3_spool_ref_t *spool;
	sg_human_trace_learning_trace_v3_auth_t trace;
	sg_human_trace_learning_host_report_t *report;
	learning_host_stream_scope_t **scopes;
	size_t scope_count;
	size_t scope_capacity;
	learning_host_stream_scope_t *current[MAX_CLIENTS];
	uint8_t selected;
} learning_host_stream_context_t;

#ifdef SG_HUMAN_TRACE_LEARNING_TEST
void SG_HumanTraceLearningHostGameTestResetVisitCount(void)
{
	sg_human_trace_learning_host_test_event_visits = 0U;
}

uint64_t SG_HumanTraceLearningHostGameTestVisitCount(void)
{
	return sg_human_trace_learning_host_test_event_visits;
}

void SG_HumanTraceLearningHostGameTestResetApplyOrder(void)
{
	memset(sg_human_trace_learning_host_test_apply_client, 0,
		sizeof(sg_human_trace_learning_host_test_apply_client));
	memset(sg_human_trace_learning_host_test_apply_generation, 0,
		sizeof(sg_human_trace_learning_host_test_apply_generation));
	sg_human_trace_learning_host_test_apply_order_count = 0U;
}

size_t SG_HumanTraceLearningHostGameTestApplyOrderCount(void)
{
	return sg_human_trace_learning_host_test_apply_order_count;
}

int SG_HumanTraceLearningHostGameTestApplyOrder(size_t index,
	uint32_t *client_id_out, uint64_t *spawn_generation_out)
{
	if (!client_id_out || !spawn_generation_out ||
		index >= sg_human_trace_learning_host_test_apply_order_count)
		return 0;
	*client_id_out = sg_human_trace_learning_host_test_apply_client[index];
	*spawn_generation_out =
		sg_human_trace_learning_host_test_apply_generation[index];
	return 1;
}
#endif

static void LearningHostStreamDiscardScope(learning_host_stream_scope_t *scope)
{
	if (!scope)
		return;
	free(scope->staged.records);
	memset(scope, 0, sizeof(*scope));
	free(scope);
}

static int LearningHostFinalizeStreamScope(
	learning_host_stream_context_t *stream,
	learning_host_stream_scope_t *scope)
{
	learning_host_scope_apply_result_t result;
	uint64_t records;
	int accepted;

	if (!stream || !scope || !scope->acceptance ||
		!SG_HumanTraceLearningTraceScopeValid(&scope->capture.scope) ||
		(stream->published && scope->capture.record_count !=
			scope->staged.record_count))
		return 0;
	records = scope->capture.record_count;
	if (!stream->published)
	{
		if (!scope->consumed && records != 0U && stream->report &&
			(!LearningHostReportAdd(&stream->report->queued_batches, 1U) ||
			 !LearningHostReportAdd(&stream->report->derived_batches, 1U) ||
			 !LearningHostReportAdd(&stream->report->derived_records, records) ||
			 !LearningHostReportAdd(&stream->report->pending_batches, 1U)))
			return 0;
		return 1;
	}
	if (!scope->consumed && records != 0U && stream->report &&
		!LearningHostReportAdd(&stream->report->queued_batches, 1U))
		return 0;
	if (!LearningHostIssueAcceptedV3Capability(stream->published,
		scope->acceptance, stream->spool, &scope->capture.scope,
		scope->staged.records, records))
		return 0;
	result = LearningHostApplyAcceptedV3Capability(stream->published);
	LearningHostRevokeAcceptedV3Capability();
	accepted = result != LEARNING_HOST_SCOPE_REJECTED;
	if (!accepted)
	{
		if (stream->report)
			(void)LearningHostReportAdd(&stream->report->rejected_batches, 1U);
		return 0;
	}
	if (result == LEARNING_HOST_SCOPE_COMMITTED && stream->report &&
		!LearningHostReportAdd(&stream->report->committed_batches, 1U))
		return 0;
	return 1;
}

static int LearningHostAppendStreamScope(
	learning_host_stream_context_t *stream,
	const sg_human_trace_v3_scope_acceptance_t *acceptance,
	const sg_human_trace_v3_event_t *event)
{
	learning_host_stream_scope_t **grown;
	learning_host_stream_scope_t *scope;
	const sg_human_trace_v3_spool_ref_t *accepted_spool;
	uint32_t accepted_client;
	uint64_t accepted_generation;
	size_t capacity;

	if (!stream || !acceptance || !event ||
		event->client_id == 0U || event->client_id > MAX_CLIENTS)
		return 0;
	if (!SG_HumanTraceAcceptedV3ScopeView(acceptance, &accepted_spool,
		&accepted_client, &accepted_generation) || accepted_spool != stream->spool ||
		accepted_client != event->client_id ||
		accepted_generation != event->spawn_generation)
		return 0;
	if (stream->scope_count == stream->scope_capacity)
	{
		capacity = stream->scope_capacity ? stream->scope_capacity * 2U : 8U;
		if (capacity < stream->scope_capacity ||
			capacity > SIZE_MAX / sizeof(*grown))
			return 0;
		grown = realloc(stream->scopes, capacity * sizeof(*grown));
		if (!grown)
			return 0;
		stream->scopes = grown;
		stream->scope_capacity = capacity;
	}
	scope = calloc(1U, sizeof(*scope));
	if (!scope)
		return 0;
	scope->acceptance = acceptance;
	scope->capture.scope.trace = stream->trace;
	scope->capture.scope.client_id = accepted_client;
	scope->capture.scope.spawn_generation = accepted_generation;
	if (!SG_HumanTraceLearningTraceScopeValid(&scope->capture.scope))
	{
		free(scope);
		return 0;
	}
	scope->consumed = stream->published && stream->published->store &&
		SG_HumanTraceLearningStoreScopeConsumed(stream->published->store,
			&scope->capture.scope) ? 1U : 0U;
	/* Stage published evidence even when a receipt was present at open. The
	 * receipt may disappear before finish; retaining the records makes that
	 * race retryable instead of turning it into an empty acknowledged scope. */
	if (stream->published)
	{
		scope->staged.published = stream->published;
		scope->staged.scope = &scope->capture.scope;
		scope->capture.sink = LearningHostStageSourceRecord;
		scope->capture.sink_context = &scope->staged;
	}
	stream->scopes[stream->scope_count++] = scope;
	stream->current[event->client_id - 1U] = scope;
	return LearningHostCaptureEvent(&scope->capture, event);
}

static int LearningHostStreamEvent(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *acceptance,
	const sg_human_trace_v3_event_t *event)
{
	learning_host_stream_context_t *stream = opaque;
	learning_host_stream_scope_t *scope;

	if (!stream || !acceptance || !event || !stream->spool ||
		event->order == 0U ||
		event->order >= stream->trace.end_order ||
		event->frame > stream->trace.end_frame || event->client_id == 0U ||
		event->spawn_generation == 0U)
		return 0;
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
	if (sg_human_trace_learning_host_test_event_visits == UINT64_MAX)
		return 0;
	sg_human_trace_learning_host_test_event_visits++;
#endif
	if (!stream->selected)
		return 1;
	if (event->client_id > MAX_CLIENTS)
	{
		stream->selected = 0U;
		return 1;
	}
	scope = stream->current[event->client_id - 1U];
	if (!scope || scope->acceptance != acceptance)
	{
		if (scope && event->spawn_generation <=
			scope->capture.scope.spawn_generation)
		{
			stream->selected = 0U;
			return 1;
		}
		return LearningHostAppendStreamScope(stream, acceptance, event);
	}
	return LearningHostCaptureEvent(&scope->capture, event);
}

static void LearningHostDiscardStream(learning_host_stream_context_t *stream)
{
	size_t index;

	if (!stream)
		return;
	for (index = 0U; index < stream->scope_count; index++)
		LearningHostStreamDiscardScope(stream->scopes[index]);
	free(stream->scopes);
	stream->scopes = NULL;
	stream->scope_count = 0U;
	stream->scope_capacity = 0U;
	memset(stream->current, 0, sizeof(stream->current));
}

static int LearningHostBeginAcceptedRoot(void *opaque,
	const sg_human_trace_v3_spool_ref_t *spool)
{
	learning_host_stream_context_t *stream = opaque;

	if (!stream || !spool || stream->scope_count != 0U || stream->scopes ||
		!LearningHostCompletionToTrace(&spool->completion, &stream->trace))
		return 0;
	stream->spool = spool;
	stream->selected =
		SG_HumanTraceAcceptedV3RootLearningCompatible(spool) &&
		LearningHostTraceCurrent(&stream->trace) &&
		(!stream->published || LearningHostPublishedMatchesTrace(
			stream->published, &stream->trace));
	return 1;
}

static int LearningHostFinishAcceptedRoot(void *opaque)
{
	learning_host_stream_context_t *stream = opaque;
	size_t index;

	if (!stream || !stream->spool)
		return 0;
	if (stream->selected)
		for (index = 0U; index < stream->scope_count; index++)
			if (!LearningHostFinalizeStreamScope(stream,
				stream->scopes[index]))
			{
				LearningHostDiscardStream(stream);
				return 0;
			}
	LearningHostDiscardStream(stream);
	stream->spool = NULL;
	stream->selected = 0U;
	return 1;
}

static int LearningHostVisitStored(
	const sg_human_trace_learning_host_published_runtime_t *published,
	const sg_level_identity_t *identity,
	sg_human_trace_learning_host_report_t *report)
{
	learning_host_stream_context_t stream;
	sg_human_trace_v3_collection_visitor_t visitor;
	int result;

	if (!identity)
		return 0;
	memset(&stream, 0, sizeof(stream));
	memset(&visitor, 0, sizeof(visitor));
	stream.published = published;
	stream.report = report;
	visitor.begin_root = LearningHostBeginAcceptedRoot;
	visitor.event = LearningHostStreamEvent;
	visitor.finish_root = LearningHostFinishAcceptedRoot;
	result = SG_HumanTraceVisitAcceptedV3Collection(identity, &visitor, &stream);
	LearningHostDiscardStream(&stream);
	return result;
}

static int LearningHostApplyStored(
	const sg_human_trace_learning_host_published_runtime_t *published,
	sg_human_trace_learning_host_report_t *report)
{
	if (!published)
		return 0;
	return LearningHostVisitStored(published, &published->level_identity, report);
}

static int LearningHostReportStoredPending(const sg_level_identity_t *identity,
	sg_human_trace_learning_host_report_t *report)
{
	return LearningHostVisitStored(NULL, identity, report);
}

int SG_HumanTraceLearningHostGamePublishRuntime(
	const sg_human_trace_learning_host_runtime_publication_t *publication,
	sg_human_trace_learning_host_report_t *report_out)
{
	sg_human_trace_learning_host_published_runtime_t published;
	sg_level_identity_t current;
	char directory[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];

	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!publication || !publication->locate_hook_kernel ||
		sg_human_trace_learning_host_runtime_published ||
		!LearningHostRuntimeCurrent(publication->runtime,
			publication->snapshot) ||
		!level.mapname[0] ||
		SG_LevelIdentitySnapshot(level.mapname, &current) != SG_IDENTITY_OK ||
		!LearningHostPublishedLevelMatches(&publication->level_identity, &current) ||
		!SG_HumanTraceAcceptedV3Directory(directory))
		return 0;
	memset(&published, 0, sizeof(published));
	published.runtime = publication->runtime;
	published.snapshot = publication->snapshot;
	published.level_identity = publication->level_identity;
	published.evidence_context = publication->evidence_context;
	published.locate_hook_kernel = publication->locate_hook_kernel;
	if (!SG_HumanTraceLearningStoreOpen(&sg_human_trace_learning_host_store,
		directory,
		&publication->level_identity, publication->runtime->parameters,
		publication->runtime->next_transaction_id) ||
		!SG_HumanTraceLearningStoreRestore(&sg_human_trace_learning_host_store,
			publication->runtime->parameters,
			&publication->runtime->next_transaction_id) ||
		!LearningHostRuntimeCurrent(publication->runtime,
			publication->snapshot))
	{
		SG_HumanTraceLearningStoreClose(&sg_human_trace_learning_host_store);
		return 0;
	}
	published.store = &sg_human_trace_learning_host_store;
	sg_human_trace_learning_host_published_runtime = published;
	sg_human_trace_learning_host_runtime_published = 1U;
	if (report_out)
		report_out->runtime_published = 1U;
	return LearningHostApplyStored(
		&sg_human_trace_learning_host_published_runtime, report_out);
}

void SG_HumanTraceLearningHostGameWithdrawRuntime(
	const sg_human_trace_learning_runtime_t *runtime,
	const sg_rune_runtime_snapshot_t *snapshot)
{
	if (sg_human_trace_learning_host_runtime_published != 1U ||
		!runtime || !snapshot ||
		sg_human_trace_learning_host_published_runtime.runtime != runtime ||
		sg_human_trace_learning_host_published_runtime.snapshot != snapshot)
		return;
	SG_HumanTraceLearningStoreClose(&sg_human_trace_learning_host_store);
	memset(&sg_human_trace_learning_host_published_runtime, 0,
		sizeof(sg_human_trace_learning_host_published_runtime));
	sg_human_trace_learning_host_runtime_published = 0U;
}

#ifdef SG_HUMAN_TRACE_LEARNING_TEST

int SG_HumanTraceLearningHostGameTestPublishRuntime(
	const sg_human_trace_learning_test_published_runtime_t *published,
	sg_human_trace_learning_host_report_t *report_out)
{
	return SG_HumanTraceLearningHostGamePublishRuntime(published, report_out);
}

void SG_HumanTraceLearningHostGameTestWithdrawRuntime(
	const sg_human_trace_learning_runtime_t *runtime,
	const sg_rune_runtime_snapshot_t *snapshot)
{
	SG_HumanTraceLearningHostGameWithdrawRuntime(runtime, snapshot);
}

#endif /* SG_HUMAN_TRACE_LEARNING_TEST */

void SG_HumanTraceLearningHostGamePostMatch(sg_human_trace_learning_host_report_t *report_out)
{
	sg_human_trace_completion_t completion;
	sg_human_trace_learning_trace_v3_auth_t trace;
	sg_human_trace_learning_host_published_runtime_t published;
	sg_level_identity_t trace_identity;
	uint64_t rejected_before = 0U;

	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!SG_HumanTraceCompleted(&completion) ||
		!LearningHostCompletionToTrace(&completion, &trace) ||
		!LearningHostTraceCurrent(&trace))
	{
		if (report_out)
			report_out->rejected_batches++;
		return;
	}
	if (report_out)
		report_out->trace_authenticated = 1U;
	memset(&trace_identity, 0, sizeof(trace_identity));
	memcpy(trace_identity.mapname, trace.mapname, sizeof(trace_identity.mapname));
	trace_identity.bsp_checksum = trace.bsp_checksum;
	trace_identity.entity_crc32 = trace.entity_crc32;
	trace_identity.host_physics_id = trace.host_physics_id;
	if (!LearningHostPublishedForTrace(&published, &trace))
	{
		if (!LearningHostReportStoredPending(&trace_identity, report_out) &&
			report_out)
			report_out->rejected_batches++;
		return;
	}
	if (report_out)
		report_out->runtime_published = 1U;
	if (report_out)
		rejected_before = report_out->rejected_batches;
	if (!LearningHostApplyStored(&published, report_out) && report_out &&
		report_out->rejected_batches == rejected_before)
		report_out->rejected_batches++;
}

void SG_HumanTraceLearningHostGameReset(void)
{
	LearningHostRevokeAcceptedV3Capability();
	SG_HumanTraceLearningStoreClose(&sg_human_trace_learning_host_store);
	memset(&sg_human_trace_learning_host_published_runtime, 0,
		sizeof(sg_human_trace_learning_host_published_runtime));
	sg_human_trace_learning_host_runtime_published = 0U;
}
