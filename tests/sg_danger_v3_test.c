/* Focused model/codec tests for graph-bound DNG3 danger. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_danger.h"

#define TEST_SEEDS 4U
#define TEST_PAYLOAD_BYTES (2U * TEST_SEEDS * 4U)
#define TEST_RUNE_HEADER_CRC_OFFSET 60U

level_locals_t level;

static rune_t *published_rune;
static qboolean physics_compatible = true;
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct danger_fixture_s
{
	rune_t rune;
	rune_seed_t seeds[TEST_SEEDS];
	byte linked[TEST_SEEDS];
} danger_fixture_t;

rune_t *SG_Rune(void)
{
	return published_rune;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	return rune && rune == published_rune && physics_compatible;
}

int SG_TeamIdx(int team)
{
	return team - 1;
}

void SG_TimerArm(float *stamp, float delay)
{
	*stamp = level.time + delay;
}

qboolean SG_TimerPending(float stamp)
{
	return level.time < stamp;
}

static uint32_t GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] |
	       ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

static void PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static void FixtureInit(danger_fixture_t *fixture)
{
	unsigned char header[SG_RUNE_V3_HEADER_BYTES];
	sg_rune_v3_header_t *wire;

	memset(fixture, 0, sizeof(*fixture));
	wire = &fixture->rune.v3_header;
	wire->magic = SG_RUNE_V3_MAGIC;
	wire->version = SG_RUNE_V3_VERSION;
	wire->header_bytes = SG_RUNE_V3_HEADER_BYTES;
	wire->seed_bytes = SG_RUNE_V3_SEED_BYTES;
	wire->link_bytes = SG_RUNE_V3_LINK_BYTES;
	wire->num_seeds = TEST_SEEDS;
	wire->num_links = 3;
	wire->payload_crc32 = UINT32_C(0x10203040);
	wire->bsp_checksum = UINT32_C(0x11223344);
	wire->entity_crc32 = UINT32_C(0x55667788);
	wire->action_contract_crc32 = SG_ACTION_CONTRACT_CRC32;
	wire->physics_flags = SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED;
	wire->gravity = 800.0f;
	wire->airaccelerate = 0.0f;
	wire->maxvelocity = 2000.0f;
	wire->pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	wire->server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	wire->host_physics_id = 1;
	memcpy(wire->map_name, "dng3_test", sizeof("dng3_test"));
	CHECK(SG_RuneV3EncodeHeader(wire, header, sizeof(header)) == RLW_OK);
	wire->header_crc32 = GetU32(header + TEST_RUNE_HEADER_CRC_OFFSET);

	fixture->rune.hdr.magic = (int)SG_RUNE_V3_MAGIC;
	fixture->rune.hdr.version = SG_RUNE_V3_VERSION;
	fixture->rune.hdr.num_seeds = (int)TEST_SEEDS;
	fixture->rune.hdr.num_links = 3;
	memcpy(fixture->rune.hdr.mapname, wire->map_name,
	    sizeof(fixture->rune.hdr.mapname));
	fixture->rune.seeds = fixture->seeds;
	fixture->rune.linked_seed = fixture->linked;
	fixture->linked[0] = 1;
	fixture->linked[1] = 1;
	fixture->linked[2] = 0;
	fixture->linked[3] = 1;
	fixture->seeds[1].flags = RSF_WATER;
	fixture->seeds[2].flags = RSF_TOMBSTONE;
	published_rune = &fixture->rune;
	physics_compatible = true;
}

static void BuildPayload(unsigned char payload[TEST_PAYLOAD_BYTES],
	const int red[TEST_SEEDS], const int blue[TEST_SEEDS])
{
	size_t seed;

	for (seed = 0; seed < TEST_SEEDS; seed++)
	{
		PutU32(payload + seed * 4U, (uint32_t)red[seed]);
		PutU32(payload + (TEST_SEEDS + seed) * 4U,
		    (uint32_t)blue[seed]);
	}
}

static void CheckPlane(const int *actual, const int expected[TEST_SEEDS])
{
	size_t seed;

	CHECK(actual != NULL);
	if (!actual)
		return;
	for (seed = 0; seed < TEST_SEEDS; seed++)
		CHECK(actual[seed] == expected[seed]);
}

static void CheckZeroPlanes(void)
{
	static const int zero[TEST_SEEDS] = { 0, 0, 0, 0 };

	CheckPlane(Danger_Field(1), zero);
	CheckPlane(Danger_Field(2), zero);
}

static void TestResetAndCandidateCodec(void)
{
	danger_fixture_t fixture;
	static const int red_values[TEST_SEEDS] = { 1, 0x1234, 0, 8000 };
	static const int blue_values[TEST_SEEDS] = { 0x0102, 4097, 0, 7999 };
	unsigned char payload[TEST_PAYLOAD_BYTES];
	unsigned char saved[TEST_PAYLOAD_BYTES];
	int red[TEST_SEEDS] = { -1, -2, -3, -4 };
	int blue[TEST_SEEDS] = { -5, -6, -7, -8 };
	int red_sentinel[TEST_SEEDS];
	int blue_sentinel[TEST_SEEDS];
	uint64_t revision;
	size_t payload_size;

	FixtureInit(&fixture);
	revision = Danger_Revision();
	Danger_ResetLevel();
	CHECK(Danger_Revision() == revision + 1);
	CHECK(!Danger_IsActive());
	CHECK(!Danger_PersistenceEnabled());
	CHECK(!Danger_IsDirty());
	CHECK(!Danger_CheckpointPending());
	CHECK(Danger_Field(0) == NULL);
	CHECK(Danger_Field(3) == NULL);
	CheckZeroPlanes();

	CHECK(Danger_V3PayloadBytes(&fixture.rune) == TEST_PAYLOAD_BYTES);
	BuildPayload(payload, red_values, blue_values);
	CHECK(payload[0] == 1 && payload[1] == 0);
	CHECK(payload[4] == 0x34 && payload[5] == 0x12);
	CHECK(payload[16] == 0x02 && payload[17] == 0x01);
	CHECK(payload[28] == 0x3f && payload[29] == 0x1f);
	CHECK(Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, blue, TEST_SEEDS));
	CheckPlane(red, red_values);
	CheckPlane(blue, blue_values);

	memcpy(red_sentinel, red, sizeof(red));
	memcpy(blue_sentinel, blue, sizeof(blue));
	CHECK(!Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload) - 1, red, blue, TEST_SEEDS));
	CHECK(memcmp(red, red_sentinel, sizeof(red)) == 0);
	CHECK(memcmp(blue, blue_sentinel, sizeof(blue)) == 0);
	CHECK(!Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, blue, TEST_SEEDS - 1));
	CHECK(memcmp(red, red_sentinel, sizeof(red)) == 0);
	CHECK(!Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, red, TEST_SEEDS));

	memcpy(saved, payload, sizeof(saved));
	PutU32(payload, 8001);
	CHECK(!Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, blue, TEST_SEEDS));
	CHECK(memcmp(red, red_sentinel, sizeof(red)) == 0);
	memcpy(payload, saved, sizeof(payload));
	PutU32(payload + 2U * 4U, 1);
	CHECK(!Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, blue, TEST_SEEDS));
	CHECK(memcmp(blue, blue_sentinel, sizeof(blue)) == 0);

	/* A malformed non-tombstone without outgoing ownership is still inert. */
	memcpy(payload, saved, sizeof(payload));
	fixture.linked[1] = 0;
	CHECK(!Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, blue, TEST_SEEDS));
	PutU32(payload + 1U * 4U, 0);
	PutU32(payload + (TEST_SEEDS + 1U) * 4U, 0);
	CHECK(Danger_DecodeV3Candidate(&fixture.rune, payload,
	    sizeof(payload), red, blue, TEST_SEEDS));
	fixture.linked[1] = 1;

	payload_size = 777;
	revision = UINT64_C(0xabcdef);
	memset(payload, 0xa5, sizeof(payload));
	memcpy(saved, payload, sizeof(saved));
	CHECK(!Danger_CaptureV3Payload(payload, sizeof(payload),
	    &payload_size, &revision));
	CHECK(memcmp(payload, saved, sizeof(payload)) == 0);
	CHECK(payload_size == 777);
	CHECK(revision == UINT64_C(0xabcdef));
}

static void TestPublicationBindingAndCapture(void)
{
	union capture_payload_alias_u
	{
		max_align_t alignment;
		unsigned char bytes[TEST_PAYLOAD_BYTES + sizeof(max_align_t)];
	} payload_alias;
	union capture_outputs_u
	{
		size_t size;
		uint64_t revision;
	} aliased_outputs;
	danger_fixture_t fixture;
	static const int red[TEST_SEEDS] = { 1, 0x1234, 0, 8000 };
	static const int blue[TEST_SEEDS] = { 0x0102, 4097, 0, 7999 };
	static const int bad_tombstone[TEST_SEEDS] = { 1, 2, 3, 4 };
	unsigned char expected[TEST_PAYLOAD_BYTES];
	unsigned char captured[TEST_PAYLOAD_BYTES];
	unsigned char sentinel[TEST_PAYLOAD_BYTES];
	unsigned char payload_alias_saved[sizeof(payload_alias.bytes)];
	uint64_t before;
	uint64_t captured_revision = 0;
	size_t captured_size = 0;

	FixtureInit(&fixture);
	Danger_ResetLevel();
	before = Danger_Revision();
	published_rune = NULL;
	CHECK(!Danger_Publish(&fixture.rune, red, blue, TEST_SEEDS, true));
	CHECK(Danger_Revision() == before);
	CHECK(!Danger_IsActive());
	published_rune = &fixture.rune;
	CHECK(!Danger_Publish(&fixture.rune, bad_tombstone, blue,
	    TEST_SEEDS, true));
	CHECK(!Danger_Publish(&fixture.rune, red, NULL, TEST_SEEDS, true));
	CHECK(!Danger_Publish(&fixture.rune, red, blue,
	    TEST_SEEDS - 1, true));
	CHECK(Danger_Revision() == before);
	CHECK(Danger_Publish(&fixture.rune, red, blue, TEST_SEEDS, false));
	CHECK(Danger_Revision() == before + 1);
	CHECK(Danger_IsActive());
	CHECK(!Danger_PersistenceEnabled());
	CHECK(!Danger_IsDirty());
	CHECK(!Danger_CheckpointPending());
	CheckPlane(Danger_Field(1), red);
	CheckPlane(Danger_Field(2), blue);

	BuildPayload(expected, red, blue);
	CHECK(Danger_CaptureV3Payload(captured, sizeof(captured),
	    &captured_size, &captured_revision));
	CHECK(captured_size == sizeof(captured));
	CHECK(captured_revision == Danger_Revision());
	CHECK(memcmp(captured, expected, sizeof(captured)) == 0);

	/* Const candidate-plane aliasing is safe: publication copies into two
	 * private, disjoint team planes only after complete validation. */
	CHECK(Danger_Publish(&fixture.rune, red, red, TEST_SEEDS, false));
	CheckPlane(Danger_Field(1), red);
	CheckPlane(Danger_Field(2), red);
	CHECK(Danger_Publish(&fixture.rune, red, blue, TEST_SEEDS, false));

	memset(captured, 0xa5, sizeof(captured));
	memcpy(sentinel, captured, sizeof(sentinel));
	captured_size = 999;
	captured_revision = 888;
	CHECK(!Danger_CaptureV3Payload(captured, sizeof(captured) - 1,
	    &captured_size, &captured_revision));
	CHECK(memcmp(captured, sentinel, sizeof(captured)) == 0);
	CHECK(captured_size == 999 && captured_revision == 888);
	aliased_outputs.revision = UINT64_C(0xa5a5a5a5a5a5a5a5);
	memcpy(sentinel, captured, sizeof(sentinel));
	CHECK(!Danger_CaptureV3Payload(captured, sizeof(captured),
	    &aliased_outputs.size, &aliased_outputs.revision));
	CHECK(memcmp(captured, sentinel, sizeof(captured)) == 0);
	CHECK(aliased_outputs.revision == UINT64_C(0xa5a5a5a5a5a5a5a5));

	/* Also reject a correctly aligned scalar that partially overlaps the
	 * payload output; neither output may change on the rejected tuple. */
	memset(payload_alias.bytes, 0x5a, sizeof(payload_alias.bytes));
	memcpy(payload_alias_saved, payload_alias.bytes,
	    sizeof(payload_alias_saved));
	captured_revision = UINT64_C(0x123456789abcdef0);
	CHECK(!Danger_CaptureV3Payload(payload_alias.bytes,
	    TEST_PAYLOAD_BYTES,
	    (size_t *)(void *)(payload_alias.bytes + _Alignof(size_t)),
	    &captured_revision));
	CHECK(captured_revision == UINT64_C(0x123456789abcdef0));
	CHECK(memcmp(payload_alias.bytes, payload_alias_saved,
	    sizeof(payload_alias.bytes)) == 0);
#if SIZE_MAX < UINT64_MAX
	/* On 32-bit size_t, two naturally aligned scalar outputs can partially
	 * overlap at distinct addresses. */
	{
		union partial_scalar_alias_u
		{
			uint64_t revision;
			size_t sizes[2];
		} partial;

		partial.revision = UINT64_C(0x55aa55aa55aa55aa);
		CHECK(!Danger_CaptureV3Payload(captured, sizeof(captured),
		    &partial.sizes[1], &partial.revision));
		CHECK(partial.revision == UINT64_C(0x55aa55aa55aa55aa));
	}
#endif

	/* Both the canonical header and the live physics law remain bound. */
	before = Danger_Revision();
	fixture.rune.v3_header.gravity = 801.0f;
	CHECK(!Danger_CaptureV3Payload(captured, sizeof(captured),
	    &captured_size, &captured_revision));
	Danger_Learn(1, 0);
	CHECK(Danger_Revision() == before);
	fixture.rune.v3_header.gravity = 800.0f;
	physics_compatible = false;
	CHECK(!Danger_CaptureV3Payload(captured, sizeof(captured),
	    &captured_size, &captured_revision));
	Danger_Learn(1, 0);
	CHECK(Danger_Revision() == before);
	physics_compatible = true;

	/* A rejected replacement cannot disturb the already-published model. */
	CHECK(!Danger_Publish(&fixture.rune, bad_tombstone, blue,
	    TEST_SEEDS, true));
	CHECK(Danger_Revision() == before);
	CheckPlane(Danger_Field(1), red);
	CHECK(Danger_Publish(&fixture.rune, NULL, NULL, 0, true));
	CHECK(Danger_PersistenceEnabled());
	CHECK(!Danger_IsDirty());
	CheckZeroPlanes();
	CHECK(!Danger_Publish(&fixture.rune, NULL, blue, 0, true));
}

static void TestLearningDirtyAndCommitRace(void)
{
	danger_fixture_t fixture;
	static const int zero[TEST_SEEDS] = { 0, 0, 0, 0 };
	unsigned char payload[TEST_PAYLOAD_BYTES];
	uint64_t baseline;
	uint64_t first_capture;
	uint64_t second_capture;
	size_t payload_size;
	int attempt;

	FixtureInit(&fixture);
	Danger_ResetLevel();
	CHECK(Danger_Publish(&fixture.rune, zero, zero, TEST_SEEDS, true));
	baseline = Danger_Revision();
	Danger_Learn(0, 0);
	Danger_Learn(3, 0);
	Danger_Learn(1, -1);
	Danger_Learn(1, (int)TEST_SEEDS);
	Danger_Learn(1, 2);
	fixture.linked[1] = 0;
	Danger_Learn(2, 1);
	fixture.linked[1] = 1;
	CHECK(Danger_Revision() == baseline);
	CHECK(!Danger_IsDirty());

	for (attempt = 0; attempt < 8; attempt++)
		Danger_Learn(1, 0);
	CHECK(Danger_Field(1)[0] == 8000);
	CHECK(Danger_Field(2)[0] == 0);
	CHECK(Danger_Revision() == baseline + 7);
	Danger_Learn(1, 0);
	CHECK(Danger_Revision() == baseline + 7);
	Danger_Learn(2, 1);
	CHECK(Danger_Field(2)[1] == 1200);
	CHECK(Danger_Field(1)[1] == 0);
	CHECK(Danger_Revision() == baseline + 8);
	CHECK(Danger_IsDirty());
	CHECK(Danger_CheckpointPending());

	CHECK(Danger_CaptureV3Payload(payload, sizeof(payload),
	    &payload_size, &first_capture));
	CHECK(payload_size == sizeof(payload));
	Danger_Learn(2, 3);
	CHECK(!Danger_MarkCommitted(first_capture));
	CHECK(Danger_IsDirty());
	CHECK(Danger_CaptureV3Payload(payload, sizeof(payload),
	    &payload_size, &second_capture));
	CHECK(second_capture == first_capture + 1);
	CHECK(Danger_MarkCommitted(second_capture));
	CHECK(!Danger_IsDirty());
	CHECK(!Danger_CheckpointPending());

	CHECK(Danger_Publish(&fixture.rune, zero, zero, TEST_SEEDS, false));
	Danger_Learn(1, 0);
	CHECK(Danger_IsDirty());
	CHECK(!Danger_CheckpointPending());
	CHECK(!Danger_MarkCommitted(Danger_Revision()));
	Danger_ResetLevel();
	CHECK(!Danger_MarkCommitted(Danger_Revision()));
}

static void TestDecayCadenceAndDrift(void)
{
	danger_fixture_t fixture;
	static const int red[TEST_SEEDS] = { 64, 0, 0, 0 };
	static const int blue[TEST_SEEDS] = { 0, 1, 0, 0 };
	static const int zero[TEST_SEEDS] = { 0, 0, 0, 0 };
	uint64_t baseline;

	FixtureInit(&fixture);
	Danger_ResetLevel();
	level.time = 10.0f;
	CHECK(Danger_Publish(&fixture.rune, red, blue, TEST_SEEDS, true));
	baseline = Danger_Revision();
	level.time = 10.999f;
	Danger_Decay();
	CHECK(Danger_Revision() == baseline);
	CHECK(Danger_Field(1)[0] == 64 && Danger_Field(2)[1] == 1);
	level.time = 11.0f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 62);
	CHECK(Danger_Field(2)[1] == 0);
	CHECK(Danger_Revision() == baseline + 1);
	Danger_Decay();
	CHECK(Danger_Revision() == baseline + 1);

	/* A missed deadline consumes one formula tick, never a catch-up loop. */
	level.time = 13.5f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 61);
	CHECK(Danger_Revision() == baseline + 2);
	level.time = 14.0f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 61);

	/* Incompatible time rearms the cadence and cannot age the plane. */
	physics_compatible = false;
	level.time = 20.0f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 61);
	physics_compatible = true;
	level.time = 20.999f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 61);
	level.time = 21.0f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 60);

	/* Canonical header drift, including gravity, is equally fail closed. */
	fixture.rune.v3_header.gravity = 801.0f;
	level.time = 30.0f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 60);
	fixture.rune.v3_header.gravity = 800.0f;
	level.time = 30.999f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 60);
	level.time = 31.0f;
	Danger_Decay();
	CHECK(Danger_Field(1)[0] == 59);

	/* A due zero-plane scan advances cadence but is not a mutation. */
	level.time = 40.0f;
	CHECK(Danger_Publish(&fixture.rune, zero, zero, TEST_SEEDS, true));
	baseline = Danger_Revision();
	level.time = 41.0f;
	Danger_Decay();
	CHECK(Danger_Revision() == baseline);
	CHECK(!Danger_IsDirty());
}

int main(void)
{
	TestResetAndCandidateCodec();
	TestPublicationBindingAndCapture();
	TestLearningDirtyAndCommitRace();
	TestDecayCadenceAndDrift();

	if (failures)
	{
		fprintf(stderr, "sg_danger_v3_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_danger_v3_test: ok");
	return 0;
}
