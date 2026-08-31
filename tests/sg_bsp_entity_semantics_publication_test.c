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
	sg_bsp_plane_t plane;
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaf;
	sg_bsp_model_t models[4];
	sg_host_collision_authority_t authority;
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
	sg_rune_model_identity_t identity;
	sg_host_collision_error_t host_error;

	memset(fixture, 0, sizeof(*fixture));
	fixture->entities = malloc(length);
	CHECK(fixture->entities != NULL);
	if (!fixture->entities)
		return;
	memcpy(fixture->entities, text, length);
	memcpy(fixture->world.content_identity.bytes,
		binding.source_identity.bytes, SG_BSP_CONTENT_ID_BYTES);
	fixture->world.entities = fixture->entities;
	fixture->world.entity_byte_count = (uint32_t)length;
	fixture->world.planes = &fixture->plane;
	fixture->world.plane_count = 1U;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.leaves = &fixture->leaf;
	fixture->world.leaf_count = 1U;
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
	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(1);
	identity.physics_abi_id = UINT64_C(2);
	identity.standing_hull.mins.value[0] = -16.0f;
	identity.standing_hull.mins.value[1] = -16.0f;
	identity.standing_hull.mins.value[2] = -24.0f;
	identity.standing_hull.maxs.value[0] = 16.0f;
	identity.standing_hull.maxs.value[1] = 16.0f;
	identity.standing_hull.maxs.value[2] = 32.0f;
	identity.crouching_hull = identity.standing_hull;
	identity.crouching_hull.maxs.value[2] = 4.0f;
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100;
	identity.physics.substep_ms = 10;
	CHECK(SG_HostCollisionInit(&fixture->authority, &fixture->world,
		&identity, &host_error));
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

static sg_bsp_entity_semantics_t *BuildEffective(fixture_t *fixture,
	const sg_bsp_entity_semantics_source_t *source)
{
	sg_bsp_entity_semantics_t *semantics = NULL;
	sg_bsp_entity_semantics_error_t error;

	CHECK(source != NULL);
	if (!source)
		return NULL;
	CHECK(SG_BspEntitySemanticsBuildEffective(&fixture->world,
		source->selected_entity_text, source->selected_entity_text_bytes,
		source->survivors, source->survivor_count,
		binding.source_set_identity, &semantics, &error));
	return semantics;
}

static void TestEffectiveAuditUsesSelectedAuthority(void)
{
	static const char embedded[] =
		"{ \"classname\" \"worldspawn\" \"gravity\" \"800\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*1\" "
			"\"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*2\" }\n";
	static const char selected[] =
		"{ \"classname\" \"worldspawn\" \"gravity\" \"100\" }\n"
		"{ \"classname\" \"item_quad\" \"origin\" \"1 2 3\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*2\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*3\" "
			"\"spawnflags\" \"0\" }\n";
	static const sg_rune_source_entity_record_t survivors[] = {
		{ 0U, 0 }, { 2U, 0 }, { 3U, 0 }
	};
	fixture_t fixture;
	sg_bsp_entity_semantics_source_t source;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	const sg_bsp_entity_semantic_t *without_spawnflags;
	const sg_bsp_entity_semantic_t *explicit_zero;

	InitFixture(&fixture, embedded);
	memset(&source, 0, sizeof(source));
	source.selected_entity_text = selected;
	source.selected_entity_text_bytes = sizeof(selected);
	source.survivors = survivors;
	source.survivor_count = sizeof(survivors) / sizeof(survivors[0]);
	candidate = BuildEffective(&fixture, &source);
	CHECK(candidate != NULL);
	CHECK(candidate && candidate->world.gravity == 100.0f);
	CHECK(candidate && candidate->entity_count == 2U);
	without_spawnflags = candidate && candidate->entity_count == 2U
		? &candidate->entities[0] : NULL;
	explicit_zero = candidate && candidate->entity_count == 2U
		? &candidate->entities[1] : NULL;
	CHECK(without_spawnflags &&
		without_spawnflags->source_entity_ordinal == 2U);
	CHECK(without_spawnflags &&
		!(without_spawnflags->flags & SG_BSP_ENTITY_SPAWNFLAGS_DEFINED));
	CHECK(explicit_zero && explicit_zero->source_entity_ordinal == 3U);
	CHECK(explicit_zero &&
		(explicit_zero->flags & SG_BSP_ENTITY_SPAWNFLAGS_DEFINED));
	CHECK(SG_BspEntitySemanticsAuditEffective(&fixture.authority, &binding,
		&source, candidate, &audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OK);
	CHECK(audit.expected_entities == 2U);
	CHECK(audit.expected_landmarks == 0U);
	CHECK(audit.expected_mechanisms == 2U);
	CHECK(audit.expected_edges == 0U);
	if (candidate && explicit_zero)
	{
		sg_bsp_entity_semantics_t *mutable_candidate = candidate;

		mutable_candidate->entities[1].flags &=
			~(sg_bsp_entity_semantic_flags_t)
				SG_BSP_ENTITY_SPAWNFLAGS_DEFINED;
		CHECK(!SG_BspEntitySemanticsAuditEffective(&fixture.authority,
			&binding, &source, mutable_candidate, &audit));
		CHECK(audit.code ==
			SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT);
		mutable_candidate->entities[1].flags |=
			SG_BSP_ENTITY_SPAWNFLAGS_DEFINED;
		CHECK(SG_BspEntitySemanticsAuditEffective(&fixture.authority,
			&binding, &source, mutable_candidate, &audit));
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
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
	CHECK(SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
		&audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OK);
	CHECK(audit.completeness == SG_BSP_ENTITY_SEMANTICS_COMPLETE);
	CHECK(audit.expected_entities == 3U);
	CHECK(audit.expected_edges == 1U);
	CHECK(SG_BspEntitySemanticsPublicationIssue(&fixture.authority, &binding,
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
	CHECK(SG_BspEntitySemanticsAudit(&fixture.authority, &binding, &candidate,
		&audit));
	CHECK(audit.completeness == SG_BSP_ENTITY_SEMANTICS_PROVEN_EMPTY);
	CHECK(audit.expected_entities == 0U);
	CHECK(audit.expected_edges == 0U);
	DestroyFixture(&fixture);
}

static void TestHostileStringExtent(void)
{
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	char *original_strings;
	char *replacement;
	uint32_t original_bytes;

	InitFixture(&fixture,
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"item_quad\" \"origin\" \"1 2 3\" }\n");
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		original_strings = candidate->strings;
		original_bytes = candidate->string_bytes;
		candidate->string_bytes = UINT32_C(1048586);
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
			candidate, &audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT);
		candidate->string_bytes = original_bytes;
		replacement = malloc((size_t)original_bytes);
		CHECK(replacement != NULL);
		if (replacement)
		{
			memcpy(replacement, original_strings, (size_t)original_bytes);
			candidate->strings = replacement;
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
				candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT);
			candidate->string_bytes = UINT32_C(1048586);
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
				candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT);
			free(replacement);
			candidate->strings = original_strings;
			candidate->string_bytes = original_bytes;
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestHostileArrayExtent(void)
{
	fixture_t entity_fixture;
	fixture_t edge_fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantic_t *original_entities;
	sg_bsp_entity_semantic_t *replacement_entities;
	sg_bsp_entity_semantic_edge_t *original_edges;
	sg_bsp_entity_semantic_edge_t *replacement_edges;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&entity_fixture,
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n");
	candidate = Build(&entity_fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		original_entities = candidate->entities;
		replacement_entities = malloc(sizeof(*replacement_entities));
		CHECK(replacement_entities != NULL);
		if (replacement_entities)
		{
			CHECK(candidate->entity_count > 1U);
			replacement_entities[0] = original_entities[0];
			candidate->entities = replacement_entities;
			CHECK(!SG_BspEntitySemanticsAudit(&entity_fixture.authority,
				&binding, candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT);
			CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY);
			candidate->entities = original_entities;
			free(replacement_entities);
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&entity_fixture);

	InitFixture(&edge_fixture,
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n"
		"{ \"classname\" \"func_button\" \"model\" \"*3\" \"targetname\" \"gate\" }\n");
	candidate = Build(&edge_fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		original_edges = candidate->edges;
		replacement_edges = malloc(sizeof(*replacement_edges));
		CHECK(replacement_edges != NULL);
		if (replacement_edges)
		{
			CHECK(candidate->edge_count > 1U);
			replacement_edges[0] = original_edges[0];
			candidate->edges = replacement_edges;
			CHECK(!SG_BspEntitySemanticsAudit(&edge_fixture.authority,
				&binding, candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT);
			CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY);
			candidate->edges = original_edges;
			free(replacement_edges);
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&edge_fixture);
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
	CHECK(SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
		&audit));
	CHECK(audit.completeness == SG_BSP_ENTITY_SEMANTICS_COMPLETE);
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestExactStringFacts(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_key\" \"target\" \"event\" \"item\" \"key_data_cd\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"event\" }\n"
		"{ \"classname\" \"target_spawner\" \"target\" \"spawned_class\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"spawned_class\" }\n"
		"{ \"classname\" \"target_changelevel\" \"map\" \"nextmap\" }\n"
		"{ \"classname\" \"func_button\" \"model\" \"*1\" \"team\" \"team_name\" }\n"
		"{ \"classname\" \"func_button\" \"model\" \"*2\" \"team\" \"team_name\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	uint32_t field_hits[5] = { 0U, 0U, 0U, 0U, 0U };
	uint32_t edge_hits = 0U;
	uint32_t index;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		for (index = 0U; index < candidate->entity_count; index++)
		{
			uint32_t *fields[] = {
				&candidate->entities[index].classname,
				&candidate->entities[index].targetname,
				&candidate->entities[index].required_item,
				&candidate->entities[index].spawned_classname,
				&candidate->entities[index].destination_map
			};
			uint32_t field;

			for (field = 0U; field < sizeof(fields) / sizeof(fields[0]); field++)
				if (*fields[field] != SG_BSP_ENTITY_STRING_NONE)
				{
					char saved = candidate->strings[*fields[field]];

					field_hits[field]++;
					candidate->strings[*fields[field]] = saved == 'x' ? 'y' : 'x';
					CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority,
						&binding, candidate, &audit));
					CHECK(audit.code ==
						SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT);
					CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY);
					{
						uint32_t first_record = index;
						uint32_t prior;
						uint32_t prior_field;

						for (prior = 0U; prior < index; prior++)
						{
							uint32_t *prior_fields[] = {
								&candidate->entities[prior].classname,
								&candidate->entities[prior].targetname,
								&candidate->entities[prior].required_item,
								&candidate->entities[prior].spawned_classname,
								&candidate->entities[prior].destination_map
							};

							for (prior_field = 0U;
								prior_field < sizeof(prior_fields) /
									sizeof(prior_fields[0]); prior_field++)
								if (first_record == index &&
									*prior_fields[prior_field] == *fields[field])
									first_record = prior;
						}
						CHECK(audit.record == first_record);
					}
					candidate->strings[*fields[field]] = saved;
				}
		}
		for (index = 0U; index < candidate->edge_count; index++)
		{
			uint32_t offset = candidate->edges[index].name;
			char saved;
			uint32_t entity_index;
			int entity_reference = 0;

			CHECK(offset != SG_BSP_ENTITY_STRING_NONE);
			if (offset == SG_BSP_ENTITY_STRING_NONE)
				continue;
			for (entity_index = 0U; entity_index < candidate->entity_count;
				entity_index++)
			{
				uint32_t *fields[] = {
					&candidate->entities[entity_index].classname,
					&candidate->entities[entity_index].targetname,
					&candidate->entities[entity_index].required_item,
					&candidate->entities[entity_index].spawned_classname,
					&candidate->entities[entity_index].destination_map
				};
				uint32_t field;

				for (field = 0U; field < sizeof(fields) / sizeof(fields[0]); field++)
					if (*fields[field] == offset)
						entity_reference = 1;
			}
			if (entity_reference)
				continue;
			edge_hits++;
			saved = candidate->strings[offset];
			candidate->strings[offset] = saved == 'x' ? 'y' : 'x';
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
				candidate, &audit));
			CHECK(audit.code ==
				SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT);
			CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY);
			CHECK(audit.record == index);
			candidate->strings[offset] = saved;
		}
		for (index = 0U; index < sizeof(field_hits) / sizeof(field_hits[0]); index++)
			CHECK(field_hits[index] != 0U);
		CHECK(edge_hits != 0U);
		CHECK(SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
			&audit));
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
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
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
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OMITTED_FACT);
		CHECK(audit.omitted_facts == 1U);
		candidate->entity_count++;
		candidate->entities[0].origin.value[0] += 1.0f;
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
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
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT);
		CHECK(audit.duplicate_facts != 0U);
		candidate->entities[1].source_entity_ordinal = saved_source;
		saved_destination = candidate->edges[0].destination;
		candidate->edges[0].destination = candidate->entity_count;
		CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding, candidate,
			&audit));
		CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_UNRESOLVED_FACT);
		CHECK(audit.unresolved_facts == 1U);
		candidate->edges[0].destination = saved_destination;
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestDuplicateTopologyFact(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n"
		"{ \"classname\" \"func_button\" \"model\" \"*3\" \"targetname\" \"gate\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		CHECK(candidate->edge_count == 2U);
		if (candidate->edge_count == 2U)
		{
			candidate->edges[1] = candidate->edges[0];
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
				candidate, &audit));
			CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT);
			CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY);
			CHECK(audit.duplicate_facts == 1U);
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestAppendedDuplicateFacts(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"targetname\" \"gate\" }\n";
	fixture_t fixture;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;
	uint32_t entity_count;
	uint32_t edge_count;

	InitFixture(&fixture, text);
	candidate = Build(&fixture);
	CHECK(candidate != NULL);
	if (candidate)
	{
		entity_count = candidate->entity_count;
		edge_count = candidate->edge_count;
		CHECK(entity_count > 0U);
		CHECK(edge_count > 0U);
		if (entity_count > 0U)
		{
			candidate->entities[entity_count] = candidate->entities[0];
			candidate->entity_count = entity_count + 1U;
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
				candidate, &audit));
			CHECK(audit.code ==
				SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT);
			CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY);
			CHECK(audit.record == entity_count);
			CHECK(audit.duplicate_facts == 1U);
			candidate->entity_count = entity_count;
		}
		if (edge_count > 0U)
		{
			candidate->edges[edge_count] = candidate->edges[0];
			candidate->edge_count = edge_count + 1U;
			CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &binding,
				candidate, &audit));
			CHECK(audit.code ==
				SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT);
			CHECK(audit.domain == SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY);
			CHECK(audit.record == edge_count);
			CHECK(audit.duplicate_facts == 1U);
			candidate->edge_count = edge_count;
		}
	}
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

static void TestCrossBspIdentityMismatch(void)
{
	fixture_t first;
	fixture_t second;
	sg_bsp_entity_semantics_t *candidate;
	sg_bsp_entity_semantics_audit_result_t audit;

	InitFixture(&first, "{ \"classname\" \"worldspawn\" }\n");
	InitFixture(&second, "{ \"classname\" \"worldspawn\" }\n");
	second.world.content_identity.bytes[0] ^= 1U;
	candidate = Build(&second);
	CHECK(candidate != NULL);
	CHECK(!SG_BspEntitySemanticsAudit(&second.authority, &binding,
		candidate, &audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH);
	second.authority.content_identity = second.world.content_identity;
	CHECK(!SG_BspEntitySemanticsAudit(&second.authority, &binding,
		candidate, &audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH);
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&second);
	DestroyFixture(&first);
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
	CHECK(!SG_BspEntitySemanticsAudit(&fixture.authority, &wrong, candidate,
		&audit));
	CHECK(audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH);
	CHECK(!SG_BspEntitySemanticsPublicationIssue(&fixture.authority, &binding,
		candidate, &sentinel, &audit));
	CHECK(sentinel == (sg_bsp_entity_semantics_publication_t *)(uintptr_t)1U);
	SG_BspEntitySemanticsDestroy(candidate);
	DestroyFixture(&fixture);
}

int main(void)
{
	InitBinding();
	TestEffectiveAuditUsesSelectedAuthority();
	TestCompleteAuditAndOwnedPublication();
	TestProvenEmpty();
	TestHostileStringExtent();
	TestHostileArrayExtent();
	TestExplicitWorldFactIsComplete();
	TestExactStringFacts();
	TestTrailingInventedInvalidFact();
	TestOmittedAndInventedFacts();
	TestDuplicateAndUnresolvedFacts();
	TestDuplicateTopologyFact();
	TestAppendedDuplicateFacts();
	TestCrossBspIdentityMismatch();
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
