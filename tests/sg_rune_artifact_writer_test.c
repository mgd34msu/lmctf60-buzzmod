/* Adversarial tests for the allocation-free RUNE artifact streaming writer. */
#include "q_shared.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_action.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_artifact_writer.h"

#define TEST_SEEDS 2U
#define TEST_LINKS 2U
#define TEST_NODES 2U
#define TEST_INVENTORY_EDGES 1U
#define TEST_PLAN_EDGES 1U
#define TEST_EDGES 2U
#define TEST_PLANS 1U
#define TEST_STRING_BYTES 35U
#define TEST_FILE_BYTES 571U
#define TEST_FRAGMENTS 11U

#define DUAL_BUTTON_NODES 3U
#define DUAL_BUTTON_EDGES 4U
#define DUAL_BUTTON_PLANS 2U
#define DUAL_BUTTON_LINKS 3U

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_U32(expected, actual) do { \
	uint32_t actual_ = (actual); \
	if (actual_ != (uint32_t)(expected)) { \
		fprintf(stderr, "%s:%d: expected 0x%08x, got 0x%08x: %s\n", \
		        __FILE__, __LINE__, (unsigned)(expected), \
		        (unsigned)actual_, #actual); \
		failures++; \
	} \
} while (0)

#define CHECK_RESULT(result_, diagnostic_, stage_, index_) do { \
	const sg_rune_artifact_write_result_t *checked_ = &(result_); \
	if (checked_->diagnostic != (sg_rune_codec_diagnostic_t)(diagnostic_) || \
	    checked_->stage != (stage_) || checked_->index != (index_)) { \
		fprintf(stderr, "%s:%d: got d=%d s=%d i=%u; " \
		        "expected d=%d s=%d i=%u\n", __FILE__, __LINE__, \
		        (int)checked_->diagnostic, (int)checked_->stage, \
		        checked_->index, (int)(diagnostic_), (int)(stage_), \
		        (unsigned int)(index_)); \
		failures++; \
	} \
} while (0)

typedef enum mutation_e
{
	MUTATE_NONE = 0,
	MUTATE_IDENTITY,
	MUTATE_SEED,
	MUTATE_LINK,
	MUTATE_NODE,
	MUTATE_EDGE,
	MUTATE_PLAN,
	MUTATE_STRING
} mutation_t;

typedef struct fixture_s
{
	sg_rune_codec_identity_t identity;
	sg_rune_codec_seed_t seeds[TEST_SEEDS];
	sg_rune_codec_link_t links[TEST_LINKS];
	sg_rune_codec_activation_node_t nodes[TEST_NODES];
	sg_rune_codec_activation_edge_t edges[TEST_EDGES];
	sg_rune_codec_activation_plan_t plans[TEST_PLANS];
	unsigned char strings[64];

	uint64_t graph_link_keys[TEST_LINKS];
	uint8_t graph_source_marks[TEST_SEEDS];
	uint32_t plan_references[TEST_PLANS];
	uint32_t node_references[TEST_NODES];
	uint32_t node_heads[TEST_NODES];
	uint32_t node_indegrees[TEST_NODES];
	uint32_t node_generations[TEST_NODES];
	uint32_t node_touched[TEST_NODES];
	uint32_t node_queue[TEST_NODES];
	uint32_t edge_next[TEST_EDGES];
	uint8_t string_marks[64];
	sg_rune_codec_workspace_t workspace;
} fixture_t;

typedef struct dual_button_fixture_s
{
	sg_rune_codec_identity_t identity;
	sg_rune_codec_seed_t seeds[TEST_SEEDS];
	sg_rune_codec_link_t links[DUAL_BUTTON_LINKS];
	sg_rune_codec_activation_node_t nodes[DUAL_BUTTON_NODES];
	sg_rune_codec_activation_edge_t edges[DUAL_BUTTON_EDGES];
	sg_rune_codec_activation_plan_t plans[DUAL_BUTTON_PLANS];
	unsigned char strings[64];
	uint64_t graph_link_keys[DUAL_BUTTON_LINKS];
	uint8_t graph_source_marks[TEST_SEEDS];
	uint32_t plan_references[DUAL_BUTTON_PLANS];
	uint32_t node_references[DUAL_BUTTON_NODES];
	uint32_t node_heads[DUAL_BUTTON_NODES];
	uint32_t node_indegrees[DUAL_BUTTON_NODES];
	uint32_t node_generations[DUAL_BUTTON_NODES];
	uint32_t node_touched[DUAL_BUTTON_NODES];
	uint32_t node_queue[DUAL_BUTTON_NODES];
	uint32_t edge_next[DUAL_BUTTON_EDGES];
	uint8_t string_marks[64];
	sg_rune_codec_workspace_t workspace;
} dual_button_fixture_t;

typedef struct memory_sink_s
{
	unsigned char *bytes;
	size_t capacity;
	size_t size;
	size_t calls;
	size_t fail_call;
	size_t mutate_call;
	mutation_t mutation;
	fixture_t *fixture;
	size_t fragment_sizes[TEST_FRAGMENTS + 2U];
} memory_sink_t;

static const unsigned char canonical_strings[TEST_STRING_BYTES] =
	"\0Door1\0door1\0func_button\0func_door";

static void SetVector(float vector[3], float x, float y, float z)
{
	vector[0] = x;
	vector[1] = y;
	vector[2] = z;
}

static void SetBounds(sg_rune_codec_activation_node_t *node,
	int16_t min_x, int16_t min_y, int16_t min_z,
	int16_t max_x, int16_t max_y, int16_t max_z)
{
	node->absmin_q8[0] = min_x;
	node->absmin_q8[1] = min_y;
	node->absmin_q8[2] = min_z;
	node->absmax_q8[0] = max_x;
	node->absmax_q8[1] = max_y;
	node->absmax_q8[2] = max_z;
}

static void FixtureWorkspace(fixture_t *fixture)
{
	sg_rune_codec_workspace_t *workspace = &fixture->workspace;

	memset(workspace, 0, sizeof(*workspace));
	workspace->graph_link_keys = fixture->graph_link_keys;
	workspace->graph_link_key_capacity = TEST_LINKS;
	workspace->graph_source_marks = fixture->graph_source_marks;
	workspace->graph_source_mark_capacity = TEST_SEEDS;
	workspace->plan_references = fixture->plan_references;
	workspace->plan_reference_capacity = TEST_PLANS;
	workspace->node_references = fixture->node_references;
	workspace->node_reference_capacity = TEST_NODES;
	workspace->node_heads = fixture->node_heads;
	workspace->node_head_capacity = TEST_NODES;
	workspace->node_indegrees = fixture->node_indegrees;
	workspace->node_indegree_capacity = TEST_NODES;
	workspace->node_generations = fixture->node_generations;
	workspace->node_generation_capacity = TEST_NODES;
	workspace->node_touched = fixture->node_touched;
	workspace->node_touched_capacity = TEST_NODES;
	workspace->node_queue = fixture->node_queue;
	workspace->node_queue_capacity = TEST_NODES;
	workspace->edge_next = fixture->edge_next;
	workspace->edge_next_capacity = TEST_EDGES;
	workspace->string_marks = fixture->string_marks;
	workspace->string_mark_capacity = sizeof(fixture->string_marks);
}

static void FixtureInit(fixture_t *fixture)
{
	uint32_t closure_crc = 0U;

	memset(fixture, 0, sizeof(*fixture));
	FixtureWorkspace(fixture);
	memcpy(fixture->identity.map_name, "active-test", 11U);
	fixture->identity.bsp_checksum = UINT32_C(0x12345678);
	fixture->identity.entity_crc32 = UINT32_C(0x9abcdef0);
	fixture->identity.gravity = 650.0f;
	fixture->identity.maxvelocity = 2000.0f;
	fixture->identity.pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	fixture->identity.server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	fixture->identity.host_physics_id = 1U;

	SetVector(fixture->seeds[0].origin, 0.0f, 0.0f, 0.0f);
	SetVector(fixture->seeds[1].origin, 128.0f, 0.0f, 0.0f);
	fixture->seeds[0].area_hint = 1;
	fixture->seeds[1].area_hint = 2;

	fixture->links[0].source = 0U;
	fixture->links[0].destination = 1U;
	fixture->links[0].action = RL_BUTTON_DOOR;
	fixture->links[0].provenance = RL_DECLARED;
	fixture->links[0].cost_ms = 100;
	SetVector(fixture->links[0].mechanism_anchor, 0.0f, 0.0f, -2.0f);
	fixture->links[0].sweep_clear_ms = 100U;
	fixture->links[0].mode = RLCM_RIDE;
	fixture->links[0].activation_plan = 0U;
	fixture->links[1].source = 1U;
	fixture->links[1].destination = 0U;
	fixture->links[1].action = RL_RUN;
	fixture->links[1].provenance = RL_PROVEN;
	fixture->links[1].cost_ms = 100;
	fixture->links[1].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;

	fixture->nodes[0].key = 1U;
	fixture->nodes[0].kind = SG_RUNE_CODEC_NODE_BUTTON;
	fixture->nodes[0].flags = SG_RUNE_CODEC_NODEF_REPEATABLE |
		SG_RUNE_CODEC_NODEF_TOUCHABLE | SG_RUNE_CODEC_NODEF_USABLE |
		SG_RUNE_CODEC_NODEF_MOVER;
	fixture->nodes[0].classname_offset = 13U;
	fixture->nodes[0].target_offset = 1U;
	fixture->nodes[0].owner_key = SG_RUNE_CODEC_NO_KEY;
	fixture->nodes[0].team_master_key = SG_RUNE_CODEC_NO_KEY;
	fixture->nodes[0].touch_callback = SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH;
	fixture->nodes[0].use_callback = SG_RUNE_CODEC_CALLBACK_BUTTON_USE;
	fixture->nodes[0].wait_ms = 3000;
	fixture->nodes[0].speed_q8 = 320U;
	fixture->nodes[0].accel_q8 = 320U;
	fixture->nodes[0].decel_q8 = 320U;
	SetBounds(&fixture->nodes[0], -64, -64, -16, 64, 64, 64);

	fixture->nodes[1].key = 2U;
	fixture->nodes[1].kind = SG_RUNE_CODEC_NODE_DOOR_MASTER;
	fixture->nodes[1].flags = SG_RUNE_CODEC_NODEF_USABLE |
		SG_RUNE_CODEC_NODEF_MOVER | SG_RUNE_CODEC_NODEF_TEAM_MASTER;
	fixture->nodes[1].classname_offset = 25U;
	fixture->nodes[1].targetname_offset = 7U;
	fixture->nodes[1].owner_key = SG_RUNE_CODEC_NO_KEY;
	fixture->nodes[1].team_master_key = 2U;
	fixture->nodes[1].use_callback = SG_RUNE_CODEC_CALLBACK_USE_DOOR;
	fixture->nodes[1].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED;
	fixture->nodes[1].blocked_callback =
		SG_RUNE_CODEC_CALLBACK_BLOCKED_DOOR;
	fixture->nodes[1].wait_ms = 3000;
	fixture->nodes[1].speed_q8 = 800U;
	fixture->nodes[1].accel_q8 = 800U;
	fixture->nodes[1].decel_q8 = 800U;
	SetBounds(&fixture->nodes[1], 0, 0, 0, 256, 128, 512);

	fixture->edges[0].from_key = 1U;
	fixture->edges[0].to_key = 2U;
	fixture->edges[0].kind = SG_RUNE_CODEC_EDGE_TARGET;
	fixture->edges[1] = fixture->edges[0];

	fixture->plans[0].entry_key = 1U;
	fixture->plans[0].mover_key = 2U;
	fixture->plans[0].first_edge = TEST_INVENTORY_EDGES;
	fixture->plans[0].num_edges = TEST_PLAN_EDGES;
	fixture->plans[0].controller_kind =
		SG_RUNE_CODEC_CONTROLLER_BUTTON_DOOR;
	fixture->plans[0].flags = SG_RUNE_CODEC_PLANF_TOUCH |
		SG_RUNE_CODEC_PLANF_ATOMIC | SG_RUNE_CODEC_PLANF_REQUIRES_LEASE;
	fixture->plans[0].expected_members = 1U;
	fixture->plans[0].cooldown_ms = 3000U;
	CHECK(SG_RuneCodecPlanClosureCRC32(fixture->edges,
		fixture->plans[0].first_edge, fixture->plans[0].num_edges,
		TEST_EDGES, &closure_crc) == RLCODEC_OK);
	fixture->plans[0].closure_crc32 = closure_crc;
	memcpy(fixture->strings, canonical_strings, TEST_STRING_BYTES);
}

static void DualButtonWorkspace(dual_button_fixture_t *fixture)
{
	sg_rune_codec_workspace_t *workspace = &fixture->workspace;

	memset(workspace, 0, sizeof(*workspace));
	workspace->graph_link_keys = fixture->graph_link_keys;
	workspace->graph_link_key_capacity = DUAL_BUTTON_LINKS;
	workspace->graph_source_marks = fixture->graph_source_marks;
	workspace->graph_source_mark_capacity = TEST_SEEDS;
	workspace->plan_references = fixture->plan_references;
	workspace->plan_reference_capacity = DUAL_BUTTON_PLANS;
	workspace->node_references = fixture->node_references;
	workspace->node_reference_capacity = DUAL_BUTTON_NODES;
	workspace->node_heads = fixture->node_heads;
	workspace->node_head_capacity = DUAL_BUTTON_NODES;
	workspace->node_indegrees = fixture->node_indegrees;
	workspace->node_indegree_capacity = DUAL_BUTTON_NODES;
	workspace->node_generations = fixture->node_generations;
	workspace->node_generation_capacity = DUAL_BUTTON_NODES;
	workspace->node_touched = fixture->node_touched;
	workspace->node_touched_capacity = DUAL_BUTTON_NODES;
	workspace->node_queue = fixture->node_queue;
	workspace->node_queue_capacity = DUAL_BUTTON_NODES;
	workspace->edge_next = fixture->edge_next;
	workspace->edge_next_capacity = DUAL_BUTTON_EDGES;
	workspace->string_marks = fixture->string_marks;
	workspace->string_mark_capacity = sizeof(fixture->string_marks);
}

static void DualButtonInit(dual_button_fixture_t *fixture)
{
	fixture_t base;
	uint32_t closure_crc;

	FixtureInit(&base);
	memset(fixture, 0, sizeof(*fixture));
	memcpy(&fixture->identity, &base.identity, sizeof(fixture->identity));
	memcpy(fixture->seeds, base.seeds, sizeof(fixture->seeds));
	fixture->links[0] = base.links[0];
	fixture->links[1] = base.links[0];
	fixture->links[1].activation_plan = 1U;
	fixture->links[2] = base.links[1];
	fixture->nodes[0] = base.nodes[0];
	fixture->nodes[1] = base.nodes[1];
	fixture->nodes[2] = base.nodes[0];
	fixture->nodes[2].key = 3U;
	fixture->edges[0] = base.edges[0];
	fixture->edges[1] = base.edges[0];
	fixture->edges[1].from_key = 3U;
	fixture->edges[2] = fixture->edges[0];
	fixture->edges[3] = fixture->edges[1];
	fixture->plans[0] = base.plans[0];
	fixture->plans[0].first_edge = 2U;
	CHECK(SG_RuneCodecPlanClosureCRC32(fixture->edges, 2U, 1U,
		DUAL_BUTTON_EDGES, &closure_crc) == RLCODEC_OK);
	fixture->plans[0].closure_crc32 = closure_crc;
	fixture->plans[1] = fixture->plans[0];
	fixture->plans[1].entry_key = 3U;
	fixture->plans[1].first_edge = 3U;
	CHECK(SG_RuneCodecPlanClosureCRC32(fixture->edges, 3U, 1U,
		DUAL_BUTTON_EDGES, &closure_crc) == RLCODEC_OK);
	fixture->plans[1].closure_crc32 = closure_crc;
	memcpy(fixture->strings, canonical_strings, TEST_STRING_BYTES);
	DualButtonWorkspace(fixture);
}

static memory_sink_t MakeSink(unsigned char *bytes, size_t capacity)
{
	memory_sink_t sink;

	memset(&sink, 0, sizeof(sink));
	sink.bytes = bytes;
	sink.capacity = capacity;
	sink.fail_call = SIZE_MAX;
	sink.mutate_call = SIZE_MAX;
	return sink;
}

static void MutateFixture(fixture_t *fixture, mutation_t mutation)
{
	if (!fixture)
		return;
	switch (mutation)
	{
	case MUTATE_IDENTITY:
		fixture->identity.bsp_checksum ^= 1U;
		break;
	case MUTATE_SEED:
		fixture->seeds[0].area_hint ^= 1;
		break;
	case MUTATE_LINK:
		fixture->links[0].cost_ms++;
		break;
	case MUTATE_NODE:
		fixture->nodes[0].speed_q8++;
		break;
	case MUTATE_EDGE:
		fixture->edges[0].ordinal ^= 1U;
		break;
	case MUTATE_PLAN:
		fixture->plans[0].cooldown_ms++;
		break;
	case MUTATE_STRING:
		fixture->strings[1] ^= 0x20U;
		break;
	case MUTATE_NONE:
	default:
		break;
	}
}

static int MemorySink(void *context, const unsigned char *fragment,
	size_t fragment_size)
{
	memory_sink_t *sink = (memory_sink_t *)context;
	size_t call = sink->calls++;

	CHECK(fragment != NULL);
	if (call < sizeof(sink->fragment_sizes) /
	    sizeof(sink->fragment_sizes[0]))
		sink->fragment_sizes[call] = fragment_size;
	if (call == sink->fail_call || fragment_size > sink->capacity - sink->size)
		return -1;
	if (sink->bytes)
		memcpy(sink->bytes + sink->size, fragment, fragment_size);
	sink->size += fragment_size;
	if (call == sink->mutate_call)
		MutateFixture(sink->fixture, sink->mutation);
	return 0;
}

static sg_rune_artifact_write_result_t WriteFixture(fixture_t *fixture,
	memory_sink_t *sink)
{
	FixtureWorkspace(fixture);
	return SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture->identity, fixture->seeds, TEST_SEEDS,
		fixture->links, TEST_LINKS, fixture->nodes, TEST_NODES,
		fixture->edges, TEST_EDGES, fixture->plans, TEST_PLANS,
		fixture->strings, TEST_STRING_BYTES, &fixture->workspace,
		MemorySink, sink);
}

static void ExpectPreflightFailure(fixture_t *fixture,
	sg_rune_codec_diagnostic_t diagnostic, sg_rune_artifact_write_stage_t stage,
	uint32_t index)
{
	memory_sink_t sink = MakeSink(NULL, SIZE_MAX);
	sg_rune_artifact_write_result_t result = WriteFixture(fixture, &sink);

	CHECK_RESULT(result, diagnostic, stage, index);
	CHECK(result.bytes_written == 0U);
	CHECK(result.file_size == TEST_FILE_BYTES);
	CHECK(sink.calls == 0U);
}

static void TestCanonicalParity(void)
{
	static const size_t expected_sizes[TEST_FRAGMENTS] = {
		160U, 16U, 16U, 48U, 48U, 92U, 92U, 16U, 16U, 32U, 35U
	};
	fixture_t fixture;
	fixture_t expected_fixture;
	fixture_t input_copy;
	unsigned char output[TEST_FILE_BYTES];
	unsigned char expected[TEST_FILE_BYTES];
	memory_sink_t sink = MakeSink(output, sizeof(output));
	sg_rune_artifact_write_result_t result;
	size_t expected_size = 0U;
	size_t index;
	uint32_t crc = 0U;

	FixtureInit(&fixture);
	memcpy(&input_copy.identity, &fixture.identity, sizeof(fixture.identity));
	memcpy(input_copy.seeds, fixture.seeds, sizeof(fixture.seeds));
	memcpy(input_copy.links, fixture.links, sizeof(fixture.links));
	memcpy(input_copy.nodes, fixture.nodes, sizeof(fixture.nodes));
	memcpy(input_copy.edges, fixture.edges, sizeof(fixture.edges));
	memcpy(input_copy.plans, fixture.plans, sizeof(fixture.plans));
	memcpy(input_copy.strings, fixture.strings, sizeof(fixture.strings));
	result = WriteFixture(&fixture, &sink);
	CHECK_RESULT(result, RLCODEC_OK, SG_RUNE_ARTIFACT_WRITE_STAGE_DONE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.file_size == TEST_FILE_BYTES);
	CHECK(result.bytes_written == TEST_FILE_BYTES);
	CHECK(sink.size == TEST_FILE_BYTES && sink.calls == TEST_FRAGMENTS);
	for (index = 0U; index < TEST_FRAGMENTS; index++)
		CHECK(sink.fragment_sizes[index] == expected_sizes[index]);
	CHECK(memcmp(&input_copy.identity, &fixture.identity,
		sizeof(fixture.identity)) == 0);
	CHECK(memcmp(input_copy.seeds, fixture.seeds, sizeof(fixture.seeds)) == 0);
	CHECK(memcmp(input_copy.links, fixture.links, sizeof(fixture.links)) == 0);
	CHECK(memcmp(input_copy.nodes, fixture.nodes, sizeof(fixture.nodes)) == 0);
	CHECK(memcmp(input_copy.edges, fixture.edges, sizeof(fixture.edges)) == 0);
	CHECK(memcmp(input_copy.plans, fixture.plans, sizeof(fixture.plans)) == 0);
	CHECK(memcmp(input_copy.strings, fixture.strings,
		sizeof(fixture.strings)) == 0);

	FixtureInit(&expected_fixture);
	CHECK(SG_RuneCodecEncode(RUNE_ROUTE_CONTRACT_COMPLETE,
		&expected_fixture.identity,
		expected_fixture.seeds, TEST_SEEDS, expected_fixture.links,
		TEST_LINKS, expected_fixture.nodes, TEST_NODES,
		expected_fixture.edges, TEST_EDGES, expected_fixture.plans,
		TEST_PLANS, expected_fixture.strings, TEST_STRING_BYTES,
		&expected_fixture.workspace, expected, sizeof(expected),
		&expected_size) == RLCODEC_OK);
	CHECK(expected_size == sizeof(expected));
	CHECK(memcmp(output, expected, sizeof(output)) == 0);
	CHECK(SG_CRC32Buffer(output + SG_RUNE_CODEC_HEADER_BYTES,
		sizeof(output) - SG_RUNE_CODEC_HEADER_BYTES, &crc));
	CHECK(crc == result.payload_crc32);
	CHECK_U32(UINT32_C(0x77264ff8), result.payload_crc32);
	CHECK(SG_ActionRuntimeSupported(RL_BUTTON_DOOR));
}

static void TestNoMechanism(void)
{
	fixture_t fixture;
	unsigned char output[400];
	memory_sink_t sink = MakeSink(output, sizeof(output));
	sg_rune_artifact_write_result_t result;
	size_t expected_size = SG_RUNE_CODEC_HEADER_BYTES +
		TEST_SEEDS * SG_RUNE_CODEC_SEED_BYTES +
		TEST_LINKS * SG_RUNE_CODEC_LINK_BYTES + 1U;

	FixtureInit(&fixture);
	fixture.links[0].action = RL_RUN;
	fixture.links[0].provenance = RL_PROVEN;
	SetVector(fixture.links[0].mechanism_anchor, 0.0f, 0.0f, 0.0f);
	fixture.links[0].sweep_clear_ms = 0U;
	fixture.links[0].mode = RLCM_NONE;
	fixture.links[0].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;
	fixture.strings[0] = 0U;
	FixtureWorkspace(&fixture);
	result = SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture.identity, fixture.seeds, TEST_SEEDS,
		fixture.links, TEST_LINKS, NULL, 0U, NULL, 0U, NULL, 0U,
		fixture.strings, 1U, &fixture.workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLCODEC_OK, SG_RUNE_ARTIFACT_WRITE_STAGE_DONE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.file_size == expected_size);
	CHECK(result.bytes_written == expected_size);
	CHECK(sink.calls == 6U);
	CHECK(sink.fragment_sizes[0] == SG_RUNE_CODEC_HEADER_BYTES);
	CHECK(sink.fragment_sizes[5] == 1U);
}

static void TestRouteContract(void)
{
	fixture_t fixture;
	unsigned char output[TEST_FILE_BYTES];
	memory_sink_t sink = MakeSink(output, sizeof(output));
	sg_rune_artifact_write_result_t result;
	sg_rune_codec_header_t header;

	FixtureInit(&fixture);
	fixture.seeds[0].flags |= SG_RUNE_CODEC_SEED_OBJECTIVE;
	fixture.seeds[1].flags |= SG_RUNE_CODEC_SEED_OBJECTIVE;
	result = SG_RuneArtifactWrite(
		RUNE_ROUTE_CONTRACT_LOCAL_ONLY, &fixture.identity, fixture.seeds,
		TEST_SEEDS, fixture.links, TEST_LINKS, fixture.nodes, TEST_NODES,
		fixture.edges, TEST_EDGES, fixture.plans, TEST_PLANS,
		fixture.strings, TEST_STRING_BYTES, &fixture.workspace, MemorySink,
		&sink);
	CHECK_RESULT(result, RLCODEC_OK, SG_RUNE_ARTIFACT_WRITE_STAGE_DONE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(SG_RuneCodecDecodeHeader(output, SG_RUNE_CODEC_HEADER_BYTES,
		&header) == RLCODEC_OK);
	CHECK(header.route_contract == RUNE_ROUTE_CONTRACT_LOCAL_ONLY);

	FixtureWorkspace(&fixture);
	sink = MakeSink(output, sizeof(output));
	result = SG_RuneArtifactWrite(2U, &fixture.identity,
		fixture.seeds, TEST_SEEDS, fixture.links, TEST_LINKS, fixture.nodes,
		TEST_NODES, fixture.edges, TEST_EDGES, fixture.plans, TEST_PLANS,
		fixture.strings, TEST_STRING_BYTES, &fixture.workspace, MemorySink,
		&sink);
	CHECK_RESULT(result, RLCODEC_BAD_ROUTE_CONTRACT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_HEADER,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.bytes_written == 0U && sink.calls == 0U);
}

static void TestPreflightSections(void)
{
	fixture_t fixture;

	FixtureInit(&fixture);
	fixture.identity.gravity = 0.5f;
	ExpectPreflightFailure(&fixture,
		(sg_rune_codec_diagnostic_t)RLW_BAD_PHYSICS_LAW,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_HEADER,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	FixtureInit(&fixture);
	fixture.seeds[1].flags = 8;
	ExpectPreflightFailure(&fixture,
		(sg_rune_codec_diagnostic_t)RLW_BAD_SEED_RECORD,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_SEED, 1U);
	FixtureInit(&fixture);
	fixture.links[1].action = UINT8_MAX;
	ExpectPreflightFailure(&fixture,
		(sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_LINK, 1U);
	FixtureInit(&fixture);
	fixture.nodes[1].kind = SG_RUNE_CODEC_NODE_NONE;
	ExpectPreflightFailure(&fixture, RLCODEC_BAD_ACTIVATION_NODE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_NODE, 1U);
	FixtureInit(&fixture);
	fixture.edges[0].kind = SG_RUNE_CODEC_EDGE_NONE;
	ExpectPreflightFailure(&fixture, RLCODEC_BAD_ACTIVATION_EDGE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_EDGE, 0U);
	FixtureInit(&fixture);
	fixture.strings[1] = (unsigned char)'z';
	ExpectPreflightFailure(&fixture, RLCODEC_BAD_STRING_POOL,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	FixtureInit(&fixture);
	fixture.plans[0].closure_crc32 ^= 1U;
	ExpectPreflightFailure(&fixture, RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
}

static void TestArgumentsAndBounds(void)
{
	fixture_t fixture;
	memory_sink_t sink;
	sg_rune_artifact_write_result_t result;
	size_t maximum_size = 0U;
	size_t expected_maximum;

	FixtureInit(&fixture);
	sink = MakeSink(NULL, SIZE_MAX);
#define CALL_WRITER(identity_, seeds_, links_, nodes_, edges_, plans_, \
		strings_, workspace_, sink_) \
	SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE, (identity_), \
		(seeds_), TEST_SEEDS, (links_), TEST_LINKS, \
		(nodes_), TEST_NODES, (edges_), TEST_EDGES, (plans_), TEST_PLANS, \
		(strings_), TEST_STRING_BYTES, (workspace_), (sink_), &sink)
	result = CALL_WRITER(NULL, fixture.seeds, fixture.links, fixture.nodes,
		fixture.edges, fixture.plans, fixture.strings, &fixture.workspace,
		MemorySink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.file_size == TEST_FILE_BYTES && sink.calls == 0U);
	result = CALL_WRITER(&fixture.identity, NULL, fixture.links,
		fixture.nodes, fixture.edges, fixture.plans, fixture.strings,
		&fixture.workspace, MemorySink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	result = CALL_WRITER(&fixture.identity, fixture.seeds, NULL,
		fixture.nodes, fixture.edges, fixture.plans, fixture.strings,
		&fixture.workspace, MemorySink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	result = CALL_WRITER(&fixture.identity, fixture.seeds, fixture.links,
		NULL, fixture.edges, fixture.plans, fixture.strings,
		&fixture.workspace, MemorySink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	result = CALL_WRITER(&fixture.identity, fixture.seeds, fixture.links,
		fixture.nodes, fixture.edges, fixture.plans, NULL,
		&fixture.workspace, MemorySink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	result = CALL_WRITER(&fixture.identity, fixture.seeds, fixture.links,
		fixture.nodes, fixture.edges, fixture.plans, fixture.strings,
		NULL, MemorySink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	result = CALL_WRITER(&fixture.identity, fixture.seeds, fixture.links,
		fixture.nodes, fixture.edges, fixture.plans, fixture.strings,
		&fixture.workspace, NULL);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT, SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
#undef CALL_WRITER
	CHECK(sink.calls == 0U);

	result = SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture.identity, fixture.seeds,
		SG_RUNE_CODEC_MAX_SEEDS + 1U, fixture.links, TEST_LINKS,
		fixture.nodes, TEST_NODES, fixture.edges, TEST_EDGES,
		fixture.plans, TEST_PLANS, fixture.strings, TEST_STRING_BYTES,
		&fixture.workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_BAD_COUNTS, SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.file_size == 0U && sink.calls == 0U);
	result = SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture.identity, fixture.seeds, TEST_SEEDS,
		fixture.links, TEST_LINKS, fixture.nodes, TEST_NODES,
		fixture.edges, TEST_EDGES, fixture.plans, TEST_PLANS,
		fixture.strings, SG_RUNE_CODEC_MAX_STRING_BYTES + 1U,
		&fixture.workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_BAD_COUNTS, SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.file_size == 0U && sink.calls == 0U);

	expected_maximum = SG_RUNE_CODEC_HEADER_BYTES +
		(size_t)SG_RUNE_CODEC_MAX_SEEDS * SG_RUNE_CODEC_SEED_BYTES +
		(size_t)SG_RUNE_CODEC_MAX_LINKS * SG_RUNE_CODEC_LINK_BYTES +
		(size_t)SG_RUNE_CODEC_MAX_ACTIVATION_NODES *
			SG_RUNE_CODEC_ACTIVATION_NODE_BYTES +
		(size_t)SG_RUNE_CODEC_MAX_ACTIVATION_EDGES *
			SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES +
		(size_t)SG_RUNE_CODEC_MAX_ACTIVATION_PLANS *
			SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES +
		(size_t)SG_RUNE_CODEC_MAX_STRING_BYTES;
	CHECK(SG_RuneCodecFileSize(SG_RUNE_CODEC_MAX_SEEDS,
		SG_RUNE_CODEC_MAX_LINKS, SG_RUNE_CODEC_MAX_ACTIVATION_NODES,
		SG_RUNE_CODEC_MAX_ACTIVATION_EDGES,
		SG_RUNE_CODEC_MAX_ACTIVATION_PLANS,
		SG_RUNE_CODEC_MAX_STRING_BYTES, &maximum_size) == RLCODEC_OK);
	CHECK(maximum_size == expected_maximum);
	CHECK(SG_RuneCodecFileSize(UINT32_MAX, UINT32_MAX, UINT32_MAX,
		UINT32_MAX, UINT32_MAX, UINT32_MAX, &maximum_size) ==
		(sg_rune_codec_diagnostic_t)RLW_BAD_COUNTS);
	CHECK(maximum_size == 0U);
}

static void TestWorkspaceCapacities(void)
{
	fixture_t fixture;
	memory_sink_t sink;
	sg_rune_artifact_write_result_t result;
	size_t *capacities[11];
	size_t index;

	for (index = 0U; index < 11U; index++)
	{
		FixtureInit(&fixture);
		capacities[0] = &fixture.workspace.graph_link_key_capacity;
		capacities[1] = &fixture.workspace.graph_source_mark_capacity;
		capacities[2] = &fixture.workspace.plan_reference_capacity;
		capacities[3] = &fixture.workspace.node_reference_capacity;
		capacities[4] = &fixture.workspace.node_head_capacity;
		capacities[5] = &fixture.workspace.node_indegree_capacity;
		capacities[6] = &fixture.workspace.node_generation_capacity;
		capacities[7] = &fixture.workspace.node_touched_capacity;
		capacities[8] = &fixture.workspace.node_queue_capacity;
		capacities[9] = &fixture.workspace.edge_next_capacity;
		capacities[10] = &fixture.workspace.string_mark_capacity;
		*capacities[index] = 0U;
		sink = MakeSink(NULL, SIZE_MAX);
		result = SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
			&fixture.identity, fixture.seeds,
			TEST_SEEDS, fixture.links, TEST_LINKS, fixture.nodes,
			TEST_NODES, fixture.edges, TEST_EDGES, fixture.plans,
			TEST_PLANS, fixture.strings, TEST_STRING_BYTES,
			&fixture.workspace, MemorySink, &sink);
		CHECK_RESULT(result, RLW_ALLOCATION_FAILED,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		CHECK(result.bytes_written == 0U && sink.calls == 0U);
	}
}

static void TestWorkspaceAliasing(void)
{
	fixture_t fixture;
	sg_rune_codec_link_t links_before[TEST_LINKS];
	memory_sink_t sink;
	sg_rune_artifact_write_result_t result;

	FixtureInit(&fixture);
	memcpy(links_before, fixture.links, sizeof(links_before));
	fixture.workspace.graph_link_keys = (uint64_t *)fixture.links;
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture.identity, fixture.seeds, TEST_SEEDS,
		fixture.links, TEST_LINKS, fixture.nodes, TEST_NODES,
		fixture.edges, TEST_EDGES, fixture.plans, TEST_PLANS,
		fixture.strings, TEST_STRING_BYTES, &fixture.workspace,
		MemorySink, &sink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.bytes_written == 0U && sink.calls == 0U);
	CHECK(memcmp(links_before, fixture.links, sizeof(links_before)) == 0);

	FixtureInit(&fixture);
	fixture.workspace.node_heads = fixture.workspace.node_references;
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture.identity, fixture.seeds, TEST_SEEDS,
		fixture.links, TEST_LINKS, fixture.nodes, TEST_NODES,
		fixture.edges, TEST_EDGES, fixture.plans, TEST_PLANS,
		fixture.strings, TEST_STRING_BYTES, &fixture.workspace,
		MemorySink, &sink);
	CHECK_RESULT(result, RLW_INVALID_ARGUMENT,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.bytes_written == 0U && sink.calls == 0U);
}

static void TestSinkFailures(void)
{
	static const sg_rune_artifact_write_stage_t expected_stages[TEST_FRAGMENTS] = {
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_HEADER,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_SEED,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_SEED,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_LINK,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_LINK,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_NODE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_NODE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_EDGE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_EDGE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_PLAN,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_STRING_POOL
	};
	static const uint32_t expected_indices[TEST_FRAGMENTS] = {
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE, 0U, 1U, 0U, 1U,
		0U, 1U, 0U, 1U, 0U, 0U
	};
	static const size_t fragment_sizes[TEST_FRAGMENTS] = {
		160U, 16U, 16U, 48U, 48U, 92U, 92U, 16U, 16U, 32U, 35U
	};
	size_t fail_call;

	for (fail_call = 0U; fail_call < TEST_FRAGMENTS; fail_call++)
	{
		fixture_t fixture;
		unsigned char output[TEST_FILE_BYTES];
		memory_sink_t sink = MakeSink(output, sizeof(output));
		sg_rune_artifact_write_result_t result;
		size_t accepted = 0U;
		size_t index;

		FixtureInit(&fixture);
		sink.fail_call = fail_call;
		result = WriteFixture(&fixture, &sink);
		for (index = 0U; index < fail_call; index++)
			accepted += fragment_sizes[index];
		CHECK_RESULT(result, RLW_IO_ERROR, expected_stages[fail_call],
			expected_indices[fail_call]);
		CHECK(result.bytes_written == accepted);
		CHECK(result.file_size == TEST_FILE_BYTES);
		CHECK(sink.size == accepted);
		CHECK(sink.calls == fail_call + 1U);
	}
}

static void TestMutationDetection(void)
{
	mutation_t mutation;

	for (mutation = MUTATE_SEED; mutation <= MUTATE_STRING; mutation++)
	{
		fixture_t fixture;
		unsigned char output[TEST_FILE_BYTES];
		memory_sink_t sink = MakeSink(output, sizeof(output));
		sg_rune_artifact_write_result_t result;

		FixtureInit(&fixture);
		sink.fixture = &fixture;
		sink.mutation = mutation;
		sink.mutate_call = 0U;
		result = WriteFixture(&fixture, &sink);
		CHECK_RESULT(result, RLW_BAD_PAYLOAD_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		CHECK(result.file_size == TEST_FILE_BYTES);
		CHECK(result.bytes_written == TEST_FILE_BYTES);
		CHECK(sink.calls == TEST_FRAGMENTS);
	}
	{
		fixture_t fixture;
		unsigned char output[TEST_FILE_BYTES];
		memory_sink_t sink = MakeSink(output, sizeof(output));
		sg_rune_artifact_write_result_t result;

		FixtureInit(&fixture);
		sink.fixture = &fixture;
		sink.mutation = MUTATE_IDENTITY;
		sink.mutate_call = 0U;
		result = WriteFixture(&fixture, &sink);
		CHECK_RESULT(result, RLW_BAD_HEADER_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_HEADER,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		CHECK(result.bytes_written == TEST_FILE_BYTES);
	}
	/* Mutation after the affected record's pass-two hash is consumed is found
	 * by the final re-hash, not merely by the in-flight CRC. */
	{
		fixture_t fixture;
		unsigned char output[TEST_FILE_BYTES];
		memory_sink_t sink = MakeSink(output, sizeof(output));
		sg_rune_artifact_write_result_t result;

		FixtureInit(&fixture);
		sink.fixture = &fixture;
		sink.mutation = MUTATE_SEED;
		sink.mutate_call = 2U;
		result = WriteFixture(&fixture, &sink);
		CHECK_RESULT(result, RLW_BAD_PAYLOAD_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		CHECK(result.bytes_written == TEST_FILE_BYTES);
	}
}

static sg_rune_artifact_write_result_t WriteDualButtonFixture(
	dual_button_fixture_t *fixture, memory_sink_t *sink)
{
	DualButtonWorkspace(fixture);
	return SG_RuneArtifactWrite(RUNE_ROUTE_CONTRACT_COMPLETE,
		&fixture->identity, fixture->seeds, TEST_SEEDS,
		fixture->links, DUAL_BUTTON_LINKS, fixture->nodes,
		DUAL_BUTTON_NODES, fixture->edges, DUAL_BUTTON_EDGES,
		fixture->plans, DUAL_BUTTON_PLANS, fixture->strings,
		TEST_STRING_BYTES, &fixture->workspace, MemorySink, sink);
}

static void TestActivationPlanLinkIdentity(void)
{
	dual_button_fixture_t fixture;
	dual_button_fixture_t decoded;
	sg_rune_codec_header_t header;
	unsigned char output[1024];
	memory_sink_t sink = MakeSink(output, sizeof(output));
	sg_rune_artifact_write_result_t result;

	DualButtonInit(&fixture);
	result = WriteDualButtonFixture(&fixture, &sink);
	CHECK_RESULT(result, RLCODEC_OK, SG_RUNE_ARTIFACT_WRITE_STAGE_DONE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	DualButtonInit(&decoded);
	CHECK(SG_RuneCodecDecode(output, sink.size, &fixture.identity, &header,
		decoded.seeds, TEST_SEEDS, decoded.links, DUAL_BUTTON_LINKS,
		decoded.nodes, DUAL_BUTTON_NODES, decoded.edges,
		DUAL_BUTTON_EDGES, decoded.plans, DUAL_BUTTON_PLANS,
		decoded.strings, sizeof(decoded.strings), &decoded.workspace) ==
		RLCODEC_OK);
	CHECK(header.num_links == DUAL_BUTTON_LINKS);
	CHECK(decoded.links[0].source == decoded.links[1].source);
	CHECK(decoded.links[0].destination == decoded.links[1].destination);
	CHECK(decoded.links[0].action == decoded.links[1].action);
	CHECK(decoded.links[0].activation_plan == 0U);
	CHECK(decoded.links[1].activation_plan == 1U);
	CHECK(decoded.plans[0].mover_key == decoded.plans[1].mover_key);

	DualButtonInit(&fixture);
	fixture.links[1].activation_plan = fixture.links[0].activation_plan;
	sink = MakeSink(output, sizeof(output));
	result = WriteDualButtonFixture(&fixture, &sink);
	CHECK_RESULT(result, RLW_DUPLICATE_LINK,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
		SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
	CHECK(result.bytes_written == 0U && sink.calls == 0U);
}

int main(void)
{
	TestCanonicalParity();
	TestNoMechanism();
	TestRouteContract();
	TestPreflightSections();
	TestArgumentsAndBounds();
	TestWorkspaceCapacities();
	TestWorkspaceAliasing();
	TestSinkFailures();
	TestMutationDetection();
	TestActivationPlanLinkIdentity();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_artifact_writer_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_artifact_writer_test: PASS");
	return 0;
}
