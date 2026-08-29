#ifndef SG_RUNE_V2_EXACT_SNAPSHOT_PRIVATE_H
#define SG_RUNE_V2_EXACT_SNAPSHOT_PRIVATE_H

#include "sg_rune_v2_exact_snapshot.h"

typedef struct sg_rune_v2_snapshot_file_info_s
{
	uint64_t size;
	int is_regular;
} sg_rune_v2_snapshot_file_info_t;

typedef struct sg_rune_v2_snapshot_io_s
{
	void *context;
	void *(*open_read)(void *context, const char *utf8_path);
	int (*inspect)(void *context, void *file,
		sg_rune_v2_snapshot_file_info_t *info_out);
	size_t (*read)(void *context, void *file, unsigned char *output,
		size_t output_size, int *failed_out);
	int (*close_file)(void *context, void *file);
	void *(*allocate)(void *context, size_t size);
	void (*deallocate)(void *context, void *allocation);
} sg_rune_v2_snapshot_io_t;

sg_rune_v2_snapshot_diagnostic_t SG_RuneV2ExactSnapshotAcquireWithIO(
	const char *utf8_path, sg_rune_v2_snapshot_kind_t kind,
	sg_rune_v2_exact_snapshot_t **snapshot_out,
	const sg_rune_v2_snapshot_io_t *io);

#endif
