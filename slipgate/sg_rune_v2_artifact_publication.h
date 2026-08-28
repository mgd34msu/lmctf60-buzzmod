/* Atomic filesystem publication for one accepted RUNE v2 generation. */
#ifndef SG_RUNE_V2_ARTIFACT_PUBLICATION_H
#define SG_RUNE_V2_ARTIFACT_PUBLICATION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_v2_acceptance.h"

typedef struct sg_rune_v2_publication_sidecar_file_s
{
	uint32_t kind;
	const unsigned char *bytes;
	size_t size;
	/* Supplied by the exact-file identity authority, never derived here. */
	sg_rune_v2_content_id_t exact_identity;
} sg_rune_v2_publication_sidecar_file_t;

typedef struct sg_rune_v2_staged_file_s
{
	void *ops_context;
	void *file;
	uint32_t sidecar_kind;
	size_t size;
	size_t (*read_at)(void *context, void *file, size_t offset,
		unsigned char *output, size_t output_size, int *os_error_out);
} sg_rune_v2_staged_file_t;

/* The identity authority reads the same still-open file that the publisher
 * wrote. It returns nonzero only when those bytes own expected_identity. */
typedef int (*sg_rune_v2_verify_staged_file_fn)(void *context,
	const sg_rune_v2_staged_file_t *staged,
	const sg_rune_v2_content_id_t *expected_identity, int *os_error_out);

/* accepted is the semantic authority. File bytes and exact sidecar identities
 * remain immutable for the call and come from the same accepted boundary. */
typedef struct sg_rune_v2_publication_candidate_s
{
	const sg_rune_v2_accepted_artifact_t *accepted;
	const unsigned char *artifact_bytes;
	size_t artifact_size;
	const sg_rune_v2_publication_sidecar_file_t *sidecars;
	uint32_t sidecar_count;
	sg_rune_v2_verify_staged_file_fn verify_staged_file;
	void *verify_context;
} sg_rune_v2_publication_candidate_t;

typedef struct sg_rune_v2_active_generation_s
{
	sg_rune_v2_accepted_artifact_t accepted;
} sg_rune_v2_active_generation_t;

/* All callbacks return nonzero on success. close_file always consumes the
 * handle. remove callbacks also succeed when the target does not exist.
 * rename_generation must not replace an existing target. */
typedef struct sg_rune_v2_artifact_publication_ops_s
{
	void *context;
	void *(*open_read)(void *context, const char *path,
		int *not_found_out, int *os_error_out);
	void *(*open_exclusive)(void *context, const char *path,
		int *os_error_out);
	size_t (*read)(void *context, void *file, unsigned char *output,
		size_t output_size, int *os_error_out);
	size_t (*read_at)(void *context, void *file, size_t offset,
		unsigned char *output, size_t output_size, int *os_error_out);
	size_t (*write)(void *context, void *file, const unsigned char *bytes,
		size_t size, int *os_error_out);
	int (*sync_file)(void *context, void *file, int *os_error_out);
	int (*close_file)(void *context, void *file, int *os_error_out);
	int (*make_directory)(void *context, const char *path, int *os_error_out);
	int (*rename_generation)(void *context, const char *staging_path,
		const char *generation_path, int *os_error_out);
	int (*replace_file)(void *context, const char *temporary_path,
		const char *destination_path, int *os_error_out);
	int (*sync_directory)(void *context, const char *path, int *os_error_out);
	int (*inspect_directory)(void *context, const char *path, int *exists_out,
		int *is_owned_directory_out, int *os_error_out);
	int (*remove_file)(void *context, const char *path, int *os_error_out);
	int (*remove_directory)(void *context, const char *path, int *os_error_out);
} sg_rune_v2_artifact_publication_ops_t;

typedef enum sg_rune_v2_artifact_publication_diagnostic_e
{
	SG_RUNE_V2_FS_PUBLICATION_OK = 0,
	SG_RUNE_V2_FS_PUBLICATION_ALREADY_ACTIVE,
	SG_RUNE_V2_FS_PUBLICATION_NO_ACTIVE,
	SG_RUNE_V2_FS_PUBLICATION_INVALID_ARGUMENT,
	SG_RUNE_V2_FS_PUBLICATION_ALLOCATION_FAILED,
	SG_RUNE_V2_FS_PUBLICATION_ACTIVE_CORRUPT,
	SG_RUNE_V2_FS_PUBLICATION_STALE_GENERATION,
	SG_RUNE_V2_FS_PUBLICATION_GENERATION_CONFLICT,
	SG_RUNE_V2_FS_PUBLICATION_STAGED_IDENTITY_REJECTED,
	SG_RUNE_V2_FS_PUBLICATION_IO_ERROR
} sg_rune_v2_artifact_publication_diagnostic_t;

typedef enum sg_rune_v2_artifact_publication_stage_e
{
	SG_RUNE_V2_FS_STAGE_NONE = 0,
	SG_RUNE_V2_FS_STAGE_ARGUMENT,
	SG_RUNE_V2_FS_STAGE_ACTIVE_OPEN,
	SG_RUNE_V2_FS_STAGE_ACTIVE_READ,
	SG_RUNE_V2_FS_STAGE_ACTIVE_CLOSE,
	SG_RUNE_V2_FS_STAGE_ACTIVE_MANIFEST,
	SG_RUNE_V2_FS_STAGE_RECOVERY_CLEANUP,
	SG_RUNE_V2_FS_STAGE_STAGING_CREATE,
	SG_RUNE_V2_FS_STAGE_STAGING_PARENT_SYNC,
	SG_RUNE_V2_FS_STAGE_ARTIFACT_OPEN,
	SG_RUNE_V2_FS_STAGE_ARTIFACT_WRITE,
	SG_RUNE_V2_FS_STAGE_ARTIFACT_SYNC,
	SG_RUNE_V2_FS_STAGE_ARTIFACT_VERIFY,
	SG_RUNE_V2_FS_STAGE_ARTIFACT_CLOSE,
	SG_RUNE_V2_FS_STAGE_SIDECAR_OPEN,
	SG_RUNE_V2_FS_STAGE_SIDECAR_WRITE,
	SG_RUNE_V2_FS_STAGE_SIDECAR_SYNC,
	SG_RUNE_V2_FS_STAGE_SIDECAR_VERIFY,
	SG_RUNE_V2_FS_STAGE_SIDECAR_CLOSE,
	SG_RUNE_V2_FS_STAGE_STAGING_SYNC,
	SG_RUNE_V2_FS_STAGE_GENERATION_RENAME,
	SG_RUNE_V2_FS_STAGE_GENERATION_SYNC,
	SG_RUNE_V2_FS_STAGE_POINTER_OPEN,
	SG_RUNE_V2_FS_STAGE_POINTER_WRITE,
	SG_RUNE_V2_FS_STAGE_POINTER_SYNC,
	SG_RUNE_V2_FS_STAGE_POINTER_CLOSE,
	SG_RUNE_V2_FS_STAGE_POINTER_RENAME,
	SG_RUNE_V2_FS_STAGE_COMMIT_SYNC,
	SG_RUNE_V2_FS_STAGE_DONE
} sg_rune_v2_artifact_publication_stage_t;

typedef struct sg_rune_v2_artifact_publication_result_s
{
	sg_rune_v2_artifact_publication_diagnostic_t diagnostic;
	sg_rune_v2_artifact_publication_stage_t stage;
	int os_error;
	int close_error;
	int cleanup_error;
	uint32_t sidecar_kind;
	size_t bytes_transferred;
	uint64_t observed_generation;
	int commit_visible;
	int durability_complete;
} sg_rune_v2_artifact_publication_result_t;

void SG_RuneV2ArtifactPublicationDefaultOps(
	sg_rune_v2_artifact_publication_ops_t *ops_out);

int SG_RuneV2ArtifactPublicationSucceeded(
	const sg_rune_v2_artifact_publication_result_t *result);

sg_rune_v2_artifact_publication_result_t
SG_RuneV2ArtifactPublicationReadActive(const char *publication_directory,
	sg_rune_v2_active_generation_t *active_out,
	const sg_rune_v2_artifact_publication_ops_t *ops);

/* One writer owns publication_directory. The function reconciles remnants for
 * this generation before staging and uses CURRENT's atomic replacement as the
 * sole visibility change. Equal identical retries complete the root sync. */
sg_rune_v2_artifact_publication_result_t SG_RuneV2ArtifactPublicationPublish(
	const char *publication_directory,
	const sg_rune_v2_publication_candidate_t *candidate,
	const sg_rune_v2_artifact_publication_ops_t *ops);

#endif /* SG_RUNE_V2_ARTIFACT_PUBLICATION_H */
