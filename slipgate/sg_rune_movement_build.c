/* Movement capabilities from the cell complex.
 *
 * Contact crossings come from each directed portal's own facts.  Flights
 * come from tracing: at every portal that leaves a floor into the air
 * through a wall, the body is launched at the portal's foot at full run
 * speed toward it, once as a drop, once as a jump, once as a rocket jump,
 * and the arc is followed through the complex to where it lands.  Each
 * landing on a floor or in water is one capability to that cell; an arc
 * into lava or slime, or a portal into them, is none. */
#include "sg_rune_movement.h"

#include <math.h>
#include <string.h>

#include "sg_rune_cx.h"
#include "sg_rune_flight.h"

#define LAUNCH_TOLERANCE 0.10f    /* the run speed a flight is checked against, either way */
#define MIN_FLIGHT_SECONDS 0.15f  /* shorter is a step onto the floor beside, not a flight */

static float FloatBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return isfinite(value) ? value : 0.0f;
}

static uint8_t Stances(sg_rune_cx_stances_t validity)
{
	uint8_t stances = 0U;

	if (validity & SG_RUNE_CX_STANCE_STANDING)
		stances |= SG_RUNE_MOVE_STANDING;
	if (validity & SG_RUNE_CX_STANCE_CROUCHING)
		stances |= SG_RUNE_MOVE_CROUCHING;
	return stances;
}

static void Q8(const sg_rune_cx_vec3_t *q8, float out[3])
{
	out[0] = (float)q8->value[0] / (float)SG_RUNE_CX_Q8_ONE;
	out[1] = (float)q8->value[1] / (float)SG_RUNE_CX_Q8_ONE;
	out[2] = (float)q8->value[2] / (float)SG_RUNE_CX_Q8_ONE;
}

/* The foot of a portal: the middle of its lowest edge. */
static void PortalFoot(const sg_rune_cx_view_t *cx,
	const sg_rune_cx_portal_t *portal, float foot[3])
{
	const sg_rune_cx_facet_t *facet = &cx->facets[portal->facet];
	float lowest = INFINITY, sum[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t index, count = 0U;

	for (index = 0U; index < facet->vertices.count; index++)
	{
		float vertex[3];

		Q8(&cx->vertices[facet->vertices.first + index], vertex);
		if (vertex[2] < lowest)
			lowest = vertex[2];
	}
	for (index = 0U; index < facet->vertices.count; index++)
	{
		float vertex[3];

		Q8(&cx->vertices[facet->vertices.first + index], vertex);
		if (vertex[2] > lowest + 8.0f)
			continue;
		sum[0] += vertex[0];
		sum[1] += vertex[1];
		sum[2] += vertex[2];
		count++;
	}
	foot[0] = count ? sum[0] / (float)count : 0.0f;
	foot[1] = count ? sum[1] / (float)count : 0.0f;
	foot[2] = count ? sum[2] / (float)count : 0.0f;
}

typedef struct flight_launch_s
{
	sg_rune_move_kind_t kind;
	float vertical;           /* impulse */
	float lead_seconds;
	float rise_before;        /* rocket jump: rise before the blast */
	float run;                /* fraction of run speed the body leaves at */
} flight_launch_t;

static int EmitFlights(sg_rune_move_store_t *store,
	const sg_rune_cx_view_t *cx, const sg_rune_law_t *law,
	uint32_t portal_index, const sg_rune_cx_portal_t *portal,
	uint32_t source_cell, uint8_t source_stances)
{
	const sg_rune_cx_facet_t *facet = &cx->facets[portal->facet];
	const sg_rune_cx_cell_t *source = &cx->cells[source_cell];
	flight_launch_t launches[4];
	uint32_t launch_count = 0U, index;
	float foot[3], origin[3], direction[3], normal[3], length;
	float floor_z = (float)source->bounds.mins.value[2] /
		(float)SG_RUNE_CX_Q8_ONE;

	PortalFoot(cx, portal, foot);
	/* Launch at the foot, at the floor's origin height, running toward it
	 * from the cell's middle; if the foot is the middle, run out along the
	 * facet's outward normal. */
	origin[0] = foot[0];
	origin[1] = foot[1];
	origin[2] = floor_z;
	direction[0] = foot[0] - (float)((double)source->bounds.mins.value[0] +
		(double)source->bounds.maxs.value[0]) / (2.0f * (float)SG_RUNE_CX_Q8_ONE);
	direction[1] = foot[1] - (float)((double)source->bounds.mins.value[1] +
		(double)source->bounds.maxs.value[1]) / (2.0f * (float)SG_RUNE_CX_Q8_ONE);
	length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
	normal[0] = FloatBits(facet->plane.normal_bits[0]);
	normal[1] = FloatBits(facet->plane.normal_bits[1]);
	if (cx->incidences[portal->source_incidence].side ==
		SG_RUNE_CX_POSITIVE_SIDE)
	{
		normal[0] = -normal[0];
		normal[1] = -normal[1];
	}
	if (length < 1.0f)
	{
		length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1]);
		direction[0] = normal[0];
		direction[1] = normal[1];
	}
	if (length < 1e-3f)
		return 1;
	direction[0] /= length;
	direction[1] /= length;
	/* A drop at a run and a drop at half a run: a walkway below a ledge
	 * is reached by the short arc where the long one overshoots it. */
	launches[launch_count].kind = SG_RUNE_MOVE_DROP;
	launches[launch_count].vertical = 0.0f;
	launches[launch_count].lead_seconds = 0.0f;
	launches[launch_count].rise_before = 0.0f;
	launches[launch_count].run = 1.0f;
	launch_count++;
	launches[launch_count].kind = SG_RUNE_MOVE_DROP;
	launches[launch_count].vertical = 0.0f;
	launches[launch_count].lead_seconds = 0.0f;
	launches[launch_count].rise_before = 0.0f;
	launches[launch_count].run = 0.5f;
	launch_count++;
	launches[launch_count].kind = SG_RUNE_MOVE_JUMP;
	launches[launch_count].vertical = SG_RuneMoveJumpVelocity(store);
	launches[launch_count].lead_seconds = 0.0f;
	launches[launch_count].rise_before = 0.0f;
	launches[launch_count].run = 1.0f;
	launch_count++;
	if (SG_RuneMoveRocketVelocity(store) > 0.0f)
	{
		launches[launch_count].kind = SG_RUNE_MOVE_ROCKET_JUMP;
		launches[launch_count].vertical = SG_RuneMoveRocketVelocity(store);
		launches[launch_count].lead_seconds = SG_RuneMoveRocketLead(store);
		launches[launch_count].rise_before = store->rocket_pre_blast_rise;
		launches[launch_count].run = 1.0f;
		launch_count++;
	}
	for (index = 0U; index < launch_count; index++)
	{
		const flight_launch_t *launch = &launches[index];
		float velocity[3], start[3];
		sg_rune_flight_t flight;
		const sg_rune_cx_cell_t *landing;

		velocity[0] = direction[0] * store->law.max_velocity * launch->run;
		velocity[1] = direction[1] * store->law.max_velocity * launch->run;
		velocity[2] = launch->vertical;
		start[0] = origin[0];
		start[1] = origin[1];
		start[2] = origin[2] + launch->rise_before;
		/* The arc must land well at the modelled speed and a little slower
		 * and faster: a body never leaves the edge at exactly that speed. */
		if (!SG_RuneFlightLandsRobustly(cx, law, source_cell, start, velocity,
			LAUNCH_TOLERANCE, &flight))
			continue;
		if (flight.landing_cell == source_cell ||
			flight.landing_cell >= cx->cell_count)
			continue;
		/* A flight over in a frame or two is a step, not a flight: the
		 * portal's foot sits a little above the floor beside it and the
		 * contact crossing already goes there. */
		if (flight.seconds < MIN_FLIGHT_SECONDS)
			continue;
		landing = &cx->cells[flight.landing_cell];
		if (!SG_RuneMoveAppendFlight(store, source_cell, portal_index,
			launch->kind, source_stances, Stances(landing->valid_stances),
			flight.landing_cell, velocity, flight.seconds + launch->lead_seconds))
			return 0;
	}
	return 1;
}

int SG_RuneMoveEmitComplex(sg_rune_move_store_t *store,
	const sg_rune_cx_view_t *complex, const sg_rune_law_t *law)
{
	uint32_t index;

	if (!store || !complex || !law)
		return 0;
	for (index = 0U; index < complex->portal_count; index++)
	{
		const sg_rune_cx_portal_t *portal = &complex->portals[index];
		const sg_rune_cx_facet_t *facet;
		const sg_rune_cx_incidence_t *negative;
		const sg_rune_cx_incidence_t *positive;
		const sg_rune_cx_cell_t *source;
		const sg_rune_cx_cell_t *target;
		sg_rune_move_crossing_t crossing;
		int vertical;

		if (portal->facet >= complex->facet_count ||
			portal->source_incidence >= complex->incidence_count ||
			portal->destination_incidence >= complex->incidence_count)
			return 0;
		facet = &complex->facets[portal->facet];
		negative = &complex->incidences[portal->source_incidence];
		positive = &complex->incidences[portal->destination_incidence];
		if (negative->cell >= complex->cell_count ||
			positive->cell >= complex->cell_count)
			return 0;
		source = &complex->cells[negative->cell];
		target = &complex->cells[positive->cell];
		/* Nothing is routed into lava or slime; a body already in it may
		 * still climb out, so its own departures stand. */
		if (target->semantics & SG_RUNE_CX_CELL_HAZARD)
			continue;
		vertical = fabsf(FloatBits(facet->plane.normal_bits[2])) < 0.70710678f;
		memset(&crossing, 0, sizeof(crossing));
		crossing.cell = negative->cell;
		crossing.other_cell = positive->cell;
		crossing.portal = index;
		crossing.cell_stances = Stances(source->valid_stances);
		crossing.other_stances = Stances(target->valid_stances);
		crossing.portal_stances = Stances(portal->valid_stances);
		crossing.source_supported =
			(source->semantics & SG_RUNE_CX_CELL_SUPPORTED) != 0U;
		crossing.target_supported =
			(target->semantics & SG_RUNE_CX_CELL_SUPPORTED) != 0U;
		crossing.source_water = (source->semantics & SG_RUNE_CX_CELL_WATER) != 0U;
		crossing.target_water = (target->semantics & SG_RUNE_CX_CELL_WATER) != 0U;
		crossing.vertical_facet = vertical;
		crossing.floor_delta = (float)(target->bounds.mins.value[2] -
			source->bounds.mins.value[2]) / (float)SG_RUNE_CX_Q8_ONE;
		if (!SG_RuneMoveEmitCrossing(store, &crossing))
			return 0;
		/* Off a floor into the air through a wall: trace the flights. */
		if (crossing.source_supported && !crossing.target_supported &&
			!crossing.source_water && !crossing.target_water && vertical &&
			(crossing.cell_stances & crossing.portal_stances) != 0U &&
			!EmitFlights(store, complex, law, index, portal, negative->cell,
				(uint8_t)(crossing.cell_stances & crossing.portal_stances)))
			return 0;
	}
	return 1;
}
