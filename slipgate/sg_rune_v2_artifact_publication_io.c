#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "sg_rune_v2_artifact_publication.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

static int IOError(int reported)
{
	return reported != 0 ? reported : EIO;
}

static void *OpenRead(void *context, const char *path, int *not_found_out,
	int *os_error_out)
{
	FILE *file;

	(void)context;
	*not_found_out = 0;
	*os_error_out = 0;
	errno = 0;
	file = fopen(path, "rb");
	if (!file)
	{
		*not_found_out = errno == ENOENT;
		*os_error_out = IOError(errno);
	}
	return file;
}

static void *OpenExclusive(void *context, const char *path, int *os_error_out)
{
	FILE *file;

	(void)context;
	*os_error_out = 0;
	errno = 0;
	file = fopen(path, "w+bx");
	if (!file)
		*os_error_out = IOError(errno);
	return file;
}

static size_t Read(void *context, void *file, unsigned char *output,
	size_t output_size, int *os_error_out)
{
	size_t count;

	(void)context;
	*os_error_out = 0;
	errno = 0;
	count = fread(output, 1U, output_size, (FILE *)file);
	if (count != output_size && ferror((FILE *)file))
		*os_error_out = IOError(errno);
	return count;
}

static size_t ReadAt(void *context, void *file, size_t offset,
	unsigned char *output, size_t output_size, int *os_error_out)
{
	(void)context;
	*os_error_out = 0;
	if (offset > (size_t)INT64_MAX)
	{
		*os_error_out = EOVERFLOW;
		return 0U;
	}
#ifdef _WIN32
	{
		int fd = _fileno((FILE *)file);
		__int64 prior = _telli64(fd);
		int count;

		if (prior < 0 || _lseeki64(fd, (__int64)offset, SEEK_SET) < 0)
		{
			*os_error_out = IOError(errno);
			return 0U;
		}
		count = _read(fd, output, output_size > (size_t)UINT_MAX
			? UINT_MAX : (unsigned int)output_size);
		if (_lseeki64(fd, prior, SEEK_SET) < 0 && count >= 0)
		{
			*os_error_out = IOError(errno);
			return 0U;
		}
		if (count < 0)
		{
			*os_error_out = IOError(errno);
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
			count = pread(fileno((FILE *)file), output, output_size,
				(off_t)offset);
		} while (count < 0 && errno == EINTR);
		if (count < 0)
		{
			*os_error_out = IOError(errno);
			return 0U;
		}
		return (size_t)count;
	}
#endif
}

static size_t Write(void *context, void *file, const unsigned char *bytes,
	size_t size, int *os_error_out)
{
	size_t count;

	(void)context;
	*os_error_out = 0;
	errno = 0;
	count = fwrite(bytes, 1U, size, (FILE *)file);
	if (count != size)
		*os_error_out = IOError(errno);
	return count;
}

static int SyncFile(void *context, void *file, int *os_error_out)
{
	int status;

	(void)context;
	*os_error_out = 0;
	errno = 0;
	status = fflush((FILE *)file);
	if (status == 0)
#ifdef _WIN32
		status = _commit(_fileno((FILE *)file));
#else
		status = fsync(fileno((FILE *)file));
#endif
	if (status != 0)
		*os_error_out = IOError(errno);
	return status == 0;
}

static int Close(void *context, void *file, int *os_error_out)
{
	int status;

	(void)context;
	*os_error_out = 0;
	errno = 0;
	status = fclose((FILE *)file);
	if (status != 0)
		*os_error_out = IOError(errno);
	return status == 0;
}

static int MakeDirectory(void *context, const char *path, int *os_error_out)
{
	int status;

	(void)context;
	*os_error_out = 0;
	errno = 0;
#ifdef _WIN32
	status = _mkdir(path);
#else
	status = mkdir(path, S_IRWXU);
#endif
	if (status != 0)
		*os_error_out = IOError(errno);
	return status == 0;
}

static int RenameGeneration(void *context, const char *from, const char *to,
	int *os_error_out)
{
	(void)context;
	*os_error_out = 0;
#ifdef _WIN32
	if (GetFileAttributesA(to) != INVALID_FILE_ATTRIBUTES)
	{
		*os_error_out = EEXIST;
		return 0;
	}
	if (MoveFileExA(from, to, MOVEFILE_WRITE_THROUGH))
		return 1;
	*os_error_out = EIO;
	return 0;
#else
	{
		struct stat status;

		errno = 0;
		if (lstat(to, &status) == 0)
		{
			*os_error_out = EEXIST;
			return 0;
		}
		if (errno != ENOENT || rename(from, to) != 0)
		{
			*os_error_out = IOError(errno);
			return 0;
		}
		return 1;
	}
#endif
}

static int ReplaceFile(void *context, const char *from, const char *to,
	int *os_error_out)
{
	(void)context;
	*os_error_out = 0;
#ifdef _WIN32
	if (MoveFileExA(from, to,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return 1;
	*os_error_out = EIO;
	return 0;
#else
	errno = 0;
	if (rename(from, to) == 0)
		return 1;
	*os_error_out = IOError(errno);
	return 0;
#endif
}

static int SyncDirectory(void *context, const char *path, int *os_error_out)
{
	(void)context;
	*os_error_out = 0;
#ifdef _WIN32
	(void)path;
	return 1;
#else
	{
		int fd;
		int status;
		int saved = 0;

		fd = open(path, O_RDONLY
#ifdef O_DIRECTORY
			| O_DIRECTORY
#endif
#ifdef O_CLOEXEC
			| O_CLOEXEC
#endif
			);
		if (fd < 0)
		{
			*os_error_out = IOError(errno);
			return 0;
		}
		status = fsync(fd);
		if (status != 0)
			saved = IOError(errno);
		if (close(fd) != 0 && status == 0)
		{
			status = -1;
			saved = IOError(errno);
		}
		if (status != 0)
			*os_error_out = saved;
		return status == 0;
	}
#endif
}

static int InspectDirectory(void *context, const char *path, int *exists_out,
	int *is_owned_directory_out, int *os_error_out)
{
	(void)context;
	*exists_out = 0;
	*is_owned_directory_out = 0;
	*os_error_out = 0;
#ifdef _WIN32
	{
		DWORD attributes = GetFileAttributesA(path);

		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			DWORD error = GetLastError();

			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
				return 1;
			*os_error_out = EIO;
			return 0;
		}
		*exists_out = 1;
		*is_owned_directory_out =
			(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
			(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
		return 1;
	}
#else
	{
		struct stat status;

		errno = 0;
		if (lstat(path, &status) != 0)
		{
			if (errno == ENOENT)
				return 1;
			*os_error_out = IOError(errno);
			return 0;
		}
		*exists_out = 1;
		*is_owned_directory_out = S_ISDIR(status.st_mode);
		return 1;
	}
#endif
}

static int RemoveFile(void *context, const char *path, int *os_error_out)
{
	int status;

	(void)context;
	*os_error_out = 0;
	errno = 0;
#ifdef _WIN32
	status = _unlink(path);
#else
	status = unlink(path);
#endif
	if (status == 0 || errno == ENOENT)
		return 1;
	*os_error_out = IOError(errno);
	return 0;
}

static int RemoveDirectory(void *context, const char *path, int *os_error_out)
{
	int status;

	(void)context;
	*os_error_out = 0;
	errno = 0;
#ifdef _WIN32
	status = _rmdir(path);
#else
	status = rmdir(path);
#endif
	if (status == 0 || errno == ENOENT)
		return 1;
	*os_error_out = IOError(errno);
	return 0;
}

void SG_RuneV2ArtifactPublicationDefaultOps(
	sg_rune_v2_artifact_publication_ops_t *ops_out)
{
	if (!ops_out)
		return;
	memset(ops_out, 0, sizeof(*ops_out));
	ops_out->open_read = OpenRead;
	ops_out->open_exclusive = OpenExclusive;
	ops_out->read = Read;
	ops_out->read_at = ReadAt;
	ops_out->write = Write;
	ops_out->sync_file = SyncFile;
	ops_out->close_file = Close;
	ops_out->make_directory = MakeDirectory;
	ops_out->rename_generation = RenameGeneration;
	ops_out->replace_file = ReplaceFile;
	ops_out->sync_directory = SyncDirectory;
	ops_out->inspect_directory = InspectDirectory;
	ops_out->remove_file = RemoveFile;
	ops_out->remove_directory = RemoveDirectory;
}
