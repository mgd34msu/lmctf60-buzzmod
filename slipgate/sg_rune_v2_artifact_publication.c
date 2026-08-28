#include "sg_rune_v2_artifact_publication_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int PubError(int reported)
{
	return reported != 0 ? reported : EIO;
}

static int OpsValid(const sg_rune_v2_artifact_publication_ops_t *ops)
{
	return ops && ops->open_read && ops->open_exclusive && ops->read &&
		ops->read_at && ops->write && ops->sync_file && ops->close_file &&
		ops->make_directory && ops->rename_generation && ops->replace_file &&
		ops->sync_directory && ops->inspect_directory && ops->remove_file &&
		ops->remove_directory;
}

static sg_rune_v2_artifact_publication_result_t Result(
	sg_rune_v2_artifact_publication_diagnostic_t diagnostic,
	sg_rune_v2_artifact_publication_stage_t stage)
{
	sg_rune_v2_artifact_publication_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.stage = stage;
	return result;
}

int SG_RuneV2ArtifactPublicationSucceeded(
	const sg_rune_v2_artifact_publication_result_t *result)
{
	return result && (result->diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK ||
		result->diagnostic == SG_RUNE_V2_FS_PUBLICATION_ALREADY_ACTIVE);
}

static char *PathJoin(const char *directory, const char *leaf)
{
	size_t directory_size = strlen(directory);
	size_t leaf_size = strlen(leaf);
	int separator = directory_size != 0U && directory[directory_size - 1U] != '/';
	char *path;

	if (directory_size > SIZE_MAX - leaf_size - (size_t)separator - 1U)
		return NULL;
	path = malloc(directory_size + (size_t)separator + leaf_size + 1U);
	if (!path)
		return NULL;
	memcpy(path, directory, directory_size);
	if (separator)
		path[directory_size++] = '/';
	memcpy(path + directory_size, leaf, leaf_size + 1U);
	return path;
}

static int FormatGenerationLeaf(char output[64], const char *prefix,
	uint64_t generation)
{
	int written = snprintf(output, 64U, "%s-%016llx", prefix,
		(unsigned long long)generation);

	return written >= 0 && written < 64;
}

static const sg_rune_v2_publication_sidecar_file_t *SidecarByKind(
	const sg_rune_v2_publication_candidate_t *candidate, uint32_t kind)
{
	uint32_t index;

	for (index = 0U; index < candidate->sidecar_count; index++)
		if (candidate->sidecars[index].kind == kind)
			return &candidate->sidecars[index];
	return NULL;
}

static sg_rune_v2_artifact_publication_result_t ReadActiveInternal(
	const char *directory, sg_rune_v2_active_generation_t *active,
	unsigned char manifest[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES], size_t *manifest_size,
	const sg_rune_v2_artifact_publication_ops_t *ops)
{
	sg_rune_v2_artifact_publication_result_t result;
	char *path = PathJoin(directory, "CURRENT");
	void *file;
	size_t total = 0U;
	int error = 0;
	int not_found = 0;

	if (!path)
		return Result(SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED,
			SG_RUNE_V2_FS_STAGE_ACTIVE_OPEN);
	file = ops->open_read(ops->context, path, &not_found, &error);
	free(path);
	if (!file)
	{
		result = Result(not_found ? SG_RUNE_V2_FS_PUBLICATION_NO_ACTIVE :
			SG_RUNE_V2_FS_PUBLICATION_IO_ERROR,
			SG_RUNE_V2_FS_STAGE_ACTIVE_OPEN);
		result.os_error = not_found ? 0 : PubError(error);
		return result;
	}
	while (total < SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES)
	{
		size_t count = ops->read(ops->context, file, manifest + total,
			SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES - total, &error);
		if (count > SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES - total)
		{
			error = EIO;
			break;
		}
		total += count;
		if (count == 0U || error != 0)
			break;
	}
	if (total == SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES && error == 0)
	{
		unsigned char extra;
		if (ops->read(ops->context, file, &extra, 1U, &error) != 0U)
			error = EFBIG;
	}
	if (error != 0)
	{
		result = Result(SG_RUNE_V2_FS_PUBLICATION_IO_ERROR,
			SG_RUNE_V2_FS_STAGE_ACTIVE_READ);
		result.os_error = PubError(error);
	}
	else
		result = Result(SG_RUNE_V2_FS_PUBLICATION_OK,
			SG_RUNE_V2_FS_STAGE_ACTIVE_CLOSE);
	error = 0;
	if (!ops->close_file(ops->context, file, &error))
	{
		if (result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK)
		{
			result.diagnostic = SG_RUNE_V2_FS_PUBLICATION_IO_ERROR;
			result.stage = SG_RUNE_V2_FS_STAGE_ACTIVE_CLOSE;
			result.os_error = PubError(error);
		}
		else
			result.close_error = PubError(error);
	}
	if (result.diagnostic != SG_RUNE_V2_FS_PUBLICATION_OK)
		return result;
	if (!SG_RuneV2PublicationManifestDecode(manifest, total, active))
		return Result(SG_RUNE_V2_FS_PUBLICATION_ACTIVE_CORRUPT,
			SG_RUNE_V2_FS_STAGE_ACTIVE_MANIFEST);
	*manifest_size = total;
	result.stage = SG_RUNE_V2_FS_STAGE_DONE;
	result.observed_generation = active->accepted.generation;
	result.commit_visible = 1;
	return result;
}

sg_rune_v2_artifact_publication_result_t
SG_RuneV2ArtifactPublicationReadActive(const char *publication_directory,
	sg_rune_v2_active_generation_t *active_out,
	const sg_rune_v2_artifact_publication_ops_t *provided_ops)
{
	sg_rune_v2_artifact_publication_ops_t default_ops;
	unsigned char manifest[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES];
	size_t manifest_size = 0U;

	if (active_out)
		memset(active_out, 0, sizeof(*active_out));
	if (!publication_directory || publication_directory[0] == '\0' ||
		!active_out)
		return Result(SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT,
			SG_RUNE_V2_FS_STAGE_ARGUMENT);
	if (!provided_ops)
	{
		SG_RuneV2ArtifactPublicationDefaultOps(&default_ops);
		provided_ops = &default_ops;
	}
	if (!OpsValid(provided_ops))
		return Result(SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT,
			SG_RUNE_V2_FS_STAGE_ARGUMENT);
	return ReadActiveInternal(publication_directory, active_out, manifest,
		&manifest_size, provided_ops);
}

static int RemoveDirectoryContents(
	const sg_rune_v2_artifact_publication_ops_t *ops, const char *directory,
	int *error_out)
{
	char leaf[32];
	char *path;
	uint32_t kind;
	int exists = 0;
	int is_owned_directory = 0;

	if (!ops->inspect_directory(ops->context, directory, &exists,
		&is_owned_directory, error_out))
		return 0;
	if (!exists)
		return 1;
	if (!is_owned_directory)
	{
		*error_out = EEXIST;
		return 0;
	}

	path = PathJoin(directory, "artifact.rune");
	if (!path)
	{
		*error_out = ENOMEM;
		return 0;
	}
	if (!ops->remove_file(ops->context, path, error_out))
	{
		free(path);
		return 0;
	}
	free(path);
	for (kind = 1U; kind <= SG_RUNE_V2_MAX_SIDECARS; kind++)
	{
		(void)snprintf(leaf, sizeof(leaf), "sidecar-%02u", kind);
		path = PathJoin(directory, leaf);
		if (!path)
		{
			*error_out = ENOMEM;
			return 0;
		}
		if (!ops->remove_file(ops->context, path, error_out))
		{
			free(path);
			return 0;
		}
		free(path);
	}
	return ops->remove_directory(ops->context, directory, error_out);
}

static int CleanupRemnants(const char *root, uint64_t active_generation,
	uint64_t target_generation,
	const sg_rune_v2_artifact_publication_ops_t *ops, int *error_out)
{
	char leaf[64];
	char *path;
	int okay;

	if (active_generation == target_generation)
	{
		*error_out = EBUSY;
		return 0;
	}
	if (!FormatGenerationLeaf(leaf, ".CURRENT", target_generation))
	{
		*error_out = EOVERFLOW;
		return 0;
	}
	(void)strcat(leaf, ".tmp");
	path = PathJoin(root, leaf);
	if (!path)
	{
		*error_out = ENOMEM;
		return 0;
	}
	okay = ops->remove_file(ops->context, path, error_out);
	free(path);
	if (!okay)
		return 0;
	if (!FormatGenerationLeaf(leaf, ".staging", target_generation))
	{
		*error_out = EOVERFLOW;
		return 0;
	}
	path = PathJoin(root, leaf);
	if (!path)
	{
		*error_out = ENOMEM;
		return 0;
	}
	okay = RemoveDirectoryContents(ops, path, error_out);
	free(path);
	if (!okay)
		return 0;
	if (!FormatGenerationLeaf(leaf, "generation", target_generation))
	{
		*error_out = EOVERFLOW;
		return 0;
	}
	path = PathJoin(root, leaf);
	if (!path)
	{
		*error_out = ENOMEM;
		return 0;
	}
	okay = RemoveDirectoryContents(ops, path, error_out);
	free(path);
	return okay;
}

static void CleanupAfterFailure(const char *root, uint64_t generation,
	const sg_rune_v2_artifact_publication_ops_t *ops,
	sg_rune_v2_artifact_publication_result_t *result)
{
	char leaf[64];
	char *path;
	int error = 0;

	if (FormatGenerationLeaf(leaf, ".CURRENT", generation))
	{
		(void)strcat(leaf, ".tmp");
		path = PathJoin(root, leaf);
		if (path)
		{
			if (!ops->remove_file(ops->context, path, &error))
				result->cleanup_error = PubError(error);
			free(path);
		}
	}
	if (FormatGenerationLeaf(leaf, ".staging", generation))
	{
		path = PathJoin(root, leaf);
		if (path)
		{
			if (!RemoveDirectoryContents(ops, path, &error) &&
				result->cleanup_error == 0)
				result->cleanup_error = PubError(error);
			free(path);
		}
	}
}

static int WriteCandidateFile(const char *path, const unsigned char *bytes,
	size_t size, const sg_rune_v2_content_id_t *identity, uint32_t sidecar_kind,
	const sg_rune_v2_publication_candidate_t *candidate,
	const sg_rune_v2_artifact_publication_ops_t *ops,
	sg_rune_v2_artifact_publication_result_t *result)
{
	sg_rune_v2_artifact_publication_stage_t open_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN : SG_RUNE_V2_FS_STAGE_SIDECAR_OPEN;
	sg_rune_v2_artifact_publication_stage_t write_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_WRITE : SG_RUNE_V2_FS_STAGE_SIDECAR_WRITE;
	sg_rune_v2_artifact_publication_stage_t sync_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_SYNC : SG_RUNE_V2_FS_STAGE_SIDECAR_SYNC;
	sg_rune_v2_artifact_publication_stage_t verify_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_VERIFY :
		SG_RUNE_V2_FS_STAGE_SIDECAR_VERIFY;
	sg_rune_v2_artifact_publication_stage_t close_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_CLOSE : SG_RUNE_V2_FS_STAGE_SIDECAR_CLOSE;
	sg_rune_v2_staged_file_t staged;
	void *file;
	size_t offset = 0U;
	int error = 0;
	int failed = 0;

	result->sidecar_kind = sidecar_kind;
	file = ops->open_exclusive(ops->context, path, &error);
	if (!file)
	{
		result->stage = open_stage;
		result->os_error = PubError(error);
		return 0;
	}
	while (offset < size)
	{
		size_t count = ops->write(ops->context, file, bytes + offset,
			size - offset, &error);
		if (count > size - offset || error != 0 || count == 0U)
		{
			result->stage = write_stage;
			result->os_error = count > size - offset ? EIO : PubError(error);
			failed = 1;
			break;
		}
		offset += count;
		result->bytes_transferred += count;
	}
	if (!failed && !ops->sync_file(ops->context, file, &error))
	{
		result->stage = sync_stage;
		result->os_error = PubError(error);
		failed = 1;
	}
	if (!failed)
	{
		staged.ops_context = ops->context;
		staged.file = file;
		staged.sidecar_kind = sidecar_kind;
		staged.size = size;
		staged.read_at = ops->read_at;
		error = 0;
		if (!candidate->verify_staged_file(candidate->verify_context,
			&staged, identity, &error))
		{
			result->diagnostic =
				SG_RUNE_V2_FS_PUBLICATION_STAGED_IDENTITY_REJECTED;
			result->stage = verify_stage;
			result->os_error = error;
			failed = 1;
		}
	}
	error = 0;
	if (!ops->close_file(ops->context, file, &error))
	{
		if (!failed)
		{
			result->stage = close_stage;
			result->os_error = PubError(error);
			failed = 1;
		}
		else
			result->close_error = PubError(error);
	}
	return !failed;
}

static int VerifyActiveFile(const char *path, size_t size,
	const sg_rune_v2_content_id_t *identity, uint32_t sidecar_kind,
	const sg_rune_v2_publication_candidate_t *candidate,
	const sg_rune_v2_artifact_publication_ops_t *ops,
	sg_rune_v2_artifact_publication_result_t *result)
{
	sg_rune_v2_staged_file_t staged;
	sg_rune_v2_artifact_publication_stage_t open_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN : SG_RUNE_V2_FS_STAGE_SIDECAR_OPEN;
	sg_rune_v2_artifact_publication_stage_t verify_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_VERIFY :
		SG_RUNE_V2_FS_STAGE_SIDECAR_VERIFY;
	sg_rune_v2_artifact_publication_stage_t close_stage = sidecar_kind == 0U
		? SG_RUNE_V2_FS_STAGE_ARTIFACT_CLOSE : SG_RUNE_V2_FS_STAGE_SIDECAR_CLOSE;
	void *file;
	int not_found = 0;
	int error = 0;
	int verified;

	result->sidecar_kind = sidecar_kind;
	file = ops->open_read(ops->context, path, &not_found, &error);
	if (!file)
	{
		result->diagnostic = SG_RUNE_V2_FS_PUBLICATION_IO_ERROR;
		result->stage = open_stage;
		result->os_error = not_found ? ENOENT : PubError(error);
		return 0;
	}
	staged.ops_context = ops->context;
	staged.file = file;
	staged.sidecar_kind = sidecar_kind;
	staged.size = size;
	staged.read_at = ops->read_at;
	verified = candidate->verify_staged_file(candidate->verify_context,
		&staged, identity, &error);
	if (!verified)
	{
		result->diagnostic =
			SG_RUNE_V2_FS_PUBLICATION_STAGED_IDENTITY_REJECTED;
		result->stage = verify_stage;
		result->os_error = error;
	}
	error = 0;
	if (!ops->close_file(ops->context, file, &error))
	{
		if (verified)
		{
			result->diagnostic = SG_RUNE_V2_FS_PUBLICATION_IO_ERROR;
			result->stage = close_stage;
			result->os_error = PubError(error);
			verified = 0;
		}
		else
			result->close_error = PubError(error);
	}
	return verified;
}

static int VerifyActiveGeneration(const char *root,
	const sg_rune_v2_publication_candidate_t *candidate,
	const sg_rune_v2_artifact_publication_ops_t *ops,
	sg_rune_v2_artifact_publication_result_t *result)
{
	char generation_leaf[64];
	char sidecar_leaf[32];
	char *generation_path;
	char *file_path = NULL;
	uint32_t kind;
	int verified = 0;

	if (!FormatGenerationLeaf(generation_leaf, "generation",
		candidate->accepted->generation))
		return 0;
	generation_path = PathJoin(root, generation_leaf);
	if (!generation_path)
	{
		result->diagnostic = SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED;
		result->stage = SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN;
		return 0;
	}
	file_path = PathJoin(generation_path, "artifact.rune");
	if (!file_path)
	{
		result->diagnostic = SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED;
		result->stage = SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN;
		goto done;
	}
	if (!VerifyActiveFile(file_path, candidate->artifact_size,
		&candidate->accepted->artifact_identity, 0U, candidate, ops, result))
		goto done;
	free(file_path);
	file_path = NULL;
	for (kind = 1U; kind <= SG_RUNE_V2_MAX_SIDECARS; kind++)
	{
		const sg_rune_v2_publication_sidecar_file_t *sidecar =
			SidecarByKind(candidate, kind);

		if (!sidecar)
			continue;
		(void)snprintf(sidecar_leaf, sizeof(sidecar_leaf), "sidecar-%02u", kind);
		file_path = PathJoin(generation_path, sidecar_leaf);
		if (!file_path)
		{
			result->diagnostic = SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED;
			result->stage = SG_RUNE_V2_FS_STAGE_SIDECAR_OPEN;
			goto done;
		}
		if (!VerifyActiveFile(file_path, sidecar->size,
			&sidecar->exact_identity, kind, candidate, ops, result))
			goto done;
		free(file_path);
		file_path = NULL;
	}
	verified = 1;
done:
	free(file_path);
	free(generation_path);
	return verified;
}

static int WriteManifestFile(const char *path, const unsigned char *manifest,
	size_t manifest_size, const sg_rune_v2_artifact_publication_ops_t *ops,
	sg_rune_v2_artifact_publication_result_t *result)
{
	void *file;
	size_t offset = 0U;
	int error = 0;
	int failed = 0;

	file = ops->open_exclusive(ops->context, path, &error);
	if (!file)
	{
		result->stage = SG_RUNE_V2_FS_STAGE_POINTER_OPEN;
		result->os_error = PubError(error);
		return 0;
	}
	while (offset < manifest_size)
	{
		size_t count = ops->write(ops->context, file, manifest + offset,
			manifest_size - offset, &error);
		if (count > manifest_size - offset || error != 0 || count == 0U)
		{
			result->stage = SG_RUNE_V2_FS_STAGE_POINTER_WRITE;
			result->os_error = count > manifest_size - offset
				? EIO : PubError(error);
			failed = 1;
			break;
		}
		offset += count;
		result->bytes_transferred += count;
	}
	if (!failed && !ops->sync_file(ops->context, file, &error))
	{
		result->stage = SG_RUNE_V2_FS_STAGE_POINTER_SYNC;
		result->os_error = PubError(error);
		failed = 1;
	}
	error = 0;
	if (!ops->close_file(ops->context, file, &error))
	{
		if (!failed)
		{
			result->stage = SG_RUNE_V2_FS_STAGE_POINTER_CLOSE;
			result->os_error = PubError(error);
			failed = 1;
		}
		else
			result->close_error = PubError(error);
	}
	return !failed;
}

sg_rune_v2_artifact_publication_result_t SG_RuneV2ArtifactPublicationPublish(
	const char *publication_directory,
	const sg_rune_v2_publication_candidate_t *candidate,
	const sg_rune_v2_artifact_publication_ops_t *provided_ops)
{
	sg_rune_v2_artifact_publication_ops_t default_ops;
	sg_rune_v2_artifact_publication_result_t result;
	sg_rune_v2_active_generation_t active;
	sg_rune_v2_accepted_artifact_t accepted_copy;
	sg_rune_v2_publication_sidecar_file_t
		sidecar_copy[SG_RUNE_V2_MAX_SIDECARS];
	sg_rune_v2_publication_candidate_t candidate_copy;
	unsigned char current_manifest[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES];
	unsigned char candidate_manifest[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES];
	size_t current_size = 0U;
	size_t candidate_size;
	uint64_t active_generation = 0U;
	char stage_leaf[64], final_leaf[64], pointer_leaf[64], sidecar_leaf[32];
	char *stage_path = NULL, *final_path = NULL, *pointer_path = NULL;
	char *current_path = NULL, *file_path = NULL;
	uint32_t kind;
	int error = 0;

	if (!publication_directory || publication_directory[0] == '\0' ||
		!SG_RuneV2PublicationCandidateValid(candidate))
		return Result(SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT,
			SG_RUNE_V2_FS_STAGE_ARGUMENT);
	accepted_copy = *candidate->accepted;
	if (candidate->sidecar_count != 0U)
		memcpy(sidecar_copy, candidate->sidecars,
			(size_t)candidate->sidecar_count * sizeof(sidecar_copy[0]));
	candidate_copy = *candidate;
	candidate_copy.accepted = &accepted_copy;
	candidate_copy.sidecars = candidate->sidecar_count == 0U
		? NULL : sidecar_copy;
	candidate = &candidate_copy;
	if (!provided_ops)
	{
		SG_RuneV2ArtifactPublicationDefaultOps(&default_ops);
		provided_ops = &default_ops;
	}
	if (!OpsValid(provided_ops))
		return Result(SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT,
			SG_RUNE_V2_FS_STAGE_ARGUMENT);
	candidate_size = SG_RuneV2PublicationManifestEncode(candidate, candidate_manifest);
	result = ReadActiveInternal(publication_directory, &active,
		current_manifest, &current_size, provided_ops);
	if (result.diagnostic == SG_RUNE_V2_FS_PUBLICATION_OK)
	{
		active_generation = active.accepted.generation;
		if (active_generation == candidate->accepted->generation)
		{
			if (current_size != candidate_size || memcmp(current_manifest,
				candidate_manifest, candidate_size) != 0)
				return Result(SG_RUNE_V2_FS_PUBLICATION_GENERATION_CONFLICT,
					SG_RUNE_V2_FS_STAGE_ACTIVE_MANIFEST);
			result = Result(SG_RUNE_V2_FS_PUBLICATION_ALREADY_ACTIVE,
				SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN);
			result.observed_generation = active_generation;
			result.commit_visible = 1;
			if (!VerifyActiveGeneration(publication_directory, candidate,
				provided_ops, &result))
				return result;
			result.stage = SG_RUNE_V2_FS_STAGE_COMMIT_SYNC;
			if (!provided_ops->sync_directory(provided_ops->context,
				publication_directory, &error))
			{
				result.diagnostic = SG_RUNE_V2_FS_PUBLICATION_IO_ERROR;
				result.os_error = PubError(error);
				return result;
			}
			result.stage = SG_RUNE_V2_FS_STAGE_DONE;
			result.durability_complete = 1;
			return result;
		}
		if (active_generation > candidate->accepted->generation)
		{
			result = Result(SG_RUNE_V2_FS_PUBLICATION_STALE_GENERATION,
				SG_RUNE_V2_FS_STAGE_ACTIVE_MANIFEST);
			result.observed_generation = active_generation;
			return result;
		}
	}
	else if (result.diagnostic != SG_RUNE_V2_FS_PUBLICATION_NO_ACTIVE)
		return result;
	if (!CleanupRemnants(publication_directory, active_generation,
		candidate->accepted->generation, provided_ops, &error))
	{
		result = Result(SG_RUNE_V2_FS_PUBLICATION_IO_ERROR,
			SG_RUNE_V2_FS_STAGE_RECOVERY_CLEANUP);
		result.os_error = PubError(error);
		return result;
	}
	if (!FormatGenerationLeaf(stage_leaf, ".staging",
		candidate->accepted->generation) ||
		!FormatGenerationLeaf(final_leaf, "generation",
			candidate->accepted->generation) ||
		!FormatGenerationLeaf(pointer_leaf, ".CURRENT",
			candidate->accepted->generation))
		return Result(SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT,
			SG_RUNE_V2_FS_STAGE_ARGUMENT);
	(void)strcat(pointer_leaf, ".tmp");
	stage_path = PathJoin(publication_directory, stage_leaf);
	final_path = PathJoin(publication_directory, final_leaf);
	pointer_path = PathJoin(publication_directory, pointer_leaf);
	current_path = PathJoin(publication_directory, "CURRENT");
	result = Result(SG_RUNE_V2_FS_PUBLICATION_IO_ERROR,
		SG_RUNE_V2_FS_STAGE_STAGING_CREATE);
	if (!stage_path || !final_path || !pointer_path || !current_path)
	{
		result.diagnostic = SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED;
		goto done;
	}
	if (!provided_ops->make_directory(provided_ops->context, stage_path, &error))
	{
		result.os_error = PubError(error);
		goto done;
	}
	result.stage = SG_RUNE_V2_FS_STAGE_STAGING_PARENT_SYNC;
	if (!provided_ops->sync_directory(provided_ops->context,
		publication_directory, &error))
	{
		result.os_error = PubError(error);
		goto failed;
	}
	file_path = PathJoin(stage_path, "artifact.rune");
	if (!file_path)
	{
		result.diagnostic = SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED;
		result.stage = SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN;
		goto failed;
	}
	if (!WriteCandidateFile(file_path, candidate->artifact_bytes,
		candidate->artifact_size, &candidate->accepted->artifact_identity, 0U,
		candidate, provided_ops, &result))
		goto failed;
	free(file_path);
	file_path = NULL;
	for (kind = 1U; kind <= SG_RUNE_V2_MAX_SIDECARS; kind++)
	{
		const sg_rune_v2_publication_sidecar_file_t *sidecar =
			SidecarByKind(candidate, kind);

		if (!sidecar)
			continue;
		(void)snprintf(sidecar_leaf, sizeof(sidecar_leaf), "sidecar-%02u", kind);
		file_path = PathJoin(stage_path, sidecar_leaf);
		if (!file_path)
		{
			result.diagnostic = SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED;
			result.stage = SG_RUNE_V2_FS_STAGE_SIDECAR_OPEN;
			goto failed;
		}
		if (!WriteCandidateFile(file_path, sidecar->bytes, sidecar->size,
			&sidecar->exact_identity, kind, candidate, provided_ops, &result))
			goto failed;
		free(file_path);
		file_path = NULL;
	}
	result.stage = SG_RUNE_V2_FS_STAGE_STAGING_SYNC;
	if (!provided_ops->sync_directory(provided_ops->context, stage_path, &error))
	{
		result.os_error = PubError(error);
		goto failed;
	}
	result.stage = SG_RUNE_V2_FS_STAGE_GENERATION_RENAME;
	if (!provided_ops->rename_generation(provided_ops->context, stage_path,
		final_path, &error))
	{
		result.os_error = PubError(error);
		goto failed;
	}
	result.stage = SG_RUNE_V2_FS_STAGE_GENERATION_SYNC;
	if (!provided_ops->sync_directory(provided_ops->context,
		publication_directory, &error))
	{
		result.os_error = PubError(error);
		goto failed;
	}
	if (!WriteManifestFile(pointer_path, candidate_manifest, candidate_size,
		provided_ops, &result))
		goto failed;
	result.stage = SG_RUNE_V2_FS_STAGE_POINTER_RENAME;
	if (!provided_ops->replace_file(provided_ops->context, pointer_path,
		current_path, &error))
	{
		result.os_error = PubError(error);
		goto failed;
	}
	result.commit_visible = 1;
	result.stage = SG_RUNE_V2_FS_STAGE_COMMIT_SYNC;
	if (!provided_ops->sync_directory(provided_ops->context,
		publication_directory, &error))
	{
		result.os_error = PubError(error);
		goto failed;
	}
	result.diagnostic = SG_RUNE_V2_FS_PUBLICATION_OK;
	result.stage = SG_RUNE_V2_FS_STAGE_DONE;
	result.observed_generation = candidate->accepted->generation;
	result.durability_complete = 1;
	goto done;

failed:
	CleanupAfterFailure(publication_directory, candidate->accepted->generation,
		provided_ops, &result);
done:
	free(file_path);
	free(current_path);
	free(pointer_path);
	free(final_path);
	free(stage_path);
	return result;
}
