/* sg_rune_v2_artifact_loader.h -- fail-closed RUNE v2 publication. */
#ifndef SG_RUNE_V2_ARTIFACT_LOADER_H
#define SG_RUNE_V2_ARTIFACT_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_v2_codec.h"

typedef enum sg_rune_v2_loader_seek_origin_e
{
	SG_RUNE_V2_LOADER_SEEK_BEGIN = 0,
	SG_RUNE_V2_LOADER_SEEK_END
} sg_rune_v2_loader_seek_origin_t;

/* close_file always consumes the handle. A zero error on a short read means
 * clean EOF. Every successful allocation is independently owned. */
typedef struct sg_rune_v2_artifact_loader_ops_s
{
	void *context;
	void *(*open_read)(void *context, const char *path, int *os_error_out);
	size_t (*read)(void *context, void *file, unsigned char *output,
		size_t output_size, int *os_error_out);
	int (*seek)(void *context, void *file,
		sg_rune_v2_loader_seek_origin_t origin, size_t offset,
		int *os_error_out);
	int (*tell)(void *context, void *file, size_t *offset_out,
		int *os_error_out);
	int (*close_file)(void *context, void *file, int *os_error_out);
	void *(*allocate)(void *context, size_t size);
	void (*deallocate)(void *context, void *allocation);
} sg_rune_v2_artifact_loader_ops_t;

typedef enum sg_rune_v2_artifact_loader_diagnostic_e
{
	SG_RUNE_V2_LOADER_OK = 0,
	SG_RUNE_V2_LOADER_INVALID_ARGUMENT,
	SG_RUNE_V2_LOADER_NOT_INITIALIZED,
	SG_RUNE_V2_LOADER_IO_ERROR,
	SG_RUNE_V2_LOADER_BAD_FILE_SIZE,
	SG_RUNE_V2_LOADER_ALLOCATION_FAILED,
	SG_RUNE_V2_LOADER_BINDING_MISMATCH,
	SG_RUNE_V2_LOADER_WIRE_REJECTED
} sg_rune_v2_artifact_loader_diagnostic_t;

typedef enum sg_rune_v2_artifact_loader_stage_e
{
	SG_RUNE_V2_LOADER_STAGE_NONE = 0,
	SG_RUNE_V2_LOADER_STAGE_ARGUMENT,
	SG_RUNE_V2_LOADER_STAGE_OPEN,
	SG_RUNE_V2_LOADER_STAGE_FILE_SIZE,
	SG_RUNE_V2_LOADER_STAGE_FILE_ALLOCATION,
	SG_RUNE_V2_LOADER_STAGE_FILE_READ,
	SG_RUNE_V2_LOADER_STAGE_CLOSE,
	SG_RUNE_V2_LOADER_STAGE_INSPECT,
	SG_RUNE_V2_LOADER_STAGE_BINDING,
	SG_RUNE_V2_LOADER_STAGE_MODEL_ALLOCATION,
	SG_RUNE_V2_LOADER_STAGE_DECODE,
	SG_RUNE_V2_LOADER_STAGE_PUBLICATION
} sg_rune_v2_artifact_loader_stage_t;

typedef struct sg_rune_v2_artifact_load_result_s
{
	sg_rune_v2_artifact_loader_diagnostic_t diagnostic;
	sg_rune_v2_artifact_loader_stage_t stage;
	sg_rune_v2_wire_diagnostic_t wire_diagnostic;
	int os_error;
	/* Secondary when an earlier file failure owns diagnostic and stage. */
	int close_error;
	size_t observed_file_size;
	size_t bytes_read;
} sg_rune_v2_artifact_load_result_t;

/* All model array pointers remain valid and immutable until Reset, Destroy,
 * or the next successful load. Rejected loads preserve this object and every
 * byte it references. */
typedef struct sg_rune_v2_artifact_snapshot_s
{
	sg_rune_v2_artifact_binding_t binding;
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
} sg_rune_v2_artifact_snapshot_t;

/* Public only for caller-provided static/stack storage. Treat fields as
 * opaque, do not copy an initialized loader, and do not use it concurrently. */
typedef struct sg_rune_v2_artifact_loader_s
{
	uint32_t state;
	uint32_t state_inverse;
	sg_rune_v2_artifact_loader_ops_t ops;
	void *owned_active;
} sg_rune_v2_artifact_loader_t;

#define SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER { 0 }

void SG_RuneV2ArtifactLoaderDefaultOps(
	sg_rune_v2_artifact_loader_ops_t *ops_out);
/* loader must initially use SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER. Init
 * rejects an already initialized loader so it cannot orphan a publication. */
int SG_RuneV2ArtifactLoaderInit(sg_rune_v2_artifact_loader_t *loader,
	const sg_rune_v2_artifact_loader_ops_t *ops);
void SG_RuneV2ArtifactLoaderReset(sg_rune_v2_artifact_loader_t *loader);
void SG_RuneV2ArtifactLoaderDestroy(sg_rune_v2_artifact_loader_t *loader);
const sg_rune_v2_artifact_snapshot_t *SG_RuneV2ArtifactLoaderSnapshot(
	const sg_rune_v2_artifact_loader_t *loader);

/* exact_file_identity comes from the external exact-file identity boundary.
 * The loader compares this opaque value and never computes a replacement. */
sg_rune_v2_artifact_load_result_t SG_RuneV2ArtifactLoaderLoadBytes(
	sg_rune_v2_artifact_loader_t *loader,
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_artifact_binding_t *expected_binding,
	const sg_rune_v2_content_id_t *exact_file_identity);
sg_rune_v2_artifact_load_result_t SG_RuneV2ArtifactLoaderLoadFile(
	sg_rune_v2_artifact_loader_t *loader, const char *path,
	const sg_rune_v2_artifact_binding_t *expected_binding,
	const sg_rune_v2_content_id_t *exact_file_identity);

#endif /* SG_RUNE_V2_ARTIFACT_LOADER_H */
