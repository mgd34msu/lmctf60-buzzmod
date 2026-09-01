#ifndef SG_RUNE_COMPACT_RESPONSE_PARTITION_OWNER_H
#define SG_RUNE_COMPACT_RESPONSE_PARTITION_OWNER_H

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_response_partition.h"

int SG_RuneCompactResponsePartitionOwnerBuild(
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_response_allocator_t *allocator,
	sg_rune_compact_response_partition_t **partition_out,
	sg_rune_compact_response_error_t *error_out);

#endif
