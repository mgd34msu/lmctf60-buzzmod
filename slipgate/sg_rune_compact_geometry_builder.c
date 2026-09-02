/* The builder-facing entry to era-4 compact geometry. */
#include "sg_rune_compact_geometry.h"

#include <string.h>

#include "sg_rune_compact_builder_owner.h"

int SG_RuneCompactGeometryMaterialize(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out)
{
	sg_rune_compact_builder_owner_view_t owner;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!geometry_out)
		return 0;
	*geometry_out = NULL;
	if (!builder || !SG_RuneCompactBuilderOwnerRead(builder, &owner))
	{
		if (error_out)
			error_out->code = SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	return SG_RuneCompactGeometryFromSpace(owner.world, owner.configuration,
		owner.semantics, &owner.identity, allocator, geometry_out, error_out);
}
