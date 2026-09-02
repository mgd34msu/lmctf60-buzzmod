#include "sg_rune_flight.h"

#include <math.h>
#include <string.h>

/* A flight that has crossed this many cells or flown this long is lost:
 * nothing a body launches lands usefully beyond it. */
#define INSIDE_TOLERANCE 0.25f
#define TIE_TOLERANCE 1e-4f
#define MAX_CROSSINGS 96U
#define MAX_SECONDS 8.0f
#define OVERCLIP 1.001f
#define TIME_EPSILON 1e-4f
#define FLOOR_NORMAL 0.7f

static float FloatBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* The facet's plane oriented outward from the cell on the given side. */
static void OutwardPlane(const sg_rune_cx_facet_t *facet, uint32_t side,
	float normal[3], float *distance)
{
	normal[0] = FloatBits(facet->plane.normal_bits[0]);
	normal[1] = FloatBits(facet->plane.normal_bits[1]);
	normal[2] = FloatBits(facet->plane.normal_bits[2]);
	*distance = FloatBits(facet->plane.distance_bits);
	if (side == SG_RUNE_CX_POSITIVE_SIDE)
	{
		normal[0] = -normal[0];
		normal[1] = -normal[1];
		normal[2] = -normal[2];
		*distance = -*distance;
	}
}

/* Earliest t >= 0 at which a t^2 + b t + c is positive and increasing:
 * the moment the body leaves the half-space.  A body already on the plane
 * and moving out leaves now.  INFINITY when it never leaves. */
static float ExitTime(float a, float b, float c)
{
	float best = INFINITY;

	if (c > 0.0f)
		return 0.0f;
	if (fabsf(a) < 1e-9f)
	{
		if (b > 0.0f)
		{
			float t = -c / b;

			return t > 0.0f ? t : 0.0f;
		}
		return INFINITY;
	}
	{
		float discriminant = b * b - 4.0f * a * c;
		float roots[2];
		uint32_t index;

		if (discriminant < 0.0f)
			return INFINITY;
		discriminant = sqrtf(discriminant);
		roots[0] = (-b - discriminant) / (2.0f * a);
		roots[1] = (-b + discriminant) / (2.0f * a);
		for (index = 0U; index < 2U; index++)
		{
			float t = roots[index];
			float slope;

			if (!(t >= 0.0f) || t >= best)
				continue;
			slope = 2.0f * a * t + b;
			if (slope > 0.0f)
				best = t;
		}
	}
	return best;
}

/* Whether a point is inside a cell (within a small tolerance): cells are
 * convex, so every facet plane's inner side is the test. */
static int Inside(const sg_rune_cx_view_t *cx, uint32_t cell, const float point[3])
{
	const sg_rune_cx_cell_t *record = &cx->cells[cell];
	uint32_t slot;

	for (slot = 0U; slot < record->incidences.count; slot++)
	{
		const sg_rune_cx_incidence_t *incidence = &cx->incidences[
			cx->cell_incidences[record->incidences.first + slot]];
		float normal[3], distance;

		OutwardPlane(&cx->facets[incidence->facet], incidence->side, normal, &distance);
		if (normal[0] * point[0] + normal[1] * point[1] + normal[2] * point[2] -
			distance > INSIDE_TOLERANCE)
			return 0;
	}
	return 1;
}

static uint32_t Neighbor(const sg_rune_cx_view_t *cx,
	const sg_rune_cx_facet_t *facet, uint32_t cell)
{
	uint32_t slot;

	for (slot = 0U; slot < facet->incidences.count; slot++)
	{
		const sg_rune_cx_incidence_t *incidence =
			&cx->incidences[facet->incidences.first + slot];

		if (incidence->cell != cell)
			return incidence->cell;
	}
	return SG_RUNE_CX_INDEX_NONE;
}

int SG_RuneFlightTrace(const sg_rune_cx_view_t *cx,
	const sg_rune_law_t *law, uint32_t start_cell, const float origin[3],
	const float velocity[3], sg_rune_flight_t *flight_out)
{
	float position[3], speed[3], gravity;
	uint32_t cell = start_cell;
	uint32_t skip_facet = SG_RUNE_CX_INDEX_NONE;
	float elapsed = 0.0f;
	uint32_t axis;

	if (!flight_out)
		return 0;
	memset(flight_out, 0, sizeof(*flight_out));
	flight_out->outcome = SG_RUNE_FLIGHT_INVALID;
	flight_out->landing_cell = SG_RUNE_CX_INDEX_NONE;
	if (!cx || !law || !origin || !velocity || start_cell >= cx->cell_count)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		position[axis] = origin[axis];
		speed[axis] = velocity[axis];
		if (!isfinite(position[axis]) || !isfinite(speed[axis]))
			return 0;
	}
	gravity = law->gravity > 0.0f ? law->gravity : 800.0f;
	/* Gravity is applied before each substep's move, so the boundary
	 * trajectory carries a half-substep term in the vertical velocity. */
	speed[2] -= gravity * (float)law->substep_ms / 2000.0f;
	while (flight_out->crossings < MAX_CROSSINGS && elapsed < MAX_SECONDS)
	{
		const sg_rune_cx_cell_t *record = &cx->cells[cell];
		uint32_t slot, exit_facet = SG_RUNE_CX_INDEX_NONE, exit_side = 0U;
		float exit_time = INFINITY, exit_normal[3] = { 0.0f, 0.0f, 0.0f };

		if (record->semantics & SG_RUNE_CX_CELL_HAZARD)
		{
			flight_out->outcome = SG_RUNE_FLIGHT_HARM;
			break;
		}
		if (record->semantics & SG_RUNE_CX_CELL_WATER)
		{
			flight_out->outcome = SG_RUNE_FLIGHT_WATER;
			break;
		}
		for (slot = 0U; slot < record->incidences.count; slot++)
		{
			const sg_rune_cx_incidence_t *incidence = &cx->incidences[
				cx->cell_incidences[record->incidences.first + slot]];
			const sg_rune_cx_facet_t *facet = &cx->facets[incidence->facet];
			float normal[3], distance, a, b, c, t;

			if (incidence->facet == skip_facet)
				continue;
			OutwardPlane(facet, incidence->side, normal, &distance);
			c = normal[0] * position[0] + normal[1] * position[1] +
				normal[2] * position[2] - distance;
			b = normal[0] * speed[0] + normal[1] * speed[1] +
				normal[2] * speed[2];
			a = -normal[2] * gravity * 0.5f;
			t = ExitTime(a, b, c);
			if (t < exit_time)
			{
				exit_time = t;
				exit_facet = incidence->facet;
				exit_side = incidence->side;
				memcpy(exit_normal, normal, sizeof(exit_normal));
			}
		}
		if (!(exit_time < INFINITY))
		{
			flight_out->outcome = SG_RUNE_FLIGHT_LOST;
			break;
		}
		if (exit_time < TIME_EPSILON)
			exit_time = TIME_EPSILON;
		/* Advance to the exit. */
		position[0] += speed[0] * exit_time;
		position[1] += speed[1] * exit_time;
		position[2] += speed[2] * exit_time - 0.5f * gravity * exit_time *
			exit_time;
		speed[2] -= gravity * exit_time;
		elapsed += exit_time;
		{
			const sg_rune_cx_facet_t *facet = &cx->facets[exit_facet];
			uint32_t next = Neighbor(cx, facet, cell);

			/* Several facets share the exit plane (the face and the portals
			 * on it): the exit goes through the portal whose cell beyond
			 * holds the point; otherwise it is the face, a wall. */
			if (next == SG_RUNE_CX_INDEX_NONE || !Inside(cx, next, position))
			{
				uint32_t other;

				next = SG_RUNE_CX_INDEX_NONE;
				for (other = 0U; other < record->incidences.count; other++)
				{
					const sg_rune_cx_incidence_t *incidence = &cx->incidences[
						cx->cell_incidences[record->incidences.first + other]];
					const sg_rune_cx_facet_t *candidate = &cx->facets[incidence->facet];
					float normal[3], distance, c, b, a, t;
					uint32_t beyond;

					if (incidence->facet == exit_facet || incidence->facet == skip_facet)
						continue;
					OutwardPlane(candidate, incidence->side, normal, &distance);
					if (fabsf(normal[0] - exit_normal[0]) > 1e-4f ||
						fabsf(normal[1] - exit_normal[1]) > 1e-4f ||
						fabsf(normal[2] - exit_normal[2]) > 1e-4f)
						continue;
					c = normal[0] * position[0] + normal[1] * position[1] +
						normal[2] * position[2] - distance;
					if (fabsf(c) > INSIDE_TOLERANCE)
						continue;
					(void)b; (void)a; (void)t;
					beyond = Neighbor(cx, candidate, cell);
					if (beyond != SG_RUNE_CX_INDEX_NONE && Inside(cx, beyond, position))
					{
						exit_facet = incidence->facet;
						facet = candidate;
						next = beyond;
						break;
					}
				}
			}

			(void)exit_side;
			if (next != SG_RUNE_CX_INDEX_NONE)
			{
				cell = next;
				skip_facet = exit_facet;
				flight_out->crossings++;
				continue;
			}
			/* Solid.  A floor under the body ends the flight; anything else
			 * clips the velocity and the flight goes on inside the cell. */
			if (exit_normal[2] < -FLOOR_NORMAL)
			{
				flight_out->outcome = SG_RUNE_FLIGHT_LANDED;
				break;
			}
			{
				float into = speed[0] * exit_normal[0] +
					speed[1] * exit_normal[1] + speed[2] * exit_normal[2];

				if (into > 0.0f)
				{
					speed[0] -= exit_normal[0] * into * OVERCLIP;
					speed[1] -= exit_normal[1] * into * OVERCLIP;
					speed[2] -= exit_normal[2] * into * OVERCLIP;
				}
			}
			skip_facet = exit_facet;
			flight_out->crossings++;
		}
	}
	if (flight_out->outcome == SG_RUNE_FLIGHT_INVALID)
		flight_out->outcome = SG_RUNE_FLIGHT_LOST;
	flight_out->landing_cell = cell;
	flight_out->seconds = elapsed;
	memcpy(flight_out->landing, position, sizeof(position));
	memcpy(flight_out->landing_velocity, speed, sizeof(speed));
	return 1;
}

static int LandsWell(const sg_rune_cx_view_t *cx, const sg_rune_flight_t *flight)
{
	if (flight->outcome == SG_RUNE_FLIGHT_WATER)
		return 1;
	if (flight->outcome != SG_RUNE_FLIGHT_LANDED || flight->landing_cell >= cx->cell_count)
		return 0;
	return (cx->cells[flight->landing_cell].semantics & SG_RUNE_CX_CELL_SUPPORTED) &&
		!(cx->cells[flight->landing_cell].semantics & SG_RUNE_CX_CELL_HAZARD);
}

int SG_RuneFlightLandsRobustly(const sg_rune_cx_view_t *cx,
	const sg_rune_law_t *law, uint32_t start_cell, const float origin[3],
	const float velocity[3], float tolerance, sg_rune_flight_t *flight_out)
{
	static const float signs[2] = { -1.0f, 1.0f };
	sg_rune_flight_t nominal, other;
	uint32_t index;

	if (!SG_RuneFlightTrace(cx, law, start_cell, origin, velocity, &nominal))
		return 0;
	if (flight_out)
		*flight_out = nominal;
	if (!LandsWell(cx, &nominal))
		return 0;
	for (index = 0U; index < 2U && tolerance > 0.0f; index++)
	{
		float scaled[3];

		scaled[0] = velocity[0] * (1.0f + signs[index] * tolerance);
		scaled[1] = velocity[1] * (1.0f + signs[index] * tolerance);
		scaled[2] = velocity[2];
		if (!SG_RuneFlightTrace(cx, law, start_cell, origin, scaled, &other) ||
			!LandsWell(cx, &other))
			return 0;
	}
	return 1;
}
