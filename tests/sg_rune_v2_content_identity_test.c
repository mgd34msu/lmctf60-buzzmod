#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "slipgate/sg_rune_v2_exact_snapshot.h"
#include "slipgate/sg_rune_v2_exact_snapshot_private.h"
#include "slipgate/sg_rune_v2_artifact_publication_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

typedef struct fault_io_s
{
	unsigned char payload[16];
	size_t payload_size;
	size_t offset;
	uint64_t first_size;
	uint64_t second_size;
	size_t read_chunk;
	unsigned int inspect_calls;
	unsigned int read_calls;
	unsigned int allocation_calls;
	unsigned int deallocation_calls;
	unsigned int fail_inspect_call;
	unsigned int fail_read_call;
	unsigned int fail_allocation_call;
	int fail_open;
	int regular;
	int fail_close;
} fault_io_t;

static void IdentityHex(const sg_rune_v2_content_id_t *identity, char output[65])
{
	static const char digits[] = "0123456789abcdef";
	size_t index;

	for (index = 0U; index < sizeof(identity->bytes); index++)
	{
		output[index * 2U] = digits[identity->bytes[index] >> 4];
		output[index * 2U + 1U] = digits[identity->bytes[index] & 15U];
	}
	output[64] = '\0';
}

static void ExpectDigest(const unsigned char *bytes, size_t size,
	const char *expected)
{
	sg_rune_v2_content_id_t identity;
	char actual[65];

	assert(SG_RuneV2ContentIdentitySHA256(bytes, size, &identity));
	IdentityHex(&identity, actual);
	assert(strcmp(actual, expected) == 0);
}

static void TestSHA256Vectors(void)
{
	unsigned char block[1000];

	memset(block, 'a', sizeof(block));
	ExpectDigest(NULL, 0U,
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	ExpectDigest((const unsigned char *)"abc", 3U,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	ExpectDigest(block, 55U,
		"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
	ExpectDigest(block, 56U,
		"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
	ExpectDigest(block, 63U,
		"7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
	ExpectDigest(block, 64U,
		"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
	ExpectDigest(block, 65U,
		"635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
	ExpectDigest(block, sizeof(block),
		"41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
	memset(&block[0], 0x5a, 32U);
	assert(!SG_RuneV2ContentIdentitySHA256(NULL, 1U,
		(sg_rune_v2_content_id_t *)block));
	assert(block[0] == 0U);
	assert(!SG_RuneV2ContentIdentitySHA256(block, sizeof(block), NULL));
}

static void TestCopyOwnsExactBytes(void)
{
	unsigned char source[] = { 1U, 2U, 3U, 4U };
	unsigned char too_large[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES + 1U];
	sg_rune_v2_exact_snapshot_t *snapshot = NULL;
	const sg_rune_v2_snapshot_view_t *view = NULL;
	sg_rune_v2_content_id_t expected;

	assert(SG_RuneV2ContentIdentitySHA256(source, sizeof(source), &expected));
	assert(SG_RuneV2ExactSnapshotCopyBytes(SG_RUNE_V2_SNAPSHOT_ARTIFACT,
		source, sizeof(source), &snapshot) == SG_RUNE_V2_SNAPSHOT_OK);
	source[0] = 9U;
	assert(SG_RuneV2ExactSnapshotInspect(snapshot, &view) ==
		SG_RUNE_V2_SNAPSHOT_OK);
	assert(view->kind == SG_RUNE_V2_SNAPSHOT_ARTIFACT);
	assert(view->size == 4U);
	assert(view->bytes[0] == 1U);
	assert(SG_RuneV2ContentIdEqual(&view->content_identity, &expected));
	SG_RuneV2ExactSnapshotDestroy(snapshot);

	snapshot = (sg_rune_v2_exact_snapshot_t *)(uintptr_t)1U;
	assert(SG_RuneV2ExactSnapshotCopyBytes(SG_RUNE_V2_SNAPSHOT_PROOF,
		source, sizeof(source), &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_UNSUPPORTED_KIND);
	assert(snapshot == NULL);
	assert(SG_RuneV2ExactSnapshotCopyBytes(SG_RUNE_V2_SNAPSHOT_SIDECAR,
		source, sizeof(source), &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_UNSUPPORTED_KIND);
	assert(snapshot == NULL);
	assert(SG_RuneV2ExactSnapshotCopyBytes((sg_rune_v2_snapshot_kind_t)99,
		source, sizeof(source), &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT);
	assert(snapshot == NULL);
	assert(SG_RuneV2ExactSnapshotCopyBytes(SG_RUNE_V2_SNAPSHOT_ARTIFACT,
		NULL, 1U, &snapshot) == SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT);
	assert(snapshot == NULL);
	assert(SG_RuneV2ExactSnapshotCopyBytes(SG_RUNE_V2_SNAPSHOT_ARTIFACT,
		NULL, 0U, &snapshot) == SG_RUNE_V2_SNAPSHOT_OK);
	SG_RuneV2ExactSnapshotDestroy(snapshot);
	memset(too_large, 0, sizeof(too_large));
	assert(SG_RuneV2ExactSnapshotCopyBytes(SG_RUNE_V2_SNAPSHOT_MANIFEST,
		too_large, sizeof(too_large), &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_TOO_LARGE);
	assert(snapshot == NULL);

	view = (const sg_rune_v2_snapshot_view_t *)(uintptr_t)1U;
	assert(SG_RuneV2ExactSnapshotInspect(NULL, &view) ==
		SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT);
	assert(view == NULL);
	assert(SG_RuneV2ExactSnapshotInspect(NULL, NULL) ==
		SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT);
	SG_RuneV2ExactSnapshotDestroy(NULL);
}

static void *FaultOpen(void *context, const char *path)
{
	fault_io_t *fault = (fault_io_t *)context;
	(void)path;
	return fault->fail_open ? NULL : fault;
}

static int FaultInspect(void *context, void *file,
	sg_rune_v2_snapshot_file_info_t *info_out)
{
	fault_io_t *fault = (fault_io_t *)context;
	(void)file;
	fault->inspect_calls++;
	if (fault->inspect_calls == fault->fail_inspect_call)
		return 0;
	info_out->is_regular = fault->regular;
	info_out->size = fault->inspect_calls == 1U
		? fault->first_size : fault->second_size;
	return 1;
}

static size_t FaultRead(void *context, void *file, unsigned char *output,
	size_t output_size, int *failed_out)
{
	fault_io_t *fault = (fault_io_t *)context;
	size_t amount;
	(void)file;

	fault->read_calls++;
	if (fault->read_calls == fault->fail_read_call)
	{
		*failed_out = 1;
		return 0U;
	}
	*failed_out = 0;
	if (fault->offset == fault->payload_size)
		return 0U;
	amount = fault->payload_size - fault->offset;
	if (amount > output_size)
		amount = output_size;
	if (fault->read_chunk != 0U && amount > fault->read_chunk)
		amount = fault->read_chunk;
	memcpy(output, fault->payload + fault->offset, amount);
	fault->offset += amount;
	return amount;
}

static int FaultClose(void *context, void *file)
{
	fault_io_t *fault = (fault_io_t *)context;
	(void)file;
	return !fault->fail_close;
}

static void *FaultAllocate(void *context, size_t size)
{
	fault_io_t *fault = (fault_io_t *)context;
	fault->allocation_calls++;
	if (fault->allocation_calls == fault->fail_allocation_call)
		return NULL;
	return malloc(size);
}

static void FaultDeallocate(void *context, void *allocation)
{
	fault_io_t *fault = (fault_io_t *)context;
	fault->deallocation_calls++;
	free(allocation);
}

static sg_rune_v2_snapshot_diagnostic_t AcquireFault(fault_io_t *fault,
	sg_rune_v2_exact_snapshot_t **snapshot_out)
{
	sg_rune_v2_snapshot_io_t io;

	memset(&io, 0, sizeof(io));
	io.context = fault;
	io.open_read = FaultOpen;
	io.inspect = FaultInspect;
	io.read = FaultRead;
	io.close_file = FaultClose;
	io.allocate = FaultAllocate;
	io.deallocate = FaultDeallocate;
	return SG_RuneV2ExactSnapshotAcquireWithIO("fixture",
		SG_RUNE_V2_SNAPSHOT_ARTIFACT, snapshot_out, &io);
}

static fault_io_t FaultFixture(void)
{
	fault_io_t fault;

	memset(&fault, 0, sizeof(fault));
	memcpy(fault.payload, "abcd", 4U);
	fault.payload_size = 4U;
	fault.first_size = 4U;
	fault.second_size = 4U;
	fault.regular = 1;
	return fault;
}

static void TestInjectedIOFailures(void)
{
	fault_io_t fault;
	sg_rune_v2_exact_snapshot_t *snapshot;
	const sg_rune_v2_snapshot_view_t *view;
	sg_rune_v2_snapshot_io_t invalid_io;

	fault = FaultFixture();
	fault.read_chunk = 1U;
	snapshot = NULL;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_OK);
	assert(fault.read_calls == 5U);
	assert(SG_RuneV2ExactSnapshotInspect(snapshot, &view) ==
		SG_RUNE_V2_SNAPSHOT_OK);
	assert(memcmp(view->bytes, "abcd", 4U) == 0);
	SG_RuneV2ExactSnapshotDestroy(snapshot);
	assert(fault.deallocation_calls == 2U);

	fault = FaultFixture();
	fault.fail_open = 1;
	snapshot = (sg_rune_v2_exact_snapshot_t *)(uintptr_t)1U;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_OPEN_FAILED);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.fail_inspect_call = 1U;
	assert(AcquireFault(&fault, &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_INSPECT_FAILED);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.regular = 0;
	assert(AcquireFault(&fault, &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_NOT_REGULAR);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.first_size = UINT64_MAX;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_TOO_LARGE);
	assert(snapshot == NULL);
	assert(fault.allocation_calls == 0U);

	fault = FaultFixture();
	fault.fail_allocation_call = 1U;
	assert(AcquireFault(&fault, &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_ALLOCATION_FAILED);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.fail_allocation_call = 2U;
	assert(AcquireFault(&fault, &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_ALLOCATION_FAILED);
	assert(snapshot == NULL);
	assert(fault.deallocation_calls == 1U);

	fault = FaultFixture();
	fault.fail_read_call = 1U;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_READ_FAILED);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.payload_size = 2U;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_SHORT_READ);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.first_size = 3U;
	fault.second_size = 3U;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_EXTRA_BYTES);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.fail_inspect_call = 2U;
	assert(AcquireFault(&fault, &snapshot) ==
		SG_RUNE_V2_SNAPSHOT_INSPECT_FAILED);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.second_size = 5U;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_FILE_CHANGED);
	assert(snapshot == NULL);

	fault = FaultFixture();
	fault.fail_close = 1;
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_CLOSE_FAILED);
	assert(snapshot == NULL);

	memset(&invalid_io, 0, sizeof(invalid_io));
	snapshot = (sg_rune_v2_exact_snapshot_t *)(uintptr_t)1U;
	assert(SG_RuneV2ExactSnapshotAcquireWithIO("fixture",
		SG_RUNE_V2_SNAPSHOT_ARTIFACT, &snapshot, &invalid_io) ==
		SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT);
	assert(snapshot == NULL);

#if SIZE_MAX < UINT64_MAX
	fault = FaultFixture();
	fault.first_size = UINT64_C(4294967296);
	assert(AcquireFault(&fault, &snapshot) == SG_RUNE_V2_SNAPSHOT_TOO_LARGE);
	assert(snapshot == NULL);
	assert(fault.allocation_calls == 0U);
#endif
}

#ifndef _WIN32

static void MakePath(char output[1024], const char *directory,
	const char *leaf)
{
	int written = snprintf(output, 1024U, "%s/%s", directory, leaf);
	assert(written > 0 && written < 1024);
}

static void WriteExactFile(const char *path, const unsigned char *bytes,
	size_t size)
{
	int file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	size_t offset = 0U;

	assert(file >= 0);
	while (offset < size)
	{
		ssize_t amount = write(file, bytes + offset, size - offset);
		assert(amount > 0);
		offset += (size_t)amount;
	}
	assert(close(file) == 0);
}

static void ExpectAcquire(const char *path,
	sg_rune_v2_snapshot_diagnostic_t expected)
{
	sg_rune_v2_exact_snapshot_t *snapshot =
		(sg_rune_v2_exact_snapshot_t *)(uintptr_t)1U;
	sg_rune_v2_snapshot_diagnostic_t actual =
		SG_RuneV2ExactSnapshotAcquireFile(path,
			SG_RUNE_V2_SNAPSHOT_ARTIFACT, &snapshot);

	if (actual != expected)
		fprintf(stderr, "%s: expected %d, got %d\n", path,
			(int)expected, (int)actual);
	assert(actual == expected);
	if (expected == SG_RUNE_V2_SNAPSHOT_OK)
		SG_RuneV2ExactSnapshotDestroy(snapshot);
	else
		assert(snapshot == NULL);
}

static void TestPOSIXFilesystemPolicy(void)
{
	char template_path[] = "/tmp/sg-rune-v2-snapshot-XXXXXX";
	char *directory = mkdtemp(template_path);
	char regular[1024];
	char hardlink_path[1024];
	char symlink_path[1024];
	char fifo_path[1024];
	char socket_path[1024];
	char directory_path[1024];
	char sparse_path[1024];
	struct sockaddr_un address;
	int socket_file;
	int sparse_file;

	assert(directory);
	MakePath(regular, directory, "regular");
	MakePath(hardlink_path, directory, "hardlink");
	MakePath(symlink_path, directory, "symlink");
	MakePath(fifo_path, directory, "fifo");
	MakePath(socket_path, directory, "socket");
	MakePath(directory_path, directory, "directory");
	MakePath(sparse_path, directory, "sparse");
	WriteExactFile(regular, (const unsigned char *)"exact", 5U);
	assert(link(regular, hardlink_path) == 0);
	assert(symlink(regular, symlink_path) == 0);
	assert(mkfifo(fifo_path, 0600) == 0);
	assert(mkdir(directory_path, 0700) == 0);
	socket_file = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(socket_file >= 0);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	assert(strlen(socket_path) < sizeof(address.sun_path));
	memcpy(address.sun_path, socket_path, strlen(socket_path) + 1U);
	assert(bind(socket_file, (const struct sockaddr *)&address,
		(socklen_t)sizeof(address)) == 0);

	ExpectAcquire(regular, SG_RUNE_V2_SNAPSHOT_OK);
	ExpectAcquire(hardlink_path, SG_RUNE_V2_SNAPSHOT_OK);
	ExpectAcquire(symlink_path, SG_RUNE_V2_SNAPSHOT_OPEN_FAILED);
	ExpectAcquire(fifo_path, SG_RUNE_V2_SNAPSHOT_NOT_REGULAR);
	ExpectAcquire(socket_path, SG_RUNE_V2_SNAPSHOT_OPEN_FAILED);
	ExpectAcquire(directory_path, SG_RUNE_V2_SNAPSHOT_NOT_REGULAR);
	ExpectAcquire("/dev/null", SG_RUNE_V2_SNAPSHOT_NOT_REGULAR);

	sparse_file = open(sparse_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	assert(sparse_file >= 0);
	assert(ftruncate(sparse_file, (off_t)UINT64_C(4294967297)) == 0);
	assert(close(sparse_file) == 0);
	ExpectAcquire(sparse_path, SG_RUNE_V2_SNAPSHOT_TOO_LARGE);

	assert(close(socket_file) == 0);
	assert(unlink(socket_path) == 0);
	assert(unlink(sparse_path) == 0);
	assert(rmdir(directory_path) == 0);
	assert(unlink(fifo_path) == 0);
	assert(unlink(symlink_path) == 0);
	assert(unlink(hardlink_path) == 0);
	assert(unlink(regular) == 0);
	assert(rmdir(directory) == 0);
}

typedef struct replacement_io_s
{
	const char *replacement_path;
	const char *active_path;
} replacement_io_t;

static void *ReplacementOpen(void *context, const char *path)
{
	replacement_io_t *replacement = (replacement_io_t *)context;
	FILE *file = fopen(path, "rb");

	if (!file)
		return NULL;
	if (rename(replacement->replacement_path, replacement->active_path) != 0)
	{
		(void)fclose(file);
		return NULL;
	}
	return file;
}

static int ReplacementInspect(void *context, void *file,
	sg_rune_v2_snapshot_file_info_t *info_out)
{
	struct stat status;
	(void)context;
	if (fstat(fileno((FILE *)file), &status) != 0 || status.st_size < 0)
		return 0;
	info_out->is_regular = S_ISREG(status.st_mode) ? 1 : 0;
	info_out->size = (uint64_t)status.st_size;
	return 1;
}

static size_t ReplacementRead(void *context, void *file,
	unsigned char *output, size_t output_size, int *failed_out)
{
	size_t amount;
	(void)context;
	amount = fread(output, 1U, output_size, (FILE *)file);
	*failed_out = ferror((FILE *)file) ? 1 : 0;
	return amount;
}

static int ReplacementClose(void *context, void *file)
{
	(void)context;
	return fclose((FILE *)file) == 0;
}

static void *ReplacementAllocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

static void ReplacementDeallocate(void *context, void *allocation)
{
	(void)context;
	free(allocation);
}

static void TestReplacementAfterOpen(void)
{
	char template_path[] = "/tmp/sg-rune-v2-replace-XXXXXX";
	char *directory = mkdtemp(template_path);
	char active[1024];
	char replacement_path[1024];
	replacement_io_t replacement;
	sg_rune_v2_snapshot_io_t io;
	sg_rune_v2_exact_snapshot_t *snapshot = NULL;
	const sg_rune_v2_snapshot_view_t *view = NULL;

	assert(directory);
	MakePath(active, directory, "active");
	MakePath(replacement_path, directory, "replacement");
	WriteExactFile(active, (const unsigned char *)"old", 3U);
	WriteExactFile(replacement_path, (const unsigned char *)"new", 3U);
	replacement.replacement_path = replacement_path;
	replacement.active_path = active;
	memset(&io, 0, sizeof(io));
	io.context = &replacement;
	io.open_read = ReplacementOpen;
	io.inspect = ReplacementInspect;
	io.read = ReplacementRead;
	io.close_file = ReplacementClose;
	io.allocate = ReplacementAllocate;
	io.deallocate = ReplacementDeallocate;
	assert(SG_RuneV2ExactSnapshotAcquireWithIO(active,
		SG_RUNE_V2_SNAPSHOT_ARTIFACT, &snapshot, &io) ==
		SG_RUNE_V2_SNAPSHOT_OK);
	assert(SG_RuneV2ExactSnapshotInspect(snapshot, &view) ==
		SG_RUNE_V2_SNAPSHOT_OK);
	assert(view->size == 3U);
	assert(memcmp(view->bytes, "old", 3U) == 0);
	SG_RuneV2ExactSnapshotDestroy(snapshot);
	assert(unlink(active) == 0);
	assert(rmdir(directory) == 0);
}

#endif

int main(void)
{
	TestSHA256Vectors();
	TestCopyOwnsExactBytes();
	TestInjectedIOFailures();
#ifndef _WIN32
	TestPOSIXFilesystemPolicy();
	TestReplacementAfterOpen();
#endif
	return 0;
}
