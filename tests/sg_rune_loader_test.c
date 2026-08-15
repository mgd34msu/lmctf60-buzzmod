/* Authentication, semantic, mapping, and boundary tests for RUNE v3 load. */
#include "q_shared.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_loader.h"

#define TEST_SEEDS 8U
#define TEST_LINKS 8U
#define TEST_BYTES (SG_RUNE_V3_HEADER_BYTES + \
	TEST_SEEDS * SG_RUNE_V3_SEED_BYTES + \
	TEST_LINKS * SG_RUNE_V3_LINK_BYTES)
#define SEED_OFFSET(index_) (SG_RUNE_V3_HEADER_BYTES + \
	(size_t)(index_) * SG_RUNE_V3_SEED_BYTES)
#define LINK_OFFSET(index_) (SG_RUNE_V3_HEADER_BYTES + \
	TEST_SEEDS * SG_RUNE_V3_SEED_BYTES + \
	(size_t)(index_) * SG_RUNE_V3_LINK_BYTES)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_RESULT(result_, diagnostic_, reason_, stage_, index_) do { \
	sg_rune_load_result_t checked_ = (result_); \
	if (checked_.diagnostic != (diagnostic_) || \
	    checked_.reason != (reason_) || checked_.stage != (stage_) || \
	    checked_.index != (index_)) { \
		fprintf(stderr, "%s:%d: result got d=%d r=%d s=%d i=%u; " \
		        "expected d=%d r=%d s=%d i=%u\n", __FILE__, __LINE__, \
		        (int)checked_.diagnostic, (int)checked_.reason, \
		        (int)checked_.stage, checked_.index, (int)(diagnostic_), \
		        (int)(reason_), (int)(stage_), (uint32_t)(index_)); \
		failures++; \
	} \
} while (0)

static void PutU16(unsigned char *output, uint16_t value)
{
	output[0] = (unsigned char)(value & UINT16_C(0xff));
	output[1] = (unsigned char)(value >> 8);
}

static void PutU32(unsigned char *output, uint32_t value)
{
	output[0] = (unsigned char)(value & UINT32_C(0xff));
	output[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	output[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	output[3] = (unsigned char)(value >> 24);
}

static void SetMap(char output[SG_RUNE_V3_MAP_NAME_BYTES],
	const char *value)
{
	size_t length = strlen(value);

	memset(output, 0, SG_RUNE_V3_MAP_NAME_BYTES);
	CHECK(length < SG_RUNE_V3_MAP_NAME_BYTES);
	if (length < SG_RUNE_V3_MAP_NAME_BYTES)
		memcpy(output, value, length);
}

static void SetVector(float output[3], float x, float y, float z)
{
	output[0] = x;
	output[1] = y;
	output[2] = z;
}

static sg_rune_v3_identity_t Identity(float gravity)
{
	sg_rune_v3_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	SetMap(identity.map_name, gravity == 650.0f ? "lmctf07" : "lmctf03");
	identity.bsp_checksum = UINT32_C(0x12345678);
	identity.entity_crc32 = UINT32_C(0x9abcdef0);
	identity.gravity = gravity;
	identity.airaccelerate = 0.0f;
	identity.maxvelocity = 2000.0f;
	identity.pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	identity.server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	identity.host_physics_id = 1;
	return identity;
}

static sg_rune_v3_workspace_t GraphWorkspace(uint64_t *keys,
	size_t key_count, uint8_t *marks, size_t mark_count)
{
	sg_rune_v3_workspace_t workspace;

	workspace.link_keys = keys;
	workspace.link_key_capacity = key_count;
	workspace.source_marks = marks;
	workspace.source_mark_capacity = mark_count;
	return workspace;
}

static sg_rune_v3_loader_workspace_t LoaderWorkspace(
	sg_rune_v3_seed_t *seeds, size_t seed_count,
	sg_rune_v3_link_t *links, size_t link_count,
	uint64_t *keys, uint8_t *marks)
{
	sg_rune_v3_loader_workspace_t workspace;

	workspace.wire_seeds = seeds;
	workspace.wire_seed_capacity = seed_count;
	workspace.wire_links = links;
	workspace.wire_link_capacity = link_count;
	workspace.graph = GraphWorkspace(keys, link_count, marks, seed_count);
	return workspace;
}

static void DeclaredLink(sg_rune_v3_link_t *link, int action,
	uint32_t source, uint32_t destination, float x)
{
	memset(link, 0, sizeof(*link));
	link->source = source;
	link->destination = destination;
	link->action = (uint8_t)action;
	link->provenance = RL_DECLARED;
	link->heading_slack = SG_RUNE_PROOF_DECLARED_CONTROL_MARKER;
	link->cost_ms = 300;
	SetVector(link->suffix_anchor, x, 0.0f, 0.0f);
}

static void AllActionsGraph(sg_rune_v3_seed_t seeds[TEST_SEEDS],
	sg_rune_v3_link_t links[TEST_LINKS])
{
	uint32_t index;

	memset(seeds, 0, sizeof(*seeds) * TEST_SEEDS);
	memset(links, 0, sizeof(*links) * TEST_LINKS);
	for (index = 0; index < TEST_SEEDS; index++)
	{
		SetVector(seeds[index].origin, (float)index * 64.0f,
			0.0f, 0.0f);
		seeds[index].area_hint = (int16_t)index;
	}
	seeds[5].flags = SG_RUNE_V3_SEED_WATER;

	links[0].source = 0;
	links[0].destination = 1;
	links[0].action = RL_RUN;
	links[0].provenance = RL_PROVEN;
	links[0].min_speed = 4;
	links[0].heading = 64;
	links[0].heading_slack = 8;
	links[0].exit_speed = 50;
	links[0].cost_ms = 125;
	SetVector(links[0].suffix_anchor, 32.0f, 16.0f, 0.0f);

	links[1].source = 1;
	links[1].destination = 2;
	links[1].action = RL_JUMP;
	links[1].provenance = RL_PROVEN;
	links[1].heading = 0;
	links[1].heading_slack = 8;
	links[1].exit_speed = 30;
	links[1].cost_ms = 200;

	links[2].source = 2;
	links[2].destination = 3;
	links[2].action = RL_DROP;
	links[2].provenance = RL_PROVEN;
	links[2].heading = 0;
	links[2].heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	links[2].exit_speed = 20;
	links[2].cost_ms = 250;
	SetVector(links[2].suffix_anchor, 192.0f, 0.0f, 8.0f);

	links[3].source = 3;
	links[3].destination = 4;
	links[3].action = RL_HOOK;
	links[3].provenance = RL_PROVEN;
	links[3].heading_slack = SG_RUNE_PROOF_HOOK_CONTROL_SLACK;
	links[3].cost_ms = 500;
	SetVector(links[3].suffix_anchor, 0.0f, 0.0f, 64.0f);

	links[4].source = 4;
	links[4].destination = 5;
	links[4].action = RL_SWIM;
	links[4].provenance = RL_PROVEN;
	links[4].cost_ms = 400;

	DeclaredLink(&links[5], RL_LIFT, 5, 6, 352.0f);
	DeclaredLink(&links[6], RL_TELEPORT, 6, 7, 400.0f);
	DeclaredLink(&links[7], RL_DOOR, 7, 0, 256.0f);
}

static int EncodeGraph(const sg_rune_v3_identity_t *identity,
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *links, uint32_t num_links,
	unsigned char *encoded, size_t encoded_capacity, size_t *encoded_size)
{
	uint64_t *keys = NULL;
	uint8_t *marks = NULL;
	sg_rune_v3_workspace_t workspace;
	rune_wire_diagnostic_t diagnostic;

	keys = (uint64_t *)malloc((size_t)(num_links ? num_links : 1U) *
		sizeof(*keys));
	marks = (uint8_t *)malloc((size_t)num_seeds);
	if (!keys || !marks)
	{
		free(keys);
		free(marks);
		return 0;
	}
	workspace = GraphWorkspace(keys, num_links, marks, num_seeds);
	diagnostic = SG_RuneV3Encode(identity, seeds, num_seeds, links,
		num_links, &workspace, encoded, encoded_capacity, encoded_size);
	free(keys);
	free(marks);
	return diagnostic == RLW_OK;
}

static int EncodeBase(float gravity, unsigned char encoded[TEST_BYTES],
	sg_rune_v3_identity_t *identity_out)
{
	sg_rune_v3_seed_t seeds[TEST_SEEDS];
	sg_rune_v3_link_t links[TEST_LINKS];
	size_t encoded_size = 0;

	*identity_out = Identity(gravity);
	AllActionsGraph(seeds, links);
	return EncodeGraph(identity_out, seeds, TEST_SEEDS, links, TEST_LINKS,
		encoded, TEST_BYTES, &encoded_size) && encoded_size == TEST_BYTES;
}

static void FixHeaderCRC(unsigned char *encoded)
{
	uint32_t crc = 0;

	PutU32(encoded + SG_RUNE_V3_HEADER_CRC_OFFSET, 0);
	CHECK(SG_RuneV3HeaderCRC32(encoded, SG_RUNE_V3_HEADER_BYTES,
		&crc) == RLW_OK);
	PutU32(encoded + SG_RUNE_V3_HEADER_CRC_OFFSET, crc);
}

static void FixPayloadCRC(unsigned char *encoded, size_t encoded_size)
{
	uint32_t crc = 0;

	CHECK(SG_CRC32Buffer(encoded + SG_RUNE_V3_HEADER_BYTES,
		encoded_size - SG_RUNE_V3_HEADER_BYTES, &crc));
	PutU32(encoded + 20, crc);
	FixHeaderCRC(encoded);
}

static sg_rune_load_result_t LoadSmall(const unsigned char *encoded,
	size_t encoded_size, const sg_rune_v3_identity_t *identity,
	sg_rune_v3_header_t *header, rune_seed_t seeds[TEST_SEEDS],
	rune_link_t links[TEST_LINKS])
{
	sg_rune_v3_seed_t wire_seeds[TEST_SEEDS];
	sg_rune_v3_link_t wire_links[TEST_LINKS];
	uint64_t keys[TEST_LINKS];
	uint8_t marks[TEST_SEEDS];
	sg_rune_v3_loader_workspace_t workspace = LoaderWorkspace(wire_seeds,
		TEST_SEEDS, wire_links, TEST_LINKS, keys, marks);

	return SG_RuneV3Load(encoded, encoded_size, identity, header,
		seeds, TEST_SEEDS, links, TEST_LINKS, &workspace);
}

static void TestGoldenAndMapping(void)
{
	static const float gravities[2] = { 800.0f, 650.0f };
	unsigned int which;

	for (which = 0; which < 2U; which++)
	{
		unsigned char encoded[TEST_BYTES];
		sg_rune_v3_identity_t identity;
		sg_rune_v3_header_t inspected;
		sg_rune_v3_header_t decoded;
		sg_rune_v3_header_t header;
		sg_rune_v3_seed_t wire_seeds[TEST_SEEDS];
		sg_rune_v3_link_t wire_links[TEST_LINKS];
		rune_seed_t native_seeds[TEST_SEEDS];
		rune_link_t native_links[TEST_LINKS];
		uint64_t keys[TEST_LINKS];
		uint8_t marks[TEST_SEEDS];
		sg_rune_v3_loader_workspace_t workspace = LoaderWorkspace(
			wire_seeds, TEST_SEEDS, wire_links, TEST_LINKS, keys, marks);
		sg_rune_load_result_t result;
		uint32_t index;

		CHECK(EncodeBase(gravities[which], encoded, &identity));
		CHECK(SG_RuneV3Probe(encoded, sizeof(encoded)) ==
			SG_RUNE_SNAPSHOT_V3);
		result = SG_RuneV3InspectHeader(encoded,
			SG_RUNE_V3_HEADER_BYTES, &identity, &inspected);
		CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_LOAD_STAGE_DONE,
			SG_RUNE_LOAD_INDEX_NONE);
		CHECK(result.file_size == TEST_BYTES);
		result = SG_RuneV3Inspect(encoded, sizeof(encoded), &identity,
			&inspected);
		CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_LOAD_STAGE_DONE,
			SG_RUNE_LOAD_INDEX_NONE);
		CHECK(result.file_size == TEST_BYTES);
		CHECK(inspected.gravity == gravities[which]);
		CHECK(SG_RuneV3DecodeHeader(encoded, SG_RUNE_V3_HEADER_BYTES,
			&decoded) == RLW_OK);
		memset(&header, 0xa5, sizeof(header));
		result = SG_RuneV3Load(encoded, sizeof(encoded), &identity, &header,
			native_seeds, TEST_SEEDS, native_links, TEST_LINKS,
			&workspace);
		CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_LOAD_STAGE_DONE,
			SG_RUNE_LOAD_INDEX_NONE);
		CHECK(memcmp(&decoded, &header, sizeof(header)) == 0);
		for (index = 0; index < TEST_SEEDS; index++)
		{
			CHECK(memcmp(native_seeds[index].origin,
				wire_seeds[index].origin,
				sizeof(native_seeds[index].origin)) == 0);
			CHECK(native_seeds[index].area_hint ==
				wire_seeds[index].area_hint);
			CHECK(native_seeds[index].flags == wire_seeds[index].flags);
		}
		for (index = 0; index < TEST_LINKS; index++)
		{
			CHECK(native_links[index].action ==
				(index == 7U ? RL_DOOR : index));
			CHECK(native_links[index].from ==
				(int)wire_links[index].source);
			CHECK(native_links[index].to ==
				(int)wire_links[index].destination);
			CHECK(native_links[index].provenance ==
				wire_links[index].provenance);
			CHECK(native_links[index].min_speed ==
				wire_links[index].min_speed);
			CHECK(native_links[index].heading ==
				wire_links[index].heading);
			CHECK(native_links[index].heading_slack ==
				wire_links[index].heading_slack);
			CHECK(native_links[index].exit_speed ==
				wire_links[index].exit_speed);
			CHECK(native_links[index].cost_ms ==
				wire_links[index].cost_ms);
			CHECK(memcmp(native_links[index].anchor,
				wire_links[index].suffix_anchor,
				sizeof(native_links[index].anchor)) == 0);
		}
		CHECK(native_links[7].action == RL_DOOR);
	}
}

static void TestProbeInspectIdentity(void)
{
	unsigned char encoded[TEST_BYTES];
	unsigned char mutated[TEST_BYTES + 1U];
	unsigned char v2[8] = { 0 };
	sg_rune_v3_identity_t identity;
	sg_rune_v3_identity_t wrong;
	sg_rune_v3_header_t header;
	sg_rune_v3_header_t sentinel;
	sg_rune_load_result_t result;

	CHECK(EncodeBase(650.0f, encoded, &identity));
	PutU32(v2, SG_RUNE_V3_MAGIC);
	PutU32(v2 + 4, RUNE_VERSION);
	CHECK(SG_RuneV3Probe(v2, sizeof(v2)) == SG_RUNE_SNAPSHOT_V2);
	CHECK(SG_RuneV3Probe(v2, sizeof(v2) - 1U) ==
		SG_RUNE_SNAPSHOT_UNKNOWN);
	CHECK(SG_RuneV3Probe(NULL, 0) == SG_RUNE_SNAPSHOT_UNKNOWN);
	memset(&sentinel, 0xa5, sizeof(sentinel));
	header = sentinel;
	result = SG_RuneV3Inspect(v2, sizeof(v2), &identity, &header);
	CHECK_RESULT(result, RLW_UNSUPPORTED_VERSION, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	CHECK(memcmp(&header, &sentinel, sizeof(header)) == 0);
	result = SG_RuneV3InspectHeader(v2, sizeof(v2), &identity, &header);
	CHECK_RESULT(result, RLW_UNSUPPORTED_VERSION, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	result = SG_RuneV3InspectHeader(encoded,
		SG_RUNE_V3_HEADER_BYTES - 1U, &identity, &header);
	CHECK_RESULT(result, RLW_BAD_HEADER_SIZE, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	result = SG_RuneV3Inspect(encoded, SG_RUNE_V3_HEADER_BYTES - 1U,
		&identity, &header);
	CHECK_RESULT(result, RLW_BAD_FILE_SIZE, RLR_OK,
		SG_RUNE_LOAD_STAGE_FILE_SIZE, SG_RUNE_LOAD_INDEX_NONE);
	result = SG_RuneV3Inspect(encoded, sizeof(encoded) - 1U, &identity,
		&header);
	CHECK_RESULT(result, RLW_BAD_FILE_SIZE, RLR_OK,
		SG_RUNE_LOAD_STAGE_FILE_SIZE, SG_RUNE_LOAD_INDEX_NONE);
	memcpy(mutated, encoded, sizeof(encoded));
	mutated[sizeof(encoded)] = 0;
	result = SG_RuneV3Inspect(mutated, sizeof(mutated), &identity, &header);
	CHECK_RESULT(result, RLW_BAD_FILE_SIZE, RLR_OK,
		SG_RUNE_LOAD_STAGE_FILE_SIZE, SG_RUNE_LOAD_INDEX_NONE);
	memcpy(mutated, encoded, sizeof(encoded));
	mutated[SEED_OFFSET(0)] ^= 1U;
	result = SG_RuneV3Inspect(mutated, sizeof(encoded), &identity, &header);
	CHECK_RESULT(result, RLW_BAD_PAYLOAD_CRC, RLR_OK,
		SG_RUNE_LOAD_STAGE_PAYLOAD_CRC, SG_RUNE_LOAD_INDEX_NONE);

	wrong = identity;
	wrong.map_name[6] = '8';
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_MAPNAME_MISMATCH, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.bsp_checksum++;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BSP_CHECKSUM_MISMATCH, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.entity_crc32++;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_ENTITY_CRC_MISMATCH, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.host_physics_id++;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_PHYSICS_ID_MISMATCH, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.gravity = 800.0f;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BAD_PHYSICS_LAW, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.maxvelocity = 1900.0f;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BAD_PHYSICS_LAW, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.airaccelerate = 1.0f;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BAD_PHYSICS_LAW, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.physics_flags = 1;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BAD_PHYSICS_LAW, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.pmove_substep_ms++;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BAD_PHYSICS_LAW, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	wrong = identity;
	wrong.server_frame_ms++;
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &wrong, &header),
		RLW_BAD_PHYSICS_LAW, RLR_OK, SG_RUNE_LOAD_STAGE_IDENTITY,
		SG_RUNE_LOAD_INDEX_NONE);
	CHECK_RESULT(SG_RuneV3Inspect(NULL, 0, &identity, &header),
		RLW_INVALID_ARGUMENT, RLR_OK, SG_RUNE_LOAD_STAGE_ARGUMENT,
		SG_RUNE_LOAD_INDEX_NONE);
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), NULL, &header),
		RLW_INVALID_ARGUMENT, RLR_OK, SG_RUNE_LOAD_STAGE_ARGUMENT,
		SG_RUNE_LOAD_INDEX_NONE);
	CHECK_RESULT(SG_RuneV3Inspect(encoded, sizeof(encoded), &identity, NULL),
		RLW_INVALID_ARGUMENT, RLR_OK, SG_RUNE_LOAD_STAGE_ARGUMENT,
		SG_RUNE_LOAD_INDEX_NONE);
}

static void TestLiteralControlLaws(void)
{
	sg_rune_v3_seed_t seeds[TEST_SEEDS];
	sg_rune_v3_link_t links[TEST_LINKS];
	sg_rune_v3_link_t changed;
	uint32_t index;

	AllActionsGraph(seeds, links);
	for (index = 0; index < TEST_LINKS; index++)
		CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS,
			&links[index]) == RLR_OK);
	changed = links[0];
	changed.action = RL_ROCKETJUMP;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_ACTION_DISABLED);
	changed.action = RL_DOOR_DROP;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_ACTION_DISABLED);
	changed.action = UINT8_MAX;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_UNKNOWN_ACTION);
	changed = links[0];
	changed.mechanism_anchor[0] = -0.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_NONZERO_TAIL);
	changed = links[0];
	changed.sweep_clear_ms = 100;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_NONZERO_TAIL);
	changed = links[0];
	changed.mode = RLCM_PREOPEN;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_MODE);
	changed = links[0];
	changed.reserved = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_NONZERO_RESERVED);

	changed = links[0];
	changed.suffix_anchor[0] = 5000.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_RUN_CONTROL);
	changed = links[1];
	changed.min_speed = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_JUMP_CONTROL);
	changed = links[1];
	changed.suffix_anchor[0] = 1.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_JUMP_CONTROL);
	changed = links[2];
	changed.heading_slack = 0;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DROP_CONTROL);
	changed = links[2];
	changed.min_speed = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DROP_CONTROL);
	changed = links[2];
	changed.suffix_anchor[0] = seeds[2].origin[0] + 1.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DROP_CONTROL);
	changed = links[2];
	changed.suffix_anchor[2] = 9.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DROP_CONTROL);
	changed = links[2];
	changed.heading = 64;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DROP_CONTROL);
	changed = links[2];
	changed.provenance = RL_OBSERVED;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_PROVENANCE_FORBIDDEN);
	changed.provenance = RL_ADJUSTED;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_PROVENANCE_FORBIDDEN);
	changed.provenance = RL_DECLARED;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_PROVENANCE_FORBIDDEN);

	changed = links[3];
	changed.min_speed = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_HOOK_CONTROL);
	changed = links[3];
	changed.heading_slack = 0;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_HOOK_CONTROL);
	changed = links[3];
	changed.suffix_anchor[PITCH] = 0.1f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_HOOK_CONTROL);
	changed = links[3];
	changed.suffix_anchor[PITCH] = 90.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_HOOK_CONTROL);
	changed = links[3];
	changed.suffix_anchor[ROLL] = 0.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_HOOK_CONTROL);

	changed = links[4];
	changed.min_speed = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_SWIM_CONTROL);
	changed = links[4];
	changed.heading = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_SWIM_CONTROL);
	changed = links[4];
	changed.heading_slack = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_SWIM_CONTROL);
	changed = links[4];
	changed.suffix_anchor[0] = 1.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_SWIM_CONTROL);
	changed = links[4];
	changed.provenance = RL_ADJUSTED;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_PROVENANCE_FORBIDDEN);

	changed = links[5];
	changed.min_speed = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DECLARED_CONTROL);
	changed = links[5];
	changed.heading = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DECLARED_CONTROL);
	changed = links[5];
	changed.heading_slack = 0;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DECLARED_CONTROL);
	changed = links[5];
	changed.exit_speed = 1;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DECLARED_CONTROL);
	changed = links[6];
	changed.suffix_anchor[0] = 600.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_TELEPORT_REACH);
	changed = links[7];
	changed.suffix_anchor[0] = 0.1f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_ANCHOR_POLICY);
	changed = links[7];
	changed.suffix_anchor[0] = 64.0f;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_DOOR_REACH);
	changed = links[0];
	changed.destination = 5;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_ENDPOINT_POLICY);
	changed = links[0];
	changed.destination = changed.source;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_SELF_LINK);
	changed.destination = TEST_SEEDS;
	CHECK(SG_RuneV3ValidateLiteralLink(seeds, TEST_SEEDS, &changed) ==
		RLR_BAD_INDEX);
	CHECK(SG_RuneV3ValidateLiteralLink(NULL, TEST_SEEDS, &links[0]) ==
		RLR_BAD_CONTROL_POLICY);
}

static void ExpectEncodedControlFailure(uint32_t link_index,
	rune_reject_reason_t expected_reason, int mutation)
{
	sg_rune_v3_identity_t identity = Identity(800.0f);
	sg_rune_v3_seed_t seeds[TEST_SEEDS];
	sg_rune_v3_link_t links[TEST_LINKS];
	unsigned char encoded[TEST_BYTES];
	size_t encoded_size = 0;
	sg_rune_v3_header_t header;
	rune_seed_t native_seeds[TEST_SEEDS];
	rune_link_t native_links[TEST_LINKS];

	AllActionsGraph(seeds, links);
	switch (mutation)
	{
	case 0: links[link_index].min_speed = 1; break;
	case 1: links[link_index].heading_slack = 0; break;
	case 2: links[link_index].heading = 1; break;
	case 3: links[link_index].suffix_anchor[0] = 600.0f; break;
	case 4: links[link_index].suffix_anchor[0] = 64.0f; break;
	default: CHECK(0); return;
	}
	CHECK(EncodeGraph(&identity, seeds, TEST_SEEDS, links, TEST_LINKS,
		encoded, sizeof(encoded), &encoded_size));
	CHECK(encoded_size == sizeof(encoded));
	CHECK_RESULT(LoadSmall(encoded, encoded_size, &identity, &header,
		native_seeds, native_links), RLW_BAD_LINK_RECORD, expected_reason,
		SG_RUNE_LOAD_STAGE_CONTROL, link_index);
}

static void TestEncodedControlFailures(void)
{
	ExpectEncodedControlFailure(1, RLR_BAD_JUMP_CONTROL, 0);
	ExpectEncodedControlFailure(2, RLR_BAD_DROP_CONTROL, 1);
	ExpectEncodedControlFailure(3, RLR_BAD_HOOK_CONTROL, 0);
	ExpectEncodedControlFailure(4, RLR_BAD_SWIM_CONTROL, 2);
	ExpectEncodedControlFailure(5, RLR_BAD_DECLARED_CONTROL, 0);
	ExpectEncodedControlFailure(6, RLR_BAD_TELEPORT_REACH, 3);
	ExpectEncodedControlFailure(7, RLR_BAD_DOOR_REACH, 4);
}

static void ExpectPatchedFailure(unsigned char encoded[TEST_BYTES],
	const sg_rune_v3_identity_t *identity, rune_wire_diagnostic_t diagnostic,
	rune_reject_reason_t reason, sg_rune_load_stage_t stage, uint32_t index)
{
	sg_rune_v3_header_t header;
	sg_rune_v3_header_t sentinel_header;
	rune_seed_t seeds[TEST_SEEDS];
	rune_seed_t sentinel_seeds[TEST_SEEDS];
	rune_link_t links[TEST_LINKS];
	rune_link_t sentinel_links[TEST_LINKS];

	memset(&header, 0xa5, sizeof(header));
	sentinel_header = header;
	memset(seeds, 0x5a, sizeof(seeds));
	memcpy(sentinel_seeds, seeds, sizeof(seeds));
	memset(links, 0x3c, sizeof(links));
	memcpy(sentinel_links, links, sizeof(links));
	CHECK_RESULT(LoadSmall(encoded, TEST_BYTES, identity, &header,
		seeds, links), diagnostic, reason, stage, index);
	CHECK(memcmp(&header, &sentinel_header, sizeof(header)) == 0);
	CHECK(memcmp(seeds, sentinel_seeds, sizeof(seeds)) == 0);
	CHECK(memcmp(links, sentinel_links, sizeof(links)) == 0);
}

static void TestActionAndStructuralFailures(void)
{
	unsigned char base[TEST_BYTES];
	unsigned char changed[TEST_BYTES];
	sg_rune_v3_identity_t identity;

	CHECK(EncodeBase(800.0f, base, &identity));
	memcpy(changed, base, sizeof(changed));
	changed[24] ^= 1U;
	ExpectPatchedFailure(changed, &identity, RLW_BAD_HEADER_CRC, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	memcpy(changed, base, sizeof(changed));
	PutU16(changed + 4, 4);
	FixHeaderCRC(changed);
	ExpectPatchedFailure(changed, &identity, RLW_UNSUPPORTED_VERSION, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	memcpy(changed, base, sizeof(changed));
	PutU16(changed + 8, SG_RUNE_V3_SEED_BYTES - 1U);
	FixHeaderCRC(changed);
	ExpectPatchedFailure(changed, &identity, RLW_BAD_SEED_SIZE, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	memcpy(changed, base, sizeof(changed));
	PutU16(changed + 10, SG_RUNE_V3_LINK_BYTES - 1U);
	FixHeaderCRC(changed);
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_SIZE, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
	memcpy(changed, base, sizeof(changed));
	changed[LINK_OFFSET(0) + 8U] = UINT8_C(12);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_UNKNOWN_ACTION, SG_RUNE_LOAD_STAGE_ACTION, 0);
	memcpy(changed, base, sizeof(changed));
	changed[LINK_OFFSET(0) + 8U] = RL_ROCKETJUMP;
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_ACTION_DISABLED, SG_RUNE_LOAD_STAGE_ACTION, 0);

	/* Dense generated actions admit generation-time proof only. */
	{
		static const uint8_t drop_unproved[] =
		{
			RL_OBSERVED, RL_ADJUSTED, RL_DECLARED
		};
		size_t provenance_index;

		for (provenance_index = 0;
		     provenance_index < sizeof(drop_unproved) /
		         sizeof(drop_unproved[0]); provenance_index++)
		{
			memcpy(changed, base, sizeof(changed));
			changed[LINK_OFFSET(2) + 9U] =
				drop_unproved[provenance_index];
			FixPayloadCRC(changed, sizeof(changed));
			ExpectPatchedFailure(changed, &identity,
				RLW_BAD_LINK_RECORD, RLR_PROVENANCE_FORBIDDEN,
				SG_RUNE_LOAD_STAGE_LINK, 2);
		}
		memcpy(changed, base, sizeof(changed));
		changed[LINK_OFFSET(4) + 9U] = RL_ADJUSTED;
		FixPayloadCRC(changed, sizeof(changed));
		ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
			RLR_PROVENANCE_FORBIDDEN, SG_RUNE_LOAD_STAGE_LINK, 4);
	}

	/* Structurally valid compound records must still fail the literal gate. */
	{
		sg_rune_v3_seed_t seeds[TEST_SEEDS];
		sg_rune_v3_link_t links[TEST_LINKS];
		unsigned int action;
		size_t size;

		for (action = RL_DOOR_DROP; action <= RL_DOOR_HOOK; action++)
		{
			uint32_t target = action == RL_DOOR_DROP ? 2U : 5U;
			sg_rune_v3_link_t *compound;

			AllActionsGraph(seeds, links);
			compound = &links[target];
			compound->action = (uint8_t)action;
			compound->provenance = RL_CONTRACTED;
			compound->mode = RLCM_PREOPEN;
			compound->sweep_clear_ms = 100;
			compound->cost_ms = 500;
			if (action == RL_DOOR_DROP)
			{
				compound->heading = 0;
				compound->heading_slack =
					SG_RUNE_PROOF_DROP_CONTROL_MARKER;
				SetVector(compound->mechanism_anchor,
					128.0f, 0.0f, 0.0f);
			}
			else if (action == RL_DOOR_SWIM)
			{
				memset(compound->suffix_anchor, 0,
					sizeof(compound->suffix_anchor));
				SetVector(compound->mechanism_anchor,
					320.0f, 0.0f, 0.0f);
			}
			else
			{
				compound->heading_slack =
					SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
				SetVector(compound->suffix_anchor,
					0.0f, 0.0f, 64.0f);
				SetVector(compound->mechanism_anchor,
					320.0f, 0.0f, 0.0f);
			}
			CHECK(EncodeGraph(&identity, seeds, TEST_SEEDS, links,
				TEST_LINKS, changed, sizeof(changed), &size));
			ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
				RLR_ACTION_DISABLED, SG_RUNE_LOAD_STAGE_ACTION, target);
		}
	}

	memcpy(changed, base, sizeof(changed));
	changed[LINK_OFFSET(0) + 43U] = 1;
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_NONZERO_RESERVED, SG_RUNE_LOAD_STAGE_LINK, 0);
	memcpy(changed, base, sizeof(changed));
	changed[LINK_OFFSET(0) + SG_RUNE_V3_NONCOMPOUND_TAIL_OFFSET] = 1;
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_NONZERO_TAIL, SG_RUNE_LOAD_STAGE_LINK, 0);
	memcpy(changed, base, sizeof(changed));
	PutU32(changed + LINK_OFFSET(0) + 4U, 0);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_SELF_LINK, SG_RUNE_LOAD_STAGE_LINK, 0);
	memcpy(changed, base, sizeof(changed));
	PutU32(changed + LINK_OFFSET(0), TEST_SEEDS);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_BAD_INDEX, SG_RUNE_LOAD_STAGE_LINK, 0);
	memcpy(changed, base, sizeof(changed));
	PutU32(changed + LINK_OFFSET(0) + 4U, 5);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_LINK_RECORD,
		RLR_BAD_ENDPOINT_POLICY, SG_RUNE_LOAD_STAGE_LINK, 0);
	memcpy(changed, base, sizeof(changed));
	memcpy(changed + LINK_OFFSET(1), changed + LINK_OFFSET(0),
		SG_RUNE_V3_LINK_BYTES);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_DUPLICATE_LINK, RLR_OK,
		SG_RUNE_LOAD_STAGE_LINK, 1);
	memcpy(changed, base, sizeof(changed));
	PutU16(changed + SEED_OFFSET(0) + 14U,
		SG_RUNE_V3_SEED_TOMBSTONE);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_ROUTE_OWNERSHIP,
		RLR_TOMBSTONE_ENDPOINT, SG_RUNE_LOAD_STAGE_LINK, 0);
	memcpy(changed, base, sizeof(changed));
	PutU32(changed + LINK_OFFSET(0), 1);
	PutU32(changed + LINK_OFFSET(0) + 4U, 3);
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_ROUTE_OWNERSHIP,
		RLR_OK, SG_RUNE_LOAD_STAGE_SEED, 0);
	memcpy(changed, base, sizeof(changed));
	PutU16(changed + SEED_OFFSET(0) + 14U, UINT16_C(4));
	FixPayloadCRC(changed, sizeof(changed));
	ExpectPatchedFailure(changed, &identity, RLW_BAD_SEED_RECORD, RLR_OK,
		SG_RUNE_LOAD_STAGE_SEED, 0);
	memcpy(changed, base, sizeof(changed));
	PutU32(changed + 12, SG_RUNE_V3_MAX_SEEDS + 1U);
	FixHeaderCRC(changed);
	ExpectPatchedFailure(changed, &identity, RLW_BAD_COUNTS, RLR_OK,
		SG_RUNE_LOAD_STAGE_HEADER, SG_RUNE_LOAD_INDEX_NONE);
}

static void TestCapacityAndNoPublication(void)
{
	unsigned char encoded[TEST_BYTES];
	sg_rune_v3_identity_t identity;
	sg_rune_v3_header_t header;
	sg_rune_v3_header_t sentinel_header;
	rune_seed_t native_seeds[TEST_SEEDS];
	rune_seed_t sentinel_seeds[TEST_SEEDS];
	rune_link_t native_links[TEST_LINKS];
	rune_link_t sentinel_links[TEST_LINKS];
	sg_rune_v3_seed_t wire_seeds[TEST_SEEDS];
	sg_rune_v3_link_t wire_links[TEST_LINKS];
	uint64_t keys[TEST_LINKS];
	uint8_t marks[TEST_SEEDS];
	sg_rune_v3_loader_workspace_t workspace = LoaderWorkspace(wire_seeds,
		TEST_SEEDS, wire_links, TEST_LINKS, keys, marks);

	CHECK(EncodeBase(800.0f, encoded, &identity));
	memset(&header, 0xa5, sizeof(header));
	sentinel_header = header;
	memset(native_seeds, 0x5a, sizeof(native_seeds));
	memcpy(sentinel_seeds, native_seeds, sizeof(native_seeds));
	memset(native_links, 0x3c, sizeof(native_links));
	memcpy(sentinel_links, native_links, sizeof(native_links));
	CHECK_RESULT(SG_RuneV3Load(encoded, sizeof(encoded), &identity, &header,
		native_seeds, TEST_SEEDS - 1U, native_links, TEST_LINKS,
		&workspace), RLW_ALLOCATION_FAILED, RLR_OK,
		SG_RUNE_LOAD_STAGE_CAPACITY, SG_RUNE_LOAD_INDEX_NONE);
	CHECK(memcmp(&header, &sentinel_header, sizeof(header)) == 0);
	CHECK(memcmp(native_seeds, sentinel_seeds, sizeof(native_seeds)) == 0);
	CHECK(memcmp(native_links, sentinel_links, sizeof(native_links)) == 0);
	CHECK_RESULT(SG_RuneV3Load(encoded, sizeof(encoded), &identity, &header,
		native_seeds, TEST_SEEDS, NULL, 0, &workspace),
		RLW_ALLOCATION_FAILED, RLR_OK, SG_RUNE_LOAD_STAGE_CAPACITY,
		SG_RUNE_LOAD_INDEX_NONE);
	CHECK_RESULT(SG_RuneV3Load(encoded, sizeof(encoded), &identity, NULL,
		native_seeds, TEST_SEEDS, native_links, TEST_LINKS, &workspace),
		RLW_INVALID_ARGUMENT, RLR_OK, SG_RUNE_LOAD_STAGE_ARGUMENT,
		SG_RUNE_LOAD_INDEX_NONE);
	CHECK_RESULT(SG_RuneV3Load(encoded, sizeof(encoded), &identity, &header,
		native_seeds, TEST_SEEDS, native_links, TEST_LINKS, NULL),
		RLW_INVALID_ARGUMENT, RLR_OK, SG_RUNE_LOAD_STAGE_ARGUMENT,
		SG_RUNE_LOAD_INDEX_NONE);
	workspace.graph.source_mark_capacity = TEST_SEEDS - 1U;
	CHECK_RESULT(SG_RuneV3Load(encoded, sizeof(encoded), &identity, &header,
		native_seeds, TEST_SEEDS, native_links, TEST_LINKS, &workspace),
		RLW_ALLOCATION_FAILED, RLR_OK, SG_RUNE_LOAD_STAGE_CAPACITY,
		SG_RUNE_LOAD_INDEX_NONE);
}

static void TestZeroLinks(void)
{
	sg_rune_v3_identity_t identity = Identity(800.0f);
	sg_rune_v3_seed_t source_seed;
	unsigned char encoded[SG_RUNE_V3_HEADER_BYTES + SG_RUNE_V3_SEED_BYTES];
	size_t encoded_size = 0;
	uint8_t encode_mark;
	sg_rune_v3_workspace_t encode_workspace = GraphWorkspace(NULL, 0,
		&encode_mark, 1);
	sg_rune_v3_header_t header;
	rune_seed_t native_seed;
	sg_rune_v3_seed_t wire_seed;
	uint8_t load_mark;
	sg_rune_v3_loader_workspace_t workspace = LoaderWorkspace(&wire_seed, 1,
		NULL, 0, NULL, &load_mark);

	memset(&source_seed, 0, sizeof(source_seed));
	source_seed.flags = SG_RUNE_V3_SEED_TOMBSTONE;
	CHECK(SG_RuneV3Encode(&identity, &source_seed, 1, NULL, 0,
		&encode_workspace, encoded, sizeof(encoded), &encoded_size) == RLW_OK);
	CHECK_RESULT(SG_RuneV3Load(encoded, encoded_size, &identity, &header,
		&native_seed, 1, NULL, 0, &workspace), RLW_OK, RLR_OK,
		SG_RUNE_LOAD_STAGE_DONE, SG_RUNE_LOAD_INDEX_NONE);
	CHECK(native_seed.flags == RSF_TOMBSTONE);
	CHECK(header.num_links == 0);
}

static void TestMaximumGraph(void)
{
	const uint32_t num_seeds = SG_RUNE_V3_MAX_SEEDS;
	const uint32_t num_links = SG_RUNE_V3_MAX_LINKS;
	sg_rune_v3_identity_t identity = Identity(800.0f);
	sg_rune_v3_seed_t *source_seeds = NULL;
	sg_rune_v3_link_t *source_links = NULL;
	sg_rune_v3_seed_t *wire_seeds = NULL;
	sg_rune_v3_link_t *wire_links = NULL;
	rune_seed_t *native_seeds = NULL;
	rune_link_t *native_links = NULL;
	uint64_t *keys = NULL;
	uint8_t *marks = NULL;
	unsigned char *encoded = NULL;
	size_t encoded_size = 0;
	size_t expected_size = 0;
	sg_rune_v3_workspace_t encode_workspace;
	sg_rune_v3_loader_workspace_t load_workspace;
	sg_rune_v3_header_t header;
	sg_rune_load_result_t result;
	uint32_t index;
	int ready;

	CHECK(SG_RuneV3FileSize(num_seeds, num_links, &expected_size) == RLW_OK);
	source_seeds = (sg_rune_v3_seed_t *)calloc(num_seeds,
		sizeof(*source_seeds));
	source_links = (sg_rune_v3_link_t *)calloc(num_links,
		sizeof(*source_links));
	wire_seeds = (sg_rune_v3_seed_t *)malloc((size_t)num_seeds *
		sizeof(*wire_seeds));
	wire_links = (sg_rune_v3_link_t *)malloc((size_t)num_links *
		sizeof(*wire_links));
	native_seeds = (rune_seed_t *)malloc((size_t)num_seeds *
		sizeof(*native_seeds));
	native_links = (rune_link_t *)malloc((size_t)num_links *
		sizeof(*native_links));
	keys = (uint64_t *)malloc((size_t)num_links * sizeof(*keys));
	marks = (uint8_t *)malloc((size_t)num_seeds);
	encoded = (unsigned char *)malloc(expected_size);
	ready = source_seeds && source_links && wire_seeds && wire_links &&
		native_seeds && native_links && keys && marks && encoded;
	CHECK(ready);
	if (!ready)
		goto done;
	for (index = 0; index < num_seeds; index++)
	{
		uint32_t variant;

		for (variant = 0; variant < 8U; variant++)
		{
			uint32_t link_index = index * 8U + variant;
			sg_rune_v3_link_t *link = &source_links[link_index];

			link->source = index;
			link->destination = (index + variant + 1U) % num_seeds;
			link->action = RL_RUN;
			link->provenance = RL_PROVEN;
			link->cost_ms = 100;
		}
	}
	encode_workspace = GraphWorkspace(keys, num_links, marks, num_seeds);
	CHECK(SG_RuneV3Encode(&identity, source_seeds, num_seeds, source_links,
		num_links, &encode_workspace, encoded, expected_size,
		&encoded_size) == RLW_OK);
	CHECK(encoded_size == expected_size);
	load_workspace = LoaderWorkspace(wire_seeds, num_seeds, wire_links,
		num_links, keys, marks);
	result = SG_RuneV3Load(encoded, encoded_size, &identity, &header,
		native_seeds, num_seeds, native_links, num_links, &load_workspace);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_LOAD_STAGE_DONE,
		SG_RUNE_LOAD_INDEX_NONE);
	CHECK(result.file_size == expected_size);
	CHECK(header.num_seeds == num_seeds && header.num_links == num_links);
	CHECK(native_links[num_links - 1U].from == (int)(num_seeds - 1U));
	CHECK(native_links[num_links - 1U].to == 7);

done:
	free(source_seeds);
	free(source_links);
	free(wire_seeds);
	free(wire_links);
	free(native_seeds);
	free(native_links);
	free(keys);
	free(marks);
	free(encoded);
}

int main(void)
{
	TestGoldenAndMapping();
	TestProbeInspectIdentity();
	TestLiteralControlLaws();
	TestEncodedControlFailures();
	TestActionAndStructuralFailures();
	TestCapacityAndNoPublication();
	TestZeroLinks();
	TestMaximumGraph();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_loader_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_loader_test: all checks passed");
	return 0;
}
