#ifndef SG_RUNE_V2_EXACT_SNAPSHOT_H
#define SG_RUNE_V2_EXACT_SNAPSHOT_H

#include <stddef.h>

#include "sg_rune_v2_content_identity.h"

typedef enum sg_rune_v2_snapshot_kind_e
{
	SG_RUNE_V2_SNAPSHOT_ARTIFACT = 1
} sg_rune_v2_snapshot_kind_t;

typedef struct sg_rune_v2_exact_snapshot_s sg_rune_v2_exact_snapshot_t;

typedef struct sg_rune_v2_snapshot_view_s
{
	sg_rune_v2_snapshot_kind_t kind;
	const unsigned char *bytes;
	size_t size;
	sg_rune_v2_content_id_t content_identity;
} sg_rune_v2_snapshot_view_t;

typedef enum sg_rune_v2_snapshot_diagnostic_e
{
	SG_RUNE_V2_SNAPSHOT_OK = 0,
	SG_RUNE_V2_SNAPSHOT_INVALID_ARGUMENT,
	SG_RUNE_V2_SNAPSHOT_OPEN_FAILED,
	SG_RUNE_V2_SNAPSHOT_INSPECT_FAILED,
	SG_RUNE_V2_SNAPSHOT_NOT_REGULAR,
	SG_RUNE_V2_SNAPSHOT_TOO_LARGE,
	SG_RUNE_V2_SNAPSHOT_ALLOCATION_FAILED,
	SG_RUNE_V2_SNAPSHOT_READ_FAILED,
	SG_RUNE_V2_SNAPSHOT_SHORT_READ,
	SG_RUNE_V2_SNAPSHOT_EXTRA_BYTES,
	SG_RUNE_V2_SNAPSHOT_FILE_CHANGED,
	SG_RUNE_V2_SNAPSHOT_CLOSE_FAILED
} sg_rune_v2_snapshot_diagnostic_t;

sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotAcquireFile(
	const char *utf8_path, sg_rune_v2_snapshot_kind_t kind,
	sg_rune_v2_exact_snapshot_t **snapshot_out);
sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotCopyBytes(
	sg_rune_v2_snapshot_kind_t kind, const unsigned char *bytes, size_t size,
	sg_rune_v2_exact_snapshot_t **snapshot_out);
sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotInspect(
	const sg_rune_v2_exact_snapshot_t *snapshot,
	const sg_rune_v2_snapshot_view_t **view_out);
void SG_RuneV2ExactSnapshotDestroy(sg_rune_v2_exact_snapshot_t *snapshot);

#endif
