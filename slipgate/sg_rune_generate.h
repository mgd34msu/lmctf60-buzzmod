/* Era-4 RUNE generation: BSP world plus host collision authority in, one
 * artifact image out.  Cells, regions, complex, movement, encode. */
#ifndef SG_RUNE_GENERATE_H
#define SG_RUNE_GENERATE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_artifact.h"

struct sg_rune_bsp_s;
struct sg_rune_law_s;

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
	uint32_t hooks;           /* hook ride records */
	uint32_t fires;           /* fire relation records */
	uint32_t statics;         /* brush models carved as part of the world */
	size_t image_bytes;
	double seconds;
} sg_rune_generate_report_t;

/* The image is malloc'd; free it with free().  Progress is reported per
 * stage with done/total, and once with total 0 as each stage begins. */
/* The world is carved as it stands: the static brush models the entity
 * text declares are set on it first (SG_RuneBspSetStatics). */
int SG_RuneGenerate(struct sg_rune_bsp_s *world,
	const sg_rune_identity_t *identity, const sg_rune_law_t *law,
	sg_rune_generate_progress_fn progress, void *progress_context,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_generate_report_t *report_out);

#endif /* SG_RUNE_GENERATE_H */
