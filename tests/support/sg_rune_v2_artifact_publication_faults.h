#ifndef SG_RUNE_V2_ARTIFACT_PUBLICATION_FAULTS_H
#define SG_RUNE_V2_ARTIFACT_PUBLICATION_FAULTS_H

typedef enum fault_mode_e
{
	FAULT_NONE = 0,
	FAULT_BEFORE,
	FAULT_AFTER
} fault_mode_t;

typedef struct fault_ops_s
{
	sg_rune_v2_artifact_publication_ops_t base;
	size_t events;
	size_t fail_event;
	size_t write_limit;
	fault_mode_t mode;
	int suppress_cleanup_after_failure;
	int fail_close_after_failure;
} fault_ops_t;

static const unsigned char OLD_ARTIFACT[] = "old-rune";
static const unsigned char OLD_SIDECAR_1[] = "old-sidecar-one";
static const unsigned char OLD_SIDECAR_3[] = "old-sidecar-three";
static const unsigned char NEW_ARTIFACT[] = "new-rune-content";
static const unsigned char NEW_SIDECAR_1[] = "new-one";
static const unsigned char NEW_SIDECAR_3[] = "new-three-content";

static sg_rune_v2_content_id_t Identity(unsigned int seed)
{
	sg_rune_v2_content_id_t identity = { { 0 } };
	unsigned int index;

	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		identity.bytes[index] = (uint8_t)(seed + index);
	return identity;
}

static int IdentityEqual(const sg_rune_v2_content_id_t *left,
	const sg_rune_v2_content_id_t *right)
{
	return SG_RuneV2ContentIdEqual(left, right);
}

static int VerifyStaged(void *context, const sg_rune_v2_staged_file_t *staged,
	const sg_rune_v2_content_id_t *expected_identity, int *os_error_out)
{
	verifier_t *verifier = context;
	unsigned char observed[5];
	size_t index;
	size_t offset = 0U;

	verifier->calls++;
	if (verifier->calls == verifier->fail_call)
	{
		*os_error_out = ESTALE;
		return 0;
	}
	for (index = 0U; index < verifier->file_count; index++)
	{
		const exact_file_t *file = &verifier->files[index];

		if (!IdentityEqual(&file->identity, expected_identity))
			continue;
		if (file->size != staged->size)
		{
			*os_error_out = EINVAL;
			return 0;
		}
		while (offset < file->size)
		{
			size_t request = file->size - offset;
			size_t count;

			if (request > sizeof(observed))
				request = sizeof(observed);
			*os_error_out = 0;
			count = staged->read_at(staged->ops_context, staged->file,
				offset, observed, request, os_error_out);
			if (count != request || *os_error_out != 0 ||
				memcmp(observed, file->bytes + offset, count) != 0)
			{
				if (*os_error_out == 0)
					*os_error_out = EILSEQ;
				return 0;
			}
			offset += count;
		}
		*os_error_out = 0;
		if (staged->read_at(staged->ops_context, staged->file, offset,
			observed, 1U, os_error_out) != 0U || *os_error_out != 0)
		{
			if (*os_error_out == 0)
				*os_error_out = EFBIG;
			return 0;
		}
		return 1;
	}
	*os_error_out = ENOENT;
	return 0;
}

static void InitVerifier(verifier_t *verifier)
{
	memset(verifier, 0, sizeof(*verifier));
	verifier->files[0] = (exact_file_t){ Identity(10U), OLD_ARTIFACT,
		sizeof(OLD_ARTIFACT) };
	verifier->files[1] = (exact_file_t){ Identity(20U), OLD_SIDECAR_1,
		sizeof(OLD_SIDECAR_1) };
	verifier->files[2] = (exact_file_t){ Identity(30U), OLD_SIDECAR_3,
		sizeof(OLD_SIDECAR_3) };
	verifier->files[3] = (exact_file_t){ Identity(40U), NEW_ARTIFACT,
		sizeof(NEW_ARTIFACT) };
	verifier->files[4] = (exact_file_t){ Identity(50U), NEW_SIDECAR_1,
		sizeof(NEW_SIDECAR_1) };
	verifier->files[5] = (exact_file_t){ Identity(60U), NEW_SIDECAR_3,
		sizeof(NEW_SIDECAR_3) };
	verifier->file_count = 6U;
}

static sg_rune_v2_accepted_artifact_t Accepted(uint64_t generation,
	unsigned int artifact_seed, unsigned int set_seed)
{
	sg_rune_v2_accepted_artifact_t accepted = { 0 };

	accepted.generation = generation;
	accepted.reader_mask = SG_RUNE_V2_REQUIRED_READER_MASK;
	accepted.sidecar_mask = UINT32_C(1) | UINT32_C(4);
	accepted.sidecar_count = 2U;
	accepted.bsp_identity = Identity(70U);
	accepted.schema_identity = Identity(80U);
	accepted.artifact_identity = Identity(artifact_seed);
	accepted.proof_identity = Identity(90U);
	accepted.sidecar_set_identity = Identity(set_seed);
	accepted.sidecars[0].kind = 1U;
	accepted.sidecars[0].exact_identity = Identity(artifact_seed == 40U
		? 50U : 20U);
	accepted.sidecars[1].kind = 3U;
	accepted.sidecars[1].exact_identity = Identity(artifact_seed == 40U
		? 60U : 30U);
	return accepted;
}

static sg_rune_v2_publication_candidate_t Candidate(uint64_t generation,
	int newer, verifier_t *verifier,
	sg_rune_v2_accepted_artifact_t *accepted_out,
	sg_rune_v2_publication_sidecar_file_t sidecars_out[2])
{
	sg_rune_v2_publication_candidate_t candidate;

	*accepted_out = Accepted(generation, newer ? 40U : 10U,
		newer ? 100U : 110U);
	sidecars_out[0].kind = 3U;
	sidecars_out[0].bytes = newer ? NEW_SIDECAR_3 : OLD_SIDECAR_3;
	sidecars_out[0].size = newer ? sizeof(NEW_SIDECAR_3) :
		sizeof(OLD_SIDECAR_3);
	sidecars_out[0].exact_identity = Identity(newer ? 60U : 30U);
	sidecars_out[1].kind = 1U;
	sidecars_out[1].bytes = newer ? NEW_SIDECAR_1 : OLD_SIDECAR_1;
	sidecars_out[1].size = newer ? sizeof(NEW_SIDECAR_1) :
		sizeof(OLD_SIDECAR_1);
	sidecars_out[1].exact_identity = Identity(newer ? 50U : 20U);
	candidate.accepted = accepted_out;
	candidate.artifact_bytes = newer ? NEW_ARTIFACT : OLD_ARTIFACT;
	candidate.artifact_size = newer ? sizeof(NEW_ARTIFACT) :
		sizeof(OLD_ARTIFACT);
	candidate.sidecars = sidecars_out;
	candidate.sidecar_count = 2U;
	candidate.verify_staged_file = VerifyStaged;
	candidate.verify_context = verifier;
	return candidate;
}

static int Format(char *output, size_t output_size, const char *format,
	const char *root, uint64_t generation, unsigned int kind)
{
	int written = snprintf(output, output_size, format, root,
		(unsigned long long)generation, kind);

	return written >= 0 && (size_t)written < output_size;
}

static int ReadBytes(const char *path, unsigned char *output,
	size_t output_size, size_t *size_out)
{
	FILE *file = fopen(path, "rb");
	struct stat status;
	size_t size;

	if (!file)
		return 0;
	if (fstat(fileno(file), &status) != 0 || status.st_size < 0 ||
		(uintmax_t)status.st_size > (uintmax_t)output_size)
	{
		(void)fclose(file);
		return 0;
	}
	size = (size_t)status.st_size;
	if (fread(output, 1U, size, file) != size || ferror(file))
	{
		(void)fclose(file);
		return 0;
	}
	if (fclose(file) != 0)
		return 0;
	*size_out = size;
	return 1;
}

static void CheckFile(const char *root, uint64_t generation,
	unsigned int kind, const unsigned char *expected, size_t expected_size)
{
	char path[512];
	unsigned char observed[128];
	size_t observed_size = 0U;

	if (kind == 0U)
		CHECK(Format(path, sizeof(path), "%s/generation-%016llx/artifact.rune",
			root, generation, kind));
	else
		CHECK(Format(path, sizeof(path),
			"%s/generation-%016llx/sidecar-%02u", root, generation, kind));
	CHECK(ReadBytes(path, observed, sizeof(observed), &observed_size));
	CHECK(observed_size == expected_size);
	CHECK(memcmp(observed, expected, expected_size) == 0);
}

static uint64_t CheckComplete(const char *root,
	const sg_rune_v2_artifact_publication_ops_t *ops)
{
	sg_rune_v2_active_generation_t active;
	sg_rune_v2_artifact_publication_result_t result;
	sg_rune_v2_content_id_t old_identity = Identity(10U);
	uint64_t generation;

	memset(&active, 0, sizeof(active));
	result = SG_RuneV2ArtifactPublicationReadActive(root, &active, ops);
	CHECK(result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK);
	CHECK(result.stage == SG_RUNE_V2_FS_STAGE_DONE);
	generation = active.accepted.generation;
	CHECK(active.accepted.sidecar_count == 2U);
	CHECK(active.accepted.sidecars[0].kind == 1U &&
		active.accepted.sidecars[1].kind == 3U);
	if (generation == 7U)
	{
		CHECK(IdentityEqual(&active.accepted.artifact_identity,
			&old_identity));
		CheckFile(root, generation, 0U, OLD_ARTIFACT, sizeof(OLD_ARTIFACT));
		CheckFile(root, generation, 1U, OLD_SIDECAR_1,
			sizeof(OLD_SIDECAR_1));
		CheckFile(root, generation, 3U, OLD_SIDECAR_3,
			sizeof(OLD_SIDECAR_3));
	}
	else
	{
		CHECK(generation == 8U);
		CheckFile(root, generation, 0U, NEW_ARTIFACT, sizeof(NEW_ARTIFACT));
		CheckFile(root, generation, 1U, NEW_SIDECAR_1,
			sizeof(NEW_SIDECAR_1));
		CheckFile(root, generation, 3U, NEW_SIDECAR_3,
			sizeof(NEW_SIDECAR_3));
	}
	return generation;
}

static int EventFails(fault_ops_t *fault)
{
	fault->events++;
	return fault->events == fault->fail_event;
}

static void *FaultOpenRead(void *context, const char *path,
	int *not_found_out, int *os_error_out)
{
	fault_ops_t *fault = context;
	return fault->base.open_read(fault->base.context, path, not_found_out,
		os_error_out);
}

static void *FaultOpenExclusive(void *context, const char *path,
	int *os_error_out)
{
	fault_ops_t *fault = context;
	return fault->base.open_exclusive(fault->base.context, path, os_error_out);
}

static size_t FaultRead(void *context, void *file, unsigned char *output,
	size_t output_size, int *os_error_out)
{
	fault_ops_t *fault = context;
	return fault->base.read(fault->base.context, file, output, output_size,
		os_error_out);
}

static size_t FaultReadAt(void *context, void *file, size_t offset,
	unsigned char *output, size_t output_size, int *os_error_out)
{
	fault_ops_t *fault = context;
	return fault->base.read_at(fault->base.context, file, offset, output,
		output_size, os_error_out);
}

static size_t FaultWrite(void *context, void *file,
	const unsigned char *bytes, size_t size, int *os_error_out)
{
	fault_ops_t *fault = context;
	size_t written;
	int fail;

	if (fault->write_limit != 0U && size > fault->write_limit)
		size = fault->write_limit;
	fail = EventFails(fault);
	if (fail && fault->mode == FAULT_BEFORE)
	{
		*os_error_out = EIO;
		return 0U;
	}
	written = fault->base.write(fault->base.context, file, bytes, size,
		os_error_out);
	if (fail && fault->mode == FAULT_AFTER)
		*os_error_out = EIO;
	return written;
}

static int FaultScalar(fault_ops_t *fault,
	int (*operation)(void *, void *, int *), void *argument,
	int *os_error_out)
{
	int fail = EventFails(fault);
	int status;

	if (fail && fault->mode == FAULT_BEFORE)
	{
		*os_error_out = EIO;
		return 0;
	}
	status = operation(fault->base.context, argument, os_error_out);
	if (fail && fault->mode == FAULT_AFTER)
	{
		*os_error_out = EIO;
		return 0;
	}
	return status;
}

static int FaultSyncFile(void *context, void *file, int *os_error_out)
{
	fault_ops_t *fault = context;
	return FaultScalar(fault, fault->base.sync_file, file, os_error_out);
}

static int FaultClose(void *context, void *file, int *os_error_out)
{
	fault_ops_t *fault = context;
	int fail = EventFails(fault);
	int status = fault->base.close_file(fault->base.context, file, os_error_out);

	if (fail || (fault->fail_close_after_failure && fault->fail_event != 0U &&
		fault->events > fault->fail_event))
	{
		*os_error_out = EIO;
		return 0;
	}
	return status;
}

static int FaultMakeDirectory(void *context, const char *path,
	int *os_error_out)
{
	fault_ops_t *fault = context;
	return fault->base.make_directory(fault->base.context, path, os_error_out);
}

static int FaultRenameGeneration(void *context, const char *from,
	const char *to, int *os_error_out)
{
	fault_ops_t *fault = context;
	int fail = EventFails(fault);
	int status;

	if (fail && fault->mode == FAULT_BEFORE)
	{
		*os_error_out = EIO;
		return 0;
	}
	status = fault->base.rename_generation(fault->base.context, from, to,
		os_error_out);
	if (fail && fault->mode == FAULT_AFTER)
	{
		*os_error_out = EIO;
		return 0;
	}
	return status;
}

static int FaultReplace(void *context, const char *from, const char *to,
	int *os_error_out)
{
	fault_ops_t *fault = context;
	int fail = EventFails(fault);
	int status;

	if (fail && fault->mode == FAULT_BEFORE)
	{
		*os_error_out = EIO;
		return 0;
	}
	status = fault->base.replace_file(fault->base.context, from, to,
		os_error_out);
	if (fail && fault->mode == FAULT_AFTER)
	{
		*os_error_out = EIO;
		return 0;
	}
	return status;
}

static int FaultSyncDirectory(void *context, const char *path,
	int *os_error_out)
{
	fault_ops_t *fault = context;
	int fail = EventFails(fault);
	int status;

	if (fail && fault->mode == FAULT_BEFORE)
	{
		*os_error_out = EIO;
		return 0;
	}
	status = fault->base.sync_directory(fault->base.context, path,
		os_error_out);
	if (fail && fault->mode == FAULT_AFTER)
	{
		*os_error_out = EIO;
		return 0;
	}
	return status;
}

static int FaultRemoveFile(void *context, const char *path, int *os_error_out)
{
	fault_ops_t *fault = context;

	if (fault->suppress_cleanup_after_failure && fault->fail_event != 0U &&
		fault->events >= fault->fail_event)
	{
		*os_error_out = EIO;
		return 0;
	}
	return fault->base.remove_file(fault->base.context, path, os_error_out);
}

static int FaultInspectDirectory(void *context, const char *path,
	int *exists_out, int *is_owned_directory_out, int *os_error_out)
{
	fault_ops_t *fault = context;
	return fault->base.inspect_directory(fault->base.context, path, exists_out,
		is_owned_directory_out, os_error_out);
}

static int FaultRemoveDirectory(void *context, const char *path,
	int *os_error_out)
{
	fault_ops_t *fault = context;

	if (fault->suppress_cleanup_after_failure && fault->fail_event != 0U &&
		fault->events >= fault->fail_event)
	{
		*os_error_out = EIO;
		return 0;
	}
	return fault->base.remove_directory(fault->base.context, path,
		os_error_out);
}

static sg_rune_v2_artifact_publication_ops_t FaultOps(fault_ops_t *fault)
{
	sg_rune_v2_artifact_publication_ops_t ops;

	SG_RuneV2ArtifactPublicationDefaultOps(&fault->base);
	memset(&ops, 0, sizeof(ops));
	ops.context = fault;
	ops.open_read = FaultOpenRead;
	ops.open_exclusive = FaultOpenExclusive;
	ops.read = FaultRead;
	ops.read_at = FaultReadAt;
	ops.write = FaultWrite;
	ops.sync_file = FaultSyncFile;
	ops.close_file = FaultClose;
	ops.make_directory = FaultMakeDirectory;
	ops.rename_generation = FaultRenameGeneration;
	ops.replace_file = FaultReplace;
	ops.sync_directory = FaultSyncDirectory;
	ops.inspect_directory = FaultInspectDirectory;
	ops.remove_file = FaultRemoveFile;
	ops.remove_directory = FaultRemoveDirectory;
	return ops;
}

#endif /* SG_RUNE_V2_ARTIFACT_PUBLICATION_FAULTS_H */
