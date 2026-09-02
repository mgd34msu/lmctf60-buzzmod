/* Era-4 hook reach: where a rope from here can take the body.
 *
 * From every floor cell, the hookable surfaces in the clusters the engine's
 * own visibility says are in view, within rope range and above the eye,
 * are the candidate bites.  Each survives an exact line trace from the eye
 * (the bolt's flight), then the pull is traced with the body's hull toward
 * the bite, and letting go at a few points along the clear pull is traced
 * as a flight to where the body lands.  Each distinct landing is one hook
 * record: fire at this bite from this cell, ride, let go, land there.  The
 * runtime executes it with live traces; nothing here is sampled positions,
 * every step is a root against a plane or a trace against the world. */
#ifndef SG_RUNE_HOOK_H
#define SG_RUNE_HOOK_H

#include <stdint.h>

#include "sg_rune_movement.h"

struct sg_bsp_world_s;
struct sg_host_collision_authority_s;
struct sg_rune_cx_s;
struct sg_rune_law_s;

typedef struct sg_rune_hook_report_s
{
	uint32_t bites;           /* hookable surfaces considered */
	uint32_t cells;           /* floor cells that got at least one record */
	uint32_t records;
	uint32_t traces;
	uint32_t candidates;      /* bites in view, in range, above, facing */
	uint32_t bolt_clear;      /* whose line from the eye reached them */
	uint32_t pull_clear;      /* whose pull had room */
	uint32_t flights;         /* releases that were traced */
} sg_rune_hook_report_t;

int SG_RuneHookEmit(const struct sg_bsp_world_s *bsp,
	const struct sg_host_collision_authority_s *authority,
	const struct sg_rune_cx_s *cx, const struct sg_rune_law_s *law,
	sg_rune_move_store_t *movement, sg_rune_hook_report_t *report_out);

#endif /* SG_RUNE_HOOK_H */
