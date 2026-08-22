/* Golden-vector and fail-closed tests for the isolated RUNE codec. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_action.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_codec.h"

#define TEST_SEEDS 2U
#define TEST_LINKS 2U
#define TEST_NODES 2U
#define TEST_INVENTORY_EDGES 1U
#define TEST_PLAN_EDGES 1U
#define TEST_EDGES (TEST_INVENTORY_EDGES + TEST_PLAN_EDGES)
#define TEST_PLANS 1U
#define TEST_NODE_CAPACITY 3U
#define TEST_EDGE_CAPACITY 6U
#define TEST_STRING_BYTES 35U
#define TEST_FILE_BYTES 547U

#define SEED_OFFSET SG_RUNE_CODEC_HEADER_BYTES
#define LINK_OFFSET (SEED_OFFSET + TEST_SEEDS * SG_RUNE_CODEC_SEED_BYTES)
#define NODE_OFFSET (LINK_OFFSET + TEST_LINKS * SG_RUNE_CODEC_LINK_BYTES)
#define EDGE_OFFSET (NODE_OFFSET + TEST_NODES * \
	SG_RUNE_CODEC_ACTIVATION_NODE_BYTES)
#define PLAN_OFFSET (EDGE_OFFSET + TEST_EDGES * \
	SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES)
#define STRING_OFFSET (PLAN_OFFSET + TEST_PLANS * \
	SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	sg_rune_codec_diagnostic_t actual_ = (expression); \
	if (actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: expected diagnostic %d, got %d: %s\n", \
		        __FILE__, __LINE__, (int)(expected), (int)actual_, \
		        #expression); \
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

typedef struct fixture_s
{
	sg_rune_codec_identity_t identity;
	sg_rune_codec_seed_t seeds[TEST_SEEDS];
	sg_rune_codec_link_t links[TEST_LINKS];
	sg_rune_codec_activation_node_t nodes[TEST_NODE_CAPACITY];
	sg_rune_codec_activation_edge_t edges[TEST_EDGE_CAPACITY];
	sg_rune_codec_activation_plan_t plans[TEST_PLANS];
	unsigned char strings[64];

	uint64_t graph_link_keys[TEST_LINKS];
	uint8_t graph_source_marks[TEST_SEEDS];
	uint32_t plan_references[TEST_PLANS];
	uint32_t node_references[TEST_NODE_CAPACITY];
	uint32_t node_heads[TEST_NODE_CAPACITY];
	uint32_t node_indegrees[TEST_NODE_CAPACITY];
	uint32_t node_generations[TEST_NODE_CAPACITY];
	uint32_t node_touched[TEST_NODE_CAPACITY];
	uint32_t node_queue[TEST_NODE_CAPACITY];
	uint32_t edge_next[TEST_EDGE_CAPACITY];
	uint8_t string_marks[64];
	sg_rune_codec_workspace_t workspace;
} fixture_t;

static const unsigned char canonical_strings[TEST_STRING_BYTES] =
	"\0Door1\0door1\0func_button\0func_door";

static void PutU16(unsigned char *out, uint16_t value)
{
	out[0] = (unsigned char)(value & UINT16_C(0xff));
	out[1] = (unsigned char)(value >> 8);
}

static void PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static uint32_t GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static void SetMap(char map_name[SG_RUNE_CODEC_MAP_NAME_BYTES],
	const char *value)
{
	size_t length = strlen(value);

	memset(map_name, 0, SG_RUNE_CODEC_MAP_NAME_BYTES);
	CHECK(length < SG_RUNE_CODEC_MAP_NAME_BYTES);
	if (length < SG_RUNE_CODEC_MAP_NAME_BYTES)
		memcpy(map_name, value, length);
}

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
	workspace->node_reference_capacity = TEST_NODE_CAPACITY;
	workspace->node_heads = fixture->node_heads;
	workspace->node_head_capacity = TEST_NODE_CAPACITY;
	workspace->node_indegrees = fixture->node_indegrees;
	workspace->node_indegree_capacity = TEST_NODE_CAPACITY;
	workspace->node_generations = fixture->node_generations;
	workspace->node_generation_capacity = TEST_NODE_CAPACITY;
	workspace->node_touched = fixture->node_touched;
	workspace->node_touched_capacity = TEST_NODE_CAPACITY;
	workspace->node_queue = fixture->node_queue;
	workspace->node_queue_capacity = TEST_NODE_CAPACITY;
	workspace->edge_next = fixture->edge_next;
	workspace->edge_next_capacity = TEST_EDGE_CAPACITY;
	workspace->string_marks = fixture->string_marks;
	workspace->string_mark_capacity = sizeof(fixture->string_marks);
}

static void FixtureInit(fixture_t *fixture)
{
	uint32_t closure_crc = 0U;

	memset(fixture, 0, sizeof(*fixture));
	FixtureWorkspace(fixture);
	SetMap(fixture->identity.map_name, "codectest");
	fixture->identity.bsp_checksum = UINT32_C(0x12345678);
	fixture->identity.entity_crc32 = UINT32_C(0x9abcdef0);
	fixture->identity.gravity = 650.0f;
	fixture->identity.airaccelerate = 0.0f;
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
	fixture->edges[0].ordinal = 0U;
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
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecPlanClosureCRC32(fixture->edges,
		TEST_INVENTORY_EDGES, TEST_PLAN_EDGES, TEST_EDGES, &closure_crc));
	fixture->plans[0].closure_crc32 = closure_crc;
	memcpy(fixture->strings, canonical_strings, TEST_STRING_BYTES);
}

static sg_rune_codec_diagnostic_t ValidateFixture(fixture_t *fixture)
{
	FixtureWorkspace(fixture);
	return SG_RuneCodecValidate(fixture->seeds, TEST_SEEDS,
		fixture->links, TEST_LINKS, fixture->nodes, TEST_NODES,
		fixture->edges, TEST_EDGES, fixture->plans, TEST_PLANS,
		fixture->strings, TEST_STRING_BYTES, &fixture->workspace);
}

static sg_rune_codec_diagnostic_t ValidateFixtureCounts(fixture_t *fixture,
	uint32_t num_nodes, uint32_t num_edges)
{
	FixtureWorkspace(fixture);
	return SG_RuneCodecValidate(fixture->seeds, TEST_SEEDS,
		fixture->links, TEST_LINKS, fixture->nodes, num_nodes,
		fixture->edges, num_edges, fixture->plans, TEST_PLANS,
		fixture->strings, TEST_STRING_BYTES, &fixture->workspace);
}

static void MakeFrameCompleteButton(sg_rune_codec_activation_node_t *node)
{
	node->flags = SG_RUNE_CODEC_NODEF_REPEATABLE |
		SG_RUNE_CODEC_NODEF_USABLE | SG_RUNE_CODEC_NODEF_MOVER |
		SG_RUNE_CODEC_NODEF_SHOOTABLE |
		SG_RUNE_CODEC_NODEF_FRAME_COMPLETE_MOVER;
	node->touch_callback = SG_RUNE_CODEC_CALLBACK_NONE;
	node->use_callback = SG_RUNE_CODEC_CALLBACK_BUTTON_USE;
	node->think_callback = SG_RUNE_CODEC_CALLBACK_NONE;
	node->blocked_callback = SG_RUNE_CODEC_CALLBACK_NONE;
	node->speed_q8 = node->accel_q8 = node->decel_q8 = 7600U;
}

static void AddDoorMember(fixture_t *fixture)
{
	fixture->nodes[2] = fixture->nodes[1];
	fixture->nodes[2].key = 3U;
	fixture->nodes[2].kind = SG_RUNE_CODEC_NODE_DOOR_MEMBER;
	fixture->nodes[2].flags = SG_RUNE_CODEC_NODEF_USABLE |
		SG_RUNE_CODEC_NODEF_MOVER | SG_RUNE_CODEC_NODEF_TEAM_MEMBER;
	fixture->nodes[2].targetname_offset = 0U;
	fixture->nodes[2].team_master_key = 2U;
	fixture->nodes[2].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_SPAWN_DOOR_TRIGGER;
	fixture->edges[1].from_key = 2U;
	fixture->edges[1].to_key = 3U;
	fixture->edges[1].kind = SG_RUNE_CODEC_EDGE_TEAM;
	fixture->edges[1].ordinal = 0U;
	fixture->edges[2] = fixture->edges[0];
	fixture->edges[3] = fixture->edges[1];
	fixture->plans[0].first_edge = 2U;
	fixture->plans[0].num_edges = 2U;
	fixture->plans[0].expected_members = 2U;
}

static void AddTargetedDoorMember(fixture_t *fixture)
{
	fixture->nodes[2] = fixture->nodes[1];
	fixture->nodes[2].key = 3U;
	fixture->nodes[2].kind = SG_RUNE_CODEC_NODE_DOOR_MEMBER;
	fixture->nodes[2].flags = SG_RUNE_CODEC_NODEF_USABLE |
		SG_RUNE_CODEC_NODEF_MOVER | SG_RUNE_CODEC_NODEF_TEAM_MEMBER;
	fixture->nodes[2].team_master_key = 2U;
	/* The mapper gave every member the button's targetname, so this slave
	 * retains the per-brush targeted spawn callback. */
	fixture->nodes[2].targetname_offset = 7U;
	fixture->nodes[2].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED;
	fixture->edges[1].from_key = 1U;
	fixture->edges[1].to_key = 3U;
	fixture->edges[1].kind = SG_RUNE_CODEC_EDGE_TARGET;
	fixture->edges[1].ordinal = 1U;
	fixture->edges[2].from_key = 2U;
	fixture->edges[2].to_key = 3U;
	fixture->edges[2].kind = SG_RUNE_CODEC_EDGE_TEAM;
	fixture->edges[2].ordinal = 0U;
	fixture->edges[3] = fixture->edges[0];
	fixture->edges[4] = fixture->edges[1];
	fixture->edges[5] = fixture->edges[2];
	fixture->plans[0].first_edge = 3U;
	fixture->plans[0].num_edges = 3U;
	fixture->plans[0].expected_members = 2U;
}

static void FixHeaderCRC(unsigned char data[TEST_FILE_BYTES])
{
	uint32_t crc = 0U;

	PutU32(data + SG_RUNE_CODEC_HEADER_CRC_OFFSET, 0U);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecHeaderCRC32(data,
		SG_RUNE_CODEC_HEADER_BYTES, &crc));
	PutU32(data + SG_RUNE_CODEC_HEADER_CRC_OFFSET, crc);
}

static void FixPayloadCRC(unsigned char data[TEST_FILE_BYTES])
{
	uint32_t crc = 0U;

	CHECK(SG_CRC32Buffer(data + SG_RUNE_CODEC_HEADER_BYTES,
		TEST_FILE_BYTES - SG_RUNE_CODEC_HEADER_BYTES, &crc));
	PutU32(data + 20, crc);
	FixHeaderCRC(data);
}

static void TestPrimitiveGolden(void)
{
	fixture_t fixture;
	unsigned char encoded_node[SG_RUNE_CODEC_ACTIVATION_NODE_BYTES];
	unsigned char encoded_edge[SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES];
	unsigned char encoded_plan[SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES];
	unsigned char encoded_link[SG_RUNE_CODEC_LINK_BYTES];
	sg_rune_codec_activation_node_t decoded_node;
	sg_rune_codec_activation_edge_t decoded_edge;
	sg_rune_codec_activation_plan_t decoded_plan;
	sg_rune_codec_link_t decoded_link;
	static const unsigned char golden_node[80] = {
		[0] = 0x01, [4] = 0x02, [6] = 0x1e,
		[8] = 0x0d, [12] = 0x01,
		[24] = 0xff, [25] = 0xff, [26] = 0xff, [27] = 0xff,
		[28] = 0xff, [29] = 0xff, [30] = 0xff, [31] = 0xff,
		[36] = 0x03, [38] = 0x05,
		[48] = 0xb8, [49] = 0x0b,
		[52] = 0x40, [53] = 0x01,
		[56] = 0x40, [57] = 0x01,
		[60] = 0x40, [61] = 0x01,
		[64] = 0xc0, [65] = 0xff,
		[66] = 0xc0, [67] = 0xff,
		[68] = 0xf0, [69] = 0xff,
		[70] = 0x40, [72] = 0x40, [74] = 0x40
	};
	static const unsigned char golden_edge[16] = {
		0x01, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	};
	static const unsigned char golden_plan[32] = {
		0x01, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00,
		0x03, 0x00, 0x00, 0x00,
		0x0d, 0x00, 0x01, 0x00,
		0xb8, 0x0b, 0x00, 0x00,
		0xc5, 0x44, 0x42, 0x62
	};
	static const unsigned char golden_link[48] = {
		[4] = 0x01, [8] = 0x0c, [9] = 0x03, [14] = 0x64,
		[39] = 0xc0, [40] = 0x64, [42] = 0x02,
		[44] = 0x44, [45] = 0x33, [46] = 0x22, [47] = 0x11
	};

	FixtureInit(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncodeActivationNode(
		&fixture.nodes[0], encoded_node, sizeof(encoded_node)));
	CHECK(memcmp(encoded_node, golden_node, sizeof(golden_node)) == 0);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecDecodeActivationNode(encoded_node,
		sizeof(encoded_node), &decoded_node));
	CHECK(decoded_node.key == 1U && decoded_node.wait_ms == 3000);

	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncodeActivationEdge(
		&fixture.edges[0], encoded_edge, sizeof(encoded_edge)));
	CHECK(memcmp(encoded_edge, golden_edge, sizeof(golden_edge)) == 0);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecDecodeActivationEdge(encoded_edge,
		sizeof(encoded_edge), &decoded_edge));
	CHECK(decoded_edge.from_key == 1U && decoded_edge.to_key == 2U);

	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncodeActivationPlan(
		&fixture.plans[0], encoded_plan, sizeof(encoded_plan)));
	CHECK(memcmp(encoded_plan, golden_plan, sizeof(golden_plan)) == 0);
	CHECK(GetU32(encoded_plan + 0) == 1U);
	CHECK(GetU32(encoded_plan + 4) == 2U);
	CHECK(GetU32(encoded_plan + 12) == TEST_PLAN_EDGES);
	CHECK(GetU32(encoded_plan + 28) == fixture.plans[0].closure_crc32);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecDecodeActivationPlan(encoded_plan,
		sizeof(encoded_plan), &decoded_plan));
	CHECK(decoded_plan.controller_kind == SG_RUNE_CODEC_CONTROLLER_BUTTON_DOOR);
	PutU16(encoded_plan + 18U, 1U);
	CHECK_DIAGNOSTIC(RLCODEC_NONZERO_RESERVED,
		SG_RuneCodecDecodeActivationPlan(encoded_plan, sizeof(encoded_plan),
			&decoded_plan));

	fixture.links[0].activation_plan = UINT32_C(0x11223344);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncodeLink(&fixture.links[0],
		encoded_link, sizeof(encoded_link)));
	CHECK(memcmp(encoded_link, golden_link, sizeof(golden_link)) == 0);
	CHECK(encoded_link[44] == 0x44 && encoded_link[45] == 0x33 &&
	      encoded_link[46] == 0x22 && encoded_link[47] == 0x11);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecDecodeLink(encoded_link,
		sizeof(encoded_link), &decoded_link));
	CHECK(decoded_link.activation_plan == UINT32_C(0x11223344));
	CHECK(decoded_link.action == RL_BUTTON_DOOR);
}

static sg_rune_codec_diagnostic_t EncodeFixture(fixture_t *fixture,
	unsigned char encoded[TEST_FILE_BYTES], size_t *encoded_size)
{
	FixtureWorkspace(fixture);
	return SG_RuneCodecEncode(&fixture->identity, fixture->seeds, TEST_SEEDS,
		fixture->links, TEST_LINKS, fixture->nodes, TEST_NODES,
		fixture->edges, TEST_EDGES, fixture->plans, TEST_PLANS,
		fixture->strings, TEST_STRING_BYTES, &fixture->workspace,
		encoded, TEST_FILE_BYTES, encoded_size);
}

static sg_rune_codec_diagnostic_t DecodeFixture(
	const unsigned char encoded[TEST_FILE_BYTES], size_t encoded_size,
	const sg_rune_codec_identity_t *identity, fixture_t *decoded,
	sg_rune_codec_header_t *header)
{
	memset(decoded, 0, sizeof(*decoded));
	FixtureWorkspace(decoded);
	return SG_RuneCodecDecode(encoded, encoded_size, identity, header,
		decoded->seeds, TEST_SEEDS, decoded->links, TEST_LINKS,
		decoded->nodes, TEST_NODES, decoded->edges, TEST_EDGES,
		decoded->plans, TEST_PLANS, decoded->strings,
		sizeof(decoded->strings), &decoded->workspace);
}

static void TestWholeGolden(void)
{
	fixture_t fixture;
	fixture_t decoded;
	sg_rune_codec_header_t header;
	unsigned char encoded[TEST_FILE_BYTES];
	size_t encoded_size = 0U;
	size_t computed_size = 0U;
	uint32_t descriptor_crc = 0U;
	uint32_t action_descriptor_crc = 0U;
	uint32_t file_crc = 0U;
	static const unsigned char golden_extension[32] = {
		0x50, 0x00, 0x10, 0x00, 0x20, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
		0x0f, 0xe5, 0x8d, 0xdd, 0x01, 0x00, 0x00, 0x00
	};

	FixtureInit(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecFileSize(TEST_SEEDS,
		TEST_LINKS, TEST_NODES, TEST_EDGES, TEST_PLANS,
		TEST_STRING_BYTES, &computed_size));
	CHECK(computed_size == TEST_FILE_BYTES);
	CHECK(SG_CRC32Buffer(SG_RUNE_MECHANISM_CONTRACT_DESCRIPTOR,
		strlen(SG_RUNE_MECHANISM_CONTRACT_DESCRIPTOR), &descriptor_crc));
	CHECK_U32(SG_RUNE_MECHANISM_CONTRACT_CRC32, descriptor_crc);
	CHECK(SG_CRC32Buffer(SG_RUNE_ACTION_CONTRACT_DESCRIPTOR,
		strlen(SG_RUNE_ACTION_CONTRACT_DESCRIPTOR),
		&action_descriptor_crc));
	CHECK(SG_RUNE_ACTION_CONTRACT_CRC32 != action_descriptor_crc);
	CHECK_DIAGNOSTIC(RLCODEC_OK, EncodeFixture(&fixture, encoded,
		&encoded_size));
	CHECK(encoded_size == TEST_FILE_BYTES);
	CHECK(GetU32(encoded + 0) == SG_RUNE_CODEC_MAGIC);
	CHECK(GetU32(encoded + 32) == SG_RUNE_ACTION_CONTRACT_CRC32);
	CHECK(SG_ActionWireValid(RL_BUTTON_DOOR));
	CHECK(SG_RUNE_WIRE_ACTION_MAX == RL_BUTTON_DOOR);
	CHECK(encoded[4] == 0U && encoded[5] == 0U);
	CHECK(memcmp(encoded + 128, golden_extension,
		sizeof(golden_extension)) == 0);
	CHECK(GetU32(encoded + LINK_OFFSET + 44) == 0U);
	CHECK(GetU32(encoded + LINK_OFFSET + SG_RUNE_CODEC_LINK_BYTES + 44) ==
		SG_RUNE_CODEC_NO_ACTIVATION_PLAN);
	CHECK(memcmp(encoded + STRING_OFFSET, canonical_strings,
		TEST_STRING_BYTES) == 0);
	CHECK_U32(UINT32_C(0x624244c5), fixture.plans[0].closure_crc32);
	CHECK_U32(UINT32_C(0x6c814182), GetU32(encoded + 20));
	CHECK_U32(UINT32_C(0x6335f469),
		GetU32(encoded + SG_RUNE_CODEC_HEADER_CRC_OFFSET));
	CHECK(SG_CRC32Buffer(encoded, sizeof(encoded), &file_crc));
	CHECK_U32(UINT32_C(0x611a80ed), file_crc);

	CHECK_DIAGNOSTIC(RLCODEC_OK, DecodeFixture(encoded, encoded_size,
		&fixture.identity, &decoded, &header));
	CHECK(header.header_bytes == SG_RUNE_CODEC_HEADER_BYTES);
	CHECK(header.num_activation_nodes == TEST_NODES);
	CHECK(header.num_activation_edges == TEST_EDGES);
	CHECK(header.num_activation_plans == TEST_PLANS);
	CHECK(decoded.links[0].activation_plan == 0U);
	CHECK(decoded.links[1].activation_plan ==
		SG_RUNE_CODEC_NO_ACTIVATION_PLAN);
	CHECK(decoded.nodes[0].kind == SG_RUNE_CODEC_NODE_BUTTON);
	CHECK(decoded.nodes[0].touch_callback ==
		SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH);
	CHECK(decoded.nodes[0].use_callback == SG_RUNE_CODEC_CALLBACK_BUTTON_USE);
	CHECK(decoded.nodes[1].use_callback == SG_RUNE_CODEC_CALLBACK_USE_DOOR);
	CHECK(decoded.plans[0].closure_crc32 ==
		fixture.plans[0].closure_crc32);
	CHECK(memcmp(decoded.strings, canonical_strings,
		TEST_STRING_BYTES) == 0);
}

static void TestPrimitiveMalformed(void)
{
	fixture_t fixture;
	unsigned char encoded[SG_RUNE_CODEC_ACTIVATION_NODE_BYTES];
	sg_rune_codec_activation_node_t decoded;

	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	memset(&decoded, 0, sizeof(decoded));
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		SG_RuneCodecDecodeActivationNode(encoded, sizeof(encoded), &decoded));
	CHECK(memcmp(&decoded, &fixture.nodes[0], sizeof(decoded)) == 0);
	fixture.nodes[0].kind = SG_RUNE_CODEC_NODE_DOOR_MASTER;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	fixture.nodes[0].flags |= SG_RUNE_CODEC_NODEF_INVENTORY_ONLY;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	fixture.nodes[0].flags &= (uint16_t)~SG_RUNE_CODEC_NODEF_MOVER;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	fixture.nodes[0].flags |= SG_RUNE_CODEC_NODEF_SYNTHETIC;
	fixture.nodes[0].owner_key = 2U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	fixture.nodes[0].speed_q8 = fixture.nodes[0].accel_q8 =
		fixture.nodes[0].decel_q8 = 0U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	fixture.nodes[0].accel_q8++;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	fixture.nodes[0].speed_q8 = fixture.nodes[0].accel_q8 =
		fixture.nodes[0].decel_q8 = 7601U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));

	FixtureInit(&fixture);
	SetVector(fixture.links[0].mechanism_anchor, 0.0f, 0.0f, 0.0f);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));
	FixtureInit(&fixture);
	SetVector(fixture.links[0].mechanism_anchor, -0.0f, 0.0f, 0.0f);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));
	FixtureInit(&fixture);
	fixture.links[0].mechanism_anchor[0] = 0.03125f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));
	FixtureInit(&fixture);
	fixture.links[0].mode = RLCM_NONE;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));
	FixtureInit(&fixture);
	fixture.links[0].sweep_clear_ms = 50U;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));
	FixtureInit(&fixture);
	fixture.links[0].suffix_anchor[0] =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MAX /
		(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;
	fixture.links[0].mechanism_anchor[0] = 0.125f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));
	FixtureInit(&fixture);
	fixture.links[0].suffix_anchor[0] =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MIN /
		(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;
	fixture.links[0].mechanism_anchor[0] = -0.125f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		SG_RuneCodecEncodeLink(&fixture.links[0], encoded,
			SG_RUNE_CODEC_LINK_BYTES));

	FixtureInit(&fixture);
	fixture.seeds[0].origin[2] = 0.03125f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_SEED_RECORD,
		SG_RuneCodecEncodeSeed(&fixture.seeds[0], encoded,
			SG_RUNE_CODEC_SEED_BYTES));
	FixtureInit(&fixture);
	fixture.seeds[0].origin[2] = -0.03125f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_SEED_RECORD,
		SG_RuneCodecEncodeSeed(&fixture.seeds[0], encoded,
			SG_RUNE_CODEC_SEED_BYTES));

	FixtureInit(&fixture);
	fixture.nodes[0].speed_q8 = SG_RUNE_CODEC_MAX_Q8 + 1U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[0].kind = SG_RUNE_CODEC_NODE_NONE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[0].flags |= UINT16_C(0x8000);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[0].touch_callback = UINT16_MAX;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[1].use_callback = SG_RUNE_CODEC_CALLBACK_USE_MULTI;
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[1], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[0].killtarget_offset = 1U;
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[0].delay_ms = 1;
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));
	FixtureInit(&fixture);
	fixture.nodes[0].absmin_q8[0] = 2;
	fixture.nodes[0].absmax_q8[0] = 1;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneCodecEncodeActivationNode(&fixture.nodes[0], encoded,
			sizeof(encoded)));

	FixtureInit(&fixture);
	fixture.edges[0].kind = SG_RUNE_CODEC_EDGE_NONE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_EDGE,
		SG_RuneCodecEncodeActivationEdge(&fixture.edges[0], encoded, 16U));
	FixtureInit(&fixture);
	fixture.edges[0].delay_ms = 1U;
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		SG_RuneCodecEncodeActivationEdge(&fixture.edges[0], encoded, 16U));
	FixtureInit(&fixture);
	fixture.plans[0].controller_kind = SG_RUNE_CODEC_CONTROLLER_RELAY_DOOR;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecEncodeActivationPlan(&fixture.plans[0], encoded, 32U));
	FixtureInit(&fixture);
	fixture.plans[0].flags |= UINT16_C(0x8000);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecEncodeActivationPlan(&fixture.plans[0], encoded, 32U));
	FixtureInit(&fixture);
	fixture.plans[0].flags |= SG_RUNE_CODEC_PLANF_USE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecEncodeActivationPlan(&fixture.plans[0], encoded, 32U));
	FixtureInit(&fixture);
	fixture.plans[0].flags &= (uint16_t)~SG_RUNE_CODEC_PLANF_ATOMIC;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecEncodeActivationPlan(&fixture.plans[0], encoded, 32U));
	FixtureInit(&fixture);
	fixture.plans[0].flags &=
		(uint16_t)~SG_RUNE_CODEC_PLANF_REQUIRES_LEASE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecEncodeActivationPlan(&fixture.plans[0], encoded, 32U));
	FixtureInit(&fixture);
	fixture.plans[0].closure_crc32 = 0U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecEncodeActivationPlan(&fixture.plans[0], encoded, 32U));
}

static sg_rune_codec_diagnostic_t ValidateFixtureStringBytes(fixture_t *fixture,
	uint32_t string_bytes)
{
	FixtureWorkspace(fixture);
	return SG_RuneCodecValidate(fixture->seeds, TEST_SEEDS,
		fixture->links, TEST_LINKS, fixture->nodes, TEST_NODES,
		fixture->edges, TEST_EDGES, fixture->plans, TEST_PLANS,
		fixture->strings, string_bytes, &fixture->workspace);
}

static void RecomputeClosure(fixture_t *fixture, uint32_t total_edges)
{
	uint32_t crc = 0U;

	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecPlanClosureCRC32(fixture->edges,
		fixture->plans[0].first_edge, fixture->plans[0].num_edges,
		total_edges, &crc));
	fixture->plans[0].closure_crc32 = crc;
}

static void TestGraphMalformed(void)
{
	fixture_t fixture;

	FixtureInit(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_OK, ValidateFixture(&fixture));
	CHECK(fixture.nodes[0].target_offset !=
		fixture.nodes[1].targetname_offset);
	FixtureInit(&fixture);
	MakeFrameCompleteButton(&fixture.nodes[0]);
	/* Shootable frame-complete buttons are catalog inventory, never a
	 * BUTTON_DOOR controller entry. */
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		ValidateFixture(&fixture));

	FixtureInit(&fixture);
	fixture.nodes[1].key = fixture.nodes[0].key;
	fixture.nodes[1].team_master_key = fixture.nodes[0].key;
	CHECK_DIAGNOSTIC(RLCODEC_DUPLICATE_NODE_KEY, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].owner_key = 99U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_MECHANISM_GRAPH, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].classname_offset = 14U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_STRING_POOL, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.strings[1] = (unsigned char)'z';
	CHECK_DIAGNOSTIC(RLCODEC_BAD_STRING_POOL, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.strings[35] = (unsigned char)'z';
	fixture.strings[36] = (unsigned char)'z';
	fixture.strings[37] = 0U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_STRING_POOL,
		ValidateFixtureStringBytes(&fixture, 38U));

	FixtureInit(&fixture);
	fixture.links[0].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.links[0].action = RL_RUN;
	fixture.links[0].provenance = RL_PROVEN;
	SetVector(fixture.links[0].mechanism_anchor, 0.0f, 0.0f, 0.0f);
	fixture.links[0].sweep_clear_ms = 0U;
	fixture.links[0].mode = RLCM_NONE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.links[1].action = RL_DOOR;
	fixture.links[1].provenance = RL_DECLARED;
	fixture.links[1].activation_plan = 0U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.links[0].action = (uint8_t)SG_RUNE_WIRE_ACTION_COUNT;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.plans[0].controller_kind = SG_RUNE_CODEC_CONTROLLER_RELAY_DOOR;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));

	FixtureInit(&fixture);
	fixture.edges[0].ordinal = 1U;
	RecomputeClosure(&fixture, TEST_EDGES);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_EDGE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.edges[0].delay_ms = 1U;
	fixture.edges[1].delay_ms = 1U;
	RecomputeClosure(&fixture, TEST_EDGES);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[1].use_callback = SG_RUNE_CODEC_CALLBACK_USE_MULTI;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].killtarget_offset = 1U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].delay_ms = 1;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].target_offset = 13U;
	fixture.nodes[0].targetname_offset = 1U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_EDGE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.plans[0].closure_crc32 ^= 1U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.plans[0].expected_members = 2U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));

	FixtureInit(&fixture);
	fixture.plans[0].cooldown_ms--;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_BUTTON_WAIT;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[1].blocked_callback = SG_RUNE_CODEC_CALLBACK_NONE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[1].think_callback = SG_RUNE_CODEC_CALLBACK_NONE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[1].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_SPAWN_DOOR_TRIGGER;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].accel_q8--;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[1].decel_q8++;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].flags &= (uint16_t)~SG_RUNE_CODEC_NODEF_REPEATABLE;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].wait_ms = -1;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[0].classname_offset = 25U;
	fixture.nodes[0].targetname_offset = 13U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));
	FixtureInit(&fixture);
	fixture.nodes[1].spawnflags = 32U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN, ValidateFixture(&fixture));

	/* A second case-insensitive targetname match cannot be omitted from the
	 * closure, even when it is otherwise a well-formed door master. */
	FixtureInit(&fixture);
	fixture.nodes[2] = fixture.nodes[1];
	fixture.nodes[2].key = 3U;
	fixture.nodes[2].targetname_offset = 1U;
	fixture.nodes[2].team_master_key = 3U;
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		ValidateFixtureCounts(&fixture, 3U, TEST_EDGES));

	/* Every member of mover_key's canonical team requires its TEAM edge. */
	FixtureInit(&fixture);
	AddDoorMember(&fixture);
	RecomputeClosure(&fixture, 4U);
	CHECK_DIAGNOSTIC(RLCODEC_OK, ValidateFixtureCounts(&fixture, 3U, 4U));
	FixtureInit(&fixture);
	AddDoorMember(&fixture);
	fixture.nodes[2].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED;
	RecomputeClosure(&fixture, 4U);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		ValidateFixtureCounts(&fixture, 3U, 4U));
	/* Callback choice is per brush at spawn time, not derived from its later
	 * team role.  A targeted slave retains Think_CalcMoveSpeed. */
	FixtureInit(&fixture);
	AddDoorMember(&fixture);
	fixture.nodes[2].targetname_offset = 13U;
	fixture.nodes[2].think_callback =
		SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED;
	RecomputeClosure(&fixture, 4U);
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		ValidateFixtureCounts(&fixture, 3U, 4U));

	/* A button's exhaustive target fanout may reach the canonical master and
	 * then the same-team slave no-op.  Both TARGET records and the physical
	 * TEAM closure are authenticated. */
	FixtureInit(&fixture);
	AddTargetedDoorMember(&fixture);
	RecomputeClosure(&fixture, 6U);
	CHECK_DIAGNOSTIC(RLCODEC_OK,
		ValidateFixtureCounts(&fixture, 3U, 6U));
	FixtureInit(&fixture);
	AddTargetedDoorMember(&fixture);
	fixture.edges[0].to_key = 3U;
	fixture.edges[1].to_key = 2U;
	fixture.edges[3] = fixture.edges[0];
	fixture.edges[4] = fixture.edges[1];
	RecomputeClosure(&fixture, 6U);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		ValidateFixtureCounts(&fixture, 3U, 6U));
	FixtureInit(&fixture);
	AddDoorMember(&fixture);
	fixture.plans[0].num_edges = 1U;
	fixture.plans[0].expected_members = 1U;
	RecomputeClosure(&fixture, 3U);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		ValidateFixtureCounts(&fixture, 3U, 3U));
}

static void TestWholeMalformed(void)
{
	fixture_t fixture;
	fixture_t decoded;
	sg_rune_codec_header_t header;
	sg_rune_codec_header_t header_before;
	unsigned char golden[TEST_FILE_BYTES];
	unsigned char mutated[TEST_FILE_BYTES];
	size_t encoded_size = 0U;

	FixtureInit(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_OK, EncodeFixture(&fixture, golden,
		&encoded_size));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + SEED_OFFSET + 8U, UINT32_C(0x3d000000));
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_SEED_RECORD,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 4, 1U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_NONZERO_RESERVED,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 134U, 1U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_NONZERO_RESERVED,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 128, 79U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_MECHANISM_CONTRACT,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 32, 0U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_ACTION_CONTRACT,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 152, 0U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_MECHANISM_CONTRACT,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 156, 0U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_MECHANISM_CONTRACT,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	mutated[24] ^= 1U;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_HEADER_CRC,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	mutated[SEED_OFFSET] ^= 1U;
	memset(&header, 0xa5, sizeof(header));
	header_before = header;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_PAYLOAD_CRC,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	CHECK(memcmp(&header, &header_before, sizeof(header)) == 0);
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + NODE_OFFSET + 76U, 2U);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_STRING_POOL,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + LINK_OFFSET + 44U, 99U);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		DecodeFixture(mutated, sizeof(mutated), NULL, &decoded, &header));
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_FILE_SIZE,
		DecodeFixture(golden, sizeof(golden) - 1U, NULL, &decoded,
			&header));

	memset(&decoded, 0, sizeof(decoded));
	FixtureWorkspace(&decoded);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
		SG_RuneCodecDecode(golden, sizeof(golden), NULL, &header,
			decoded.seeds, TEST_SEEDS, decoded.links, TEST_LINKS,
			decoded.nodes, TEST_NODE_CAPACITY, decoded.edges,
			TEST_EDGE_CAPACITY, decoded.plans, TEST_PLANS,
			golden + STRING_OFFSET, TEST_STRING_BYTES,
			&decoded.workspace));
	memset(&decoded, 0, sizeof(decoded));
	FixtureWorkspace(&decoded);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
		SG_RuneCodecDecode(golden, sizeof(golden), NULL, &header,
			decoded.seeds, TEST_SEEDS, decoded.links, TEST_LINKS,
			decoded.nodes, TEST_NODE_CAPACITY, decoded.edges,
			TEST_EDGE_CAPACITY, decoded.plans, TEST_PLANS,
			(unsigned char *)(void *)decoded.nodes, TEST_STRING_BYTES,
			&decoded.workspace));
}

static void TestEmptyMechanismCompatibility(void)
{
	fixture_t fixture;
	static const unsigned char empty_strings[1] = { 0U };
	size_t size = 0U;

	FixtureInit(&fixture);
	fixture.links[0].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;
	fixture.links[0].action = RL_RUN;
	fixture.links[0].provenance = RL_PROVEN;
	SetVector(fixture.links[0].mechanism_anchor, 0.0f, 0.0f, 0.0f);
	fixture.links[0].sweep_clear_ms = 0U;
	fixture.links[0].mode = RLCM_NONE;
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecFileSize(TEST_SEEDS, TEST_LINKS,
		0U, 0U, 0U, 1U, &size));
	CHECK(size == SG_RUNE_CODEC_HEADER_BYTES +
		TEST_SEEDS * SG_RUNE_CODEC_SEED_BYTES +
		TEST_LINKS * SG_RUNE_CODEC_LINK_BYTES + 1U);
	FixtureWorkspace(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecValidate(fixture.seeds,
		TEST_SEEDS, fixture.links, TEST_LINKS, NULL, 0U, NULL, 0U,
		NULL, 0U, empty_strings, 1U, &fixture.workspace));
	fixture.links[0].action = RL_DOOR;
	fixture.links[0].provenance = RL_DECLARED;
	FixtureWorkspace(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_PLAN,
		SG_RuneCodecValidate(fixture.seeds, TEST_SEEDS, fixture.links,
			TEST_LINKS, NULL, 0U, NULL, 0U, NULL, 0U, empty_strings,
			1U, &fixture.workspace));
}

static sg_rune_codec_diagnostic_t ValidatePlanlessFixture(
	fixture_t *fixture)
{
	static const unsigned char empty_strings[1] = { 0U };

	FixtureWorkspace(fixture);
	return SG_RuneCodecValidate(fixture->seeds, TEST_SEEDS, fixture->links,
		TEST_LINKS, NULL, 0U, NULL, 0U, NULL, 0U, empty_strings, 1U,
		&fixture->workspace);
}

static void MakeRocketJumpLink(fixture_t *fixture)
{
	FixtureInit(fixture);
	fixture->links[0].action = RL_ROCKETJUMP;
	fixture->links[0].provenance = RL_PROVEN;
	fixture->links[0].cost_ms = 1800;
	fixture->links[0].min_speed = 0U;
	fixture->links[0].heading = 32U;
	fixture->links[0].heading_slack = 32U;
	fixture->links[0].exit_speed = 100U;
	SetVector(fixture->links[0].suffix_anchor,
		16384.0f, -8192.0f, 46.0f);
	SetVector(fixture->links[0].mechanism_anchor, 0.0f, 0.0f, 0.0f);
	fixture->links[0].sweep_clear_ms = 0U;
	fixture->links[0].mode = RLCM_NONE;
	fixture->links[0].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;
}

static void TestRocketJumpControlCodec(void)
{
	fixture_t fixture;

	MakeRocketJumpLink(&fixture);
	CHECK_DIAGNOSTIC(RLCODEC_OK, ValidatePlanlessFixture(&fixture));
	MakeRocketJumpLink(&fixture);
	fixture.links[0].suffix_anchor[0] += 0.5f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		ValidatePlanlessFixture(&fixture));
	MakeRocketJumpLink(&fixture);
	fixture.links[0].suffix_anchor[1] = 32768.0f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		ValidatePlanlessFixture(&fixture));
	MakeRocketJumpLink(&fixture);
	fixture.links[0].suffix_anchor[2] = 0.0f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		ValidatePlanlessFixture(&fixture));
	MakeRocketJumpLink(&fixture);
	fixture.links[0].suffix_anchor[2] = 101.0f;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		ValidatePlanlessFixture(&fixture));
	MakeRocketJumpLink(&fixture);
	fixture.links[0].min_speed = 1U;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_LINK_RECORD,
		ValidatePlanlessFixture(&fixture));
}

int main(void)
{
	TestPrimitiveGolden();
	TestWholeGolden();
	TestPrimitiveMalformed();
	TestGraphMalformed();
	TestWholeMalformed();
	TestEmptyMechanismCompatibility();
	TestRocketJumpControlCodec();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_codec_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_codec_test: PASS");
	return 0;
}
