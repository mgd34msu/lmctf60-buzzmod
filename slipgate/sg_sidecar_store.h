/* sg_sidecar_store.h -- failure-atomic authenticated sidecar replacement. */
#ifndef SG_SIDECAR_STORE_H
#define SG_SIDECAR_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_sidecar_wire.h"

#define SG_SIDECAR_STORE_TEMP_ATTEMPTS 16U

typedef enum sg_sidecar_revalidate_e
{
	SG_SIDECAR_REVALIDATE_ERROR = -1,
	SG_SIDECAR_REVALIDATE_DRIFT = 0,
	SG_SIDECAR_REVALIDATE_MATCH = 1
} sg_sidecar_revalidate_t;

/* The caller owns the authority captured by context.  MATCH authorizes the
 * immediately following atomic replacement; DRIFT is a clean state mismatch;
 * ERROR is an I/O or authority-capture failure and reports its OS error. */
typedef sg_sidecar_revalidate_t (*sg_sidecar_store_revalidate_fn)(
	void *context, const rune_artifact_t *artifact, int *os_error_out);

/* All scalar callbacks return zero on success and nonzero on failure.  A zero
 * reported OS error on failure is normalized to EIO.  open_exclusive must use
 * create-new/O_EXCL semantics and may return EEXIST for a file, directory, or
 * symlink collision.  close_file always consumes the handle.  replace_file
 * atomically replaces destination or leaves it unchanged.  On Windows the
 * default replacement uses write-through; its directory-sync callback is
 * consequently a successful durability barrier. */
typedef struct sg_sidecar_store_ops_s
{
	void *context;
	/* Called once per transaction.  The default combines process identity,
	 * high-resolution wall/CPU time, address-space entropy, and a monotonic
	 * sequence so stale crash remnants do not consume one global set of names. */
	uint64_t (*temp_nonce)(void *context);
	void *(*open_exclusive)(void *context, const char *path,
		int *os_error_out);
	size_t (*write)(void *context, void *file,
		const unsigned char *data, size_t data_size, int *os_error_out);
	int (*flush)(void *context, void *file, int *os_error_out);
	int (*sync_file)(void *context, void *file, int *os_error_out);
	int (*close_file)(void *context, void *file, int *os_error_out);
	int (*replace_file)(void *context, const char *temporary_path,
		const char *destination_path, int *os_error_out);
	int (*sync_directory)(void *context, const char *directory_path,
		int *os_error_out);
	int (*remove_file)(void *context, const char *path,
		int *os_error_out);
} sg_sidecar_store_ops_t;

typedef struct sg_sidecar_store_result_s
{
	sg_sidecar_diagnostic_t diagnostic;
	sg_sidecar_stage_t stage;
	int os_error;
	/* Secondary errors never obscure the first transaction failure. */
	int close_error;
	int cleanup_error;
	uint32_t plane;
	uint32_t index;
	size_t expected_file_size;
	size_t bytes_written;
	unsigned int temp_attempts;
	/* Historical transaction facts: temp_created remains true after cleanup
	 * or replacement; cleanup_complete is meaningful when cleanup_attempted. */
	int temp_created;
	int cleanup_attempted;
	int cleanup_complete;
	int replacement_complete;
	int durability_complete;
} sg_sidecar_store_result_t;

void SG_SidecarDefaultStoreOps(sg_sidecar_store_ops_t *ops_out);

sg_sidecar_store_result_t SG_SidecarStoreFile(
	const char *game_directory, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact,
	const uint8_t *live_seed_marks, size_t live_seed_capacity,
	const unsigned char *encoded, size_t encoded_size,
	sg_sidecar_store_revalidate_fn revalidate, void *revalidate_context,
	const sg_sidecar_store_ops_t *ops);

#endif /* SG_SIDECAR_STORE_H */
