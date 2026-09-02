/* Era-4 RUNE generation: BSP world plus host collision authority in, one
 * artifact image out.  Cells, regions, complex, movement, encode. */
#ifndef SG_RUNE_GENERATE_H
#define SG_RUNE_GENERATE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_artifact.h"

struct sg_bsp_world_s;
struct sg_host_collision_authority_s;

typedef void (*sg_rune_generate_progress_fn)(void *context,
	const char *stage, uint32_t done, uint32_t total);

typedef struct sg_rune_generate_report_s
{
	const char *stage;        /* the stage that failed, or the last one */
	const char *error;        /* NULL on success */
	uint32_t cells;
	uint32_t portals;
	uint32_t capabilities;
	uint32_t surfaces;
	uint32_t mechanisms;
	size_t image_bytes;
	double seconds;
} sg_rune_generate_report_t;

/* The image is malloc'd; free it with free().  Progress is reported per
 * stage with done/total, and once with total 0 as each stage begins. */
int SG_RuneGenerate(const struct sg_bsp_world_s *world,
	const struct sg_host_collision_authority_s *authority,
	const sg_rune_identity_t *identity, const sg_rune_law_t *law,
	sg_rune_generate_progress_fn progress, void *progress_context,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_generate_report_t *report_out);

#endif /* SG_RUNE_GENERATE_H */
