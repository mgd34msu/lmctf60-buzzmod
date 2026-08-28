#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slipgate/sg_rune_v2_artifact_publication.h"

static int failures;
static size_t filesystem_fault_points;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct exact_file_s
{
	sg_rune_v2_content_id_t identity;
	const unsigned char *bytes;
	size_t size;
} exact_file_t;

typedef struct verifier_s
{
	exact_file_t files[6];
	size_t file_count;
	size_t calls;
	size_t fail_call;
} verifier_t;

#include "support/sg_rune_v2_artifact_publication_faults.h"

static int Publish(const char *root, int newer, uint64_t generation,
	verifier_t *verifier, const sg_rune_v2_artifact_publication_ops_t *ops,
	sg_rune_v2_artifact_publication_result_t *result_out)
{
	sg_rune_v2_accepted_artifact_t accepted;
	sg_rune_v2_publication_sidecar_file_t sidecars[2];
	sg_rune_v2_publication_candidate_t candidate = Candidate(generation,
		newer, verifier, &accepted, sidecars);

	*result_out = SG_RuneV2ArtifactPublicationPublish(root, &candidate, ops);
	return SG_RuneV2ArtifactPublicationSucceeded(result_out);
}

static void RemoveGeneration(const char *root, uint64_t generation,
	const char *prefix)
{
	char path[512];
	unsigned int kind;
	int written;

	written = snprintf(path, sizeof(path), "%s/%s-%016llx/artifact.rune",
		root, prefix, (unsigned long long)generation);
	CHECK(written >= 0 && (size_t)written < sizeof(path));
	(void)remove(path);
	for (kind = 1U; kind <= SG_RUNE_V2_MAX_SIDECARS; kind++)
	{
		written = snprintf(path, sizeof(path),
			"%s/%s-%016llx/sidecar-%02u", root, prefix,
			(unsigned long long)generation, kind);
		CHECK(written >= 0 && (size_t)written < sizeof(path));
		(void)remove(path);
	}
	written = snprintf(path, sizeof(path), "%s/%s-%016llx", root, prefix,
		(unsigned long long)generation);
	CHECK(written >= 0 && (size_t)written < sizeof(path));
	(void)rmdir(path);
}

static void CleanupRoot(char *root)
{
	char path[512];

	RemoveGeneration(root, 6U, "generation");
	RemoveGeneration(root, 7U, "generation");
	RemoveGeneration(root, 8U, "generation");
	RemoveGeneration(root, 7U, ".staging");
	RemoveGeneration(root, 8U, ".staging");
	CHECK(Format(path, sizeof(path), "%s/.CURRENT-%016llx.tmp", root, 7U, 0U));
	(void)remove(path);
	CHECK(Format(path, sizeof(path), "%s/.CURRENT-%016llx.tmp", root, 8U, 0U));
	(void)remove(path);
	CHECK(snprintf(path, sizeof(path), "%s/CURRENT", root) > 0);
	(void)remove(path);
	CHECK(rmdir(root) == 0);
}

static void TestArgumentsAndIdentityBoundary(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	verifier_t verifier;
	sg_rune_v2_accepted_artifact_t accepted;
	sg_rune_v2_publication_sidecar_file_t sidecars[2];
	sg_rune_v2_publication_candidate_t candidate;
	sg_rune_v2_artifact_publication_result_t result;
	sg_rune_v2_content_id_t zero = { { 0 } };

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	candidate = Candidate(7U, 0, &verifier, &accepted, sidecars);
	accepted.artifact_identity = zero;
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	CHECK(verifier.calls == 0U);
	accepted = Accepted(7U, 10U, 110U);
	sidecars[0].exact_identity = zero;
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	sidecars[0].exact_identity = Identity(30U);
	accepted.sidecar_set_identity = zero;
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	accepted = Accepted(7U, 10U, 110U);
	sidecars[0].exact_identity = Identity(61U);
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	sidecars[0].exact_identity = Identity(30U);
	sidecars[0].kind = 1U;
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	sidecars[0].kind = 3U;
	accepted.sidecars[0] = (sg_rune_v2_accepted_sidecar_t){ 3U,
		Identity(30U) };
	accepted.sidecars[1] = (sg_rune_v2_accepted_sidecar_t){ 1U,
		Identity(20U) };
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	accepted = Accepted(7U, 10U, 110U);
	accepted.sidecars[2].kind = 4U;
	accepted.sidecars[2].exact_identity = Identity(62U);
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	accepted.sidecars[2] = (sg_rune_v2_accepted_sidecar_t){ 0 };
	candidate.verify_staged_file = NULL;
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT);
	CleanupRoot(root);
}

static void TestSuccessShortWritesStaleAndConflict(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	verifier_t verifier;
	fault_ops_t fault;
	sg_rune_v2_artifact_publication_ops_t ops;
	sg_rune_v2_artifact_publication_result_t result;
	sg_rune_v2_accepted_artifact_t accepted;
	sg_rune_v2_publication_sidecar_file_t sidecars[2];
	sg_rune_v2_publication_candidate_t candidate;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	memset(&fault, 0, sizeof(fault));
	fault.write_limit = 1U;
	ops = FaultOps(&fault);
	CHECK(Publish(root, 0, 7U, &verifier, &ops, &result));
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK);
	CHECK(result.commit_visible && result.durability_complete);
	CHECK(verifier.calls == 3U);
	CHECK(CheckComplete(root, NULL) == 7U);
	verifier.calls = 0U;
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_ALREADY_ACTIVE);
	CHECK(verifier.calls == 3U);
	CHECK(!Publish(root, 0, 6U, &verifier, NULL, &result));
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_STALE_GENERATION);
	candidate = Candidate(7U, 1, &verifier, &accepted, sidecars);
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_GENERATION_CONFLICT);
	CHECK(CheckComplete(root, NULL) == 7U);
	CleanupRoot(root);
}

static void TestEmptySidecarSet(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	verifier_t verifier;
	sg_rune_v2_accepted_artifact_t accepted = Accepted(7U, 10U, 110U);
	sg_rune_v2_publication_candidate_t candidate;
	sg_rune_v2_active_generation_t active;
	sg_rune_v2_artifact_publication_result_t result;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	accepted.sidecar_mask = 0U;
	accepted.sidecar_count = 0U;
	accepted.sidecar_set_identity = (sg_rune_v2_content_id_t){ { 0 } };
	memset(accepted.sidecars, 0, sizeof(accepted.sidecars));
	candidate.accepted = &accepted;
	candidate.artifact_bytes = OLD_ARTIFACT;
	candidate.artifact_size = sizeof(OLD_ARTIFACT);
	candidate.sidecars = NULL;
	candidate.sidecar_count = 0U;
	candidate.verify_staged_file = VerifyStaged;
	candidate.verify_context = &verifier;
	result = SG_RuneV2ArtifactPublicationPublish(root, &candidate, NULL);
	CHECK(SG_RuneV2ArtifactPublicationSucceeded(&result));
	result = SG_RuneV2ArtifactPublicationReadActive(root, &active, NULL);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK);
	CHECK(active.accepted.sidecar_count == 0U);
	CHECK(active.accepted.sidecar_mask == 0U);
	CHECK(!SG_RuneV2ContentIdValid(&active.accepted.sidecar_set_identity));
	CleanupRoot(root);
}

static void TestCleanupPreservesActiveGeneration(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	char guard[512];
	verifier_t verifier;
	sg_rune_v2_artifact_publication_result_t result;
	FILE *file;
	struct stat status;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	CHECK(Format(guard, sizeof(guard),
		"%s/generation-%016llx/active.guard", root, 7U, 0U));
	file = fopen(guard, "wb");
	CHECK(file != NULL);
	if (file)
		CHECK(fclose(file) == 0);
	CHECK(Publish(root, 1, 8U, &verifier, NULL, &result));
	CHECK(stat(guard, &status) == 0 && S_ISREG(status.st_mode));
	CHECK(remove(guard) == 0);
	CleanupRoot(root);
}

static void TestCleanupRefusesSymlinkCandidate(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	char outside[] = "/tmp/sg-rune-v2-pub-outside-XXXXXX";
	char staging[512];
	char sentinel[512];
	verifier_t verifier;
	sg_rune_v2_artifact_publication_result_t result;
	FILE *file;
	struct stat status;

	CHECK(mkdtemp(root) != NULL);
	CHECK(mkdtemp(outside) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	CHECK(Format(staging, sizeof(staging), "%s/.staging-%016llx", root, 8U,
		0U));
	CHECK(snprintf(sentinel, sizeof(sentinel), "%s/artifact.rune", outside) >
		0);
	file = fopen(sentinel, "wb");
	CHECK(file != NULL);
	if (file)
	{
		CHECK(fwrite("guard", 1U, 5U, file) == 5U);
		CHECK(fclose(file) == 0);
	}
	CHECK(symlink(outside, staging) == 0);
	CHECK(!Publish(root, 1, 8U, &verifier, NULL, &result));
	CHECK(result.stage == SG_RUNE_V2_FS_STAGE_RECOVERY_CLEANUP);
	CHECK(result.os_error == EEXIST);
	CHECK(lstat(staging, &status) == 0 && S_ISLNK(status.st_mode));
	CHECK(stat(sentinel, &status) == 0 && S_ISREG(status.st_mode));
	CHECK(CheckComplete(root, NULL) == 7U);
	CHECK(unlink(staging) == 0);
	CHECK(remove(sentinel) == 0);
	CHECK(rmdir(outside) == 0);
	CleanupRoot(root);
}

static size_t CountFaultEvents(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	verifier_t verifier;
	fault_ops_t fault;
	sg_rune_v2_artifact_publication_ops_t ops;
	sg_rune_v2_artifact_publication_result_t result;
	size_t events;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	memset(&fault, 0, sizeof(fault));
	fault.write_limit = 3U;
	ops = FaultOps(&fault);
	CHECK(Publish(root, 1, 8U, &verifier, &ops, &result));
	events = fault.events;
	CHECK(events > 20U);
	CleanupRoot(root);
	return events;
}

static void TestOneFault(size_t fail_event, fault_mode_t mode)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	verifier_t verifier;
	fault_ops_t fault;
	sg_rune_v2_artifact_publication_ops_t ops;
	sg_rune_v2_artifact_publication_result_t result;
	uint64_t observed;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	verifier.calls = 0U;
	memset(&fault, 0, sizeof(fault));
	fault.fail_event = fail_event;
	fault.write_limit = 3U;
	fault.mode = mode;
	fault.suppress_cleanup_after_failure = 1;
	ops = FaultOps(&fault);
	CHECK(!Publish(root, 1, 8U, &verifier, &ops, &result));
	observed = CheckComplete(root, NULL);
	CHECK(observed == 7U || observed == 8U);
	verifier.calls = 0U;
	CHECK(Publish(root, 1, 8U, &verifier, NULL, &result));
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK ||
		result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_ALREADY_ACTIVE);
	CHECK(CheckComplete(root, NULL) == 8U);
	CleanupRoot(root);
}

static void TestExhaustiveFilesystemFaults(void)
{
	size_t event_count = CountFaultEvents();
	size_t event;
	filesystem_fault_points = event_count;

	for (event = 1U; event <= event_count; event++)
	{
		TestOneFault(event, FAULT_BEFORE);
		TestOneFault(event, FAULT_AFTER);
	}
}

static void TestVerificationFailures(void)
{
	size_t fail_call;

	for (fail_call = 1U; fail_call <= 3U; fail_call++)
	{
		char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
		verifier_t verifier;
		sg_rune_v2_artifact_publication_result_t result;

		CHECK(mkdtemp(root) != NULL);
		InitVerifier(&verifier);
		CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
		verifier.calls = 0U;
		verifier.fail_call = fail_call;
		CHECK(!Publish(root, 1, 8U, &verifier, NULL, &result));
		CHECK(result.stage == (fail_call == 1U
			? SG_RUNE_V2_FS_STAGE_ARTIFACT_VERIFY
			: SG_RUNE_V2_FS_STAGE_SIDECAR_VERIFY));
		CHECK(CheckComplete(root, NULL) == 7U);
		verifier.calls = 0U;
		verifier.fail_call = 0U;
		CHECK(Publish(root, 1, 8U, &verifier, NULL, &result));
		CHECK(CheckComplete(root, NULL) == 8U);
		CleanupRoot(root);
	}
}

static void TestSecondaryCloseFailureIsSurfaced(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	verifier_t verifier;
	fault_ops_t fault;
	sg_rune_v2_artifact_publication_ops_t ops;
	sg_rune_v2_artifact_publication_result_t result;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	memset(&fault, 0, sizeof(fault));
	/* Active close and staging-parent sync precede the artifact write. */
	fault.fail_event = 3U;
	fault.mode = FAULT_BEFORE;
	fault.fail_close_after_failure = 1;
	ops = FaultOps(&fault);
	CHECK(!Publish(root, 1, 8U, &verifier, &ops, &result));
	CHECK(result.stage == SG_RUNE_V2_FS_STAGE_ARTIFACT_WRITE);
	CHECK(result.os_error == EIO);
	CHECK(result.close_error == EIO);
	CHECK(CheckComplete(root, NULL) == 7U);
	CleanupRoot(root);
}

static void TestEqualRetryRevalidatesActiveFiles(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	char path[512];
	verifier_t verifier;
	sg_rune_v2_artifact_publication_result_t result;
	FILE *file;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	CHECK(Format(path, sizeof(path),
		"%s/generation-%016llx/artifact.rune", root, 7U, 0U));
	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (file)
	{
		CHECK(fwrite(NEW_ARTIFACT, 1U, sizeof(NEW_ARTIFACT), file) ==
			sizeof(NEW_ARTIFACT));
		CHECK(fclose(file) == 0);
	}
	verifier.calls = 0U;
	CHECK(!Publish(root, 0, 7U, &verifier, NULL, &result));
	CHECK(result.diagnostic ==
		SG_RUNE_V2_FS_PUBLICATION_STAGED_IDENTITY_REJECTED);
	CHECK(result.stage == SG_RUNE_V2_FS_STAGE_ARTIFACT_VERIFY);
	CHECK(result.commit_visible);
	CleanupRoot(root);
}

static void TestCorruptActiveFailsClosed(void)
{
	char root[] = "/tmp/sg-rune-v2-pub-XXXXXX";
	char current[512];
	verifier_t verifier;
	sg_rune_v2_artifact_publication_result_t result;
	FILE *file;

	CHECK(mkdtemp(root) != NULL);
	InitVerifier(&verifier);
	CHECK(Publish(root, 0, 7U, &verifier, NULL, &result));
	CHECK(snprintf(current, sizeof(current), "%s/CURRENT", root) > 0);
	file = fopen(current, "r+b");
	CHECK(file != NULL);
	if (file)
	{
		int byte;

		CHECK(fseek(file, 40L, SEEK_SET) == 0);
		byte = fgetc(file);
		CHECK(byte != EOF);
		CHECK(fseek(file, 40L, SEEK_SET) == 0);
		CHECK(fputc(byte ^ 1, file) != EOF);
		CHECK(fclose(file) == 0);
	}
	CHECK(!Publish(root, 1, 8U, &verifier, NULL, &result));
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_ACTIVE_CORRUPT);
	CHECK(result.stage == SG_RUNE_V2_FS_STAGE_ACTIVE_MANIFEST);
	CleanupRoot(root);
}

int main(void)
{
	TestArgumentsAndIdentityBoundary();
	TestSuccessShortWritesStaleAndConflict();
	TestEmptySidecarSet();
	TestCleanupPreservesActiveGeneration();
	TestCleanupRefusesSymlinkCandidate();
	TestExhaustiveFilesystemFaults();
	TestVerificationFailures();
	TestSecondaryCloseFailureIsSurfaced();
	TestEqualRetryRevalidatesActiveFiles();
	TestCorruptActiveFailsClosed();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_v2_artifact_publication_test: %d failure(s)\n",
			failures);
		return 1;
	}
	printf("sg_rune_v2_artifact_publication_test: ok "
		"(filesystem_fault_scenarios=%zu verification_faults=3)\n",
		filesystem_fault_points * 2U);
	return 0;
}
