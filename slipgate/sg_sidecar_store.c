/* sg_sidecar_store.c -- failure-atomic compact sidecar replacement. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "q_shared.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_sidecar_store.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

static uint64_t sg_store_nonce_sequence;

static uint64_t Store_DefaultTempNonce(void *context)
{
	uint64_t value;
	uint64_t sequence;
	time_t wall;

	(void)context;
	wall = time(NULL);
	sequence = ++sg_store_nonce_sequence;
	value = (uint64_t)(uintmax_t)wall ^ sequence;
#ifdef _WIN32
	value ^= (uint64_t)(unsigned int)_getpid() << 32;
#else
	value ^= (uint64_t)(unsigned int)getpid() << 32;
#endif
	return value != 0U ? value : sequence;
}

static int Store_Error(int reported)
{
	return reported != 0 ? reported : EIO;
}

static void *Store_HandleFromFD(int fd)
{
	return (void *)((uintptr_t)(unsigned int)fd + (uintptr_t)1U);
}

static int Store_HandleFD(void *handle)
{
	uintptr_t value = (uintptr_t)handle;

	if (value == 0U || value - (uintptr_t)1U > (uintptr_t)INT_MAX)
		return -1;
	return (int)(value - (uintptr_t)1U);
}

static void *Store_DefaultOpenExclusive(void *context, const char *path,
	int *os_error_out)
{
	int fd;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	errno = 0;
#ifdef _WIN32
	fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY |
		_O_NOINHERIT, _S_IREAD | _S_IWRITE);
#else
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL
#ifdef O_CLOEXEC
		| O_CLOEXEC
#endif
		, S_IRUSR | S_IWUSR);
#endif
	if (fd < 0)
	{
		if (os_error_out != NULL)
			*os_error_out = Store_Error(errno);
		return NULL;
	}
	return Store_HandleFromFD(fd);
}

static size_t Store_DefaultWrite(void *context, void *handle,
	const unsigned char *data, size_t data_size, int *os_error_out)
{
	int fd = Store_HandleFD(handle);
	size_t request = data_size > (size_t)INT_MAX ?
		(size_t)INT_MAX : data_size;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (fd < 0 || (data == NULL && data_size != 0U))
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0U;
	}
#ifdef _WIN32
	{
		int count;

		errno = 0;
		count = _write(fd, data, (unsigned int)request);
		if (count < 0)
		{
			if (os_error_out != NULL)
				*os_error_out = Store_Error(errno);
			return 0U;
		}
		return (size_t)count;
	}
#else
	{
		ssize_t count;

		do
		{
			errno = 0;
			count = write(fd, data, request);
		} while (count < 0 && errno == EINTR);
		if (count < 0)
		{
			if (os_error_out != NULL)
				*os_error_out = Store_Error(errno);
			return 0U;
		}
		return (size_t)count;
	}
#endif
}

static int Store_DefaultFlush(void *context, void *handle, int *os_error_out)
{
	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (Store_HandleFD(handle) < 0)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	return 0;
}

static int Store_DefaultSyncFile(void *context, void *handle,
	int *os_error_out)
{
	int fd = Store_HandleFD(handle);
	int status;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (fd < 0)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	errno = 0;
#ifdef _WIN32
	status = _commit(fd);
#else
	status = fsync(fd);
#endif
	if (status != 0 && os_error_out != NULL)
		*os_error_out = Store_Error(errno);
	return status;
}

static int Store_DefaultClose(void *context, void *handle,
	int *os_error_out)
{
	int fd = Store_HandleFD(handle);
	int status;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (fd < 0)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return -1;
	}
	errno = 0;
#ifdef _WIN32
	status = _close(fd);
#else
	status = close(fd);
#endif
	if (status != 0 && os_error_out != NULL)
		*os_error_out = Store_Error(errno);
	return status;
}

static int Store_DefaultReplace(void *context, const char *temporary_path,
	const char *destination_path, int *os_error_out)
{
	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
#ifdef _WIN32
	if (!MoveFileExA(temporary_path, destination_path,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		if (os_error_out != NULL)
			*os_error_out = Store_Error((int)GetLastError());
		return -1;
	}
	return 0;
#else
	errno = 0;
	if (rename(temporary_path, destination_path) != 0)
	{
		if (os_error_out != NULL)
			*os_error_out = Store_Error(errno);
		return -1;
	}
	return 0;
#endif
}

static int Store_DefaultSyncDirectory(void *context,
	const char *directory_path, int *os_error_out)
{
	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
#ifdef _WIN32
	(void)directory_path;
	return 0;
#else
	{
		int fd;
		int status;
		int saved_error = 0;

		errno = 0;
		fd = open(directory_path, O_RDONLY
#ifdef O_DIRECTORY
			| O_DIRECTORY
#endif
#ifdef O_CLOEXEC
			| O_CLOEXEC
#endif
			);
		if (fd < 0)
		{
			if (os_error_out != NULL)
				*os_error_out = Store_Error(errno);
			return -1;
		}
		errno = 0;
		status = fsync(fd);
		if (status != 0)
			saved_error = Store_Error(errno);
		errno = 0;
		if (close(fd) != 0 && status == 0)
		{
			status = -1;
			saved_error = Store_Error(errno);
		}
		if (status != 0 && os_error_out != NULL)
			*os_error_out = saved_error;
		return status;
	}
#endif
}

static int Store_DefaultRemove(void *context, const char *path,
	int *os_error_out)
{
	int status;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	errno = 0;
#ifdef _WIN32
	status = _unlink(path);
#else
	status = unlink(path);
#endif
	if (status != 0 && os_error_out != NULL)
		*os_error_out = Store_Error(errno);
	return status;
}

void SG_SidecarDefaultStoreOps(sg_sidecar_store_ops_t *ops_out)
{
	if (ops_out == NULL)
		return;
	memset(ops_out, 0, sizeof(*ops_out));
	ops_out->temp_nonce = Store_DefaultTempNonce;
	ops_out->open_exclusive = Store_DefaultOpenExclusive;
	ops_out->write = Store_DefaultWrite;
	ops_out->flush = Store_DefaultFlush;
	ops_out->sync_file = Store_DefaultSyncFile;
	ops_out->close_file = Store_DefaultClose;
	ops_out->replace_file = Store_DefaultReplace;
	ops_out->sync_directory = Store_DefaultSyncDirectory;
	ops_out->remove_file = Store_DefaultRemove;
}

static int Store_OpsValid(const sg_sidecar_store_ops_t *ops)
{
	return ops != NULL && ops->temp_nonce != NULL &&
		ops->open_exclusive != NULL && ops->write != NULL &&
		ops->flush != NULL && ops->sync_file != NULL &&
		ops->close_file != NULL && ops->replace_file != NULL &&
		ops->sync_directory != NULL && ops->remove_file != NULL;
}

static sg_sidecar_store_result_t Store_Result(void)
{
	sg_sidecar_store_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = SCD_INVALID_ARGUMENT;
	result.stage = SCS_ARGUMENT;
	return result;
}

static sg_sidecar_stage_t Store_InspectStage(
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

static int Store_DirectoryPath(char *output, size_t output_size,
	const char *destination)
{
	const char *separator;
	size_t length;

	if (output != NULL && output_size > 0U)
		output[0] = '\0';
	if (output == NULL || output_size == 0U || destination == NULL)
		return 0;
	separator = strrchr(destination, '/');
	if (separator == NULL)
	{
		if (output_size < 2U)
			return 0;
		output[0] = '.';
		output[1] = '\0';
		return 1;
	}
	if (separator == destination)
	{
		if (output_size < 2U)
			return 0;
		output[0] = '/';
		output[1] = '\0';
		return 1;
	}
	length = (size_t)(separator - destination);
	if (length >= output_size)
		return 0;
	memcpy(output, destination, length);
	output[length] = '\0';
	return 1;
}

static int Store_TempPath(char *output, size_t output_size,
	const char *destination, uint64_t nonce, unsigned int attempt)
{
	int written;

	if (output != NULL && output_size > 0U)
		output[0] = '\0';
	if (output == NULL || output_size == 0U || destination == NULL ||
		attempt >= SG_SIDECAR_STORE_TEMP_ATTEMPTS)
		return 0;
	written = snprintf(output, output_size, "%s.tmp.%016llx.%02u",
		destination, (unsigned long long)nonce, attempt);
	if (written < 0 || (size_t)written >= output_size)
	{
		output[0] = '\0';
		return 0;
	}
	return 1;
}

static void Store_Cleanup(sg_sidecar_store_result_t *result,
	const sg_sidecar_store_ops_t *ops, const char *temporary_path)
{
	int os_error = 0;

	if (result == NULL || ops == NULL || temporary_path == NULL ||
		!result->temp_created || result->replacement_complete)
		return;
	result->cleanup_attempted = 1;
	if (ops->remove_file(ops->context, temporary_path, &os_error) != 0)
	{
		result->cleanup_error = Store_Error(os_error);
		return;
	}
	result->cleanup_complete = 1;
}

sg_sidecar_store_result_t SG_SidecarStoreFile(
	const char *destination, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info,
	const unsigned char *encoded, size_t encoded_size,
	sg_sidecar_store_revalidate_fn revalidate, void *revalidate_context,
	const sg_sidecar_store_ops_t *provided_ops)
{
	sg_sidecar_store_result_t result = Store_Result();
	sg_sidecar_store_ops_t default_ops;
	const sg_sidecar_store_ops_t *ops = provided_ops;
	sg_sidecar_header_t inspected;
	char directory[MAX_OSPATH];
	char temporary[MAX_OSPATH];
	void *file = NULL;
	size_t offset = 0U;
	size_t count;
	uint32_t payload_crc = 0U;
	uint64_t temp_nonce;
	unsigned int attempt;
	int os_error = 0;
	int primary_failure = 0;
	int close_status;
	sg_sidecar_revalidate_t authority;

	if (ops == NULL)
	{
		SG_SidecarDefaultStoreOps(&default_ops);
		ops = &default_ops;
	}
	if (destination == NULL || destination[0] == '\0' || info == NULL ||
		encoded == NULL || revalidate == NULL || !Store_OpsValid(ops) ||
		encoded_size < SG_SIDECAR_HEADER_BYTES)
		return result;
	result.diagnostic = SG_SidecarInspect(encoded, SG_SIDECAR_HEADER_BYTES,
		encoded_size, kind, info, &inspected);
	if (result.diagnostic != SCD_OK)
	{
		result.stage = Store_InspectStage(result.diagnostic);
		return result;
	}
	result.expected_file_size = encoded_size;
	if (!SG_CRC32Buffer(encoded + SG_SIDECAR_HEADER_BYTES,
		inspected.payload_bytes, &payload_crc))
	{
		result.diagnostic = SCD_INTERNAL_ERROR;
		result.stage = SCS_PAYLOAD_CRC;
		return result;
	}
	if (payload_crc != inspected.payload_crc32)
	{
		result.diagnostic = SCD_BAD_PAYLOAD_CRC;
		result.stage = SCS_PAYLOAD_CRC;
		return result;
	}
	if (!Store_DirectoryPath(directory, sizeof(directory), destination))
	{
		result.diagnostic = SCD_PATH_TOO_LONG;
		result.stage = SCS_PATH;
		return result;
	}
	temp_nonce = ops->temp_nonce(ops->context);
	if (!Store_TempPath(temporary, sizeof(temporary), destination, temp_nonce,
		SG_SIDECAR_STORE_TEMP_ATTEMPTS - 1U))
	{
		result.diagnostic = SCD_PATH_TOO_LONG;
		result.stage = SCS_PATH;
		return result;
	}

	for (attempt = 0U; attempt < SG_SIDECAR_STORE_TEMP_ATTEMPTS; attempt++)
	{
		if (!Store_TempPath(temporary, sizeof(temporary), destination,
			temp_nonce, attempt))
		{
			result.diagnostic = SCD_PATH_TOO_LONG;
			result.stage = SCS_PATH;
			return result;
		}
		os_error = 0;
		file = ops->open_exclusive(ops->context, temporary, &os_error);
		result.temp_attempts++;
		if (file != NULL)
			break;
		if (os_error != EEXIST)
		{
			result.diagnostic = SCD_IO_ERROR;
			result.stage = SCS_OPEN;
			result.os_error = Store_Error(os_error);
			return result;
		}
	}
	if (file == NULL)
	{
		result.diagnostic = SCD_TEMP_EXHAUSTED;
		result.stage = SCS_OPEN;
		result.os_error = EEXIST;
		return result;
	}
	result.temp_created = 1;

	while (offset < encoded_size)
	{
		os_error = 0;
		count = ops->write(ops->context, file, encoded + offset,
			encoded_size - offset, &os_error);
		if (count > encoded_size - offset)
		{
			result.diagnostic = SCD_INTERNAL_ERROR;
			result.stage = SCS_WRITE;
			result.os_error = EIO;
			primary_failure = 1;
			break;
		}
		result.bytes_written += count;
		offset += count;
		if (os_error != 0 || count == 0U)
		{
			result.diagnostic = SCD_IO_ERROR;
			result.stage = SCS_WRITE;
			result.os_error = Store_Error(os_error);
			primary_failure = 1;
			break;
		}
	}
	if (!primary_failure)
	{
		os_error = 0;
		if (ops->flush(ops->context, file, &os_error) != 0)
		{
			result.diagnostic = SCD_IO_ERROR;
			result.stage = SCS_FLUSH;
			result.os_error = Store_Error(os_error);
			primary_failure = 1;
		}
	}
	if (!primary_failure)
	{
		os_error = 0;
		if (ops->sync_file(ops->context, file, &os_error) != 0)
		{
			result.diagnostic = SCD_IO_ERROR;
			result.stage = SCS_FILE_SYNC;
			result.os_error = Store_Error(os_error);
			primary_failure = 1;
		}
	}

	os_error = 0;
	close_status = ops->close_file(ops->context, file, &os_error);
	file = NULL;
	if (close_status != 0)
	{
		result.close_error = Store_Error(os_error);
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
		Store_Cleanup(&result, ops, temporary);
		return result;
	}

	os_error = 0;
	authority = revalidate(revalidate_context, info, &os_error);
	if (authority != SG_SIDECAR_REVALIDATE_MATCH)
	{
		result.stage = SCS_RECHECK;
		if (authority == SG_SIDECAR_REVALIDATE_DRIFT)
		{
			result.diagnostic = SCD_STATE_DRIFT;
			result.os_error = os_error;
		}
		else if (authority == SG_SIDECAR_REVALIDATE_ERROR)
		{
			result.diagnostic = SCD_IO_ERROR;
			result.os_error = Store_Error(os_error);
		}
		else
		{
			result.diagnostic = SCD_INTERNAL_ERROR;
			result.os_error = EIO;
		}
		Store_Cleanup(&result, ops, temporary);
		return result;
	}

	/* No filesystem or authority operation occurs between this match and
	 * atomic replacement. */
	os_error = 0;
	if (ops->replace_file(ops->context, temporary, destination, &os_error) != 0)
	{
		result.diagnostic = SCD_IO_ERROR;
		result.stage = SCS_RENAME;
		result.os_error = Store_Error(os_error);
		Store_Cleanup(&result, ops, temporary);
		return result;
	}
	result.replacement_complete = 1;

	os_error = 0;
	if (ops->sync_directory(ops->context, directory, &os_error) != 0)
	{
		result.diagnostic = SCD_IO_ERROR;
		result.stage = SCS_DIRECTORY_SYNC;
		result.os_error = Store_Error(os_error);
		return result;
	}
	result.durability_complete = 1;
	result.diagnostic = SCD_OK;
	result.stage = SCS_DONE;
	return result;
}
