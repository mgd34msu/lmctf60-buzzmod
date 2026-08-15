/* sg_danger_lease.h -- whole-level single-writer danger sidecar lease. */
#ifndef SG_DANGER_LEASE_H
#define SG_DANGER_LEASE_H

#include <stddef.h>

#define SG_DANGER_LEASE_PATH_SUFFIX ".lock"
#define SG_DANGER_LEASE_OPAQUE_BYTES 128U

/* The representation is intentionally opaque while remaining caller-owned.
 * Static-duration objects are already empty; automatic objects must use the
 * initializer before their first acquire. */
typedef union sg_danger_lease_u
{
	unsigned char opaque[SG_DANGER_LEASE_OPAQUE_BYTES];
	/* long double keeps this caller-owned storage aligned for the private
	 * lease state without requiring C11's max_align_t on legacy MSVC. */
	long double alignment;
} sg_danger_lease_t;

#define SG_DANGER_LEASE_INITIALIZER { { 0 } }

typedef enum sg_danger_lease_status_e
{
	SG_DANGER_LEASE_OK = 0,
	SG_DANGER_LEASE_INVALID_ARGUMENT,
	SG_DANGER_LEASE_BAD_DESTINATION,
	SG_DANGER_LEASE_PATH_TOO_LONG,
	SG_DANGER_LEASE_OPEN_FAILED,
	SG_DANGER_LEASE_UNSAFE_LOCKFILE,
	SG_DANGER_LEASE_CONTENDED,
	SG_DANGER_LEASE_LOCK_FAILED,
	SG_DANGER_LEASE_UNLOCK_FAILED,
	SG_DANGER_LEASE_CLOSE_FAILED
} sg_danger_lease_status_t;

typedef enum sg_danger_lease_open_status_e
{
	SG_DANGER_LEASE_OPEN_OK = 0,
	SG_DANGER_LEASE_OPEN_UNSAFE,
	SG_DANGER_LEASE_OPEN_ERROR
} sg_danger_lease_open_status_t;

typedef enum sg_danger_lease_lock_status_e
{
	SG_DANGER_LEASE_LOCK_ACQUIRED = 0,
	SG_DANGER_LEASE_LOCK_CONTENDED,
	SG_DANGER_LEASE_LOCK_ERROR
} sg_danger_lease_lock_status_t;

typedef int (*sg_danger_lease_release_fn)(void *context, void *handle,
	int *os_error_out);

/* open_lock_file uses create-if-absent read/write semantics without truncating
 * an existing lockfile.  It must reject unsafe filesystem objects rather than
 * follow a final-component symlink.  On any non-OK result it may still return
 * a handle that the caller must close.  try_lock never blocks.  close_file
 * always consumes its handle, including when it reports a late error.  On a
 * successful acquire, context and the two release callbacks must remain valid
 * until release; the ops structure itself need not remain alive. */
typedef struct sg_danger_lease_ops_s
{
	void *context;
	sg_danger_lease_open_status_t (*open_lock_file)(void *context,
		const char *path, void **handle_out, int *os_error_out);
	sg_danger_lease_lock_status_t (*try_lock)(void *context, void *handle,
		int *os_error_out);
	sg_danger_lease_release_fn unlock;
	sg_danger_lease_release_fn close_file;
} sg_danger_lease_ops_t;

typedef struct sg_danger_lease_result_s
{
	sg_danger_lease_status_t status;
	int os_error;
	/* Cleanup never obscures the operation that prevented ownership. */
	int cleanup_error;
} sg_danger_lease_result_t;

const sg_danger_lease_ops_t *SG_DangerLeaseDefaultOps(void);
const char *SG_DangerLeaseReason(sg_danger_lease_status_t status);

/* Derive <canonical DNG destination>.lock.  The destination must end in the
 * exact ".rune.danger" suffix.  Output is cleared on every rejection, and an
 * exact fit includes the terminating NUL.  Input and output must not overlap. */
int SG_DangerLeasePath(char *output, size_t output_size,
	const char *canonical_danger_path);

/* Acquire and publish a nonblocking lease.  The caller must pass an empty
 * lease.  Every failed acquisition leaves it empty; lock_path remains the
 * checked derived path after open or contention failures for diagnostics.
 * NULL ops selects the native implementation. */
sg_danger_lease_result_t SG_DangerLeaseAcquire(sg_danger_lease_t *lease,
	const char *canonical_danger_path, char *lock_path,
	size_t lock_path_size, const sg_danger_lease_ops_t *ops);

int SG_DangerLeaseHeld(const sg_danger_lease_t *lease);

/* Release is idempotent.  It explicitly unlocks before consuming the handle;
 * ownership is cleared even if either operation reports a late error. */
sg_danger_lease_result_t SG_DangerLeaseRelease(sg_danger_lease_t *lease);

#endif /* SG_DANGER_LEASE_H */
