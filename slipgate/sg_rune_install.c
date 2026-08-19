/* sg_rune_install.c -- checked, atomic installation for RUNE. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

/* q_shared.h intentionally has no include guard.  Include it exactly once,
 * before the internal writer/install headers that consume its native types. */
#include "q_shared.h"
#include "slipgate/sg_rune_install.h"
#include "slipgate/sg_action.h"

typedef struct sg_rune_install_sink_s
{
	const sg_rune_install_ops_t *ops;
	FILE *file;
	size_t bytes_written;
	int failed;
	int os_error;
} sg_rune_install_sink_t;

typedef struct sg_rune_install_count_s
{
	size_t bytes_written;
	int failed;
} sg_rune_install_count_t;

static int Install_MapInitial(unsigned char c)
{
	return (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
	       (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
	       (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
	       c == (unsigned char)'_';
}

static int Install_MapTail(unsigned char c)
{
	return Install_MapInitial(c) || c == (unsigned char)'-';
}

static int Install_MapNameValid(const char *map_name)
{
	size_t i;

	if (!map_name || !Install_MapInitial((unsigned char)map_name[0]))
		return 0;
	for (i = 1; i < RUNE_MAP_NAME_BYTES; i++)
	{
		if (map_name[i] == '\0')
			return 1;
		if (!Install_MapTail((unsigned char)map_name[i]))
			return 0;
	}
	return 0;
}

int SG_RuneInstallDestinationPath(char *output, size_t output_size,
	const char *game_directory, const char *map_name)
{
	int written;

	if (output && output_size > 0)
		output[0] = '\0';
	if (!output || output_size == 0 || !game_directory ||
	    game_directory[0] == '\0' || !Install_MapNameValid(map_name))
		return 0;
	written = snprintf(output, output_size, "%s/maps/%s.rune",
		game_directory, map_name);
	if (written < 0 || (size_t)written >= output_size)
	{
		output[0] = '\0';
		return 0;
	}
	return 1;
}

int SG_RuneInstallTemporaryPath(char *output, size_t output_size,
	const char *game_directory, unsigned long process_id,
	unsigned int attempt)
{
	int written;

	if (output && output_size > 0)
		output[0] = '\0';
	if (!output || output_size == 0 || !game_directory ||
	    game_directory[0] == '\0')
		return 0;
	written = snprintf(output, output_size,
		"%s/maps/.rune.%lu.%u.tmp", game_directory, process_id,
		attempt);
	if (written < 0 || (size_t)written >= output_size)
	{
		output[0] = '\0';
		return 0;
	}
	return 1;
}

static FILE *Install_DefaultOpen(void *context, const char *path)
{
	(void)context;
	return fopen(path, "wbx");
}

static size_t Install_DefaultWrite(void *context, FILE *file,
	const void *data, size_t data_size)
{
	(void)context;
	return fwrite(data, 1, data_size, file);
}

static int Install_DefaultFlush(void *context, FILE *file)
{
	(void)context;
	return fflush(file);
}

static int Install_DefaultSync(void *context, FILE *file)
{
	(void)context;
#ifdef _WIN32
	return _commit(_fileno(file));
#else
	return fsync(fileno(file));
#endif
}

static int Install_DefaultClose(void *context, FILE *file)
{
	(void)context;
	return fclose(file);
}

static int Install_DefaultRename(void *context, const char *temporary_path,
	const char *destination_path)
{
	(void)context;
#ifdef _WIN32
	if (MoveFileExA(temporary_path, destination_path,
	    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return 0;
	errno = EIO;
	return -1;
#else
	return rename(temporary_path, destination_path);
#endif
}

static int Install_DefaultRemove(void *context, const char *path)
{
	(void)context;
	return remove(path);
}

static unsigned long Install_DefaultProcessId(void *context)
{
	(void)context;
#ifdef _WIN32
	return (unsigned long)_getpid();
#else
	return (unsigned long)getpid();
#endif
}

static const sg_rune_install_ops_t install_default_ops = {
	NULL,
	Install_DefaultOpen,
	Install_DefaultWrite,
	Install_DefaultFlush,
	Install_DefaultSync,
	Install_DefaultClose,
	Install_DefaultRename,
	Install_DefaultRemove,
	Install_DefaultProcessId
};

const sg_rune_install_ops_t *SG_RuneInstallDefaultOps(void)
{
	return &install_default_ops;
}

const char *SG_RuneInstallReason(sg_rune_install_status_t status)
{
	static const char *const reasons[] = {
		"ok",
		"invalid argument",
		"path exceeds the checked output boundary",
		"cannot create exclusive temporary",
		"exclusive temporary names exhausted",
		"RUNE writer failed",
		"temporary flush failed",
		"temporary sync failed",
		"temporary close failed",
		"captured identity or proof law changed",
		"atomic rename failed"
	};

	if (status < SG_RUNE_INSTALL_OK ||
	    (size_t)status >= sizeof(reasons) / sizeof(reasons[0]))
		return "unknown install status";
	return reasons[status];
}

static int Install_OpsValid(const sg_rune_install_ops_t *ops)
{
	return ops && ops->open_exclusive && ops->write && ops->flush &&
	       ops->sync_file && ops->close_file && ops->rename_replace &&
	       ops->remove_path && ops->process_id;
}

static int Install_Error(void)
{
	return errno != 0 ? errno : EIO;
}

static int Install_Sink(void *context, const unsigned char *fragment,
	size_t fragment_size)
{
	sg_rune_install_sink_t *sink = context;
	size_t written;

	if (!sink || !fragment || fragment_size == 0 || sink->failed ||
	    fragment_size > SIZE_MAX - sink->bytes_written)
		return 1;
	errno = 0;
	written = sink->ops->write(sink->ops->context, sink->file, fragment,
		fragment_size);
	if (written != fragment_size)
	{
		sink->failed = 1;
		sink->os_error = Install_Error();
		return 1;
	}
	sink->bytes_written += written;
	return 0;
}

static int Install_CountSink(void *context, const unsigned char *fragment,
	size_t fragment_size)
{
	sg_rune_install_count_t *count = context;

	if (!count || !fragment || fragment_size == 0 || count->failed ||
	    fragment_size > SIZE_MAX - count->bytes_written)
	{
		if (count)
			count->failed = 1;
		return 1;
	}
	count->bytes_written += fragment_size;
	return 0;
}

static sg_rune_install_result_t Install_Result(void)
{
	sg_rune_install_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = SG_RUNE_INSTALL_INVALID_ARGUMENT;
	result.writer.diagnostic = RLW_INVALID_ARGUMENT;
	result.writer.stage = SG_RUNE_STREAM_STAGE_ARGUMENT;
	result.writer.index = SG_RUNE_STREAM_INDEX_NONE;
	return result;
}

static void Install_CleanupOwned(sg_rune_install_result_t *result,
	const sg_rune_install_ops_t *ops, const char *temporary_path,
	int owns_temporary)
{
	if (!result || !ops || !owns_temporary)
		return;
	errno = 0;
	if (ops->remove_path(ops->context, temporary_path) != 0 &&
	    errno != ENOENT)
	{
		result->cleanup_error = Install_Error();
	}
}

sg_rune_install_result_t SG_RuneInstall(
	const char *game_directory, const char *map_name,
	char *destination_path, size_t destination_path_size,
	char *temporary_path, size_t temporary_path_size,
	sg_rune_install_stream_fn stream, void *stream_context,
	sg_rune_install_revalidate_fn revalidate, void *revalidate_context,
	const sg_rune_install_ops_t *ops)
{
	sg_rune_install_result_t result = Install_Result();
	sg_rune_stream_result_t dry_writer;
	sg_rune_install_sink_t sink;
	sg_rune_install_count_t count;
	FILE *file = NULL;
	unsigned long process_id;
	unsigned int attempt;
	int owns_temporary = 0;
	int dry_mismatch;

	if (destination_path && destination_path_size > 0)
		destination_path[0] = '\0';
	if (temporary_path && temporary_path_size > 0)
		temporary_path[0] = '\0';
	if (!game_directory || !map_name || !destination_path ||
	    destination_path_size == 0 || !temporary_path ||
	    temporary_path_size == 0 || !stream || !revalidate)
		return result;
	if (!ops)
		ops = SG_RuneInstallDefaultOps();
	if (!Install_OpsValid(ops))
		return result;
	if (!SG_RuneInstallDestinationPath(destination_path,
	    destination_path_size, game_directory, map_name))
	{
		result.status = SG_RUNE_INSTALL_BAD_PATH;
		return result;
	}

	/* Full native/identity preflight before the first filesystem operation.
	 * The stream performs another complete validation while emitting,
	 * so mutation between dry and write passes fails closed. */
	memset(&count, 0, sizeof(count));
	result.writer_called++;
	result.writer = stream(stream_context, Install_CountSink, &count);
	if (!SG_RuneStreamResultSucceeded(&result.writer) || count.failed ||
	    result.writer.bytes_written != count.bytes_written ||
	    result.writer.file_size != count.bytes_written)
	{
		if (result.writer.diagnostic == RLW_OK)
			SG_RuneStreamResultMarkIOFailure(&result.writer, 0);
		result.status = SG_RUNE_INSTALL_WRITER_FAILED;
		return result;
	}
	dry_writer = result.writer;

	process_id = ops->process_id(ops->context);
	for (attempt = 0; attempt < SG_RUNE_INSTALL_TEMP_ATTEMPTS; attempt++)
	{
		if (!SG_RuneInstallTemporaryPath(temporary_path,
		    temporary_path_size, game_directory, process_id, attempt))
		{
			result.status = SG_RUNE_INSTALL_BAD_PATH;
			return result;
		}
		errno = 0;
		file = ops->open_exclusive(ops->context, temporary_path);
		if (file)
		{
			owns_temporary = 1;
			result.temp_attempt = attempt;
			break;
		}
		if (errno != EEXIST)
		{
			result.status = SG_RUNE_INSTALL_TEMP_OPEN_FAILED;
			result.os_error = Install_Error();
			return result;
		}
	}
	if (!file)
	{
		result.status = SG_RUNE_INSTALL_TEMP_EXHAUSTED;
		result.os_error = EEXIST;
		return result;
	}

	memset(&sink, 0, sizeof(sink));
	sink.ops = ops;
	sink.file = file;
	result.writer_called++;
	result.writer = stream(stream_context, Install_Sink, &sink);
	dry_mismatch = result.writer.file_size != dry_writer.file_size ||
		result.writer.payload_crc32 != dry_writer.payload_crc32;
	if (!SG_RuneStreamResultSucceeded(&result.writer) || sink.failed ||
	    result.writer.bytes_written != sink.bytes_written ||
	    result.writer.file_size != sink.bytes_written || dry_mismatch)
	{
		if (result.writer.diagnostic == RLW_OK)
			SG_RuneStreamResultMarkIOFailure(&result.writer, dry_mismatch);
		result.status = SG_RUNE_INSTALL_WRITER_FAILED;
		result.os_error = sink.os_error;
		goto close_and_cleanup;
	}

	errno = 0;
	if (ops->flush(ops->context, file) != 0)
	{
		result.status = SG_RUNE_INSTALL_FLUSH_FAILED;
		result.os_error = Install_Error();
		goto close_and_cleanup;
	}
	errno = 0;
	if (ops->sync_file(ops->context, file) != 0)
	{
		result.status = SG_RUNE_INSTALL_SYNC_FAILED;
		result.os_error = Install_Error();
		goto close_and_cleanup;
	}
	errno = 0;
	if (ops->close_file(ops->context, file) != 0)
	{
		file = NULL;
		result.status = SG_RUNE_INSTALL_CLOSE_FAILED;
		result.os_error = Install_Error();
		goto cleanup;
	}
	file = NULL;

	if (!revalidate(revalidate_context))
	{
		result.status = SG_RUNE_INSTALL_REVALIDATE_FAILED;
		goto cleanup;
	}
	errno = 0;
	if (ops->rename_replace(ops->context, temporary_path,
	    destination_path) != 0)
	{
		result.status = SG_RUNE_INSTALL_RENAME_FAILED;
		result.os_error = Install_Error();
		goto cleanup;
	}
	owns_temporary = 0;
	result.status = SG_RUNE_INSTALL_OK;
	return result;

close_and_cleanup:
	errno = 0;
	if (ops->close_file(ops->context, file) != 0)
		result.cleanup_error = Install_Error();
	file = NULL;
cleanup:
	Install_CleanupOwned(&result, ops, temporary_path, owns_temporary);
	return result;
}
