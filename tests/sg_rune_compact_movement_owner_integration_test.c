/* Reuse the builder's engine fixture, but link and execute the real compact
 * owner chain from builder through movement. */
#define main SG_EmbeddedBuilderFixtureMain
#define SG_RuneCompactGeometryRead SG_EmbeddedBuilderGeometryRead
#define SG_RuneCompactIdentityMatches SG_EmbeddedBuilderIdentityMatches
#define SG_HostLawConstructionRead SG_EmbeddedHostLawConstructionRead
#define SG_HostLawConstructionOwnerCopyBsp \
	SG_EmbeddedHostLawConstructionOwnerCopyBsp
#define SG_HostLawConstructionOwnerPmoveEvaluatorAcquire \
	SG_EmbeddedPmoveEvaluatorAcquire
#define SG_HostLawPmoveEvaluatorCurrent SG_EmbeddedPmoveEvaluatorCurrent
#define SG_HostLawPmoveEvaluatorRun SG_EmbeddedPmoveEvaluatorRun
#define SG_HostLawPmoveEvaluatorReplayFrame SG_EmbeddedPmoveEvaluatorReplayFrame
#define SG_HostLawPmoveEvaluatorDestroy SG_EmbeddedPmoveEvaluatorDestroy
#define SG_ConfigurationBuild SG_EmbeddedConfigurationBuild
#define SG_ConfigurationDestroy SG_EmbeddedConfigurationDestroy
#define SG_ConfigurationSemanticsBuild SG_EmbeddedConfigurationSemanticsBuild
#define SG_ConfigurationSemanticsDestroy \
	SG_EmbeddedConfigurationSemanticsDestroy
#define SG_StaticVisibilityBuild SG_EmbeddedStaticVisibilityBuild
#define SG_StaticVisibilityDestroy SG_EmbeddedStaticVisibilityDestroy
#define SG_StaticVisibilityDefaultLimits \
	SG_EmbeddedStaticVisibilityDefaultLimits
#define SG_StaticVisibilityAudit SG_EmbeddedStaticVisibilityAudit
#define SG_HostCollisionInit SG_EmbeddedHostCollisionInit
#define SG_HostCollisionWorldTransform SG_EmbeddedHostCollisionWorldTransform
#define SG_HostCollisionModelToWorldPoint \
	SG_EmbeddedHostCollisionModelToWorldPoint
#define SG_HostCollisionClassifyPose SG_EmbeddedHostCollisionClassifyPose
#define SG_HostCollisionModelPositiveAreaPolygonOverlap \
	SG_EmbeddedHostCollisionModelPositiveAreaPolygonOverlap
#define SG_HostCollisionTraceModel SG_EmbeddedHostCollisionTraceModel
#define SG_HostCollisionTransition SG_EmbeddedHostCollisionTransition
#define SG_HostCollisionPusherCarry SG_EmbeddedHostCollisionPusherCarry
int SG_EmbeddedBuilderFixtureMain(void);
#include "sg_rune_compact_builder_test.c"
#undef SG_StaticVisibilityDestroy
#undef SG_StaticVisibilityBuild
#undef SG_StaticVisibilityAudit
#undef SG_StaticVisibilityDefaultLimits
#undef SG_HostCollisionPusherCarry
#undef SG_HostCollisionTransition
#undef SG_HostCollisionTraceModel
#undef SG_HostCollisionModelPositiveAreaPolygonOverlap
#undef SG_HostCollisionClassifyPose
#undef SG_HostCollisionModelToWorldPoint
#undef SG_HostCollisionWorldTransform
#undef SG_HostCollisionInit
#undef SG_ConfigurationSemanticsDestroy
#undef SG_ConfigurationSemanticsBuild
#undef SG_ConfigurationDestroy
#undef SG_ConfigurationBuild
#undef SG_HostLawPmoveEvaluatorDestroy
#undef SG_HostLawPmoveEvaluatorReplayFrame
#undef SG_HostLawPmoveEvaluatorRun
#undef SG_HostLawPmoveEvaluatorCurrent
#undef SG_HostLawConstructionOwnerPmoveEvaluatorAcquire
#undef SG_HostLawConstructionOwnerCopyBsp
#undef SG_HostLawConstructionRead
#undef SG_RuneCompactGeometryRead
#undef SG_RuneCompactIdentityMatches
#undef main

#include "../slipgate/sg_rune_compact_movement_fields.h"
#include "../slipgate/sg_rune_compact_composer.h"
#include "../slipgate/sg_rune_compact_wire.h"

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out);
int SG_HostCollisionInit(sg_host_collision_authority_t *authority,
	const sg_bsp_world_t *world, const sg_rune_model_identity_t *identity,
	sg_host_collision_error_t *error_out);
sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out);
sg_host_law_result_t SG_HostLawConstructionOwnerPmoveEvaluatorAcquire(
	const sg_host_law_construction_t *construction,
	sg_host_law_pmove_evaluator_t **evaluator_out);
sg_host_law_result_t SG_HostLawPmoveEvaluatorCurrent(
	const sg_host_law_pmove_evaluator_t *evaluator);
sg_host_law_result_t SG_HostLawPmoveEvaluatorRun(
	const sg_host_law_pmove_evaluator_t *evaluator,
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawPmoveEvaluatorReplayFrame(
	const sg_host_law_pmove_evaluator_t *evaluator,
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out,
	sg_host_pmove_error_t *error_out);
void SG_HostLawPmoveEvaluatorDestroy(
	sg_host_law_pmove_evaluator_t *evaluator);
int SG_ConfigurationBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out);
void SG_ConfigurationDestroy(sg_configuration_space_t *space);
int SG_ConfigurationSemanticsBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_limits_t *limits,
	sg_configuration_semantics_t **semantics_out,
	sg_configuration_semantics_error_t *error_out);
void SG_ConfigurationSemanticsDestroy(
	sg_configuration_semantics_t *semantics);
int SG_StaticVisibilityBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_limits_t *limits,
	sg_static_visibility_t **visibility_out,
	sg_static_visibility_error_t *error_out);
void SG_StaticVisibilityDestroy(sg_static_visibility_t *visibility);
void SG_StaticVisibilityDefaultLimits(
	sg_static_visibility_limits_t *limits_out);
int SG_StaticVisibilityAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result_out);
void SG_RealStaticVisibilityDefaultLimits(
	sg_static_visibility_limits_t *limits_out);
int SG_RealStaticVisibilityBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_limits_t *limits,
	sg_static_visibility_t **visibility_out,
	sg_static_visibility_error_t *error_out);
int SG_RealStaticVisibilityAudit(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result_out);
void SG_RealStaticVisibilityDestroy(sg_static_visibility_t *visibility);

void SG_StaticVisibilityDefaultLimits(
	sg_static_visibility_limits_t *limits_out)
{
	SG_RealStaticVisibilityDefaultLimits(limits_out);
}

int SG_StaticVisibilityBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_limits_t *limits,
	sg_static_visibility_t **visibility_out,
	sg_static_visibility_error_t *error_out)
{
	return SG_RealStaticVisibilityBuild(authority, configuration, semantics,
		limits, visibility_out, error_out);
}

int SG_StaticVisibilityAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result_out)
{
	return SG_RealStaticVisibilityAudit(authority, configuration, semantics,
		visibility, result_out);
}

void SG_StaticVisibilityDestroy(sg_static_visibility_t *visibility)
{
	SG_RealStaticVisibilityDestroy(visibility);
}

void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);
void Com_Printf(char *format, ...);
int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);

game_import_t gi;
cvar_t *sv_gravity;
cvar_t *sv_maxvelocity;
cvar_t *want_funky_gravity;

static cvar_t integration_gravity;
static cvar_t integration_maxvelocity;
static cvar_t integration_funky_gravity;
static cvar_t integration_airaccelerate;

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	(void)start;
	(void)bite;
	VectorClear(velocity);
	return 0;
}

static cvar_t *IntegrationCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	return strcmp(name, "sv_airaccelerate") == 0 ?
		&integration_airaccelerate : NULL;
}

int SG_HostEnginePhysicsLaw(sg_rune_physics_parameters_t *law_out)
{
	if (law_out == NULL)
		return 0;
	memset(law_out, 0, sizeof(*law_out));
	law_out->gravity = integration_gravity.value;
	law_out->ground_acceleration = SG_HOST_ENGINE_GROUND_ACCELERATION;
	law_out->air_acceleration = integration_airaccelerate.value == 0.0f ?
		SG_HOST_ENGINE_AIR_ACCELERATION :
		SG_HOST_ENGINE_GROUND_ACCELERATION;
	law_out->water_acceleration = SG_HOST_ENGINE_WATER_ACCELERATION;
	law_out->hook_acceleration = SG_HOST_ENGINE_HOOK_ACCELERATION;
	law_out->external_acceleration = SG_HOST_ENGINE_EXTERNAL_ACCELERATION;
	law_out->water_drag = SG_HOST_ENGINE_WATER_DRAG;
	law_out->max_velocity = integration_maxvelocity.value;
	law_out->frame_ms = SG_HOST_ENGINE_FRAME_MS;
	law_out->substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return 1;
}

int SG_HostHookLiveCapture(sg_host_hook_law_t *law_out)
{
	if (law_out == NULL)
		return 0;
	SG_HostHookLawDefault(law_out);
	law_out->no_grapple_damage = 0U;
	return 1;
}

int SG_HostMechanismLiveCapture(sg_host_mechanism_law_t *law_out)
{
	if (law_out == NULL)
		return 0;
	SG_HostMechanismLawDefault(law_out);
	return 1;
}

int SG_HostEngineRuntimeAccepted(const sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return 0;
}

const sg_host_static_identity_t *SG_HostEngineRuntimeStaticIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return NULL;
}

static void IntegrationSetPoint(sg_rune_vec3_t *point, float x, float y,
	float z)
{
	point->value[0] = x;
	point->value[1] = y;
	point->value[2] = z;
}

static void IntegrationSetFace(sg_configuration_space_t *space,
	uint32_t index, float nx, float ny, float nz, float distance,
	uint32_t axis, uint32_t variant)
{
	sg_configuration_face_t *face = &space->faces[index];
	const uint32_t first_vertex = index * 3U;

	face->plane.normal[0] = nx;
	face->plane.normal[1] = ny;
	face->plane.normal[2] = nz;
	face->plane.distance = distance;
	face->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	face->plane.source_index = axis;
	face->plane.source_variant = variant;
	face->first_vertex = first_vertex;
	face->vertex_count = 3U;
	face->kind = SG_CONFIGURATION_FACE_FACET;
	IntegrationSetPoint(&space->vertices[first_vertex], 0.0f, 0.0f, 0.0f);
	IntegrationSetPoint(&space->vertices[first_vertex + 1U], 1.0f, 0.0f,
		0.0f);
	IntegrationSetPoint(&space->vertices[first_vertex + 2U], 0.0f, 1.0f,
		0.0f);
}

int SG_ConfigurationBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out)
{
	sg_configuration_space_t *space;
	sg_configuration_cell_t *cell;

	if (authority == NULL || limits == NULL || space_out == NULL ||
		*space_out != NULL || error_out == NULL) {
		if (error_out != NULL)
			error_out->code = SG_CONFIGURATION_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	space = calloc(1U, sizeof(*space));
	if (space == NULL)
		goto allocation_failed;
	space->cells = calloc(1U, sizeof(*space->cells));
	space->faces = calloc(6U, sizeof(*space->faces));
	space->vertices = calloc(18U, sizeof(*space->vertices));
	if (space->cells == NULL || space->faces == NULL ||
		space->vertices == NULL) {
		SG_ConfigurationDestroy(space);
		goto allocation_failed;
	}
	space->identity = authority->identity;
	space->cell_count = 1U;
	space->face_count = 6U;
	space->vertex_count = 18U;
	IntegrationSetPoint(&space->domain.mins, -8.0f, -8.0f, -8.0f);
	IntegrationSetPoint(&space->domain.maxs, 8.0f, 8.0f, 8.0f);
	IntegrationSetFace(space, 0U, 1.0f, 0.0f, 0.0f, 8.0f, 0U, 0U);
	IntegrationSetFace(space, 1U, -1.0f, 0.0f, 0.0f, 8.0f, 0U, 1U);
	IntegrationSetFace(space, 2U, 0.0f, 1.0f, 0.0f, 8.0f, 1U, 0U);
	IntegrationSetFace(space, 3U, 0.0f, -1.0f, 0.0f, 8.0f, 1U, 1U);
	IntegrationSetFace(space, 4U, 0.0f, 0.0f, 1.0f, 8.0f, 2U, 0U);
	IntegrationSetFace(space, 5U, 0.0f, 0.0f, -1.0f, 8.0f, 2U, 1U);
	cell = &space->cells[0];
	cell->stance = SG_RUNE_STANCE_STANDING;
	cell->face_count = 6U;
	IntegrationSetPoint(&cell->bounds.mins, -8.0f, -8.0f, -8.0f);
	IntegrationSetPoint(&cell->bounds.maxs, 8.0f, 8.0f, 8.0f);
	IntegrationSetPoint(&cell->interior_witness, 0.0f, 0.0f, 0.0f);
	cell->bsp_leaf.index = 1U;
	cell->bsp_area.index = 0U;
	cell->bsp_cluster.index = 0U;
	cell->witness_pose_flags = SG_CONFIGURATION_POSE_SUPPORTED;
	error_out->code = SG_CONFIGURATION_ERROR_NONE;
	*space_out = space;
	return 1;

allocation_failed:
	error_out->code = SG_CONFIGURATION_ERROR_OUT_OF_MEMORY;
	return 0;
}

void SG_ConfigurationDestroy(sg_configuration_space_t *space)
{
	if (space == NULL)
		return;
	free(space->stance_overlaps);
	free(space->portals);
	free(space->vertices);
	free(space->faces);
	free(space->cells);
	free(space);
}

int SG_ConfigurationSemanticsBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_limits_t *limits,
	sg_configuration_semantics_t **semantics_out,
	sg_configuration_semantics_error_t *error_out)
{
	sg_configuration_semantics_t *semantics;
	sg_configuration_semantic_region_t *region;
	uint32_t sample;

	if (authority == NULL || configuration == NULL || limits == NULL ||
		semantics_out == NULL || *semantics_out != NULL || error_out == NULL) {
		if (error_out != NULL)
			error_out->code =
				SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	semantics = calloc(1U, sizeof(*semantics));
	if (semantics == NULL)
		goto allocation_failed;
	semantics->regions = calloc(1U, sizeof(*semantics->regions));
	semantics->hook_surfaces = calloc(1U, sizeof(*semantics->hook_surfaces));
	semantics->hook_vertices = calloc(3U, sizeof(*semantics->hook_vertices));
	if (semantics->regions == NULL || semantics->hook_surfaces == NULL ||
		semantics->hook_vertices == NULL) {
		SG_ConfigurationSemanticsDestroy(semantics);
		goto allocation_failed;
	}
	semantics->identity = configuration->identity;
	semantics->region_count = 1U;
	semantics->hook_surface_count = 1U;
	semantics->hook_vertex_count = 3U;
	region = &semantics->regions[0];
	region->id = UINT64_C(1);
	region->cell = 0U;
	region->bounds = configuration->cells[0].bounds;
	region->interior_witness = configuration->cells[0].interior_witness;
	region->flags = SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
	for (sample = 0U; sample < 3U; sample++) {
		region->sample_leaves[sample] = 1U;
		region->sample_areas[sample] = 0U;
		region->sample_clusters[sample] = 0;
	}
	semantics->hook_surfaces[0].model = 0U;
	semantics->hook_surfaces[0].brush = 0U;
	semantics->hook_surfaces[0].brush_side = 0U;
	semantics->hook_surfaces[0].texinfo = 0U;
	semantics->hook_surfaces[0].normal[2] = 1.0f;
	semantics->hook_surfaces[0].first_vertex = 0U;
	semantics->hook_surfaces[0].vertex_count = 3U;
	semantics->hook_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
	IntegrationSetPoint(&semantics->hook_surfaces[0].bounds.mins, -16.0f,
		-16.0f, -0.125f);
	IntegrationSetPoint(&semantics->hook_surfaces[0].bounds.maxs, 16.0f,
		16.0f, 0.125f);
	IntegrationSetPoint(&semantics->hook_vertices[0], -16.0f, -16.0f, 0.0f);
	IntegrationSetPoint(&semantics->hook_vertices[1], 16.0f, -16.0f, 0.0f);
	IntegrationSetPoint(&semantics->hook_vertices[2], 0.0f, 16.0f, 0.0f);
	error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_NONE;
	*semantics_out = semantics;
	return 1;

allocation_failed:
	error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY;
	return 0;
}

void SG_ConfigurationSemanticsDestroy(
	sg_configuration_semantics_t *semantics)
{
	if (semantics == NULL)
		return;
	free(semantics->hook_vertices);
	free(semantics->hook_surfaces);
	free(semantics->boundaries);
	free(semantics->vertices);
	free(semantics->faces);
	free(semantics->regions);
	free(semantics);
}

static void CopyStaticGeometry(
	sg_rune_compact_static_geometry_view_t *target,
	const sg_rune_compact_geometry_view_t *source)
{
	memset(target, 0, sizeof(*target));
	target->identity = source->identity;
	target->cells = source->cells;
	target->cell_count = source->cell_count;
	target->facets = source->facets;
	target->facet_count = source->facet_count;
	target->incidences = source->incidences;
	target->incidence_count = source->incidence_count;
	target->cell_incidences = source->cell_incidences;
	target->cell_incidence_count = source->cell_incidence_count;
	target->vertices = source->vertices;
	target->vertex_count = source->vertex_count;
	target->portals = source->portals;
	target->portal_count = source->portal_count;
	target->source_surfaces = source->source_surfaces;
	target->source_surface_count = source->source_surface_count;
	target->source_surface_vertices = source->source_surface_vertices;
	target->source_surface_vertex_count = source->source_surface_vertex_count;
	target->compact_cells_for_configuration_cell =
		source->compact_cells_for_configuration_cell;
	target->compact_cells_for_configuration_cell_count =
		source->compact_cells_for_configuration_cell_count;
	target->configuration_cell_compact_cells =
		source->configuration_cell_compact_cells;
	target->configuration_cell_compact_cell_count =
		source->configuration_cell_compact_cell_count;
}

static int RecordsMatch(const void *left, const void *right, uint32_t count,
	size_t record_size)
{
	return count == 0U || (left != NULL && right != NULL &&
		memcmp(left, right, (size_t)count * record_size) == 0);
}

static int MovementProjectionMatches(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_movement_fields_view_t *view)
{
	return model != NULL && view != NULL &&
		model->movement_capability_count == view->capability_count &&
		model->movement_state_count == view->state_count &&
		model->movement_fiber_count == view->fiber_count &&
		model->movement_hook_target_count == view->hook_target_count &&
		model->movement_fiber_function_ref_count ==
			view->fiber_function_ref_count &&
		model->movement_angular_schedule_count == view->angular_schedule_count &&
		model->movement_pmove_behavior_fingerprint ==
			view->pmove_behavior_fingerprint &&
		model->movement_host_level_generation == view->host_level_generation &&
		memcmp(&model->movement_pmove_abi, &view->pmove_abi,
			sizeof(model->movement_pmove_abi)) == 0 &&
		RecordsMatch(model->movement_capabilities, view->capabilities,
			view->capability_count, sizeof(*view->capabilities)) &&
		RecordsMatch(model->movement_states, view->states, view->state_count,
			sizeof(*view->states)) &&
		RecordsMatch(model->movement_fibers, view->fibers, view->fiber_count,
			sizeof(*view->fibers)) &&
		RecordsMatch(model->movement_hook_targets, view->hook_targets,
			view->hook_target_count, sizeof(*view->hook_targets)) &&
		RecordsMatch(model->movement_angular_schedules, view->angular_schedules,
			view->angular_schedule_count, sizeof(*view->angular_schedules));
}

static int CheckHookTargetModelContract(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity)
{
	sg_rune_compact_model_t mutated;
	sg_rune_analytic_function_index_t *refs;
	sg_rune_compact_error_t error = { 0 };
	uint32_t target_index;
	int rejected;

	if (model == NULL || identity == NULL ||
		model->movement_hook_target_count == 0U ||
		model->movement_fiber_function_ref_count == 0U)
		return 0;
	refs = malloc((size_t)model->movement_fiber_function_ref_count *
		sizeof(*refs));
	if (refs == NULL)
		return 0;
	memcpy(refs, model->movement_fiber_function_refs,
		(size_t)model->movement_fiber_function_ref_count * sizeof(*refs));
	for (target_index = 0U;
		target_index < model->movement_hook_target_count; target_index++) {
		const sg_rune_compact_movement_hook_target_t *target =
			&model->movement_hook_targets[target_index];
		const sg_rune_analytic_function_span_t spans[6] = {
			target->functions.bolt, target->functions.body,
			target->functions.pull, target->functions.release,
			target->functions.coast, target->functions.relaunch
		};
		uint32_t phase;

		for (phase = 0U; phase < 6U; phase++)
			if (spans[phase].count >= 2U) {
				refs[spans[phase].first + 1U] = refs[spans[phase].first];
				goto mutated_reference;
			}
	}
	free(refs);
	return 0;

mutated_reference:
	mutated = *model;
	mutated.movement_fiber_function_refs = refs;
	rejected = !SG_RuneCompactModelValidateBound(&mutated, identity, &error) &&
		error.code == SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD &&
		error.domain == SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD &&
		error.record == target_index;
	free(refs);
	return rejected;
}

static int CheckRealConstructionEvidence(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene)
{
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error = SG_HOST_PMOVE_ERROR_NONE;
	sg_host_pmove_substep_t substeps[
		SG_HOST_ENGINE_FRAME_MS / SG_HOST_ENGINE_PMOVE_SUBSTEP_MS];
	sg_host_pmove_trace_t *traces;
	sg_host_pmove_replay_workspace_t workspace;
	sg_host_pmove_replay_t replay;
	sg_host_collision_trace_t collision;
	const float start[3] = { 1.0f, 0.0f, 0.0f };
	const float end[3] = { -1.0f, 0.0f, 0.0f };
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	int valid = 0;

	if (construction == NULL || scene == NULL)
		return 0;
	traces = calloc(SG_HOST_ENGINE_PMOVE_REPLAY_TRACE_LIMIT,
		sizeof(*traces));
	if (traces == NULL)
		return 0;
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.gravity = 800;
	request.previous_state = request.state;
	memset(&result, 0, sizeof(result));
	memset(&workspace, 0, sizeof(workspace));
	workspace.substeps = substeps;
	workspace.substep_capacity = sizeof(substeps) / sizeof(substeps[0]);
	workspace.traces = traces;
	workspace.trace_capacity = SG_HOST_ENGINE_PMOVE_REPLAY_TRACE_LIMIT;
	memset(&replay, 0, sizeof(replay));
	memset(&collision, 0, sizeof(collision));
	if (SG_HostLawConstructionPmove(construction, scene, &request, &result,
			&pmove_error).status != SG_HOST_LAW_OK ||
		pmove_error != SG_HOST_PMOVE_ERROR_NONE ||
		result.evaluated_steps !=
			SG_HOST_ENGINE_FRAME_MS / SG_HOST_ENGINE_PMOVE_SUBSTEP_MS ||
		result.elapsed_ms != SG_HOST_ENGINE_FRAME_MS ||
		result.physics_abi_id != SG_HOST_ENGINE_PMOVE_ABI_ID ||
		SG_HostLawConstructionReplayFrame(construction, scene, &request,
			&workspace, &replay, &pmove_error).status != SG_HOST_LAW_OK ||
		pmove_error != SG_HOST_PMOVE_ERROR_NONE || replay.substeps != substeps ||
		replay.substep_count !=
			SG_HOST_ENGINE_FRAME_MS / SG_HOST_ENGINE_PMOVE_SUBSTEP_MS ||
		replay.traces != traces || replay.trace_count == 0U ||
		replay.physics_abi_id != SG_HOST_ENGINE_PMOVE_ABI_ID ||
		replay.frame_ms != SG_HOST_ENGINE_FRAME_MS ||
		replay.substep_ms != SG_HOST_ENGINE_PMOVE_SUBSTEP_MS ||
		SG_HostLawConstructionCollisionTrace(construction, scene, start, zero,
			zero, end, SG_HOST_CONTENTS_SOLID, &collision).status !=
			SG_HOST_LAW_OK || !isfinite(collision.fraction) ||
		collision.fraction < 0.0f || collision.fraction > 1.0f)
		goto done;
	valid = 1;
done:
	free(traces);
	return valid;
}

static int PmoveFibersAbsent(
	const sg_rune_compact_movement_fields_view_t *view)
{
	uint32_t index;

	if (view == NULL)
		return 0;
	for (index = 0U; index < view->fiber_count; index++)
		if (view->fibers[index].kind == SG_RUNE_MOVEMENT_FIBER_PMOVE)
			return 0;
	return 1;
}

static int ExpandRealBspLump(sg_bsp_lump_t lump, uint32_t additional_bytes)
{
	const uint32_t old_end = real_bsp_offsets[lump] + real_bsp_lengths[lump];
	uint32_t index;

	if (additional_bytes == 0U)
		return 1;
	if (old_end > real_bsp_size || real_bsp_size >
		REAL_BSP_CAPACITY - additional_bytes)
		return 0;
	memmove(real_bsp_bytes + old_end + additional_bytes,
		real_bsp_bytes + old_end, real_bsp_size - old_end);
	for (index = 0U; index < SG_BSP_LUMP_COUNT; index++)
		if (index != (uint32_t)lump && real_bsp_lengths[index] != 0U &&
			real_bsp_offsets[index] >= old_end)
			real_bsp_offsets[index] += additional_bytes;
	real_bsp_lengths[lump] += additional_bytes;
	real_bsp_size += additional_bytes;
	for (index = 0U; index < SG_BSP_LUMP_COUNT; index++) {
		WriteU32(real_bsp_bytes + 8U + index * 8U,
			real_bsp_offsets[index]);
		WriteU32(real_bsp_bytes + 12U + index * 8U,
			real_bsp_lengths[index]);
	}
	return 1;
}

static int BuildMovementBsp(void)
{
	uint8_t *record;
	static const float normals[6][3] = {
		{ 0.0f, 0.0f, 1.0f },
		{ 1.0f, 0.0f, 0.0f },
		{ -1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, -1.0f, 0.0f },
		{ 0.0f, 0.0f, -1.0f }
	};
	static const float distances[6] = {
		0.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f
	};
	uint32_t plane;

	BuildRealBsp();
	if (real_bsp_lengths[SG_BSP_LUMP_PLANES] != 20U ||
		real_bsp_lengths[SG_BSP_LUMP_BRUSH_SIDES] != 4U ||
		!ExpandRealBspLump(SG_BSP_LUMP_PLANES, 5U * 20U) ||
		!ExpandRealBspLump(SG_BSP_LUMP_BRUSH_SIDES, 5U * 4U))
		return 0;
	record = real_bsp_bytes + real_bsp_offsets[SG_BSP_LUMP_PLANES];
	for (plane = 0U; plane < 6U; plane++) {
		WriteFloat(record + plane * 20U + 0U, normals[plane][0]);
		WriteFloat(record + plane * 20U + 4U, normals[plane][1]);
		WriteFloat(record + plane * 20U + 8U, normals[plane][2]);
		WriteFloat(record + plane * 20U + 12U, distances[plane]);
		WriteI32(record + plane * 20U + 16U,
			normals[plane][0] != 0.0f ? 0 :
			normals[plane][1] != 0.0f ? 1 : 2);
	}
	record = real_bsp_bytes + real_bsp_offsets[SG_BSP_LUMP_BRUSHES];
	WriteI32(record + 4U, 6);
	record = real_bsp_bytes + real_bsp_offsets[SG_BSP_LUMP_MODELS];
	WriteI32(record + 84U, -2);
	record = real_bsp_bytes + real_bsp_offsets[SG_BSP_LUMP_BRUSH_SIDES];
	for (plane = 0U; plane < 6U; plane++) {
		WriteU16(record + plane * 4U, (uint16_t)plane);
		WriteI16(record + plane * 4U + 2U, 0);
	}
	return 1;
}

int main(void)
{
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_construction_t *construction = NULL;
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error = { 0 };
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error = SG_HOST_COLLISION_ERROR_NONE;
	sg_host_collision_scene_t collision_scene;
	sg_rune_model_identity_t collision_identity;
	sg_host_static_identity_t static_identity;
	sg_rune_compact_builder_input_t builder_input;
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_response_partition_t *response = NULL;
	sg_rune_compact_mechanisms_t *mechanisms = NULL;
	sg_rune_compact_static_materializer_t *static_owner = NULL;
	sg_rune_compact_movement_fields_t *movement = NULL;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_field_t *weapon_field = NULL;
	sg_rune_compact_composer_t *composer = NULL;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	size_t image_written = 0U;
	sg_rune_compact_builder_error_t builder_error = { 0 };
	sg_rune_compact_geometry_error_t geometry_error = { 0 };
	sg_rune_compact_response_error_t response_error = { 0 };
	sg_rune_compact_mechanisms_error_t mechanisms_error = { 0 };
	sg_rune_compact_static_materializer_error_t static_error = { 0 };
	sg_rune_compact_movement_fields_error_t movement_error = { 0 };
	sg_rune_compact_weapon_relations_error_t relation_error = { 0 };
	sg_rune_compact_weapon_field_error_t weapon_error = { 0 };
	sg_rune_compact_composer_error_t composer_error = { 0 };
	sg_rune_compact_wire_error_t wire_error = { 0 };
	sg_rune_compact_error_t model_error = { 0 };
	sg_rune_compact_builder_owner_view_t owner;
	sg_rune_compact_builder_view_t builder_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_movement_fields_view_t movement_view;
	sg_rune_compact_weapon_field_input_t weapon_input;
	sg_rune_compact_static_materializer_input_t static_input;
	sg_rune_compact_movement_fields_input_t movement_input;
	sg_rune_compact_wire_info_t wire_info;
	const sg_rune_compact_model_t *model;
	const sg_rune_compact_model_t *decoded_model;
	int success = 0;
	unsigned stage = 0U;

	memset(&gi, 0, sizeof(gi));
	memset(&integration_gravity, 0, sizeof(integration_gravity));
	memset(&integration_maxvelocity, 0, sizeof(integration_maxvelocity));
	memset(&integration_funky_gravity, 0,
		sizeof(integration_funky_gravity));
	memset(&integration_airaccelerate, 0,
		sizeof(integration_airaccelerate));
	gi.Pmove = Pmove;
	gi.cvar = IntegrationCvar;
	integration_gravity.value = 800.0f;
	integration_maxvelocity.value = 2000.0f;
	sv_gravity = &integration_gravity;
	sv_maxvelocity = &integration_maxvelocity;
	want_funky_gravity = &integration_funky_gravity;
	memset(&authority, 0, sizeof(authority));
	memset(&collision_scene, 0, sizeof(collision_scene));
	memset(&collision_identity, 0, sizeof(collision_identity));
	memset(&static_identity, 0, sizeof(static_identity));
	memset(&builder_input, 0, sizeof(builder_input));
	if (!BuildMovementBsp())
		return 2;
	SetHost();
	host_view.geometry.plane_count = 6U;
	host_view.geometry.brush_side_count = 6U;
	/* Make the authenticated world face border the non-solid leaf.  The compact
	 * response path then binds a world patch whose exact source surface is
	 * authoritative even though its parent compact facet is NONE. */
	WriteI32(real_bsp_bytes + real_bsp_offsets[SG_BSP_LUMP_NODES] + 4U, -2);
	WriteI32(real_bsp_bytes + real_bsp_offsets[SG_BSP_LUMP_NODES] + 8U, -1);
	if (!SG_BspWorldContentIdentity(real_bsp_bytes, (size_t)real_bsp_size,
			&host_view.host_static_identity.bsp_identity) ||
		!SG_BspWorldEngineChecksum(real_bsp_bytes, (size_t)real_bsp_size,
			&host_view.host_static_identity.engine_checksum))
		return 2;
	host_view.geometry.bsp_identity = host_view.host_static_identity.bsp_identity;
	host_view.geometry.engine_checksum =
		host_view.host_static_identity.engine_checksum;
	host_view.laws.bsp_identity = host_view.host_static_identity.bsp_identity;
	host_view.host_static_identity.physics.gravity = 800.0f;
	host_view.host_static_identity.physics.ground_acceleration =
		SG_HOST_ENGINE_GROUND_ACCELERATION;
	host_view.host_static_identity.physics.air_acceleration =
		SG_HOST_ENGINE_AIR_ACCELERATION;
	host_view.host_static_identity.physics.water_acceleration =
		SG_HOST_ENGINE_WATER_ACCELERATION;
	host_view.host_static_identity.physics.hook_acceleration =
		SG_HOST_ENGINE_HOOK_ACCELERATION;
	host_view.host_static_identity.physics.external_acceleration =
		SG_HOST_ENGINE_EXTERNAL_ACCELERATION;
	host_view.host_static_identity.physics.water_drag =
		SG_HOST_ENGINE_WATER_DRAG;
	host_view.host_static_identity.physics.max_velocity = 2000.0f;
	host_view.host_static_identity.physics.frame_ms = 100U;
	host_view.host_static_identity.physics.substep_ms = 25U;
	host_view.host_static_identity.crouching_hull.maxs.value[2] = 4.0f;
	host_view.laws.static_identity = host_view.host_static_identity;
	host_view.laws.pmove_abi.version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	host_view.laws.pmove_abi.game_api_version = 1U;
	host_view.laws.pmove_abi.import_size = 1U;
	host_view.laws.pmove_abi.pmove_offset = 1U;
	host_view.laws.pmove_abi.pmove_size = (uint32_t)sizeof(pmove_t);
	host_view.laws.pmove_abi.state_size = (uint32_t)sizeof(pmove_state_t);
	host_view.laws.pmove_abi.command_size = (uint32_t)sizeof(usercmd_t);
	host_view.laws.pmove_abi.fraction_bits = SG_HOST_ENGINE_PMOVE_FRACTION_BITS;
	host_view.laws.pmove_abi.substep_ms = 25U;
	host_view.laws.pmove_abi.identity = SG_HOST_ENGINE_PMOVE_ABI_ID;
	host_view.laws.pmove_behavior_fingerprint = SG_HOST_ENGINE_PMOVE_ABI_ID;
	SG_HostHookLawDefault(&host_view.laws.hook);
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	host_view.laws.hook_fire_speed = host_view.laws.hook.fire_speed;
	host_view.laws.hook_pull_speed = host_view.laws.hook.pull_speed;
	host_view.laws.hook_initial_damage = host_view.laws.hook.initial_damage;
	host_view.laws.hook_attached_damage = host_view.laws.hook.attached_damage;
	host_view.laws.hook_health = host_view.laws.hook.projectile_health;
	host_view.laws.maxvelocity =
		host_view.host_static_identity.physics.max_velocity;
	host_view.laws.hook_law_id = host_view.laws.hook.identity;
	host_view.laws.mechanism_law_id = host_view.laws.mechanism.identity;
	host_view.host_static_identity.physics_abi_id =
		SG_HOST_ENGINE_PMOVE_ABI_ID;
	host_view.laws.static_identity.physics_abi_id =
		SG_HOST_ENGINE_PMOVE_ABI_ID;
	SetSourceAuthority();
	static_identity = host_view.host_static_identity;
	collision_identity.bsp_content_id = UINT64_C(1);
	collision_identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	collision_identity.standing_hull = static_identity.standing_hull;
	collision_identity.crouching_hull = static_identity.crouching_hull;
	collision_identity.physics = static_identity.physics;
	if (!SG_BspWorldLoadMemory(real_bsp_bytes, (size_t)real_bsp_size,
			&world, &bsp_error) ||
		!SG_HostCollisionInit(&authority, world, &collision_identity,
			&collision_error) ||
		SG_HostLawPublicationOwnerIssueStatic(&static_identity,
			&publication).status != SG_HOST_LAW_OK ||
		SG_HostLawPublicationOwnerConstructionIssue(publication, &authority,
			&construction).status != SG_HOST_LAW_OK ||
		SG_HostLawConstructionRead(construction, &host_view).status !=
			SG_HOST_LAW_OK ||
		!CheckRealConstructionEvidence(construction, &collision_scene))
		goto done;
	/* The source snapshot must bind the real construction law view, not the
	 * builder fixture's provisional host record used to seed the BSP identity. */
	SetSourceAuthority();
	builder_input = Input(construction);
	stage = 1U;
	if (!SG_RuneCompactBuilderBuild(&builder_input, &builder, &builder_error) ||
		!SG_RuneCompactBuilderOwnerRead(builder, &owner) ||
		!SG_RuneCompactBuilderRead(builder, &builder_view))
		goto done;
	stage = 10U;
	if (!SG_RuneCompactGeometryMaterialize(builder, NULL, &geometry,
			&geometry_error) ||
		!SG_RuneCompactGeometryRead(geometry, &geometry_view))
		goto done;
	stage = 11U;
	if (!SG_RuneCompactResponsePartitionBuild(builder, geometry, NULL, &response,
			&response_error))
		goto done;
	stage = 12U;
	if (!SG_RuneCompactMechanismsMaterialize(builder, geometry, &mechanisms,
			&mechanisms_error))
		goto done;
	stage = 2U;
	memset(&static_input, 0, sizeof(static_input));
	CopyStaticGeometry(&static_input.geometry, &geometry_view);
	static_input.entities = owner.entity_semantics;
	static_input.configuration = owner.semantics;
	static_input.visibility = owner.visibility;
	static_input.mechanisms = mechanisms;
	if (!SG_RuneCompactStaticMaterializerBuild(&static_input, &static_owner,
		&static_error))
		goto done;
	stage = 3U;
	memset(&movement_input, 0, sizeof(movement_input));
	movement_input.builder = builder;
	movement_input.host_owner = construction;
	movement_input.geometry_owner = geometry;
	movement_input.response_owner = response;
	movement_input.mechanisms_owner = mechanisms;
	movement_input.static_owner = static_owner;
	movement_input.collision_scene = &collision_scene;
	if (!SG_RuneCompactMovementFieldsBuild(&movement_input, &movement,
			&movement_error) ||
		!SG_RuneCompactMovementFieldsRead(movement, &movement_view) ||
		movement_view.capability_count == 0U ||
		movement_view.state_count == 0U ||
		movement_view.fiber_count == 0U ||
		movement_view.hook_target_count == 0U ||
		movement_view.fiber_function_ref_count == 0U ||
		!PmoveFibersAbsent(&movement_view))
		goto done;
	stage = 4U;
	if (!SG_RuneCompactWeaponRelationsBuild(builder, geometry, response,
			&relations, &relation_error))
		goto done;
	stage = 5U;
	memset(&weapon_input, 0, sizeof(weapon_input));
	weapon_input.identity = &builder_view.identity;
	weapon_input.compact_profiles = builder_view.weapon_profiles;
	weapon_input.resolved_profiles = builder_view.resolved_weapon_profiles;
	weapon_input.profile_count = builder_view.weapon_profile_count;
	weapon_input.weapon_law = owner.weapon_law;
	weapon_input.physics_abi_id = builder_view.identity.physics_abi_id;
	weapon_input.weapon_law_id = builder_view.identity.weapon_law_id;
	weapon_input.relations_owner = relations;
	if (SG_RuneCompactWeaponFieldBuild(&weapon_input, &weapon_field,
			&weapon_error) != SG_RUNE_COMPACT_WEAPON_FIELD_OK ||
		!SG_RuneCompactComposerBuild(builder, geometry, mechanisms, static_owner,
			movement, relations, weapon_field, &composer, &composer_error))
		goto done;
	stage = 6U;
	model = SG_RuneCompactComposerModel(composer);
	if (model == NULL ||
		!SG_RuneCompactModelValidateBound(model, &builder_view.identity,
			&model_error) ||
		!MovementProjectionMatches(model, &movement_view) ||
		(model->response.candidate_group_count == 0U &&
			model->response.fact_count == 0U))
		goto done;
	if (!CheckHookTargetModelContract(model, &builder_view.identity))
		goto done;
	stage = 7U;
	if (!SG_RuneCompactWireMeasure(model, &image_size, &wire_error))
		goto done;
	stage = 8U;
	image = malloc(image_size);
	if (image == NULL ||
		!SG_RuneCompactWireEncode(model, image, image_size, &image_written,
			&wire_error) || image_written != image_size ||
		!SG_RuneCompactWireInspect(image, image_size, &wire_info,
			&wire_error) ||
		wire_info.wire_version != SG_RUNE_COMPACT_WIRE_VERSION ||
		wire_info.counts[
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES] !=
			movement_view.capability_count ||
		wire_info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES] !=
			movement_view.state_count ||
		wire_info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS] !=
			movement_view.fiber_count ||
		wire_info.counts[
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS] !=
			movement_view.hook_target_count ||
		wire_info.counts[
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS] !=
			movement_view.fiber_function_ref_count ||
		wire_info.counts[
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES] !=
			movement_view.angular_schedule_count ||
		!SG_RuneCompactWireDecode(image, image_size, &builder_view.identity,
			&decoded, &wire_error))
		goto done;
	stage = 9U;
	decoded_model = SG_RuneCompactWireModel(decoded);
	if (decoded_model == NULL ||
		!SG_RuneCompactModelValidateBound(decoded_model,
			&builder_view.identity, &model_error) ||
		!MovementProjectionMatches(decoded_model, &movement_view))
		goto done;
	success = 1;
done:
	if (!success)
		fprintf(stderr,
			"real movement owner chain failed at %u: builder=%u/%u/%llu/%llu geometry=%u/%u/%u response=%u/%u/%u mechanisms=%u static=%u/%u/%u movement=%u/%u/%llu/%llu relations=%u/%u/%u weapon=%u/%u composer=%u/%u/%u wire=%u/%u/%u wire_model=%u/%u/%u model=%u/%u/%u\n",
			stage,
			(unsigned)builder_error.code, (unsigned)builder_error.record,
			(unsigned long long)builder_error.expected,
			(unsigned long long)builder_error.observed,
			(unsigned)geometry_error.code,
			(unsigned)geometry_error.domain, (unsigned)geometry_error.record,
			(unsigned)response_error.code, (unsigned)response_error.domain,
			(unsigned)response_error.record, (unsigned)mechanisms_error.code,
			(unsigned)static_error.code, (unsigned)static_error.domain,
			(unsigned)static_error.record, (unsigned)movement_error.code,
			(unsigned)movement_error.record,
			(unsigned long long)movement_error.expected,
			(unsigned long long)movement_error.observed,
			(unsigned)relation_error.code, (unsigned)relation_error.domain,
			(unsigned)relation_error.record, (unsigned)weapon_error.status,
			(unsigned)weapon_error.record, (unsigned)composer_error.code,
			(unsigned)composer_error.domain, (unsigned)composer_error.record,
			(unsigned)wire_error.code, (unsigned)wire_error.section,
			(unsigned)wire_error.record,
			(unsigned)wire_error.model_error.code,
			(unsigned)wire_error.model_error.domain,
			(unsigned)wire_error.model_error.record,
			(unsigned)model_error.code,
			(unsigned)model_error.domain, (unsigned)model_error.record);
	SG_RuneCompactWireDestroy(decoded);
	free(image);
	SG_RuneCompactComposerDestroy(composer);
	SG_RuneCompactWeaponFieldDestroy(weapon_field);
	SG_RuneCompactWeaponRelationsDestroy(relations);
	SG_RuneCompactMovementFieldsDestroy(movement);
	SG_RuneCompactStaticMaterializerDestroy(static_owner);
	SG_RuneCompactMechanismsDestroy(mechanisms);
	SG_RuneCompactResponsePartitionDestroy(response);
	SG_RuneCompactGeometryDestroy(geometry);
	SG_RuneCompactBuilderDestroy(builder);
	SG_HostLawConstructionDestroy(construction);
	SG_HostLawPublicationOwnerDestroy(publication);
	SG_BspWorldDestroy(world);
	if (!success)
		return 1;
	puts("sg_rune_compact_movement_owner_integration: ok");
	return 0;
}
