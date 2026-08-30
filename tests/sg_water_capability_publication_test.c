#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_water_capability_fixture.h"
#include "slipgate/sg_host_engine_pmove.h"
#include "slipgate/sg_host_engine_runtime.h"
#include "slipgate/sg_host_law_publication_private.h"
#include "slipgate/sg_phase_catalog_internal.h"
#include "slipgate/sg_water_capability_publication.h"
#include "slipgate/sg_action_contract.generated.h"

#ifndef q_exported
#define q_exported
#endif
#include "game.h"
#include "slipgate/sg_hooks.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

sg_host_t sg_host;
game_import_t gi;
cvar_t *sv_gravity;
cvar_t *sv_maxvelocity;
cvar_t *want_funky_gravity;
cvar_t *ctfflags;

static cvar_t gravity_cvar;
static cvar_t maxvelocity_cvar;
static cvar_t funky_gravity_cvar;
static cvar_t airaccelerate_cvar;
static cvar_t ctf_flags_cvar;
static uint32_t bsp_calls;
static uint32_t configuration_calls;
static uint32_t semantics_calls;
static int reject_bsp;
static int reject_configuration;
static int reject_semantics;
static sg_phase_mover_support_provider_payload_t empty_phase_provider;

static void Set3(float value[3], float x, float y, float z);

int SG_AuthorityTokenMint(uintptr_t *token_out)
{
	static uintptr_t next_token = (uintptr_t)UINT64_C(0x5741544552000000);

	if (!token_out || next_token == UINTPTR_MAX)
		return 0;
	next_token++;
	*token_out = next_token;
	return 1;
}

int SG_HostEnginePmoveABI(sg_host_engine_pmove_abi_t *abi_out)
{
	if (!abi_out || !gi.Pmove)
		return 0;
	memset(abi_out, 0, sizeof(*abi_out));
	abi_out->version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	abi_out->game_api_version = GAME_API_VERSION;
	abi_out->import_size = (uint32_t)sizeof(game_import_t);
	abi_out->pmove_offset = (uint32_t)offsetof(game_import_t, Pmove);
	abi_out->pmove_size = (uint32_t)sizeof(pmove_t);
	abi_out->state_size = (uint32_t)sizeof(pmove_state_t);
	abi_out->command_size = (uint32_t)sizeof(usercmd_t);
	abi_out->fraction_bits = SG_HOST_ENGINE_PMOVE_FRACTION_BITS;
	abi_out->substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	abi_out->identity = SG_HOST_ENGINE_PMOVE_ABI_ID;
	return 1;
}

int SG_HostEnginePmoveBindingCapture(
	sg_host_engine_pmove_binding_t *binding_out)
{
	if (!binding_out || !gi.Pmove)
		return 0;
	binding_out->entry = gi.Pmove;
	binding_out->owner = &gi;
	return 1;
}

int SG_HostEnginePmoveBindingCurrent(
	const sg_host_engine_pmove_binding_t *binding)
{
	return binding && binding->entry == gi.Pmove && binding->owner == &gi;
}

int SG_HostEnginePhysicsLaw(sg_rune_physics_parameters_t *law_out)
{
	if (!law_out)
		return 0;
	memset(law_out, 0, sizeof(*law_out));
	law_out->gravity = gravity_cvar.value;
	law_out->ground_acceleration = SG_HOST_ENGINE_GROUND_ACCELERATION;
	law_out->air_acceleration = SG_HOST_ENGINE_AIR_ACCELERATION;
	law_out->water_acceleration = SG_HOST_ENGINE_WATER_ACCELERATION;
	law_out->hook_acceleration = SG_HOST_ENGINE_HOOK_ACCELERATION;
	law_out->external_acceleration = SG_HOST_ENGINE_EXTERNAL_ACCELERATION;
	law_out->water_drag = SG_HOST_ENGINE_WATER_DRAG;
	law_out->max_velocity = maxvelocity_cvar.value;
	law_out->frame_ms = SG_HOST_ENGINE_FRAME_MS;
	law_out->substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return 1;
}

int SG_HostEngineHullProfiles(sg_rune_hull_profile_t *standing_out,
	sg_rune_hull_profile_t *crouching_out)
{
	if (!standing_out || !crouching_out)
		return 0;
	memset(standing_out, 0, sizeof(*standing_out));
	memset(crouching_out, 0, sizeof(*crouching_out));
	Set3(standing_out->mins.value, -16.0f, -16.0f, -24.0f);
	Set3(standing_out->maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(crouching_out->mins.value, -16.0f, -16.0f, -24.0f);
	Set3(crouching_out->maxs.value, 16.0f, 16.0f, 4.0f);
	return 1;
}

const sg_host_static_identity_t *SG_HostEngineRuntimeStaticIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return NULL;
}

int SG_HostEngineRuntimeAccepted(const sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return 0;
}

int SG_HostHookLiveCapture(sg_host_hook_law_t *law_out)
{
	if (!law_out)
		return 0;
	SG_HostHookLawDefault(law_out);
	law_out->no_grapple_damage = 0U;
	return 1;
}

int SG_HostMechanismLiveCapture(sg_host_mechanism_law_t *law_out)
{
	if (!law_out)
		return 0;
	SG_HostMechanismLawDefault(law_out);
	return 1;
}

int SG_PhaseMoverSupportProviderHeaderValid(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider)
{
	return owner != NULL && provider != NULL &&
		empty_phase_provider.completion == SG_PHASE_CATALOG_PROVEN_EMPTY &&
		empty_phase_provider.verifier_identity != 0U;
}

sg_phase_mover_support_provider_payload_t *
SG_PhaseMoverSupportProviderPayload(
	const sg_phase_mover_support_provider_owner_t *owner,
	const sg_phase_mover_support_provider_t *provider)
{
	return owner != NULL && provider != NULL ? &empty_phase_provider : NULL;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);

static cvar_t *TestCvar(const char *name, const char *value, int flags)
{
	(void)value;
	(void)flags;
	return strcmp(name, "sv_airaccelerate") == 0 ? &airaccelerate_cvar : NULL;
}

static cvar_t *EngineTestCvar(char *name, char *value, int flags)
{
	return TestCvar(name, value, flags);
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	(void)start;
	(void)bite;
	VectorClear(velocity);
	return 0;
}

/* The focused test isolates the prerequisite boundary. The real, exhaustive
 * prerequisite implementations run in their own contract gates. */
int SG_BspCompletenessProve(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_bsp_completeness_result_t *result_out)
{
	bsp_calls++;
	memset(result_out, 0, sizeof(*result_out));
	result_out->record = SG_CONFIGURATION_INDEX_NONE;
	result_out->code = reject_bsp ? SG_BSP_COMPLETENESS_INVALID_CELL :
		SG_BSP_COMPLETENESS_OK;
	result_out->expected_cells = space->cell_count;
	result_out->represented_cells = space->cell_count;
	result_out->proved_cells = space->cell_count;
	result_out->expected_portals = space->portal_count;
	result_out->represented_portals = space->portal_count;
	result_out->proved_portals = space->portal_count;
	result_out->lattice_solve_calls = UINT64_C(11);
	result_out->lattice_constraints = UINT64_C(17);
	result_out->lattice_maximum_binary_shift = 3U;
	return authority != NULL && !reject_bsp;
}

int SG_ConfigurationAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_configuration_audit_result_t *result_out)
{
	configuration_calls++;
	memset(result_out, 0, sizeof(*result_out));
	result_out->record = SG_CONFIGURATION_INDEX_NONE;
	result_out->code = reject_configuration ?
		SG_CONFIGURATION_AUDIT_INVALID_CERTIFICATE : SG_CONFIGURATION_AUDIT_OK;
	result_out->proved_cells = space->cell_count;
	result_out->proved_portals = space->portal_count;
	result_out->boundary_witnesses = UINT64_C(23);
	result_out->lattice_solve_calls = UINT64_C(29);
	result_out->lattice_constraints = UINT64_C(31);
	result_out->lattice_maximum_binary_shift = 5U;
	return authority != NULL && !reject_configuration;
}

int SG_ConfigurationSemanticsAudit(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *result_out)
{
	semantics_calls++;
	memset(result_out, 0, sizeof(*result_out));
	result_out->record = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
	result_out->code = reject_semantics ?
		SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH :
		SG_CONFIGURATION_SEMANTICS_AUDIT_OK;
	result_out->lattice_solve_calls = UINT64_C(37);
	result_out->lattice_constraints = UINT64_C(41);
	result_out->lattice_maximum_binary_shift = 7U;
	return authority != NULL && configuration != NULL && semantics != NULL &&
		!reject_semantics;
}

static void ResetPreflight(void)
{
	bsp_calls = 0U;
	configuration_calls = 0U;
	semantics_calls = 0U;
	reject_bsp = 0;
	reject_configuration = 0;
	reject_semantics = 0;
}

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void ProductionPmove(pmove_t *pmove)
{
	WaterFixturePmove(pmove);
	if ((pmove->watertype & MASK_WATER) == 0)
		pmove->watertype = 0;
	Set3(pmove->mins, -16.0f, -16.0f, -24.0f);
	Set3(pmove->maxs, 16.0f, 16.0f,
		(pmove->s.pm_flags & PMF_DUCKED) ? 4.0f : 32.0f);
}

static int ground_marker;

static void GroundedProductionPmove(pmove_t *pmove)
{
	const short initial_z = pmove->s.origin[2];

	ProductionPmove(pmove);
	if (pmove->s.origin[2] < initial_z)
		pmove->s.origin[2] = initial_z;
	pmove->groundentity = (struct edict_s *)(void *)&ground_marker;
}

static void SetProductionHull(sg_rune_model_identity_t *identity)
{
	Set3(identity->standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity->standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(identity->crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity->crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
}

static void SetBspPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance, int32_t type)
{
	Set3(plane->normal.value, x, y, z);
	plane->distance = distance;
	plane->type = type;
}

static void ConfigureSolidFloor(water_fixture_t *fixture, float top)
{
	SetBspPlane(&fixture->planes[1], 1.0f, 0.0f, 0.0f, 4096.0f, 0);
	SetBspPlane(&fixture->planes[2], -1.0f, 0.0f, 0.0f, 4096.0f, 3);
	SetBspPlane(&fixture->planes[3], 0.0f, 1.0f, 0.0f, 4096.0f, 1);
	SetBspPlane(&fixture->planes[4], 0.0f, -1.0f, 0.0f, 4096.0f, 4);
	SetBspPlane(&fixture->planes[5], 0.0f, 0.0f, 1.0f, top, 2);
	SetBspPlane(&fixture->planes[6], 0.0f, 0.0f, -1.0f, 128.0f, 5);
}

static sg_rune_order_key_t ModelOrder(const water_fixture_t *fixture,
	uint32_t domain, uint32_t index)
{
	const sg_rune_order_key_t order = {
		fixture->authority.identity.source_set_identity, domain, index, index, 0U
	};

	return order;
}

typedef struct model_fixture_s
{
	sg_rune_plane_t planes[4];
	sg_rune_phase_basis_t phase;
	sg_rune_cell_t cell;
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
} model_fixture_t;

static void SetModelPlane(model_fixture_t *fixture,
	const water_fixture_t *water, uint32_t index, float x, float y, float z,
	float distance)
{
	sg_rune_plane_t *plane = &fixture->planes[index];

	memset(plane, 0, sizeof(*plane));
	plane->order = ModelOrder(water, SG_RUNE_ORDER_PLANE, index);
	plane->id.value = SG_RuneModelStableIdFromOrderKey(&plane->order);
	Set3(plane->normal.value, x, y, z);
	plane->distance = distance;
}

static void InitFullModel(const water_fixture_t *water,
	model_fixture_t *fixture)
{
	sg_rune_model_t *model = &fixture->model;

	memset(fixture, 0, sizeof(*fixture));
	SetModelPlane(fixture, water, 0U, 1.0f, 0.0f, 0.0f, 64.0f);
	SetModelPlane(fixture, water, 1U, -1.0f, 0.0f, 0.0f, 64.0f);
	SetModelPlane(fixture, water, 2U, 0.0f, 1.0f, 0.0f, 64.0f);
	SetModelPlane(fixture, water, 3U, 0.0f, -1.0f, 0.0f, 64.0f);
	memset(&fixture->phase, 0, sizeof(fixture->phase));
	fixture->phase.order = ModelOrder(water, SG_RUNE_ORDER_PHASE, 0U);
	fixture->phase.id.value = SG_RuneModelStableIdFromOrderKey(
		&fixture->phase.order);
	fixture->phase.stance = SG_RUNE_STANCE_STANDING;
	fixture->phase.motion = SG_RUNE_MOTION_AIRBORNE;
	fixture->phase.support = SG_RUNE_SUPPORT_NONE;
	fixture->phase.medium = SG_RUNE_MEDIUM_DRY;
	fixture->phase.void_relation = SG_RUNE_VOID_CLEAR;
	fixture->phase.reference_frame = SG_RUNE_FRAME_WORLD;
	fixture->phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	fixture->phase.velocity.x.min_value = -2000.0f;
	fixture->phase.velocity.x.max_value = 2000.0f;
	fixture->phase.velocity.y = fixture->phase.velocity.x;
	fixture->phase.velocity.z = fixture->phase.velocity.x;
	fixture->phase.elapsed_ms.min_value = 0.0f;
	fixture->phase.elapsed_ms.max_value = 100.0f;
	fixture->phase.time_quantum_ms =
		water->authority.identity.physics.substep_ms;
	fixture->phase.time_horizon_ms = water->authority.identity.physics.frame_ms;
	memset(&fixture->cell, 0, sizeof(fixture->cell));
	fixture->cell.id = water->cells[0].id;
	fixture->cell.order = ModelOrder(water, SG_RUNE_ORDER_CELL, 0U);
	fixture->cell.geometry.source_set_identity =
		water->authority.identity.source_set_identity;
	fixture->cell.geometry.source_index = 0U;
	fixture->cell.geometry.source_ordinal = 0U;
	Set3(fixture->cell.bounds.mins.value, -64.0f, -64.0f, -64.0f);
	Set3(fixture->cell.bounds.maxs.value, 64.0f, 64.0f, 64.0f);
	fixture->cell.boundary_planes.first = 0U;
	fixture->cell.boundary_planes.count = 4U;
	fixture->cell.phases.first = 0U;
	fixture->cell.phases.count = 1U;
	fixture->cell.bsp_leaf.index = 0U;
	fixture->cell.bsp_area.index = 0U;
	fixture->cell.bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
	model->version = SG_RUNE_MODEL_VERSION;
	model->schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	model->flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	model->identity = water->authority.identity;
	model->completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	model->completeness.reason = SG_RUNE_FAILURE_NONE;
	model->completeness.expected_cells = 1U;
	model->completeness.covered_cells = 1U;
	model->completeness.expected_portals = 0U;
	model->completeness.covered_portals = 0U;
	model->completeness.failure_record = UINT32_MAX;
	model->planes = fixture->planes;
	model->plane_count = 4U;
	model->phases = &fixture->phase;
	model->phase_count = 1U;
	model->cells = &fixture->cell;
	model->cell_count = 1U;
	fixture->evidence.version = SG_RUNE_VALIDATION_EVIDENCE_VERSION;
	fixture->evidence.verifier_identity = UINT64_C(0x5741544552505246);
	fixture->evidence.bsp_content_id = model->identity.bsp_content_id;
	fixture->evidence.source_set_identity = model->identity.source_set_identity;
	fixture->evidence.fixed_point_identity = UINT64_C(0x5741544552464958);
	fixture->evidence.fixed_point_rounds = 1U;
	fixture->evidence.proved_cells = model->cell_count;
	fixture->evidence.proved_portals = model->portal_count;
}

typedef struct input_bundle_s
{
	sg_host_law_publication_t *host_laws;
	sg_phase_catalog_publication_owner_t *phase_owner;
	sg_phase_catalog_publication_t *phase_catalog;
	sg_water_capability_publication_owner_t *water_owner;
	model_fixture_t model;
} input_bundle_t;

static int BuildPhaseCatalog(const water_fixture_t *fixture,
	sg_phase_catalog_publication_owner_t *owner,
	sg_phase_catalog_publication_t **publication_out)
{
	sg_phase_catalog_source_t source;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;
	int success;

	memset(&source, 0, sizeof(source));
	memset(&error, 0, sizeof(error));
	source.authority = &fixture->authority;
	source.configuration = &fixture->configuration;
	source.semantics = &fixture->semantics;
	memset(&empty_phase_provider, 0, sizeof(empty_phase_provider));
	empty_phase_provider.identity = fixture->semantics.identity;
	empty_phase_provider.completion = SG_PHASE_CATALOG_PROVEN_EMPTY;
	empty_phase_provider.verifier_identity = UINT64_C(0x5741544552504853);
	source.mover_support_owner =
		(const sg_phase_mover_support_provider_owner_t *)(const void *)fixture;
	source.mover_support_provider =
		(const sg_phase_mover_support_provider_t *)(const void *)&fixture->world;
	success = SG_PhaseCatalogBuild(&source, &catalog, &error) &&
		SG_PhaseCatalogPublicationIssue(owner, &source, catalog,
			publication_out, &audit);
	SG_PhaseCatalogDestroy(catalog);
	return success;
}

static int PhaseIndex(const sg_phase_catalog_view_t *view,
	sg_rune_phase_ref_t reference, uint32_t *index_out)
{
	uint32_t index;

	for (index = 0U; index < view->phase_count; index++)
		if (SG_RuneModelStableIdEqual(&view->phases[index].id.value,
			&reference.value))
		{
			*index_out = index;
			return 1;
		}
	return 0;
}

static int BuildCandidate(const water_fixture_t *fixture,
	const sg_phase_catalog_publication_owner_t *phase_owner,
	const sg_phase_catalog_publication_t *phase_catalog,
	sg_water_capability_set_t **candidate_out)
{
	const sg_phase_catalog_view_t *view = NULL;
	sg_water_phase_binding_t *bindings = NULL;
	sg_water_capability_error_t error;
	uint32_t binding;
	int success = 0;

	if (!candidate_out || *candidate_out ||
		!SG_PhaseCatalogPublicationRead(phase_owner, phase_catalog, &view) ||
		!view)
		return 0;
	bindings = calloc(view->binding_count, sizeof(*bindings));
	if (!bindings)
		return 0;
	for (binding = 0U; binding < view->binding_count; binding++)
	{
		bindings[binding].semantic_region_id =
			view->bindings[binding].semantic_region_id;
		if (!PhaseIndex(view, view->bindings[binding].phase,
			&bindings[binding].phase))
			goto done;
	}
	success = sg_host.pmove && SG_WaterCapabilityBuild(&fixture->authority,
		sg_host.pmove,
		&fixture->configuration, &fixture->semantics, view->phases,
		view->phase_count, bindings, view->binding_count, candidate_out, &error);
done:
	free(bindings);
	return success;
}

static void BindFixtureToProductionPmoveLaw(water_fixture_t *fixture)
{
	SetProductionHull(&fixture->authority.identity);
	SetProductionHull(&fixture->configuration.identity);
	SetProductionHull(&fixture->semantics.identity);
	fixture->authority.identity.physics.substep_ms =
		SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	fixture->configuration.identity.physics.substep_ms =
		SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	fixture->semantics.identity.physics.substep_ms =
		SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
}

static int PrepareBundle(water_fixture_t *fixture, input_bundle_t *bundle)
{
	sg_host_law_result_t host_result;

	memset(bundle, 0, sizeof(*bundle));
	BindFixtureToProductionPmoveLaw(fixture);
	InitFullModel(fixture, &bundle->model);
	host_result = SG_HostLawPublicationOwnerIssue(&fixture->authority,
		&bundle->host_laws);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		fprintf(stderr, "host issue failed: status=%d field=%d element=%u\n",
			(int)host_result.status, (int)host_result.field, host_result.element);
		return 0;
	}
	if (!SG_PhaseCatalogPublicationOwnerCreate(&bundle->phase_owner) ||
		!SG_WaterCapabilityPublicationOwnerCreate(&bundle->water_owner) ||
		!BuildPhaseCatalog(fixture, bundle->phase_owner,
			&bundle->phase_catalog))
	{
		SG_WaterCapabilityPublicationOwnerDestroy(bundle->water_owner);
		SG_PhaseCatalogPublicationOwnerDestroy(bundle->phase_owner);
		SG_HostLawPublicationOwnerDestroy(bundle->host_laws);
		bundle->host_laws = NULL;
		bundle->phase_owner = NULL;
		bundle->water_owner = NULL;
		return 0;
	}
	return 1;
}

static void DestroyBundle(input_bundle_t *bundle)
{
	SG_PhaseCatalogPublicationDestroy(bundle->phase_owner,
		bundle->phase_catalog);
	SG_WaterCapabilityPublicationOwnerDestroy(bundle->water_owner);
	SG_PhaseCatalogPublicationOwnerDestroy(bundle->phase_owner);
	SG_HostLawPublicationOwnerDestroy(bundle->host_laws);
	memset(bundle, 0, sizeof(*bundle));
}

static void DestroyBundleSources(input_bundle_t *bundle)
{
	SG_PhaseCatalogPublicationDestroy(bundle->phase_owner,
		bundle->phase_catalog);
	SG_PhaseCatalogPublicationOwnerDestroy(bundle->phase_owner);
	SG_HostLawPublicationOwnerDestroy(bundle->host_laws);
	bundle->phase_catalog = NULL;
	bundle->phase_owner = NULL;
	bundle->host_laws = NULL;
}

static sg_water_capability_issue_source_t IssueSource(
	const water_fixture_t *fixture, const input_bundle_t *bundle)
{
	sg_water_capability_issue_source_t source;

	memset(&source, 0, sizeof(source));
	source.host_laws = bundle->host_laws;
	source.configuration = &fixture->configuration;
	source.semantics = &fixture->semantics;
	source.phase_catalog_owner = bundle->phase_owner;
	source.phase_catalog = bundle->phase_catalog;
	return source;
}

static int Issue(const water_fixture_t *fixture, const input_bundle_t *bundle,
	const sg_water_capability_set_t *candidate,
	sg_water_capability_publication_t **publication_out,
	sg_water_capability_audit_result_t *audit_out)
{
	const sg_water_capability_issue_source_t source = IssueSource(fixture,
		bundle);
	int success;

	success = SG_WaterCapabilityPublicationIssue(bundle->water_owner, &source,
		candidate,
		publication_out, audit_out);
	return success;
}

static int FindFact(const sg_water_capability_publication_owner_t *owner,
	const sg_water_capability_publication_t *publication,
	sg_water_capability_kind_t kind, uint64_t source_region,
	uint64_t destination_region, sg_water_capability_publication_fact_t *out)
{
	sg_water_capability_publication_info_t info;
	uint32_t index;

	if (!SG_WaterCapabilityPublicationInfo(owner, publication, &info))
		return 0;
	for (index = 0U; index < info.fact_count; index++)
	{
		if (!SG_WaterCapabilityPublicationFact(owner, publication, index, out))
			return 0;
		if (out->kind == kind && out->source_semantic_region_id == source_region &&
			out->destination_semantic_region_id == destination_region)
			return 1;
	}
	return 0;
}

static void MakeRegionWet(water_fixture_t *fixture, uint32_t index,
	uint32_t contents)
{
	sg_configuration_semantic_region_t *region = &fixture->regions[index];
	uint32_t sample;

	region->origin_contents = contents;
	region->origin_rune_contents = SG_HostCollisionRuneContents(contents);
	for (sample = 0U; sample < 3U; sample++)
		region->sample_contents[sample] = contents;
	region->water_type = contents;
	region->water_level = 3U;
	region->flags = SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
}

static void MakeRegionDry(water_fixture_t *fixture, uint32_t index,
	uint32_t contents, sg_configuration_semantic_region_flags_t flags)
{
	sg_configuration_semantic_region_t *region = &fixture->regions[index];
	uint32_t sample;

	region->origin_contents = contents;
	region->origin_rune_contents = SG_HostCollisionRuneContents(contents);
	for (sample = 0U; sample < 3U; sample++)
		region->sample_contents[sample] = contents;
	region->water_type = contents;
	region->water_level = 0U;
	region->flags = flags;
}

static void ConfigureVerticalDrop(water_fixture_t *fixture)
{
	sg_configuration_semantic_region_t *dry = &fixture->regions[0];
	sg_configuration_semantic_region_t *wet = &fixture->regions[1];
	uint32_t face;

	Set3(fixture->planes[0].normal.value, 0.0f, 0.0f, 1.0f);
	fixture->planes[0].distance = 0.0f;
	fixture->planes[0].type = 2;
	fixture->leaves[0].contents = 0;
	fixture->leaves[1].contents = SG_HOST_CONTENTS_WATER;
	Set3(dry->bounds.mins.value, -64.0f, -64.0f, 0.0f);
	Set3(dry->bounds.maxs.value, 64.0f, 64.0f, 64.0f);
	Set3(wet->bounds.mins.value, -64.0f, -64.0f, -64.0f);
	Set3(wet->bounds.maxs.value, 64.0f, 64.0f, 0.0f);
	Set3(dry->interior_witness.value, 0.0f, 0.0f, 32.0f);
	Set3(wet->interior_witness.value, 0.0f, 0.0f, -32.0f);
	for (face = 0U; face < 12U; face++)
	{
		memset(&fixture->faces[face], 0, sizeof(fixture->faces[face]));
		fixture->faces[face].source_kind =
			SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP;
		fixture->faces[face].source_index = face;
	}
	Set3(fixture->faces[0].normal, 1.0f, 0.0f, 0.0f);
	fixture->faces[0].distance = 64.0f;
	Set3(fixture->faces[1].normal, -1.0f, 0.0f, 0.0f);
	fixture->faces[1].distance = 64.0f;
	Set3(fixture->faces[2].normal, 0.0f, 1.0f, 0.0f);
	fixture->faces[2].distance = 64.0f;
	Set3(fixture->faces[3].normal, 0.0f, -1.0f, 0.0f);
	fixture->faces[3].distance = 64.0f;
	Set3(fixture->faces[4].normal, 0.0f, 0.0f, 1.0f);
	fixture->faces[4].distance = 64.0f;
	Set3(fixture->faces[5].normal, 0.0f, 0.0f, -1.0f);
	fixture->faces[5].distance = 0.0f;
	fixture->faces[5].source_kind =
		SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE;
	fixture->faces[5].source_index = 911U;
	Set3(fixture->faces[6].normal, 1.0f, 0.0f, 0.0f);
	fixture->faces[6].distance = 64.0f;
	Set3(fixture->faces[7].normal, -1.0f, 0.0f, 0.0f);
	fixture->faces[7].distance = 64.0f;
	Set3(fixture->faces[8].normal, 0.0f, 1.0f, 0.0f);
	fixture->faces[8].distance = 64.0f;
	Set3(fixture->faces[9].normal, 0.0f, -1.0f, 0.0f);
	fixture->faces[9].distance = 64.0f;
	Set3(fixture->faces[10].normal, 0.0f, 0.0f, 1.0f);
	fixture->faces[10].distance = 0.0f;
	fixture->faces[10].source_kind =
		SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE;
	fixture->faces[10].source_index = 911U;
	fixture->faces[10].reversed = 1U;
	Set3(fixture->faces[11].normal, 0.0f, 0.0f, -1.0f);
	fixture->faces[11].distance = 64.0f;
}

static void TestAcceptedPublicationAndSourceDestruction(void)
{
	water_fixture_t fixture;
	input_bundle_t bundle;
	sg_water_capability_set_t *candidate = NULL;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_publication_info_t info;
	sg_water_capability_publication_binding_t binding;
	sg_water_capability_publication_fact_t swim;
	sg_water_capability_publication_fact_t saved;
	sg_water_capability_audit_result_t audit;

	memset(&bundle, 0, sizeof(bundle));
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_CURRENT_0, 800.0f, 0, 0));
	/* Region zero is a canonical semantics identity, not a sentinel. */
	fixture.regions[0].id = 0U;
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog, &candidate));
	ResetPreflight();
	CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_OK);
	CHECK(bsp_calls == 1U && configuration_calls == 1U && semantics_calls == 1U);
	if (!publication || !candidate)
		goto done;
	CHECK(SG_WaterCapabilityPublicationInfo(bundle.water_owner, publication, &info));
	CHECK(info.state == SG_WATER_CAPABILITY_PUBLICATION_COMPLETE);
	CHECK(info.collision_law_id != 0U && info.pmove_law_id != 0U);
	CHECK(info.host_pmove_frames == candidate->host_pmove_frames);
	CHECK(info.lattice_solve_calls == candidate->lattice_solve_calls);
	CHECK(info.lattice_constraints == candidate->lattice_constraints);
	CHECK(info.same_cell_candidate_pairs == candidate->same_cell_candidate_pairs);
	CHECK(info.lattice_maximum_binary_shift ==
		candidate->lattice_maximum_binary_shift);
	CHECK(info.bsp_completeness.code == SG_BSP_COMPLETENESS_OK);
	CHECK(info.configuration_audit.code == SG_CONFIGURATION_AUDIT_OK);
	CHECK(info.semantics_audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_OK);
	CHECK(SG_WaterCapabilityPublicationBinding(bundle.water_owner, publication, 1U, &binding));
	CHECK(SG_RuneModelStableIdValid(&binding.cell.value));
	CHECK(SG_RuneModelStableIdValid(&binding.phase.value));
	if (!FindFact(bundle.water_owner, publication, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
		fixture.regions[1].id, fixture.regions[1].id, &swim))
	{
		CHECK(0);
		goto done;
	}
	CHECK(SG_RuneModelStableIdValid(&swim.source_cell.value));
	CHECK(SG_RuneModelStableIdValid(&swim.source_phase.value));
	CHECK(swim.source_watertype == (SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_CURRENT_0));
	CHECK(swim.source_current == SG_HOST_CONTENTS_CURRENT_0);
	CHECK(swim.result_current == SG_HOST_CONTENTS_CURRENT_0);
	CHECK(swim.source_groundcontents == 0U);
	CHECK((swim.source_environment &
		SG_WATER_ENVIRONMENT_BREATH_LIMITED) != 0U);
	CHECK((swim.source_environment &
		SG_WATER_ENVIRONMENT_WATER_CURRENT) != 0U);
	if (!FindFact(bundle.water_owner, publication, SG_WATER_CAPABILITY_ENTRY,
		fixture.regions[0].id, fixture.regions[1].id, &saved))
	{
		CHECK(0);
		goto done;
	}
	SG_WaterCapabilityDestroy(candidate);
	candidate = NULL;
	DestroyBundleSources(&bundle);
	memset(&fixture, 0, sizeof(fixture));
	CHECK(SG_WaterCapabilityPublicationInfo(bundle.water_owner, publication, &info));
	CHECK(SG_WaterCapabilityPublicationFact(bundle.water_owner, publication, saved.order, &swim));
	CHECK(memcmp(&saved, &swim, sizeof(saved)) == 0);
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	CHECK(!SG_WaterCapabilityPublicationInfo(bundle.water_owner, publication,
		&info));
	publication = NULL;
done:
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	SG_WaterCapabilityDestroy(candidate);
	if (bundle.host_laws || bundle.phase_catalog || bundle.water_owner)
		DestroyBundle(&bundle);
}

static void TestDropEntryExitAndPortalReferences(void)
{
	water_fixture_t fixture;
	input_bundle_t bundle;
	sg_water_capability_set_t *candidate = NULL;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_publication_fact_t fact;
	sg_water_capability_publication_fact_t saved;
	sg_water_capability_audit_result_t audit;

	memset(&bundle, 0, sizeof(bundle));
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	ConfigureVerticalDrop(&fixture);
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog, &candidate));
	CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
	if (FindFact(bundle.water_owner, publication, SG_WATER_CAPABILITY_ENTRY,
		fixture.regions[0].id, fixture.regions[1].id, &fact))
	{
		CHECK(fact.direction == SG_WATER_DIRECTION_BOUNDARY);
		CHECK(fact.direction_vector.value[2] < 0.0f);
		CHECK(fact.flags & SG_WATER_CAPABILITY_STRADDLES_FRAME_LAW);
	}
	else CHECK(0);
	if (FindFact(bundle.water_owner, publication, SG_WATER_CAPABILITY_EXIT,
		fixture.regions[1].id, fixture.regions[0].id, &fact))
	{
		CHECK(fact.direction_vector.value[2] > 0.0f);
		CHECK(fact.result_water_type == 0U);
		CHECK(fact.result_watertype == SG_HOST_CONTENTS_WATER);
	}
	else CHECK(0);
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	SG_WaterCapabilityDestroy(candidate);
	DestroyBundle(&bundle);

	publication = NULL;
	candidate = NULL;
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	fixture.leaves[1].contents = SG_HOST_CONTENTS_WATER;
	MakeRegionWet(&fixture, 0U, SG_HOST_CONTENTS_WATER);
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog, &candidate));
	CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
	if (FindFact(bundle.water_owner, publication, SG_WATER_CAPABILITY_VOLUME_CROSSING,
		fixture.regions[0].id, fixture.regions[1].id, &fact))
	{
		sg_water_capability_publication_info_t info;
		sg_rune_phase_transition_t transition;
		sg_phase_catalog_transition_evidence_t evidence;

		CHECK(SG_RuneModelStableIdEqual(&fact.portal.value,
			&fixture.portal.id.value));
		CHECK(fact.flags & SG_WATER_CAPABILITY_CROSSES_PORTAL);
		CHECK(SG_RuneModelStableIdValid(&fact.phase_transition.value));
		CHECK(SG_WaterCapabilityPublicationInfo(bundle.water_owner, publication,
			&info));
		CHECK(info.transition_count != 0U);
		CHECK(SG_WaterCapabilityPublicationTransition(bundle.water_owner,
			publication, 0U, &transition));
		CHECK(SG_WaterCapabilityPublicationTransitionEvidence(bundle.water_owner,
			publication, 0U, &evidence));
		CHECK(SG_RuneModelStableIdValid(&transition.id.value));
		CHECK(evidence.origin == SG_PHASE_CATALOG_TRANSITION_PORTAL);
		saved = fact;
		SG_WaterCapabilityDestroy(candidate);
		candidate = NULL;
		DestroyBundleSources(&bundle);
		memset(&fixture, 0, sizeof(fixture));
		CHECK(SG_WaterCapabilityPublicationFact(bundle.water_owner, publication, saved.order,
			&fact));
		CHECK(SG_RuneModelStableIdEqual(&fact.portal.value,
			&saved.portal.value));
		CHECK(fact.source_semantic_region_id ==
			saved.source_semantic_region_id);
	}
	else CHECK(0);
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	SG_WaterCapabilityDestroy(candidate);
	if (bundle.host_laws || bundle.phase_catalog || bundle.water_owner)
		DestroyBundle(&bundle);
}

static void TestGroundAndCurrentLaws(void)
{
	water_fixture_t fixture;
	input_bundle_t bundle;
	sg_water_capability_set_t *candidate = NULL;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_publication_fact_t fact;
	sg_water_capability_audit_result_t audit;
	float saved_floor;

	memset(&bundle, 0, sizeof(bundle));
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 1, 0));
	ConfigureSolidFloor(&fixture, -24.125f);
	fixture.brush.contents = SG_HOST_CONTENTS_SOLID |
		SG_HOST_CONTENTS_CURRENT_90;
	fixture.leaves[0].contents = fixture.brush.contents;
	fixture.leaves[1].contents = SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_SOLID;
	MakeRegionWet(&fixture, 0U, SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_SOLID);
	MakeRegionDry(&fixture, 1U, 0U,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
	fixture.regions[0].flags = SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
	sg_host.pmove = GroundedProductionPmove;
	gi.Pmove = GroundedProductionPmove;
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog, &candidate));
	CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
	if (FindFact(bundle.water_owner, publication, SG_WATER_CAPABILITY_CURRENT,
		fixture.regions[0].id, fixture.regions[0].id, &fact))
	{
		CHECK(fact.source_watertype == (SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_SOLID));
		CHECK(fact.source_water_current == 0U);
		CHECK(fact.source_ground_current == SG_HOST_CONTENTS_CURRENT_90);
		CHECK(fact.source_current == SG_HOST_CONTENTS_CURRENT_90);
		CHECK(fact.source_groundcontents == (SG_HOST_CONTENTS_SOLID |
			SG_HOST_CONTENTS_CURRENT_90));
		CHECK(fact.result_groundcontents == (SG_HOST_CONTENTS_SOLID |
			SG_HOST_CONTENTS_CURRENT_90));
		CHECK(fact.result_current == SG_HOST_CONTENTS_CURRENT_90);
		CHECK((fact.source_environment &
			SG_WATER_ENVIRONMENT_GROUND_CURRENT) != 0U);
		CHECK(fact.result_water_type == (SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_SOLID));
		CHECK(fact.result_watertype == (SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_SOLID));
	}
	else CHECK(0);
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	publication = NULL;

	saved_floor = fixture.planes[5].distance;
	fixture.planes[5].distance = -64.0f;
	CHECK(!Issue(&fixture, &bundle, candidate, &publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT);
	fixture.planes[5].distance = saved_floor;
	SG_WaterCapabilityDestroy(candidate);
	DestroyBundle(&bundle);
	sg_host.pmove = ProductionPmove;
	gi.Pmove = ProductionPmove;
}

static void TestHazardMedia(void)
{
	static const sg_host_collision_contents_t contents[] = {
		SG_HOST_CONTENTS_LAVA, SG_HOST_CONTENTS_SLIME
	};
	static const sg_rune_medium_t media[] = {
		SG_RUNE_MEDIUM_LAVA, SG_RUNE_MEDIUM_SLIME
	};
	uint32_t medium;

	for (medium = 0U; medium < 2U; medium++)
	{
		water_fixture_t fixture;
		input_bundle_t bundle;
		sg_water_capability_set_t *candidate = NULL;
		sg_water_capability_publication_t *publication = NULL;
		sg_water_capability_publication_fact_t fact;
		sg_water_capability_audit_result_t audit;

		memset(&bundle, 0, sizeof(bundle));
		CHECK(WaterFixtureInit(&fixture, contents[medium], 800.0f, 0, 0));
		CHECK(PrepareBundle(&fixture, &bundle));
		CHECK(BuildCandidate(&fixture, bundle.phase_owner,
			bundle.phase_catalog, &candidate));
		CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
		if (FindFact(bundle.water_owner, publication,
			SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, fixture.regions[1].id,
			fixture.regions[1].id, &fact))
		{
			CHECK(fact.source_medium == media[medium]);
			CHECK((fact.source_environment &
				SG_WATER_ENVIRONMENT_HAZARDOUS) != 0U);
			CHECK((fact.source_environment &
				SG_WATER_ENVIRONMENT_BREATH_LIMITED) != 0U);
		}
		else
			CHECK(0);
		SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
		SG_WaterCapabilityDestroy(candidate);
		DestroyBundle(&bundle);
	}
}

static void TestProvenEmptyDryDomain(void)
{
	water_fixture_t fixture;
	input_bundle_t bundle;
	sg_water_capability_set_t *candidate = NULL;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_publication_info_t info;
	sg_water_capability_audit_result_t audit;

	memset(&bundle, 0, sizeof(bundle));
	CHECK(WaterFixtureInit(&fixture, 0U, 800.0f, 0, 0));
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog,
		&candidate));
	CHECK(candidate != NULL && candidate->fact_count == 0U);
	CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_OK);
	CHECK(audit.obligation_count == 0U && audit.wet_region_count == 0U);
	CHECK(SG_WaterCapabilityPublicationInfo(bundle.water_owner, publication,
		&info));
	CHECK(info.state == SG_WATER_CAPABILITY_PUBLICATION_PROVEN_EMPTY);
	CHECK(info.fact_count == 0U && info.wet_region_count == 0U);
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	SG_WaterCapabilityDestroy(candidate);
	DestroyBundle(&bundle);
}

static void TestSweepLineDenseBoundaries(void)
{
	enum { STRIPES = 16, REGIONS = STRIPES * 2, FACES = REGIONS * 6 };
	water_fixture_t fixture;
	input_bundle_t bundle;
	sg_configuration_semantic_region_t *regions = NULL;
	sg_configuration_semantic_face_t *faces = NULL;
	sg_water_capability_set_t *candidate = NULL;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_publication_info_t info;
	sg_water_capability_audit_result_t audit;
	uint32_t region;

	memset(&bundle, 0, sizeof(bundle));
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	regions = calloc(REGIONS, sizeof(*regions));
	faces = calloc(FACES, sizeof(*faces));
	CHECK(regions != NULL && faces != NULL);
	if (!regions || !faces)
		goto done;
	for (region = 0U; region < REGIONS; region++)
	{
		const uint32_t side = region & 1U;
		const uint32_t stripe = region / 2U;
		const float min_y = -64.0f + (float)stripe * 8.0f;
		const float max_y = min_y + 8.0f;
		uint32_t face;

		regions[region] = fixture.regions[side];
		regions[region].id = (uint64_t)region;
		regions[region].first_face = region * 6U;
		regions[region].bounds.mins.value[1] = min_y;
		regions[region].bounds.maxs.value[1] = max_y;
		regions[region].interior_witness.value[1] = (min_y + max_y) * 0.5f;
		for (face = 0U; face < 6U; face++)
			faces[region * 6U + face] = fixture.faces[side * 6U + face];
		faces[region * 6U + 2U].distance = max_y;
		faces[region * 6U + 3U].distance = -min_y;
	}
	fixture.semantics.regions = regions;
	fixture.semantics.region_count = REGIONS;
	fixture.semantics.faces = faces;
	fixture.semantics.face_count = FACES;
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog, &candidate));
	CHECK(candidate != NULL && candidate->same_cell_candidate_pairs == STRIPES);
	CHECK(Issue(&fixture, &bundle, candidate, &publication, &audit));
	CHECK(SG_WaterCapabilityPublicationInfo(bundle.water_owner, publication, &info));
	CHECK(info.boundary_count == STRIPES);
	CHECK(info.same_cell_candidate_pairs == STRIPES);
done:
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	SG_WaterCapabilityDestroy(candidate);
	if (bundle.host_laws || bundle.phase_catalog)
		DestroyBundle(&bundle);
	free(faces);
	free(regions);
}

static void ForgedPmove(pmove_t *pmove)
{
	ProductionPmove(pmove);
	pmove->mins[0] = -15.0f;
}

static void TestAdversarialRejectionAndMetricAuthentication(void)
{
	water_fixture_t fixture;
	input_bundle_t bundle;
	sg_water_capability_set_t *candidate = NULL;
	sg_water_capability_publication_t *publication = NULL;
	sg_water_capability_audit_result_t audit;
	sg_water_capability_issue_source_t source;
	float saved_plane_distance;
	uint64_t saved_u64;
	uint32_t saved_u32;
	uint32_t saved_fact_count;
	int32_t saved_contents;

	memset(&bundle, 0, sizeof(bundle));
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_CURRENT_0, 800.0f, 0, 0));
	CHECK(PrepareBundle(&fixture, &bundle));
	CHECK(BuildCandidate(&fixture, bundle.phase_owner, bundle.phase_catalog, &candidate));
	if (!candidate)
		goto done;
	source = IssueSource(&fixture, &bundle);
	source.phase_catalog = NULL;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	source.phase_catalog = bundle.phase_catalog;
	source.phase_catalog_owner = NULL;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source,
		candidate, &publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	source.phase_catalog_owner = bundle.phase_owner;
	saved_fact_count = candidate->fact_count;
	candidate->fact_count = 0U;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source,
		candidate, &publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_OMITTED_FACT);
	candidate->fact_count = saved_fact_count;

	sg_host.pmove = ForgedPmove;
	gi.Pmove = ForgedPmove;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_HOST_LAW);
	sg_host.pmove = ProductionPmove;
	gi.Pmove = ProductionPmove;

	saved_plane_distance = fixture.planes[0].distance;
	fixture.planes[0].distance = 40.0f;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT);
	fixture.planes[0].distance = saved_plane_distance;

	fixture.regions[1].id += UINT64_C(10000);
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_UNBOUND_PHASE);
	fixture.regions[1].id -= UINT64_C(10000);

	ResetPreflight();
	reject_bsp = 1;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_BSP_COMPLETENESS);
	CHECK(bsp_calls == 1U && configuration_calls == 1U && semantics_calls == 1U);
	ResetPreflight();
	reject_configuration = 1;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_CONFIGURATION_AUDIT);
	ResetPreflight();
	reject_semantics = 1;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_SEMANTICS_AUDIT);
	ResetPreflight();

	saved_u64 = candidate->host_pmove_frames;
	candidate->host_pmove_frames++;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT);
	candidate->host_pmove_frames = saved_u64;
	saved_u64 = candidate->lattice_solve_calls;
	candidate->lattice_solve_calls++;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT);
	candidate->lattice_solve_calls = saved_u64;
	saved_u64 = candidate->lattice_constraints;
	candidate->lattice_constraints++;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT);
	candidate->lattice_constraints = saved_u64;
	saved_u64 = candidate->same_cell_candidate_pairs;
	candidate->same_cell_candidate_pairs++;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT);
	candidate->same_cell_candidate_pairs = saved_u64;
	saved_u32 = candidate->lattice_maximum_binary_shift;
	candidate->lattice_maximum_binary_shift++;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_METRIC_DISAGREEMENT);
	candidate->lattice_maximum_binary_shift = saved_u32;

	saved_contents = fixture.leaves[0].contents;
	fixture.leaves[0].contents = SG_HOST_CONTENTS_WATER;
	CHECK(!SG_WaterCapabilityPublicationIssue(bundle.water_owner, &source, candidate,
		&publication, &audit));
	CHECK(audit.code == SG_WATER_CAPABILITY_AUDIT_HOST_DISAGREEMENT);
	fixture.leaves[0].contents = saved_contents;
done:
	sg_host.pmove = ProductionPmove;
	gi.Pmove = ProductionPmove;
	SG_WaterCapabilityPublicationDestroy(bundle.water_owner, publication);
	SG_WaterCapabilityDestroy(candidate);
	if (bundle.host_laws || bundle.phase_catalog)
		DestroyBundle(&bundle);
}

int main(void)
{
	memset(&sg_host, 0, sizeof(sg_host));
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_gravity_cvar, 0, sizeof(funky_gravity_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	memset(&ctf_flags_cvar, 0, sizeof(ctf_flags_cvar));
	sg_host.pmove = ProductionPmove;
	sg_host.cvar = TestCvar;
	gi.Pmove = ProductionPmove;
	gi.cvar = EngineTestCvar;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_gravity_cvar;
	ctfflags = &ctf_flags_cvar;
	gravity_cvar.value = 800.0f;
	maxvelocity_cvar.value = 2000.0f;
	airaccelerate_cvar.value = 0.0f;
	ResetPreflight();
	TestAcceptedPublicationAndSourceDestruction();
	TestDropEntryExitAndPortalReferences();
	TestGroundAndCurrentLaws();
	TestHazardMedia();
	TestProvenEmptyDryDomain();
	TestSweepLineDenseBoundaries();
	TestAdversarialRejectionAndMetricAuthentication();
	if (failures)
	{
		fprintf(stderr, "sg_water_capability_publication_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_water_capability_publication_test: ok");
	return 0;
}
