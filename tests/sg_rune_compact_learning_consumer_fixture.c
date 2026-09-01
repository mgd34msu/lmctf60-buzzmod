#define main sg_rune_compact_model_fixture_main
int sg_rune_compact_model_fixture_main(void);
#include "sg_rune_compact_model_test.c"
#undef main

#include <string.h>

static compact_fixture_t fixture;

typedef struct compact_trace_identity_wire_s
{
	uint32_t magic;
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint64_t bsp_bytes;
	uint8_t bsp_sha256[SG_LEVEL_BSP_SHA256_BYTES];
	uint32_t host_physics_id;
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
} compact_trace_identity_wire_t;

#define COMPACT_TRACE_IDENTITY_MAGIC UINT32_C(0x54494433)

int SG_TestCompactLearningConsumerParseModelIdentity(
	const sg_rune_compact_model_t *model, sg_level_identity_t *identity_out);

static int BytesNonzero(const uint8_t *bytes, size_t count)
{
	size_t index;

	if (bytes == NULL)
		return 0;
	for (index = 0U; index < count; index++)
		if (bytes[index] != 0U)
			return 1;
	return 0;
}

/* This is the fixture's serialized segment-header boundary.  The consumer
 * must receive identity from parsed recorder data, not by copying the level
 * identity into the segment after the callback has been constructed. */
int SG_TestCompactLearningConsumerParseModelIdentity(
	const sg_rune_compact_model_t *model, sg_level_identity_t *identity_out)
{
	compact_trace_identity_wire_t wire;

	if (model == NULL || identity_out == NULL)
		return 0;
	memset(&wire, 0, sizeof(wire));
	wire.magic = COMPACT_TRACE_IDENTITY_MAGIC;
	wire.bsp_checksum = model->identity.bsp_checksum;
	wire.entity_crc32 = model->identity.entity_crc32;
	wire.bsp_bytes = model->identity.bsp_bytes;
	memcpy(wire.bsp_sha256, model->identity.bsp_sha256,
		SG_LEVEL_BSP_SHA256_BYTES);
	wire.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	memcpy(wire.mapname, "lmctf-test", sizeof("lmctf-test"));
	if (wire.magic != COMPACT_TRACE_IDENTITY_MAGIC ||
		wire.bsp_bytes == 0U || !BytesNonzero(wire.bsp_sha256,
			SG_LEVEL_BSP_SHA256_BYTES) || wire.host_physics_id == 0U ||
		wire.mapname[0] == '\0' || memchr(wire.mapname, '\0',
			SG_LEVEL_IDENTITY_MAPNAME_BYTES) == NULL)
		return 0;
	memset(identity_out, 0, sizeof(*identity_out));
	identity_out->bsp_checksum = wire.bsp_checksum;
	identity_out->entity_crc32 = wire.entity_crc32;
	identity_out->bsp_bytes = wire.bsp_bytes;
	memcpy(identity_out->bsp_sha256, wire.bsp_sha256,
		SG_LEVEL_BSP_SHA256_BYTES);
	identity_out->host_physics_id = wire.host_physics_id;
	memcpy(identity_out->mapname, wire.mapname,
		SG_LEVEL_IDENTITY_MAPNAME_BYTES);
	return 1;
}

sg_rune_compact_model_t *SG_TestCompactLearningConsumerModel(void);

sg_rune_compact_model_t *SG_TestCompactLearningConsumerModel(void)
{
	InitFixture(&fixture);
	return &fixture.model;
}
