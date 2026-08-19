/* sg_sidecar_loader.c -- one-open immutable sidecar snapshot lifecycle. */
#include "q_shared.h"
#include "slipgate/sg_sidecar_loader.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Loader_Error(int reported)
{
	return reported != 0 ? reported : EIO;
}

static void *Loader_DefaultOpen(void *context, const char *path,
	int *os_error_out)
{
	FILE *file;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	errno = 0;
	file = fopen(path, "rb");
	if (!file && os_error_out)
		*os_error_out = Loader_Error(errno);
	return file;
}

static size_t Loader_DefaultRead(void *context, void *handle,
	unsigned char *output, size_t output_size, int *os_error_out)
{
	FILE *file = handle;
	size_t count;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	errno = 0;
	count = fread(output, 1, output_size, file);
	if (count != output_size && ferror(file) && os_error_out)
		*os_error_out = Loader_Error(errno);
	return count;
}

static int Loader_DefaultSeek(void *context, void *handle,
	sg_sidecar_seek_origin_t origin, size_t offset, int *os_error_out)
{
	FILE *file = handle;
	int whence;
	int status;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (offset > (size_t)LONG_MAX ||
	    (origin != SG_SIDECAR_SEEK_BEGIN &&
	     origin != SG_SIDECAR_SEEK_END))
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return -1;
	}
	whence = origin == SG_SIDECAR_SEEK_BEGIN ? SEEK_SET : SEEK_END;
	errno = 0;
	status = fseek(file, (long)offset, whence);
	if (status != 0 && os_error_out)
		*os_error_out = Loader_Error(errno);
	return status;
}

static int Loader_DefaultTell(void *context, void *handle,
	size_t *offset_out, int *os_error_out)
{
	FILE *file = handle;
	long position;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (!offset_out)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return -1;
	}
	errno = 0;
	position = ftell(file);
	if (position < 0)
	{
		if (os_error_out)
			*os_error_out = Loader_Error(errno);
		return -1;
	}
	*offset_out = (size_t)position;
	return 0;
}

static int Loader_DefaultClose(void *context, void *handle,
	int *os_error_out)
{
	int status;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	errno = 0;
	status = fclose(handle);
	if (status != 0 && os_error_out)
		*os_error_out = Loader_Error(errno);
	return status;
}

static void *Loader_DefaultAllocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

static void Loader_DefaultDeallocate(void *context, void *allocation)
{
	(void)context;
	free(allocation);
}

void SG_SidecarDefaultLoadOps(sg_sidecar_load_ops_t *ops_out)
{
	if (!ops_out)
		return;
	memset(ops_out, 0, sizeof(*ops_out));
	ops_out->open_read = Loader_DefaultOpen;
	ops_out->read = Loader_DefaultRead;
	ops_out->seek = Loader_DefaultSeek;
	ops_out->tell = Loader_DefaultTell;
	ops_out->close_file = Loader_DefaultClose;
	ops_out->allocate = Loader_DefaultAllocate;
	ops_out->deallocate = Loader_DefaultDeallocate;
}

static int Loader_OpsValid(const sg_sidecar_load_ops_t *ops)
{
	return ops && ops->open_read && ops->read && ops->seek && ops->tell &&
	       ops->close_file && ops->allocate && ops->deallocate;
}

sg_sidecar_diagnostic_t SG_SidecarPath(char *output, size_t output_size,
	const char *game_directory, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact)
{
	const char *extension;
	size_t ignored_size;
	int written;

	if (output && output_size > 0U)
		output[0] = '\0';
	if (!output || output_size == 0U || !game_directory ||
	    game_directory[0] == '\0' || !artifact)
		return SCD_INVALID_ARGUMENT;
	extension = SG_SidecarKindExtension(kind);
	if (!extension || SG_SidecarFileSize(kind, artifact, &ignored_size) !=
	    SCD_OK)
		return SCD_INVALID_ARGUMENT;
	written = snprintf(output, output_size, "%s/maps/%s%s",
		game_directory, artifact->identity.map_name, extension);
	if (written < 0 || (size_t)written >= output_size)
	{
		output[0] = '\0';
		return SCD_PATH_TOO_LONG;
	}
	return SCD_OK;
}

static sg_sidecar_load_result_t Loader_Result(void)
{
	sg_sidecar_load_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = SCD_INVALID_ARGUMENT;
	result.stage = SCS_ARGUMENT;
	result.plane = SG_SIDECAR_INDEX_NONE;
	result.index = SG_SIDECAR_INDEX_NONE;
	return result;
}

static sg_sidecar_stage_t Loader_InspectStage(
	sg_sidecar_diagnostic_t diagnostic)
{
	switch (diagnostic)
	{
	case SCD_BAD_HEADER_CRC:
		return SCS_HEADER_CRC;
	case SCD_NONZERO_RESERVED:
	case SCD_BAD_SHAPE:
	case SCD_BAD_COUNTS:
	case SCD_BAD_PAYLOAD_SIZE:
		return SCS_SHAPE;
	case SCD_RUNE_PAYLOAD_MISMATCH:
	case SCD_ACTION_CONTRACT_MISMATCH:
	case SCD_RUNE_HEADER_MISMATCH:
		return SCS_RUNE_BINDING;
	case SCD_BAD_FILE_SIZE:
		return SCS_FILE_SIZE;
	default:
		return SCS_HEADER;
	}
}

static sg_sidecar_stage_t Loader_DecodeStage(
	sg_sidecar_diagnostic_t diagnostic)
{
	if (diagnostic == SCD_BAD_PAYLOAD_CRC)
		return SCS_PAYLOAD_CRC;
	if (diagnostic == SCD_BAD_PAYLOAD_VALUE)
		return SCS_PAYLOAD_VALUE;
	return Loader_InspectStage(diagnostic);
}

static int Loader_ArtifactSeedMarksValid(sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, const uint8_t *marks,
	size_t mark_capacity)
{
	uint32_t seed;

	if (kind != SG_SIDECAR_DEFENSE && kind != SG_SIDECAR_DANGER)
		return 1;
	if (!artifact || !marks || mark_capacity < artifact->num_seeds)
		return 0;
	for (seed = 0U; seed < artifact->num_seeds; seed++)
		if (marks[seed] > 1U)
			return 0;
	return 1;
}

sg_sidecar_load_result_t SG_SidecarLoadFile(
	const char *game_directory, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact,
	const uint8_t *live_seed_marks, size_t live_seed_capacity,
	unsigned char **payload_out, size_t *payload_size_out,
	const sg_sidecar_load_ops_t *provided_ops)
{
	sg_sidecar_load_result_t result = Loader_Result();
	sg_sidecar_load_ops_t default_ops;
	const sg_sidecar_load_ops_t *ops = provided_ops;
	sg_sidecar_header_t inspected;
	unsigned char header[SG_SIDECAR_HEADER_BYTES];
	unsigned char trailing;
	unsigned char *snapshot = NULL;
	void *file = NULL;
	char path[MAX_OSPATH];
	size_t expected_file_size = 0;
	size_t expected_payload_size;
	size_t count;
	size_t decoded_size = 0;
	int os_error = 0;
	int close_status;
	int primary_failure = 0;

	if (!ops)
	{
		SG_SidecarDefaultLoadOps(&default_ops);
		ops = &default_ops;
	}
	if (!game_directory || !artifact || !payload_out || !payload_size_out ||
	    !Loader_OpsValid(ops) ||
	    SG_SidecarFileSize(kind, artifact, &expected_file_size) != SCD_OK ||
	    expected_file_size < SG_SIDECAR_HEADER_BYTES)
		return result;
	expected_payload_size = expected_file_size - SG_SIDECAR_HEADER_BYTES;
	result.expected_file_size = expected_file_size;
	if (!Loader_ArtifactSeedMarksValid(kind, artifact, live_seed_marks,
	        live_seed_capacity))
		return result;
	result.diagnostic = SG_SidecarPath(path, sizeof(path), game_directory,
		kind, artifact);
	if (result.diagnostic != SCD_OK)
	{
		result.stage = SCS_PATH;
		return result;
	}

	os_error = 0;
	file = ops->open_read(ops->context, path, &os_error);
	if (!file)
	{
		result.os_error = Loader_Error(os_error);
		result.diagnostic = result.os_error == ENOENT
			? SCD_ABSENT : SCD_IO_ERROR;
		result.stage = SCS_OPEN;
		return result;
	}

	os_error = 0;
	count = ops->read(ops->context, file, header, sizeof(header),
		&os_error);
	result.bytes_read = count;
	if (count != sizeof(header))
	{
		result.os_error = os_error;
		result.diagnostic = os_error != 0
			? SCD_IO_ERROR : SCD_BAD_HEADER_SIZE;
		result.stage = SCS_HEADER_READ;
		primary_failure = 1;
		goto close_file;
	}

	os_error = 0;
	if (ops->seek(ops->context, file, SG_SIDECAR_SEEK_END, 0,
	    &os_error) != 0)
	{
		result.diagnostic = SCD_IO_ERROR;
		result.stage = SCS_FILE_SIZE;
		result.os_error = Loader_Error(os_error);
		primary_failure = 1;
		goto close_file;
	}
	os_error = 0;
	if (ops->tell(ops->context, file, &result.observed_file_size,
	    &os_error) != 0)
	{
		result.diagnostic = SCD_IO_ERROR;
		result.stage = SCS_FILE_SIZE;
		result.os_error = Loader_Error(os_error);
		primary_failure = 1;
		goto close_file;
	}
	result.diagnostic = SG_SidecarInspect(header, sizeof(header),
		result.observed_file_size, kind, artifact, &inspected);
	if (result.diagnostic != SCD_OK)
	{
		result.stage = Loader_InspectStage(result.diagnostic);
		primary_failure = 1;
		goto close_file;
	}
	os_error = 0;
	if (ops->seek(ops->context, file, SG_SIDECAR_SEEK_BEGIN,
	    0, &os_error) != 0)
	{
		result.diagnostic = SCD_IO_ERROR;
		result.stage = SCS_FILE_SIZE;
		result.os_error = Loader_Error(os_error);
		primary_failure = 1;
		goto close_file;
	}

	snapshot = ops->allocate(ops->context, expected_file_size);
	if (!snapshot)
	{
		result.diagnostic = SCD_ALLOCATION_FAILED;
		result.stage = SCS_ALLOCATION;
		primary_failure = 1;
		goto close_file;
	}
	os_error = 0;
	count = ops->read(ops->context, file, snapshot, expected_file_size,
		&os_error);
	result.bytes_read += count;
	if (count != expected_file_size)
	{
		result.os_error = os_error;
		result.diagnostic = os_error != 0
			? SCD_IO_ERROR : SCD_BAD_FILE_SIZE;
		result.stage = SCS_PAYLOAD_READ;
		primary_failure = 1;
		goto close_file;
	}
	if (memcmp(snapshot, header, sizeof(header)) != 0)
	{
		result.diagnostic = SCD_STATE_DRIFT;
		result.stage = SCS_RECHECK;
		primary_failure = 1;
		goto close_file;
	}
	/* A same-handle EOF probe rejects growth after the size preflight. */
	os_error = 0;
	count = ops->read(ops->context, file, &trailing, 1, &os_error);
	if (count != 0 || os_error != 0)
	{
		result.os_error = os_error;
		result.diagnostic = os_error != 0
			? SCD_IO_ERROR : SCD_BAD_FILE_SIZE;
		result.stage = SCS_PAYLOAD_READ;
		primary_failure = 1;
		goto close_file;
	}

close_file:
	os_error = 0;
	close_status = ops->close_file(ops->context, file, &os_error);
	file = NULL;
	if (close_status != 0)
	{
		result.close_error = Loader_Error(os_error);
		if (!primary_failure)
		{
			result.diagnostic = SCD_IO_ERROR;
			result.stage = SCS_CLOSE;
			result.os_error = result.close_error;
			primary_failure = 1;
		}
	}
	if (primary_failure)
	{
		if (snapshot)
			ops->deallocate(ops->context, snapshot);
		return result;
	}

	result.diagnostic = SG_SidecarDecode(snapshot, expected_file_size,
		kind, artifact, live_seed_marks, live_seed_capacity, snapshot,
		expected_payload_size, &decoded_size);
	if (result.diagnostic != SCD_OK)
	{
		result.stage = Loader_DecodeStage(result.diagnostic);
		if (result.diagnostic == SCD_BAD_PAYLOAD_VALUE)
			(void)SG_SidecarValidatePayload(kind, artifact,
				live_seed_marks, live_seed_capacity,
				snapshot + SG_SIDECAR_HEADER_BYTES,
				expected_payload_size, &result.plane,
				&result.index);
		ops->deallocate(ops->context, snapshot);
		return result;
	}
	*payload_out = snapshot;
	*payload_size_out = decoded_size;
	result.diagnostic = SCD_OK;
	result.stage = SCS_DONE;
	return result;
}
