#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#elif !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif

#include "sg_rune_v2_exact_snapshot_private.h"

#include "sg_rune_v2_artifact_publication_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct sg_rune_v2_exact_snapshot_s
{
	sg_rune_v2_snapshot_view_t view;
	unsigned char *owned_bytes;
	void *allocator_context;
	void (*deallocate)(void *context, void *allocation);
};

static sg_rune_v2_snapshot_diagnostic_t KindLimit(
	sg_rune_v2_snapshot_kind_t kind, uint64_t *limit_out)
{
	if (!limit_out)
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	switch (kind)
	{
	case SG_RUNE_V2_SNAPSHOT_ARTIFACT:
		*limit_out = SG_RUNE_V2_MAX_ARTIFACT_BYTES;
		return SG_RUNE_V2_SNAPSHOT_OK;
	case SG_RUNE_V2_SNAPSHOT_MANIFEST:
		*limit_out = SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES;
		return SG_RUNE_V2_SNAPSHOT_OK;
	case SG_RUNE_V2_SNAPSHOT_PROOF:
	case SG_RUNE_V2_SNAPSHOT_SIDECAR:
		return SG_RUNE_V2_SNAPSHOT_UNSUPPORTED_KIND;
	default:
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	}
}

static sg_rune_v2_snapshot_diagnostic_t SnapshotAdopt(
	sg_rune_v2_snapshot_kind_t kind, unsigned char *owned_bytes, size_t size,
	void *allocator_context,
	void *(*allocate)(void *context, size_t allocation_size),
	void (*deallocate)(void *context, void *allocation),
	sg_rune_v2_exact_snapshot_t **snapshot_out)
{
	sg_rune_v2_exact_snapshot_t *snapshot;

	snapshot = allocate(allocator_context, sizeof(*snapshot));
	if (!snapshot)
	{
		deallocate(allocator_context, owned_bytes);
		return SG_RUNE_V2_SNAPSHOT_ALLOCATION_FAILED;
	}
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->owned_bytes = owned_bytes;
	snapshot->allocator_context = allocator_context;
	snapshot->deallocate = deallocate;
	snapshot->view.kind = kind;
	snapshot->view.bytes = owned_bytes;
	snapshot->view.size = size;
	if (!SG_RuneV2ContentIdentitySHA256(owned_bytes, size,
		&snapshot->view.content_identity))
	{
		deallocate(allocator_context, owned_bytes);
		deallocate(allocator_context, snapshot);
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	}
	*snapshot_out = snapshot;
	return SG_RUNE_V2_SNAPSHOT_OK;
}

static void *HeapAllocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

static void HeapDeallocate(void *context, void *allocation)
{
	(void)context;
	free(allocation);
}

sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotCopyBytes(
	sg_rune_v2_snapshot_kind_t kind, const unsigned char *bytes, size_t size,
	sg_rune_v2_exact_snapshot_t **snapshot_out)
{
	unsigned char *copy;
	uint64_t limit;
	uint64_t encoded_size;
	sg_rune_v2_snapshot_diagnostic_t diagnostic;

	if (!snapshot_out)
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	*snapshot_out = NULL;
	diagnostic = KindLimit(kind, &limit);
	if (diagnostic != SG_RUNE_V2_SNAPSHOT_OK)
		return diagnostic;
	if (!bytes && size != 0U)
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	encoded_size = (uint64_t)size;
	if ((size_t)encoded_size != size || encoded_size > limit)
		return SG_RUNE_V2_SNAPSHOT_TOO_LARGE;
	copy = HeapAllocate(NULL, size == 0U ? 1U : size);
	if (!copy)
		return SG_RUNE_V2_SNAPSHOT_ALLOCATION_FAILED;
	if (size != 0U)
		memcpy(copy, bytes, size);
	return SnapshotAdopt(kind, copy, size, NULL, HeapAllocate,
		HeapDeallocate, snapshot_out);
}

static int IOValid(const sg_rune_v2_snapshot_io_t *io)
{
	return io && io->open_read && io->inspect && io->read &&
		io->close_file && io->allocate && io->deallocate;
}

sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotAcquireWithIO(
	const char *utf8_path, sg_rune_v2_snapshot_kind_t kind,
	sg_rune_v2_exact_snapshot_t **snapshot_out,
	const sg_rune_v2_snapshot_io_t *io)
{
	void *file;
	unsigned char *bytes = NULL;
	sg_rune_v2_snapshot_file_info_t before;
	sg_rune_v2_snapshot_file_info_t after;
	sg_rune_v2_snapshot_diagnostic_t diagnostic;
	uint64_t limit;
	size_t expected;
	size_t offset = 0U;
	int failed = 0;
	unsigned char extra;

	if (!snapshot_out)
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	*snapshot_out = NULL;
	if (!utf8_path || utf8_path[0] == '\0' || !IOValid(io))
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	diagnostic = KindLimit(kind, &limit);
	if (diagnostic != SG_RUNE_V2_SNAPSHOT_OK)
		return diagnostic;
	file = io->open_read(io->context, utf8_path);
	if (!file)
		return SG_RUNE_V2_SNAPSHOT_OPEN_FAILED;
	memset(&before, 0, sizeof(before));
	if (!io->inspect(io->context, file, &before))
		diagnostic = SG_RUNE_V2_SNAPSHOT_INSPECT_FAILED;
	else if (!before.is_regular)
		diagnostic = SG_RUNE_V2_SNAPSHOT_NOT_REGULAR;
	else if (before.size > limit || (uint64_t)(size_t)before.size != before.size)
		diagnostic = SG_RUNE_V2_SNAPSHOT_TOO_LARGE;
	else
	{
		expected = (size_t)before.size;
		bytes = io->allocate(io->context, expected == 0U ? 1U : expected);
		if (!bytes)
			diagnostic = SG_RUNE_V2_SNAPSHOT_ALLOCATION_FAILED;
		else
		{
			diagnostic = SG_RUNE_V2_SNAPSHOT_OK;
			while (offset < expected)
			{
				size_t amount = io->read(io->context, file, bytes + offset,
					expected - offset, &failed);

				if (failed || amount > expected - offset)
				{
					diagnostic = SG_RUNE_V2_SNAPSHOT_READ_FAILED;
					break;
				}
				if (amount == 0U)
				{
					diagnostic = SG_RUNE_V2_SNAPSHOT_SHORT_READ;
					break;
				}
				offset += amount;
			}
			if (diagnostic == SG_RUNE_V2_SNAPSHOT_OK)
			{
				size_t amount = io->read(io->context, file, &extra, 1U,
					&failed);

				if (failed)
					diagnostic = SG_RUNE_V2_SNAPSHOT_READ_FAILED;
				else if (amount != 0U)
					diagnostic = SG_RUNE_V2_SNAPSHOT_EXTRA_BYTES;
			}
			if (diagnostic == SG_RUNE_V2_SNAPSHOT_OK)
			{
				memset(&after, 0, sizeof(after));
				if (!io->inspect(io->context, file, &after))
					diagnostic = SG_RUNE_V2_SNAPSHOT_INSPECT_FAILED;
				else if (!after.is_regular || after.size != before.size)
					diagnostic = SG_RUNE_V2_SNAPSHOT_FILE_CHANGED;
			}
		}
	}
	if (!io->close_file(io->context, file) &&
		diagnostic == SG_RUNE_V2_SNAPSHOT_OK)
		diagnostic = SG_RUNE_V2_SNAPSHOT_CLOSE_FAILED;
	if (diagnostic != SG_RUNE_V2_SNAPSHOT_OK)
	{
		if (bytes)
			io->deallocate(io->context, bytes);
		return diagnostic;
	}
	return SnapshotAdopt(kind, bytes, (size_t)before.size, io->context,
		io->allocate, io->deallocate, snapshot_out);
}

#ifdef _WIN32

static wchar_t *WidePath(const char *utf8_path)
{
	int count;
	wchar_t *wide;

	count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path,
		-1, NULL, 0);
	if (count <= 0)
		return NULL;
	wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
	if (!wide)
		return NULL;
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path,
		-1, wide, count) != count)
	{
		free(wide);
		return NULL;
	}
	return wide;
}

static void *DefaultOpenRead(void *context, const char *utf8_path)
{
	wchar_t *wide;
	HANDLE file;
	(void)context;

	wide = WidePath(utf8_path);
	if (!wide)
		return NULL;
	file = CreateFileW(wide, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
		FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	free(wide);
	return file == INVALID_HANDLE_VALUE ? NULL : file;
}

static int DefaultInspect(void *context, void *file,
	sg_rune_v2_snapshot_file_info_t *info_out)
{
	FILE_ATTRIBUTE_TAG_INFO attributes;
	FILE_STANDARD_INFO standard;
	HANDLE handle = (HANDLE)file;
	(void)context;

	if (GetFileType(handle) != FILE_TYPE_DISK ||
		!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
			&attributes, (DWORD)sizeof(attributes)) ||
		!GetFileInformationByHandleEx(handle, FileStandardInfo,
			&standard, (DWORD)sizeof(standard)))
		return 0;
	info_out->is_regular =
		(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U &&
		!standard.Directory && !standard.DeletePending;
	if (standard.EndOfFile.QuadPart < 0)
		return 0;
	info_out->size = (uint64_t)standard.EndOfFile.QuadPart;
	return 1;
}

static size_t DefaultRead(void *context, void *file, unsigned char *output,
	size_t output_size, int *failed_out)
{
	DWORD request = output_size > (size_t)UINT32_MAX
		? UINT32_MAX : (DWORD)output_size;
	DWORD amount = 0U;
	(void)context;

	if (!ReadFile((HANDLE)file, output, request, &amount, NULL))
	{
		*failed_out = 1;
		return 0U;
	}
	*failed_out = 0;
	return (size_t)amount;
}

static int DefaultClose(void *context, void *file)
{
	(void)context;
	return CloseHandle((HANDLE)file) != 0;
}

#else

static void *DefaultOpenRead(void *context, const char *utf8_path)
{
	int flags = O_RDONLY;
	int file;
	int *owned;
	(void)context;

#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
	flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	file = open(utf8_path, flags);
	if (file < 0)
		return NULL;
	owned = (int *)malloc(sizeof(*owned));
	if (!owned)
	{
		(void)close(file);
		return NULL;
	}
	*owned = file;
	return owned;
}

static int DefaultInspect(void *context, void *file,
	sg_rune_v2_snapshot_file_info_t *info_out)
{
	struct stat status;
	(void)context;

	if (fstat(*(int *)file, &status) != 0 || status.st_size < 0)
		return 0;
	info_out->is_regular = S_ISREG(status.st_mode) ? 1 : 0;
	info_out->size = (uint64_t)status.st_size;
	return 1;
}

static size_t DefaultRead(void *context, void *file, unsigned char *output,
	size_t output_size, int *failed_out)
{
	ssize_t amount;
	(void)context;

	do
	{
		errno = 0;
		amount = read(*(int *)file, output, output_size);
	} while (amount < 0 && errno == EINTR);
	if (amount < 0)
	{
		*failed_out = 1;
		return 0U;
	}
	*failed_out = 0;
	return (size_t)amount;
}

static int DefaultClose(void *context, void *file)
{
	int descriptor = *(int *)file;
	int result;
	(void)context;

	free(file);
	result = close(descriptor);
	return result == 0;
}

#endif

sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotAcquireFile(
	const char *utf8_path, sg_rune_v2_snapshot_kind_t kind,
	sg_rune_v2_exact_snapshot_t **snapshot_out)
{
	sg_rune_v2_snapshot_io_t io;

	memset(&io, 0, sizeof(io));
	io.open_read = DefaultOpenRead;
	io.inspect = DefaultInspect;
	io.read = DefaultRead;
	io.close_file = DefaultClose;
	io.allocate = HeapAllocate;
	io.deallocate = HeapDeallocate;
	return SG_RuneV2ExactSnapshotAcquireWithIO(utf8_path, kind,
		snapshot_out, &io);
}

sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotInspect(
	const sg_rune_v2_exact_snapshot_t *snapshot,
	const sg_rune_v2_snapshot_view_t **view_out)
{
	if (!view_out)
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	*view_out = NULL;
	if (!snapshot)
		return SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT;
	*view_out = &snapshot->view;
	return SG_RUNE_V2_SNAPSHOT_OK;
}

void SG_RuneV2ExactSnapshotDestroy(sg_rune_v2_exact_snapshot_t *snapshot)
{
	void *context;
	void (*deallocate)(void *allocator_context, void *allocation);
	unsigned char *bytes;

	if (!snapshot)
		return;
	context = snapshot->allocator_context;
	deallocate = snapshot->deallocate;
	bytes = snapshot->owned_bytes;
	memset(snapshot, 0, sizeof(*snapshot));
	deallocate(context, bytes);
	deallocate(context, snapshot);
}
