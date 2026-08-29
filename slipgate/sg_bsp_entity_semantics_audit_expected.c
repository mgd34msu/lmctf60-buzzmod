#include "sg_bsp_entity_semantics_audit_internal.h"

#include <stdint.h>
#include <string.h>

/* The schema id is deliberately independent from the RUNE model schema.  A
 * change to the entity projection must invalidate every publication even
 * when the surrounding RUNE wire format is unchanged. */
const sg_rune_v2_content_id_t SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID = {
	{
		0x53U, 0x47U, 0x2dU, 0x42U, 0x53U, 0x50U, 0x2dU, 0x45U,
		0x4eU, 0x54U, 0x49U, 0x54U, 0x59U, 0x2dU, 0x53U, 0x45U,
		0x4dU, 0x41U, 0x4eU, 0x54U, 0x49U, 0x43U, 0x53U, 0x2dU,
		0x56U, 0x31U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U
	}
};

static void ResetResult(sg_bsp_entity_semantics_audit_result_t *result)
{
	if (!result)
		return;
	memset(result, 0, sizeof(*result));
	result->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OK;
	result->domain = SG_BSP_ENTITY_SEMANTICS_FACT_NONE;
	result->record = UINT32_MAX;
}

static int BindingValid(const sg_bsp_entity_semantics_binding_t *binding)
{
	return binding && SG_RuneV2ContentIdValid(&binding->source_identity) &&
		binding->source_set_identity != 0U &&
		binding->source_set_identity != UINT64_MAX &&
		SG_RuneV2ContentIdEqual(&binding->schema_identity,
			&SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID);
}

static int WorldSourceValid(const sg_bsp_world_t *world)
{
	return world && world->entities && world->entity_byte_count != 0U &&
		world->models && world->model_count != 0U;
}

static void CountExpected(const sg_bsp_entity_semantics_t *expected,
	sg_bsp_entity_semantics_audit_result_t *result)
{
	uint32_t index;

	result->expected_entities = expected->entity_count;
	result->expected_edges = expected->edge_count;
	for (index = 0U; index < expected->entity_count; index++)
	{
		if (expected->entities[index].flags & SG_BSP_ENTITY_HAS_LANDMARK &&
			result->expected_landmarks != UINT32_MAX)
			result->expected_landmarks++;
		if (expected->entities[index].flags & SG_BSP_ENTITY_HAS_MECHANISM &&
			result->expected_mechanisms != UINT32_MAX)
			result->expected_mechanisms++;
	}
	result->completeness = expected->entity_count == 0U &&
		expected->edge_count == 0U && expected->world.flags == 0U
		? SG_BSP_ENTITY_SEMANTICS_PROVEN_EMPTY
		: SG_BSP_ENTITY_SEMANTICS_COMPLETE;
}

int SG_BspEntitySemanticsAuditOwned(const sg_bsp_world_t *world,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_t **owned_out,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	sg_bsp_entity_semantics_error_t error;
	sg_bsp_entity_semantics_t *expected = NULL;

	ResetResult(result_out);
	if (!world || !binding || !candidate || !owned_out || *owned_out ||
		!result_out)
	{
		if (result_out)
			result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	if (!BindingValid(binding))
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH;
		result_out->domain = SG_BSP_ENTITY_SEMANTICS_FACT_IDENTITY;
		return 0;
	}
	if (!WorldSourceValid(world))
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_SOURCE;
		result_out->domain = SG_BSP_ENTITY_SEMANTICS_FACT_WORLD;
		return 0;
	}
	if (candidate->source_set_identity != binding->source_set_identity ||
		candidate->world.source_set_identity != binding->source_set_identity)
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH;
		result_out->domain = SG_BSP_ENTITY_SEMANTICS_FACT_IDENTITY;
		return 0;
	}
	if (!SG_BspEntitySemanticsBuild(world, binding->source_set_identity,
		&expected, &error))
	{
		result_out->code = error.code ==
			SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY
			? SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY
			: SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_SOURCE;
		result_out->domain = SG_BSP_ENTITY_SEMANTICS_FACT_WORLD;
		return 0;
	}
	CountExpected(expected, result_out);
	*owned_out = expected;
	return 1;
}
