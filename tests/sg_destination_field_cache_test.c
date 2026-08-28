#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_destination_field_cache.h"

#define TEST_SOURCE_SET UINT64_C(0x4341434845544553)
#define TEST_PHASE_COUNT 4U
#define TEST_CELL_COUNT 3U
#define TEST_KERNEL_COUNT 4U

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct field_fixture_s
{
	sg_rune_phase_basis_t phases[TEST_PHASE_COUNT];
	sg_rune_cell_t cells[TEST_CELL_COUNT];
	sg_rune_capability_kernel_t kernels[TEST_KERNEL_COUNT];
	sg_phase_coordinate_t coordinates[TEST_PHASE_COUNT];
	sg_rune_model_t model;
	sg_rune_runtime_snapshot_t snapshot;
} field_fixture_t;

static sg_rune_order_key_t TestOrder(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_order_key_t){
		TEST_SOURCE_SET, domain, 1U, ordinal, 0U
	};
}

static sg_rune_stable_id_t TestId(uint32_t domain, uint32_t ordinal)
{
	sg_rune_order_key_t order = TestOrder(domain, ordinal);

	return SG_RuneModelStableIdFromOrderKey(&order);
}

static sg_rune_cell_id_t TestCellId(uint32_t ordinal)
{
	return (sg_rune_cell_id_t){ TestId(SG_RUNE_ORDER_CELL, ordinal) };
}

static sg_rune_phase_id_t TestPhaseId(uint32_t ordinal)
{
	return (sg_rune_phase_id_t){ TestId(SG_RUNE_ORDER_PHASE, ordinal) };
}

static sg_rune_kernel_id_t TestKernelId(uint32_t ordinal)
{
	return (sg_rune_kernel_id_t){ TestId(SG_RUNE_ORDER_KERNEL, ordinal) };
}

static sg_rune_interval_t TestInterval(float minimum, float maximum)
{
	return (sg_rune_interval_t){ minimum, maximum };
}

static void SetTestKernel(field_fixture_t *fixture, uint32_t index,
	uint32_t source, uint32_t destination, float duration)
{
	sg_rune_capability_kernel_t *kernel = &fixture->kernels[index];
	uint32_t source_cell = source < 2U ? 0U : source - 1U;
	uint32_t destination_cell = destination < 2U ? 0U : destination - 1U;

	memset(kernel, 0, sizeof(*kernel));
	kernel->id = TestKernelId(index);
	kernel->order = TestOrder(SG_RUNE_ORDER_KERNEL, index);
	kernel->source_cell = TestCellId(source_cell);
	kernel->destination_cell = TestCellId(destination_cell);
	kernel->boundary = SG_RUNE_PORTAL_REF_NONE;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->mechanism = SG_RUNE_MECHANISM_REF_NONE;
	kernel->source_phase = fixture->phases[source].id;
	kernel->destination_phase = fixture->phases[destination].id;
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	kernel->family = SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT;
	kernel->cost_law = SG_RUNE_COST_CONSTANT_RATE;
	kernel->parameters.displacement.x = TestInterval(1.0f, 1.0f);
	kernel->parameters.displacement.y = TestInterval(0.0f, 0.0f);
	kernel->parameters.displacement.z = TestInterval(0.0f, 0.0f);
	kernel->parameters.duration_ms = TestInterval(duration, duration);
	kernel->parameters.speed = TestInterval(0.0f, 320.0f);
	kernel->parameters.acceleration = TestInterval(0.0f, 10.0f);
	kernel->parameters.vertical_acceleration = TestInterval(0.0f, 10.0f);
	kernel->parameters.physics_abi_id = UINT64_C(0x303);
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL |
		SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN;
}

static void InitFixture(field_fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	for (index = 0U; index < TEST_PHASE_COUNT; index++) {
		fixture->phases[index].id = TestPhaseId(index);
		fixture->phases[index].order = TestOrder(SG_RUNE_ORDER_PHASE, index);
		fixture->phases[index].velocity.x = TestInterval(-320.0f, 320.0f);
		fixture->phases[index].velocity.y = TestInterval(-320.0f, 320.0f);
		fixture->phases[index].velocity.z = TestInterval(0.0f, 0.0f);
		fixture->coordinates[index] = (sg_phase_coordinate_t){
			index, index < 2U ? 0U : index - 1U
		};
	}
	for (index = 0U; index < TEST_CELL_COUNT; index++) {
		fixture->cells[index].id = TestCellId(index);
		fixture->cells[index].order = TestOrder(SG_RUNE_ORDER_CELL, index);
		fixture->cells[index].phases.first = index == 0U ? 0U : index + 1U;
		fixture->cells[index].phases.count = index == 0U ? 2U : 1U;
	}
	SetTestKernel(fixture, 0U, 0U, 1U, 10.0f);
	SetTestKernel(fixture, 1U, 1U, 0U, 12.0f);
	SetTestKernel(fixture, 2U, 2U, 3U, 20.0f);
	SetTestKernel(fixture, 3U, 3U, 2U, 21.0f);
	fixture->model.version = SG_RUNE_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->model.flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->model.identity.bsp_content_id = UINT64_C(0x101);
	fixture->model.identity.entity_semantics_id = UINT64_C(0x202);
	fixture->model.identity.physics_abi_id = UINT64_C(0x303);
	fixture->model.identity.source_set_identity = TEST_SOURCE_SET;
	fixture->model.identity.schema_id = UINT64_C(0x404);
	fixture->model.identity.producer_identity = UINT64_C(0x505);
	fixture->model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->model.completeness.reason = SG_RUNE_FAILURE_NONE;
	fixture->model.completeness.expected_cells = TEST_CELL_COUNT;
	fixture->model.completeness.covered_cells = TEST_CELL_COUNT;
	fixture->model.phases = fixture->phases;
	fixture->model.phase_count = TEST_PHASE_COUNT;
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = TEST_CELL_COUNT;
	fixture->model.kernels = fixture->kernels;
	fixture->model.kernel_count = TEST_KERNEL_COUNT;
	fixture->snapshot = (sg_rune_runtime_snapshot_t){
		.identity = UINT64_C(0x909),
		.topology_revision = UINT64_C(7),
		.cell_count = TEST_CELL_COUNT,
		.phase_count = TEST_PHASE_COUNT,
		.region_count = TEST_CELL_COUNT,
		.model = &fixture->model,
		.phases = fixture->coordinates
	};
}

static sg_destination_handle_t Destination(uint64_t generation,
	uint32_t phase_id, uint32_t cell_id)
{
	sg_destination_handle_t destination;

	memset(&destination, 0, sizeof(destination));
	destination.id = UINT64_C(0x44);
	destination.generation = generation;
	destination.kind = SG_DESTINATION_WAYPOINT;
	destination.motion = SG_DESTINATION_STATIC;
	destination.valid = 1U;
	destination.pose.phase = (sg_phase_coordinate_t){ phase_id, cell_id };
	destination.pose.position[0] = 16.0f;
	destination.pose.position[1] = 16.0f;
	destination.pose.position[2] = 16.0f;
	destination.pose.region_id = cell_id;
	return destination;
}

typedef struct cache_fixture_s
{
	field_fixture_t field;
	uint32_t leaf[TEST_CELL_COUNT];
	uint32_t coarse[TEST_CELL_COUNT];
	uint32_t phase_leaf[TEST_PHASE_COUNT];
	sg_field_region_level_t levels[2];
	sg_field_region_hierarchy_t hierarchy;
} cache_fixture_t;

static void InitCacheFixture(cache_fixture_t *fixture)
{
	InitFixture(&fixture->field);
	fixture->leaf[0] = 0U;
	fixture->leaf[1] = 1U;
	fixture->leaf[2] = 2U;
	fixture->coarse[0] = 0U;
	fixture->coarse[1] = 0U;
	fixture->coarse[2] = 1U;
	fixture->phase_leaf[0] = 0U;
	fixture->phase_leaf[1] = 0U;
	fixture->phase_leaf[2] = 1U;
	fixture->phase_leaf[3] = 2U;
	fixture->levels[0] = (sg_field_region_level_t){
		.region_count = TEST_CELL_COUNT,
		.leaf_to_region = fixture->leaf
	};
	fixture->levels[1] = (sg_field_region_level_t){
		.region_count = 2U,
		.leaf_to_region = fixture->coarse
	};
	fixture->hierarchy = (sg_field_region_hierarchy_t){
		.leaf_region_count = TEST_CELL_COUNT,
		.level_count = 2U,
		.levels = fixture->levels,
		.phase_to_leaf_region = fixture->phase_leaf
	};
}

static int SamplesEqual(const sg_destination_field_t *field,
	const sg_field_sample_t clean[TEST_PHASE_COUNT])
{
	return field->sample_count == TEST_PHASE_COUNT &&
		memcmp(field->samples, clean,
			TEST_PHASE_COUNT * sizeof(clean[0])) == 0;
}

static void TestSamePhaseReuseProof(void)
{
	cache_fixture_t fixture;
	field_fixture_t original;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	sg_destination_field_t clean_field;
	sg_field_sample_t clean[TEST_PHASE_COUNT];
	sg_field_cache_result_t first;
	sg_field_cache_result_t updated;
	sg_field_query_result_t query;
	const sg_destination_field_t *cached;
	sg_destination_pose_t source;

	InitCacheFixture(&fixture);
	original = fixture.field;
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 3U, &cache));
	before = Destination(1U, 2U, 1U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&before, 100U, &first));
	after = before;
	after.generation = 2U;
	after.pose.position[0] += 7.0f;
	after.pose.velocity[1] = 25.0f;
	CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&before, &after, 200U, &updated));
	CHECK(updated.disposition == SG_FIELD_CACHE_INCREMENTAL_REUSE);
	CHECK(updated.scope == SG_FIELD_CACHE_SCOPE_LOCAL);
	CHECK(updated.affected_region_count == 2U);
	CHECK(!SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		first.ref, &cached));
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		updated.ref, &cached));
	CHECK(SG_DestinationFieldSolve(&fixture.field.snapshot, &after, 200U,
		clean, TEST_PHASE_COUNT, &clean_field));
	CHECK(SamplesEqual(cached, clean));
	CHECK(cached->computed_at_ms == clean_field.computed_at_ms);
	CHECK(memcmp(&cached->destination, &clean_field.destination,
		sizeof(cached->destination)) == 0);
	source = after.pose;
	source.sample_time_ms = 200U;
	CHECK(SG_DestinationFieldCacheQuery(cache, &fixture.field.snapshot,
		updated.ref, &source, &query));
	CHECK(query.terminal_residual.status == SG_FIELD_TERMINAL_RESIDUAL_EXACT);
	/* Cache activity cannot write into any RUNE-owned record. */
	CHECK(memcmp(&fixture.field.model, &original.model,
		sizeof(fixture.field.model)) == 0);
	CHECK(memcmp(fixture.field.kernels, original.kernels,
		sizeof(fixture.field.kernels)) == 0);
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestStaticHitAndKeySemantics(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t destination;
	sg_destination_handle_t distinct;
	sg_field_cache_result_t first;
	sg_field_cache_result_t hit;
	sg_field_cache_result_t miss;
	sg_destination_field_cache_stats_t stats;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 4U, &cache));
	destination = Destination(1U, 1U, 0U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&destination, 100U, &first));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&destination, UINT64_MAX, &hit));
	CHECK(hit.disposition == SG_FIELD_CACHE_HIT);
	CHECK(hit.ref.slot == first.ref.slot && hit.ref.serial == first.ref.serial);
	distinct = destination;
	distinct.pose.position[2] += 1.0f;
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&distinct, 100U, &miss));
	CHECK(miss.disposition == SG_FIELD_CACHE_MISS_SOLVED);
	CHECK(miss.ref.serial != first.ref.serial);
	SG_DestinationFieldCacheStats(cache, &stats);
	CHECK(stats.clean_solves == 2U);
	CHECK(stats.static_hits == 1U);
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestMovingTimeAndCleanPhaseChange(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	sg_destination_handle_t stale;
	sg_destination_handle_t changed_phase;
	sg_field_cache_result_t result;
	sg_field_query_result_t query;
	sg_field_sample_t clean[TEST_PHASE_COUNT];
	sg_destination_field_t clean_field;
	const sg_destination_field_t *field;
	sg_destination_pose_t source;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 3U, &cache));
	before = Destination(1U, 1U, 0U);
	before.motion = SG_DESTINATION_MOVING;
	before.pose.sample_time_ms = 100U;
	before.pose.velocity[0] = 32.0f;
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&before, 100U, &result));
	after = before;
	after.generation = 2U;
	after.pose.sample_time_ms = 200U;
	after.pose.position[0] += 3.0f;
	CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&before, &after, 300U, &result));
	CHECK(result.disposition == SG_FIELD_CACHE_INCREMENTAL_REUSE);
	source = after.pose;
	CHECK(SG_DestinationFieldCacheQuery(cache, &fixture.field.snapshot,
		result.ref, &source, &query));
	CHECK(query.terminal_residual.status == SG_FIELD_TERMINAL_RESIDUAL_UNKNOWN);
	CHECK(query.terminal_residual.upper_ms == SG_DESTINATION_FIELD_INF);
	stale = after;
	stale.generation = 3U;
	CHECK(!SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&after, &stale, 300U, &result));
	changed_phase = after;
	changed_phase.generation = 4U;
	changed_phase.pose.sample_time_ms = 400U;
	changed_phase.pose.phase = fixture.field.coordinates[2];
	changed_phase.pose.region_id = 1U;
	CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&after, &changed_phase, 400U, &result));
	CHECK(result.disposition == SG_FIELD_CACHE_CLEAN_REBUILD);
	CHECK(result.scope == SG_FIELD_CACHE_SCOPE_ALL);
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		result.ref, &field));
	CHECK(SG_DestinationFieldSolve(&fixture.field.snapshot, &changed_phase,
		400U, clean, TEST_PHASE_COUNT, &clean_field));
	CHECK(SamplesEqual(field, clean));
	source = changed_phase.pose;
	CHECK(SG_DestinationFieldCacheQuery(cache, &fixture.field.snapshot,
		result.ref, &source, &query));
	CHECK(query.terminal_residual.status == SG_FIELD_TERMINAL_RESIDUAL_EXACT);
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestCrossPhaseRegionalClosure(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	sg_field_cache_result_t result;
	sg_field_sample_t clean[TEST_PHASE_COUNT];
	sg_destination_field_t clean_field;
	const sg_destination_field_t *field;
	uint8_t closure[TEST_PHASE_COUNT];
	sg_destination_field_cache_stats_t stats;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 2U, &cache));
	before = Destination(1U, 0U, 0U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&before, 10U, &result));
	after = before;
	after.generation = 2U;
	after.pose.phase = fixture.field.coordinates[1];
	CHECK(SG_DestinationFieldDependencyClosure(&fixture.field.snapshot,
		&before.pose.phase, &after.pose.phase, closure, TEST_PHASE_COUNT));
	CHECK(closure[0] == 1U && closure[1] == 1U && closure[2] == 0U &&
		closure[3] == 0U);
	CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&before, &after, 20U, &result));
	CHECK(result.disposition == SG_FIELD_CACHE_INCREMENTAL_REUSE);
	CHECK(result.scope == SG_FIELD_CACHE_SCOPE_LOCAL);
	CHECK(result.affected_region_count == 2U);
	CHECK(result.affected_regions[0].level == 0U);
	CHECK(result.affected_regions[0].region == 0U);
	CHECK(result.affected_regions[1].level == 1U);
	CHECK(result.affected_regions[1].region == 0U);
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		result.ref, &field));
	CHECK(SG_DestinationFieldSolve(&fixture.field.snapshot, &after, 20U,
		clean, TEST_PHASE_COUNT, &clean_field));
	CHECK(SamplesEqual(field, clean));
	SG_DestinationFieldCacheStats(cache, &stats);
	CHECK(stats.clean_solves == 1U);
	CHECK(stats.regional_updates == 1U);
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestEvictionInvalidationAndIdentity(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t a;
	sg_destination_handle_t b;
	sg_destination_handle_t c;
	sg_field_cache_result_t ra;
	sg_field_cache_result_t rb;
	sg_field_cache_result_t rc;
	const sg_destination_field_t *field;
	sg_destination_field_cache_stats_t stats;
	uint64_t saved_bsp;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 2U, &cache));
	a = Destination(1U, 0U, 0U);
	b = Destination(1U, 1U, 0U);
	b.id = 0x45U;
	c = Destination(1U, 2U, 1U);
	c.id = 0x46U;
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&a, 10U, &ra));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&b, 10U, &rb));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&a, 20U, &ra));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&c, 10U, &rc));
	CHECK(!SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		rb.ref, &field));
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		ra.ref, &field));
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		rc.ref, &field));
	SG_DestinationFieldCacheStats(cache, &stats);
	CHECK(stats.evictions == 1U);
	saved_bsp = fixture.field.model.identity.bsp_content_id;
	fixture.field.model.identity.bsp_content_id++;
	CHECK(!SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		ra.ref, &field));
	fixture.field.model.identity.bsp_content_id = saved_bsp;
	SG_DestinationFieldCacheInvalidate(cache);
	CHECK(!SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		ra.ref, &field));
	SG_DestinationFieldCacheInvalidate(cache);
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestCachedAfterUpdateReporting(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	sg_field_cache_result_t before_result;
	sg_field_cache_result_t after_result;
	sg_field_cache_result_t update_result;
	const sg_destination_field_t *field;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 3U, &cache));
	before = Destination(1U, 0U, 0U);
	after = Destination(2U, 1U, 0U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&before, 10U, &before_result));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&after, 20U, &after_result));
	CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&before, &after, 30U, &update_result));
	CHECK(update_result.disposition == SG_FIELD_CACHE_HIT);
	CHECK(update_result.scope == SG_FIELD_CACHE_SCOPE_LOCAL);
	CHECK(update_result.affected_region_count == 2U);
	CHECK(!SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		before_result.ref, &field));
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		after_result.ref, &field));
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestRejectedUpdateIsTransactional(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t a;
	sg_destination_handle_t b;
	sg_destination_handle_t failed_after;
	sg_destination_handle_t c;
	sg_field_cache_result_t ra;
	sg_field_cache_result_t rb;
	sg_field_cache_result_t result;
	sg_field_cache_result_t zero_result;
	sg_destination_field_cache_stats_t before_stats;
	sg_destination_field_cache_stats_t after_stats;
	const sg_destination_field_t *field;
	float saved_duration;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 2U, &cache));
	a = Destination(1U, 0U, 0U);
	b = Destination(1U, 2U, 1U);
	b.id = 0x45U;
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&a, 10U, &ra));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&b, 10U, &rb));
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&a, 20U, &ra));
	SG_DestinationFieldCacheStats(cache, &before_stats);
	failed_after = a;
	failed_after.generation = 2U;
	failed_after.pose.phase = fixture.field.coordinates[1];
	saved_duration = fixture.field.kernels[0].parameters.duration_ms.max_value;
	fixture.field.kernels[0].parameters.duration_ms.max_value = 0.0f;
	memset(&result, 0xa5, sizeof(result));
	memset(&zero_result, 0, sizeof(zero_result));
	CHECK(!SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
		&a, &failed_after, 30U, &result));
	CHECK(memcmp(&result, &zero_result, sizeof(result)) == 0);
	fixture.field.kernels[0].parameters.duration_ms.max_value = saved_duration;
	SG_DestinationFieldCacheStats(cache, &after_stats);
	CHECK(memcmp(&before_stats, &after_stats, sizeof(before_stats)) == 0);
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		ra.ref, &field));
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		rb.ref, &field));
	c = Destination(1U, 3U, 2U);
	c.id = 0x46U;
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&c, 40U, &result));
	CHECK(result.ref.serial == rb.ref.serial + 1U);
	CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		ra.ref, &field));
	CHECK(!SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
		rb.ref, &field));
	SG_DestinationFieldCacheDestroy(cache);
}

static uint32_t RandomState(uint32_t *state)
{
	*state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
	return *state;
}

static void TestRandomIncrementalCleanEquivalence(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	sg_field_cache_result_t result;
	sg_field_sample_t clean[TEST_PHASE_COUNT];
	sg_destination_field_t clean_field;
	const sg_destination_field_t *field;
	uint32_t random = UINT32_C(0x8147253);
	uint32_t iteration;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 4U, &cache));
	/* Prove the cache copied the hierarchy rather than retaining mutable input. */
	fixture.coarse[0] = 1U;
	fixture.coarse[1] = 1U;
	before = Destination(1U, 0U, 0U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&before, 1U, &result));
	for (iteration = 0U; iteration < 512U; iteration++) {
		uint32_t value = RandomState(&random);

		after = before;
		after.generation++;
		after.pose.position[0] = (float)(value & 255U);
		after.pose.position[1] = (float)((value >> 8) & 255U);
		after.pose.velocity[0] = (float)((int32_t)(value & 63U) - 31);
		CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
			&before, &after, (uint64_t)iteration + 2U, &result));
		CHECK(result.disposition == SG_FIELD_CACHE_INCREMENTAL_REUSE);
		CHECK(result.scope == SG_FIELD_CACHE_SCOPE_LOCAL);
		CHECK(result.affected_region_count == 2U);
		CHECK(result.affected_regions[1].level == 1U);
		CHECK(result.affected_regions[1].region == 0U);
		CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
			result.ref, &field));
		CHECK(SG_DestinationFieldSolve(&fixture.field.snapshot, &after,
			(uint64_t)iteration + 2U, clean, TEST_PHASE_COUNT, &clean_field));
		CHECK(SamplesEqual(field, clean));
		before = after;
	}
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestRandomCrossPhaseEquivalence(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_destination_handle_t before;
	sg_destination_handle_t after;
	sg_field_cache_result_t result;
	sg_field_sample_t clean[TEST_PHASE_COUNT];
	sg_destination_field_t clean_field;
	const sg_destination_field_t *field;
	uint32_t random = UINT32_C(0x71a4c953);
	uint32_t iteration;

	InitCacheFixture(&fixture);
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 3U, &cache));
	before = Destination(1U, 0U, 0U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&before, 1U, &result));
	for (iteration = 0U; iteration < 512U; iteration++) {
		uint32_t value = RandomState(&random);
		uint32_t phase = value % TEST_PHASE_COUNT;

		after = before;
		after.generation++;
		after.pose.phase = fixture.field.coordinates[phase];
		after.pose.region_id = fixture.phase_leaf[phase];
		after.pose.position[0] = (float)((value >> 8) & 255U);
		after.pose.velocity[1] = (float)((int32_t)((value >> 16) & 63U) - 31);
		CHECK(SG_DestinationFieldCacheUpdate(cache, &fixture.field.snapshot,
			&before, &after, (uint64_t)iteration + 2U, &result));
		CHECK(SG_DestinationFieldCacheField(cache, &fixture.field.snapshot,
			result.ref, &field));
		CHECK(SG_DestinationFieldSolve(&fixture.field.snapshot, &after,
			(uint64_t)iteration + 2U, clean, TEST_PHASE_COUNT, &clean_field));
		CHECK(SamplesEqual(field, clean));
		before = after;
	}
	SG_DestinationFieldCacheDestroy(cache);
}

static void TestInvalidBoundaries(void)
{
	cache_fixture_t fixture;
	sg_destination_field_cache_t *cache = NULL;
	sg_field_region_hierarchy_t bad;
	sg_destination_handle_t destination;
	sg_field_cache_result_t result;

	InitCacheFixture(&fixture);
	bad = fixture.hierarchy;
	bad.level_count = SG_DESTINATION_FIELD_MAX_REGION_LEVEL + 2U;
	CHECK(!SG_DestinationFieldCacheCreate(&fixture.field.snapshot, &bad, 2U,
		&cache));
	CHECK(!SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 0U, &cache));
	CHECK(SG_DestinationFieldCacheCreate(&fixture.field.snapshot,
		&fixture.hierarchy, 2U, &cache));
	destination = Destination(1U, 0U, 0U);
	destination.pose.region_id = TEST_CELL_COUNT;
	CHECK(!SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&destination, 1U, &result));
	destination = Destination(1U, 2U, 1U);
	destination.pose.region_id = 2U;
	CHECK(!SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&destination, 1U, &result));
	destination = Destination(1U, 0U, 0U);
	destination.pose.position[0] = NAN;
	CHECK(!SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&destination, 1U, &result));
	destination = Destination(UINT64_MAX, 0U, 0U);
	CHECK(SG_DestinationFieldCacheResolve(cache, &fixture.field.snapshot,
		&destination, UINT64_MAX, &result));
	SG_DestinationFieldCacheDestroy(cache);
}

int main(void)
{
	TestSamePhaseReuseProof();
	TestStaticHitAndKeySemantics();
	TestMovingTimeAndCleanPhaseChange();
	TestCrossPhaseRegionalClosure();
	TestEvictionInvalidationAndIdentity();
	TestCachedAfterUpdateReporting();
	TestRejectedUpdateIsTransactional();
	TestRandomIncrementalCleanEquivalence();
	TestRandomCrossPhaseEquivalence();
	TestInvalidBoundaries();
	if (failures != 0) {
		fprintf(stderr, "%d destination-field-cache checks failed\n", failures);
		return 1;
	}
	puts("destination field cache tests passed");
	return 0;
}
