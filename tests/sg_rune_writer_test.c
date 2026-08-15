/* Semantic, streaming, fault, and parity tests for the pure RUNE v3 writer. */
#include "q_shared.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_writer.h"

#define SMALL_SEEDS 2U
#define SMALL_LINKS 2U
#define SMALL_BYTES (SG_RUNE_V3_HEADER_BYTES + \
	SMALL_SEEDS * SG_RUNE_V3_SEED_BYTES + \
	SMALL_LINKS * SG_RUNE_V3_LINK_BYTES)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_RESULT(result_, diagnostic_, reason_, stage_, index_) do { \
	const sg_rune_write_result_t *checked_ = &(result_); \
	if (checked_->diagnostic != (diagnostic_) || \
	    checked_->reason != (reason_) || checked_->stage != (stage_) || \
	    checked_->index != (index_)) { \
		fprintf(stderr, "%s:%d: got d=%d r=%d s=%d i=%u; " \
		        "expected d=%d r=%d s=%d i=%u\n", __FILE__, __LINE__, \
		        (int)checked_->diagnostic, (int)checked_->reason, \
		        (int)checked_->stage, checked_->index, \
		        (int)(diagnostic_), (int)(reason_), (int)(stage_), \
		        (unsigned int)(index_)); \
		failures++; \
	} \
} while (0)

typedef struct memory_sink_s
{
	unsigned char *bytes;
	size_t capacity;
	size_t size;
	size_t calls;
	size_t fail_call;
	size_t fragment_sizes[32];
	rune_seed_t *mutate_seed;
	size_t mutate_call;
} memory_sink_t;

static int MemorySink(void *context, const unsigned char *fragment,
	size_t fragment_size)
{
	memory_sink_t *sink = (memory_sink_t *)context;
	size_t call = sink->calls++;

	if (call < sizeof(sink->fragment_sizes) /
	    sizeof(sink->fragment_sizes[0]))
		sink->fragment_sizes[call] = fragment_size;
	if (call == sink->fail_call || fragment_size > sink->capacity - sink->size)
		return -1;
	if (sink->bytes)
		memcpy(sink->bytes + sink->size, fragment, fragment_size);
	sink->size += fragment_size;
	if (sink->mutate_seed && call == sink->mutate_call)
		sink->mutate_seed->area_hint ^= 1;
	return 0;
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

static void SetVector(float vector[3], float x, float y, float z)
{
	vector[0] = x;
	vector[1] = y;
	vector[2] = z;
}

static sg_rune_v3_identity_t Identity(void)
{
	sg_rune_v3_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	memcpy(identity.map_name, "lmctf07", 7);
	identity.bsp_checksum = UINT32_C(0x12345678);
	identity.entity_crc32 = UINT32_C(0x9abcdef0);
	identity.gravity = 650.0f;
	identity.maxvelocity = 2000.0f;
	identity.pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	identity.server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	identity.host_physics_id = 1;
	return identity;
}

static void InitRun(rune_link_t *link, int from, int to)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = RL_RUN;
	link->provenance = RL_PROVEN;
	link->heading_slack = 255;
	link->cost_ms = 100;
}

static void TwoRunGraph(rune_seed_t seeds[SMALL_SEEDS],
	rune_link_t links[SMALL_LINKS])
{
	memset(seeds, 0, sizeof(*seeds) * SMALL_SEEDS);
	SetVector(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	SetVector(seeds[1].origin, 64.0f, 0.0f, 0.0f);
	seeds[0].area_hint = 7;
	seeds[1].area_hint = 255;
	InitRun(&links[0], 0, 1);
	InitRun(&links[1], 1, 0);
}

static void PrepareAction(int action, rune_seed_t seeds[SMALL_SEEDS],
	rune_link_t links[SMALL_LINKS])
{
	TwoRunGraph(seeds, links);
	links[0].action = (byte)action;
	links[0].heading_slack = 0;
	links[0].cost_ms = 500;
	switch (action)
	{
	case RL_RUN:
		links[0].heading_slack = 255;
		SetVector(links[0].anchor, 32.0f, 8.0f, 0.0f);
		break;
	case RL_JUMP:
		links[0].heading_slack = 255;
		break;
	case RL_DROP:
		links[0].heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
		SetVector(links[0].anchor, 4.0f, 0.0f, 8.0f);
		break;
	case RL_HOOK:
		links[0].heading_slack = SG_RUNE_PROOF_HOOK_CONTROL_SLACK;
		SetVector(links[0].anchor, 22.5f, -45.0f, 128.0f);
		break;
	case RL_SWIM:
		seeds[0].flags = RSF_WATER;
		links[0].provenance = RL_PROVEN;
		links[1].action = RL_SWIM;
		links[1].heading = 0;
		links[1].heading_slack = 0;
		break;
	case RL_LIFT:
	case RL_TELEPORT:
	case RL_DOOR:
		links[0].provenance = RL_DECLARED;
		links[0].heading_slack = SG_RUNE_PROOF_DECLARED_CONTROL_MARKER;
		SetVector(links[0].anchor, 16.0f, 0.0f, 0.0f);
		break;
	default:
		break;
	}
}

static sg_rune_write_result_t WriteSmall(const sg_rune_v3_identity_t *identity,
	rune_seed_t seeds[SMALL_SEEDS], rune_link_t links[SMALL_LINKS],
	memory_sink_t *sink)
{
	uint64_t keys[SMALL_LINKS];
	uint8_t marks[SMALL_SEEDS];
	sg_rune_v3_workspace_t workspace = Workspace(keys, SMALL_LINKS,
		marks, SMALL_SEEDS);

	return SG_RuneV3Write(identity, seeds, SMALL_SEEDS, links, SMALL_LINKS,
		&workspace, MemorySink, sink);
}

static void ExpectLinkFailure(const sg_rune_v3_identity_t *identity,
	rune_seed_t seeds[SMALL_SEEDS], rune_link_t links[SMALL_LINKS],
	rune_wire_diagnostic_t diagnostic, rune_reject_reason_t reason)
{
	memory_sink_t sink = MakeSink(NULL, SIZE_MAX);
	sg_rune_write_result_t result = WriteSmall(identity, seeds, links, &sink);

	CHECK_RESULT(result, diagnostic, reason,
		SG_RUNE_WRITE_STAGE_ADAPT_LINK, 0);
	CHECK(sink.calls == 0);
}

#define EXPECT_ACTION_FAILURE(action_, change_, diagnostic_, reason_) do { \
	PrepareAction((action_), seeds, links); \
	change_; \
	ExpectLinkFailure(&identity, seeds, links, (diagnostic_), (reason_)); \
} while (0)

static void AllActionsGraph(rune_seed_t seeds[8], rune_link_t links[8])
{
	int index;

	memset(seeds, 0, sizeof(*seeds) * 8U);
	memset(links, 0, sizeof(*links) * 8U);
	for (index = 0; index < 8; index++)
	{
		SetVector(seeds[index].origin, (float)(index * 64), 0.0f, 0.0f);
		seeds[index].area_hint = (short)(index * 17);
		links[index].from = index;
		links[index].to = (index + 1) & 7;
		links[index].cost_ms = (short)(125 + index * 25);
		links[index].provenance = RL_PROVEN;
	}
	seeds[4].flags = RSF_WATER;
	links[0].action = RL_RUN;
	links[0].provenance = RL_OBSERVED;
	links[0].min_speed = 4;
	links[0].heading = 64;
	links[0].heading_slack = 8;
	links[0].exit_speed = 50;
	SetVector(links[0].anchor, 32.0f, 16.0f, 0.0f);
	links[1].action = RL_JUMP;
	links[1].provenance = RL_ADJUSTED;
	links[1].heading = 32;
	links[1].heading_slack = 255;
	links[1].exit_speed = 44;
	links[2].action = RL_DROP;
	links[2].heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	links[2].exit_speed = 30;
	SetVector(links[2].anchor, 132.0f, 0.0f, 8.0f);
	links[3].action = RL_HOOK;
	links[3].heading = 224;
	links[3].heading_slack = SG_RUNE_PROOF_HOOK_CONTROL_SLACK;
	links[3].exit_speed = 60;
	SetVector(links[3].anchor, 22.5f, -45.0f, 256.0f);
	links[4].action = RL_SWIM;
	links[4].provenance = RL_PROVEN;
	links[4].exit_speed = 20;
	links[5].action = RL_LIFT;
	links[5].provenance = RL_DECLARED;
	links[5].heading_slack = SG_RUNE_PROOF_DECLARED_CONTROL_MARKER;
	SetVector(links[5].anchor, 336.0f, 0.0f, 0.0f);
	links[6].action = RL_TELEPORT;
	links[6].provenance = RL_DECLARED;
	links[6].heading_slack = SG_RUNE_PROOF_DECLARED_CONTROL_MARKER;
	SetVector(links[6].anchor, 400.0f, 0.0f, 0.0f);
	links[7].action = RL_DOOR;
	links[7].provenance = RL_DECLARED;
	links[7].heading_slack = SG_RUNE_PROOF_DECLARED_CONTROL_MARKER;
	SetVector(links[7].anchor, 448.0f, 0.0f, 0.0f);
}

static void MapExpected(const rune_seed_t *native_seeds,
	const rune_link_t *native_links, sg_rune_v3_seed_t seeds[8],
	sg_rune_v3_link_t links[8])
{
	uint32_t index;

	for (index = 0; index < 8; index++)
	{
		memset(&seeds[index], 0, sizeof(seeds[index]));
		memcpy(seeds[index].origin, native_seeds[index].origin,
			sizeof(seeds[index].origin));
		seeds[index].area_hint = native_seeds[index].area_hint;
		seeds[index].flags = native_seeds[index].flags;
		memset(&links[index], 0, sizeof(links[index]));
		links[index].source = (uint32_t)native_links[index].from;
		links[index].destination = (uint32_t)native_links[index].to;
		links[index].action = native_links[index].action;
		links[index].provenance = native_links[index].provenance;
		links[index].min_speed = native_links[index].min_speed;
		links[index].heading = native_links[index].heading;
		links[index].heading_slack = native_links[index].heading_slack;
		links[index].exit_speed = native_links[index].exit_speed;
		links[index].cost_ms = native_links[index].cost_ms;
		memcpy(links[index].suffix_anchor, native_links[index].anchor,
			sizeof(links[index].suffix_anchor));
	}
}

static void TestAllActionParity(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t native_seeds[8];
	rune_link_t native_links[8];
	sg_rune_v3_seed_t seeds[8];
	sg_rune_v3_link_t links[8];
	uint64_t writer_keys[8], codec_keys[8];
	uint8_t writer_marks[8], codec_marks[8];
	sg_rune_v3_workspace_t writer_workspace = Workspace(writer_keys, 8,
		writer_marks, 8);
	sg_rune_v3_workspace_t codec_workspace = Workspace(codec_keys, 8,
		codec_marks, 8);
	unsigned char writer_bytes[608], codec_bytes[608];
	memory_sink_t sink = MakeSink(writer_bytes, sizeof(writer_bytes));
	sg_rune_write_result_t result;
	size_t codec_size = 0;
	uint32_t index;

	AllActionsGraph(native_seeds, native_links);
	MapExpected(native_seeds, native_links, seeds, links);
	result = SG_RuneV3Write(&identity, native_seeds, 8, native_links, 8,
		&writer_workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_WRITE_STAGE_DONE,
		SG_RUNE_WRITE_INDEX_NONE);
	CHECK(result.file_size == sizeof(writer_bytes));
	CHECK(result.bytes_written == sizeof(writer_bytes));
	CHECK(sink.calls == 17 && sink.size == sizeof(writer_bytes));
	CHECK(sink.fragment_sizes[0] == SG_RUNE_V3_HEADER_BYTES);
	for (index = 0; index < 8; index++)
		CHECK(sink.fragment_sizes[1U + index] == SG_RUNE_V3_SEED_BYTES);
	for (index = 0; index < 8; index++)
		CHECK(sink.fragment_sizes[9U + index] == SG_RUNE_V3_LINK_BYTES);
	CHECK(SG_RuneV3Encode(&identity, seeds, 8, links, 8,
		&codec_workspace, codec_bytes, sizeof(codec_bytes),
		&codec_size) == RLW_OK);
	CHECK(codec_size == sizeof(codec_bytes));
	CHECK(memcmp(writer_bytes, codec_bytes, sizeof(writer_bytes)) == 0);
	for (index = 0; index < 8; index++)
	{
		const unsigned char *tail = writer_bytes + SG_RUNE_V3_HEADER_BYTES +
			8U * SG_RUNE_V3_SEED_BYTES + index * SG_RUNE_V3_LINK_BYTES +
			SG_RUNE_V3_NONCOMPOUND_TAIL_OFFSET;
		size_t byte;

		for (byte = 0; byte < SG_RUNE_V3_NONCOMPOUND_TAIL_BYTES; byte++)
			CHECK(tail[byte] == 0);
	}
}

static int HexDigit(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int LoadGolden(unsigned char golden[248])
{
	FILE *file = fopen("tests/fixtures/rune_v3_wire_golden.hex", "rb");
	size_t count = 0;
	int high = -1;
	int c;

	if (!file) return 0;
	while ((c = fgetc(file)) != EOF)
	{
		int digit = HexDigit(c);

		if (digit < 0) continue;
		if (high < 0) high = digit;
		else
		{
			if (count >= 248) { fclose(file); return 0; }
			golden[count++] = (unsigned char)((high << 4) | digit);
			high = -1;
		}
	}
	return fclose(file) == 0 && count == 248 && high < 0;
}

static void TestGoldenRecordParity(void)
{
	unsigned char golden[248], encoded[SMALL_BYTES];
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS];
	memory_sink_t sink = MakeSink(encoded, sizeof(encoded));
	sg_rune_write_result_t result;
	size_t seed0 = SG_RUNE_V3_HEADER_BYTES;
	size_t seed1 = seed0 + SG_RUNE_V3_SEED_BYTES;
	size_t link0 = seed1 + SG_RUNE_V3_SEED_BYTES;

	CHECK(LoadGolden(golden));
	TwoRunGraph(seeds, links);
	SetVector(seeds[1].origin, 128.0f, 0.0f, 0.0f);
	links[0].min_speed = 4;
	links[0].heading = 64;
	links[0].heading_slack = 8;
	links[0].exit_speed = 50;
	links[0].cost_ms = 125;
	SetVector(links[0].anchor, 64.0f, 16.0f, 0.0f);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_WRITE_STAGE_DONE,
		SG_RUNE_WRITE_INDEX_NONE);
	CHECK(memcmp(encoded + seed0, golden + seed0,
		SG_RUNE_V3_SEED_BYTES) == 0);
	CHECK(memcmp(encoded + seed1, golden + seed1,
		SG_RUNE_V3_SEED_BYTES) == 0);
	CHECK(memcmp(encoded + link0, golden + link0,
		SG_RUNE_V3_LINK_BYTES) == 0);
}

static void TestSeedAndGeneralFailures(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS];
	memory_sink_t sink;
	sg_rune_write_result_t result;

	TwoRunGraph(seeds, links);
	seeds[1].origin[0] = 4096.0f;
	sink = MakeSink(NULL, SIZE_MAX);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_BAD_SEED_RECORD, RLR_OK,
		SG_RUNE_WRITE_STAGE_ADAPT_SEED, 1);
	CHECK(sink.calls == 0);
	TwoRunGraph(seeds, links);
	seeds[1].origin[0] = NAN;
	sink = MakeSink(NULL, SIZE_MAX);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_BAD_SEED_RECORD, RLR_OK,
		SG_RUNE_WRITE_STAGE_ADAPT_SEED, 1);
	TwoRunGraph(seeds, links);
	seeds[1].area_hint = 256;
	sink = MakeSink(NULL, SIZE_MAX);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_BAD_SEED_RECORD, RLR_OK,
		SG_RUNE_WRITE_STAGE_ADAPT_SEED, 1);
	TwoRunGraph(seeds, links);
	seeds[1].flags = 4;
	sink = MakeSink(NULL, SIZE_MAX);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_BAD_SEED_RECORD, RLR_OK,
		SG_RUNE_WRITE_STAGE_ADAPT_SEED, 1);

	EXPECT_ACTION_FAILURE(RL_RUN, links[0].action = RL_ROCKETJUMP,
		RLW_BAD_LINK_RECORD, RLR_ACTION_DISABLED);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].action = RL_DOOR_DROP,
		RLW_BAD_LINK_RECORD, RLR_ACTION_DISABLED);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].action = RL_DOOR_SWIM,
		RLW_BAD_LINK_RECORD, RLR_ACTION_DISABLED);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].action = RL_DOOR_HOOK,
		RLW_BAD_LINK_RECORD, RLR_ACTION_DISABLED);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].action = UINT8_MAX,
		RLW_BAD_LINK_RECORD, RLR_UNKNOWN_ACTION);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].provenance = UINT8_MAX,
		RLW_BAD_LINK_RECORD, RLR_UNKNOWN_PROVENANCE);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].provenance = RL_CONTRACTED,
		RLW_BAD_LINK_RECORD, RLR_PROVENANCE_FORBIDDEN);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].from = -1,
		RLW_BAD_LINK_RECORD, RLR_BAD_INDEX);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].to = 2,
		RLW_BAD_LINK_RECORD, RLR_BAD_INDEX);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].to = 0,
		RLW_BAD_LINK_RECORD, RLR_SELF_LINK);
	EXPECT_ACTION_FAILURE(RL_RUN, seeds[1].flags = RSF_TOMBSTONE,
		RLW_BAD_ROUTE_OWNERSHIP, RLR_TOMBSTONE_ENDPOINT);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].cost_ms = 0,
		RLW_BAD_LINK_RECORD, RLR_BAD_COST);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].cost_ms = 30001,
		RLW_BAD_LINK_RECORD, RLR_BAD_COST);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].anchor[0] = NAN,
		RLW_BAD_LINK_RECORD, RLR_NONFINITE_ANCHOR);
	EXPECT_ACTION_FAILURE(RL_RUN, links[0].anchor[0] = 4096.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_RUN_CONTROL);
	EXPECT_ACTION_FAILURE(RL_RUN, seeds[1].flags = RSF_WATER,
		RLW_BAD_LINK_RECORD, RLR_BAD_ENDPOINT_POLICY);
}

static void TestControllerFailures(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS];
	memory_sink_t sink;
	sg_rune_write_result_t result;

	EXPECT_ACTION_FAILURE(RL_JUMP, links[0].min_speed = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_JUMP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_JUMP, links[0].anchor[0] = -0.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_JUMP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].min_speed = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_DROP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].heading_slack = 255,
		RLW_BAD_LINK_RECORD, RLR_BAD_DROP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].anchor[0] = 1.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_DROP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].anchor[0] = 257.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_DROP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].anchor[2] = 8.5f,
		RLW_BAD_LINK_RECORD, RLR_BAD_DROP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].heading = 64,
		RLW_BAD_LINK_RECORD, RLR_BAD_DROP_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].provenance = RL_OBSERVED,
		RLW_BAD_LINK_RECORD, RLR_PROVENANCE_FORBIDDEN);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].provenance = RL_ADJUSTED,
		RLW_BAD_LINK_RECORD, RLR_PROVENANCE_FORBIDDEN);
	EXPECT_ACTION_FAILURE(RL_DROP, links[0].provenance = RL_DECLARED,
		RLW_BAD_LINK_RECORD, RLR_PROVENANCE_FORBIDDEN);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].min_speed = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].heading_slack = 23,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].anchor[PITCH] = 1.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].anchor[YAW] = 1.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].anchor[PITCH] = 90.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].anchor[ROLL] = 0.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK, links[0].anchor[ROLL] = 8193.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_HOOK_CONTROL);
	EXPECT_ACTION_FAILURE(RL_HOOK,
		seeds[0].flags = seeds[1].flags = RSF_WATER,
		RLW_BAD_LINK_RECORD, RLR_BAD_ENDPOINT_POLICY);

	/* The wet-source marker is a distinct accepted HOOK schema. */
	PrepareAction(RL_HOOK, seeds, links);
	seeds[0].flags = RSF_WATER;
	links[0].heading_slack = SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
	links[1].action = RL_SWIM;
	links[1].heading = links[1].heading_slack = 0;
	sink = MakeSink(NULL, SIZE_MAX);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_WRITE_STAGE_DONE,
		SG_RUNE_WRITE_INDEX_NONE);

	EXPECT_ACTION_FAILURE(RL_SWIM, links[0].min_speed = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_SWIM_CONTROL);
	EXPECT_ACTION_FAILURE(RL_SWIM, links[0].heading = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_SWIM_CONTROL);
	EXPECT_ACTION_FAILURE(RL_SWIM, links[0].heading_slack = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_SWIM_CONTROL);
	EXPECT_ACTION_FAILURE(RL_SWIM, links[0].anchor[2] = -0.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_SWIM_CONTROL);
	EXPECT_ACTION_FAILURE(RL_SWIM, links[0].provenance = RL_ADJUSTED,
		RLW_BAD_LINK_RECORD, RLR_PROVENANCE_FORBIDDEN);
	EXPECT_ACTION_FAILURE(RL_SWIM,
		seeds[0].flags = 0; InitRun(&links[1], 1, 0),
		RLW_BAD_LINK_RECORD, RLR_BAD_ENDPOINT_POLICY);
}

static void TestDeclaredFailures(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS];
	memory_sink_t sink;
	sg_rune_write_result_t result;

	EXPECT_ACTION_FAILURE(RL_LIFT, links[0].min_speed = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_DECLARED_CONTROL);
	EXPECT_ACTION_FAILURE(RL_LIFT, links[0].heading_slack = 0,
		RLW_BAD_LINK_RECORD, RLR_BAD_DECLARED_CONTROL);
	EXPECT_ACTION_FAILURE(RL_LIFT, links[0].anchor[0] = 4096.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_DECLARED_CONTROL);
	/* LIFT deliberately retains the registry's ANY water policy. */
	PrepareAction(RL_LIFT, seeds, links);
	seeds[0].flags = RSF_WATER;
	links[1].action = RL_SWIM;
	links[1].heading = links[1].heading_slack = 0;
	sink = MakeSink(NULL, SIZE_MAX);
	result = WriteSmall(&identity, seeds, links, &sink);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_WRITE_STAGE_DONE,
		SG_RUNE_WRITE_INDEX_NONE);

	EXPECT_ACTION_FAILURE(RL_TELEPORT, links[0].heading = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_DECLARED_CONTROL);
	EXPECT_ACTION_FAILURE(RL_TELEPORT, links[0].anchor[0] = 129.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_TELEPORT_REACH);
	EXPECT_ACTION_FAILURE(RL_TELEPORT, links[0].anchor[2] = 129.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_TELEPORT_REACH);

	EXPECT_ACTION_FAILURE(RL_DOOR, links[0].exit_speed = 1,
		RLW_BAD_LINK_RECORD, RLR_BAD_DECLARED_CONTROL);
	EXPECT_ACTION_FAILURE(RL_DOOR, links[0].anchor[0] = 0.1f,
		RLW_BAD_LINK_RECORD, RLR_BAD_ANCHOR_POLICY);
	EXPECT_ACTION_FAILURE(RL_DOOR, links[0].anchor[0] = 321.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_DOOR_REACH);
	EXPECT_ACTION_FAILURE(RL_DOOR,
		seeds[1].origin[0] = 800.0f; links[0].anchor[0] = 16.0f,
		RLW_BAD_LINK_RECORD, RLR_BAD_DOOR_REACH);
	EXPECT_ACTION_FAILURE(RL_DOOR, seeds[1].flags = RSF_WATER,
		RLW_BAD_LINK_RECORD, RLR_BAD_ENDPOINT_POLICY);
}

static void TestGraphAndArguments(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS];
	uint64_t keys[SMALL_LINKS];
	uint8_t marks[SMALL_SEEDS];
	sg_rune_v3_workspace_t workspace = Workspace(keys, SMALL_LINKS,
		marks, SMALL_SEEDS);
	sg_rune_v3_workspace_t short_workspace = Workspace(keys, 1, marks, 1);
	memory_sink_t sink;
	sg_rune_write_result_t result;

	TwoRunGraph(seeds, links);
	links[1] = links[0];
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links,
		SMALL_LINKS, &workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_DUPLICATE_LINK, RLR_OK,
		SG_RUNE_WRITE_STAGE_PREFLIGHT, 1);
	CHECK(sink.calls == 0);

	TwoRunGraph(seeds, links);
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links, 1,
		&workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_BAD_ROUTE_OWNERSHIP, RLR_OK,
		SG_RUNE_WRITE_STAGE_PREFLIGHT, 1);
	CHECK(sink.calls == 0);

	memset(seeds, 0, sizeof(seeds));
	seeds[0].flags = RSF_TOMBSTONE;
	workspace = Workspace(NULL, 0, marks, SMALL_SEEDS);
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneV3Write(&identity, seeds, 1, NULL, 0, &workspace,
		MemorySink, &sink);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_WRITE_STAGE_DONE,
		SG_RUNE_WRITE_INDEX_NONE);
	CHECK(sink.calls == 2);

	TwoRunGraph(seeds, links);
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links,
		SMALL_LINKS, &short_workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_ALLOCATION_FAILED, RLR_OK,
		SG_RUNE_WRITE_STAGE_ARGUMENT, SG_RUNE_WRITE_INDEX_NONE);
	CHECK(sink.calls == 0);

	workspace = Workspace(keys, SMALL_LINKS, marks, SMALL_SEEDS);
	result = SG_RuneV3Write(NULL, seeds, SMALL_SEEDS, links, SMALL_LINKS,
		&workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_INVALID_ARGUMENT);
	result = SG_RuneV3Write(&identity, NULL, SMALL_SEEDS, links, SMALL_LINKS,
		&workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_INVALID_ARGUMENT);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, NULL, SMALL_LINKS,
		&workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_INVALID_ARGUMENT);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links, SMALL_LINKS,
		NULL, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_INVALID_ARGUMENT);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links, SMALL_LINKS,
		&workspace, NULL, &sink);
	CHECK(result.diagnostic == RLW_INVALID_ARGUMENT);
	result = SG_RuneV3Write(&identity, seeds, 0, links, SMALL_LINKS,
		&workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_BAD_COUNTS);
	result = SG_RuneV3Write(&identity, seeds, SG_RUNE_V3_MAX_SEEDS + 1U,
		links, SMALL_LINKS, &workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_BAD_COUNTS);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links,
		SG_RUNE_V3_MAX_LINKS + 1U, &workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_BAD_COUNTS);

	TwoRunGraph(seeds, links);
	identity.map_name[0] = '-';
	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneV3Write(&identity, seeds, SMALL_SEEDS, links,
		SMALL_LINKS, &workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_BAD_MAPNAME, RLR_OK,
		SG_RUNE_WRITE_STAGE_HEADER, SG_RUNE_WRITE_INDEX_NONE);
	CHECK(sink.calls == 0);
}

static void TestFaultsAndMutation(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS];
	unsigned char encoded[SMALL_BYTES];
	size_t fail_call;

	for (fail_call = 0; fail_call < 5; fail_call++)
	{
		memory_sink_t sink;
		sg_rune_write_result_t result;
		sg_rune_write_stage_t stage;
		uint32_t index;
		size_t bytes;

		TwoRunGraph(seeds, links);
		sink = MakeSink(encoded, sizeof(encoded));
		sink.fail_call = fail_call;
		result = WriteSmall(&identity, seeds, links, &sink);
		if (fail_call == 0)
		{
			stage = SG_RUNE_WRITE_STAGE_EMIT_HEADER;
			index = SG_RUNE_WRITE_INDEX_NONE;
			bytes = 0;
		}
		else if (fail_call < 3)
		{
			stage = SG_RUNE_WRITE_STAGE_EMIT_SEED;
			index = (uint32_t)(fail_call - 1U);
			bytes = SG_RUNE_V3_HEADER_BYTES +
				(fail_call - 1U) * SG_RUNE_V3_SEED_BYTES;
		}
		else
		{
			stage = SG_RUNE_WRITE_STAGE_EMIT_LINK;
			index = (uint32_t)(fail_call - 3U);
			bytes = SG_RUNE_V3_HEADER_BYTES +
				SMALL_SEEDS * SG_RUNE_V3_SEED_BYTES +
				(fail_call - 3U) * SG_RUNE_V3_LINK_BYTES;
		}
		CHECK_RESULT(result, RLW_IO_ERROR, RLR_OK, stage, index);
		CHECK(result.bytes_written == bytes && sink.size == bytes);
		CHECK(sink.calls == fail_call + 1U);
	}

	/* A sink that mutates still-valid input after accepting the header cannot
	 * make a CRC-incoherent file report success. */
	TwoRunGraph(seeds, links);
	{
		memory_sink_t sink = MakeSink(encoded, sizeof(encoded));
		sg_rune_write_result_t result;

		sink.mutate_seed = &seeds[0];
		sink.mutate_call = 0;
		result = WriteSmall(&identity, seeds, links, &sink);
		CHECK_RESULT(result, RLW_BAD_PAYLOAD_CRC, RLR_OK,
			SG_RUNE_WRITE_STAGE_VERIFY, SG_RUNE_WRITE_INDEX_NONE);
		CHECK(result.bytes_written == result.file_size);
		CHECK(sink.calls == 5);
	}

	/* A pre-open counting call never blesses a malformed second invocation. */
	TwoRunGraph(seeds, links);
	{
		memory_sink_t counter = MakeSink(NULL, SIZE_MAX);
		memory_sink_t actual = MakeSink(encoded, sizeof(encoded));
		sg_rune_write_result_t first = WriteSmall(&identity, seeds, links,
			&counter);
		sg_rune_write_result_t second;

		CHECK(first.diagnostic == RLW_OK && counter.size == first.file_size);
		links[1].cost_ms = 0;
		second = WriteSmall(&identity, seeds, links, &actual);
		CHECK_RESULT(second, RLW_BAD_LINK_RECORD, RLR_BAD_COST,
			SG_RUNE_WRITE_STAGE_ADAPT_LINK, 1);
		CHECK(actual.calls == 0);
	}
}

static void TestOrderPermutation(void)
{
	sg_rune_v3_identity_t identity = Identity();
	rune_seed_t seeds[SMALL_SEEDS];
	rune_link_t links[SMALL_LINKS], swapped[SMALL_LINKS];
	unsigned char first[SMALL_BYTES], second[SMALL_BYTES];
	memory_sink_t first_sink = MakeSink(first, sizeof(first));
	memory_sink_t second_sink = MakeSink(second, sizeof(second));
	sg_rune_write_result_t first_result, second_result;
	size_t payload = SG_RUNE_V3_HEADER_BYTES +
		SMALL_SEEDS * SG_RUNE_V3_SEED_BYTES;

	TwoRunGraph(seeds, links);
	links[0].cost_ms = 100;
	links[1].cost_ms = 200;
	swapped[0] = links[1];
	swapped[1] = links[0];
	first_result = WriteSmall(&identity, seeds, links, &first_sink);
	second_result = WriteSmall(&identity, seeds, swapped, &second_sink);
	CHECK(first_result.diagnostic == RLW_OK);
	CHECK(second_result.diagnostic == RLW_OK);
	CHECK(memcmp(first + payload,
		second + payload + SG_RUNE_V3_LINK_BYTES,
		SG_RUNE_V3_LINK_BYTES) == 0);
	CHECK(memcmp(first + payload + SG_RUNE_V3_LINK_BYTES,
		second + payload, SG_RUNE_V3_LINK_BYTES) == 0);
	CHECK(memcmp(first, second, sizeof(first)) != 0);
}

static void TestMaximumBoundaries(void)
{
	rune_seed_t *seeds = NULL;
	rune_link_t *links = NULL;
	uint64_t *keys = NULL;
	uint8_t *marks = NULL;
	sg_rune_v3_workspace_t workspace;
	sg_rune_v3_identity_t identity = Identity();
	memory_sink_t sink = MakeSink(NULL, SIZE_MAX);
	sg_rune_write_result_t result;
	uint32_t index;
	size_t expected_size = 0;

	seeds = (rune_seed_t *)calloc(SG_RUNE_V3_MAX_SEEDS, sizeof(*seeds));
	links = (rune_link_t *)calloc(SG_RUNE_V3_MAX_LINKS, sizeof(*links));
	keys = (uint64_t *)malloc((size_t)SG_RUNE_V3_MAX_LINKS * sizeof(*keys));
	marks = (uint8_t *)malloc((size_t)SG_RUNE_V3_MAX_SEEDS);
	CHECK(seeds && links && keys && marks);
	if (!seeds || !links || !keys || !marks)
		goto cleanup;
	for (index = 0; index < SG_RUNE_V3_MAX_LINKS; index++)
	{
		uint32_t source = index % SG_RUNE_V3_MAX_SEEDS;
		uint32_t band = index / SG_RUNE_V3_MAX_SEEDS;

		links[index].from = (int)source;
		links[index].to = (int)((source + band + 1U) %
			SG_RUNE_V3_MAX_SEEDS);
		links[index].action = RL_RUN;
		links[index].provenance = RL_PROVEN;
		links[index].cost_ms = 1;
	}
	workspace = Workspace(keys, SG_RUNE_V3_MAX_LINKS, marks,
		SG_RUNE_V3_MAX_SEEDS);
	result = SG_RuneV3Write(&identity, seeds, SG_RUNE_V3_MAX_SEEDS,
		links, SG_RUNE_V3_MAX_LINKS, &workspace, MemorySink, &sink);
	CHECK_RESULT(result, RLW_OK, RLR_OK, SG_RUNE_WRITE_STAGE_DONE,
		SG_RUNE_WRITE_INDEX_NONE);
	CHECK(SG_RuneV3FileSize(SG_RUNE_V3_MAX_SEEDS,
		SG_RUNE_V3_MAX_LINKS, &expected_size) == RLW_OK);
	CHECK(result.file_size == expected_size &&
		result.bytes_written == expected_size && sink.size == expected_size);
	CHECK(sink.calls == (size_t)1 + SG_RUNE_V3_MAX_SEEDS +
		SG_RUNE_V3_MAX_LINKS);

	sink = MakeSink(NULL, SIZE_MAX);
	result = SG_RuneV3Write(&identity, seeds, SG_RUNE_V3_MAX_SEEDS + 1U,
		links, SG_RUNE_V3_MAX_LINKS, &workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_BAD_COUNTS && sink.calls == 0);
	result = SG_RuneV3Write(&identity, seeds, SG_RUNE_V3_MAX_SEEDS,
		links, SG_RUNE_V3_MAX_LINKS + 1U, &workspace, MemorySink, &sink);
	CHECK(result.diagnostic == RLW_BAD_COUNTS && sink.calls == 0);

cleanup:
	free(marks);
	free(keys);
	free(links);
	free(seeds);
}

int main(void)
{
	TestAllActionParity();
	TestGoldenRecordParity();
	TestSeedAndGeneralFailures();
	TestControllerFailures();
	TestDeclaredFailures();
	TestGraphAndArguments();
	TestFaultsAndMutation();
	TestOrderPermutation();
	TestMaximumBoundaries();
	if (failures)
	{
		fprintf(stderr, "sg_rune_writer_test: %d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	puts("sg_rune_writer_test: ok");
	return EXIT_SUCCESS;
}
