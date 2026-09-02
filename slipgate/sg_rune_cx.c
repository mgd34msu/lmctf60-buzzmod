#include "sg_rune_cx.h"

#include <stddef.h>
#include <string.h>

static int SpanInside(const sg_rune_cx_span_t *span, uint32_t count)
{
	return span->first <= count && span->count <= count - span->first;
}

static int IndexInside(uint32_t index, uint32_t count, int optional)
{
	return index < count || (optional && index == SG_RUNE_CX_INDEX_NONE);
}

static int Fault(sg_rune_fault_t *fault_out, const char *array,
	uint32_t record, const char *reason)
{
	if (fault_out)
	{
		fault_out->array = array;
		fault_out->record = record;
		fault_out->reason = reason;
	}
	return 0;
}

int SG_RuneCxViewValid(const sg_rune_cx_view_t *view,
	sg_rune_fault_t *fault_out)
{
	uint32_t index;

	if (fault_out)
		memset(fault_out, 0, sizeof(*fault_out));
	if (!view || (view->cell_count && !view->cells) ||
		(view->facet_count && !view->facets) ||
		(view->incidence_count && !view->incidences) ||
		(view->cell_incidence_count && !view->cell_incidences) ||
		(view->vertex_count && !view->vertices) ||
		(view->portal_count && !view->portals) ||
		(view->surface_count && !view->surfaces) ||
		(view->surface_vertex_count && !view->surface_vertices))
		return Fault(fault_out, "view", 0U, "missing array");
	for (index = 0U; index < view->cell_count; index++)
	{
		const sg_rune_cx_cell_t *cell = &view->cells[index];
		uint32_t slot;

		if (!SpanInside(&cell->incidences, view->cell_incidence_count))
			return Fault(fault_out, "cells", index, "incidence span");
		if (cell->valid_stances == 0U ||
			(cell->valid_stances & ~SG_RUNE_CX_STANCE_ALL) != 0U)
			return Fault(fault_out, "cells", index, "stances");
		for (slot = 0U; slot < cell->incidences.count; slot++)
		{
			uint32_t incidence =
				view->cell_incidences[cell->incidences.first + slot];

			if (incidence >= view->incidence_count)
				return Fault(fault_out, "cells", index, "incidence index");
			if (view->incidences[incidence].cell != index)
				return Fault(fault_out, "cells", index, "incidence cell");
		}
	}
	for (index = 0U; index < view->facet_count; index++)
	{
		const sg_rune_cx_facet_t *facet = &view->facets[index];

		if (facet->source.kind >= SG_RUNE_CX_SOURCE_KIND_COUNT)
			return Fault(fault_out, "facets", index, "source kind");
		if (view->vertex_count == 0U ? (facet->vertices.first != 0U ||
			facet->vertices.count != 0U) :
			!SpanInside(&facet->vertices, view->vertex_count))
			return Fault(fault_out, "facets", index, "vertex span");
		if (!SpanInside(&facet->incidences, view->incidence_count))
			return Fault(fault_out, "facets", index, "incidence span");
		if (!IndexInside(facet->portal, view->portal_count, 1))
			return Fault(fault_out, "facets", index, "portal index");
		if (facet->kind >= SG_RUNE_CX_FACET_KIND_COUNT)
			return Fault(fault_out, "facets", index, "kind");
		if (facet->kind == SG_RUNE_CX_FACET_POLYGON && view->vertex_count != 0U &&
			facet->vertices.count < 3U)
			return Fault(fault_out, "facets", index, "polygon vertices");
		if (facet->portal != SG_RUNE_CX_INDEX_NONE &&
			view->portals[facet->portal].facet != index)
			return Fault(fault_out, "facets", index, "portal facet");
	}
	for (index = 0U; index < view->incidence_count; index++)
	{
		const sg_rune_cx_incidence_t *incidence = &view->incidences[index];

		if (incidence->cell >= view->cell_count)
			return Fault(fault_out, "incidences", index, "cell");
		if (incidence->facet >= view->facet_count)
			return Fault(fault_out, "incidences", index, "facet");
		if (incidence->side >= SG_RUNE_CX_SIDE_COUNT)
			return Fault(fault_out, "incidences", index, "side");
		if (incidence->boundary >= SG_RUNE_CX_BOUNDARY_COUNT)
			return Fault(fault_out, "incidences", index, "boundary");
	}
	for (index = 0U; index < view->portal_count; index++)
	{
		const sg_rune_cx_portal_t *portal = &view->portals[index];
		const sg_rune_cx_incidence_t *negative, *positive;   /* source, destination */

		if (portal->facet >= view->facet_count)
			return Fault(fault_out, "portals", index, "facet");
		if (portal->source_incidence >= view->incidence_count ||
			portal->destination_incidence >= view->incidence_count)
			return Fault(fault_out, "portals", index, "incidence index");
		if (portal->valid_stances == 0U ||
			(portal->valid_stances & ~SG_RUNE_CX_STANCE_ALL) != 0U)
			return Fault(fault_out, "portals", index, "stances");
		negative = &view->incidences[portal->source_incidence];
		positive = &view->incidences[portal->destination_incidence];
		if (negative->facet != portal->facet || positive->facet != portal->facet)
			return Fault(fault_out, "portals", index, "incidence facet");
		if (negative->side == positive->side)
			return Fault(fault_out, "portals", index, "same side");
		if (negative->cell == positive->cell)
			return Fault(fault_out, "portals", index, "same cell");
	}
	for (index = 0U; index < view->surface_count; index++)
	{
		const sg_rune_cx_surface_t *surface = &view->surfaces[index];

		if (surface->frame >= SG_RUNE_CX_SURFACE_FRAME_COUNT)
			return Fault(fault_out, "surfaces", index, "frame");
		if (!IndexInside(surface->cell, view->cell_count, 1))
			return Fault(fault_out, "surfaces", index, "cell");
		if (!SpanInside(&surface->vertices, view->surface_vertex_count) ||
			surface->vertices.count < 3U)
			return Fault(fault_out, "surfaces", index, "vertex span");
	}
	return 1;
}
