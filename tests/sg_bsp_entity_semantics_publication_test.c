#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_bsp_entity_semantics_publication.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_bsp_world_t world;
	uint8_t *entities;
	sg_bsp_model_t models[4];
} fixture_t;

static sg_bsp_entity_semantics_binding_t binding;

static void InitBinding(void)
{
	uint32_t index;

	memset(&binding, 0, sizeof(binding));
	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		binding.source_identity.bytes[index] = (uint8_t)(index + 1U);
	binding.source_set_identity = UINT64_C(0x534f555243455345);
	binding.schema_identity = SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID;
}

static void InitFixture(fixture_t *fixture, const char *text)
{
	size_t length = strlen(text) + 1U;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->entities = malloc(length);
	CHECK(fixture->entities != NULL);
	if (!fixture->entities)
		return;
	memcpy(fixture->entities, text, length);
	fixture->world.entities = fixture->entities;
	fixture->world.entity_byte_count = (uint32_t)length;
	fixture->world.models = fixture->models;
	fixture->world.model_count = 4U;
	for (index = 0U; index < fixture->world.model_count; index++)
	{
		fixture->models[index].mins.value[0] = -16.0f;
		fixture->models[index].mins.value[1] = -16.0f;
		fixture->models[index].mins.value[2] = -1.0f;
		fixture->models[index].maxs.value[0] = 16.0f;
		fixture->models[index].maxs.value[1] = 16.0f;
		fixture->models[index].maxs.value[2] = 65.0f;
	}
}

static void DestroyFixture(fixture_t *fixture)
{
	free(fixture->entities);
	memset(fixture, 0, sizeof(*fixture));
}

static sg_bsp_entity_semantics_t *Build(fixture_t *fixture)
{
	sg_bsp_entity_semantics_t *semantics = NULL;
	sg_bsp_entity_semantics_error_t error;

	CHECK(SG_BspEntitySemanticsBuild(&fixture->world,
		binding.source_set_identity, &semantics, &error));
	return semantics;
}

static void ShiftStringOffsets(sg_bsp_entity_semantics_t *semantics,
	uint32_t offset)
{
	uint32_t index;

	for (index = 0U; index < semantics->entity_count; index++)
	{
		uint32_t *values[] = {
			&semantics->entities[index].classname,
			&semantics->entities[index].targetname,
			&semantics->entities[index].required_item,
			&semantics->entities[index].spawned_classname,
			&semantics->entities[index].destination_map
		};
		uint32_t value_index;

		for (value_index = 0U;
			value_index < sizeof(values) / sizeof(values[0]); value_index++)
			if (*values[value_index] != SG_BSP_ENTITY_STRING_NONE)
				*values[value_index] += offset;
	}
	for (index = 0U; index < semantics->edge_count; index++)
		if (semantics->edges[index].name != SG_BSP_ENTITY_STRING_NONE)
			semantics->edges[index].name += offset;
}

static void TestCompleteAuditAndOwnedPublication(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" \"gravity\" \"100\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n"
		"{ \"classname\" \"info_flag_red\" \"origin\" \"1 2 3\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	sg_bsp_entity_semantics_publication_t *publication = NULL;
	sg_bsp_entity_semantics_view_t view;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	CHECK(SG_BspEntitySemanticsAudit(&fixture.world, &binding, candidate,
		&audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OK);
	CHECK(audit.completeness == SG_BSP_ENTITY_SEMANTICS_COMPLETE);
	CHECK(audit.expected_entities == 3U);
	CHECK(audit.expected_edges == 1U);
	CHECK(SG_BspEntitySemanticsPublicationIssue(&fixture.world, &binding,
		candidate, &publication, &audit));
	CHECK(publication != NULL);
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
	memset(&view, 0, sizeof(view));
	CHECK(SG_BspEntitySemanticsPublicationRead(publication, &view));
	CHECK(!memcmp(&view.binding, &binding, sizeof(binding)));
	CHECK(view.completeness == SG_BSP_ENTITY_SEMANTICS_COMPLETE);
	CHECK(view.entity_count == 3U);
	CHECK(view.edge_count == 1U);
	CHECK(view.entities != NULL && !strcmp(
		SG_BspEntitySemanticsViewString(&view, view.entities[0].classname),
		"func_door"));
	SG_BspEntitySemanticsPublicationDestroy(publication);
}

static void TestProvenEmpty(void)
{
	fixture_t fixture;
	sg_bsp_entity_semantics_t candidate;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&fixture, "{ \"classname\" \"worldspawn\" }\n");
	memset(&candidate, 0, sizeof(candidate));
	candidate.source_set_identity = binding.source_set_identity;
	candidate.world.source_set_identity = binding.source_set_identity;
	candidate.world.source_entity_ordinal = 0U;
	candidate.world.gravity = 800.0f;
	CHECK(SG_BspEntitySemanticsAudit(&fixture.world, &binding, &candidate,
		&audit));
	CHECK(audit.completeness == SG_BSP_ENTITY_SEMANTICS_PROVEN_EMPTY);
	CHECK(audit.expected_entities == 0U);
	CHECK(audit.expected_edges == 0U);
	DestroyFixture(&fixture);
}

static void TestExplicitWorldFactIsComplete(void)
{
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&fixture,
		"{ \"classname\" \"worldspawn\" \"gravity\" \"100\" }\n");
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	CHECK(SG_BspEntitySemanticsAudit(&fixture.world, &binding, candidate,
		&audit));
	CHECK(audit.completeness == SG_BSP_ENTITY_SEMANTICS_COMPLETE);
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestEquivalentStringLayout(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	char *shifted;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		shifted = malloc((size_t)candidate->string_bytes + 2U);
		CHECK(shifted != NULL);
		if (shifted)
		{
			shifted[0] = 'x';
			shifted[1] = '\0';
			memcpy(shifted + 2, candidate->strings, candidate->string_bytes);
			free(candidate->strings);
			candidate->strings = shifted;
			candidate->string_bytes += 2U;
			ShiftStringOffsets(candidate, 2U);
			CHECK(SG_BspEntitySemanticsAudit(&fixture.world, &binding,
				candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OK);
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestTrailingInventedInvalidFact(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"item_quad\" \"origin\" \"1 2 3\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantic_t *grown;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		grown = realloc(candidate->entities, 2U * sizeof(*grown));
		CHECK(grown != NULL);
		if (grown)
		{
			candidate->entities = grown;
			candidate->entities[1] = candidate->entities[0];
			candidate->entities[1].canonical_ordinal = 1U;
			candidate->entities[1].source_entity_ordinal = UINT32_MAX;
			candidate->entity_count = 2U;
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.world, &binding,
				candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT ||
				audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVENTED_FACT);
			CHECK(audit.invalid_facts != 0U || audit.invented_facts != 0U);
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestOmittedAndInventedFacts(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"weapon_railgun\" \"origin\" \"1 2 3\" }\n"
		"{ \"classname\" \"item_quad\" \"origin\" \"4 5 6\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		candidate->entity_count--;
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.world, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OMITTED_FACT);
		CHECK(audit.omitted_facts == 1U);
		candidate->entity_count++;
		candidate->entities[0].origin.value[0] += 1.0f;
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.world, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT);
		CHECK(audit.omitted_facts == 1U);
		CHECK(audit.invented_facts == 1U);
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestDuplicateAndUnresolvedFacts(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	uint32_t saved_source;
	uint32_t saved_destination;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate && candidate->entity_count == 2U && candidate->edge_count == 1U)
	{
		saved_source = candidate->entities[1].source_entity_ordinal;
		candidate->entities[1].source_entity_ordinal =
			candidate->entities[0].source_entity_ordinal;
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.world, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT);
		CHECK(audit.duplicate_facts != 0U);
		candidate->entities[1].source_entity_ordinal = saved_source;
		saved_destination = candidate->edges[0].destination;
		candidate->edges[0].destination = candidate->entity_count;
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.world, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_UNRESOLVED_FACT);
		CHECK(audit.unresolved_facts == 1U);
		candidate->edges[0].destination = saved_destination;
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestIdentityAndTransactionalOutput(void)
{
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_binding_t wrong = binding;
	sg_bsp_entity_semantics_audit_result_t audit;
	sg_bsp_entity_semantics_publication_t *sentinel =
		(sg_bsp_entity_semantics_publication_t *)(uintptr_t)1U;

	InitFixture(&fixture, "{ \"classname\" \"worldspawn\" }\n");
	candidate = Build(&fixture);
	wrong.schema_identity.bytes[0] ^= 1U;
	CHECK(!SG_BspEntitySemanticsAudit(&fixture.world, &wrong, candidate,
		&audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH);
	CHECK(!SG_BspEntitySemanticsPublicationIssue(&fixture.world, &binding,
		candidate, &sentinel, &audit));
	CHECK(sentinel == (sg_bsp_entity_semantics_publication_t *)(uintptr_t)1U);
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

int main(void)
{
	InitBinding();
	TestCompleteAuditAndOwnedPublication();
	TestProvenEmpty();
	TestExplicitWorldFactIsComplete();
	TestEquivalentStringLayout();
	TestTrailingInventedInvalidFact();
	TestOmittedAndInventedFacts();
	TestDuplicateAndUnresolvedFacts();
	TestIdentityAndTransactionalOutput();
	if (failures)
	{
		fprintf(stderr, "%d BSP entity semantics publication checks failed\n",
			failures);
		return 1;
	}
	puts("BSP entity semantics publication checks passed");
	return 0;
}
