/* Injectable and real-process tests for the whole-level danger lease. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "slipgate/sg_danger_lease.h"

#define TEST_PATH_BYTES 512U

_Static_assert(sizeof(sg_danger_lease_t) == SG_DANGER_LEASE_OPAQUE_BYTES,
	"danger lease ABI storage size drift");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct lease_fixture_s
{
	sg_danger_lease_open_status_t open_status;
	sg_danger_lease_lock_status_t lock_status;
	int open_error;
	int lock_error;
	int unlock_error;
	int close_error;
	int return_failure_handle;
	int handle_open;
	size_t open_calls;
	size_t lock_calls;
	size_t unlock_calls;
	size_t close_calls;
	char opened_path[TEST_PATH_BYTES];
} lease_fixture_t;

static sg_danger_lease_open_status_t TestOpen(void *context,
	const char *path, void **handle_out, int *os_error_out)
{
	lease_fixture_t *fixture = context;

	fixture->open_calls++;
	*handle_out = NULL;
	*os_error_out = fixture->open_error;
	(void)snprintf(fixture->opened_path, sizeof(fixture->opened_path),
		"%s", path);
	if (fixture->open_status == SG_DANGER_LEASE_OPEN_OK ||
	    fixture->return_failure_handle)
	{
		fixture->handle_open = 1;
		*handle_out = fixture;
	}
	return fixture->open_status;
}

static sg_danger_lease_lock_status_t TestTryLock(void *context,
	void *handle, int *os_error_out)
{
	lease_fixture_t *fixture = context;

	fixture->lock_calls++;
	CHECK(handle == fixture);
	CHECK(fixture->handle_open);
	*os_error_out = fixture->lock_error;
	return fixture->lock_status;
}

static int TestUnlock(void *context, void *handle, int *os_error_out)
{
	lease_fixture_t *fixture = context;

	fixture->unlock_calls++;
	CHECK(handle == fixture);
	CHECK(fixture->handle_open);
	*os_error_out = fixture->unlock_error;
	return fixture->unlock_error != 0 ? -1 : 0;
}

static int TestClose(void *context, void *handle, int *os_error_out)
{
	lease_fixture_t *fixture = context;

	fixture->close_calls++;
	CHECK(handle == fixture);
	CHECK(fixture->handle_open);
	fixture->handle_open = 0;
	*os_error_out = fixture->close_error;
	return fixture->close_error != 0 ? -1 : 0;
}

static sg_danger_lease_ops_t TestOps(lease_fixture_t *fixture)
{
	sg_danger_lease_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.context = fixture;
	ops.open_lock_file = TestOpen;
	ops.try_lock = TestTryLock;
	ops.unlock = TestUnlock;
	ops.close_file = TestClose;
	return ops;
}

static void FixtureInit(lease_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->open_status = SG_DANGER_LEASE_OPEN_OK;
	fixture->lock_status = SG_DANGER_LEASE_LOCK_ACQUIRED;
}

static void TestPathContract(void)
{
	static const char destination[] = "/game/maps/arena.rune.danger";
	static const char expected[] = "/game/maps/arena.rune.danger.lock";
	char output[sizeof(expected) + 8U];

	memset(output, 'x', sizeof(output));
	CHECK(SG_DangerLeasePath(output, sizeof(output), destination));
	CHECK(strcmp(output, expected) == 0);

	memset(output, 'x', sizeof(output));
	CHECK(SG_DangerLeasePath(output, sizeof(expected), destination));
	CHECK(strcmp(output, expected) == 0);

	memset(output, 'x', sizeof(output));
	CHECK(!SG_DangerLeasePath(output, sizeof(expected) - 1U, destination));
	CHECK(output[0] == '\0');

	memset(output, 'x', sizeof(output));
	CHECK(!SG_DangerLeasePath(output, sizeof(output),
		"/game/maps/arena.rune.dangerous"));
	CHECK(output[0] == '\0');

	memset(output, 'x', sizeof(output));
	CHECK(!SG_DangerLeasePath(output, sizeof(output), ".rune.danger"));
	CHECK(output[0] == '\0');

	CHECK(!SG_DangerLeasePath(NULL, sizeof(output), destination));
	memset(output, 'x', sizeof(output));
	CHECK(!SG_DangerLeasePath(output, 0, destination));
	CHECK(output[0] == 'x');
	memset(output, 'x', sizeof(output));
	CHECK(!SG_DangerLeasePath(output, sizeof(output), NULL));
	CHECK(output[0] == '\0');
}

static void TestInjectedLifecycle(void)
{
	static const char destination[] = "/game/maps/arena.rune.danger";
	sg_danger_lease_t lease = SG_DANGER_LEASE_INITIALIZER;
	lease_fixture_t fixture;
	sg_danger_lease_ops_t ops;
	sg_danger_lease_result_t result;
	char path[TEST_PATH_BYTES];

	FixtureInit(&fixture);
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(result.os_error == 0 && result.cleanup_error == 0);
	CHECK(SG_DangerLeaseHeld(&lease));
	CHECK(strcmp(path, "/game/maps/arena.rune.danger.lock") == 0);
	CHECK(strcmp(fixture.opened_path, path) == 0);
	CHECK(fixture.open_calls == 1 && fixture.lock_calls == 1);
	CHECK(fixture.unlock_calls == 0 && fixture.close_calls == 0);

	memset(path, 'x', sizeof(path));
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_INVALID_ARGUMENT);
	CHECK(path[0] == '\0');
	CHECK(fixture.open_calls == 1);
	CHECK(SG_DangerLeaseHeld(&lease));

	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(fixture.unlock_calls == 1 && fixture.close_calls == 1);
	CHECK(!fixture.handle_open);

	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(fixture.unlock_calls == 1 && fixture.close_calls == 1);
}

static void TestInjectedAcquireFailures(void)
{
	static const char destination[] = "/game/maps/arena.rune.danger";
	sg_danger_lease_t lease = SG_DANGER_LEASE_INITIALIZER;
	lease_fixture_t fixture;
	sg_danger_lease_ops_t ops;
	sg_danger_lease_result_t result;
	char path[TEST_PATH_BYTES];

	FixtureInit(&fixture);
	fixture.open_status = SG_DANGER_LEASE_OPEN_ERROR;
	fixture.open_error = EACCES;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_OPEN_FAILED);
	CHECK(result.os_error == EACCES && result.cleanup_error == 0);
	CHECK(strcmp(path, "/game/maps/arena.rune.danger.lock") == 0);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(fixture.open_calls == 1 && fixture.lock_calls == 0);
	CHECK(fixture.close_calls == 0);

	FixtureInit(&fixture);
	fixture.open_status = SG_DANGER_LEASE_OPEN_UNSAFE;
	fixture.open_error = EPERM;
	fixture.return_failure_handle = 1;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_UNSAFE_LOCKFILE);
	CHECK(result.os_error == EPERM && result.cleanup_error == 0);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(fixture.lock_calls == 0 && fixture.close_calls == 1);
	CHECK(!fixture.handle_open);

	FixtureInit(&fixture);
	fixture.open_status = SG_DANGER_LEASE_OPEN_ERROR;
	fixture.open_error = EIO;
	fixture.return_failure_handle = 1;
	fixture.close_error = EBADF;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_OPEN_FAILED);
	CHECK(result.os_error == EIO && result.cleanup_error == EBADF);
	CHECK(!SG_DangerLeaseHeld(&lease));

	FixtureInit(&fixture);
	fixture.lock_status = SG_DANGER_LEASE_LOCK_CONTENDED;
	fixture.lock_error = EAGAIN;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_CONTENDED);
	CHECK(result.os_error == EAGAIN && result.cleanup_error == 0);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(fixture.open_calls == 1 && fixture.lock_calls == 1);
	CHECK(fixture.unlock_calls == 0 && fixture.close_calls == 1);
	CHECK(!fixture.handle_open);
	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(fixture.close_calls == 1);

	FixtureInit(&fixture);
	fixture.lock_status = SG_DANGER_LEASE_LOCK_ERROR;
	fixture.lock_error = ENOLCK;
	fixture.close_error = EIO;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_LOCK_FAILED);
	CHECK(result.os_error == ENOLCK && result.cleanup_error == EIO);
	CHECK(!SG_DangerLeaseHeld(&lease));

	FixtureInit(&fixture);
	ops = TestOps(&fixture);
	ops.try_lock = NULL;
	memset(path, 'x', sizeof(path));
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_INVALID_ARGUMENT);
	CHECK(path[0] == '\0');
	CHECK(fixture.open_calls == 0);

	FixtureInit(&fixture);
	ops = TestOps(&fixture);
	memset(path, 'x', sizeof(path));
	result = SG_DangerLeaseAcquire(&lease, "/bad/path", path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_BAD_DESTINATION);
	CHECK(path[0] == '\0' && fixture.open_calls == 0);

	memset(path, 'x', sizeof(path));
	result = SG_DangerLeaseAcquire(&lease, destination, path, 4, &ops);
	CHECK(result.status == SG_DANGER_LEASE_PATH_TOO_LONG);
	CHECK(path[0] == '\0' && fixture.open_calls == 0);
}

static void TestInjectedReleaseFailures(void)
{
	static const char destination[] = "/game/maps/arena.rune.danger";
	sg_danger_lease_t lease = SG_DANGER_LEASE_INITIALIZER;
	lease_fixture_t fixture;
	sg_danger_lease_ops_t ops;
	sg_danger_lease_result_t result;
	char path[TEST_PATH_BYTES];

	FixtureInit(&fixture);
	fixture.unlock_error = ENOLCK;
	fixture.close_error = EIO;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_UNLOCK_FAILED);
	CHECK(result.os_error == ENOLCK && result.cleanup_error == EIO);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(fixture.unlock_calls == 1 && fixture.close_calls == 1);
	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(fixture.unlock_calls == 1 && fixture.close_calls == 1);

	FixtureInit(&fixture);
	fixture.close_error = EBADF;
	ops = TestOps(&fixture);
	result = SG_DangerLeaseAcquire(&lease, destination, path,
		sizeof(path), &ops);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_CLOSE_FAILED);
	CHECK(result.os_error == EBADF && result.cleanup_error == 0);
	CHECK(!SG_DangerLeaseHeld(&lease));
}

static void TestReasonInventory(void)
{
	sg_danger_lease_status_t status;

	for (status = SG_DANGER_LEASE_OK;
	     status <= SG_DANGER_LEASE_CLOSE_FAILED;
	     status = (sg_danger_lease_status_t)((int)status + 1))
		CHECK(strcmp(SG_DangerLeaseReason(status),
			"unknown danger lease status") != 0);
	CHECK(strcmp(SG_DangerLeaseReason((sg_danger_lease_status_t)-1),
		"unknown danger lease status") == 0);
}

#ifndef _WIN32

static int JoinPath(char *output, size_t output_size, const char *directory,
	const char *leaf)
{
	int written = snprintf(output, output_size, "%s/%s", directory, leaf);

	return written >= 0 && (size_t)written < output_size;
}

static int WriteBytes(const char *path, const char *bytes, size_t size)
{
	FILE *file = fopen(path, "wb");
	size_t count;

	if (!file)
		return 0;
	count = fwrite(bytes, 1, size, file);
	return fclose(file) == 0 && count == size;
}

static int FileIsExact(const char *path, const char *bytes, size_t size)
{
	unsigned char buffer[32];
	FILE *file;
	size_t count;
	int trailing;

	if (size > sizeof(buffer))
		return 0;
	file = fopen(path, "rb");
	if (!file)
		return 0;
	count = fread(buffer, 1, sizeof(buffer), file);
	trailing = fgetc(file);
	if (fclose(file) != 0)
		return 0;
	return count == size && trailing == EOF &&
		memcmp(buffer, bytes, size) == 0;
}

static void TestDefaultFilesystemSafety(void)
{
	char directory_template[] = "/tmp/sg-danger-lease-safety-XXXXXX";
	char *directory = mkdtemp(directory_template);
	char destination[TEST_PATH_BYTES];
	char lock_path[TEST_PATH_BYTES];
	char victim[TEST_PATH_BYTES];
	char anchor[TEST_PATH_BYTES];
	char renamed[TEST_PATH_BYTES];
	sg_danger_lease_t lease = SG_DANGER_LEASE_INITIALIZER;
	sg_danger_lease_result_t result;
	struct stat before;
	struct stat after;

	CHECK(directory != NULL);
	if (!directory)
		return;
	CHECK(JoinPath(destination, sizeof(destination), directory,
		"arena.rune.danger"));
	CHECK(JoinPath(victim, sizeof(victim), directory, "victim"));
	CHECK(JoinPath(anchor, sizeof(anchor), directory, "anchor"));
	CHECK(JoinPath(renamed, sizeof(renamed), directory, "replacement"));

	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(SG_DangerLeaseHeld(&lease));
	CHECK(lstat(lock_path, &before) == 0);
	CHECK(S_ISREG(before.st_mode));
	CHECK(before.st_nlink == 1 && before.st_size == 0);
	CHECK(WriteBytes(renamed, "new", 3));
	CHECK(rename(renamed, destination) == 0);
	CHECK(lstat(lock_path, &after) == 0);
	CHECK(before.st_dev == after.st_dev && before.st_ino == after.st_ino);
	result = SG_DangerLeaseRelease(&lease);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(lstat(lock_path, &after) == 0);

	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(SG_DangerLeaseRelease(&lease).status == SG_DANGER_LEASE_OK);
	CHECK(unlink(lock_path) == 0);

	CHECK(WriteBytes(victim, "keep", 4));
	CHECK(symlink(victim, lock_path) == 0);
	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_UNSAFE_LOCKFILE);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(FileIsExact(victim, "keep", 4));
	CHECK(unlink(lock_path) == 0);
	CHECK(unlink(victim) == 0);

	CHECK(WriteBytes(anchor, "", 0));
	CHECK(link(anchor, lock_path) == 0);
	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_UNSAFE_LOCKFILE);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(unlink(lock_path) == 0);
	CHECK(unlink(anchor) == 0);

	CHECK(mkfifo(lock_path, (mode_t)0600) == 0);
	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_UNSAFE_LOCKFILE);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(unlink(lock_path) == 0);

	CHECK(WriteBytes(lock_path, "x", 1));
	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_UNSAFE_LOCKFILE);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(FileIsExact(lock_path, "x", 1));
	CHECK(unlink(lock_path) == 0);

	CHECK(mkdir(lock_path, (mode_t)0700) == 0);
	result = SG_DangerLeaseAcquire(&lease, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status != SG_DANGER_LEASE_OK);
	CHECK(!SG_DangerLeaseHeld(&lease));
	CHECK(rmdir(lock_path) == 0);

	CHECK(unlink(destination) == 0);
	CHECK(rmdir(directory) == 0);
}

static void TestForkContentionAndCrashRelease(void)
{
	char directory_template[] = "/tmp/sg-danger-lease-fork-XXXXXX";
	char *directory = mkdtemp(directory_template);
	char destination[TEST_PATH_BYTES];
	char lock_path[TEST_PATH_BYTES];
	int ready_pipe[2];
	pid_t child;
	char ready = '0';
	ssize_t count;
	int wait_status = 0;
	sg_danger_lease_t contender = SG_DANGER_LEASE_INITIALIZER;
	sg_danger_lease_result_t result;

	CHECK(directory != NULL);
	if (!directory)
		return;
	CHECK(JoinPath(destination, sizeof(destination), directory,
		"arena.rune.danger"));
	CHECK(pipe(ready_pipe) == 0);
	child = fork();
	CHECK(child >= 0);
	if (child == 0)
	{
		sg_danger_lease_t owner = SG_DANGER_LEASE_INITIALIZER;
		sg_danger_lease_result_t child_result;
		char child_path[TEST_PATH_BYTES];
		char child_ready;

		(void)close(ready_pipe[0]);
		child_result = SG_DangerLeaseAcquire(&owner, destination,
			child_path, sizeof(child_path), NULL);
		child_ready = child_result.status == SG_DANGER_LEASE_OK &&
			SG_DangerLeaseHeld(&owner) ? '1' : '0';
		(void)write(ready_pipe[1], &child_ready, 1);
		(void)close(ready_pipe[1]);
		if (child_ready != '1')
			_exit(2);
		for (;;)
			pause();
	}
	if (child < 0)
	{
		(void)close(ready_pipe[0]);
		(void)close(ready_pipe[1]);
		(void)rmdir(directory);
		return;
	}
	(void)close(ready_pipe[1]);
	count = read(ready_pipe[0], &ready, 1);
	(void)close(ready_pipe[0]);
	CHECK(count == 1 && ready == '1');
	if (count == 1 && ready == '1')
	{
		result = SG_DangerLeaseAcquire(&contender, destination,
			lock_path, sizeof(lock_path), NULL);
		CHECK(result.status == SG_DANGER_LEASE_CONTENDED);
		CHECK(!SG_DangerLeaseHeld(&contender));
		CHECK(SG_DangerLeaseRelease(&contender).status ==
			SG_DANGER_LEASE_OK);
	}
	CHECK(kill(child, SIGKILL) == 0);
	CHECK(waitpid(child, &wait_status, 0) == child);
	CHECK(WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGKILL);

	result = SG_DangerLeaseAcquire(&contender, destination, lock_path,
		sizeof(lock_path), NULL);
	CHECK(result.status == SG_DANGER_LEASE_OK);
	CHECK(SG_DangerLeaseHeld(&contender));
	CHECK(SG_DangerLeaseRelease(&contender).status == SG_DANGER_LEASE_OK);
	CHECK(unlink(lock_path) == 0);
	CHECK(rmdir(directory) == 0);
}

#endif /* !_WIN32 */

int main(void)
{
	TestPathContract();
	TestInjectedLifecycle();
	TestInjectedAcquireFailures();
	TestInjectedReleaseFailures();
	TestReasonInventory();
#ifndef _WIN32
	TestDefaultFilesystemSafety();
	TestForkContentionAndCrashRelease();
#endif

	if (failures != 0)
	{
		fprintf(stderr, "sg_danger_lease_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_danger_lease_test: PASS");
	return 0;
}
