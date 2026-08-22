/* Transaction, ownership, identity, and bounded-query tests for RUNE load. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_action.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_artifact_loader.h"

#define TEST_SEEDS 2U
#define TEST_LINKS 2U
#define TEST_NODES 2U
#define TEST_INVENTORY_EDGES 1U
#define TEST_EDGES 2U
#define TEST_PLANS 1U
#define TEST_STRING_BYTES 35U
#define TEST_FILE_BYTES 571U

#define TEST_SEED_OFFSET SG_RUNE_CODEC_HEADER_BYTES
#define TEST_LINK_OFFSET (TEST_SEED_OFFSET + \
	TEST_SEEDS * SG_RUNE_CODEC_SEED_BYTES)
#define TEST_NODE_OFFSET (TEST_LINK_OFFSET + \
	TEST_LINKS * SG_RUNE_CODEC_LINK_BYTES)
#define TEST_EDGE_OFFSET (TEST_NODE_OFFSET + \
	TEST_NODES * SG_RUNE_CODEC_ACTIVATION_NODE_BYTES)
#define TEST_PLAN_OFFSET (TEST_EDGE_OFFSET + \
	TEST_EDGES * SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES)
#define TEST_STRING_OFFSET (TEST_PLAN_OFFSET + \
	TEST_PLANS * SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES)
#define TEST_MAP_OFFSET 64U

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

typedef struct workspace_storage_s
{
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
	uint8_t string_marks[TEST_STRING_BYTES];
	sg_rune_codec_workspace_t workspace;
} workspace_storage_t;

typedef struct fixture_s
{
	sg_rune_codec_identity_t identity;
	sg_rune_codec_seed_t seeds[TEST_SEEDS];
	sg_rune_codec_link_t links[TEST_LINKS];
	sg_rune_codec_activation_node_t nodes[TEST_NODES];
	sg_rune_codec_activation_edge_t edges[TEST_EDGES];
	sg_rune_codec_activation_plan_t plans[TEST_PLANS];
	unsigned char strings[TEST_STRING_BYTES];
	workspace_storage_t encode_workspace;
} fixture_t;

typedef struct bank_s
{
	sg_rune_artifact_backing_t backing;
	sg_rune_codec_seed_t seeds[TEST_SEEDS];
	sg_rune_codec_link_t links[TEST_LINKS];
	sg_rune_codec_activation_node_t nodes[TEST_NODES];
	sg_rune_codec_activation_edge_t edges[TEST_EDGES];
	sg_rune_codec_activation_plan_t plans[TEST_PLANS];
	unsigned char strings[TEST_STRING_BYTES];
} bank_t;

static const unsigned char canonical_strings[TEST_STRING_BYTES] =
	"\0Door1\0door1\0func_button\0func_door";

static void PutU32(unsigned char *output, uint32_t value)
{
	output[0] = (unsigned char)(value & UINT32_C(0xff));
	output[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	output[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	output[3] = (unsigned char)(value >> 24);
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

static void WorkspaceInit(workspace_storage_t *storage)
{
	sg_rune_codec_workspace_t *workspace;

	memset(storage, 0, sizeof(*storage));
	workspace = &storage->workspace;
	workspace->graph_link_keys = storage->graph_link_keys;
	workspace->graph_link_key_capacity = TEST_LINKS;
	workspace->graph_source_marks = storage->graph_source_marks;
	workspace->graph_source_mark_capacity = TEST_SEEDS;
	workspace->plan_references = storage->plan_references;
	workspace->plan_reference_capacity = TEST_PLANS;
	workspace->node_references = storage->node_references;
	workspace->node_reference_capacity = TEST_NODES;
	workspace->node_heads = storage->node_heads;
	workspace->node_head_capacity = TEST_NODES;
	workspace->node_indegrees = storage->node_indegrees;
	workspace->node_indegree_capacity = TEST_NODES;
	workspace->node_generations = storage->node_generations;
	workspace->node_generation_capacity = TEST_NODES;
	workspace->node_touched = storage->node_touched;
	workspace->node_touched_capacity = TEST_NODES;
	workspace->node_queue = storage->node_queue;
	workspace->node_queue_capacity = TEST_NODES;
	workspace->edge_next = storage->edge_next;
	workspace->edge_next_capacity = TEST_EDGES;
	workspace->string_marks = storage->string_marks;
	workspace->string_mark_capacity = TEST_STRING_BYTES;
}

static void BankInit(bank_t *bank)
{
	memset(bank, 0, sizeof(*bank));
	bank->backing.seeds = bank->seeds;
	bank->backing.seed_capacity = TEST_SEEDS;
	bank->backing.links = bank->links;
	bank->backing.link_capacity = TEST_LINKS;
	bank->backing.nodes = bank->nodes;
	bank->backing.node_capacity = TEST_NODES;
	bank->backing.edges = bank->edges;
	bank->backing.edge_capacity = TEST_EDGES;
	bank->backing.plans = bank->plans;
	bank->backing.plan_capacity = TEST_PLANS;
	bank->backing.strings = bank->strings;
	bank->backing.string_capacity = TEST_STRING_BYTES;
}

static void FixtureInit(fixture_t *fixture)
{
	uint32_t closure_crc = 0U;

	memset(fixture, 0, sizeof(*fixture));
	WorkspaceInit(&fixture->encode_workspace);
	SetMap(fixture->identity.map_name, "active-test");
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
	fixture->links[1].activation_plan =
		SG_RUNE_CODEC_NO_ACTIVATION_PLAN;

	fixture->nodes[0].key = 1U;
	fixture->nodes[0].kind = SG_RUNE_CODEC_NODE_BUTTON;
	fixture->nodes[0].flags = SG_RUNE_CODEC_NODEF_REPEATABLE |
		SG_RUNE_CODEC_NODEF_TOUCHABLE | SG_RUNE_CODEC_NODEF_USABLE |
		SG_RUNE_CODEC_NODEF_MOVER;
	fixture->nodes[0].classname_offset = 13U;
	fixture->nodes[0].target_offset = 1U;
	fixture->nodes[0].owner_key = SG_RUNE_CODEC_NO_KEY;
	fixture->nodes[0].team_master_key = SG_RUNE_CODEC_NO_KEY;
	fixture->nodes[0].touch_callback =
		SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH;
	fixture->nodes[0].use_callback =
		SG_RUNE_CODEC_CALLBACK_BUTTON_USE;
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
	fixture->plans[0].num_edges = 1U;
	fixture->plans[0].controller_kind =
		SG_RUNE_CODEC_CONTROLLER_BUTTON_DOOR;
	fixture->plans[0].flags = SG_RUNE_CODEC_PLANF_TOUCH |
		SG_RUNE_CODEC_PLANF_ATOMIC | SG_RUNE_CODEC_PLANF_REQUIRES_LEASE;
	fixture->plans[0].expected_members = 1U;
	fixture->plans[0].cooldown_ms = 3000U;
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecPlanClosureCRC32(
		fixture->edges, fixture->plans[0].first_edge,
		fixture->plans[0].num_edges, TEST_EDGES, &closure_crc));
	fixture->plans[0].closure_crc32 = closure_crc;
	memcpy(fixture->strings, canonical_strings, TEST_STRING_BYTES);
}

static void EncodeFixture(fixture_t *fixture,
	unsigned char encoded[TEST_FILE_BYTES])
{
	size_t encoded_size = 0U;

	WorkspaceInit(&fixture->encode_workspace);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncode(&fixture->identity,
		fixture->seeds, TEST_SEEDS, fixture->links, TEST_LINKS,
		fixture->nodes, TEST_NODES, fixture->edges, TEST_EDGES,
		fixture->plans, TEST_PLANS, fixture->strings,
		TEST_STRING_BYTES, &fixture->encode_workspace.workspace,
		encoded, TEST_FILE_BYTES, &encoded_size));
	CHECK(encoded_size == TEST_FILE_BYTES);
}

static void FixHeaderCRC(unsigned char encoded[TEST_FILE_BYTES])
{
	uint32_t crc = 0U;

	PutU32(encoded + SG_RUNE_CODEC_HEADER_CRC_OFFSET, 0U);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecHeaderCRC32(encoded,
		SG_RUNE_CODEC_HEADER_BYTES, &crc));
	PutU32(encoded + SG_RUNE_CODEC_HEADER_CRC_OFFSET, crc);
}

static void FixPayloadCRC(unsigned char encoded[TEST_FILE_BYTES])
{
	uint32_t crc = 0U;

	CHECK(SG_CRC32Buffer(encoded + SG_RUNE_CODEC_HEADER_BYTES,
		TEST_FILE_BYTES - SG_RUNE_CODEC_HEADER_BYTES, &crc));
	PutU32(encoded + 20U, crc);
	FixHeaderCRC(encoded);
}

static void CheckPublicationUnchanged(
	const sg_rune_artifact_loader_t *loader,
	const sg_rune_artifact_loader_t *loader_before,
	const bank_t *bank, const bank_t *bank_before)
{
	CHECK(memcmp(loader, loader_before, sizeof(*loader)) == 0);
	CHECK(memcmp(bank, bank_before, sizeof(*bank)) == 0);
}

static void TestPublicationAndQueries(void)
{
	fixture_t fixture;
	bank_t bank;
	workspace_storage_t decode_workspace;
	sg_rune_artifact_loader_t loader;
	unsigned char encoded[TEST_FILE_BYTES];
	unsigned char malformed[TEST_FILE_BYTES];
	const sg_rune_codec_activation_node_t *node;
	const sg_rune_codec_activation_plan_t *plan;
	const unsigned char *string_value;

	FixtureInit(&fixture);
	BankInit(&bank);
	WorkspaceInit(&decode_workspace);
	EncodeFixture(&fixture, encoded);
	SG_RuneArtifactLoaderReset(&loader);
	CHECK(!SG_RuneArtifactLoaderIsPublished(&loader));
	CHECK(SG_RuneArtifactLoaderHeader(&loader) == NULL);
	memcpy(malformed, encoded, sizeof(malformed));
	malformed[TEST_SEED_OFFSET] ^= 1U;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_PAYLOAD_CRC,
		SG_RuneArtifactLoaderLoad(&loader, malformed, sizeof(malformed),
			&fixture.identity, &bank.backing,
			&decode_workspace.workspace));
	CHECK(!SG_RuneArtifactLoaderIsPublished(&loader));
	WorkspaceInit(&decode_workspace);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneArtifactLoaderLoad(&loader,
		encoded, sizeof(encoded), &fixture.identity, &bank.backing,
		&decode_workspace.workspace));
	CHECK(SG_RuneArtifactLoaderIsPublished(&loader));
	CHECK(SG_RuneArtifactLoaderHeader(&loader) == &loader.header);
	CHECK(loader.header.num_seeds == TEST_SEEDS);
	CHECK(loader.header.num_activation_plans == TEST_PLANS);
	CHECK(memcmp(loader.header.map_name, fixture.identity.map_name,
		SG_RUNE_CODEC_MAP_NAME_BYTES) == 0);
	CHECK(SG_RuneArtifactLoaderSeedAt(&loader, 0U) == &bank.seeds[0]);
	CHECK(SG_RuneArtifactLoaderSeedAt(&loader, TEST_SEEDS) == NULL);
	CHECK(SG_RuneArtifactLoaderLinkAt(&loader, 0U) == &bank.links[0]);
	CHECK(SG_RuneArtifactLoaderLinkAt(&loader, TEST_LINKS) == NULL);
	CHECK(SG_RuneArtifactLoaderNodeAt(&loader, 1U) == &bank.nodes[1]);
	CHECK(SG_RuneArtifactLoaderNodeAt(&loader, TEST_NODES) == NULL);
	CHECK(SG_RuneArtifactLoaderEdgeAt(&loader, 0U) == &bank.edges[0]);
	CHECK(SG_RuneArtifactLoaderEdgeAt(&loader, TEST_EDGES) == NULL);
	CHECK(SG_RuneArtifactLoaderPlanAt(&loader, 0U) == &bank.plans[0]);
	CHECK(SG_RuneArtifactLoaderPlanAt(&loader, TEST_PLANS) == NULL);
	node = SG_RuneArtifactLoaderNodeByKey(&loader, 2U);
	CHECK(node == &bank.nodes[1]);
	CHECK(node && node->kind == SG_RUNE_CODEC_NODE_DOOR_MASTER);
	CHECK(SG_RuneArtifactLoaderNodeByKey(&loader, 0U) == NULL);
	CHECK(SG_RuneArtifactLoaderNodeByKey(&loader, UINT32_MAX) == NULL);
	plan = SG_RuneArtifactLoaderPlanForLink(&loader, 0U);
	CHECK(plan == &bank.plans[0]);
	CHECK(plan && plan->entry_key == 1U && plan->mover_key == 2U);
	CHECK(SG_RuneArtifactLoaderPlanForLink(&loader, 1U) == NULL);
	CHECK(SG_RuneArtifactLoaderPlanForLink(&loader, TEST_LINKS) == NULL);
	string_value = SG_RuneArtifactLoaderStringAt(&loader, 13U);
	CHECK(string_value == bank.strings + 13U);
	CHECK(string_value && strcmp((const char *)string_value,
		"func_button") == 0);
	CHECK(SG_RuneArtifactLoaderStringAt(&loader, 2U) == NULL);
	CHECK(SG_RuneArtifactLoaderStringAt(&loader, TEST_STRING_BYTES) == NULL);
	CHECK(SG_ActionRuntimeSupported(RL_BUTTON_DOOR));

	/* Every published byte is independent of both the encoded snapshot and
	 * the caller's pre-encode native fixture. */
	encoded[TEST_MAP_OFFSET] = (unsigned char)'X';
	encoded[TEST_SEED_OFFSET] ^= 0xffU;
	encoded[TEST_LINK_OFFSET] ^= 0xffU;
	encoded[TEST_NODE_OFFSET] ^= 0xffU;
	encoded[TEST_EDGE_OFFSET] ^= 0xffU;
	encoded[TEST_PLAN_OFFSET] ^= 0xffU;
	encoded[TEST_STRING_OFFSET + 1U] = (unsigned char)'X';
	fixture.seeds[0].origin[0] = 999.0f;
	fixture.links[0].source = 1U;
	fixture.nodes[0].key = 99U;
	fixture.edges[0].from_key = 99U;
	fixture.plans[0].entry_key = 99U;
	fixture.strings[13] = (unsigned char)'X';
	CHECK(SG_RuneArtifactLoaderSeedAt(&loader, 0U)->origin[0] == 0.0f);
	CHECK(SG_RuneArtifactLoaderLinkAt(&loader, 0U)->source == 0U);
	CHECK(SG_RuneArtifactLoaderNodeAt(&loader, 0U)->key == 1U);
	CHECK(SG_RuneArtifactLoaderEdgeAt(&loader, 0U)->from_key == 1U);
	CHECK(SG_RuneArtifactLoaderPlanAt(&loader, 0U)->entry_key == 1U);
	CHECK(strcmp((const char *)SG_RuneArtifactLoaderStringAt(&loader, 13U),
		"func_button") == 0);
	CHECK(memcmp(loader.header.map_name, "active-test", 12U) == 0);
	CHECK(SG_ActionRuntimeSupported(RL_DOOR_SWIM));
	CHECK(SG_ActionMechanismAdmitted(RL_DOOR_SWIM));
	CHECK(!SG_ActionMechanismPlanRequired(RL_DOOR_SWIM));
}

static void TestRollbackCapacityAndAlias(void)
{
	struct
	{
		sg_rune_artifact_backing_t backing;
		unsigned char tail[TEST_FILE_BYTES];
	} source_backing_overlap;
	fixture_t fixture;
	bank_t bank_a;
	bank_t bank_b;
	bank_t bank_a_before;
	bank_t bank_b_before;
	workspace_storage_t workspace;
	workspace_storage_t alias_workspace;
	sg_rune_artifact_loader_t loader;
	sg_rune_artifact_loader_t loader_before;
	sg_rune_artifact_backing_t rejected_backing;
	sg_rune_codec_identity_t wrong_identity;
	unsigned char golden[TEST_FILE_BYTES];
	unsigned char mutated[TEST_FILE_BYTES];

	FixtureInit(&fixture);
	BankInit(&bank_a);
	BankInit(&bank_b);
	WorkspaceInit(&workspace);
	EncodeFixture(&fixture, golden);
	SG_RuneArtifactLoaderReset(&loader);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneArtifactLoaderLoad(&loader,
		golden, sizeof(golden), &fixture.identity, &bank_a.backing,
		&workspace.workspace));
	loader_before = loader;
	bank_a_before = bank_a;

	/* A payload failure cannot disturb the previous publication. */
	memcpy(mutated, golden, sizeof(mutated));
	mutated[TEST_SEED_OFFSET] ^= 1U;
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_PAYLOAD_CRC,
		SG_RuneArtifactLoaderLoad(&loader, mutated, sizeof(mutated),
			&fixture.identity, &bank_b.backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	/* A post-decode record rejection exercises partial candidate writes while
	 * the active bank remains byte-for-byte unchanged. */
	memcpy(mutated, golden, sizeof(mutated));
	mutated[TEST_NODE_OFFSET + 4U] = 0U;
	mutated[TEST_NODE_OFFSET + 5U] = 0U;
	FixPayloadCRC(mutated);
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC(RLCODEC_BAD_ACTIVATION_NODE,
		SG_RuneArtifactLoaderLoad(&loader, mutated, sizeof(mutated),
			&fixture.identity, &bank_b.backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	wrong_identity = fixture.identity;
	SetMap(wrong_identity.map_name, "wrongmap");
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_MAPNAME_MISMATCH,
		SG_RuneArtifactLoaderLoad(&loader, golden, sizeof(golden),
			&wrong_identity, &bank_b.backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	wrong_identity = fixture.identity;
	wrong_identity.gravity = 651.0f;
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_PHYSICS_LAW,
		SG_RuneArtifactLoaderLoad(&loader, golden, sizeof(golden),
			&wrong_identity, &bank_b.backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	rejected_backing = bank_b.backing;
	rejected_backing.node_capacity = TEST_NODES - 1U;
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_ALLOCATION_FAILED,
		SG_RuneArtifactLoaderLoad(&loader, golden, sizeof(golden),
			&fixture.identity, &rejected_backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	/* The source may never double as a decoded destination. */
	rejected_backing = bank_b.backing;
	rejected_backing.strings = mutated + TEST_STRING_OFFSET;
	rejected_backing.string_capacity = TEST_STRING_BYTES;
	memcpy(mutated, golden, sizeof(mutated));
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
		SG_RuneArtifactLoaderLoad(&loader, mutated, sizeof(mutated),
			&fixture.identity, &rejected_backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	/* The encoded source may not overlap even the caller's backing descriptor.
	 * Place the first source byte over the descriptor's final capacity byte;
	 * the resulting oversized capacity remains otherwise usable, so only the
	 * explicit descriptor/source disjointness law rejects this call. */
	source_backing_overlap.backing = bank_b.backing;
	memcpy((unsigned char *)&source_backing_overlap.backing +
		sizeof(source_backing_overlap.backing) - 1U,
		golden, sizeof(golden));
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
		SG_RuneArtifactLoaderLoad(&loader,
			(unsigned char *)&source_backing_overlap.backing +
				sizeof(source_backing_overlap.backing) - 1U,
			sizeof(golden), &fixture.identity,
			&source_backing_overlap.backing, &workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	/* Reloading into the active bank would let a later decode failure corrupt
	 * the current publication, so the loader rejects it before decoding. */
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
		SG_RuneArtifactLoaderLoad(&loader, golden, sizeof(golden),
			&fixture.identity, &bank_a.backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	/* Workspace is writable decode state and likewise cannot alias active
	 * publication storage. */
	WorkspaceInit(&alias_workspace);
	alias_workspace.workspace.string_marks = bank_a.strings;
	alias_workspace.workspace.string_mark_capacity = TEST_STRING_BYTES;
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
		SG_RuneArtifactLoaderLoad(&loader, golden, sizeof(golden),
			&fixture.identity, &bank_b.backing,
			&alias_workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_a, &bank_a_before);

	/* A valid inactive bank becomes the new immutable publication. */
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneArtifactLoaderLoad(&loader,
		golden, sizeof(golden), &fixture.identity, &bank_b.backing,
		&workspace.workspace));
	CHECK(SG_RuneArtifactLoaderSeedAt(&loader, 0U) == &bank_b.seeds[0]);
	bank_b_before = bank_b;
	loader_before = loader;
	bank_a.seeds[0].origin[0] = 777.0f;
	CHECK(SG_RuneArtifactLoaderSeedAt(&loader, 0U)->origin[0] == 0.0f);

	/* The now-inactive first bank is safe reload scratch; failure still
	 * preserves the second publication. */
	memcpy(mutated, golden, sizeof(mutated));
	mutated[TEST_SEED_OFFSET] ^= 1U;
	WorkspaceInit(&workspace);
	CHECK_DIAGNOSTIC((sg_rune_codec_diagnostic_t)RLW_BAD_PAYLOAD_CRC,
		SG_RuneArtifactLoaderLoad(&loader, mutated, sizeof(mutated),
			&fixture.identity, &bank_a.backing,
			&workspace.workspace));
	CheckPublicationUnchanged(&loader, &loader_before,
		&bank_b, &bank_b_before);

	SG_RuneArtifactLoaderReset(&loader);
	CHECK(!SG_RuneArtifactLoaderIsPublished(&loader));
	CHECK(SG_RuneArtifactLoaderHeader(&loader) == NULL);
	CHECK(SG_RuneArtifactLoaderSeedAt(&loader, 0U) == NULL);
	CHECK(SG_RuneArtifactLoaderNodeByKey(&loader, 1U) == NULL);
	CHECK(SG_RuneArtifactLoaderPlanForLink(&loader, 0U) == NULL);
	CHECK(SG_RuneArtifactLoaderStringAt(&loader, 0U) == NULL);
}

static void TestInventoryWithoutPlans(void)
{
	fixture_t fixture;
	bank_t bank;
	workspace_storage_t workspace;
	sg_rune_artifact_loader_t loader;
	unsigned char encoded[TEST_FILE_BYTES];
	size_t encoded_size = 0U;

	FixtureInit(&fixture);
	fixture.links[0].action = RL_RUN;
	fixture.links[0].provenance = RL_PROVEN;
	SetVector(fixture.links[0].mechanism_anchor, 0.0f, 0.0f, 0.0f);
	fixture.links[0].sweep_clear_ms = 0U;
	fixture.links[0].mode = RLCM_NONE;
	fixture.links[0].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;

	/* Inventory edges are independently publishable.  A zero plan count must
	 * neither require plan backing nor invalidate an otherwise complete
	 * inventory publication. */
	WorkspaceInit(&fixture.encode_workspace);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncode(&fixture.identity,
		fixture.seeds, TEST_SEEDS, fixture.links, TEST_LINKS,
		fixture.nodes, TEST_NODES, fixture.edges,
		TEST_INVENTORY_EDGES,
		NULL, 0U, fixture.strings, TEST_STRING_BYTES,
		&fixture.encode_workspace.workspace, encoded, sizeof(encoded),
		&encoded_size));
	CHECK(encoded_size < sizeof(encoded));
	BankInit(&bank);
	bank.backing.plans = NULL;
	bank.backing.plan_capacity = 0U;
	WorkspaceInit(&workspace);
	SG_RuneArtifactLoaderReset(&loader);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneArtifactLoaderLoad(&loader, encoded,
		encoded_size, &fixture.identity, &bank.backing,
		&workspace.workspace));
	CHECK(SG_RuneArtifactLoaderIsPublished(&loader));
	CHECK(SG_RuneArtifactLoaderNodeAt(&loader, 0U) == &bank.nodes[0]);
	CHECK(SG_RuneArtifactLoaderEdgeAt(&loader, 0U) == &bank.edges[0]);
	CHECK(SG_RuneArtifactLoaderPlanAt(&loader, 0U) == NULL);

	/* Node-only inventories likewise publish without dummy edge or plan
	 * storage.  Each backing pointer is governed solely by its own count. */
	WorkspaceInit(&fixture.encode_workspace);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneCodecEncode(&fixture.identity,
		fixture.seeds, TEST_SEEDS, fixture.links, TEST_LINKS,
		fixture.nodes, TEST_NODES, NULL, 0U, NULL, 0U,
		fixture.strings, TEST_STRING_BYTES,
		&fixture.encode_workspace.workspace, encoded, sizeof(encoded),
		&encoded_size));
	BankInit(&bank);
	bank.backing.edges = NULL;
	bank.backing.edge_capacity = 0U;
	bank.backing.plans = NULL;
	bank.backing.plan_capacity = 0U;
	WorkspaceInit(&workspace);
	SG_RuneArtifactLoaderReset(&loader);
	CHECK_DIAGNOSTIC(RLCODEC_OK, SG_RuneArtifactLoaderLoad(&loader, encoded,
		encoded_size, &fixture.identity, &bank.backing,
		&workspace.workspace));
	CHECK(SG_RuneArtifactLoaderIsPublished(&loader));
	CHECK(SG_RuneArtifactLoaderNodeAt(&loader, 1U) == &bank.nodes[1]);
	CHECK(SG_RuneArtifactLoaderEdgeAt(&loader, 0U) == NULL);
	CHECK(SG_RuneArtifactLoaderPlanAt(&loader, 0U) == NULL);
}

int main(void)
{
	TestPublicationAndQueries();
	TestRollbackCapacityAndAlias();
	TestInventoryWithoutPlans();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_artifact_loader_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_artifact_loader_test: PASS");
	return 0;
}
