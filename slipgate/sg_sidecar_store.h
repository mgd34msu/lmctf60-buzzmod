/* sg_sidecar_store.h -- failure-atomic compact sidecar replacement. */
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

/* The callback compares the still-active exact artifact against info after
 * the temporary sidecar is durable and immediately before its replacement. */
typedef sg_sidecar_revalidate_t (*sg_sidecar_store_revalidate_fn)(
	void *context, const sg_rune_compact_wire_info_t *info,
	int *os_error_out);

/* All scalar callbacks return zero on success and nonzero on failure.
 * open_exclusive uses create-new/O_EXCL semantics.  replace_file atomically
 * replaces destination or leaves it unchanged. */
typedef struct sg_sidecar_store_ops_s
{
	void *context;
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
	int close_error;
	int cleanup_error;
	size_t expected_file_size;
	size_t bytes_written;
	unsigned int temp_attempts;
	int temp_created;
	int cleanup_attempted;
	int cleanup_complete;
	int replacement_complete;
	int durability_complete;
} sg_sidecar_store_result_t;

void SG_SidecarDefaultStoreOps(sg_sidecar_store_ops_t *ops_out);

/* destination is explicit because v12 identity deliberately contains no
 * mutable map filename.  Sidecar bytes must already encode the exact info.
 * No operation occurs between a matching revalidation and atomic replace. */
sg_sidecar_store_result_t SG_SidecarStoreFile(
	const char *destination, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info,
	const unsigned char *encoded, size_t encoded_size,
	sg_sidecar_store_revalidate_fn revalidate, void *revalidate_context,
	const sg_sidecar_store_ops_t *ops);

#endif /* SG_SIDECAR_STORE_H */
