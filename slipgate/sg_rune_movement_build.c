/* Movement capabilities from compact geometry and the regions: one call per
 * directed portal, with the crossing facts read from what the complex
 * already carries. */
#include "sg_rune_movement.h"

#include <math.h>
#include <string.h>

#include "sg_configuration_semantics.h"
#include "sg_rune_compact_geometry.h"

static float FloatBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return isfinite(value) ? value : 0.0f;
}

static uint8_t Stances(sg_rune_stance_validity_t validity)
{
	uint8_t stances = 0U;

	if (validity & SG_RUNE_STANCE_VALID_STANDING)
		stances |= SG_RUNE_MOVE_STANDING;
	if (validity & SG_RUNE_STANCE_VALID_CROUCHING)
		stances |= SG_RUNE_MOVE_CROUCHING;
	return stances;
}

int SG_RuneMoveEmitGeometry(sg_rune_move_store_t *store,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_configuration_semantics_t *semantics)
{
	uint32_t index;

	if (!store || !geometry || !semantics ||
		semantics->region_count < geometry->cell_count)
		return 0;
	for (index = 0U; index < geometry->portal_count; index++)
	{
		const sg_rune_compact_portal_t *portal = &geometry->portals[index];
		const sg_rune_compact_facet_t *facet;
		const sg_rune_compact_incidence_t *negative;
		const sg_rune_compact_incidence_t *positive;
		const sg_configuration_semantic_region_t *source_region;
		const sg_configuration_semantic_region_t *target_region;
		sg_rune_move_crossing_t crossing;

		if (portal->facet.value >= geometry->facet_count ||
			portal->negative_incidence.value >= geometry->incidence_count ||
			portal->positive_incidence.value >= geometry->incidence_count)
			return 0;
		facet = &geometry->facets[portal->facet.value];
		negative = &geometry->incidences[portal->negative_incidence.value];
		positive = &geometry->incidences[portal->positive_incidence.value];
		if (negative->cell.value >= geometry->cell_count ||
			positive->cell.value >= geometry->cell_count)
			return 0;
		source_region = &semantics->regions[negative->cell.value];
		target_region = &semantics->regions[positive->cell.value];
		memset(&crossing, 0, sizeof(crossing));
		crossing.cell = negative->cell.value;
		crossing.other_cell = positive->cell.value;
		crossing.portal = index;
		crossing.cell_stances = Stances(
			geometry->cells[negative->cell.value].valid_stances);
		crossing.other_stances = Stances(
			geometry->cells[positive->cell.value].valid_stances);
		crossing.portal_stances = Stances(portal->valid_stances);
		crossing.source_supported = (source_region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;
		crossing.target_supported = (target_region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;
		crossing.source_water = (source_region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_WATER) != 0U;
		crossing.target_water = (target_region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_WATER) != 0U;
		crossing.vertical_facet =
			fabsf(FloatBits(facet->plane.normal_bits[2])) < 0.70710678f;
		crossing.floor_delta = target_region->bounds.mins.value[2] -
			source_region->bounds.mins.value[2];
		if (!SG_RuneMoveEmitCrossing(store, &crossing))
			return 0;
	}
	return 1;
}
