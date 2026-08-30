int SG_MechanismFixtureMain(void);
#define main SG_MechanismFixtureMain
#include "sg_mechanism_capability_test.c"
#undef main

#include "slipgate/sg_external_force_publication.h"
#include "slipgate/sg_external_force_builder.h"
#include "slipgate/sg_host_law_construction_offline.h"
#include "slipgate/sg_host_law_owner.h"
#include "slipgate/sg_phase_catalog_owner.h"

void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);
void Com_Printf(char *format, ...);

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

static int external_failures;
static sg_host_law_view_t external_host_view;
static const sg_host_law_publication_t *external_host_token =
	(const sg_host_law_publication_t *)(uintptr_t)UINT32_C(1);
static const sg_host_law_construction_t *external_construction_token =
	(const sg_host_law_construction_t *)(uintptr_t)UINT32_C(2);
static mechanism_fixture_t *external_active_fixture;
static uint32_t external_construction_issues;
static uint32_t external_construction_destroys;

typedef struct external_publication_layout_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const sg_external_force_publication_t *self;
	size_t allocation_size;
	size_t allocation_size_inverse;
	sg_external_force_view_t view;
} external_publication_layout_t;

static sg_external_force_fact_t *ExternalFacts(
	external_publication_layout_t *layout)
{
	return (sg_external_force_fact_t *)(void *)(layout + 1);
}

static uint64_t ExternalHashBytes(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static sg_external_force_completeness_t ExternalKindCompleteness(
	const sg_external_force_fact_t *facts, uint32_t count, uint32_t kind)
{
	uint32_t index;
	int found = 0;

	for (index = 0U; index < count; index++)
		if ((uint32_t)facts[index].kind == kind)
		{
			found = 1;
			if ((facts[index].flags &
				SG_EXTERNAL_FORCE_LAW_UNRESOLVED) != 0U)
				return SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED;
		}
	return found ? SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE :
		SG_EXTERNAL_FORCE_COMPLETENESS_PROVEN_EMPTY;
}

static void ExternalReseal(external_publication_layout_t *layout)
{
	sg_external_force_view_t hash_view;
	sg_external_force_fact_t *facts = ExternalFacts(layout);
	uint64_t hash = UINT64_C(14695981039346656037);
	uint32_t index;
	int unresolved = 0;

	layout->self = (const sg_external_force_publication_t *)(const void *)layout;
	layout->allocation_size = sizeof(*layout) +
		(size_t)layout->view.fact_count * sizeof(*facts);
	layout->allocation_size_inverse = ~layout->allocation_size;
	memset(layout->view.fact_count_by_kind, 0,
		sizeof(layout->view.fact_count_by_kind));
	for (index = 0U; index < layout->view.fact_count; index++)
		layout->view.fact_count_by_kind[facts[index].kind]++;
	for (index = 0U; index < SG_EXTERNAL_FORCE_KIND_COUNT; index++)
	{
		layout->view.completeness_by_kind[index] =
			ExternalKindCompleteness(facts, layout->view.fact_count, index);
		unresolved |= layout->view.completeness_by_kind[index] ==
			SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED;
	}
	layout->view.completeness = unresolved ?
		SG_EXTERNAL_FORCE_COMPLETENESS_UNRESOLVED :
		(layout->view.fact_count ? SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE :
		 SG_EXTERNAL_FORCE_COMPLETENESS_PROVEN_EMPTY);
	hash_view = layout->view;
	hash_view.content_identity = 0U;
	hash = ExternalHashBytes(hash, &hash_view, sizeof(hash_view));
	hash = ExternalHashBytes(hash, facts,
		(size_t)layout->view.fact_count * sizeof(*facts));
	layout->view.content_identity = hash ? hash : UINT64_C(1);
}

static external_publication_layout_t *ExternalClonePublication(
	const sg_external_force_publication_t *publication, uint32_t fact_count)
{
	const external_publication_layout_t *source =
		(const external_publication_layout_t *)(const void *)publication;
	external_publication_layout_t *clone;
	uint32_t copy_count = fact_count < source->view.fact_count ? fact_count :
		source->view.fact_count;
	size_t size = sizeof(*clone) +
		(size_t)fact_count * sizeof(sg_external_force_fact_t);

	clone = calloc(1U, size);
	if (!clone)
		return NULL;
	memcpy(clone, source, sizeof(*clone));
	if (copy_count)
		memcpy(ExternalFacts(clone), source + 1,
			(size_t)copy_count * sizeof(sg_external_force_fact_t));
	clone->view.fact_count = fact_count;
	ExternalReseal(clone);
	return clone;
}

#define CHECK_EXTERNAL(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: external check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		external_failures++; \
	} \
} while (0)

sg_host_law_result_t SG_HostLawPublicationRead(
	const sg_host_law_publication_t *publication, sg_host_law_view_t *view_out)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	if (publication != external_host_token || !view_out)
	{
		result.status = SG_HOST_LAW_CORRUPT_PUBLICATION;
		return result;
	}
	*view_out = external_host_view;
	result.status = SG_HOST_LAW_OK;
	return result;
}

sg_host_law_result_t SG_HostLawPublicationRevalidateProduction(
	const sg_host_law_publication_t *publication)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = publication == external_host_token ? SG_HOST_LAW_OK :
		SG_HOST_LAW_CORRUPT_PUBLICATION;
	return result;
}

static sg_host_law_result_t ExternalConstructionResult(int ok)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = ok ? SG_HOST_LAW_OK : SG_HOST_LAW_CORRUPT_PUBLICATION;
	return result;
}

sg_host_law_result_t SG_HostLawProductionConstructionIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_construction_t **construction_out)
{
	if (!external_active_fixture ||
		authority != &external_active_fixture->authority || !construction_out ||
		*construction_out)
		return ExternalConstructionResult(0);
	*construction_out = (sg_host_law_construction_t *)(uintptr_t)UINT32_C(2);
	external_construction_issues++;
	return ExternalConstructionResult(1);
}

void SG_HostLawConstructionDestroy(sg_host_law_construction_t *construction)
{
	if (construction ==
		(sg_host_law_construction_t *)(uintptr_t)UINT32_C(2))
		external_construction_destroys++;
}

sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out)
{
	const sg_bsp_world_t *world_value;

	if (construction != external_construction_token || !view_out ||
		!external_active_fixture)
		return ExternalConstructionResult(0);
	world_value = external_active_fixture->authority.world;
	memset(view_out, 0, sizeof(*view_out));
	view_out->version = SG_HOST_LAW_PUBLICATION_VERSION;
	view_out->current = 1U;
	view_out->level_generation = 1U;
	view_out->host_static_identity = external_host_view.static_identity;
	view_out->geometry.bsp_identity = world_value->content_identity;
	view_out->geometry.bsp_bytes = world_value->source_size;
	view_out->geometry.engine_checksum = world_value->engine_checksum;
	view_out->geometry.node_count = world_value->node_count;
	view_out->geometry.model_count = world_value->model_count;
	view_out->geometry.brush_count = world_value->brush_count;
	view_out->geometry.brush_side_count = world_value->brush_side_count;
	view_out->laws = external_host_view;
	return ExternalConstructionResult(1);
}

sg_host_law_result_t SG_HostLawConstructionClassifyPose(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	return ExternalConstructionResult(construction ==
		external_construction_token && external_active_fixture &&
		SG_HostCollisionClassifyPose(&external_active_fixture->authority, scene,
			origin, stance, pose_out));
}

sg_host_law_result_t SG_HostLawConstructionPointContents(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	if (construction != external_construction_token ||
		!external_active_fixture || !contents_out)
		return ExternalConstructionResult(0);
	*contents_out = SG_HostCollisionPointContents(
		&external_active_fixture->authority, scene, point);
	return ExternalConstructionResult(1);
}

sg_host_law_result_t SG_HostLawConstructionPmove(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	return ExternalConstructionResult(construction ==
		external_construction_token && external_active_fixture &&
		SG_HostPmoveEvaluateFrame(&external_active_fixture->authority, scene,
			Pmove, request, result_out, error_out));
}

sg_host_law_result_t SG_HostLawConstructionConfigurationAudit(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	sg_configuration_audit_result_t *audit_out)
{
	return ExternalConstructionResult(construction ==
		external_construction_token && external_active_fixture &&
		SG_ConfigurationAudit(&external_active_fixture->authority,
			configuration, audit_out));
}

sg_host_law_result_t SG_HostLawConstructionSemanticsAudit(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *audit_out)
{
	return ExternalConstructionResult(construction ==
		external_construction_token && external_active_fixture &&
		SG_ConfigurationSemanticsAudit(&external_active_fixture->authority,
			configuration, semantics, audit_out));
}

sg_host_law_result_t SG_HostLawConstructionCompletenessProve(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	sg_bsp_completeness_result_t *proof_out)
{
	return ExternalConstructionResult(construction ==
		external_construction_token && external_active_fixture &&
		SG_BspCompletenessProve(&external_active_fixture->authority,
			configuration, proof_out));
}

static void ExternalInstallEntityWorld(mechanism_fixture_t *fixture)
{
	enum { EXTERNAL_DEEP_CONVEYOR_NODES = 4096 };
	static sg_bsp_plane_t expanded_planes[8];
	static sg_bsp_model_t expanded_models[6];
	static sg_bsp_node_t
		expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 9];
	static sg_bsp_leaf_t expanded_leaves[9];
	static uint32_t expanded_leaf_brushes[3];
	static sg_bsp_brush_t expanded_brushes[2];
	static sg_bsp_brush_side_t expanded_brush_sides[12];
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_gravity\" \"model\" \"*1\" "
		"\"gravity\" \"2\" }\n"
		"{ \"classname\" \"func_conveyor\" \"model\" \"*2\" "
		"\"origin\" \"0 32 0\" \"angles\" \"0 90 0\" "
		"\"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"func_conveyor\" \"model\" \"*5\" "
		"\"origin\" \"128 128 0\" \"angles\" \"0 90 0\" "
		"\"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"trigger_monsterjump\" \"model\" \"*3\" "
		"\"speed\" \"200\" \"height\" \"200\" }\n"
		"{ \"classname\" \"trigger_hurt\" \"model\" \"*4\" "
		"\"dmg\" \"50\" }\n";
	uint32_t index;

	memset(expanded_models, 0, sizeof(expanded_models));
	memcpy(expanded_models, fixture->models, sizeof(fixture->models));
	memset(expanded_planes, 0, sizeof(expanded_planes));
	memcpy(expanded_planes, fixture->planes, sizeof(fixture->planes));
	SetPlane(&expanded_planes[7], 0.0f, 0.0f, 1.0f, 80.0f);
	memset(expanded_nodes, 0, sizeof(expanded_nodes));
	memcpy(expanded_nodes, fixture->world.nodes,
		fixture->world.node_count * sizeof(*expanded_nodes));
	memcpy(expanded_leaves, fixture->leaves, sizeof(fixture->leaves));
	memset(expanded_leaf_brushes, 0, sizeof(expanded_leaf_brushes));
	expanded_leaf_brushes[0] = fixture->leaf_brush;
	expanded_leaf_brushes[1] = 1U;
	expanded_leaf_brushes[2] = 1U;
	memset(expanded_brushes, 0, sizeof(expanded_brushes));
	expanded_brushes[0] = fixture->brush;
	expanded_brushes[1] = fixture->brush;
	expanded_brushes[1].first_side = 6U;
	expanded_brushes[1].contents = SG_HOST_CONTENTS_SOLID |
		SG_HOST_CONTENTS_CURRENT_90;
	memcpy(expanded_brush_sides, fixture->brush_sides,
		sizeof(fixture->brush_sides));
	memcpy(&expanded_brush_sides[6], fixture->brush_sides,
		sizeof(fixture->brush_sides));
	expanded_models[2] = fixture->models[0];
	expanded_models[2].headnode = 1;
	Set3(expanded_models[2].mins.value, -128.0f, -128.0f, -128.0f);
	Set3(expanded_models[2].maxs.value, 128.0f, 128.0f, 128.0f);
	memset(&expanded_leaves[3], 0, sizeof(expanded_leaves[3]));
	expanded_leaves[3].contents = SG_HOST_CONTENTS_SOLID |
		SG_HOST_CONTENTS_CURRENT_0;
	expanded_leaves[3].cluster = -1;
	memset(&expanded_leaves[4], 0, sizeof(expanded_leaves[4]));
	expanded_leaves[4].contents = SG_HOST_CONTENTS_SOLID;
	expanded_leaves[4].cluster = -1;
	for (index = 1U; index <= EXTERNAL_DEEP_CONVEYOR_NODES; index++)
	{
		expanded_nodes[index].plane = 0U;
		expanded_nodes[index].children[0] =
			index == EXTERNAL_DEEP_CONVEYOR_NODES ? -4 :
			(int32_t)(index + 1U);
		expanded_nodes[index].children[1] = -5;
	}
	expanded_models[3] = fixture->models[1];
	expanded_models[4] = fixture->models[1];
	expanded_models[5] = fixture->models[1];
	expanded_models[5].headnode = EXTERNAL_DEEP_CONVEYOR_NODES + 1;
	memset(&expanded_leaves[5], 0, sizeof(expanded_leaves[5]));
	expanded_leaves[5].contents = SG_HOST_CONTENTS_SOLID |
		SG_HOST_CONTENTS_CURRENT_90;
	expanded_leaves[5].cluster = -1;
	expanded_leaves[5].first_leaf_brush = 1U;
	expanded_leaves[5].leaf_brush_count = 1U;
	memset(&expanded_leaves[6], 0, sizeof(expanded_leaves[6]));
	expanded_leaves[6].cluster = -1;
	expanded_leaves[6].first_leaf_brush = 2U;
	expanded_leaves[6].leaf_brush_count = 1U;
	for (index = 0U; index < 6U; index++)
	{
		uint32_t node_index = EXTERNAL_DEEP_CONVEYOR_NODES + 1U + index;

		expanded_nodes[node_index].plane = index + 1U;
		expanded_nodes[node_index].children[0] = -7;
		expanded_nodes[node_index].children[1] = index == 5U ? -6 :
			(int32_t)(node_index + 1U);
	}
	memset(&expanded_leaves[7], 0, sizeof(expanded_leaves[7]));
	memset(&expanded_leaves[8], 0, sizeof(expanded_leaves[8]));
	expanded_leaves[7].cluster = 0;
	expanded_leaves[8].cluster = 1;
	expanded_nodes[0].children[0] = EXTERNAL_DEEP_CONVEYOR_NODES + 7;
	expanded_nodes[0].children[1] = EXTERNAL_DEEP_CONVEYOR_NODES + 8;
	expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 7].plane = 7U;
	expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 7].children[0] = -8;
	expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 7].children[1] = -1;
	expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 8].plane = 7U;
	expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 8].children[0] = -9;
	expanded_nodes[EXTERNAL_DEEP_CONVEYOR_NODES + 8].children[1] = -2;
	fixture->world.planes = expanded_planes;
	fixture->world.plane_count = 8U;
	fixture->world.models = expanded_models;
	fixture->world.model_count = 6U;
	fixture->world.nodes = expanded_nodes;
	fixture->world.node_count = EXTERNAL_DEEP_CONVEYOR_NODES + 9U;
	fixture->world.leaves = expanded_leaves;
	fixture->world.leaf_count = 9U;
	fixture->world.leaf_brushes = expanded_leaf_brushes;
	fixture->world.leaf_brush_count = 3U;
	fixture->world.brushes = expanded_brushes;
	fixture->world.brush_count = 2U;
	fixture->world.brush_sides = expanded_brush_sides;
	fixture->world.brush_side_count = 12U;
	fixture->world.entities = (uint8_t *)(uintptr_t)text;
	fixture->world.entity_byte_count = (uint32_t)sizeof(text);

}

static sg_bsp_entity_semantics_publication_t *ExternalEntityPublication(
	mechanism_fixture_t *fixture)
{
	sg_bsp_entity_semantics_binding_t binding;
	sg_bsp_entity_semantics_t *candidate = NULL;
	sg_bsp_entity_semantics_publication_t *publication = NULL;
	sg_bsp_entity_semantics_error_t error;
	sg_bsp_entity_semantics_audit_result_t audit;

	memset(&binding, 0, sizeof(binding));
	memcpy(binding.source_identity.bytes,
		fixture->authority.content_identity.bytes,
		sizeof(binding.source_identity.bytes));
	binding.source_set_identity =
		fixture->authority.identity.source_set_identity;
	binding.schema_identity = SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID;
	CHECK_EXTERNAL(SG_BspEntitySemanticsBuild(&fixture->world,
		binding.source_set_identity, &candidate, &error));
	if (candidate)
	{
		CHECK_EXTERNAL(SG_BspEntitySemanticsPublicationIssue(
			&fixture->authority, &binding, candidate, &publication, &audit));
		SG_BspEntitySemanticsDestroy(candidate);
	}
	return publication;
}

static void ExternalHostView(const mechanism_fixture_t *fixture)
{
	memset(&external_host_view, 0, sizeof(external_host_view));
	external_host_view.version = SG_HOST_LAW_PUBLICATION_VERSION;
	external_host_view.collision_law_id = UINT64_C(1);
	external_host_view.pmove_law_id = UINT64_C(2);
	external_host_view.gravity_law_id = UINT64_C(3);
	external_host_view.bsp_identity = fixture->authority.content_identity;
	external_host_view.bsp_bytes = 1U;
	external_host_view.static_identity.bsp_identity =
		fixture->authority.content_identity;
	external_host_view.static_identity.bsp_bytes = 1U;
	external_host_view.static_identity.physics_abi_id =
		fixture->authority.identity.physics_abi_id;
	external_host_view.static_identity.standing_hull =
		fixture->authority.identity.standing_hull;
	external_host_view.static_identity.crouching_hull =
		fixture->authority.identity.crouching_hull;
	external_host_view.static_identity.physics =
		fixture->authority.identity.physics;
	external_host_view.pmove_behavior_fingerprint = UINT64_C(0xabc123);
}

static int ExternalRebuildWorld(mechanism_fixture_t *fixture,
	int with_currents)
{
	sg_rune_model_identity_t identity = fixture->authority.identity;
	sg_host_collision_error_t host_error;
	sg_configuration_error_t configuration_error;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t limits;
	uint32_t index;

	SG_ConfigurationSemanticsDestroy(fixture->configuration_semantics);
	SG_ConfigurationDestroy(fixture->configuration);
	fixture->configuration_semantics = NULL;
	fixture->configuration = NULL;
	if (with_currents)
	{
		fixture->world.leaves[0].contents = SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_UP;
		fixture->world.leaves[1].contents = fixture->world.leaves[0].contents;
	}
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world, &identity,
			&host_error) ||
		!SG_ConfigurationBuild(&fixture->authority, NULL,
			&fixture->configuration, &configuration_error))
		return 0;
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture->authority,
		fixture->configuration, &limits, &fixture->configuration_semantics,
		&semantics_error))
		return 0;
	if (!fixture->configuration || !fixture->configuration_semantics)
		return 0;
	fixture->source.authority = &fixture->authority;
	fixture->source.configuration = fixture->configuration;
	fixture->source.configuration_semantics =
		fixture->configuration_semantics;
	if (with_currents)
	{
		fixture->phases[0].motion = SG_RUNE_MOTION_SWIMMING;
		fixture->phases[0].support = SG_RUNE_SUPPORT_NONE;
		for (index = 0U; index < PHASE_COUNT; index++)
			fixture->phases[index].medium = SG_RUNE_MEDIUM_WATER;
	}
	for (index = 0U; index < TRACE_COUNT; index++)
	{
		fixture->traces[index].source_region = FindRegion(fixture,
			fixture->traces[index].entry_witness.value);
		fixture->traces[index].destination_region = FindRegion(fixture,
			fixture->traces[index].exit_witness.value);
		fixture->candidates[index].source_region =
			fixture->traces[index].source_region;
		fixture->candidates[index].destination_region =
			fixture->traces[index].destination_region;
	}
	return 1;
}

static int ExternalSourceInit(mechanism_fixture_t *fixture,
	sg_mechanism_capability_set_t **capabilities_out,
	sg_phase_catalog_publication_owner_t **phase_owner_out,
	sg_phase_catalog_publication_t **phase_out,
	sg_bsp_entity_semantics_publication_t **entity_out,
	sg_external_force_source_t *source_out, int with_currents)
{
	sg_mechanism_capability_error_t mechanism_error;
	sg_phase_catalog_error_t phase_error;
	sg_phase_catalog_audit_result_t phase_audit;
	uint32_t index;

	memset(source_out, 0, sizeof(*source_out));
	if (!FixtureInit(fixture))
		return 0;
	ExternalInstallEntityWorld(fixture);
	if (!ExternalRebuildWorld(fixture, with_currents))
		return 0;
	fixture->entities[6].flags |= SG_BSP_ENTITY_DWELL_DEFINED;
	fixture->entities[6].dwell_ms = -1000.0f;
	fixture->traces[7].flags |= SG_MECHANISM_HOST_TRACE_ONE_SHOT;
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
	{
		fixture->world.content_identity.bytes[index] = (uint8_t)(index + 1U);
		fixture->authority.content_identity.bytes[index] = (uint8_t)(index + 1U);
	}
	if (!Build(fixture, capabilities_out, &mechanism_error) ||
		!SG_PhaseCatalogPublicationOwnerCreate(phase_owner_out) ||
		!SG_PhaseCatalogPublicationBuild(*phase_owner_out,
			fixture->capability_owner, &fixture->authority,
			fixture->configuration, fixture->configuration_semantics,
			*capabilities_out, phase_out, &phase_error, &phase_audit))
		return 0;
	*entity_out = ExternalEntityPublication(fixture);
	if (!*entity_out)
		return 0;
	ExternalHostView(fixture);
	external_active_fixture = fixture;
	source_out->collision_authority = &fixture->authority;
	source_out->engine_authority = external_host_token;
	source_out->construction = external_construction_token;
	source_out->entity_semantics = *entity_out;
	source_out->configuration = fixture->configuration;
	source_out->configuration_semantics = fixture->configuration_semantics;
	source_out->mechanism_owner = fixture->capability_owner;
	source_out->mechanisms = *capabilities_out;
	source_out->phase_owner = *phase_owner_out;
	source_out->phases = *phase_out;
	return 1;
}

static void TestAllAcceptedMechanismForces(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *capabilities = NULL;
	sg_phase_catalog_publication_owner_t *phase_owner = NULL;
	sg_phase_catalog_publication_t *phases = NULL;
	sg_bsp_entity_semantics_publication_t *entities = NULL;
	sg_external_force_source_t source;
	sg_external_force_publication_t *publication = NULL;
	sg_external_force_publication_t *second_publication = NULL;
	sg_external_force_audit_result_t audit;
	sg_external_force_view_t view;
	sg_external_force_view_t second_view;
	uint32_t index;
	uint32_t pushes = 0U;
	uint32_t movers = 0U;
	uint32_t gravity = 0U;
	uint32_t conveyors = 0U;
	uint32_t conveyor_neutral = 0U;
	uint32_t conveyor_combined = 0U;
	uint32_t conveyor_cap = 0U;
	uint32_t currents = 0U;
	uint32_t shallow_currents = 0U;
	uint32_t deep_currents = 0U;
	uint32_t combined_currents = 0U;
	uint32_t capped_currents = 0U;
	uint32_t accelerated_currents = 0U;

	CHECK_EXTERNAL(ExternalSourceInit(&fixture, &capabilities, &phase_owner,
		&phases, &entities, &source, 1));
	if (!entities || !phases || !capabilities)
		return;
	{
		const float inside_solid[3] = { 0.0f, 64.0f, 0.0f };
		const float above[3] = { 0.0f, 64.0f, 64.0f };
		const float below[3] = { 0.0f, 64.0f, -64.0f };
		sg_host_collision_transform_t transform;
		sg_host_collision_trace_t support;
		sg_host_collision_contents_t point_contents;

		memset(&transform, 0, sizeof(transform));
		transform.origin[1] = 32.0f;
		transform.angles[1] = 90.0f;
		point_contents = SG_HostCollisionPointContentsModel(&fixture.authority,
			2U, &transform, inside_solid);
		CHECK_EXTERNAL((point_contents & SG_HOST_CONTENTS_SOLID) != 0U);
		CHECK_EXTERNAL((point_contents & SG_HOST_CONTENTS_CURRENT_0) != 0U);
		CHECK_EXTERNAL(SG_HostCollisionTraceModel(&fixture.authority, 2U,
			&transform, above,
			fixture.authority.identity.standing_hull.mins.value,
			fixture.authority.identity.standing_hull.maxs.value, below,
			SG_HOST_MASK_PLAYER_SOLID, &support));
		CHECK_EXTERNAL(support.fraction == 1.0f);
		CHECK_EXTERNAL((support.contents & SG_HOST_CONTENTS_CURRENT_0) == 0U);
	}
	{
		const sg_host_law_construction_t *saved = source.construction;

		external_construction_issues = 0U;
		external_construction_destroys = 0U;
		source.construction = NULL;
		if (!SG_ExternalForceProductionBuild(&source, &publication, &audit))
			fprintf(stderr, "external issue rejected: %s record=%u\n",
				SG_ExternalForceAuditCodeString(audit.code), audit.record);
		source.construction = saved;
	}
	CHECK_EXTERNAL(publication != NULL);
	CHECK_EXTERNAL(audit.code == SG_EXTERNAL_FORCE_AUDIT_OK);
	CHECK_EXTERNAL(external_construction_issues == 1U);
	CHECK_EXTERNAL(external_construction_destroys == 1U);
	if (!publication)
		return;
	CHECK_EXTERNAL(SG_ExternalForcePublicationRead(publication, &view));
	CHECK_EXTERNAL(view.completeness ==
		SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE);
	for (index = 0U; index < view.fact_count; index++)
	{
		sg_external_force_fact_t fact_storage;
		const sg_external_force_fact_t *fact = &fact_storage;

		CHECK_EXTERNAL(SG_ExternalForcePublicationFact(publication, index,
			&fact_storage));
		CHECK_EXTERNAL(SG_RuneModelStableIdValid(&fact->source_cell.value));
		CHECK_EXTERNAL(SG_RuneModelStableIdValid(
			&fact->destination_cell.value));
		CHECK_EXTERNAL(SG_RuneModelStableIdValid(&fact->source_phase.value));
		CHECK_EXTERNAL(fact->physics_abi_id ==
			fixture.authority.identity.physics_abi_id);
		if (fact->kind == SG_EXTERNAL_FORCE_TRIGGER_PUSH)
		{
			pushes++;
			CHECK_EXTERNAL(fact->source_entity_ordinal == UINT32_MAX);
			CHECK_EXTERNAL(fact->mechanism_entity_index != UINT32_MAX);
			CHECK_EXTERNAL((fact->flags & SG_EXTERNAL_FORCE_ONE_SHOT) != 0U);
			CHECK_EXTERNAL((fact->flags & SG_EXTERNAL_FORCE_HOST_PROVEN) != 0U);
			CHECK_EXTERNAL((fact->flags &
				SG_EXTERNAL_FORCE_LAW_UNRESOLVED) == 0U);
		}
		if (fact->kind == SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT)
		{
			movers++;
			CHECK_EXTERNAL((fact->flags & SG_EXTERNAL_FORCE_HOST_PROVEN) != 0U);
		}
		if (fact->kind == SG_EXTERNAL_FORCE_GRAVITY)
		{
			gravity++;
			CHECK_EXTERNAL(fact->source_entity_ordinal == UINT32_MAX);
			CHECK_EXTERNAL(fact->gravity == 800.0f);
			CHECK_EXTERNAL(fact->duration_ms == 100U);
			CHECK_EXTERNAL(fact->observation ==
				SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL);
			CHECK_EXTERNAL((fact->flags & SG_EXTERNAL_FORCE_HOST_PROVEN) != 0U);
			CHECK_EXTERNAL((fact->flags &
				SG_EXTERNAL_FORCE_LAW_UNRESOLVED) == 0U);
			if (fact->output_grounded)
				CHECK_EXTERNAL(fact->velocity.value[2] >= 0.0f);
		}
		if (fact->kind == SG_EXTERNAL_FORCE_CONVEYOR_CURRENT)
		{
			conveyors++;
			CHECK_EXTERNAL(fact->source_model_index == 5U);
			CHECK_EXTERNAL(fact->source_leaf_index == 5U);
			CHECK_EXTERNAL(fact->source_brush_index == 1U);
			CHECK_EXTERNAL(fact->source_brush_side_index == 4U);
			CHECK_EXTERNAL((fact->source_contents &
				SG_HOST_CONTENTS_CURRENT_90) != 0U);
			CHECK_EXTERNAL((fact->flags & SG_EXTERNAL_FORCE_HOST_PROVEN) != 0U);
			CHECK_EXTERNAL((fact->flags &
				SG_EXTERNAL_FORCE_LAW_UNRESOLVED) == 0U);
			CHECK_EXTERNAL(fact->source_witness.value[2] >
				fact->support_witness.value[2]);
			CHECK_EXTERNAL(fact->input_grounded == 1U);
			CHECK_EXTERNAL(fact->input_support_model_index == 5U);
			CHECK_EXTERNAL(fact->input_support_instance_id != 0U);
			if (fact->observation == SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL)
				conveyor_neutral++;
			else if (fact->observation ==
				SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)
				conveyor_combined++;
			else if (fact->observation ==
				SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)
				conveyor_cap++;
		}
		if (fact->kind == SG_EXTERNAL_FORCE_WATER_CURRENT)
		{
			currents++;
			CHECK_EXTERNAL((fact->source_contents &
				(SG_HOST_CONTENTS_CURRENT_0 |
				 SG_HOST_CONTENTS_CURRENT_UP)) != 0U);
			CHECK_EXTERNAL(fact->observation >=
				SG_EXTERNAL_FORCE_OBSERVATION_NEUTRAL &&
				fact->observation <=
				SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP);
			CHECK_EXTERNAL((fact->flags & SG_EXTERNAL_FORCE_HOST_PROVEN) != 0U);
			CHECK_EXTERNAL((fact->flags &
				SG_EXTERNAL_FORCE_LAW_UNRESOLVED) == 0U);
			if (fact->input_water_level == 1U)
				shallow_currents++;
			if (fact->input_water_level == 3U)
				deep_currents++;
			if (fact->observation ==
				SG_EXTERNAL_FORCE_OBSERVATION_COMMAND_COMBINED)
			{
				combined_currents++;
				CHECK_EXTERNAL(fact->input_command.forwardmove == SHRT_MAX);
				CHECK_EXTERNAL(fact->input_command.sidemove == SHRT_MAX);
			}
			if (fact->observation ==
				SG_EXTERNAL_FORCE_OBSERVATION_VELOCITY_CAP)
			{
				capped_currents++;
				CHECK_EXTERNAL(fabsf(fact->velocity.value[0]) <=
					fixture.authority.identity.physics.max_velocity);
				CHECK_EXTERNAL(fabsf(fact->velocity.value[1]) <=
					fixture.authority.identity.physics.max_velocity);
				CHECK_EXTERNAL(fabsf(fact->velocity.value[2]) <=
					fixture.authority.identity.physics.max_velocity);
			}
			if (fact->acceleration.value[0] != 0.0f ||
				fact->acceleration.value[1] != 0.0f ||
				fact->acceleration.value[2] != 0.0f)
				accelerated_currents++;
		}
	}
	CHECK_EXTERNAL(pushes > 0U);
	CHECK_EXTERNAL(movers >= 2U);
	CHECK_EXTERNAL(gravity > 0U);
	/* Model 2 proves an inside-solid current leaf is not a support obligation;
	 * model 5 proves rotated, bounded support is published at valid origins. */
	CHECK_EXTERNAL(conveyors > 0U);
	CHECK_EXTERNAL(conveyor_neutral == conveyor_combined);
	CHECK_EXTERNAL(conveyor_neutral == conveyor_cap);
	CHECK_EXTERNAL(conveyors == conveyor_neutral + conveyor_combined +
		conveyor_cap);
	CHECK_EXTERNAL(currents > 0U);
	CHECK_EXTERNAL(shallow_currents > 0U);
	CHECK_EXTERNAL(deep_currents > 0U);
	CHECK_EXTERNAL(combined_currents > 0U);
	CHECK_EXTERNAL(capped_currents > 0U);
	CHECK_EXTERNAL(accelerated_currents > 0U);
	CHECK_EXTERNAL(view.fact_count_by_kind[SG_EXTERNAL_FORCE_TRIGGER_PUSH] ==
		pushes);
	CHECK_EXTERNAL(view.fact_count_by_kind[
		SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT] == movers);
	CHECK_EXTERNAL(view.completeness_by_kind[
		SG_EXTERNAL_FORCE_TRIGGER_PUSH] ==
		SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE);
	CHECK_EXTERNAL(view.completeness_by_kind[
		SG_EXTERNAL_FORCE_MOVER_DISPLACEMENT] ==
		SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE);
	CHECK_EXTERNAL(view.completeness_by_kind[
		SG_EXTERNAL_FORCE_WATER_CURRENT] ==
		SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE);
	CHECK_EXTERNAL(view.completeness_by_kind[
		SG_EXTERNAL_FORCE_CONVEYOR_CURRENT] ==
		SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE);
	CHECK_EXTERNAL(view.completeness_by_kind[SG_EXTERNAL_FORCE_GRAVITY] ==
		SG_EXTERNAL_FORCE_COMPLETENESS_COMPLETE);

	/* Reissuing from the same accepted authorities is byte-deterministic. */
	CHECK_EXTERNAL(SG_ExternalForcePublicationIssue(&source,
		&second_publication, &audit));
	CHECK_EXTERNAL(SG_ExternalForcePublicationRead(second_publication,
		&second_view));
	CHECK_EXTERNAL(memcmp(&view, &second_view, sizeof(view)) == 0);
	for (index = 0U; index < view.fact_count; index++)
	{
		sg_external_force_fact_t first_fact;
		sg_external_force_fact_t second_fact;

		CHECK_EXTERNAL(SG_ExternalForcePublicationFact(publication, index,
			&first_fact));
		CHECK_EXTERNAL(SG_ExternalForcePublicationFact(second_publication,
			index, &second_fact));
		CHECK_EXTERNAL(memcmp(&first_fact, &second_fact,
			sizeof(first_fact)) == 0);
	}
	SG_ExternalForcePublicationDestroy(second_publication);

	/* The auditor reconstructs independently and classifies internally sealed
	 * hostile candidates rather than trusting issuer storage. */
	{
		external_publication_layout_t *candidate;
		sg_external_force_fact_t temporary;

		CHECK_EXTERNAL(view.fact_count >= 2U);
		/* Sorted gravity obligations are last. Removing one proves the auditor's
		 * binding-first reconstruction does not share the issuer's region walk. */
		CHECK_EXTERNAL(ExternalFacts(
			(external_publication_layout_t *)(void *)publication)[
				view.fact_count - 1U].kind == SG_EXTERNAL_FORCE_GRAVITY);
		candidate = ExternalClonePublication(publication, view.fact_count - 1U);
		CHECK_EXTERNAL(candidate != NULL);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code == SG_EXTERNAL_FORCE_AUDIT_OMITTED_FACT);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);

		candidate = ExternalClonePublication(publication, view.fact_count + 1U);
		CHECK_EXTERNAL(candidate != NULL);
		ExternalFacts(candidate)[view.fact_count] =
			ExternalFacts(candidate)[view.fact_count - 1U];
		ExternalFacts(candidate)[view.fact_count].reset_ms++;
		ExternalReseal(candidate);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code == SG_EXTERNAL_FORCE_AUDIT_INVENTED_FACT);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);

		candidate = ExternalClonePublication(publication, view.fact_count);
		CHECK_EXTERNAL(candidate != NULL);
		ExternalFacts(candidate)[1] = ExternalFacts(candidate)[0];
		ExternalReseal(candidate);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code == SG_EXTERNAL_FORCE_AUDIT_DUPLICATE_FACT);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);

		candidate = ExternalClonePublication(publication, view.fact_count);
		CHECK_EXTERNAL(candidate != NULL);
		ExternalFacts(candidate)[view.fact_count - 1U].duration_ms++;
		ExternalReseal(candidate);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code ==
			SG_EXTERNAL_FORCE_AUDIT_FACT_DISAGREEMENT);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);

		candidate = ExternalClonePublication(publication, view.fact_count);
		CHECK_EXTERNAL(candidate != NULL);
		candidate->view.pmove_behavior_fingerprint++;
		ExternalReseal(candidate);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code ==
			SG_EXTERNAL_FORCE_AUDIT_METADATA_DISAGREEMENT);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);

		candidate = ExternalClonePublication(publication, 0U);
		CHECK_EXTERNAL(candidate != NULL);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code == SG_EXTERNAL_FORCE_AUDIT_OMITTED_FACT);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);

		candidate = ExternalClonePublication(publication, view.fact_count);
		CHECK_EXTERNAL(candidate != NULL);
		temporary = ExternalFacts(candidate)[0];
		ExternalFacts(candidate)[0] =
			ExternalFacts(candidate)[view.fact_count - 1U];
		ExternalFacts(candidate)[view.fact_count - 1U] = temporary;
		ExternalReseal(candidate);
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source,
			(const sg_external_force_publication_t *)(const void *)candidate,
			&audit));
		CHECK_EXTERNAL(audit.code ==
			SG_EXTERNAL_FORCE_AUDIT_NONDETERMINISTIC_ORDER);
		SG_ExternalForcePublicationDestroy(
			(sg_external_force_publication_t *)(void *)candidate);
	}

	/* A forged zero-fact claim invalidates the seal and cannot be audited as
	 * proven-empty. No public read API exposes the mutated storage. */
	{
		external_publication_layout_t *layout =
			(external_publication_layout_t *)(void *)publication;
		sg_external_force_completeness_t saved_completeness =
			layout->view.completeness;
		uint32_t saved_count = layout->view.fact_count;

		layout->view.completeness =
			SG_EXTERNAL_FORCE_COMPLETENESS_PROVEN_EMPTY;
		layout->view.fact_count = 0U;
		CHECK_EXTERNAL(!SG_ExternalForcePublicationRead(publication, &view));
		CHECK_EXTERNAL(!SG_ExternalForcePublicationAudit(&source, publication,
			&audit));
		CHECK_EXTERNAL(audit.code ==
			SG_EXTERNAL_FORCE_AUDIT_STORAGE_DISAGREEMENT);
		layout->view.fact_count = saved_count;
		layout->view.completeness = saved_completeness;
		CHECK_EXTERNAL(SG_ExternalForcePublicationRead(publication, &view));
	}

	/* The publication owns its bytes after every predecessor ends. */
	SG_BspEntitySemanticsPublicationDestroy(entities);
	SG_PhaseCatalogPublicationDestroy(phase_owner, phases);
	SG_PhaseCatalogPublicationOwnerDestroy(phase_owner);
	SG_MechanismCapabilityDestroy(fixture.capability_owner, capabilities);
	FixtureDestroy(&fixture);
	CHECK_EXTERNAL(SG_ExternalForcePublicationRead(publication, &view));
	CHECK_EXTERNAL(view.fact_count ==
		pushes + movers + gravity + conveyors + currents);
	SG_ExternalForcePublicationDestroy(publication);
}

static void TestRejectsIdentityDriftAndRuntimeActors(void)
{
	mechanism_fixture_t fixture;
	sg_mechanism_capability_set_t *capabilities = NULL;
	sg_phase_catalog_publication_owner_t *phase_owner = NULL;
	sg_phase_catalog_publication_t *phases = NULL;
	sg_bsp_entity_semantics_publication_t *entities = NULL;
	sg_external_force_source_t source;
	sg_external_force_publication_t *publication = NULL;
	sg_external_force_audit_result_t audit;
	uint64_t physics_abi;

	CHECK_EXTERNAL(ExternalSourceInit(&fixture, &capabilities, &phase_owner,
		&phases, &entities, &source, 0));
	if (!entities || !phases || !capabilities)
		return;
	physics_abi = external_host_view.static_identity.physics_abi_id;
	external_host_view.static_identity.physics_abi_id++;
	CHECK_EXTERNAL(!SG_ExternalForcePublicationIssue(&source, &publication,
		&audit));
	CHECK_EXTERNAL(publication == NULL);
	CHECK_EXTERNAL(audit.code == SG_EXTERNAL_FORCE_AUDIT_IDENTITY_MISMATCH);
	external_host_view.static_identity.physics_abi_id = physics_abi;

	/* The only actor-only physics kind in the canonical entity schema is
	 * monster jump. Damage volumes and beams are also absent by construction. */
	CHECK_EXTERNAL(SG_ExternalForcePublicationIssue(&source, &publication,
		&audit));
	if (publication)
	{
		sg_external_force_view_t view;
		uint32_t index;

		CHECK_EXTERNAL(SG_ExternalForcePublicationRead(publication, &view));
		for (index = 0U; index < view.fact_count; index++)
		{
			sg_external_force_fact_t fact;

			CHECK_EXTERNAL(SG_ExternalForcePublicationFact(publication, index,
				&fact));
			CHECK_EXTERNAL(fact.kind >=
				SG_EXTERNAL_FORCE_TRIGGER_PUSH &&
				fact.kind < SG_EXTERNAL_FORCE_KIND_COUNT);
			CHECK_EXTERNAL(fact.source_entity_ordinal != 1U);
			CHECK_EXTERNAL(fact.source_entity_ordinal != 4U);
			CHECK_EXTERNAL(fact.source_entity_ordinal != 5U);
		}
	}
	SG_ExternalForcePublicationDestroy(publication);
	SG_BspEntitySemanticsPublicationDestroy(entities);
	SG_PhaseCatalogPublicationDestroy(phase_owner, phases);
	SG_PhaseCatalogPublicationOwnerDestroy(phase_owner);
	SG_MechanismCapabilityDestroy(fixture.capability_owner, capabilities);
	FixtureDestroy(&fixture);
}

int main(void)
{
	TestAllAcceptedMechanismForces();
	TestRejectsIdentityDriftAndRuntimeActors();
	if (external_failures)
	{
		fprintf(stderr, "%d external-force checks failed\n",
			external_failures);
		return 1;
	}
	puts("external-force publication checks passed");
	return 0;
}
