/* sg_rune_runtime.c -- native rune queries. */
#include "../q_shared.h"
#include "sg_rune.h"

#include <string.h>

static int Runtime_FloatBitsEqual(float left, float right)
{
	return memcmp(&left, &right, sizeof(left)) == 0;
}

qboolean SG_RunePublishedShapeValid(const rune_t *rune)
{
	if (!rune || !rune->seeds || !rune->first_link ||
	    !rune->linked_seed ||
	    rune->artifact.magic != RUNE_ARTIFACT_MAGIC ||
	    rune->artifact.header_crc32 == 0U ||
	    rune->hdr.magic != (int)RUNE_ARTIFACT_MAGIC ||
	    rune->hdr.num_seeds <= 0 || rune->hdr.num_seeds > RUNE_MAX_SEEDS ||
	    rune->hdr.num_links < 0 || rune->hdr.num_links > RUNE_MAX_LINKS ||
	    (rune->hdr.num_links > 0 && (!rune->links || !rune->next_link)) ||
	    (uint32_t)rune->hdr.num_seeds != rune->artifact.num_seeds ||
	    (uint32_t)rune->hdr.num_links != rune->artifact.num_links ||
	    rune->artifact.num_mechanism_nodes > RUNE_MAX_MECHANISM_NODES ||
	    rune->artifact.num_mechanism_edges > RUNE_MAX_MECHANISM_EDGES ||
	    rune->artifact.num_inventory_edges >
	        rune->artifact.num_mechanism_edges ||
	    rune->artifact.num_mechanism_plans > RUNE_MAX_MECHANISM_PLANS ||
	    rune->artifact.string_bytes == 0U ||
	    rune->artifact.string_bytes > RUNE_MAX_MECHANISM_STRING_BYTES ||
	    (rune->artifact.num_mechanism_nodes != 0U &&
	     !rune->mechanism_nodes) ||
	    (rune->artifact.num_mechanism_edges != 0U &&
	     !rune->mechanism_edges) ||
	    (rune->artifact.num_mechanism_plans != 0U &&
	     !rune->mechanism_plans) || !rune->mechanism_strings ||
	    memcmp(rune->hdr.mapname, rune->artifact.identity.map_name,
	        sizeof(rune->hdr.mapname)) != 0)
		return false;
	return true;
}

int SG_RuneArtifactsEqual(const rune_artifact_t *left,
	const rune_artifact_t *right)
{
	return left && right &&
	       left->magic == right->magic &&
	       left->payload_crc32 == right->payload_crc32 &&
	       left->header_crc32 == right->header_crc32 &&
	       left->action_contract_crc32 == right->action_contract_crc32 &&
	       left->mechanism_contract_crc32 ==
	           right->mechanism_contract_crc32 &&
	       left->num_seeds == right->num_seeds &&
	       left->num_links == right->num_links &&
	       left->num_mechanism_nodes == right->num_mechanism_nodes &&
	       left->num_mechanism_edges == right->num_mechanism_edges &&
	       left->num_inventory_edges == right->num_inventory_edges &&
	       left->num_mechanism_plans == right->num_mechanism_plans &&
	       left->string_bytes == right->string_bytes &&
	       left->identity.bsp_checksum == right->identity.bsp_checksum &&
	       left->identity.entity_crc32 == right->identity.entity_crc32 &&
	       left->identity.physics_flags == right->identity.physics_flags &&
	       Runtime_FloatBitsEqual(left->identity.gravity,
	           right->identity.gravity) &&
	       Runtime_FloatBitsEqual(left->identity.airaccelerate,
	           right->identity.airaccelerate) &&
	       Runtime_FloatBitsEqual(left->identity.maxvelocity,
	           right->identity.maxvelocity) &&
	       left->identity.pmove_substep_ms ==
	           right->identity.pmove_substep_ms &&
	       left->identity.server_frame_ms ==
	           right->identity.server_frame_ms &&
	       left->identity.host_physics_id ==
	           right->identity.host_physics_id &&
	       memcmp(left->identity.map_name, right->identity.map_name,
	           sizeof(left->identity.map_name)) == 0;
}

const rune_artifact_t *SG_RuneArtifact(const rune_t *rune)
{
	return rune && SG_RunePublishedShapeValid(rune) ? &rune->artifact : NULL;
}

const rune_mechanism_node_t *SG_RuneMechanismNodeByKey(
	const rune_t *rune, uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high;

	if (!rune || !SG_RunePublishedShapeValid(rune))
		return NULL;
	high = rune->artifact.num_mechanism_nodes;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (rune->mechanism_nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= rune->artifact.num_mechanism_nodes ||
	    rune->mechanism_nodes[low].key != key)
		return NULL;
	return &rune->mechanism_nodes[low];
}

const rune_mechanism_plan_t *SG_RuneMechanismPlanForLink(
	const rune_t *rune, uint32_t link_index)
{
	const rune_link_t *link;

	if (!rune || !SG_RunePublishedShapeValid(rune) ||
	    link_index >= rune->artifact.num_links)
		return NULL;
	link = &rune->links[link_index];
	if (link->mechanism_plan == RUNE_NO_MECHANISM_PLAN ||
	    link->mechanism_plan >= rune->artifact.num_mechanism_plans)
		return NULL;
	return &rune->mechanism_plans[link->mechanism_plan];
}

const char *SG_RuneMechanismStringAt(const rune_t *rune, uint32_t offset)
{
	uint32_t cursor;

	if (!rune || !SG_RunePublishedShapeValid(rune) ||
	    offset >= rune->artifact.string_bytes ||
	    (offset != 0U &&
	     (rune->mechanism_strings[offset] == 0U ||
	      rune->mechanism_strings[offset - 1U] != 0U)))
		return NULL;
	for (cursor = offset; cursor < rune->artifact.string_bytes; cursor++)
		if (rune->mechanism_strings[cursor] == 0U)
			return (const char *)rune->mechanism_strings + offset;
	return NULL;
}
