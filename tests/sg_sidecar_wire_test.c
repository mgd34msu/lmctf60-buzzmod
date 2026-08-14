/* Golden-vector and fail-closed tests for authenticated RUNE v3 sidecars. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_wire.h"
#include "slipgate/sg_sidecar_wire.h"

#define RUNE_GOLDEN_BYTES 248U
#define SIDECAR_GOLDEN_BYTES 50U

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

static int LoadHex(const char *path, unsigned char *bytes, size_t size)
{
	FILE *file = fopen(path, "rb");
	size_t count = 0;
	int high = -1;
	int c;

	if (!file)
	{
		perror(path);
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
			if (count >= size)
			{
				fclose(file);
				return 0;
			}
			bytes[count++] = (unsigned char)((high << 4) | digit);
			high = -1;
		}
	}
	return fclose(file) == 0 && count == size && high < 0;
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

static void FixSidecarHeaderCRC(unsigned char *bytes)
{
	unsigned char copy[SG_SIDECAR_V3_HEADER_BYTES];
	uint32_t crc;

	memcpy(copy, bytes, sizeof(copy));
	memset(copy + SG_SIDECAR_V3_HEADER_CRC_OFFSET, 0, 4);
	CHECK(SG_CRC32Buffer(copy, sizeof(copy), &crc));
	PutU32(bytes + SG_SIDECAR_V3_HEADER_CRC_OFFSET, crc);
}

static int RuneHeaderFromGolden(sg_rune_v3_header_t *header)
{
	unsigned char rune[RUNE_GOLDEN_BYTES];

	if (!LoadHex("tests/fixtures/rune_v3_wire_golden.hex", rune,
	    sizeof(rune)))
		return 0;
	return SG_RuneV3DecodeHeader(rune, SG_RUNE_V3_HEADER_BYTES,
		header) == RLW_OK;
}

static sg_sidecar_diagnostic_t EncodeHuman(
	const sg_rune_v3_header_t *rune, const unsigned char *payload,
	size_t payload_size, unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out)
{
	return SG_SidecarV3Encode(SG_SIDECAR_HUMAN, rune, NULL, 0,
		payload, payload_size, encoded, encoded_capacity,
		encoded_size_out);
}

static sg_sidecar_diagnostic_t DecodeHuman(const unsigned char *encoded,
	size_t encoded_size, const sg_rune_v3_header_t *rune,
	unsigned char *payload_out, size_t payload_capacity,
	size_t *payload_size_out)
{
	return SG_SidecarV3Decode(encoded, encoded_size, SG_SIDECAR_HUMAN,
		rune, NULL, 0, payload_out, payload_capacity, payload_size_out);
}

static void TestGolden(const sg_rune_v3_header_t *rune)
{
	static const unsigned char payload[] = { 7, 200 };
	unsigned char expected[SIDECAR_GOLDEN_BYTES];
	unsigned char encoded[SIDECAR_GOLDEN_BYTES];
	unsigned char decoded[sizeof(payload)] = { 0, 0 };
	size_t encoded_size = 0;
	size_t decoded_size = 0;
	size_t file_size = 0;
	sg_sidecar_v3_header_t header;

	CHECK(LoadHex("tests/fixtures/sidecar_v3_hmn_golden.hex", expected,
		sizeof(expected)));
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3FileSize(SG_SIDECAR_HUMAN,
		rune, &file_size));
	CHECK(file_size == sizeof(encoded));
	CHECK_DIAGNOSTIC(SCD_OK, EncodeHuman(rune, payload, sizeof(payload),
		encoded, sizeof(encoded), &encoded_size));
	CHECK(encoded_size == sizeof(encoded));
	CHECK(memcmp(encoded, expected, sizeof(encoded)) == 0);
	CHECK(memcmp(encoded, "HMN3", 4) == 0);
	CHECK(GetU32(encoded + 24) == rune->payload_crc32);
	CHECK(GetU32(encoded + 28) == rune->action_contract_crc32);
	CHECK(GetU32(encoded + 32) == rune->header_crc32);
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3Inspect(encoded,
		SG_SIDECAR_V3_HEADER_BYTES, sizeof(encoded),
		SG_SIDECAR_HUMAN, rune, &header));
	CHECK(header.payload_bytes == sizeof(payload));
	CHECK_DIAGNOSTIC(SCD_OK, DecodeHuman(encoded, sizeof(encoded), rune,
		decoded, sizeof(decoded), &decoded_size));
	CHECK(decoded_size == sizeof(payload));
	CHECK(memcmp(decoded, payload, sizeof(payload)) == 0);
}

static void TestHeaderFailures(const sg_rune_v3_header_t *rune)
{
	static const unsigned char payload[] = { 7, 200 };
	unsigned char good[SIDECAR_GOLDEN_BYTES];
	unsigned char bad[SIDECAR_GOLDEN_BYTES];
	unsigned char output[2] = { 0xaa, 0xbb };
	size_t size = 0;
	sg_sidecar_v3_header_t header;
	sg_sidecar_v3_header_t sentinel;

	CHECK_DIAGNOSTIC(SCD_OK, EncodeHuman(rune, payload, sizeof(payload),
		good, sizeof(good), &size));
	memset(&sentinel, 0xa5, sizeof(sentinel));
	header = sentinel;
	CHECK_DIAGNOSTIC(SCD_BAD_HEADER_SIZE, SG_SidecarV3Inspect(good,
		SG_SIDECAR_V3_HEADER_BYTES - 1, sizeof(good),
		SG_SIDECAR_HUMAN, rune, &header));
	CHECK(memcmp(&header, &sentinel, sizeof(header)) == 0);

	memcpy(bad, good, sizeof(bad));
	bad[0] ^= 1;
	CHECK_DIAGNOSTIC(SCD_BAD_MAGIC, DecodeHuman(bad, sizeof(bad), rune,
		output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 4, 2);
	CHECK_DIAGNOSTIC(SCD_UNSUPPORTED_VERSION, DecodeHuman(bad,
		sizeof(bad), rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 6, 47);
	CHECK_DIAGNOSTIC(SCD_BAD_HEADER_SIZE, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	memcpy(bad, good, sizeof(bad));
	PutU16(bad + 8, 2);
	CHECK_DIAGNOSTIC(SCD_BAD_RUNE_VERSION, DecodeHuman(bad, sizeof(bad),
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
	bad[SG_SIDECAR_V3_HEADER_BYTES] ^= 1;
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_CRC, DecodeHuman(bad, sizeof(bad),
		rune, output, sizeof(output), &size));
	CHECK(output[0] == 0xaa && output[1] == 0xbb);
}

static void TestKindsAndDanger(sg_rune_v3_header_t rune)
{
	unsigned char danger[16];
	unsigned char defense[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	unsigned char encoded[SG_SIDECAR_V3_HEADER_BYTES + sizeof(danger)];
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
	CHECK(SG_SidecarDiagnosticWire(SCD_BAD_PAYLOAD_CRC) ==
		RLW_BAD_PAYLOAD_CRC);
	CHECK(SG_SidecarDiagnosticWire(SCD_RUNE_HEADER_MISMATCH) ==
		RLW_BAD_SIDECAR);
	for (i = 0; i < SG_SIDECAR_KIND_COUNT; i++)
		CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3FileSize(
			(sg_sidecar_kind_t)i, &rune, &size));

	/* Two planes times two seeds, encoded as signed little-endian int32. */
	PutU32(danger + 0, 0);
	PutU32(danger + 4, SG_SIDECAR_V3_DANGER_MAX);
	PutU32(danger + 8, 1200);
	PutU32(danger + 12, 1);
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3Encode(SG_SIDECAR_DANGER,
		&rune, live_marks, 2, danger, sizeof(danger), encoded,
		sizeof(encoded), &size));
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3Decode(encoded, size,
		SG_SIDECAR_DANGER, &rune, live_marks, 2, decoded,
		sizeof(decoded), &size));
	CHECK(memcmp(decoded, danger, sizeof(danger)) == 0);
	PutU32(danger + 4, SG_SIDECAR_V3_DANGER_MAX + 1);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE, SG_SidecarV3Encode(
		SG_SIDECAR_DANGER, &rune, live_marks, 2, danger, sizeof(danger),
		encoded, sizeof(encoded), &size));
	PutU32(danger + 4, UINT32_MAX);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE, SG_SidecarV3Encode(
		SG_SIDECAR_DANGER, &rune, live_marks, 2, danger, sizeof(danger),
		encoded, sizeof(encoded), &size));

	/* Every seed-indexed plane retains its slot but tombstones stay zero. */
	live_marks[1] = 0;
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE,
		SG_SidecarV3ValidatePayload(SG_SIDECAR_DEFENSE, &rune,
			live_marks, 2, defense, sizeof(defense), &plane, &index));
	CHECK(plane == 0 && index == 1);
	defense[1] = defense[3] = defense[5] = defense[7] = 0;
	CHECK_DIAGNOSTIC(SCD_OK,
		SG_SidecarV3ValidatePayload(SG_SIDECAR_DEFENSE, &rune,
			live_marks, 2, defense, sizeof(defense), &plane, &index));
	CHECK(plane == SG_SIDECAR_INDEX_NONE &&
		index == SG_SIDECAR_INDEX_NONE);
	PutU32(danger + 4, 1);
	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_VALUE,
		SG_SidecarV3ValidatePayload(SG_SIDECAR_DANGER, &rune,
			live_marks, 2, danger, sizeof(danger), &plane, &index));
	CHECK(plane == 0 && index == 1);

	CHECK_DIAGNOSTIC(SCD_BAD_PAYLOAD_SIZE, EncodeHuman(&rune, too_small,
		sizeof(too_small), encoded, sizeof(encoded), &size));
	CHECK_DIAGNOSTIC(SCD_INVALID_ARGUMENT, SG_SidecarV3FileSize(
		SG_SIDECAR_KIND_COUNT, &rune, &size));

	/* Exact maxima remain representable for every fixed kind. */
	rune.num_seeds = SG_RUNE_V3_MAX_SEEDS;
	rune.num_links = SG_RUNE_V3_MAX_LINKS;
	{
		unsigned char raw[SG_RUNE_V3_HEADER_BYTES];

		CHECK(SG_RuneV3EncodeHeader(&rune, raw, sizeof(raw)) == RLW_OK);
		rune.header_crc32 = GetU32(raw + SG_RUNE_V3_HEADER_CRC_OFFSET);
	}
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3FileSize(SG_SIDECAR_HUMAN,
		&rune, &size));
	CHECK(size == SG_SIDECAR_V3_HEADER_BYTES + SG_RUNE_V3_MAX_LINKS);
	CHECK_DIAGNOSTIC(SCD_OK, SG_SidecarV3FileSize(SG_SIDECAR_DANGER,
		&rune, &size));
	CHECK(size == SG_SIDECAR_V3_HEADER_BYTES +
		(size_t)SG_RUNE_V3_MAX_SEEDS * 8U);
}

int main(void)
{
	sg_rune_v3_header_t rune;

	if (!RuneHeaderFromGolden(&rune))
	{
		fprintf(stderr, "could not decode the shared RUNE v3 golden\n");
		return 1;
	}
	TestGolden(&rune);
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
