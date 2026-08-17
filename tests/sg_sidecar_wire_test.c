/* Artifact-bound sidecar codec tests. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* q_shared.h has no include guard; include it once before sidecar headers. */
#include "q_shared.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_sidecar_wire.h"

#define SIDECAR_IMAGE_BYTES (SG_SIDECAR_HEADER_BYTES + 2U)

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	sg_sidecar_diagnostic_t actual_ = (expression); \
	if (actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: expected diagnostic %d, got %d: %s\n", \
		        __FILE__, __LINE__, (int)(expected), (int)actual_, \
		        #expression); \
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

static uint32_t GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static void FixSidecarHeaderCRC(unsigned char *bytes)
{
	unsigned char copy[SG_SIDECAR_HEADER_BYTES];
	uint32_t crc;

	memcpy(copy, bytes, sizeof(copy));
	memset(copy + SG_SIDECAR_HEADER_CRC_OFFSET, 0, 4);
	CHECK(SG_CRC32Buffer(copy, sizeof(copy), &crc));
	PutU32(bytes + SG_SIDECAR_HEADER_CRC_OFFSET, crc);
}

static void InitArtifact(rune_artifact_t *artifact)
{
	memset(artifact, 0, sizeof(*artifact));
	artifact->magic = RUNE_ARTIFACT_MAGIC;
	artifact->payload_crc32 = UINT32_C(0x11223344);
	artifact->header_crc32 = UINT32_C(0x55667788);
	artifact->action_contract_crc32 = SG_RUNE_ACTION_CONTRACT_CRC32;
	artifact->mechanism_contract_crc32 =
		SG_RUNE_MECHANISM_CONTRACT_CRC32;
	artifact->num_seeds = 2U;
	artifact->num_links = 2U;
	artifact->num_mechanism_nodes = 2U;
	artifact->num_mechanism_edges = 2U;
	artifact->num_inventory_edges = 1U;
	artifact->num_mechanism_plans = 1U;
	artifact->string_bytes = 8U;
	memcpy(artifact->identity.map_name, "sidecar", sizeof("sidecar"));
}

static sg_sidecar_diagnostic_t EncodeHuman(
	const rune_artifact_t *rune, const unsigned char *payload,
	size_t payload_size, unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out)
{
	return SG_SidecarEncode(SG_SIDECAR_HUMAN, rune, NULL, 0,
		payload, payload_size, encoded, encoded_capacity,
		encoded_size_out);
}

static sg_sidecar_diagnostic_t DecodeHuman(const unsigned char *encoded,
	size_t encoded_size, const rune_artifact_t *rune,
	unsigned char *payload_out, size_t payload_capacity,
	size_t *payload_size_out)
{
	return SG_SidecarDecode(encoded, encoded_size, SG_SIDECAR_HUMAN,
		rune, NULL, 0, payload_out, payload_capacity, payload_size_out);
}

static void TestRoundTrip(const rune_artifact_t *rune)
{
	static const unsigned char payload[] = { 7, 200 };
	unsigned char encoded[SIDECAR_IMAGE_BYTES];
	unsigned char decoded[sizeof(payload)] = { 0, 0 };
	size_t encoded_size = 0;
	size_t decoded_size = 0;
	size_t file_size = 0;
	sg_sidecar_header_t header;

	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarFileSize(SG_SIDECAR_HUMAN,
		rune, &file_size));
	CHECK(file_size == sizeof(encoded));
	CHECK_DIAGNOSTIC(SCD_OK, EncodeHuman(rune, payload, sizeof(payload),
		encoded, sizeof(encoded), &encoded_size));
	CHECK(encoded_size == sizeof(encoded));
	CHECK(memcmp(encoded, "HMNR", 4) == 0);
	CHECK(encoded[4] == 0U && encoded[5] == 0U);
	CHECK(encoded[8] == 0U && encoded[9] == 0U);
	CHECK(GetU32(encoded + 24) == rune->payload_crc32);
	CHECK(GetU32(encoded + 28) == rune->action_contract_crc32);
	CHECK(GetU32(encoded + 32) == rune->header_crc32);
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarInspect(encoded,
		SG_SIDECAR_HEADER_BYTES, sizeof(encoded),
		SG_SIDECAR_HUMAN, rune, &header));
	CHECK(header.payload_bytes == sizeof(payload));
	CHECK_DIAGNOSTIC(SCD_OK, DecodeHuman(encoded, sizeof(encoded), rune,
		decoded, sizeof(decoded), &decoded_size));
	CHECK(decoded_size == sizeof(payload));
	CHECK(memcmp(decoded, payload, sizeof(payload)) == 0);
}

static void TestHeaderFailures(const rune_artifact_t *rune)
{
	static const unsigned char payload[] = { 7, 200 };
	unsigned char good[SIDECAR_IMAGE_BYTES];
	unsigned char bad[SIDECAR_IMAGE_BYTES];
	unsigned char output[2] = { 0xaa, 0xbb };
	size_t size = 0;
	sg_sidecar_header_t header;
	sg_sidecar_header_t sentinel;

	CHECK_DIAGNOSTIC(SCD_OK, EncodeHuman(rune, payload, sizeof(payload),
		good, sizeof(good), &size));
	memset(&sentinel, 0xa5, sizeof(sentinel));
	header = sentinel;
	CHECK_DIAGNOSTIC(SCD_BAD_HEADER_SIZE, SG_SidecarInspect(good,
		SG_SIDECAR_HEADER_BYTES - 1, sizeof(good),
		SG_SIDECAR_HUMAN, rune, &header));
	CHECK(memcmp(&header, &sentinel, sizeof(header)) == 0);

	memcpy(bad, good, sizeof(bad));
	bad[0] ^= 1;
	CHECK_DIAGNOSTIC(SCD_BAD_MAGIC, DecodeHuman(bad, sizeof(bad), rune,
		output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 4, 1U);
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_NONZERO_RESERVED, DecodeHuman(bad,
		sizeof(bad), rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 6, 47);
	CHECK_DIAGNOSTIC(SCD_BAD_HEADER_SIZE, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 8, 1U);
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_NONZERO_RESERVED, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	bad[15] = 1;
	CHECK_DIAGNOSTIC(SCD_BAD_HEADER_CRC, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_NONZERO_RESERVED, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 10, 2);
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_BAD_SHAPE, DecodeHuman(bad, sizeof(bad), rune,
		output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 16, rune->num_seeds + 1);
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_BAD_COUNTS, DecodeHuman(bad, sizeof(bad), rune,
		output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 28, rune->action_contract_crc32 ^ UINT32_C(1));
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_ACTION_CONTRACT_MISMATCH, DecodeHuman(bad,
		sizeof(bad), rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 24, rune->payload_crc32 ^ UINT32_C(1));
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_PAYLOAD_MISMATCH, DecodeHuman(bad,
		sizeof(bad), rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 32, rune->header_crc32 ^ UINT32_C(1));
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_RUNE_HEADER_MISMATCH, DecodeHuman(bad,
		sizeof(bad), rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU32(bad + 36, 1);
	FixSidecarHeaderCRC(bad);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_SIZE, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	CHECK_DIAGNOSTIC(SCD_BAD_FILE_SIZE, DecodeHuman(good,
		sizeof(good) - 1, rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	bad[SG_SIDECAR_HEADER_BYTES] ^= 1;
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_CRC, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	CHECK(output[0] == 0xaa && output[1] == 0xbb);
}

static void TestKindsAndDanger(rune_artifact_t rune)
{
	unsigned char danger[16];
	unsigned char defense[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	unsigned char encoded[SG_SIDECAR_HEADER_BYTES + sizeof(danger)];
	unsigned char decoded[sizeof(danger)];
	unsigned char too_small[1];
	uint8_t live_marks[2] = { 1, 1 };
	uint32_t plane = SG_SIDECAR_INDEX_NONE;
	uint32_t index = SG_SIDECAR_INDEX_NONE;
	size_t size;
	size_t i;
	int diagnostic;
	int stage;

	CHECK(strcmp(SG_SidecarKindName(SG_SIDECAR_DEFENSE), "defense") == 0);
	CHECK(strcmp(SG_SidecarKindExtension(SG_SIDECAR_DANGER),
		".rune.danger") == 0);
	CHECK(SG_SidecarKindExtension(SG_SIDECAR_KIND_COUNT) == NULL);
	for (diagnostic = SCD_OK; diagnostic < SCD_DIAGNOSTIC_COUNT;
	    diagnostic++)
	{
		CHECK(strcmp(SG_SidecarDiagnosticName(
			(sg_sidecar_diagnostic_t)diagnostic), "SCD_UNKNOWN") != 0);
		CHECK(strcmp(SG_SidecarDiagnosticMessage(
			(sg_sidecar_diagnostic_t)diagnostic),
			"unknown sidecar diagnostic") != 0);
	}
	for (stage = SCS_ARGUMENT; stage < SCS_STAGE_COUNT; stage++)
		CHECK(strcmp(SG_SidecarStageName((sg_sidecar_stage_t)stage),
			"unknown") != 0);
	for (i = 0; i < SG_SIDECAR_KIND_COUNT; i++)
		CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarFileSize(
			(sg_sidecar_kind_t)i, &rune, &size));

	/* Two planes times two seeds, encoded as signed little-endian int32. */
	PutU32(danger + 0, 0);
	PutU32(danger + 4, SG_SIDECAR_DANGER_MAX);
	PutU32(danger + 8, 1200);
	PutU32(danger + 12, 1);
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarEncode(SG_SIDECAR_DANGER,
		&rune, live_marks, 2, danger, sizeof(danger), encoded,
		sizeof(encoded), &size));
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarDecode(encoded, size,
		SG_SIDECAR_DANGER, &rune, live_marks, 2, decoded,
		sizeof(decoded), &size));
	CHECK(memcmp(decoded, danger, sizeof(danger)) == 0);
	PutU32(danger + 4, SG_SIDECAR_DANGER_MAX + 1);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE, SG_SidecarEncode(
		SG_SIDECAR_DANGER, &rune, live_marks, 2, danger, sizeof(danger),
		encoded, sizeof(encoded), &size));
	PutU32(danger + 4, UINT32_MAX);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE, SG_SidecarEncode(
		SG_SIDECAR_DANGER, &rune, live_marks, 2, danger, sizeof(danger),
		encoded, sizeof(encoded), &size));

	/* Every seed-indexed plane retains its slot but tombstones stay zero. */
	live_marks[1] = 0;
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE,
		SG_SidecarValidatePayload(SG_SIDECAR_DEFENSE, &rune,
			live_marks, 2, defense, sizeof(defense), &plane, &index));
	CHECK(plane == 0 && index == 1);
	defense[1] = defense[3] = defense[5] = defense[7] = 0;
	CHECK_DIAGNOSTIC(SCD_OK,
		SG_SidecarValidatePayload(SG_SIDECAR_DEFENSE, &rune,
			live_marks, 2, defense, sizeof(defense), &plane, &index));
	CHECK(plane == SG_SIDECAR_INDEX_NONE &&
		index == SG_SIDECAR_INDEX_NONE);
	PutU32(danger + 4, 1);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE,
		SG_SidecarValidatePayload(SG_SIDECAR_DANGER, &rune,
			live_marks, 2, danger, sizeof(danger), &plane, &index));
	CHECK(plane == 0 && index == 1);

	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_SIZE, EncodeHuman(&rune, too_small,
		sizeof(too_small), encoded, sizeof(encoded), &size));
	CHECK_DIAGNOSTIC(SCD_INVALID_ARGUMENT, SG_SidecarFileSize(
		SG_SIDECAR_KIND_COUNT, &rune, &size));

	/* Exact maxima remain representable for every fixed kind. */
	rune.num_seeds = RUNE_MAX_SEEDS;
	rune.num_links = RUNE_MAX_LINKS;
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarFileSize(SG_SIDECAR_HUMAN,
		&rune, &size));
	CHECK(size == SG_SIDECAR_HEADER_BYTES + RUNE_MAX_LINKS);
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarFileSize(SG_SIDECAR_DANGER,
		&rune, &size));
	CHECK(size == SG_SIDECAR_HEADER_BYTES +
		(size_t)RUNE_MAX_SEEDS * 8U);
}

int main(void)
{
	rune_artifact_t rune;

	InitArtifact(&rune);
	TestRoundTrip(&rune);
	TestHeaderFailures(&rune);
	TestKindsAndDanger(rune);
	if (failures)
	{
		fprintf(stderr, "sg_sidecar_wire_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_sidecar_wire_test: PASS");
	return 0;
}
