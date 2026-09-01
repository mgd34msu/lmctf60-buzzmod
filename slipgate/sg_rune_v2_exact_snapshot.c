#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#elif !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif

#include "sg_rune_v2_exact_snapshot_private.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
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

static wchar_t *AbsoluteWidePath(const wchar_t *wide)
{
	DWORD needed;
	DWORD written;
	wchar_t *absolute;
	size_t index;

	needed = GetFullPathNameW(wide, 0U, NULL, NULL);
	if (needed == 0U || (size_t)needed >= SIZE_MAX / sizeof(*absolute))
		return NULL;
	absolute = (wchar_t *)malloc(((size_t)needed + 1U) * sizeof(*absolute));
	if (!absolute)
		return NULL;
	written = GetFullPathNameW(wide, needed + 1U, absolute, NULL);
	if (written == 0U || written > needed)
	{
		free(absolute);
		return NULL;
	}
	for (index = 0U; absolute[index] != L'\0'; index++)
	{
		if (absolute[index] == L'/')
			absolute[index] = L'\\';
	}
	return absolute;
}

static int IsWideSeparator(wchar_t character)
{
	return character == L'\\' || character == L'/';
}

static size_t WindowsRootLength(const wchar_t *path)
{
	const wchar_t *cursor;
	size_t length = wcslen(path);

	if (length < 3U)
		return 0U;
	if ((path[0] >= L'a' && path[0] <= L'z') ||
	    (path[0] >= L'A' && path[0] <= L'Z'))
		return path[1] == L':' && IsWideSeparator(path[2]) ? 3U : 0U;
	if (!IsWideSeparator(path[0]) || !IsWideSeparator(path[1]))
		return 0U;
	cursor = path + 2;
	if (length >= 4U && path[2] == L'?' && IsWideSeparator(path[3]))
	{
		if (length < 7U)
			return 0U;
		if ((path[4] >= L'a' && path[4] <= L'z') ||
		    (path[4] >= L'A' && path[4] <= L'Z'))
			return path[5] == L':' && IsWideSeparator(path[6]) ? 7U : 0U;
		if (wcsncmp(path + 4, L"UNC\\", 4U) != 0)
			return 0U;
		cursor = path + 8;
	}
	while (*cursor && !IsWideSeparator(*cursor))
		cursor++;
	if (!IsWideSeparator(*cursor))
		return 0U;
	cursor++;
	while (*cursor && !IsWideSeparator(*cursor))
		cursor++;
	return IsWideSeparator(*cursor) ? (size_t)(cursor - path + 1) : 0U;
}

static int WindowsParentsAreNotReparsePoints(wchar_t *path)
{
	size_t root_length = WindowsRootLength(path);
	size_t index;

	if (root_length == 0U)
		return 0;
	for (index = root_length; path[index] != L'\0'; index++)
	{
		DWORD attributes;
		wchar_t saved;

		if (!IsWideSeparator(path[index]))
			continue;
		saved = path[index];
		path[index] = L'\0';
		attributes = GetFileAttributesW(path);
		path[index] = saved;
		if (attributes == INVALID_FILE_ATTRIBUTES ||
		    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
		    (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
			return 0;
	}
	return 1;
}

static wchar_t *WindowsExtendedPath(const wchar_t *path)
{
	const wchar_t *prefix;
	const wchar_t *suffix;
	size_t prefix_length;
	size_t suffix_length;
	wchar_t *extended;

	if (wcsncmp(path, L"\\\\?\\", 4U) == 0)
	{
		prefix = L"";
		suffix = path;
	}
	else if (IsWideSeparator(path[0]) && IsWideSeparator(path[1]))
	{
		prefix = L"\\\\?\\UNC\\";
		suffix = path + 2;
	}
	else
	{
		prefix = L"\\\\?\\";
		suffix = path;
	}
	prefix_length = wcslen(prefix);
	suffix_length = wcslen(suffix);
	if (suffix_length > SIZE_MAX - prefix_length - 1U ||
	    prefix_length + suffix_length + 1U > SIZE_MAX / sizeof(*extended))
		return NULL;
	extended = (wchar_t *)malloc((prefix_length + suffix_length + 1U) *
		sizeof(*extended));
	if (!extended)
		return NULL;
	memcpy(extended, prefix, prefix_length * sizeof(*extended));
	memcpy(extended + prefix_length, suffix,
		(suffix_length + 1U) * sizeof(*extended));
	return extended;
}

static wchar_t *WindowsFinalPath(HANDLE file)
{
	DWORD needed;
	DWORD written;
	wchar_t *path;

	needed = GetFinalPathNameByHandleW(file, NULL, 0U,
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (needed == 0U || (size_t)needed >= SIZE_MAX / sizeof(*path))
		return NULL;
	path = (wchar_t *)malloc(((size_t)needed + 1U) * sizeof(*path));
	if (!path)
		return NULL;
	written = GetFinalPathNameByHandleW(file, path, needed + 1U,
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (written == 0U || written > needed)
	{
		free(path);
		return NULL;
	}
	return path;
}

static int WindowsOpenedPathMatches(HANDLE file, const wchar_t *absolute)
{
	wchar_t *expected = WindowsExtendedPath(absolute);
	wchar_t *actual = WindowsFinalPath(file);
	int matches = expected && actual &&
		CompareStringOrdinal(expected, -1, actual, -1, TRUE) == CSTR_EQUAL;

	free(actual);
	free(expected);
	return matches;
}

static void *DefaultOpenRead(void *context, const char *utf8_path)
{
	wchar_t *wide;
	wchar_t *absolute;
	HANDLE file;
	(void)context;

	wide = WidePath(utf8_path);
	if (!wide)
		return NULL;
	absolute = AbsoluteWidePath(wide);
	free(wide);
	if (!absolute || !WindowsParentsAreNotReparsePoints(absolute))
	{
		free(absolute);
		errno = ELOOP;
		return NULL;
	}
	file = CreateFileW(absolute, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
		FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		DWORD error = GetLastError();

		free(absolute);
		errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
			? ENOENT : EIO;
		return NULL;
	}
	if (!WindowsParentsAreNotReparsePoints(absolute) ||
	    !WindowsOpenedPathMatches(file, absolute))
	{
		(void)CloseHandle(file);
		free(absolute);
		errno = ELOOP;
		return NULL;
	}
	free(absolute);
	return file;
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

static int ComponentIsDotOrDotDot(const char *component, size_t length)
{
	return (length == 1U && component[0] == '.') ||
		(length == 2U && component[0] == '.' && component[1] == '.');
}

static int OpenReadWithoutSymlinkComponents(const char *path)
{
	char component[NAME_MAX + 1U];
	const char *cursor = path;
	int directory;

#ifndef O_NOFOLLOW
	(void)component;
	(void)cursor;
	errno = ENOTSUP;
	return -1;
#else
	int directory_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;

#ifdef O_CLOEXEC
	directory_flags |= O_CLOEXEC;
#endif
	directory = open(path[0] == '/' ? "/" : ".", directory_flags);
	if (directory < 0)
		return -1;
	while (*cursor == '/')
		cursor++;
	while (*cursor)
	{
		const char *next = cursor;
		size_t length;
		int final_component;
		int flags;
		int opened;

		while (*next && *next != '/')
			next++;
		length = (size_t)(next - cursor);
		if (length == 0U || length > NAME_MAX ||
		    ComponentIsDotOrDotDot(cursor, length))
		{
			int saved_error = EINVAL;

			(void)close(directory);
			errno = saved_error;
			return -1;
		}
		memcpy(component, cursor, length);
		component[length] = '\0';
		while (*next == '/')
			next++;
		final_component = *next == '\0';
		flags = final_component ? O_RDONLY | O_NOFOLLOW | O_NONBLOCK
			: directory_flags;

#ifdef O_CLOEXEC
		if (final_component)
			flags |= O_CLOEXEC;
#endif
		opened = openat(directory, component, flags);
		if (opened < 0)
		{
			int saved_error = errno;

			(void)close(directory);
			errno = saved_error;
			return -1;
		}
		(void)close(directory);
		directory = opened;
		cursor = next;
	}
	return directory;
#endif
}

static void *DefaultOpenRead(void *context, const char *utf8_path)
{
	int file;
	int *owned;
	(void)context;

	file = OpenReadWithoutSymlinkComponents(utf8_path);
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
