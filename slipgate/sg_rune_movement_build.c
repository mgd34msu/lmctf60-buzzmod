/* Movement capabilities from the cell complex: one call per directed
 * portal, with the crossing facts read from what the complex carries. */
#include "sg_rune_movement.h"

#include <math.h>
#include <string.h>

#include "sg_rune_cx.h"

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

int SG_RuneMoveEmitComplex(sg_rune_move_store_t *store,
	const sg_rune_cx_view_t *complex)
{
	uint32_t index;

	if (!store || !complex)
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
		crossing.vertical_facet =
			fabsf(FloatBits(facet->plane.normal_bits[2])) < 0.70710678f;
		crossing.floor_delta = (float)(target->bounds.mins.value[2] -
			source->bounds.mins.value[2]) / (float)SG_RUNE_CX_Q8_ONE;
		if (!SG_RuneMoveEmitCrossing(store, &crossing))
			return 0;
	}
	return 1;
}
