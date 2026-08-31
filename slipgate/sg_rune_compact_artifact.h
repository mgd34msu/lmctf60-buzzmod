#ifndef SG_RUNE_COMPACT_ARTIFACT_H
#define SG_RUNE_COMPACT_ARTIFACT_H

/*
 * File boundary for the compact RUNE wire image.
 *
 * The wire codec owns the format and model validation.  This boundary owns
 * only exact-file I/O, transactional loader publication, and atomic POSIX
 * replacement.  It deliberately does not carry the retired seed/link/action
 * representation or any filesystem device/inode identity.
 */

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_wire.h"

/* This is a format safety bound, not a generation or review deadline.  It is
 * aligned with the independent readers so a hostile length cannot turn a
 * file load into an unbounded allocation. */
#define SG_RUNE_COMPACT_ARTIFACT_MAX_IMAGE_BYTES UINT64_C(4294967296)

typedef enum sg_rune_compact_artifact_load_diagnostic_e
{
	SG_RUNE_COMPACT_ARTIFACT_LOAD_OK = 0,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_NOT_INITIALIZED,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_OPEN,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_STAT,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_KIND,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_SIZE,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_READ,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_GREW,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_FILE_CLOSE,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_WIRE_REJECTED,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_CODE_COUNT
} sg_rune_compact_artifact_load_diagnostic_t;

typedef enum sg_rune_compact_artifact_load_stage_e
{
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_NONE = 0,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ARGUMENT,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_OPEN,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_STAT,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_SIZE,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_ALLOCATION,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_READ,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_GROWTH,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_CLOSE,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_WIRE,
	SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_PUBLICATION
} sg_rune_compact_artifact_load_stage_t;

typedef struct sg_rune_compact_artifact_load_result_s
{
	sg_rune_compact_artifact_load_diagnostic_t diagnostic;
	sg_rune_compact_artifact_load_stage_t stage;
	sg_rune_compact_wire_error_t wire_error;
	int os_error;
	int close_error;
	size_t file_size;
	size_t bytes_read;
} sg_rune_compact_artifact_load_result_t;

/* The loader publishes only an owned decoded model.  The public model view is
 * const and remains immutable until Reset, Destroy, or a later successful
 * load.  Rejected loads leave the current publication untouched. */
typedef struct sg_rune_compact_artifact_loader_s
{
	uint32_t state;
	uint32_t state_inverse;
	sg_rune_compact_wire_decoded_t *published;
} sg_rune_compact_artifact_loader_t;

#define SG_RUNE_COMPACT_ARTIFACT_LOADER_INITIALIZER { 0U, 0U, NULL }

int SG_RuneCompactArtifactLoaderInit(
	sg_rune_compact_artifact_loader_t *loader);
void SG_RuneCompactArtifactLoaderReset(
	sg_rune_compact_artifact_loader_t *loader);
void SG_RuneCompactArtifactLoaderDestroy(
	sg_rune_compact_artifact_loader_t *loader);
const sg_rune_compact_model_t *SG_RuneCompactArtifactLoaderSnapshot(
	const sg_rune_compact_artifact_loader_t *loader);

/* The supplied byte span is the complete file boundary.  No bytes outside it
 * are inspected.  Identity is mandatory and is checked by the wire decoder. */
sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadBytes(
	sg_rune_compact_artifact_loader_t *loader,
	const unsigned char *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity);

/* The default file path uses these operations internally.  Tests and hosts
 * that already own an open-file abstraction can inject the same operations to
 * exercise growth, shrink, non-regular, and I/O-failure paths without a path
 * race or platform-specific filesystem fixture.  Every operation refers to
 * the one handle returned by open_read; no callback may reopen path. */
typedef struct sg_rune_compact_artifact_load_ops_s
{
	void *context;
	void *(*open_read)(void *context, const char *path, int *os_error_out);
	int (*stat_file)(void *context, void *file, int64_t *size_out,
		int *regular_out, int *os_error_out);
	size_t (*read)(void *context, void *file, unsigned char *output,
		size_t output_size, int *os_error_out);
	int (*probe)(void *context, void *file, int *has_extra_out,
		int *os_error_out);
	/* close_file consumes the handle even when it reports failure. */
	int (*close_file)(void *context, void *file, int *os_error_out);
} sg_rune_compact_artifact_load_ops_t;

void SG_RuneCompactArtifactDefaultLoadOps(
	sg_rune_compact_artifact_load_ops_t *ops_out);

/* Opens one file handle, obtains its exact regular-file size, reads exactly
 * that handle, and checks for a byte beyond the bound before decoding.  It
 * does not compare device or inode numbers and does not reopen the path. */
sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadFile(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	const sg_rune_compact_identity_t *expected_identity);

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadFileWithOps(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_artifact_load_ops_t *ops);

const char *SG_RuneCompactArtifactLoadDiagnosticString(
	sg_rune_compact_artifact_load_diagnostic_t diagnostic);

typedef enum sg_rune_compact_artifact_write_diagnostic_e
{
	SG_RUNE_COMPACT_ARTIFACT_WRITE_OK = 0,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_WIRE_REJECTED,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_SINK_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_CODE_COUNT
} sg_rune_compact_artifact_write_diagnostic_t;

typedef enum sg_rune_compact_artifact_write_stage_e
{
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_NONE = 0,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ARGUMENT,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_MEASURE,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ALLOCATION,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_ENCODE,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_VALIDATE,
	SG_RUNE_COMPACT_ARTIFACT_WRITE_STAGE_SINK
} sg_rune_compact_artifact_write_stage_t;

typedef struct sg_rune_compact_artifact_write_result_s
{
	sg_rune_compact_artifact_write_diagnostic_t diagnostic;
	sg_rune_compact_artifact_write_stage_t stage;
	sg_rune_compact_wire_error_t wire_error;
	int os_error;
	size_t image_size;
	size_t bytes_transferred;
} sg_rune_compact_artifact_write_result_t;

/* A sink may accept a short write.  Returning zero is failure even when the
 * callback reports no OS error, which prevents an accidental infinite loop. */
typedef size_t (*sg_rune_compact_artifact_sink_fn)(void *context,
	const unsigned char *bytes, size_t size, int *os_error_out);

/* Allocates one deterministic encoded image.  The caller owns *image_out and
 * releases it with free().  No output is returned after any failure. */
int SG_RuneCompactArtifactEncode(const sg_rune_compact_model_t *model,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_compact_wire_error_t *wire_error_out);

sg_rune_compact_artifact_write_result_t SG_RuneCompactArtifactWriteModel(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_artifact_sink_fn sink, void *sink_context);

typedef struct sg_rune_compact_artifact_fs_ops_s
{
	void *context;
	/* open_temp writes the actual same-directory temporary path into the
	 * caller-provided buffer and returns an owned handle. */
	void *(*open_temp)(void *context, const char *destination,
		char *temp_path, size_t temp_path_size, int *os_error_out);
	size_t (*write)(void *context, void *file, const unsigned char *bytes,
		size_t size, int *os_error_out);
	int (*sync_file)(void *context, void *file, int *os_error_out);
	/* close_file consumes file even when it reports failure. */
	int (*close_file)(void *context, void *file, int *os_error_out);
	int (*rename_file)(void *context, const char *temporary_path,
		const char *destination, int *os_error_out);
	int (*remove_file)(void *context, const char *path, int *os_error_out);
	/* Returns success only after the directory-entry durability barrier. */
	int (*sync_directory)(void *context, const char *destination,
		int *os_error_out);
} sg_rune_compact_artifact_fs_ops_t;

typedef enum sg_rune_compact_artifact_publication_diagnostic_e
{
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_OK = 0,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WIRE_REJECTED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_TEMP_OPEN_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_WRITE_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_FILE_SYNC_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_FILE_CLOSE_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_RENAME_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_DIRECTORY_SYNC_FAILED,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_CODE_COUNT
} sg_rune_compact_artifact_publication_diagnostic_t;

typedef enum sg_rune_compact_artifact_publication_stage_e
{
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_NONE = 0,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ARGUMENT,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_MEASURE,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ALLOCATION,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_ENCODE,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_VALIDATE,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_TEMP_OPEN,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_WRITE,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_FILE_SYNC,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_FILE_CLOSE,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_RENAME,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DIRECTORY_SYNC,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_CLEANUP,
	SG_RUNE_COMPACT_ARTIFACT_PUBLICATION_STAGE_DONE
} sg_rune_compact_artifact_publication_stage_t;

typedef struct sg_rune_compact_artifact_publication_result_s
{
	sg_rune_compact_artifact_publication_diagnostic_t diagnostic;
	sg_rune_compact_artifact_publication_stage_t stage;
	sg_rune_compact_wire_error_t wire_error;
	int os_error;
	int cleanup_error;
	size_t image_size;
	size_t bytes_transferred;
	int published;
	int durable;
} sg_rune_compact_artifact_publication_result_t;

void SG_RuneCompactArtifactDefaultFsOps(
	sg_rune_compact_artifact_fs_ops_t *ops_out);

/* Candidate validation happens before a temporary file is created.  Before
 * rename, every byte is written, the file is synchronized, and the handle is
 * closed.  The rename is the only visibility change.  published means the
 * rename completed; durable is set only when sync_directory also succeeds.
 * The default Windows rename uses write-through replacement, while its
 * directory callback conservatively reports no portable directory barrier, so
 * Windows can return published=1,durable=0. */
sg_rune_compact_artifact_publication_result_t
SG_RuneCompactArtifactPublish(
	const char *destination, const unsigned char *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_artifact_fs_ops_t *ops);

/* Convenience boundary for a model that has not yet been encoded. */
sg_rune_compact_artifact_publication_result_t
SG_RuneCompactArtifactPublishModel(
	const char *destination, const sg_rune_compact_model_t *model,
	const sg_rune_compact_artifact_fs_ops_t *ops);

const char *SG_RuneCompactArtifactPublicationDiagnosticString(
	sg_rune_compact_artifact_publication_diagnostic_t diagnostic);

#endif /* SG_RUNE_COMPACT_ARTIFACT_H */
