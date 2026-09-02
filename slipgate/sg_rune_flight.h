/* Era-4 flight: where a launched body lands.
 *
 * The arc under the host's law is a polynomial in time; every cell facet is
 * a plane; so the exit time from a cell is a root, the next cell is the
 * facet's other side, and the arc is followed exactly from cell to cell
 * until it meets a floor (landing), water, or nothing.  Walls and ceilings
 * clip the velocity the way the host does.  No stepping, no samples.  The
 * generator uses it to turn launches into landings; the runtime uses it to
 * know where a body already in the air is going. */
#ifndef SG_RUNE_FLIGHT_H
#define SG_RUNE_FLIGHT_H

#include <stdint.h>

#include "sg_rune_artifact.h"

typedef enum sg_rune_flight_outcome_e
{
	SG_RUNE_FLIGHT_LANDED = 0,  /* on a floor in landing_cell */
	SG_RUNE_FLIGHT_WATER,       /* entered water in landing_cell */
	SG_RUNE_FLIGHT_HARM,        /* entered lava or slime in landing_cell */
	SG_RUNE_FLIGHT_LOST,        /* left the complex or flew too long */
	SG_RUNE_FLIGHT_INVALID,
	SG_RUNE_FLIGHT_OUTCOME_COUNT
} sg_rune_flight_outcome_t;

typedef struct sg_rune_flight_s
{
	sg_rune_flight_outcome_t outcome;
	uint32_t landing_cell;
	float seconds;
	float landing[3];
	float landing_velocity[3];
	uint32_t crossings;         /* cells entered */
	uint32_t clips;             /* walls and ceilings the body glanced off on the way */
} sg_rune_flight_t;

/* Whether a flight from origin in start_cell lands on a floor that is
 * neither lava nor slime (or in water), and still does so with its
 * horizontal speed a tolerance slower and faster: a body never leaves a
 * floor at exactly the modelled speed.  With clean set, every arc must
 * reach its floor without glancing off a wall or ceiling: where a body
 * ricochets depends on details no model of it survives.  flight_out is
 * the nominal flight. */
int SG_RuneFlightLandsRobustly(const sg_rune_cx_view_t *complex,
	const sg_rune_law_t *law, uint32_t start_cell, const float origin[3],
	const float velocity[3], float tolerance, int clean, sg_rune_flight_t *flight_out);

/* Traces from origin in start_cell with velocity, under law's gravity. */
int SG_RuneFlightTrace(const sg_rune_cx_view_t *complex,
	const sg_rune_law_t *law, uint32_t start_cell, const float origin[3],
	const float velocity[3], sg_rune_flight_t *flight_out);

#endif /* SG_RUNE_FLIGHT_H */
