/* sg_rune_file.h -- RUNE artifact file adapter. */
#ifndef SG_RUNE_FILE_H
#define SG_RUNE_FILE_H

#include <stdint.h>

#include "sg_rune.h"

typedef void *(*sg_rune_alloc_fn)(int bytes);
typedef void (*sg_rune_free_fn)(void *allocation);

typedef enum sg_rune_file_load_status_e
{
	SG_RUNE_FILE_LOAD_MISSING = 0,
	SG_RUNE_FILE_LOAD_REJECTED,
	SG_RUNE_FILE_LOAD_READY
} sg_rune_file_load_status_t;

typedef struct sg_rune_file_load_result_s
{
	sg_rune_file_load_status_t status;
	const char *stage;
	const char *reason;
	uint32_t index;
	int os_error;
} sg_rune_file_load_result_t;

/* Decode the RUNE artifact into the native model. On READY the caller owns
 * rune_out and
 * every native array through the supplied allocator. */
sg_rune_file_load_result_t SG_RuneFileLoad(const char *path,
	const rune_identity_t *expected_identity,
	sg_rune_alloc_fn allocate, sg_rune_free_fn release,
	rune_t **rune_out);

typedef enum sg_rune_file_inspect_status_e
{
	SG_RUNE_FILE_INSPECT_ERROR = 0,
	SG_RUNE_FILE_INSPECT_DRIFT,
	SG_RUNE_FILE_INSPECT_MATCH
} sg_rune_file_inspect_status_t;

/* Re-read only the authenticated production header and exact file extent.
 * MATCH returns the checked native artifact identity in artifact_out. */
sg_rune_file_inspect_status_t SG_RuneFileInspect(const char *path,
	const rune_identity_t *expected_identity, rune_artifact_t *artifact_out,
	int *os_error_out);

#endif /* SG_RUNE_FILE_H */
