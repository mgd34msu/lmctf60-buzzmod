/* Era-4 router and fields.
 *
 * The router indexes the artifact once per level: which capabilities arrive
 * at each cell, where each portal is, and what each crossing costs under its
 * profile.  A field is one destination's cost-to-go over every (cell,
 * stance) state and the capability to take next from it, from one reverse
 * shortest-path pass.  A step is what a body in a state does now: cross a
 * portal a certain way, or it has arrived, or it cannot get there. */
#ifndef SG_RUNE_FIELD_H
#define SG_RUNE_FIELD_H

#include <stdint.h>

#include "sg_rune_artifact.h"

typedef struct sg_rune_router_s
{
	const sg_rune_artifact_t *artifact;
	float *cell_center;       /* cell_count * 3 */
	float *portal_center;     /* portal_count * 3 */
	uint32_t *arrival_first;  /* cell_count + 1, into arrivals */
	uint32_t *arrivals;       /* capability indices by destination cell */
	uint32_t *departure_first; /* cell_count + 1, into departures */
	uint32_t *departures;     /* capability indices by source cell */
	float *edge_cost;         /* per capability; INFINITY when unusable */
	uint32_t *destination;    /* per capability: destination cell */
} sg_rune_router_t;

int SG_RuneRouterBuild(sg_rune_router_t *router,
	const sg_rune_artifact_t *artifact);
void SG_RuneRouterFree(sg_rune_router_t *router);

/* State index: cell * 2 + (0 standing, 1 crouching). */
#define SG_RUNE_FIELD_STATE(cell, crouching) ((cell) * 2U + ((crouching) ? 1U : 0U))

typedef struct sg_rune_field_s
{
	uint32_t destination_cell;
	uint32_t state_count;
	float *cost;              /* cost to go; INFINITY unreachable */
	uint32_t *next;           /* capability to take; INDEX_NONE at the end */
	uint8_t *next_crouching;  /* stance to arrive in through that capability */
	uint32_t settled;         /* states reached */
} sg_rune_field_t;

/* Builds the field for one destination cell.  Reusable: a second call on
 * the same field reuses its arrays. */
int SG_RuneFieldBuild(sg_rune_field_t *field, const sg_rune_router_t *router,
	uint32_t destination_cell);
/* The same with a surcharge (seconds) added for every cell entered: what
 * a cell costs beyond the time to cross it, such as being under an
 * enemy's line of fire.  NULL surcharge is the plain build. */
int SG_RuneFieldBuildWeighted(sg_rune_field_t *field,
	const sg_rune_router_t *router, uint32_t destination_cell,
	const float *cell_surcharge);
void SG_RuneFieldFree(sg_rune_field_t *field);

typedef enum sg_rune_step_kind_e
{
	SG_RUNE_STEP_HOLD = 0,    /* no field, or the body is nowhere */
	SG_RUNE_STEP_ARRIVED,     /* in the destination cell: go to the point */
	SG_RUNE_STEP_CROSS,       /* take capability through portal toward target */
	SG_RUNE_STEP_UNREACHABLE,
	SG_RUNE_STEP_KIND_COUNT
} sg_rune_step_kind_t;

typedef struct sg_rune_step_s
{
	sg_rune_step_kind_t kind;
	uint32_t cell;
	uint32_t portal;
	uint32_t capability;
	uint8_t move_kind;        /* sg_rune_move_kind_t */
	uint8_t crouching_now;
	uint8_t crouching_next;
	uint8_t hook_point_present;
	float target[3];          /* portal centre, or the destination point */
	float cost_to_go;
	float hook_point[3];
} sg_rune_step_t;

/* The step from (cell, crouching) toward the field's destination; point is
 * where the body should end up inside the destination cell. */
int SG_RuneStepSelect(const sg_rune_router_t *router,
	const sg_rune_field_t *field, uint32_t cell, int crouching,
	const float point[3], sg_rune_step_t *step_out);

/* As SG_RuneStepSelect, but never through a capability listed in avoid:
 * the cheapest other departure from the cell, by its edge cost plus the
 * field beyond it.  UNREACHABLE when nothing else leads anywhere. */
int SG_RuneStepSelectAvoiding(const sg_rune_router_t *router,
	const sg_rune_field_t *field, uint32_t cell, int crouching,
	const float point[3], const uint32_t *avoid, uint32_t avoid_count,
	sg_rune_step_t *step_out);

const char *SG_RuneStepKindString(sg_rune_step_kind_t kind);

struct sg_rune_locator_s;
/* The nearest cell within radius (horizontal) that the field reaches from,
 * for a body standing somewhere the complex does not know as floor (an
 * entity's top, a brush model).  Returns the cell and its floor point. */
uint32_t SG_RuneFieldNearestReachable(const sg_rune_router_t *router,
	const struct sg_rune_locator_s *locator, const sg_rune_field_t *field,
	const float origin[3], float radius, float point_out[3]);

#endif /* SG_RUNE_FIELD_H */
