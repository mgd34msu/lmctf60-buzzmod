#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "sg_rune_compact_artifact.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#endif

#define SG_RUNE_COMPACT_ARTIFACT_LOADER_STATE UINT32_C(0x434c4452)

static void ClearWireError(sg_rune_compact_wire_error_t *error)
{
	if (error == NULL)
		return;
	memset(error, 0, sizeof(*error));
	error->section = SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
	error->record = UINT32_MAX;
}

static void SetWireError(sg_rune_compact_wire_error_t *error,
	sg_rune_compact_wire_error_code_t code)
{
	ClearWireError(error);
	if (error != NULL)
		error->code = code;
}

static sg_rune_compact_artifact_load_result_t LoadResult(
	sg_rune_compact_artifact_load_diagnostic_t diagnostic,
	sg_rune_compact_artifact_load_stage_t stage)
{
	sg_rune_compact_artifact_load_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.stage = stage;
	ClearWireError(&result.wire_error);
	result.wire_info.wire_version = 0U;
	return result;
}

static int LoaderReady(const sg_rune_compact_artifact_loader_t *loader)
{
	return loader != NULL &&
		loader->state == SG_RUNE_COMPACT_ARTIFACT_LOADER_STATE &&
		loader->state_inverse == ~SG_RUNE_COMPACT_ARTIFACT_LOADER_STATE;
}

int SG_RuneCompactArtifactLoaderInit(
	sg_rune_compact_artifact_loader_t *loader)
{
	if (loader == NULL)
		return 0;
	/* Reinitialization would orphan the immutable runtime publication. */
	if (LoaderReady(loader))
		return 0;
	memset(loader, 0, sizeof(*loader));
	loader->state = SG_RUNE_COMPACT_ARTIFACT_LOADER_STATE;
	loader->state_inverse = ~SG_RUNE_COMPACT_ARTIFACT_LOADER_STATE;
	return 1;
}

void SG_RuneCompactArtifactLoaderReset(
	sg_rune_compact_artifact_loader_t *loader)
{
	sg_rune_compact_wire_decoded_t *old;

	if (!LoaderReady(loader))
		return;
	old = loader->published;
	loader->published = NULL;
	memset(&loader->published_info, 0, sizeof(loader->published_info));
	SG_RuneCompactWireDestroy(old);
}

void SG_RuneCompactArtifactLoaderDestroy(
	sg_rune_compact_artifact_loader_t *loader)
{
	if (loader == NULL)
		return;
	SG_RuneCompactArtifactLoaderReset(loader);
	memset(loader, 0, sizeof(*loader));
}

const sg_rune_compact_model_t *SG_RuneCompactArtifactLoaderSnapshot(
	const sg_rune_compact_artifact_loader_t *loader)
{
	return LoaderReady(loader) && loader->published != NULL
		? SG_RuneCompactWireModel(loader->published) : NULL;
}

int SG_RuneCompactArtifactLoaderSnapshotInfo(
	const sg_rune_compact_artifact_loader_t *loader,
	sg_rune_compact_wire_info_t *info_out)
{
	if (info_out != NULL)
		memset(info_out, 0, sizeof(*info_out));
	if (!LoaderReady(loader) || loader->published == NULL || info_out == NULL)
		return 0;
	*info_out = loader->published_info;
	return 1;
}

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadBytes(
	sg_rune_compact_artifact_loader_t *loader,
	const unsigned char *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity)
{
	sg_rune_compact_artifact_load_result_t result;
	sg_rune_compact_wire_decoded_t *candidate = NULL;
	sg_rune_compact_wire_decoded_t *old;
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_error_t wire_error;

	result = LoadResult(SG_RUNE_COMPACT_ARTIFACT_LOAD_INVALID_ARGUMENT,
		SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ARGUMENT);
	if (loader == NULL || image == NULL || expected_identity == NULL)
		return result;
	if (!LoaderReady(loader))
		return LoadResult(SG_RUNE_COMPACT_ARTIFACT_LOAD_NOT_INITIALIZED,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ARGUMENT);
	result.file_size = image_size;
	result.bytes_read = image_size;
	if ((uint64_t)image_size > SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_SIZE;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_SIZE;
		SetWireError(&result.wire_error,
			SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
		return result;
	}
	memset(&info, 0, sizeof(info));
	ClearWireError(&wire_error);
	if (!SG_RuneCompactWireInspect(image, image_size, &info, &wire_error) ||
		!SG_RuneCompactWireDecode(image, image_size, expected_identity,
		&candidate, &wire_error))
	{
		result.diagnostic = wire_error.code ==
			SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY
			? SG_RUNE_COMPACT_ARTIFACT_LOAD_ALLOCATION_FAILED
			: SG_RUNE_COMPACT_ARTIFACT_LOAD_WIRE_REJECTED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_WIRE;
		result.wire_error = wire_error;
		return result;
	}
	/* Pointer replacement is the sole publication operation. */
	old = loader->published;
	loader->published = candidate;
	loader->published_info = info;
	SG_RuneCompactWireDestroy(old);
	result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_LOAD_OK;
	result.stage = SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_PUBLICATION;
	result.wire_info = info;
	return result;
}

static sg_rune_compact_artifact_load_result_t LoadFileFailure(
	sg_rune_compact_artifact_load_diagnostic_t diagnostic,
	sg_rune_compact_artifact_load_stage_t stage, int os_error,
	size_t file_size, size_t bytes_read)
{
	sg_rune_compact_artifact_load_result_t result = LoadResult(diagnostic,
		stage);

	result.os_error = os_error;
	result.file_size = file_size;
	result.bytes_read = bytes_read;
	return result;
}

static void CloseLoadHandle(
	const sg_rune_compact_artifact_load_ops_t *ops, void *file,
	sg_rune_compact_artifact_load_result_t *result)
{
	int status;
	int error;

	if (ops == NULL || file == NULL || result == NULL)
		return;
	errno = 0;
	error = 0;
	status = ops->close_file(ops->context, file, &error);
	if (status == 0)
	{
		error = error != 0 ? error : EIO;
		if (result->close_error == 0)
			result->close_error = error;
	}
}

static void *DefaultLoadOpenRead(void *context, const char *path,
	int *os_error_out)
{
	FILE *file;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (path == NULL || path[0] == '\0')
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return NULL;
	}
	errno = 0;
	file = fopen(path, "rb");
	if (file == NULL && os_error_out != NULL)
		*os_error_out = errno != 0 ? errno : EIO;
	return file;
}

static int DefaultLoadStat(void *context, void *file, int64_t *size_out,
	int *regular_out, int *os_error_out)
{
	(void)context;
	if (size_out != NULL)
		*size_out = 0;
	if (regular_out != NULL)
		*regular_out = 0;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file == NULL || size_out == NULL || regular_out == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0;
	}
#ifdef _WIN32
	{
		struct _stat64 status;

		if (_fstat64(_fileno((FILE *)file), &status) != 0)
		{
			if (os_error_out != NULL)
				*os_error_out = errno != 0 ? errno : EIO;
			return 0;
		}
		*regular_out = (status.st_mode & _S_IFMT) == _S_IFREG;
		*size_out = (int64_t)status.st_size;
	}
#else
	{
		struct stat status;

		if (fstat(fileno((FILE *)file), &status) != 0)
		{
			if (os_error_out != NULL)
				*os_error_out = errno != 0 ? errno : EIO;
			return 0;
		}
		*regular_out = S_ISREG(status.st_mode);
		*size_out = (int64_t)status.st_size;
	}
#endif
	return 1;
}

static size_t DefaultLoadRead(void *context, void *file,
	unsigned char *output, size_t output_size, int *os_error_out)
{
	size_t count;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file == NULL || output == NULL || output_size == 0U)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0U;
	}
	errno = 0;
	count = fread(output, 1U, output_size, (FILE *)file);
	/* A short read is a failed exact-bound read even when stdio reports EOF
	 * without setting ferror.  Returning an error here prevents a second read
	 * against an EOF-marked stream and lets the caller report the true short
	 * read through bytes_read. */
	if ((count != output_size || ferror((FILE *)file)) && os_error_out != NULL)
		*os_error_out = errno != 0 ? errno : EIO;
	return count;
}

static int DefaultLoadProbe(void *context, void *file, int *has_extra_out,
	int *os_error_out)
{
	int value;

	(void)context;
	if (has_extra_out != NULL)
		*has_extra_out = 0;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file == NULL || has_extra_out == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0;
	}
	clearerr((FILE *)file);
	errno = 0;
	value = fgetc((FILE *)file);
	if (value == EOF)
	{
		if (ferror((FILE *)file))
		{
			if (os_error_out != NULL)
				*os_error_out = errno != 0 ? errno : EIO;
			return 0;
		}
		return 1;
	}
	*has_extra_out = 1;
	return 1;
}

static int DefaultLoadClose(void *context, void *file, int *os_error_out)
{
	int status;

	(void)context;
	if (os_error_out != NULL)
		*os_error_out = 0;
	if (file == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0;
	}
	errno = 0;
	status = fclose((FILE *)file);
	if (status != 0 && os_error_out != NULL)
		*os_error_out = errno != 0 ? errno : EIO;
	return status == 0;
}

void SG_RuneCompactArtifactDefaultLoadOps(
	sg_rune_compact_artifact_load_ops_t *ops_out)
{
	if (ops_out == NULL)
		return;
	memset(ops_out, 0, sizeof(*ops_out));
	ops_out->open_read = DefaultLoadOpenRead;
	ops_out->stat_file = DefaultLoadStat;
	ops_out->read = DefaultLoadRead;
	ops_out->probe = DefaultLoadProbe;
	ops_out->close_file = DefaultLoadClose;
}

static int LoadOpsValid(const sg_rune_compact_artifact_load_ops_t *ops)
{
	return ops != NULL && ops->open_read != NULL && ops->stat_file != NULL &&
		ops->read != NULL && ops->probe != NULL && ops->close_file != NULL;
}

static sg_rune_compact_artifact_load_result_t LoadFile(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_identity_t *inspected_identity_out,
	sg_rune_compact_wire_info_t *inspected_info_out,
	const sg_rune_compact_artifact_load_ops_t *provided_ops)
{
	sg_rune_compact_artifact_load_ops_t default_ops;
	const sg_rune_compact_artifact_load_ops_t *ops = provided_ops;
	sg_rune_compact_artifact_load_result_t result;
	void *file;
	unsigned char *image = NULL;
	int64_t reported_size;
	size_t file_size;
	size_t bytes_read = 0U;
	int regular;
	int error = 0;
	int has_extra = 0;
	int close_status;

	if (inspected_identity_out != NULL)
		memset(inspected_identity_out, 0, sizeof(*inspected_identity_out));
	if (inspected_info_out != NULL)
		memset(inspected_info_out, 0, sizeof(*inspected_info_out));
	if (loader == NULL || path == NULL || path[0] == '\0' ||
		(expected_identity == NULL) == (inspected_identity_out == NULL) ||
		(inspected_info_out != NULL && inspected_identity_out == NULL))
		return LoadResult(SG_RUNE_COMPACT_ARTIFACT_LOAD_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ARGUMENT);
	if (!LoaderReady(loader))
		return LoadResult(SG_RUNE_COMPACT_ARTIFACT_LOAD_NOT_INITIALIZED,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ARGUMENT);
	if (ops == NULL)
	{
		SG_RuneCompactArtifactDefaultLoadOps(&default_ops);
		ops = &default_ops;
	}
	if (!LoadOpsValid(ops))
		return LoadResult(SG_RUNE_COMPACT_ARTIFACT_LOAD_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ARGUMENT);
	error = 0;
	file = ops->open_read(ops->context, path, &error);
	if (file == NULL)
		return LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_OPEN,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_OPEN,
			error != 0 ? error : EIO, 0U, 0U);
	reported_size = 0;
	regular = 0;
	error = 0;
	if (!ops->stat_file(ops->context, file, &reported_size, &regular,
		&error))
	{
		result = LoadFileFailure(
			SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_STAT,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_STAT,
			error != 0 ? error : EIO, 0U, 0U);
		CloseLoadHandle(ops, file, &result);
		return result;
	}
	if (!regular)
	{
		result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_KIND,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_STAT, EINVAL, 0U, 0U);
		CloseLoadHandle(ops, file, &result);
		return result;
	}
	if (reported_size < 0 || (uint64_t)reported_size >
		(uint64_t)SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES ||
		(uint64_t)reported_size > (uint64_t)SIZE_MAX)
	{
		result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_SIZE,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_SIZE, EFBIG, 0U, 0U);
		CloseLoadHandle(ops, file, &result);
		return result;
	}
	file_size = (size_t)reported_size;
	image = malloc(file_size == 0U ? 1U : file_size);
	if (image == NULL)
	{
		result = LoadFileFailure(
			SG_RUNE_COMPACT_ARTIFACT_LOAD_ALLOCATION_FAILED,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ALLOCATION, ENOMEM,
			file_size, 0U);
		CloseLoadHandle(ops, file, &result);
		return result;
	}
	while (bytes_read < file_size)
	{
		size_t count;

		error = 0;
		count = ops->read(ops->context, file, image + bytes_read,
			file_size - bytes_read, &error);
		if (count > file_size - bytes_read)
		{
			free(image);
			result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ,
				SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_READ, EIO,
				file_size, bytes_read);
			CloseLoadHandle(ops, file, &result);
			return result;
		}
		bytes_read += count;
		if (error != 0)
		{
			free(image);
			result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ,
				SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_READ, error,
				file_size, bytes_read);
			CloseLoadHandle(ops, file, &result);
			return result;
		}
		if (count == 0U)
		{
			free(image);
			result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ,
				SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_READ, 0,
				file_size, bytes_read);
			CloseLoadHandle(ops, file, &result);
			return result;
		}
	}
	error = 0;
	if (!ops->probe(ops->context, file, &has_extra, &error))
	{
		free(image);
		result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_READ,
			error != 0 ? error : EIO, file_size, bytes_read);
		CloseLoadHandle(ops, file, &result);
		return result;
	}
	if (has_extra)
	{
		free(image);
		result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_GREW,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_GROWTH, EFBIG,
			file_size, bytes_read);
		CloseLoadHandle(ops, file, &result);
		return result;
	}
	error = 0;
	close_status = ops->close_file(ops->context, file, &error);
	file = NULL;
	if (!close_status)
	{
		free(image);
		result = LoadFileFailure(SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_CLOSE,
			SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_CLOSE,
			error != 0 ? error : EIO, file_size, bytes_read);
		result.close_error = error != 0 ? error : EIO;
		return result;
	}
	if (expected_identity != NULL)
		result = SG_RuneCompactArtifactLoaderLoadBytes(loader, image, file_size,
			expected_identity);
	else
	{
		sg_rune_compact_wire_info_t info;
		sg_rune_compact_wire_error_t wire_error;

		memset(&info, 0, sizeof(info));
		ClearWireError(&wire_error);
		if (!SG_RuneCompactWireInspect(image, file_size, &info, &wire_error))
		{
			result = LoadResult(
				wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY ?
					SG_RUNE_COMPACT_ARTIFACT_LOAD_ALLOCATION_FAILED :
					SG_RUNE_COMPACT_ARTIFACT_LOAD_WIRE_REJECTED,
				SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_WIRE);
			result.wire_error = wire_error;
			result.file_size = file_size;
			result.bytes_read = bytes_read;
		}
		else
		{
			result = SG_RuneCompactArtifactLoaderLoadBytes(loader, image,
				file_size, &info.identity);
			if (result.diagnostic == SG_RUNE_COMPACT_ARTIFACT_LOAD_OK)
			{
				*inspected_identity_out = info.identity;
				if (inspected_info_out != NULL)
					*inspected_info_out = result.wire_info;
			}
		}
	}
	result.file_size = file_size;
	result.bytes_read = bytes_read;
	free(image);
	return result;
}

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadFileWithOps(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_artifact_load_ops_t *provided_ops)
{
	return LoadFile(loader, path, expected_identity, NULL, NULL, provided_ops);
}

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadFile(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	const sg_rune_compact_identity_t *expected_identity)
{
	return LoadFile(loader, path, expected_identity, NULL, NULL, NULL);
}

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadAcceptedFile(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	sg_rune_compact_identity_t *identity_out)
{
	return LoadFile(loader, path, NULL, identity_out, NULL, NULL);
}

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadAcceptedFileWithInfo(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_wire_info_t *info_out)
{
	return LoadFile(loader, path, NULL, identity_out, info_out, NULL);
}

const char *SG_RuneCompactArtifactLoadDiagnosticString(
	sg_rune_compact_artifact_load_diagnostic_t diagnostic)
{
	static const char *const messages[] = {
		"ok",
		"invalid argument",
		"loader not initialized",
		"file open failed",
		"file stat failed",
		"file is not regular",
		"file size rejected",
		"allocation failed",
		"file read failed",
		"file grew while reading",
		"file close failed",
		"wire image rejected"
	};

	_Static_assert(sizeof(messages) / sizeof(messages[0]) ==
		SG_RUNE_COMPACT_ARTIFACT_LOAD_CODE_COUNT,
		"compact artifact load diagnostics must be complete");
	if ((uint32_t)diagnostic >=
		(uint32_t)SG_RUNE_COMPACT_ARTIFACT_LOAD_CODE_COUNT)
		return "unknown compact artifact load diagnostic";
	return messages[(uint32_t)diagnostic];
}

static sg_rune_compact_artifact_write_result_t WriteResult(
	sg_rune_compact_artifact_write_diagnostic_t diagnostic,
	sg_rune_compact_artifact_write_stage_t stage)
{
	sg_rune_compact_artifact_write_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.stage = stage;
	ClearWireError(&result.wire_error);
	return result;
}

static int EncodeImage(const sg_rune_compact_model_t *model,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_compact_wire_error_t *wire_error_out,
	sg_rune_compact_artifact_write_stage_t *stage_out)
{
	sg_rune_compact_wire_error_t error;
	unsigned char *image;
	size_t image_size = 0U;
	size_t written = 0U;
	sg_rune_compact_wire_decoded_t *decoded = NULL;

	if (stage_out != NULL)
		*stage_out = SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ARGUMENT;
	if (image_out != NULL)
		*image_out = NULL;
	if (image_size_out != NULL)
		*image_size_out = 0U;
	ClearWireError(wire_error_out);
	if (model == NULL || image_out == NULL || image_size_out == NULL)
	{
		SetWireError(wire_error_out,
			SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	if (stage_out != NULL)
		*stage_out = SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_MEASURE;
	if (!SG_RuneCompactWireMeasure(model, &image_size, &error))
	{
		if (wire_error_out != NULL)
			*wire_error_out = error;
		return 0;
	}
	if ((uint64_t)image_size > SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES)
	{
		SetWireError(wire_error_out,
			SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
		return 0;
	}
	if (stage_out != NULL)
		*stage_out = SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ALLOCATION;
	image = malloc(image_size == 0U ? 1U : image_size);
	if (image == NULL)
	{
		SetWireError(wire_error_out,
			SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY);
		return 0;
	}
	if (stage_out != NULL)
		*stage_out = SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ENCODE;
	if (!SG_RuneCompactWireEncode(model, image, image_size, &written,
		&error) || written != image_size)
	{
		free(image);
		if (wire_error_out != NULL)
		{
			if (written != image_size && error.code ==
				SG_RUNE_COMPACT_WIRE_ERROR_NONE)
				SetWireError(wire_error_out,
					SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT);
			else
				*wire_error_out = error;
		}
		return 0;
	}
	if (stage_out != NULL)
		*stage_out = SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_VALIDATE;
	if (!SG_RuneCompactWireInspect(image, image_size, NULL, &error) ||
		!SG_RuneCompactWireDecode(image, image_size, &model->identity,
			&decoded, &error))
	{
		if (decoded != NULL)
			SG_RuneCompactWireDestroy(decoded);
		free(image);
		if (wire_error_out != NULL)
			*wire_error_out = error;
		return 0;
	}
	SG_RuneCompactWireDestroy(decoded);
	*image_out = image;
	*image_size_out = image_size;
	return 1;
}

int SG_RuneCompactArtifactEncode(const sg_rune_compact_model_t *model,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_compact_wire_error_t *wire_error_out)
{
	return EncodeImage(model, image_out, image_size_out, wire_error_out,
		NULL);
}

sg_rune_compact_artifact_write_result_t SG_RuneCompactArtifactWriteModel(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_artifact_sink_fn sink, void *sink_context)
{
	sg_rune_compact_artifact_write_result_t result;
	sg_rune_compact_wire_error_t wire_error;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	size_t offset = 0U;
	sg_rune_compact_artifact_write_stage_t encode_stage;

	result = WriteResult(SG_RUNE_COMPACT_ARTIFACT_WRITE_INVALID_ARGUMENT,
		SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ARGUMENT);
	if (model == NULL || sink == NULL)
		return result;
	if (!EncodeImage(model, &image, &image_size, &wire_error, &encode_stage))
	{
		result.diagnostic = encode_stage ==
			SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ALLOCATION ||
			wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY
			? SG_RUNE_COMPACT_ARTIFACT_WRITE_ALLOCATION_FAILED
			: SG_RUNE_COMPACT_ARTIFACT_WRITE_WIRE_REJECTED;
		result.stage = encode_stage;
		result.wire_error = wire_error;
		return result;
	}
	result.image_size = image_size;
	result.stage = SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_SINK;
	while (offset < image_size)
	{
		int error = 0;
		size_t count = sink(sink_context, image + offset,
			image_size - offset, &error);

		if (count > image_size - offset)
		{
			result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_WRITE_SINK_FAILED;
			result.os_error = EINVAL;
			break;
		}
		result.bytes_transferred += count;
		if (error != 0 || count == 0U)
		{
			result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_WRITE_SINK_FAILED;
			result.os_error = error != 0 ? error : EIO;
			break;
		}
		offset += count;
	}
	if (offset == image_size)
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_WRITE_OK;
	free(image);
	return result;
}

static void *DefaultOpenTemp(void *context, const char *destination,
	char *temp_path, size_t temp_path_size, int *os_error_out)
{
	(void)context;
	if (destination == NULL || temp_path == NULL || temp_path_size == 0U)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return NULL;
	}
#ifndef _WIN32
	{
		int fd;
		FILE *file;
		int written = snprintf(temp_path, temp_path_size, "%s.tmp.XXXXXX",
			destination);

		if (written < 0 || (size_t)written >= temp_path_size)
		{
			if (os_error_out != NULL)
				*os_error_out = ENAMETOOLONG;
			return NULL;
		}
		errno = 0;
		fd = mkstemp(temp_path);
		if (fd < 0)
		{
			if (os_error_out != NULL)
				*os_error_out = errno != 0 ? errno : EIO;
			return NULL;
		}
		file = fdopen(fd, "wb");
		if (file == NULL)
		{
			int error = errno != 0 ? errno : EIO;
			(void)close(fd);
			(void)unlink(temp_path);
			if (os_error_out != NULL)
				*os_error_out = error;
			return NULL;
		}
		if (os_error_out != NULL)
			*os_error_out = 0;
		return file;
	}
#else
	{
		static unsigned long sequence;

		for (;;)
		{
			int written = snprintf(temp_path, temp_path_size,
				"%s.tmp.%lu.%lu", destination,
				(unsigned long)_getpid(), sequence++);
			FILE *file;

			if (written < 0 || (size_t)written >= temp_path_size)
			{
				if (os_error_out != NULL)
					*os_error_out = ENAMETOOLONG;
				return NULL;
			}
			errno = 0;
			file = fopen(temp_path, "wbx");
			if (file != NULL)
			{
				if (os_error_out != NULL)
					*os_error_out = 0;
				return file;
			}
			if (errno != EEXIST)
				break;
		}
		if (os_error_out != NULL)
			*os_error_out = errno != 0 ? errno : EIO;
		return NULL;
	}
#endif
}

static size_t DefaultWrite(void *context, void *file,
	const unsigned char *bytes, size_t size, int *os_error_out)
{
	size_t count;

	(void)context;
	errno = 0;
	count = fwrite(bytes, 1U, size, (FILE *)file);
	if (os_error_out != NULL)
		*os_error_out = count == size ? 0 : (errno != 0 ? errno : EIO);
	return count;
}

static int DefaultSyncFile(void *context, void *file, int *os_error_out)
{
	int status;

	(void)context;
	errno = 0;
	status = fflush((FILE *)file);
	if (status == 0)
#ifdef _WIN32
		status = _commit(_fileno((FILE *)file));
#else
		status = fsync(fileno((FILE *)file));
#endif
	if (os_error_out != NULL)
		*os_error_out = status == 0 ? 0 : (errno != 0 ? errno : EIO);
	return status == 0;
}

static int DefaultClose(void *context, void *file, int *os_error_out)
{
	int status;

	(void)context;
	errno = 0;
	status = fclose((FILE *)file);
	if (os_error_out != NULL)
		*os_error_out = status == 0 ? 0 : (errno != 0 ? errno : EIO);
	return status == 0;
}

static int DefaultRename(void *context, const char *temporary_path,
	const char *destination, int *os_error_out)
{
	(void)context;
	errno = 0;
	if (temporary_path == NULL || destination == NULL)
	{
		if (os_error_out != NULL)
			*os_error_out = EINVAL;
		return 0;
	}
#ifdef _WIN32
	if (!MoveFileExA(temporary_path, destination,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DWORD windows_error = GetLastError();
		if (os_error_out != NULL)
			*os_error_out = windows_error != 0U ? (int)windows_error : EIO;
		return 0;
	}
	if (os_error_out != NULL)
		*os_error_out = 0;
	return 1;
#else
	{
		int status = rename(temporary_path, destination);
		if (os_error_out != NULL)
			*os_error_out = status == 0 ? 0 : (errno != 0 ? errno : EIO);
		return status == 0;
	}
#endif
}

static int DefaultRemove(void *context, const char *path, int *os_error_out)
{
	int status;

	(void)context;
	errno = 0;
#ifdef _WIN32
	status = _unlink(path);
#else
	status = unlink(path);
#endif
	if (status == 0 || errno == ENOENT)
	{
		if (os_error_out != NULL)
			*os_error_out = 0;
		return 1;
	}
	if (os_error_out != NULL)
		*os_error_out = errno != 0 ? errno : EIO;
	return 0;
}

static int DefaultSyncDirectory(void *context, const char *destination,
	int *os_error_out)
{
	(void)context;
#ifdef _WIN32
	(void)destination;
	/* MoveFileExA(..., MOVEFILE_WRITE_THROUGH) already provides the Windows
	 * replacement barrier.  There is no portable directory-fsync equivalent
	 * here, so report the missing directory barrier instead of claiming that a
	 * no-op made the publication durable. */
	if (os_error_out != NULL)
		*os_error_out = (int)ERROR_CALL_NOT_IMPLEMENTED;
	return 0;
#else
	{
		const char *slash = strrchr(destination, '/');
		char *directory;
		int fd;
		int status;
		int error = 0;
		size_t length;

		if (slash == NULL)
		{
			directory = malloc(2U);
			if (directory != NULL)
				memcpy(directory, ".", 2U);
		}
		else
		{
			length = slash == destination ? 1U : (size_t)(slash - destination);
			directory = malloc(length + 1U);
			if (directory != NULL)
			{
				memcpy(directory, destination, length);
				directory[length] = '\0';
			}
		}
		if (directory == NULL)
		{
			if (os_error_out != NULL)
				*os_error_out = ENOMEM;
			return 0;
		}
		fd = open(directory, O_RDONLY
#ifdef O_DIRECTORY
			| O_DIRECTORY
#endif
#ifdef O_CLOEXEC
			| O_CLOEXEC
#endif
			);
		if (fd < 0)
		{
			error = errno != 0 ? errno : EIO;
			free(directory);
			if (os_error_out != NULL)
				*os_error_out = error;
			return 0;
		}
		errno = 0;
		status = fsync(fd);
		if (status != 0)
			error = errno != 0 ? errno : EIO;
		if (close(fd) != 0 && status == 0)
		{
			status = -1;
			error = errno != 0 ? errno : EIO;
		}
		free(directory);
		if (os_error_out != NULL)
			*os_error_out = status == 0 ? 0 : error;
		return status == 0;
	}
#endif
}

void SG_RuneCompactArtifactDefaultFsOps(
	sg_rune_compact_artifact_fs_ops_t *ops_out)
{
	if (ops_out == NULL)
		return;
	memset(ops_out, 0, sizeof(*ops_out));
	ops_out->open_temp = DefaultOpenTemp;
	ops_out->write = DefaultWrite;
	ops_out->sync_file = DefaultSyncFile;
	ops_out->close_file = DefaultClose;
	ops_out->rename_file = DefaultRename;
	ops_out->remove_file = DefaultRemove;
	ops_out->sync_directory = DefaultSyncDirectory;
}

static int FsOpsValid(const sg_rune_compact_artifact_fs_ops_t *ops)
{
	return ops != NULL && ops->open_temp != NULL && ops->write != NULL &&
		ops->sync_file != NULL && ops->close_file != NULL &&
		ops->rename_file != NULL && ops->remove_file != NULL &&
		ops->sync_directory != NULL;
}

static sg_rune_compact_artifact_publication_result_t PublicationResult(
	sg_rune_compact_artifact_publication_diagnostic_t diagnostic,
	sg_rune_compact_artifact_publication_stage_t stage)
{
	sg_rune_compact_artifact_publication_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.stage = stage;
	ClearWireError(&result.wire_error);
	result.wire_info.wire_version = 0U;
	return result;
}

static void RemoveTemp(const sg_rune_compact_artifact_fs_ops_t *ops,
	const char *temp_path,
	sg_rune_compact_artifact_publication_result_t *result)
{
	int error = 0;

	if (temp_path == NULL || temp_path[0] == '\0')
		return;
	if (!ops->remove_file(ops->context, temp_path, &error) &&
		result->cleanup_error == 0)
		result->cleanup_error = error != 0 ? error : EIO;
}

sg_rune_compact_artifact_publication_result_t
SG_RuneCompactArtifactPublish(
	const char *destination, const unsigned char *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_artifact_fs_ops_t *provided_ops)
{
	sg_rune_compact_artifact_fs_ops_t default_ops;
	const sg_rune_compact_artifact_fs_ops_t *ops = provided_ops;
	sg_rune_compact_artifact_publication_result_t result;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	sg_rune_compact_wire_info_t wire_info;
	sg_rune_compact_wire_error_t wire_error;
	char *temp_path;
	void *file = NULL;
	size_t temp_path_size;
	size_t destination_size;
	size_t offset = 0U;
	int error = 0;
	int close_status;

	result = PublicationResult(
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_INVALID_ARGUMENT,
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ARGUMENT);
	if (destination == NULL || destination[0] == '\0' || image == NULL ||
		expected_identity == NULL || image_size == 0U)
		return result;
	result.image_size = image_size;
	if ((uint64_t)image_size > SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE;
		SetWireError(&result.wire_error,
			SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED);
		return result;
	}
	if (ops == NULL)
	{
		SG_RuneCompactArtifactDefaultFsOps(&default_ops);
		ops = &default_ops;
	}
	if (!FsOpsValid(ops))
		return result;
	memset(&wire_info, 0, sizeof(wire_info));
	ClearWireError(&wire_error);
	if (!SG_RuneCompactWireInspect(image, image_size, &wire_info,
		&wire_error) ||
		!SG_RuneCompactWireDecode(image, image_size, expected_identity,
		&decoded, &wire_error))
	{
		result.diagnostic = wire_error.code ==
			SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY
			? SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED
			: SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE;
		result.wire_error = wire_error;
		return result;
	}
	SG_RuneCompactWireDestroy(decoded);
	result.wire_info = wire_info;
	destination_size = strlen(destination);
	if (destination_size > SIZE_MAX - 64U)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_TEMP_OPEN;
		result.os_error = EOVERFLOW;
		return result;
	}
	temp_path_size = destination_size + 64U;
	temp_path = malloc(temp_path_size);
	if (temp_path == NULL)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_TEMP_OPEN;
		result.os_error = ENOMEM;
		return result;
	}
	temp_path[0] = '\0';
	file = ops->open_temp(ops->context, destination, temp_path,
		temp_path_size, &error);
	if (file == NULL)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_TEMP_OPEN_FAILED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_TEMP_OPEN;
		result.os_error = error != 0 ? error : EIO;
		RemoveTemp(ops, temp_path, &result);
		free(temp_path);
		return result;
	}
	while (offset < image_size)
	{
		size_t count;

		error = 0;
		count = ops->write(ops->context, file, image + offset,
			image_size - offset, &error);
		if (count > image_size - offset)
		{
			result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED;
			result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE;
			result.os_error = EINVAL;
			break;
		}
		result.bytes_transferred += count;
		if (error != 0 || count == 0U)
		{
			result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED;
			result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE;
			result.os_error = error != 0 ? error : EIO;
			break;
		}
		offset += count;
	}
	if (offset != image_size)
	{
		int close_error = 0;
		(void)ops->close_file(ops->context, file, &close_error);
		RemoveTemp(ops, temp_path, &result);
		free(temp_path);
		return result;
	}
	error = 0;
	if (!ops->sync_file(ops->context, file, &error))
	{
		int close_error = 0;
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_FILE_SYNC_FAILED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_FILE_SYNC;
		result.os_error = error != 0 ? error : EIO;
		(void)ops->close_file(ops->context, file, &close_error);
		RemoveTemp(ops, temp_path, &result);
		free(temp_path);
		return result;
	}
	error = 0;
	close_status = ops->close_file(ops->context, file, &error);
	file = NULL;
	if (!close_status)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_FILE_CLOSE_FAILED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_FILE_CLOSE;
		result.os_error = error != 0 ? error : EIO;
		RemoveTemp(ops, temp_path, &result);
		free(temp_path);
		return result;
	}
	error = 0;
	if (!ops->rename_file(ops->context, temp_path, destination, &error))
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_RENAME_FAILED;
		result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_RENAME;
		result.os_error = error != 0 ? error : EIO;
		RemoveTemp(ops, temp_path, &result);
		free(temp_path);
		return result;
	}
	result.published = 1;
	error = 0;
	if (!ops->sync_directory(ops->context, destination, &error))
	{
		result.diagnostic =
			SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_DIRECTORY_SYNC_FAILED;
		result.stage =
			SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DIRECTORY_SYNC;
		result.os_error = error != 0 ? error : EIO;
		result.durable = 0;
		free(temp_path);
		return result;
	}
	result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_OK;
	result.stage = SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DONE;
	result.durable = 1;
	free(temp_path);
	return result;
}

static sg_rune_compact_artifact_publication_stage_t
PublicationStageForEncode(sg_rune_compact_artifact_write_stage_t stage)
{
	switch (stage)
	{
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_MEASURE:
		return SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_MEASURE;
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ALLOCATION:
		return SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ALLOCATION;
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ENCODE:
		return SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ENCODE;
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_VALIDATE:
		return SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE;
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ARGUMENT:
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_SINK:
	case SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_NONE:
	default:
		return SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE;
	}
}

sg_rune_compact_artifact_publication_result_t
SG_RuneCompactArtifactPublishModel(
	const char *destination, const sg_rune_compact_model_t *model,
	const sg_rune_compact_artifact_fs_ops_t *ops)
{
	sg_rune_compact_artifact_publication_result_t result;
	sg_rune_compact_wire_error_t wire_error;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	sg_rune_compact_artifact_write_stage_t encode_stage;

	result = PublicationResult(
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_INVALID_ARGUMENT,
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ARGUMENT);
	if (destination == NULL || destination[0] == '\0' || model == NULL)
		return result;
	if (!EncodeImage(model, &image, &image_size, &wire_error, &encode_stage))
	{
		result.diagnostic = encode_stage ==
			SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ALLOCATION ||
			wire_error.code == SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY
			? SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED
			: SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED;
		result.stage = PublicationStageForEncode(encode_stage);
		result.wire_error = wire_error;
		return result;
	}
	result = SG_RuneCompactArtifactPublish(destination, image, image_size,
		&model->identity, ops);
	free(image);
	return result;
}

const char *SG_RuneCompactArtifactPublicationDiagnosticString(
	sg_rune_compact_artifact_publication_diagnostic_t diagnostic)
{
	static const char *const messages[] = {
		"ok",
		"invalid argument",
		"allocation failed",
		"wire image rejected",
		"temporary open failed",
		"write failed",
		"file sync failed",
		"file close failed",
		"rename failed",
		"directory sync failed"
	};

	_Static_assert(sizeof(messages) / sizeof(messages[0]) ==
		SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_CODE_COUNT,
		"compact artifact publication diagnostics must be complete");
	if ((uint32_t)diagnostic >=
		(uint32_t)SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_CODE_COUNT)
		return "unknown compact artifact publication diagnostic";
	return messages[(uint32_t)diagnostic];
}
