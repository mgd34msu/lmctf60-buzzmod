/* sg_sidecar_loader.c -- one-open immutable compact sidecar snapshots. */
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
	if (os_error_out != NULL)
		*os_error_out = 0;
	errno = 0;
	file = fopen(path, "rb");
	if (file == NULL && os_error_out != NULL)
		*os_error_out = Loader_Error(errno);
	return file;
}

static size_t Loader_DefaultRead(void *context, void *handle,
	unsigned char *output, size_t output_size, int *os_error_out)
{
	FILE *file = handle;
	size_t count;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	errno = 0;
	count = fread(output, 1U, output_size, file);
	if (count != output_size && ferror(file) && os_error_out != NULL)
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
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (offset > (size_t)LONG_MAX ||
		(origin != SG_SIDECAR_SEEK_BEGIN &&
		 origin != SG_SIDECAR_SEEK_END))
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	whence = origin == SG_SIDECAR_SEEK_BEGIN ? SEEK_SET : SEEK_END;
	errno = 0;
	status = fseek(file, (long)offset, whence);
	if (status != 0 && os_error_out != NULL)
		*os_error_out = Loader_Error(errno);
	return status;
}

static int Loader_DefaultTell(void *context, void *handle,
	size_t *offset_out, int *os_error_out)
{
	FILE *file = handle;
	long position;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (offset_out == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	errno = 0;
	position = ftell(file);
	if (position < 0L)
	{
		if (os_error_out != NULL)
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
	if (os_error_out != NULL)
		*os_error_out = 0;
	errno = 0;
	status = fclose(handle);
	if (status != 0 && os_error_out != NULL)
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
	if (ops_out == NULL)
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
	return ops != NULL && ops->open_read != NULL && ops->read != NULL &&
		ops->seek != NULL && ops->tell != NULL && ops->close_file != NULL &&
		ops->allocate != NULL && ops->deallocate != NULL;
}

static sg_sidecar_load_result_t Loader_Result(void)
{
	sg_sidecar_load_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = SCD_INVALID_ARGUMENT;
	result.stage = SCS_ARGUMENT;
	return result;
}

static sg_sidecar_stage_t Loader_InspectStage(
	sg_sidecar_diagnostic_t diagnostic)
{
	switch (diagnostic)
	{
	case SCD_BAD_HEADER_CRC:
		return SCS_HEADER_CRC;
	case SCD_RUNE_VERSION_MISMATCH:
	case SCD_RUNE_SCHEMA_MISMATCH:
	case SCD_RUNE_IMAGE_MISMATCH:
	case SCD_RUNE_CHECKSUM_MISMATCH:
	case SCD_RUNE_IDENTITY_MISMATCH:
		return SCS_RUNE_BINDING;
	case SCD_BAD_PAYLOAD_SIZE:
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
	return Loader_InspectStage(diagnostic);
}

sg_sidecar_load_result_t SG_SidecarLoadFile(
	const char *path, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, unsigned char **payload_out,
	size_t *payload_size_out, const sg_sidecar_load_ops_t *provided_ops)
{
	sg_sidecar_load_result_t result = Loader_Result();
	sg_sidecar_load_ops_t default_ops;
	const sg_sidecar_load_ops_t *ops = provided_ops;
	sg_sidecar_header_t inspected;
	unsigned char header[SG_SIDECAR_HEADER_BYTES];
	unsigned char trailing;
	unsigned char *snapshot = NULL;
	void *file = NULL;
	size_t file_size;
	size_t expected_payload_size;
	size_t count;
	size_t decoded_size = 0U;
	int os_error = 0;
	int close_status;
	int primary_failure = 0;

	if (payload_out != NULL)
		*payload_out = NULL;
	if (payload_size_out != NULL)
		*payload_size_out = 0U;
	if (path == NULL || path[0] == '\0' || info == NULL ||
		payload_out == NULL || payload_size_out == NULL)
		return result;
	if (ops == NULL)
	{
		SG_SidecarDefaultLoadOps(&default_ops);
		ops = &default_ops;
	}
	if (!Loader_OpsValid(ops))
		return result;

	os_error = 0;
	file = ops->open_read(ops->context, path, &os_error);
	if (file == NULL)
	{
		result.os_error = Loader_Error(os_error);
		result.diagnostic = result.os_error == ENOENT ? SCD_ABSENT : SCD_IO_ERROR;
		result.stage = SCS_OPEN;
		return result;
	}

	os_error = 0;
	count = ops->read(ops->context, file, header, sizeof(header), &os_error);
	result.bytes_read = count;
	if (count != sizeof(header))
	{
		result.os_error = os_error;
		result.diagnostic = os_error != 0 ? SCD_IO_ERROR : SCD_BAD_HEADER_SIZE;
		result.stage = SCS_HEADER_READ;
		primary_failure = 1;
		goto close_file;
	}
	os_error = 0;
	if (ops->seek(ops->context, file, SG_SIDECAR_SEEK_END, 0U,
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
		result.observed_file_size, kind, info, &inspected);
	if (result.diagnostic != SCD_OK)
	{
		result.stage = Loader_InspectStage(result.diagnostic);
		primary_failure = 1;
		goto close_file;
	}
	file_size = SG_SIDECAR_HEADER_BYTES + (size_t)inspected.payload_bytes;
	result.expected_file_size = file_size;
	expected_payload_size = (size_t)inspected.payload_bytes;
	os_error = 0;
	if (ops->seek(ops->context, file, SG_SIDECAR_SEEK_BEGIN, 0U,
		&os_error) != 0)
	{
		result.diagnostic = SCD_IO_ERROR;
		result.stage = SCS_FILE_SIZE;
		result.os_error = Loader_Error(os_error);
		primary_failure = 1;
		goto close_file;
	}

	snapshot = ops->allocate(ops->context, file_size);
	if (snapshot == NULL)
	{
		result.diagnostic = SCD_ALLOCATION_FAILED;
		result.stage = SCS_ALLOCATION;
		primary_failure = 1;
		goto close_file;
	}
	os_error = 0;
	count = ops->read(ops->context, file, snapshot, file_size, &os_error);
	result.bytes_read += count;
	if (count != file_size)
	{
		result.os_error = os_error;
		result.diagnostic = os_error != 0 ? SCD_IO_ERROR : SCD_BAD_FILE_SIZE;
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
	/* A same-handle EOF probe catches growth after the size preflight. */
	os_error = 0;
	count = ops->read(ops->context, file, &trailing, 1U, &os_error);
	if (count != 0U || os_error != 0)
	{
		result.os_error = os_error;
		result.diagnostic = os_error != 0 ? SCD_IO_ERROR : SCD_BAD_FILE_SIZE;
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
		if (snapshot != NULL)
			ops->deallocate(ops->context, snapshot);
		return result;
	}

	result.diagnostic = SG_SidecarDecode(snapshot, file_size, kind, info,
		snapshot, expected_payload_size, &decoded_size);
	if (result.diagnostic != SCD_OK)
	{
		result.stage = Loader_DecodeStage(result.diagnostic);
		ops->deallocate(ops->context, snapshot);
		return result;
	}
	*payload_out = snapshot;
	*payload_size_out = decoded_size;
	result.diagnostic = SCD_OK;
	result.stage = SCS_DONE;
	return result;
}
