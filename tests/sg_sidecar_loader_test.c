/* One-open, failure-atomic filesystem tests for RUNE v3 sidecars. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* q_shared.h has no include guard; include it once before loader headers. */
#include "q_shared.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_wire.h"
#include "slipgate/sg_sidecar_loader.h"
#include "slipgate/sg_sidecar_wire.h"

#define RUNE_GOLDEN_BYTES 248U
#define HMN_GOLDEN_BYTES 50U
#define TEST_DATA_BYTES 128U
#define OUTPUT_BYTES 64U

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef enum io_failure_e
{
	IO_FAIL_NONE = 0,
	IO_FAIL_OPEN,
	IO_FAIL_READ,
	IO_FAIL_SEEK,
	IO_FAIL_TELL,
	IO_FAIL_CLOSE,
	IO_FAIL_ALLOCATE
} io_failure_t;

typedef struct io_fixture_s
{
	unsigned char data[TEST_DATA_BYTES];
	size_t data_size;
	size_t position;
	size_t reported_size;
	int override_reported_size;
	io_failure_t failure;
	size_t fail_call;
	int absent;
	int is_open;
	size_t open_calls;
	size_t read_calls;
	size_t seek_calls;
	size_t tell_calls;
	size_t close_calls;
	size_t allocate_calls;
	size_t deallocate_calls;
	size_t last_allocation_size;
	size_t mutate_read_call;
	size_t mutate_offset;
	unsigned char mutate_xor;
	int allocation_order_valid;
	char opened_path[MAX_OSPATH];
} io_fixture_t;

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
		return 0;
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

static void PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static void FixHeaderCRC(unsigned char *encoded)
{
	unsigned char header[SG_SIDECAR_V3_HEADER_BYTES];
	uint32_t crc = 0;

	memcpy(header, encoded, sizeof(header));
	memset(header + SG_SIDECAR_V3_HEADER_CRC_OFFSET, 0, 4);
	CHECK(SG_CRC32Buffer(header, sizeof(header), &crc));
	PutU32(encoded + SG_SIDECAR_V3_HEADER_CRC_OFFSET, crc);
}

static int GoldenRuneHeader(sg_rune_v3_header_t *rune)
{
	unsigned char encoded[RUNE_GOLDEN_BYTES];

	return LoadHex("tests/fixtures/rune_v3_wire_golden.hex", encoded,
		sizeof(encoded)) &&
		SG_RuneV3DecodeHeader(encoded, SG_RUNE_V3_HEADER_BYTES, rune) ==
		RLW_OK;
}

static int RuneWithMap(const sg_rune_v3_header_t *source,
	const char *map_name, sg_rune_v3_header_t *rune_out)
{
	sg_rune_v3_header_t changed = *source;
	unsigned char encoded[SG_RUNE_V3_HEADER_BYTES];

	memset(changed.map_name, 0, sizeof(changed.map_name));
	if (strlen(map_name) >= sizeof(changed.map_name))
		return 0;
	memcpy(changed.map_name, map_name, strlen(map_name));
	return SG_RuneV3EncodeHeader(&changed, encoded, sizeof(encoded)) ==
		RLW_OK &&
		SG_RuneV3DecodeHeader(encoded, sizeof(encoded), rune_out) == RLW_OK;
}

static void *TestOpen(void *context, const char *path, int *error_out)
{
	io_fixture_t *io = context;

	io->open_calls++;
	*error_out = 0;
	if (io->failure == IO_FAIL_OPEN)
	{
		*error_out = EACCES;
		return NULL;
	}
	if (io->absent)
	{
		*error_out = ENOENT;
		return NULL;
	}
	CHECK(!io->is_open);
	io->is_open = 1;
	io->position = 0;
	(void)snprintf(io->opened_path, sizeof(io->opened_path), "%s", path);
	return io;
}

static size_t TestRead(void *context, void *handle, unsigned char *output,
	size_t output_size, int *error_out)
{
	io_fixture_t *io = context;
	size_t available;
	size_t count;

	io->read_calls++;
	*error_out = 0;
	CHECK(handle == io);
	CHECK(io->is_open);
	if (io->read_calls == io->mutate_read_call)
	{
		CHECK(io->mutate_offset < io->data_size);
		if (io->mutate_offset < io->data_size)
			io->data[io->mutate_offset] ^= io->mutate_xor;
	}
	if (io->failure == IO_FAIL_READ && io->read_calls == io->fail_call)
	{
		*error_out = EIO;
		return 0;
	}
	available = io->position < io->data_size
		? io->data_size - io->position : 0;
	count = output_size < available ? output_size : available;
	if (count > 0)
		memcpy(output, io->data + io->position, count);
	io->position += count;
	return count;
}

static int TestSeek(void *context, void *handle,
	sg_sidecar_seek_origin_t origin, size_t offset, int *error_out)
{
	io_fixture_t *io = context;

	io->seek_calls++;
	*error_out = 0;
	CHECK(handle == io);
	CHECK(io->is_open);
	if (io->failure == IO_FAIL_SEEK && io->seek_calls == io->fail_call)
	{
		*error_out = ESPIPE;
		return -1;
	}
	if (origin == SG_SIDECAR_SEEK_BEGIN)
	{
		io->position = offset;
		return 0;
	}
	if (origin == SG_SIDECAR_SEEK_END && offset == 0)
	{
		io->position = io->data_size;
		return 0;
	}
	*error_out = EINVAL;
	return -1;
}

static int TestTell(void *context, void *handle, size_t *offset_out,
	int *error_out)
{
	io_fixture_t *io = context;

	io->tell_calls++;
	*error_out = 0;
	CHECK(handle == io);
	CHECK(io->is_open);
	if (io->failure == IO_FAIL_TELL)
	{
		*error_out = EOVERFLOW;
		return -1;
	}
	*offset_out = io->override_reported_size
		? io->reported_size : io->position;
	return 0;
}

static int TestClose(void *context, void *handle, int *error_out)
{
	io_fixture_t *io = context;

	io->close_calls++;
	*error_out = 0;
	CHECK(handle == io);
	CHECK(io->is_open);
	io->is_open = 0;
	if (io->failure == IO_FAIL_CLOSE)
	{
		*error_out = EIO;
		return -1;
	}
	return 0;
}

static void *TestAllocate(void *context, size_t size)
{
	io_fixture_t *io = context;

	io->allocate_calls++;
	io->last_allocation_size = size;
	if (io->read_calls != 1 || io->seek_calls != 2 ||
	    io->tell_calls != 1)
		io->allocation_order_valid = 0;
	if (io->failure == IO_FAIL_ALLOCATE)
		return NULL;
	return malloc(size);
}

static void TestDeallocate(void *context, void *allocation)
{
	io_fixture_t *io = context;

	io->deallocate_calls++;
	free(allocation);
}

static sg_sidecar_load_ops_t TestOps(io_fixture_t *io)
{
	sg_sidecar_load_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.context = io;
	ops.open_read = TestOpen;
	ops.read = TestRead;
	ops.seek = TestSeek;
	ops.tell = TestTell;
	ops.close_file = TestClose;
	ops.allocate = TestAllocate;
	ops.deallocate = TestDeallocate;
	return ops;
}

static void InitIO(io_fixture_t *io, const unsigned char *data, size_t size)
{
	memset(io, 0, sizeof(*io));
	io->allocation_order_valid = 1;
	CHECK(size <= sizeof(io->data));
	if (size <= sizeof(io->data))
	{
		memcpy(io->data, data, size);
		io->data_size = size;
	}
}

static int Encode(sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune, const uint8_t *marks,
	const unsigned char *payload, size_t payload_size,
	unsigned char *encoded, size_t *encoded_size)
{
	return SG_SidecarV3Encode(kind, rune, marks,
		marks ? rune->num_seeds : 0, payload, payload_size, encoded,
		TEST_DATA_BYTES, encoded_size) == SCD_OK;
}

static unsigned char output_sentinel;

static void FillSentinel(unsigned char **output, size_t *size_out)
{
	*output = &output_sentinel;
	*size_out = SIZE_MAX - 7U;
}

static void CheckFailureAtomic(const sg_sidecar_load_result_t *result,
	sg_sidecar_diagnostic_t diagnostic, sg_sidecar_stage_t stage,
	unsigned char *output, size_t size_out)
{
	CHECK(result->diagnostic == diagnostic);
	CHECK(result->stage == stage);
	CHECK(output == &output_sentinel);
	CHECK(size_out == SIZE_MAX - 7U);
}

static sg_sidecar_load_result_t InjectedLoad(io_fixture_t *io,
	sg_sidecar_kind_t kind, const sg_rune_v3_header_t *rune,
	const uint8_t *marks, unsigned char **output,
	size_t *size_out)
{
	sg_sidecar_load_ops_t ops = TestOps(io);

	return SG_SidecarV3LoadFile("/game", kind, rune, marks,
		marks ? rune->num_seeds : 0, output, size_out, &ops);
}

static size_t PayloadBytes(sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune)
{
	size_t size = 0;

	CHECK(SG_SidecarV3FileSize(kind, rune, &size) == SCD_OK);
	return size - SG_SIDECAR_V3_HEADER_BYTES;
}

static void SafePayload(sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune, unsigned char *payload,
	size_t payload_size)
{
	size_t i;

	memset(payload, 0, payload_size);
	if (kind == SG_SIDECAR_DANGER)
	{
		for (i = 0; i < payload_size / 4U; i++)
			PutU32(payload + i * 4U, (uint32_t)(i + 1U));
	}
	else
		for (i = 0; i < payload_size; i++)
			payload[i] = (unsigned char)(i + 1U);
	(void)rune;
}

static void TestSuccessAllKinds(const sg_rune_v3_header_t *rune)
{
	static const uint8_t marks[] = { 1, 1 };
	unsigned char payload[OUTPUT_BYTES];
	unsigned char encoded[TEST_DATA_BYTES];
	unsigned char *output;
	size_t kind_value;

	for (kind_value = 0; kind_value < SG_SIDECAR_KIND_COUNT; kind_value++)
	{
		sg_sidecar_kind_t kind = (sg_sidecar_kind_t)kind_value;
		const uint8_t *kind_marks = kind >= SG_SIDECAR_DEFENSE
			? marks : NULL;
		size_t payload_size = PayloadBytes(kind, rune);
		size_t encoded_size = 0;
		size_t output_size;
		io_fixture_t io;
		sg_sidecar_load_result_t result;
		char expected_path[MAX_OSPATH];

		if (payload_size > sizeof(payload))
		{
			fprintf(stderr, "%s:%d: payload fixture exceeds buffer\n",
				__FILE__, __LINE__);
			failures++;
			continue;
		}
		SafePayload(kind, rune, payload, payload_size);
		CHECK(Encode(kind, rune, kind_marks, payload, payload_size,
			encoded, &encoded_size));
		InitIO(&io, encoded, encoded_size);
		FillSentinel(&output, &output_size);
		result = InjectedLoad(&io, kind, rune, kind_marks, &output,
			&output_size);
		CHECK(result.diagnostic == SCD_OK);
		CHECK(result.stage == SCS_DONE);
		CHECK(result.plane == SG_SIDECAR_INDEX_NONE);
		CHECK(result.index == SG_SIDECAR_INDEX_NONE);
		CHECK(result.expected_file_size == encoded_size);
		CHECK(result.observed_file_size == encoded_size);
		CHECK(result.bytes_read ==
			SG_SIDECAR_V3_HEADER_BYTES + encoded_size);
		CHECK(output_size == payload_size);
		CHECK(memcmp(output, payload, payload_size) == 0);
		CHECK(io.open_calls == 1);
		CHECK(io.read_calls == 3);
		CHECK(io.seek_calls == 2);
		CHECK(io.tell_calls == 1);
		CHECK(io.close_calls == 1);
		CHECK(io.allocate_calls == 1);
		CHECK(io.last_allocation_size == encoded_size);
		CHECK(io.allocation_order_valid);
		CHECK(io.deallocate_calls == 0);
		CHECK(!io.is_open);
		CHECK(SG_SidecarV3Path(expected_path, sizeof(expected_path),
			"/game", kind, rune) == SCD_OK);
		CHECK(strcmp(io.opened_path, expected_path) == 0);
		TestDeallocate(&io, output);
		CHECK(io.deallocate_calls == 1);
	}
}

static void TestPythonGolden(const sg_rune_v3_header_t *rune)
{
	unsigned char golden[HMN_GOLDEN_BYTES];
	unsigned char *output;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_result_t result;

	CHECK(LoadHex("tests/fixtures/sidecar_v3_hmn_golden.hex", golden,
		sizeof(golden)));
	InitIO(&io, golden, sizeof(golden));
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CHECK(result.diagnostic == SCD_OK);
	CHECK(result.stage == SCS_DONE);
	CHECK(output_size == 2);
	CHECK(output[0] == 7 && output[1] == 200);
	CHECK(io.open_calls == 1 && io.close_calls == 1);
	TestDeallocate(&io, output);
}

static void TestPathBoundaries(const sg_rune_v3_header_t *golden,
	const unsigned char *good, size_t good_size)
{
	sg_rune_v3_header_t rune;
	char root[MAX_OSPATH];
	char path[MAX_OSPATH];
	size_t root_length;
	const char *extension;
	io_fixture_t io;
	unsigned char *output;
	size_t output_size;
	sg_sidecar_load_ops_t ops;
	sg_sidecar_load_result_t result;

	CHECK(RuneWithMap(golden, "MiXeD_7", &rune));
	extension = SG_SidecarKindExtension(SG_SIDECAR_HUMAN);
	root_length = (MAX_OSPATH - 1U) - strlen("/maps/") -
		strlen(rune.map_name) - strlen(extension);
	CHECK(root_length + 1U < sizeof(root));
	memset(root, 'g', root_length);
	root[root_length] = '\0';
	CHECK(SG_SidecarV3Path(path, sizeof(path), root,
		SG_SIDECAR_HUMAN, &rune) == SCD_OK);
	CHECK(strlen(path) == MAX_OSPATH - 1U);
	CHECK(strstr(path, "/maps/MiXeD_7.hmn") != NULL);
	InitIO(&io, good, good_size);
	ops = TestOps(&io);
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile(root, SG_SIDECAR_HUMAN, &rune,
		NULL, 0, &output, &output_size, &ops);
	/* The sidecar is deliberately bound to the original golden header, so the
	 * exact-boundary path reaches one open and rejects the binding, not path. */
	CHECK(result.diagnostic == SCD_RUNE_HEADER_MISMATCH);
	CHECK(result.stage == SCS_RUNE_BINDING);
	CHECK(io.open_calls == 1 && io.close_calls == 1);
	CheckFailureAtomic(&result, SCD_RUNE_HEADER_MISMATCH,
		SCS_RUNE_BINDING, output, output_size);
	CHECK(SG_SidecarV3Path(path, MAX_OSPATH - 1U, root,
		SG_SIDECAR_HUMAN, &rune) == SCD_PATH_TOO_LONG);
	CHECK(path[0] == '\0');
	root[root_length] = 'x';
	root[root_length + 1U] = '\0';
	CHECK(SG_SidecarV3Path(path, sizeof(path), root,
		SG_SIDECAR_HUMAN, &rune) == SCD_PATH_TOO_LONG);
	CHECK(path[0] == '\0');

	InitIO(&io, good, good_size);
	ops = TestOps(&io);
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile(root, SG_SIDECAR_HUMAN, &rune,
		NULL, 0, &output, &output_size, &ops);
	CheckFailureAtomic(&result, SCD_PATH_TOO_LONG, SCS_PATH, output,
		output_size);
	CHECK(io.open_calls == 0);
}

static void TestAbsentAndArguments(const sg_rune_v3_header_t *rune,
	const unsigned char *good, size_t good_size)
{
	static const uint8_t bad_marks[] = { 1, 2 };
	unsigned char *output;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_ops_t ops;
	sg_sidecar_load_result_t result;
	sg_rune_v3_header_t bad_rune = *rune;

	InitIO(&io, good, good_size);
	io.absent = 1;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_ABSENT, SCS_OPEN, output,
		output_size);
	CHECK(result.os_error == ENOENT);
	CHECK(io.open_calls == 1 && io.close_calls == 0);

	InitIO(&io, good, good_size);
	io.failure = IO_FAIL_OPEN;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_IO_ERROR, SCS_OPEN, output,
		output_size);
	CHECK(result.os_error == EACCES);

	InitIO(&io, good, good_size);
	ops = TestOps(&io);
	ops.read = NULL;
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile("/game", SG_SIDECAR_HUMAN, rune,
		NULL, 0, &output, &output_size, &ops);
	CheckFailureAtomic(&result, SCD_INVALID_ARGUMENT, SCS_ARGUMENT,
		output, output_size);
	CHECK(io.open_calls == 0);

	InitIO(&io, good, good_size);
	ops = TestOps(&io);
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile("/game", SG_SIDECAR_HUMAN, rune,
		NULL, 0, NULL, &output_size, &ops);
	CheckFailureAtomic(&result, SCD_INVALID_ARGUMENT, SCS_ARGUMENT,
		output, output_size);
	CHECK(io.open_calls == 0);

	InitIO(&io, good, good_size);
	ops = TestOps(&io);
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile("/game", SG_SIDECAR_DEFENSE, rune,
		NULL, 0, &output, &output_size, &ops);
	CheckFailureAtomic(&result, SCD_INVALID_ARGUMENT, SCS_ARGUMENT,
		output, output_size);
	result = SG_SidecarV3LoadFile("/game", SG_SIDECAR_DEFENSE, rune,
		bad_marks, sizeof(bad_marks), &output, &output_size, &ops);
	CheckFailureAtomic(&result, SCD_INVALID_ARGUMENT, SCS_ARGUMENT,
		output, output_size);
	CHECK(io.open_calls == 0);

	bad_rune.header_crc32 ^= UINT32_C(1);
	InitIO(&io, good, good_size);
	ops = TestOps(&io);
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile("/game", SG_SIDECAR_HUMAN,
		&bad_rune, NULL, 0, &output, &output_size, &ops);
	CheckFailureAtomic(&result, SCD_INVALID_ARGUMENT, SCS_ARGUMENT,
		output, output_size);
	CHECK(io.open_calls == 0);
}

static void CheckEncodedFailure(const sg_rune_v3_header_t *rune,
	sg_sidecar_kind_t kind, const uint8_t *marks,
	const unsigned char *encoded, size_t encoded_size,
	sg_sidecar_diagnostic_t diagnostic, sg_sidecar_stage_t stage)
{
	unsigned char *output;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_result_t result;

	InitIO(&io, encoded, encoded_size);
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, kind, rune, marks, &output, &output_size);
	CheckFailureAtomic(&result, diagnostic, stage, output,
		output_size);
	CHECK(io.open_calls == 1 && io.close_calls == 1);
	CHECK(!io.is_open);
	if (stage == SCS_PAYLOAD_CRC || stage == SCS_PAYLOAD_VALUE)
		CHECK(io.allocate_calls == 1 && io.deallocate_calls == 1);
	else
		CHECK(io.allocate_calls == 0 && io.deallocate_calls == 0);
}

static void TestFormatFailures(const sg_rune_v3_header_t *rune,
	const unsigned char *good, size_t good_size)
{
	unsigned char bad[TEST_DATA_BYTES];

	memcpy(bad, good, good_size);
	bad[SG_SIDECAR_V3_HEADER_CRC_OFFSET] ^= 1;
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_BAD_HEADER_CRC, SCS_HEADER_CRC);

	memcpy(bad, good, good_size);
	PutU32(bad, SG_SIDECAR_V3_HML_MAGIC);
	FixHeaderCRC(bad);
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_BAD_MAGIC, SCS_HEADER);

	memcpy(bad, good, good_size);
	bad[10] = 2;
	FixHeaderCRC(bad);
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_BAD_SHAPE, SCS_SHAPE);

	memcpy(bad, good, good_size);
	PutU32(bad + 36, UINT32_MAX);
	FixHeaderCRC(bad);
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_BAD_PAYLOAD_SIZE, SCS_SHAPE);

	memcpy(bad, good, good_size);
	bad[24] ^= 1;
	FixHeaderCRC(bad);
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_RUNE_PAYLOAD_MISMATCH, SCS_RUNE_BINDING);

	memcpy(bad, good, good_size);
	bad[28] ^= 1;
	FixHeaderCRC(bad);
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_ACTION_CONTRACT_MISMATCH, SCS_RUNE_BINDING);

	memcpy(bad, good, good_size);
	bad[32] ^= 1;
	FixHeaderCRC(bad);
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_RUNE_HEADER_MISMATCH, SCS_RUNE_BINDING);

	memcpy(bad, good, good_size);
	bad[SG_SIDECAR_V3_HEADER_BYTES] ^= 1;
	CheckEncodedFailure(rune, SG_SIDECAR_HUMAN, NULL, bad, good_size,
		SCD_BAD_PAYLOAD_CRC, SCS_PAYLOAD_CRC);
}

static void TestTombstoneDetail(const sg_rune_v3_header_t *rune)
{
	static const uint8_t encoded_marks[] = { 1, 1 };
	static const uint8_t live_marks[] = { 1, 0 };
	unsigned char payload[OUTPUT_BYTES];
	unsigned char encoded[TEST_DATA_BYTES];
	unsigned char *output;
	size_t payload_size = PayloadBytes(SG_SIDECAR_DEFENSE, rune);
	size_t encoded_size = 0;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_result_t result;

	memset(payload, 0, sizeof(payload));
	payload[1] = 9;
	CHECK(Encode(SG_SIDECAR_DEFENSE, rune, encoded_marks, payload,
		payload_size, encoded, &encoded_size));
	InitIO(&io, encoded, encoded_size);
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_DEFENSE, rune, live_marks,
		&output, &output_size);
	CheckFailureAtomic(&result, SCD_BAD_PAYLOAD_VALUE,
		SCS_PAYLOAD_VALUE, output, output_size);
	CHECK(result.plane == 0);
	CHECK(result.index == 1);
	CHECK(io.allocate_calls == 1 && io.deallocate_calls == 1);
}

static void TestHeaderDrift(const sg_rune_v3_header_t *rune,
	const unsigned char *good, size_t good_size)
{
	unsigned char *output;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_result_t result;

	InitIO(&io, good, good_size);
	io.mutate_read_call = 2;
	io.mutate_offset = 4;
	io.mutate_xor = 1;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_STATE_DRIFT, SCS_RECHECK, output,
		output_size);
	CHECK(io.open_calls == 1 && io.close_calls == 1);
	CHECK(io.read_calls == 2);
	CHECK(io.seek_calls == 2 && io.tell_calls == 1);
	CHECK(io.allocate_calls == 1 && io.deallocate_calls == 1);
	CHECK(io.allocation_order_valid);
	CHECK(!io.is_open);
}

static void TestIOFailures(const sg_rune_v3_header_t *rune,
	const unsigned char *good, size_t good_size)
{
	unsigned char *output;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_result_t result;
	size_t case_index;
	static const struct
	{
		io_failure_t failure;
		size_t call;
		sg_sidecar_stage_t stage;
		int error;
	} cases[] = {
		{ IO_FAIL_READ, 1, SCS_HEADER_READ, EIO },
		{ IO_FAIL_SEEK, 1, SCS_FILE_SIZE, ESPIPE },
		{ IO_FAIL_TELL, 0, SCS_FILE_SIZE, EOVERFLOW },
		{ IO_FAIL_SEEK, 2, SCS_FILE_SIZE, ESPIPE },
		{ IO_FAIL_READ, 2, SCS_PAYLOAD_READ, EIO },
		{ IO_FAIL_READ, 3, SCS_PAYLOAD_READ, EIO }
	};

	for (case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]);
	    case_index++)
	{
		InitIO(&io, good, good_size);
		io.failure = cases[case_index].failure;
		io.fail_call = cases[case_index].call;
		FillSentinel(&output, &output_size);
		result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL,
			&output, &output_size);
		CheckFailureAtomic(&result, SCD_IO_ERROR,
			cases[case_index].stage, output, output_size);
		CHECK(result.os_error == cases[case_index].error);
		CHECK(io.open_calls == 1 && io.close_calls == 1);
		CHECK(!io.is_open);
	}

	InitIO(&io, good, good_size);
	io.failure = IO_FAIL_ALLOCATE;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_ALLOCATION_FAILED, SCS_ALLOCATION,
		output, output_size);
	CHECK(io.allocate_calls == 1 && io.deallocate_calls == 0);
	CHECK(io.close_calls == 1);

	InitIO(&io, good, good_size);
	io.failure = IO_FAIL_CLOSE;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_IO_ERROR, SCS_CLOSE, output,
		output_size);
	CHECK(result.os_error == EIO && result.close_error == EIO);
	CHECK(io.allocate_calls == 1 && io.deallocate_calls == 1);

	InitIO(&io, good, good_size);
	io.data[SG_SIDECAR_V3_HEADER_CRC_OFFSET] ^= 1;
	io.failure = IO_FAIL_CLOSE;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_BAD_HEADER_CRC, SCS_HEADER_CRC,
		output, output_size);
	CHECK(result.os_error == 0 && result.close_error == EIO);
}

static void TestExactFileSize(const sg_rune_v3_header_t *rune,
	const unsigned char *good, size_t good_size)
{
	unsigned char expanded[TEST_DATA_BYTES];
	unsigned char *output;
	size_t output_size;
	io_fixture_t io;
	sg_sidecar_load_result_t result;

	InitIO(&io, good, SG_SIDECAR_V3_HEADER_BYTES - 1U);
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_BAD_HEADER_SIZE, SCS_HEADER_READ,
		output, output_size);

	InitIO(&io, good, good_size - 1U);
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_BAD_FILE_SIZE, SCS_FILE_SIZE,
		output, output_size);
	CHECK(io.allocate_calls == 0);

	InitIO(&io, good, good_size - 1U);
	io.override_reported_size = 1;
	io.reported_size = good_size;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_BAD_FILE_SIZE, SCS_PAYLOAD_READ,
		output, output_size);
	CHECK(io.allocate_calls == 1 && io.deallocate_calls == 1);

	memcpy(expanded, good, good_size);
	expanded[good_size] = 0xee;
	InitIO(&io, expanded, good_size + 1U);
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_BAD_FILE_SIZE, SCS_FILE_SIZE,
		output, output_size);
	CHECK(io.allocate_calls == 0);

	InitIO(&io, expanded, good_size + 1U);
	io.override_reported_size = 1;
	io.reported_size = good_size;
	FillSentinel(&output, &output_size);
	result = InjectedLoad(&io, SG_SIDECAR_HUMAN, rune, NULL, &output,
		&output_size);
	CheckFailureAtomic(&result, SCD_BAD_FILE_SIZE, SCS_PAYLOAD_READ,
		output, output_size);
	CHECK(io.read_calls == 3);
}

static void TestDefaultOps(const sg_rune_v3_header_t *rune,
	const unsigned char *good, size_t good_size)
{
	char root[] = "/tmp/sg-sidecar-loader-XXXXXX";
	char maps[MAX_OSPATH];
	char path[MAX_OSPATH];
	unsigned char *output;
	size_t output_size;
	FILE *file;
	sg_sidecar_load_result_t result;
	sg_sidecar_load_ops_t ops;

	SG_SidecarV3DefaultLoadOps(&ops);
	CHECK(ops.context == NULL && ops.open_read && ops.read && ops.seek &&
		ops.tell && ops.close_file && ops.allocate && ops.deallocate);
	CHECK(mkdtemp(root) != NULL);
	(void)snprintf(maps, sizeof(maps), "%s/maps", root);
	CHECK(mkdir(maps, 0700) == 0);
	CHECK(SG_SidecarV3Path(path, sizeof(path), root,
		SG_SIDECAR_HUMAN, rune) == SCD_OK);
	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (file)
	{
		CHECK(fwrite(good, 1, good_size, file) == good_size);
		CHECK(fclose(file) == 0);
	}
	FillSentinel(&output, &output_size);
	result = SG_SidecarV3LoadFile(root, SG_SIDECAR_HUMAN, rune,
		NULL, 0, &output, &output_size, NULL);
	CHECK(result.diagnostic == SCD_OK && result.stage == SCS_DONE);
	CHECK(output_size == 2 && output[0] == 7 && output[1] == 200);
	free(output);
	CHECK(unlink(path) == 0);
	CHECK(rmdir(maps) == 0);
	CHECK(rmdir(root) == 0);
}

int main(void)
{
	sg_rune_v3_header_t rune;
	unsigned char golden[HMN_GOLDEN_BYTES];

	if (!GoldenRuneHeader(&rune) ||
	    !LoadHex("tests/fixtures/sidecar_v3_hmn_golden.hex", golden,
	        sizeof(golden)))
	{
		fputs("sg_sidecar_loader_test: fixture load failed\n", stderr);
		return 1;
	}
	CHECK(rune.num_seeds == 2 && rune.num_links == 2);
	TestSuccessAllKinds(&rune);
	TestPythonGolden(&rune);
	TestPathBoundaries(&rune, golden, sizeof(golden));
	TestAbsentAndArguments(&rune, golden, sizeof(golden));
	TestFormatFailures(&rune, golden, sizeof(golden));
	TestTombstoneDetail(&rune);
	TestHeaderDrift(&rune, golden, sizeof(golden));
	TestIOFailures(&rune, golden, sizeof(golden));
	TestExactFileSize(&rune, golden, sizeof(golden));
	TestDefaultOps(&rune, golden, sizeof(golden));
	if (failures)
	{
		fprintf(stderr, "sg_sidecar_loader_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_sidecar_loader_test: PASS");
	return 0;
}
