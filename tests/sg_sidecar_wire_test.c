/* Compact v12 artifact-bound sidecar codec tests. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_sidecar_wire.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	sg_sidecar_diagnostic_t actual_ = (expression); \
	if (actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: expected %d, got %d: %s\\n", \
			__FILE__, __LINE__, (int)(expected), (int)actual_, #expression); \
		failures++; \
	} \
} while (0)

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

static void PutU64(unsigned char *out, uint64_t value)
{
	uint32_t index;

	for (index = 0U; index < 8U; index++)
		out[index] = (unsigned char)(value >> (index * 8U));
}

static uint32_t GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
		((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static void FixHeaderCRC(unsigned char *bytes)
{
	unsigned char copy[SG_SIDECAR_HEADER_BYTES];
	uint32_t crc;

	memcpy(copy, bytes, sizeof(copy));
	memset(copy + SG_SIDECAR_HEADER_CRC_OFFSET, 0, 4U);
	CHECK(SG_CRC32Buffer(copy, sizeof(copy), &crc));
	PutU32(bytes + SG_SIDECAR_HEADER_CRC_OFFSET, crc);
}

static void InitInfo(sg_rune_compact_wire_info_t *info)
{
	uint32_t index;

	memset(info, 0, sizeof(*info));
	info->wire_version = SG_RUNE_COMPACT_WIRE_VERSION;
	info->model_version = SG_RUNE_COMPACT_MODEL_VERSION;
	info->analytic_version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	info->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	info->image_bytes = UINT64_C(4096);
	info->checksum = UINT32_C(0x1a2b3c4d);
	for (index = 0U; index < 32U; index++)
		info->identity.bsp_sha256[index] = (uint8_t)(index + 1U);
	info->identity.bsp_bytes = UINT64_C(1234567);
	info->identity.bsp_checksum = UINT32_C(0x11223344);
	info->identity.entity_crc32 = UINT32_C(0x55667788);
	info->identity.entity_semantics_id = UINT64_C(1);
	info->identity.physics_abi_id = UINT64_C(2);
	info->identity.collision_law_id = UINT64_C(3);
	info->identity.pmove_law_id = UINT64_C(4);
	info->identity.gravity_law_id = UINT64_C(5);
	info->identity.hook_law_id = UINT64_C(6);
	info->identity.mechanism_law_id = UINT64_C(7);
	info->identity.weapon_law_id = UINT64_C(8);
	info->identity.construction_id = UINT64_C(9);
	info->identity.schema_id = UINT64_C(10);
	info->identity.producer_identity = UINT64_C(11);
	info->identity.source_counts.model_count = 12U;
	info->identity.source_counts.leaf_count = 13U;
	info->identity.source_counts.area_count = 14U;
	info->identity.source_counts.plane_count = 15U;
	info->identity.source_counts.brush_count = 16U;
	info->identity.source_counts.brush_side_count = 17U;
	info->identity.source_counts.entity_count = 18U;
	info->identity.standing_hull.mins.value[0] = -16;
	info->identity.standing_hull.mins.value[1] = -16;
	info->identity.standing_hull.mins.value[2] = -24;
	info->identity.standing_hull.maxs.value[0] = 16;
	info->identity.standing_hull.maxs.value[1] = 16;
	info->identity.standing_hull.maxs.value[2] = 32;
	info->identity.crouching_hull.mins.value[0] = -16;
	info->identity.crouching_hull.mins.value[1] = -16;
	info->identity.crouching_hull.mins.value[2] = -24;
	info->identity.crouching_hull.maxs.value[0] = 16;
	info->identity.crouching_hull.maxs.value[1] = 16;
	info->identity.crouching_hull.maxs.value[2] = 4;
	info->identity.physics.gravity_bits = UINT32_C(0x44480000);
	info->identity.physics.ground_acceleration_bits = UINT32_C(0x41200000);
	info->identity.physics.air_acceleration_bits = UINT32_C(0x3f800000);
	info->identity.physics.water_acceleration_bits = UINT32_C(0x40000000);
	info->identity.physics.hook_acceleration_bits = UINT32_C(0x40400000);
	info->identity.physics.external_acceleration_bits = UINT32_C(0x40800000);
	info->identity.physics.water_drag_bits = UINT32_C(0x3dcccccd);
	info->identity.physics.max_velocity_bits = UINT32_C(0x45fa0000);
	info->identity.physics.frame_ms = 100U;
	info->identity.physics.substep_ms = 8U;
	info->identity.weapon_profile_catalog_id = UINT64_C(19);
}

static void TestRoundTrip(const sg_rune_compact_wire_info_t *info)
{
	static const unsigned char payload[] = { 7U, 200U, 55U };
	unsigned char encoded[SG_SIDECAR_HEADER_BYTES + sizeof(payload)];
	unsigned char empty[SG_SIDECAR_HEADER_BYTES];
	unsigned char decoded[sizeof(payload)] = { 0U };
	uint32_t kind;
	size_t size = 0U;
	size_t decoded_size = 0U;
	sg_sidecar_header_t header;

	for (kind = 0U; kind < SG_SIDECAR_KIND_COUNT; kind++)
	{
		CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarFileSize(
			(sg_sidecar_kind_t)kind, info, sizeof(payload), &size));
		CHECK(size == sizeof(encoded));
		CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarEncode(
			(sg_sidecar_kind_t)kind, info, payload, sizeof(payload), encoded,
			sizeof(encoded), &size));
		CHECK(size == sizeof(encoded));
		CHECK(GetU32(encoded + 28U) == info->checksum);
		CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarInspect(encoded,
			SG_SIDECAR_HEADER_BYTES, sizeof(encoded),
			(sg_sidecar_kind_t)kind, info, &header));
		CHECK(header.rune_identity.bsp_bytes == info->identity.bsp_bytes);
		CHECK(header.rune_identity.physics.frame_ms ==
			info->identity.physics.frame_ms);
		CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarDecode(encoded, sizeof(encoded),
			(sg_sidecar_kind_t)kind, info, decoded, sizeof(decoded),
			&decoded_size));
		CHECK(decoded_size == sizeof(payload));
		CHECK(memcmp(decoded, payload, sizeof(payload)) == 0);
	}
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarEncode(SG_SIDECAR_HUMAN, info,
		NULL, 0U, empty, sizeof(empty), &size));
	CHECK(size == sizeof(empty));
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarDecode(empty, sizeof(empty),
		SG_SIDECAR_HUMAN, info, decoded, 0U, &decoded_size));
	CHECK(decoded_size == 0U);
	CHECK(strcmp(SG_SidecarKindName(SG_SIDECAR_DEFENSE), "defense") == 0);
	CHECK(SG_SidecarKindExtension(SG_SIDECAR_KIND_COUNT) == NULL);
}

static void TestBindingFailures(const sg_rune_compact_wire_info_t *info)
{
	static const unsigned char payload[] = { 7U, 200U, 55U };
	unsigned char good[SG_SIDECAR_HEADER_BYTES + sizeof(payload)];
	unsigned char bad[sizeof(good)];
	unsigned char output[sizeof(payload)] = { 0xa5U, 0xa5U, 0xa5U };
	size_t size = 0U;

	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarEncode(SG_SIDECAR_HUMAN, info,
		payload, sizeof(payload), good, sizeof(good), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 8U, SG_RUNE_COMPACT_WIRE_VERSION + UINT16_C(1));
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_VERSION_MISMATCH, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 16U, SG_RUNE_COMPACT_MODEL_SCHEMA_TAG ^ UINT32_C(1));
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_SCHEMA_MISMATCH, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU64(bad + 20U, info->image_bytes + UINT64_C(1));
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_IMAGE_MISMATCH, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 28U, info->checksum ^ UINT32_C(1));
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_CHECKSUM_MISMATCH, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	bad[32U] ^= UINT8_C(1);
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_IDENTITY_MISMATCH, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 14U, UINT16_C(1));
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_NONZERO_RESERVED, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	bad[SG_SIDECAR_HEADER_BYTES] ^= UINT8_C(1);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_CRC, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 292U, SG_SIDECAR_MAX_PAYLOAD_BYTES + UINT32_C(1));
	FixHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_SIZE, SG_SidecarDecode(bad,
		size, SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	CHECK_DIAGNOSTIC(SCD_BAD_FILE_SIZE, SG_SidecarDecode(good, size - 1U,
		SG_SIDECAR_HUMAN, info, output, sizeof(output), &size));
	CHECK(output[0] == 0xa5U && output[1] == 0xa5U && output[2] == 0xa5U);
}

int main(void)
{
	sg_rune_compact_wire_info_t info;
	int diagnostic;
	int stage;

	InitInfo(&info);
	TestRoundTrip(&info);
	TestBindingFailures(&info);
	for (diagnostic = SCD_OK; diagnostic < SCD_DIAGNOSTIC_COUNT; diagnostic++)
		CHECK(strcmp(SG_SidecarDiagnosticName(
			(sg_sidecar_diagnostic_t)diagnostic), "SCD_UNKNOWN") != 0);
	for (stage = SCS_ARGUMENT; stage < SCS_STAGE_COUNT; stage++)
		CHECK(strcmp(SG_SidecarStageName((sg_sidecar_stage_t)stage),
			"unknown") != 0);
	if (failures != 0)
	{
		fprintf(stderr, "sg_sidecar_wire_test: %d failure(s)\\n", failures);
		return 1;
	}
	puts("sg_sidecar_wire_test: PASS");
	return 0;
}
