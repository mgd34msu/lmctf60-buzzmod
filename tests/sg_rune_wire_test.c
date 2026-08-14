/* Golden-vector and fail-closed tests for the isolated C RUNE v3 codec. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_wire.h"

#define GOLDEN_BYTES 248U
#define SEED0_OFFSET SG_RUNE_V3_HEADER_BYTES
#define SEED1_OFFSET (SEED0_OFFSET + SG_RUNE_V3_SEED_BYTES)
#define LINK0_OFFSET (SEED1_OFFSET + SG_RUNE_V3_SEED_BYTES)
#define LINK1_OFFSET (LINK0_OFFSET + SG_RUNE_V3_LINK_BYTES)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	rune_wire_diagnostic_t actual_ = (expression); \
	if (actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: expected diagnostic %d, got %d: %s\n", \
		        __FILE__, __LINE__, (int)(expected), (int)actual_, \
		        #expression); \
		failures++; \
	} \
} while (0)

static int HexDigit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int LoadGolden(unsigned char golden[GOLDEN_BYTES])
{
	FILE *file = fopen("tests/fixtures/rune_v3_wire_golden.hex", "rb");
	size_t count = 0;
	int high = -1;
	int c;

	if (!file)
	{
		perror("tests/fixtures/rune_v3_wire_golden.hex");
		return 0;
	}
	while ((c = fgetc(file)) != EOF)
	{
		int digit = HexDigit(c);

		if (digit < 0)
			continue;
		if (high < 0)
			high = digit;
		else
		{
			if (count >= GOLDEN_BYTES)
			{
				fclose(file);
				return 0;
			}
			golden[count++] = (unsigned char)((high << 4) | digit);
			high = -1;
		}
	}
	if (fclose(file) != 0)
		return 0;
	return count == GOLDEN_BYTES && high < 0;
}

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

static void PutFloat(unsigned char *out, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	PutU32(out, bits);
}

static void FixHeaderCRC(unsigned char data[GOLDEN_BYTES])
{
	uint32_t crc = 0;

	PutU32(data + SG_RUNE_V3_HEADER_CRC_OFFSET, 0);
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3HeaderCRC32(data,
		SG_RUNE_V3_HEADER_BYTES, &crc));
	PutU32(data + SG_RUNE_V3_HEADER_CRC_OFFSET, crc);
}

static void FixPayloadCRC(unsigned char data[GOLDEN_BYTES])
{
	uint32_t crc = 0;

	CHECK(SG_CRC32Buffer(data + SG_RUNE_V3_HEADER_BYTES,
		GOLDEN_BYTES - SG_RUNE_V3_HEADER_BYTES, &crc));
	PutU32(data + 20, crc);
	FixHeaderCRC(data);
}

static void SetMap(char map_name[SG_RUNE_V3_MAP_NAME_BYTES],
	const char *value)
{
	size_t length = strlen(value);

	memset(map_name, 0, SG_RUNE_V3_MAP_NAME_BYTES);
	CHECK(length < SG_RUNE_V3_MAP_NAME_BYTES);
	if (length < SG_RUNE_V3_MAP_NAME_BYTES)
		memcpy(map_name, value, length);
}

static void SetVector(float vector[3], float x, float y, float z)
{
	vector[0] = x;
	vector[1] = y;
	vector[2] = z;
}

static void GoldenObjects(sg_rune_v3_identity_t *identity,
	sg_rune_v3_seed_t seeds[2], sg_rune_v3_link_t links[2])
{
	memset(identity, 0, sizeof(*identity));
	SetMap(identity->map_name, "lmctf07");
	identity->bsp_checksum = UINT32_C(0x12345678);
	identity->entity_crc32 = UINT32_C(0x9abcdef0);
	identity->gravity = 650.0f;
	identity->airaccelerate = 0.0f;
	identity->maxvelocity = 2000.0f;
	identity->pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	identity->server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	identity->host_physics_id = 1;

	memset(seeds, 0, sizeof(*seeds) * 2U);
	SetVector(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	seeds[0].area_hint = 7;
	SetVector(seeds[1].origin, 128.0f, 0.0f, 0.0f);
	seeds[1].area_hint = 255;

	memset(links, 0, sizeof(*links) * 2U);
	links[0].source = 0;
	links[0].destination = 1;
	links[0].action = RL_RUN;
	links[0].provenance = RL_PROVEN;
	links[0].min_speed = 4;
	links[0].heading = 64;
	links[0].heading_slack = 8;
	links[0].exit_speed = 50;
	links[0].cost_ms = 125;
	SetVector(links[0].suffix_anchor, 64.0f, 16.0f, 0.0f);

	links[1].source = 1;
	links[1].destination = 0;
	links[1].action = RL_DOOR_DROP;
	links[1].provenance = RL_CONTRACTED;
	links[1].heading = 128;
	links[1].heading_slack = 254;
	links[1].exit_speed = 20;
	links[1].cost_ms = 500;
	SetVector(links[1].suffix_anchor, 120.0f, 0.0f, 8.0f);
	SetVector(links[1].mechanism_anchor, 112.0f, 0.0f, 0.0f);
	links[1].sweep_clear_ms = 100;
	links[1].mode = RLCM_PREOPEN;
}

static sg_rune_v3_workspace_t Workspace(uint64_t *keys, size_t key_count,
	uint8_t *marks, size_t mark_count)
{
	sg_rune_v3_workspace_t workspace;

	workspace.link_keys = keys;
	workspace.link_key_capacity = key_count;
	workspace.source_marks = marks;
	workspace.source_mark_capacity = mark_count;
	return workspace;
}

static rune_wire_diagnostic_t DecodeGoldenShape(const unsigned char *data,
	size_t size, const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out)
{
	sg_rune_v3_header_t scratch_header;
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t links[2];
	uint64_t keys[2];
	uint8_t marks[2];
	sg_rune_v3_workspace_t workspace = Workspace(keys, 2, marks, 2);

	if (!header_out)
		header_out = &scratch_header;
	return SG_RuneV3Decode(data, size, expected, header_out,
		seeds, 2, links, 2, &workspace);
}

static void TestGolden(const unsigned char golden[GOLDEN_BYTES])
{
	sg_rune_v3_identity_t identity;
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t links[2];
	sg_rune_v3_seed_t decoded_seeds[2];
	sg_rune_v3_link_t decoded_links[2];
	sg_rune_v3_header_t header;
	uint64_t keys[2];
	uint8_t marks[2];
	sg_rune_v3_workspace_t workspace = Workspace(keys, 2, marks, 2);
	unsigned char encoded[GOLDEN_BYTES];
	unsigned char record[SG_RUNE_V3_LINK_BYTES];
	size_t encoded_size = 0;
	size_t computed_size = 0;
	uint32_t crc_state = 0;
	uint32_t crc = 0;

	GoldenObjects(&identity, seeds, links);
	CHECK_DIAGNOSTIC(RLW_OK,
		SG_RuneV3FileSize(2, 2, &computed_size));
	CHECK(computed_size == GOLDEN_BYTES);
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3Encode(&identity,
		seeds, 2, links, 2, &workspace, encoded, sizeof(encoded),
		&encoded_size));
	CHECK(encoded_size == GOLDEN_BYTES);
	CHECK(memcmp(encoded, golden, GOLDEN_BYTES) == 0);
	CHECK(GetU32(golden + 20) == UINT32_C(0xe3d0ac5f));
	CHECK(GetU32(golden + SG_RUNE_V3_HEADER_CRC_OFFSET) ==
		UINT32_C(0xf245b06a));

	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3Decode(golden, GOLDEN_BYTES,
		&identity, &header, decoded_seeds, 2, decoded_links, 2,
		&workspace));
	CHECK(header.magic == SG_RUNE_V3_MAGIC);
	CHECK(header.version == SG_RUNE_V3_VERSION);
	CHECK(header.num_seeds == 2 && header.num_links == 2);
	CHECK(header.gravity == 650.0f);
	CHECK(header.payload_crc32 == UINT32_C(0xe3d0ac5f));
	CHECK(memcmp(header.map_name, identity.map_name,
		SG_RUNE_V3_MAP_NAME_BYTES) == 0);
	CHECK(decoded_seeds[0].area_hint == 7);
	CHECK(decoded_seeds[1].origin[0] == 128.0f);
	CHECK(decoded_links[0].action == RL_RUN);
	CHECK(decoded_links[1].action == RL_DOOR_DROP);
	CHECK(decoded_links[1].sweep_clear_ms == 100);

	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3DecodeHeader(golden,
		SG_RUNE_V3_HEADER_BYTES, &header));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3DecodeSeed(golden + SEED0_OFFSET,
		SG_RUNE_V3_SEED_BYTES, &decoded_seeds[0]));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3EncodeSeed(&seeds[0], record,
		SG_RUNE_V3_SEED_BYTES));
	CHECK(memcmp(record, golden + SEED0_OFFSET,
		SG_RUNE_V3_SEED_BYTES) == 0);
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3DecodeLink(golden + LINK1_OFFSET,
		SG_RUNE_V3_LINK_BYTES, &decoded_links[1]));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3EncodeLink(&links[1], record,
		SG_RUNE_V3_LINK_BYTES));
	CHECK(memcmp(record, golden + LINK1_OFFSET,
		SG_RUNE_V3_LINK_BYTES) == 0);

	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3HeaderCRC32(golden,
		SG_RUNE_V3_HEADER_BYTES, &crc));
	CHECK(crc == UINT32_C(0xf245b06a));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3PayloadCRCInit(&crc_state));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3PayloadCRCUpdate(&crc_state,
		golden + SEED0_OFFSET, SG_RUNE_V3_SEED_BYTES));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3PayloadCRCUpdate(&crc_state,
		golden + SEED1_OFFSET, SG_RUNE_V3_SEED_BYTES));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3PayloadCRCUpdate(&crc_state,
		golden + LINK0_OFFSET, SG_RUNE_V3_LINK_BYTES));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3PayloadCRCUpdate(&crc_state,
		golden + LINK1_OFFSET, SG_RUNE_V3_LINK_BYTES));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3PayloadCRCFinish(crc_state, &crc));
	CHECK(crc == UINT32_C(0xe3d0ac5f));

	CHECK(SG_RuneV3ActionWireKnown(RL_ROCKETJUMP));
	CHECK(!SG_RuneV3ActionRuntimeSupported(RL_ROCKETJUMP));
	CHECK(SG_RuneV3ActionWireKnown(RL_DOOR_DROP));
	CHECK(!SG_RuneV3ActionRuntimeSupported(RL_DOOR_DROP));
	CHECK(!SG_RuneV3ActionWireKnown(UINT8_MAX));
}

static void TestHeaderAndIdentity(const unsigned char golden[GOLDEN_BYTES])
{
	unsigned char mutated[GOLDEN_BYTES];
	sg_rune_v3_identity_t identity;
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t links[2];

	GoldenObjects(&identity, seeds, links);
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 0, 0);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_MAGIC,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 4, 2);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_UNSUPPORTED_VERSION,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 6, 127);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_HEADER_SIZE,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 8, 15);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_SEED_SIZE,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + 10, 43);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_SIZE,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));

	memcpy(mutated, golden, sizeof(mutated));
	mutated[24] ^= 1;
	CHECK_DIAGNOSTIC(RLW_BAD_HEADER_CRC,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 12, 0);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_COUNTS,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 16, SG_RUNE_V3_MAX_LINKS + 1U);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_COUNTS,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	CHECK_DIAGNOSTIC(RLW_BAD_FILE_SIZE,
		DecodeGoldenShape(golden, GOLDEN_BYTES - 1U, NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	CHECK_DIAGNOSTIC(RLW_BAD_FILE_SIZE,
		DecodeGoldenShape(mutated, SG_RUNE_V3_HEADER_BYTES - 1U,
		NULL, NULL));

	memcpy(mutated, golden, sizeof(mutated));
	mutated[64] = (unsigned char)'-';
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_MAPNAME,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	memset(mutated + 64, 'a', 64);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_MAPNAME,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 32, SG_ACTION_CONTRACT_CRC32 ^ UINT32_C(1));
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_ACTION_CONTRACT,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutFloat(mutated + 40, 650.5f);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_PHYSICS_LAW,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + 56, 0);
	FixHeaderCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_IDENTITY_UNAVAILABLE,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));

	identity.bsp_checksum ^= UINT32_C(1);
	CHECK_DIAGNOSTIC(RLW_BSP_CHECKSUM_MISMATCH,
		DecodeGoldenShape(golden, GOLDEN_BYTES, &identity, NULL));
	identity.bsp_checksum ^= UINT32_C(1);
	identity.entity_crc32 ^= UINT32_C(1);
	CHECK_DIAGNOSTIC(RLW_ENTITY_CRC_MISMATCH,
		DecodeGoldenShape(golden, GOLDEN_BYTES, &identity, NULL));
	identity.entity_crc32 ^= UINT32_C(1);
	identity.host_physics_id = 2;
	CHECK_DIAGNOSTIC(RLW_PHYSICS_ID_MISMATCH,
		DecodeGoldenShape(golden, GOLDEN_BYTES, &identity, NULL));
	identity.host_physics_id = 1;
	identity.gravity = 800.0f;
	CHECK_DIAGNOSTIC(RLW_BAD_PHYSICS_LAW,
		DecodeGoldenShape(golden, GOLDEN_BYTES, &identity, NULL));
	identity.gravity = 650.0f;
	SetMap(identity.map_name, "LMCTF07");
	CHECK_DIAGNOSTIC(RLW_MAPNAME_MISMATCH,
		DecodeGoldenShape(golden, GOLDEN_BYTES, &identity, NULL));
}

static void TestPayloadRecords(const unsigned char golden[GOLDEN_BYTES])
{
	unsigned char mutated[GOLDEN_BYTES];
	sg_rune_v3_header_t header;
	sg_rune_v3_identity_t identity;
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t links[2];
	uint64_t keys[2];
	uint8_t marks[2];
	sg_rune_v3_workspace_t workspace = Workspace(keys, 2, marks, 2);
	unsigned char encoded[GOLDEN_BYTES];
	size_t encoded_size;

	GoldenObjects(&identity, seeds, links);
	memcpy(mutated, golden, sizeof(mutated));
	mutated[SEED0_OFFSET] ^= 1;
	CHECK_DIAGNOSTIC(RLW_BAD_PAYLOAD_CRC,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));

	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + SEED0_OFFSET + 14, 4);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_SEED_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + SEED0_OFFSET, UINT32_C(0x7fc00000));
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_SEED_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));

	memcpy(mutated, golden, sizeof(mutated));
	mutated[LINK0_OFFSET + 8] = UINT8_MAX;
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	mutated[LINK0_OFFSET + 9] = RL_CONTRACTED;
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + LINK0_OFFSET + 14, 0);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + LINK0_OFFSET + 28, UINT32_C(0x80000000));
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	mutated[LINK0_OFFSET + 43] = 1;
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + LINK1_OFFSET + 40, 0);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + SEED0_OFFSET + 14, SG_RUNE_V3_SEED_WATER);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));

	memcpy(mutated, golden, sizeof(mutated));
	PutU32(mutated + LINK1_OFFSET + 0, 0);
	PutU32(mutated + LINK1_OFFSET + 4, 1);
	mutated[LINK1_OFFSET + 8] = RL_RUN;
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_DUPLICATE_LINK,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));
	memcpy(mutated, golden, sizeof(mutated));
	PutU16(mutated + SEED0_OFFSET + 14, SG_RUNE_V3_SEED_TOMBSTONE);
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_BAD_ROUTE_OWNERSHIP,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, NULL));

	/* A registered but disabled controller remains a valid wire identity. */
	memcpy(mutated, golden, sizeof(mutated));
	mutated[LINK0_OFFSET + 8] = RL_ROCKETJUMP;
	FixPayloadCRC(mutated);
	CHECK_DIAGNOSTIC(RLW_OK,
		DecodeGoldenShape(mutated, sizeof(mutated), NULL, &header));

	links[0].mechanism_anchor[0] = -0.0f;
	CHECK_DIAGNOSTIC(RLW_BAD_LINK_RECORD, SG_RuneV3Encode(&identity,
		seeds, 2, links, 2, &workspace, encoded, sizeof(encoded),
		&encoded_size));
}

static void TestOwnershipCapacityAndArguments(
	const unsigned char golden[GOLDEN_BYTES])
{
	sg_rune_v3_seed_t seed;
	uint64_t key;
	uint8_t mark;
	sg_rune_v3_workspace_t workspace = Workspace(&key, 1, &mark, 1);
	sg_rune_v3_workspace_t short_workspace = Workspace(NULL, 0, NULL, 0);
	sg_rune_v3_identity_t identity;
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t links[2];
	sg_rune_v3_header_t header;
	uint64_t keys[2];
	uint8_t marks[2];
	unsigned char encoded[GOLDEN_BYTES];
	unsigned char trailing[GOLDEN_BYTES + 1U];
	size_t encoded_size = 1;
	size_t maximum_size = 0;
	size_t expected_maximum_size =
		(size_t)SG_RUNE_V3_HEADER_BYTES +
		(size_t)SG_RUNE_V3_MAX_SEEDS * SG_RUNE_V3_SEED_BYTES +
		(size_t)SG_RUNE_V3_MAX_LINKS * SG_RUNE_V3_LINK_BYTES;

	memset(&seed, 0, sizeof(seed));
	CHECK_DIAGNOSTIC(RLW_BAD_ROUTE_OWNERSHIP,
		SG_RuneV3ValidateGraph(&seed, 1, NULL, 0, &workspace));
	seed.flags = SG_RUNE_V3_SEED_TOMBSTONE;
	CHECK_DIAGNOSTIC(RLW_OK,
		SG_RuneV3ValidateGraph(&seed, 1, NULL, 0, &workspace));
	CHECK_DIAGNOSTIC(RLW_ALLOCATION_FAILED,
		SG_RuneV3ValidateGraph(&seed, 1, NULL, 0, &short_workspace));

	GoldenObjects(&identity, seeds, links);
	workspace = Workspace(keys, 2, marks, 2);
	CHECK_DIAGNOSTIC(RLW_BAD_FILE_SIZE, SG_RuneV3Encode(&identity,
		seeds, 2, links, 2, &workspace, encoded, sizeof(encoded) - 1U,
		&encoded_size));
	CHECK(encoded_size == 0);
	CHECK_DIAGNOSTIC(RLW_ALLOCATION_FAILED, SG_RuneV3Decode(golden,
		GOLDEN_BYTES, NULL, &header, seeds, 1, links, 2, &workspace));
	CHECK_DIAGNOSTIC(RLW_ALLOCATION_FAILED, SG_RuneV3Decode(golden,
		GOLDEN_BYTES, NULL, &header, seeds, 2, links, 1, &workspace));
	workspace = Workspace(keys, 1, marks, 2);
	CHECK_DIAGNOSTIC(RLW_ALLOCATION_FAILED, SG_RuneV3Decode(golden,
		GOLDEN_BYTES, NULL, &header, seeds, 2, links, 2, &workspace));
	workspace = Workspace(keys, 2, marks, 1);
	CHECK_DIAGNOSTIC(RLW_ALLOCATION_FAILED, SG_RuneV3Decode(golden,
		GOLDEN_BYTES, NULL, &header, seeds, 2, links, 2, &workspace));
	workspace = Workspace(keys, 2, marks, 2);
	CHECK_DIAGNOSTIC(RLW_INVALID_ARGUMENT, SG_RuneV3Decode(golden,
		GOLDEN_BYTES, NULL, &header, seeds, 2, links, 2, NULL));
	CHECK_DIAGNOSTIC(RLW_INVALID_ARGUMENT, SG_RuneV3Decode(golden,
		GOLDEN_BYTES, NULL, NULL, seeds, 2, links, 2, &workspace));
	memcpy(trailing, golden, GOLDEN_BYTES);
	trailing[GOLDEN_BYTES] = 0;
	CHECK_DIAGNOSTIC(RLW_BAD_FILE_SIZE, SG_RuneV3Decode(trailing,
		sizeof(trailing), NULL, &header, seeds, 2, links, 2, &workspace));
	CHECK_DIAGNOSTIC(RLW_INVALID_ARGUMENT,
		SG_RuneV3PayloadCRCInit(NULL));
	CHECK_DIAGNOSTIC(RLW_INVALID_ARGUMENT,
		SG_RuneV3PayloadCRCUpdate(NULL, golden, 1));
	CHECK_DIAGNOSTIC(RLW_INVALID_ARGUMENT,
		SG_RuneV3HeaderCRC32(NULL, SG_RUNE_V3_HEADER_BYTES, NULL));
	CHECK_DIAGNOSTIC(RLW_BAD_COUNTS,
		SG_RuneV3FileSize(0, 0, &encoded_size));
	CHECK_DIAGNOSTIC(RLW_OK, SG_RuneV3FileSize(
		SG_RUNE_V3_MAX_SEEDS, SG_RUNE_V3_MAX_LINKS, &maximum_size));
	CHECK(maximum_size == expected_maximum_size);
	CHECK_DIAGNOSTIC(RLW_BAD_COUNTS, SG_RuneV3FileSize(
		SG_RUNE_V3_MAX_SEEDS + 1U, 0, &maximum_size));
	CHECK_DIAGNOSTIC(RLW_BAD_COUNTS, SG_RuneV3FileSize(
		1, SG_RUNE_V3_MAX_LINKS + 1U, &maximum_size));
}

static void TestEverySingleBitRejects(
	const unsigned char golden[GOLDEN_BYTES])
{
	unsigned char mutated[GOLDEN_BYTES];
	size_t offset;
	unsigned int bit;

	for (offset = 0; offset < GOLDEN_BYTES; offset++)
		for (bit = 0; bit < 8; bit++)
		{
			memcpy(mutated, golden, sizeof(mutated));
			mutated[offset] ^= (unsigned char)(1U << bit);
			CHECK(DecodeGoldenShape(mutated, sizeof(mutated),
				NULL, NULL) != RLW_OK);
		}
}

int main(void)
{
	unsigned char golden[GOLDEN_BYTES];

	if (!LoadGolden(golden))
	{
		fprintf(stderr, "sg_rune_wire_test: could not load exact golden\n");
		return EXIT_FAILURE;
	}
	TestGolden(golden);
	TestHeaderAndIdentity(golden);
	TestPayloadRecords(golden);
	TestOwnershipCapacityAndArguments(golden);
	TestEverySingleBitRejects(golden);
	if (failures)
	{
		fprintf(stderr, "sg_rune_wire_test: %d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	puts("sg_rune_wire_test: ok");
	return EXIT_SUCCESS;
}
