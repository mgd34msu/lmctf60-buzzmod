/* sg_sidecar_loader.h -- one-open compact sidecar snapshots. */
#ifndef SG_SIDECAR_LOADER_H
#define SG_SIDECAR_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_sidecar_wire.h"

typedef enum sg_sidecar_seek_origin_e
{
	SG_SIDECAR_SEEK_BEGIN = 0,
	SG_SIDECAR_SEEK_END
} sg_sidecar_seek_origin_t;

/* A failed scalar callback with zero OS error is normalized to EIO.  A close
 * always consumes its handle. */
typedef struct sg_sidecar_load_ops_s
{
	void *context;
	void *(*open_read)(void *context, const char *path,
		int *os_error_out);
	size_t (*read)(void *context, void *file, unsigned char *output,
		size_t output_size, int *os_error_out);
	int (*seek)(void *context, void *file,
		sg_sidecar_seek_origin_t origin, size_t offset,
		int *os_error_out);
	int (*tell)(void *context, void *file, size_t *offset_out,
		int *os_error_out);
	int (*close_file)(void *context, void *file, int *os_error_out);
	void *(*allocate)(void *context, size_t size);
	void (*deallocate)(void *context, void *allocation);
} sg_sidecar_load_ops_t;

typedef struct sg_sidecar_load_result_s
{
	sg_sidecar_diagnostic_t diagnostic;
	sg_sidecar_stage_t stage;
	int os_error;
	int close_error;
	size_t expected_file_size;
	size_t observed_file_size;
	size_t bytes_read;
} sg_sidecar_load_result_t;

void SG_SidecarDefaultLoadOps(sg_sidecar_load_ops_t *ops_out);

/* path names one retained sidecar file.  The caller must obtain info by
 * inspecting the exact compact artifact that is active for this load.  A
 * rejected sidecar leaves both outputs clear. */
sg_sidecar_load_result_t SG_SidecarLoadFile(
	const char *path, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, unsigned char **payload_out,
	size_t *payload_size_out, const sg_sidecar_load_ops_t *ops);

#endif /* SG_SIDECAR_LOADER_H */
