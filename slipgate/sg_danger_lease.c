/* sg_danger_lease.c -- whole-level single-writer danger sidecar lease. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "slipgate/sg_danger_lease.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define DANGER_DESTINATION_SUFFIX ".rune.danger"
#define DANGER_LEASE_MAGIC UINT64_C(0x5347444c45415345)
#define DANGER_LEASE_GUARD UINT64_C(0xacb8bbb3babeadba)

typedef struct danger_lease_state_s
{
	uint64_t magic;
	uint64_t guard;
	void *handle;
	void *context;
	sg_danger_lease_release_fn unlock;
	sg_danger_lease_release_fn close_file;
} danger_lease_state_t;

_Static_assert(sizeof(danger_lease_state_t) <= SG_DANGER_LEASE_OPAQUE_BYTES,
	"danger lease opaque storage is too small");

static int Lease_Error(int reported)
{
	return reported != 0 ? reported : EIO;
}

static sg_danger_lease_result_t Lease_Result(
	sg_danger_lease_status_t status)
{
	sg_danger_lease_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

static int Lease_Empty(const sg_danger_lease_t *lease)
{
	size_t index;

	if (!lease)
		return 0;
	for (index = 0; index < sizeof(lease->opaque); index++)
		if (lease->opaque[index] != 0)
			return 0;
	return 1;
}

static int Lease_State(const sg_danger_lease_t *lease,
	danger_lease_state_t *state_out)
{
	uint64_t magic;
	uint64_t guard;
	danger_lease_state_t state;

	if (!lease || !state_out)
		return 0;
	memcpy(&magic, lease->opaque, sizeof(magic));
	memcpy(&guard, lease->opaque + sizeof(magic), sizeof(guard));
	if (magic != DANGER_LEASE_MAGIC || guard != DANGER_LEASE_GUARD)
		return 0;
	memcpy(&state, lease->opaque, sizeof(state));
	if (!state.handle || !state.unlock || !state.close_file)
		return 0;
	*state_out = state;
	return 1;
}

static sg_danger_lease_status_t Lease_Path(char *output,
	size_t output_size, const char *canonical_danger_path)
{
	const size_t danger_suffix_size = sizeof(DANGER_DESTINATION_SUFFIX) - 1U;
	const size_t lock_suffix_size = sizeof(SG_DANGER_LEASE_PATH_SUFFIX) - 1U;
	size_t destination_size;

	if (output && output_size > 0)
		output[0] = '\0';
	if (!output || output_size == 0 || !canonical_danger_path)
		return SG_DANGER_LEASE_INVALID_ARGUMENT;
	destination_size = strlen(canonical_danger_path);
	if (destination_size <= danger_suffix_size ||
	    memcmp(canonical_danger_path + destination_size - danger_suffix_size,
	        DANGER_DESTINATION_SUFFIX, danger_suffix_size) != 0)
		return SG_DANGER_LEASE_BAD_DESTINATION;
	if (destination_size > SIZE_MAX - lock_suffix_size - 1U ||
	    destination_size + lock_suffix_size + 1U > output_size)
		return SG_DANGER_LEASE_PATH_TOO_LONG;
	memcpy(output, canonical_danger_path, destination_size);
	memcpy(output + destination_size, SG_DANGER_LEASE_PATH_SUFFIX,
		lock_suffix_size + 1U);
	return SG_DANGER_LEASE_OK;
}

int SG_DangerLeasePath(char *output, size_t output_size,
	const char *canonical_danger_path)
{
	return Lease_Path(output, output_size, canonical_danger_path) ==
		SG_DANGER_LEASE_OK;
}

#ifdef _WIN32

static int Lease_WindowsError(DWORD error)
{
	return error > (DWORD)INT_MAX ? EIO : (int)error;
}

static sg_danger_lease_open_status_t Lease_DefaultOpen(void *context,
	const char *path, void **handle_out, int *os_error_out)
{
	BY_HANDLE_FILE_INFORMATION information;
	HANDLE handle;
	DWORD type;

	(void)context;
	if (handle_out)
		*handle_out = NULL;
	if (os_error_out)
		*os_error_out = 0;
	if (!path || !handle_out)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
	handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
		FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		if (os_error_out)
			*os_error_out = Lease_WindowsError(GetLastError());
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
	*handle_out = handle;
	type = GetFileType(handle);
	if (type != FILE_TYPE_DISK)
	{
		if (os_error_out)
			*os_error_out = ERROR_ACCESS_DENIED;
		return SG_DANGER_LEASE_OPEN_UNSAFE;
	}
	if (!GetFileInformationByHandle(handle, &information))
	{
		if (os_error_out)
			*os_error_out = Lease_WindowsError(GetLastError());
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
	if ((information.dwFileAttributes &
	     (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
	    information.nNumberOfLinks != 1 || information.nFileSizeHigh != 0 ||
	    information.nFileSizeLow != 0)
	{
		if (os_error_out)
			*os_error_out = ERROR_ACCESS_DENIED;
		return SG_DANGER_LEASE_OPEN_UNSAFE;
	}
	return SG_DANGER_LEASE_OPEN_OK;
}

static sg_danger_lease_lock_status_t Lease_DefaultTryLock(void *context,
	void *handle, int *os_error_out)
{
	OVERLAPPED overlapped;
	DWORD error;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (!handle)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return SG_DANGER_LEASE_LOCK_ERROR;
	}
	memset(&overlapped, 0, sizeof(overlapped));
	if (LockFileEx((HANDLE)handle,
	    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
	    MAXDWORD, MAXDWORD, &overlapped))
		return SG_DANGER_LEASE_LOCK_ACQUIRED;
	error = GetLastError();
	if (os_error_out)
		*os_error_out = Lease_WindowsError(error);
	if (error == ERROR_LOCK_VIOLATION || error == ERROR_SHARING_VIOLATION ||
	    error == ERROR_IO_PENDING)
		return SG_DANGER_LEASE_LOCK_CONTENDED;
	return SG_DANGER_LEASE_LOCK_ERROR;
}

static int Lease_DefaultUnlock(void *context, void *handle,
	int *os_error_out)
{
	OVERLAPPED overlapped;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (!handle)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return -1;
	}
	memset(&overlapped, 0, sizeof(overlapped));
	if (UnlockFileEx((HANDLE)handle, 0, MAXDWORD, MAXDWORD, &overlapped))
		return 0;
	if (os_error_out)
		*os_error_out = Lease_WindowsError(GetLastError());
	return -1;
}

static int Lease_DefaultClose(void *context, void *handle,
	int *os_error_out)
{
	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (handle && CloseHandle((HANDLE)handle))
		return 0;
	if (os_error_out)
		*os_error_out = handle ? Lease_WindowsError(GetLastError()) : EINVAL;
	return -1;
}

#else /* !_WIN32 */

static sg_danger_lease_open_status_t Lease_DefaultOpen(void *context,
	const char *path, void **handle_out, int *os_error_out)
{
	struct stat status;
	FILE *file;
	int descriptor;
	int flags = O_CREAT | O_RDWR;
	int saved_error;

	(void)context;
	if (handle_out)
		*handle_out = NULL;
	if (os_error_out)
		*os_error_out = 0;
	if (!path || !handle_out)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
#ifndef O_NOFOLLOW
	if (os_error_out)
		*os_error_out = ENOTSUP;
	return SG_DANGER_LEASE_OPEN_UNSAFE;
#else
	flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
	flags |= O_NONBLOCK;
#endif
	errno = 0;
	descriptor = open(path, flags, (mode_t)0666);
	if (descriptor < 0)
	{
		saved_error = Lease_Error(errno);
		if (os_error_out)
			*os_error_out = saved_error;
		return saved_error == ELOOP ? SG_DANGER_LEASE_OPEN_UNSAFE :
			SG_DANGER_LEASE_OPEN_ERROR;
	}
	errno = 0;
	file = fdopen(descriptor, "r+");
	if (!file)
	{
		saved_error = Lease_Error(errno);
		(void)close(descriptor);
		if (os_error_out)
			*os_error_out = saved_error;
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
	*handle_out = file;
#ifndef O_CLOEXEC
	errno = 0;
	if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0)
	{
		if (os_error_out)
			*os_error_out = Lease_Error(errno);
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
#endif
	errno = 0;
	if (fstat(descriptor, &status) != 0)
	{
		if (os_error_out)
			*os_error_out = Lease_Error(errno);
		return SG_DANGER_LEASE_OPEN_ERROR;
	}
	if (!S_ISREG(status.st_mode) || status.st_nlink != 1 ||
	    status.st_size != 0)
	{
		if (os_error_out)
			*os_error_out = EPERM;
		return SG_DANGER_LEASE_OPEN_UNSAFE;
	}
	return SG_DANGER_LEASE_OPEN_OK;
}

static sg_danger_lease_lock_status_t Lease_DefaultTryLock(void *context,
	void *handle, int *os_error_out)
{
	struct flock lock;
	int status;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (!handle)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return SG_DANGER_LEASE_LOCK_ERROR;
	}
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	errno = 0;
	status = fcntl(fileno((FILE *)handle), F_SETLK, &lock);
	if (status == 0)
		return SG_DANGER_LEASE_LOCK_ACQUIRED;
	if (os_error_out)
		*os_error_out = Lease_Error(errno);
	if (errno == EACCES || errno == EAGAIN)
		return SG_DANGER_LEASE_LOCK_CONTENDED;
	return SG_DANGER_LEASE_LOCK_ERROR;
}

static int Lease_DefaultUnlock(void *context, void *handle,
	int *os_error_out)
{
	struct flock lock;
	int status;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (!handle)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return -1;
	}
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	errno = 0;
	status = fcntl(fileno((FILE *)handle), F_SETLK, &lock);
	if (status != 0 && os_error_out)
		*os_error_out = Lease_Error(errno);
	return status;
}

static int Lease_DefaultClose(void *context, void *handle,
	int *os_error_out)
{
	int status;

	(void)context;
	if (os_error_out)
		*os_error_out = 0;
	if (!handle)
	{
		if (os_error_out)
			*os_error_out = EINVAL;
		return -1;
	}
	errno = 0;
	status = fclose((FILE *)handle);
	if (status != 0 && os_error_out)
		*os_error_out = Lease_Error(errno);
	return status;
}

#endif /* _WIN32 */

static const sg_danger_lease_ops_t danger_lease_default_ops = {
	NULL,
	Lease_DefaultOpen,
	Lease_DefaultTryLock,
	Lease_DefaultUnlock,
	Lease_DefaultClose
};

const sg_danger_lease_ops_t *SG_DangerLeaseDefaultOps(void)
{
	return &danger_lease_default_ops;
}

const char *SG_DangerLeaseReason(sg_danger_lease_status_t status)
{
	static const char *const reasons[] = {
		"ok",
		"invalid argument or nonempty lease",
		"destination is not a canonical danger sidecar path",
		"lock path exceeds the checked output boundary",
		"lockfile open failed",
		"lockfile is not a safe empty regular single-link file",
		"lockfile is held by another process",
		"nonblocking lock operation failed",
		"lock release failed",
		"lockfile close failed"
	};

	if (status < SG_DANGER_LEASE_OK ||
	    (size_t)status >= sizeof(reasons) / sizeof(reasons[0]))
		return "unknown danger lease status";
	return reasons[(size_t)status];
}

static int Lease_OpsValid(const sg_danger_lease_ops_t *ops)
{
	return ops && ops->open_lock_file && ops->try_lock && ops->unlock &&
		ops->close_file;
}

static void Lease_CloseUnowned(sg_danger_lease_result_t *result,
	const sg_danger_lease_ops_t *ops, void *handle)
{
	int os_error = 0;

	if (!result || !ops || !handle)
		return;
	if (ops->close_file(ops->context, handle, &os_error) != 0)
		result->cleanup_error = Lease_Error(os_error);
}

sg_danger_lease_result_t SG_DangerLeaseAcquire(sg_danger_lease_t *lease,
	const char *canonical_danger_path, char *lock_path,
	size_t lock_path_size, const sg_danger_lease_ops_t *provided_ops)
{
	sg_danger_lease_result_t result =
		Lease_Result(SG_DANGER_LEASE_INVALID_ARGUMENT);
	const sg_danger_lease_ops_t *ops = provided_ops;
	sg_danger_lease_open_status_t open_status;
	sg_danger_lease_lock_status_t lock_status;
	sg_danger_lease_status_t path_status;
	danger_lease_state_t state;
	void *handle = NULL;
	int os_error = 0;

	if (lock_path && lock_path_size > 0)
		lock_path[0] = '\0';
	if (!lease || !canonical_danger_path || !lock_path ||
	    lock_path_size == 0 || !Lease_Empty(lease))
		return result;
	if (!ops)
		ops = SG_DangerLeaseDefaultOps();
	if (!Lease_OpsValid(ops))
		return result;
	path_status = Lease_Path(lock_path, lock_path_size,
		canonical_danger_path);
	if (path_status != SG_DANGER_LEASE_OK)
	{
		result.status = path_status;
		return result;
	}

	open_status = ops->open_lock_file(ops->context, lock_path, &handle,
		&os_error);
	if (open_status != SG_DANGER_LEASE_OPEN_OK || !handle)
	{
		result.status = open_status == SG_DANGER_LEASE_OPEN_UNSAFE
			? SG_DANGER_LEASE_UNSAFE_LOCKFILE
			: SG_DANGER_LEASE_OPEN_FAILED;
		result.os_error = Lease_Error(os_error);
		Lease_CloseUnowned(&result, ops, handle);
		return result;
	}

	os_error = 0;
	lock_status = ops->try_lock(ops->context, handle, &os_error);
	if (lock_status != SG_DANGER_LEASE_LOCK_ACQUIRED)
	{
		result.status = lock_status == SG_DANGER_LEASE_LOCK_CONTENDED
			? SG_DANGER_LEASE_CONTENDED : SG_DANGER_LEASE_LOCK_FAILED;
		result.os_error = Lease_Error(os_error);
		Lease_CloseUnowned(&result, ops, handle);
		return result;
	}

	memset(&state, 0, sizeof(state));
	state.magic = DANGER_LEASE_MAGIC;
	state.guard = DANGER_LEASE_GUARD;
	state.handle = handle;
	state.context = ops->context;
	state.unlock = ops->unlock;
	state.close_file = ops->close_file;
	memset(lease, 0, sizeof(*lease));
	memcpy(lease->opaque, &state, sizeof(state));
	result.status = SG_DANGER_LEASE_OK;
	return result;
}

int SG_DangerLeaseHeld(const sg_danger_lease_t *lease)
{
	danger_lease_state_t state;

	return Lease_State(lease, &state);
}

sg_danger_lease_result_t SG_DangerLeaseRelease(sg_danger_lease_t *lease)
{
	sg_danger_lease_result_t result =
		Lease_Result(SG_DANGER_LEASE_INVALID_ARGUMENT);
	danger_lease_state_t state;
	int os_error = 0;
	int close_error = 0;

	if (!lease)
		return result;
	if (Lease_Empty(lease))
	{
		result.status = SG_DANGER_LEASE_OK;
		return result;
	}
	if (!Lease_State(lease, &state))
		return result;

	/* Clear before callbacks so reentrant or repeated release cannot operate on
	 * the same native handle twice.  close_file consumes even on failure. */
	memset(lease, 0, sizeof(*lease));
	if (state.unlock(state.context, state.handle, &os_error) != 0)
	{
		result.status = SG_DANGER_LEASE_UNLOCK_FAILED;
		result.os_error = Lease_Error(os_error);
	}
	os_error = 0;
	if (state.close_file(state.context, state.handle, &os_error) != 0)
	{
		close_error = Lease_Error(os_error);
		if (result.status == SG_DANGER_LEASE_UNLOCK_FAILED)
			result.cleanup_error = close_error;
		else
		{
			result.status = SG_DANGER_LEASE_CLOSE_FAILED;
			result.os_error = close_error;
		}
	}
	if (result.status == SG_DANGER_LEASE_INVALID_ARGUMENT)
		result.status = SG_DANGER_LEASE_OK;
	return result;
}
