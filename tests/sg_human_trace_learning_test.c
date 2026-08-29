#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SG_HUMAN_TRACE_LEARNING_TEST
#error "sg_human_trace_learning_test requires explicit test support"
#endif

#include "slipgate/sg_human_trace_learning_consumer.h"
#include "slipgate/sg_human_trace_learning_game_test.h"

#define TEST_KERNEL_COUNT 129U
#define TEST_SOURCE_SET UINT64_C(0x4c4541524e494e47)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct learning_fixture_s
{
	sg_rune_cell_t cells[1];
	sg_rune_phase_basis_t phases[1];
	sg_rune_capability_kernel_t kernels[TEST_KERNEL_COUNT];
	sg_phase_coordinate_t coordinates[1];
	sg_rune_model_t model;
	sg_rune_runtime_snapshot_t snapshot;
	sg_human_trace_learning_kernel_key_t keys[TEST_KERNEL_COUNT];
	uint64_t costs[TEST_KERNEL_COUNT];
	uint64_t workspace_costs[TEST_KERNEL_COUNT];
	sg_human_trace_learning_parameters_t parameters;
	sg_human_trace_learning_workspace_t workspace;
} learning_fixture_t;

static sg_rune_stable_id_t StableId(uint32_t domain, uint32_t ordinal)
{
	sg_rune_order_key_t order = {
		TEST_SOURCE_SET, domain, 7U, ordinal, 0U
	};

	return SG_RuneModelStableIdFromOrderKey(&order);
}

static void InitFixture(learning_fixture_t *fixture)
{
	sg_human_trace_learning_domain_t domain;
	sg_human_trace_learning_storage_t storage;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	for (index = 0U; index < TEST_KERNEL_COUNT; index++)
	{
		fixture->kernels[index].id.value = StableId(SG_RUNE_ORDER_KERNEL, index);
		fixture->keys[index].kernel = fixture->kernels[index].id;
		fixture->keys[index].control.value = StableId(
			SG_RUNE_ORDER_CONTROL_FIBER, index);
	}
	fixture->model = (sg_rune_model_t){
		.version = SG_RUNE_MODEL_VERSION,
		.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG,
		.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
			SG_RUNE_MODEL_NO_RUNTIME_ACTORS,
		.identity = {
			.bsp_content_id = UINT64_C(101),
			.physics_abi_id = UINT64_C(202)
		},
		.completeness = { .state = SG_RUNE_COMPLETENESS_COMPLETE },
		.cells = fixture->cells,
		.cell_count = 1U,
		.phases = fixture->phases,
		.phase_count = 1U,
		.kernels = fixture->kernels,
		.kernel_count = TEST_KERNEL_COUNT
	};
	fixture->coordinates[0] = (sg_phase_coordinate_t){ 0U, 0U };
	fixture->snapshot = (sg_rune_runtime_snapshot_t){
		.identity = UINT64_C(303),
		.topology_revision = UINT64_C(404),
		.cell_count = 1U,
		.phase_count = 1U,
		.region_count = 1U,
		.model = &fixture->model,
		.phases = fixture->coordinates
	};
	domain = (sg_human_trace_learning_domain_t){
		.identity = {
			.rune_identity = fixture->snapshot.identity,
			.topology_revision = fixture->snapshot.topology_revision,
			.bsp_identity = fixture->model.identity.bsp_content_id,
			.physics_identity = fixture->model.identity.physics_abi_id
		},
		.snapshot = &fixture->snapshot,
		.kernel_keys = fixture->keys,
		.kernel_key_count = TEST_KERNEL_COUNT
	};
	storage = (sg_human_trace_learning_storage_t){
		.effective_cost_us = fixture->costs,
		.effective_cost_capacity = TEST_KERNEL_COUNT
	};
	fixture->workspace = (sg_human_trace_learning_workspace_t){
		.effective_cost_us = fixture->workspace_costs,
		.effective_cost_capacity = TEST_KERNEL_COUNT
	};
	CHECK(SG_RuneRuntimeSnapshotValid(&fixture->snapshot));
	CHECK(SG_HumanTraceLearningParametersInit(&fixture->parameters, &domain, &storage));
	CHECK(SG_HumanTraceLearningWorkspaceValid(&fixture->parameters, &fixture->workspace));
}

static sg_human_trace_learning_trace_v3_auth_t Trace(void)
{
	sg_human_trace_learning_trace_v3_auth_t trace;
	uint32_t index;

	memset(&trace, 0, sizeof(trace));
	for (index = 0U; index < SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES; index++)
		trace.terminal_sha256.bytes[index] = (uint8_t)(index + 1U);
	trace.session = 1U;
	trace.segment = 2U;
	strcpy(trace.mapname, "tracehook");
	trace.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	trace.pmove_substep_ms = SG_HUMAN_TRACE_LEARNING_PMOVE_SUBSTEP_MS;
	trace.server_frame_ms = SG_HUMAN_TRACE_LEARNING_SERVER_FRAME_MS;
	trace.module_revision = 1U;
	strcpy(trace.module_version, "learning-test");
	trace.end_order = 1000U;
	trace.end_frame = 100U;
	return trace;
}

static sg_human_trace_learning_record_t Record(const learning_fixture_t *fixture,
	const sg_human_trace_learning_trace_scope_t *scope, uint64_t evidence_id,
	uint64_t order)
{
	sg_human_trace_learning_record_t record;

	memset(&record, 0, sizeof(record));
	record.update.evidence = (sg_human_trace_learning_evidence_t){
		.evidence_id = evidence_id,
		.identity = fixture->parameters.domain.identity,
		.trace = scope->trace.terminal_sha256,
		.captured_at_ms = order
	};
	record.update.kind = SG_HUMAN_TRACE_LEARNING_UPDATE_COST;
	record.update.key = fixture->keys[TEST_KERNEL_COUNT - 1U];
	record.update.effective_cost_us = UINT64_C(500000);
	record.first_frame = (uint32_t)order;
	record.last_frame = (uint32_t)order;
	record.first_order = order;
	record.last_order = order;
	return record;
}

static void TestTypedControlKernelCost(void)
{
	learning_fixture_t fixture;
	sg_human_trace_learning_playthrough_t cursor[1];
	sg_human_trace_learning_runtime_t runtime;
	sg_human_trace_learning_trace_scope_t scope;
	sg_human_trace_learning_record_t record;
	sg_human_trace_learning_batch_t batch;
	uint64_t cost;

	InitFixture(&fixture);
	memset(cursor, 0, sizeof(cursor));
	scope = (sg_human_trace_learning_trace_scope_t){ Trace(), 1U, UINT64_C(11) };
	record = Record(&fixture, &scope, 1U, 10U);
	batch = (sg_human_trace_learning_batch_t){ scope, &record, 1U };

	CHECK(TEST_KERNEL_COUNT > 32U);
	CHECK(SG_HumanTraceLearningTestRuntimeInit(&runtime, &fixture.parameters,
		&fixture.workspace, cursor, 1U, 1U));
	CHECK(SG_HumanTraceLearningTestApplyBatch(&runtime, &batch, &scope.trace) ==
		SG_HUMAN_TRACE_LEARNING_APPLY_COMMITTED);
	CHECK(SG_HumanTraceLearningConsumerEffectiveKernelCost(
		&fixture.parameters, &record.update.key, UINT64_C(300000), &cost));
	CHECK(cost == UINT64_C(500000));
	CHECK(SG_HumanTraceLearningConsumerEffectiveKernelCost(
		NULL, &record.update.key, UINT64_C(300000), &cost));
	CHECK(cost == UINT64_C(300000));
	CHECK(cursor[0].used == 1U);
}

static void TestWholeBatchRejectionIsAtomic(void)
{
	learning_fixture_t fixture;
	sg_human_trace_learning_playthrough_t cursor[1];
	sg_human_trace_learning_runtime_t runtime;
	sg_human_trace_learning_trace_scope_t scope;
	sg_human_trace_learning_record_t records[2];
	sg_human_trace_learning_batch_t batch;
	uint64_t generation;
	uint64_t transaction;

	InitFixture(&fixture);
	memset(cursor, 0, sizeof(cursor));
	scope = (sg_human_trace_learning_trace_scope_t){ Trace(), 1U, UINT64_C(11) };
	records[0] = Record(&fixture, &scope, 1U, 10U);
	records[1] = Record(&fixture, &scope, 2U, 11U);
	records[1].update.key.kernel = SG_RUNE_KERNEL_REF_NONE;
	batch = (sg_human_trace_learning_batch_t){ scope, records, 2U };
	generation = fixture.parameters.generation;
	CHECK(SG_HumanTraceLearningTestRuntimeInit(&runtime, &fixture.parameters,
		&fixture.workspace, cursor, 1U, 99U));
	transaction = runtime.next_transaction_id;
	CHECK(SG_HumanTraceLearningTestApplyBatch(&runtime, &batch, &scope.trace) ==
		SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED);
	CHECK(fixture.parameters.generation == generation);
	CHECK(fixture.costs[TEST_KERNEL_COUNT - 1U] == 0U);
	CHECK(cursor[0].used == 0U);
	CHECK(runtime.next_transaction_id == transaction);
}

static void TestForgedRangeAndTraceAreRejected(void)
{
	learning_fixture_t fixture;
	sg_human_trace_learning_playthrough_t cursor[1];
	sg_human_trace_learning_runtime_t runtime;
	sg_human_trace_learning_trace_scope_t scope;
	sg_human_trace_learning_record_t record;
	sg_human_trace_learning_batch_t batch;
	uint64_t generation;

	InitFixture(&fixture);
	memset(cursor, 0, sizeof(cursor));
	scope = (sg_human_trace_learning_trace_scope_t){ Trace(), 1U, UINT64_C(11) };
	record = Record(&fixture, &scope, 1U, 10U);
	record.last_order = scope.trace.end_order;
	batch = (sg_human_trace_learning_batch_t){ scope, &record, 1U };
	generation = fixture.parameters.generation;
	CHECK(SG_HumanTraceLearningTestRuntimeInit(&runtime, &fixture.parameters,
		&fixture.workspace, cursor, 1U, 1U));
	CHECK(SG_HumanTraceLearningTestApplyBatch(&runtime, &batch, &scope.trace) ==
		SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED);
	CHECK(fixture.parameters.generation == generation);
	CHECK(cursor[0].used == 0U);
	record = Record(&fixture, &scope, 2U, 11U);
	record.update.evidence.trace.bytes[0] ^= UINT8_C(1);
	batch = (sg_human_trace_learning_batch_t){ scope, &record, 1U };
	CHECK(SG_HumanTraceLearningTestApplyBatch(&runtime, &batch, &scope.trace) ==
		SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED);
	CHECK(fixture.parameters.generation == generation);
	CHECK(cursor[0].used == 0U);
}

static void TestCanonicalCostBoundsAndTypedKey(void)
{
	learning_fixture_t fixture;
	sg_human_trace_learning_kernel_key_t invalid;
	uint64_t cost;

	InitFixture(&fixture);
	invalid = fixture.keys[0];
	invalid.control.value = SG_RUNE_STABLE_ID_NONE;
	CHECK(!SG_HumanTraceLearningKernelKeyValid(&invalid));
	CHECK(!SG_HumanTraceLearningEffectiveCostValid(SG_RUNE_FIELD_COST_INFINITE));
	CHECK(!SG_HumanTraceLearningConsumerEffectiveKernelCost(NULL,
		&fixture.keys[0], SG_RUNE_FIELD_COST_INFINITE, &cost));
	CHECK(!SG_HumanTraceLearningUpdateTouchesGeometry(NULL));
}

int main(void)
{
	TestTypedControlKernelCost();
	TestWholeBatchRejectionIsAtomic();
	TestForgedRangeAndTraceAreRejected();
	TestCanonicalCostBoundsAndTypedKey();
	return failures == 0 ? 0 : 1;
}
