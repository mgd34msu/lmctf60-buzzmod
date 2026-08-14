/* sg_sidecar_loader.h -- one-open authenticated RUNE v3 sidecar snapshots. */
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

/* All callbacks report an operating-system error explicitly.  A zero error on
 * a short read means clean EOF; a failed scalar operation with zero error is
 * normalized to EIO by the loader.  close_file always consumes the handle. */
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
	/* A close error is secondary when an earlier failure already owns the
	 * diagnostic and stage. */
	int close_error;
	uint32_t plane;
	uint32_t index;
	size_t expected_file_size;
	size_t observed_file_size;
	size_t bytes_read;
} sg_sidecar_load_result_t;

/* Fill caller-owned operations with the stdio/malloc implementation. */
void SG_SidecarV3DefaultLoadOps(sg_sidecar_load_ops_t *ops_out);

/* Derive <game>/maps/<authenticated-rune-map><kind-extension>.  The output is
 * cleared on rejection, and exact fit includes the terminating NUL. */
sg_sidecar_diagnostic_t SG_SidecarV3Path(char *output, size_t output_size,
	const char *game_directory, sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune);

/* Capture and authenticate one optional sidecar through exactly one open
 * handle.  The first 48 bytes and exact file size are validated before the
 * bounded private snapshot allocation.  The handle is closed before Decode
 * can publish the payload.  On success, *payload_out owns the one allocation
 * made through ops->allocate (its first *payload_size_out bytes are the decoded
 * payload); the caller releases it through the same ops->deallocate.  Both
 * outputs remain unchanged for every other result.  NULL ops selects the
 * default stdio/malloc implementation. */
sg_sidecar_load_result_t SG_SidecarV3LoadFile(
	const char *game_directory, sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune,
	const uint8_t *live_seed_marks, size_t live_seed_capacity,
	unsigned char **payload_out, size_t *payload_size_out,
	const sg_sidecar_load_ops_t *ops);

#endif /* SG_SIDECAR_LOADER_H */
