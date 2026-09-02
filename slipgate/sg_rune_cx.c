#include "sg_rune_cx.h"

#include <stddef.h>

static int SpanInside(const sg_rune_cx_span_t *span, uint32_t count)
{
	return span->first <= count && span->count <= count - span->first;
}

static int IndexInside(uint32_t index, uint32_t count, int optional)
{
	return index < count || (optional && index == SG_RUNE_CX_INDEX_NONE);
}

int SG_RuneCxViewValid(const sg_rune_cx_view_t *view)
{
	uint32_t index;

	if (!view || (view->cell_count && !view->cells) ||
		(view->facet_count && !view->facets) ||
		(view->incidence_count && !view->incidences) ||
		(view->cell_incidence_count && !view->cell_incidences) ||
		(view->vertex_count && !view->vertices) ||
		(view->portal_count && !view->portals) ||
		(view->surface_count && !view->surfaces) ||
		(view->surface_vertex_count && !view->surface_vertices))
		return 0;
	for (index = 0U; index < view->cell_count; index++)
	{
		const sg_rune_cx_cell_t *cell = &view->cells[index];
		uint32_t slot;

		if (!SpanInside(&cell->incidences, view->cell_incidence_count) ||
			cell->valid_stances == 0U ||
			(cell->valid_stances & ~SG_RUNE_CX_STANCE_ALL) != 0U)
			return 0;
		for (slot = 0U; slot < cell->incidences.count; slot++)
		{
			uint32_t incidence =
				view->cell_incidences[cell->incidences.first + slot];

			if (incidence >= view->incidence_count ||
				view->incidences[incidence].cell != index)
				return 0;
		}
	}
	for (index = 0U; index < view->facet_count; index++)
	{
		const sg_rune_cx_facet_t *facet = &view->facets[index];

		if (facet->source.kind >= SG_RUNE_CX_SOURCE_KIND_COUNT ||
			!SpanInside(&facet->vertices, view->vertex_count) ||
			!SpanInside(&facet->incidences, view->incidence_count) ||
			!IndexInside(facet->portal, view->portal_count, 1) ||
			facet->kind >= SG_RUNE_CX_FACET_KIND_COUNT ||
			(facet->kind == SG_RUNE_CX_FACET_POLYGON &&
				facet->vertices.count < 3U))
			return 0;
		if (facet->portal != SG_RUNE_CX_INDEX_NONE &&
			view->portals[facet->portal].facet != index)
			return 0;
	}
	for (index = 0U; index < view->incidence_count; index++)
	{
		const sg_rune_cx_incidence_t *incidence = &view->incidences[index];

		if (incidence->cell >= view->cell_count ||
			incidence->facet >= view->facet_count ||
			incidence->side >= SG_RUNE_CX_SIDE_COUNT ||
			incidence->boundary >= SG_RUNE_CX_BOUNDARY_COUNT)
			return 0;
	}
	for (index = 0U; index < view->portal_count; index++)
	{
		const sg_rune_cx_portal_t *portal = &view->portals[index];
		const sg_rune_cx_incidence_t *negative, *positive;

		if (portal->facet >= view->facet_count ||
			portal->negative_incidence >= view->incidence_count ||
			portal->positive_incidence >= view->incidence_count ||
			portal->direction >= SG_RUNE_CX_CONTINUITY_COUNT ||
			portal->valid_stances == 0U ||
			(portal->valid_stances & ~SG_RUNE_CX_STANCE_ALL) != 0U)
			return 0;
		negative = &view->incidences[portal->negative_incidence];
		positive = &view->incidences[portal->positive_incidence];
		if (negative->facet != portal->facet ||
			positive->facet != portal->facet ||
			negative->side != SG_RUNE_CX_NEGATIVE_SIDE ||
			positive->side != SG_RUNE_CX_POSITIVE_SIDE ||
			negative->cell == positive->cell)
			return 0;
	}
	for (index = 0U; index < view->surface_count; index++)
	{
		const sg_rune_cx_surface_t *surface = &view->surfaces[index];

		if (surface->frame >= SG_RUNE_CX_SURFACE_FRAME_COUNT ||
			!IndexInside(surface->cell, view->cell_count, 1) ||
			!SpanInside(&surface->vertices, view->surface_vertex_count) ||
			surface->vertices.count < 3U)
			return 0;
	}
	return 1;
}
