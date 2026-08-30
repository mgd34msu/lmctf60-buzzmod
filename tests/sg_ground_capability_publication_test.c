#define SG_GROUND_CAPABILITY_TEST_NO_MAIN
#include "sg_ground_capability_test.c"

#include "../slipgate/sg_ground_capability_publication.h"
#include "../slipgate/sg_host_law_publication_private.h"
#include "../slipgate/sg_host_engine_runtime_private.h"
#include "../slipgate/sg_host_hook_law.h"
#include "../slipgate/sg_host_mechanism_law.h"
#include "sg_ground_capability_publication_phase_fixture.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"

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

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);

typedef struct publication_fixture_s
{
	fixture_t world;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *semantics;
	sg_host_law_publication_t *engine_authority;
	sg_phase_catalog_publication_owner_t *phase_owner;
	sg_phase_catalog_publication_t *phase_catalog;
	sg_ground_capability_publication_source_t source;
} publication_fixture_t;

static cvar_t *PublicationCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	return strcmp(name, "sv_airaccelerate") == 0 ?
		&airaccelerate_cvar : NULL;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	velocity[0] = bite[0] - start[0];
	velocity[1] = bite[1] - start[1];
	velocity[2] = bite[2] - start[2];
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

int SG_HostEngineRuntimeAccepted(const sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return 0;
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

sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerActivate(
	sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
}

int SG_HostEngineRuntimeTrace(const sg_host_engine_runtime_t *runtime,
	uint32_t subject_index, const float start[3], const float mins[3],
	const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	(void)runtime;
	(void)subject_index;
	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	(void)mask;
	(void)trace_out;
	return 0;
}

int SG_HostEngineRuntimeHookTrace(const sg_host_engine_runtime_t *runtime,
	uint32_t subject_index, uint32_t hook_index,
	sg_host_collision_contents_t mask,
	sg_host_hook_collision_t *collision_out,
	sg_host_collision_trace_t *trace_out)
{
	(void)runtime;
	(void)subject_index;
	(void)hook_index;
	(void)mask;
	(void)collision_out;
	(void)trace_out;
	return 0;
}

int SG_HostEngineRuntimePmove(const sg_host_engine_runtime_t *runtime,
	uint32_t subject_index, const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	(void)runtime;
	(void)subject_index;
	(void)request;
	(void)result_out;
	(void)error_out;
	return 0;
}

const sg_host_static_identity_t *SG_HostEngineRuntimeStaticIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	(void)runtime;
	return NULL;
}

int SG_HostEngineRuntimeOwnerHookCollision(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_observation_t *observation_out)
{
	(void)runtime;
	(void)subject_index;
	(void)hook_index;
	(void)target_index;
	(void)surface_flags;
	(void)observation_out;
	return 0;
}

int SG_HostEngineRuntimeOwnerHookPullInputs(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, vec3_t start_out, vec3_t bite_out)
{
	(void)runtime;
	(void)subject_index;
	(void)hook_index;
	(void)start_out;
	(void)bite_out;
	return 0;
}

static sg_rune_model_identity_t PublicationIdentity(float gravity)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x101);
	identity.entity_semantics_id = UINT64_C(0x202);
	identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	identity.source_set_identity = UINT64_C(0x404);
	identity.schema_id = UINT64_C(0x505);
	identity.producer_identity = UINT64_C(0x606);
	SetVector(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	SetVector(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = gravity;
	identity.physics.ground_acceleration =
		SG_HOST_ENGINE_GROUND_ACCELERATION;
	identity.physics.air_acceleration = SG_HOST_ENGINE_AIR_ACCELERATION;
	identity.physics.water_acceleration = SG_HOST_ENGINE_WATER_ACCELERATION;
	identity.physics.hook_acceleration = SG_HOST_ENGINE_HOOK_ACCELERATION;
	identity.physics.external_acceleration =
		SG_HOST_ENGINE_EXTERNAL_ACCELERATION;
	identity.physics.water_drag = SG_HOST_ENGINE_WATER_DRAG;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = SG_HOST_ENGINE_FRAME_MS;
	identity.physics.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return identity;
}

static fixture_t PublicationWorld(void)
{
	static unsigned char source_bytes[] = {
		0x67U, 0x72U, 0x6fU, 0x75U, 0x6eU, 0x64U, 0x2dU, 0x70U,
		0x75U, 0x62U, 0x6cU, 0x69U, 0x63U, 0x61U, 0x74U, 0x69U,
		0x6fU, 0x6eU
	};
	fixture_t fixture;
	uint32_t side;

	memset(&fixture, 0, sizeof(fixture));
	fixture.planes = calloc(8U, sizeof(*fixture.planes));
	fixture.nodes = calloc(2U, sizeof(*fixture.nodes));
	fixture.leaves = calloc(3U, sizeof(*fixture.leaves));
	fixture.leaf_brushes = calloc(1U, sizeof(*fixture.leaf_brushes));
	fixture.models = calloc(1U, sizeof(*fixture.models));
	fixture.brushes = calloc(1U, sizeof(*fixture.brushes));
	fixture.brush_sides = calloc(6U, sizeof(*fixture.brush_sides));
	if (!fixture.planes || !fixture.nodes || !fixture.leaves ||
		!fixture.leaf_brushes || !fixture.models || !fixture.brushes ||
		!fixture.brush_sides)
	{
		DestroyFixture(&fixture);
		return fixture;
	}
	SetPlane(&fixture.planes[0], 0.0f, 0.0f, 1.0f, -24.0f);
	SetPlane(&fixture.planes[1], 1.0f, 0.0f, 0.0f, 0.0f);
	fixture.nodes[0].plane = 0U;
	fixture.nodes[0].children[0] = 1;
	fixture.nodes[0].children[1] = -3;
	fixture.nodes[1].plane = 1U;
	fixture.nodes[1].children[0] = -1;
	fixture.nodes[1].children[1] = -2;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1U;
	fixture.leaves[1].cluster = 1;
	fixture.leaves[1].area = 2U;
	fixture.leaves[2].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[2].cluster = -1;
	fixture.leaves[2].area = 1U;
	fixture.leaves[2].first_leaf_brush = 0U;
	fixture.leaves[2].leaf_brush_count = 1U;
	fixture.leaf_brushes[0] = 0U;
	SetPlane(&fixture.planes[2], 1.0f, 0.0f, 0.0f, 4095.0f);
	SetPlane(&fixture.planes[3], -1.0f, 0.0f, 0.0f, 4096.0f);
	SetPlane(&fixture.planes[4], 0.0f, 1.0f, 0.0f, 4095.0f);
	SetPlane(&fixture.planes[5], 0.0f, -1.0f, 0.0f, 4096.0f);
	SetPlane(&fixture.planes[6], 0.0f, 0.0f, 1.0f, -24.125f);
	SetPlane(&fixture.planes[7], 0.0f, 0.0f, -1.0f, 4096.0f);
	fixture.brushes[0].first_side = 0U;
	fixture.brushes[0].side_count = 6U;
	fixture.brushes[0].contents = SG_HOST_CONTENTS_SOLID;
	for (side = 0U; side < 6U; side++)
	{
		fixture.brush_sides[side].plane = side + 2U;
		fixture.brush_sides[side].texinfo = -1;
	}
	fixture.models[0].headnode = 0;
	SetVector(fixture.models[0].mins.value,
		-4096.0f, -4096.0f, -4096.0f);
	SetVector(fixture.models[0].maxs.value, 4095.0f, 4095.0f, 4095.0f);
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = 8U;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = 2U;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = 3U;
	fixture.world.leaf_brushes = fixture.leaf_brushes;
	fixture.world.leaf_brush_count = 1U;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 1U;
	fixture.world.brushes = fixture.brushes;
	fixture.world.brush_count = 1U;
	fixture.world.brush_sides = fixture.brush_sides;
	fixture.world.brush_side_count = 6U;
	fixture.world.source_bytes = source_bytes;
	fixture.world.source_size = sizeof(source_bytes);
	if (!SG_BspWorldContentIdentity(source_bytes, sizeof(source_bytes),
			&fixture.world.content_identity) ||
		!SG_BspWorldEngineChecksum(source_bytes, sizeof(source_bytes),
			&fixture.world.engine_checksum))
	{
		DestroyFixture(&fixture);
		memset(&fixture, 0, sizeof(fixture));
	}
	return fixture;
}

static uint32_t PublicationFloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void InstallEngine(float gravity)
{
	memset(&gi, 0, sizeof(gi));
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_gravity_cvar, 0, sizeof(funky_gravity_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	memset(&ctf_flags_cvar, 0, sizeof(ctf_flags_cvar));
	gravity_cvar.value = gravity;
	maxvelocity_cvar.value = 2000.0f;
	gi.Pmove = Pmove;
	gi.cvar = PublicationCvar;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_gravity_cvar;
	ctfflags = &ctf_flags_cvar;
}

static void PublicationFixtureDestroy(publication_fixture_t *fixture)
{
	SG_PhaseCatalogPublicationOwnerDestroy(fixture->phase_owner);
	SG_HostLawPublicationOwnerDestroy(fixture->engine_authority);
	SG_ConfigurationSemanticsDestroy(fixture->semantics);
	SG_ConfigurationDestroy(fixture->configuration);
	DestroyFixture(&fixture->world);
	memset(fixture, 0, sizeof(*fixture));
}

static int PublicationFixtureInit(publication_fixture_t *fixture,
	float gravity)
{
	sg_rune_model_identity_t identity = PublicationIdentity(gravity);
	sg_host_collision_error_t host_error;
	sg_configuration_error_t configuration_error;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t limits;
	sg_host_law_result_t host_result;

	memset(fixture, 0, sizeof(*fixture));
	InstallEngine(gravity);
	fixture->world = PublicationWorld();
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world.world,
			&identity, &host_error) ||
		!SG_ConfigurationBuild(&fixture->authority, NULL,
			&fixture->configuration, &configuration_error))
		goto fail;
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture->authority,
			fixture->configuration, &limits, &fixture->semantics,
			&semantics_error))
		goto fail;
	host_result = SG_HostLawPublicationOwnerIssue(&fixture->authority,
		&fixture->engine_authority);
	if (host_result.status != SG_HOST_LAW_OK ||
		!SG_TestGroundPhasePublicationBuild(&fixture->authority,
			fixture->configuration, fixture->semantics, &fixture->phase_owner,
			&fixture->phase_catalog))
		goto fail;
	fixture->source.engine_authority = fixture->engine_authority;
	fixture->source.configuration = fixture->configuration;
	fixture->source.semantics = fixture->semantics;
	fixture->source.phase_catalog_owner = fixture->phase_owner;
	fixture->source.phase_catalog = fixture->phase_catalog;
	return 1;

fail:
	PublicationFixtureDestroy(fixture);
	return 0;
}

static int CandidateFromPublication(publication_fixture_t *fixture,
	sg_ground_capability_set_t **candidate_out)
{
	sg_ground_capability_publication_owner_t *owner = NULL;
	sg_ground_capability_publication_t *publication = NULL;
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_audit_result_t audit;
	const sg_phase_catalog_view_t *view = NULL;
	sg_ground_capability_set_t *candidate = NULL;
	uint32_t index;
	int ok = 0;

	if (!candidate_out || *candidate_out ||
		!SG_PhaseCatalogPublicationRead(fixture->phase_owner,
			fixture->phase_catalog, &view) || !view || view->phase_count == 0U)
		return 0;
	if (!SG_GroundCapabilityPublicationOwnerCreate(&owner) ||
		!SG_GroundCapabilityPublicationBuild(owner, &fixture->source,
			&publication, &audit) ||
		!SG_GroundCapabilityPublicationDescribe(owner, publication, &description))
		goto done;
	candidate = calloc(1U, sizeof(*candidate));
	if (!candidate)
		goto done;
	candidate->identity = description.identity;
	candidate->capability_count = description.fact_count;
	candidate->proved_portals = description.proved_portals;
	candidate->rejected_crossings = description.proven_empty_portals;
	candidate->proved_directions = description.proved_directions;
	candidate->rejected_directions = description.proven_empty_directions;
	candidate->pmove_frames = description.host_pmove_frames;
	if (candidate->capability_count != 0U)
	{
		candidate->capabilities = calloc((size_t)candidate->capability_count,
			sizeof(*candidate->capabilities));
		if (!candidate->capabilities)
			goto done;
	}
	for (index = 0U; index < candidate->capability_count; index++)
	{
		sg_ground_capability_publication_fact_t fact;
		sg_ground_capability_t *raw = &candidate->capabilities[index];
		uint32_t cursor;

		if (!SG_GroundCapabilityPublicationFact(owner, publication, index, &fact))
			goto done;
		raw->source_cell = SG_GROUND_CAPABILITY_INDEX_NONE;
		raw->destination_cell = SG_GROUND_CAPABILITY_INDEX_NONE;
		raw->source_region = SG_GROUND_CAPABILITY_INDEX_NONE;
		raw->destination_region = SG_GROUND_CAPABILITY_INDEX_NONE;
		raw->portal = SG_GROUND_CAPABILITY_INDEX_NONE;
		raw->source_phase = SG_GROUND_CAPABILITY_INDEX_NONE;
		raw->destination_phase = SG_GROUND_CAPABILITY_INDEX_NONE;
		for (cursor = 0U; cursor < fixture->configuration->cell_count; cursor++)
		{
			if (SG_RuneModelStableIdEqual(&fact.source_cell.value,
					&fixture->configuration->cells[cursor].id.value))
				raw->source_cell = cursor;
			if (SG_RuneModelStableIdEqual(&fact.destination_cell.value,
					&fixture->configuration->cells[cursor].id.value))
				raw->destination_cell = cursor;
		}
		for (cursor = 0U; cursor < fixture->semantics->region_count; cursor++)
		{
			if (fact.source_region_id == fixture->semantics->regions[cursor].id)
				raw->source_region = cursor;
			if (fact.destination_region_id ==
					fixture->semantics->regions[cursor].id)
				raw->destination_region = cursor;
		}
		for (cursor = 0U; cursor < fixture->configuration->portal_count; cursor++)
			if (SG_RuneModelStableIdEqual(&fact.portal.value,
					&fixture->configuration->portals[cursor].id.value))
				raw->portal = cursor;
		for (cursor = 0U; cursor < view->phase_count; cursor++)
		{
			if (SG_RuneModelStableIdEqual(&fact.source_phase.value,
					&view->phases[cursor].id.value))
				raw->source_phase = cursor;
			if (SG_RuneModelStableIdEqual(&fact.destination_phase.value,
					&view->phases[cursor].id.value))
				raw->destination_phase = cursor;
		}
		if (raw->source_cell == SG_GROUND_CAPABILITY_INDEX_NONE ||
			raw->destination_cell == SG_GROUND_CAPABILITY_INDEX_NONE ||
			raw->source_region == SG_GROUND_CAPABILITY_INDEX_NONE ||
			raw->destination_region == SG_GROUND_CAPABILITY_INDEX_NONE ||
			raw->source_phase == SG_GROUND_CAPABILITY_INDEX_NONE ||
			raw->destination_phase == SG_GROUND_CAPABILITY_INDEX_NONE)
			goto done;
		raw->kind = fact.kind;
		raw->source_witness = fact.source_witness;
		raw->destination_witness = fact.destination_witness;
		raw->initial_velocity = fact.initial_velocity;
		raw->observed_velocity = fact.observed_velocity;
		raw->displacement = fact.displacement;
		raw->duration_ms = fact.duration_ms;
		raw->acceleration = fact.acceleration;
		raw->gravity = fact.gravity;
		raw->physics_abi_id = fact.physics_abi_id;
		raw->flags = fact.flags;
	}
	*candidate_out = candidate;
	candidate = NULL;
	ok = 1;

done:
	SG_GroundCapabilityDestroy(candidate);
	SG_GroundCapabilityPublicationDestroy(owner, publication);
	SG_GroundCapabilityPublicationOwnerDestroy(owner);
	return ok;
}

static sg_ground_capability_set_t *CloneCandidate(
	const sg_ground_capability_set_t *source, uint32_t extra)
{
	sg_ground_capability_set_t *clone;

	if (source->capability_count > UINT32_MAX - extra)
		return NULL;
	clone = calloc(1U, sizeof(*clone));
	if (!clone)
		return NULL;
	*clone = *source;
	clone->capability_count += extra;
	clone->capabilities = calloc((size_t)clone->capability_count,
		sizeof(*clone->capabilities));
	if (!clone->capabilities)
	{
		free(clone);
		return NULL;
	}
	memcpy(clone->capabilities, source->capabilities,
		(size_t)source->capability_count * sizeof(*source->capabilities));
	return clone;
}

static void TestExactFactBits(void)
{
	sg_ground_capability_t left;
	sg_ground_capability_t right;

	memset(&left, 0, sizeof(left));
	right = left;
	CHECK(SG_GroundCapabilityFactBitsEqual(&left, &right));
	right.gravity = -0.0f;
	CHECK(!SG_GroundCapabilityFactBitsEqual(&left, &right));
	right = left;
	right.source_witness.value[1] = -0.0f;
	CHECK(!SG_GroundCapabilityFactBitsEqual(&left, &right));
	right = left;
	right.duration_ms.max_value = -0.0f;
	CHECK(!SG_GroundCapabilityFactBitsEqual(&left, &right));
	CHECK(!SG_GroundCapabilityFactBitsEqual(NULL, &right));
}

static void TestAuditRejections(void)
{
	publication_fixture_t fixture;
	sg_ground_capability_set_t *candidate = NULL;
	sg_ground_capability_set_t *mutant = NULL;
	sg_ground_capability_publication_owner_t *ground_owner = NULL;
	sg_ground_capability_publication_t *publication = NULL;
	sg_ground_capability_publication_fact_t published_before;
	sg_ground_capability_publication_fact_t published_after;
	sg_ground_capability_audit_result_t audit;
	sg_phase_catalog_publication_owner_t *cross_phase_owner = NULL;
	const sg_phase_catalog_publication_owner_t *saved_phase_owner;
	void (*saved_pmove)(pmove_t *pmove);

	CHECK(PublicationFixtureInit(&fixture, 800.0f));
	if (!fixture.configuration)
		return;
	CHECK(CandidateFromPublication(&fixture, &candidate));
	CHECK(candidate && candidate->capability_count > 2U);
	if (!candidate || candidate->capability_count <= 2U)
		goto done;
	CHECK(SG_GroundCapabilityAudit(&fixture.source, candidate, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_OK);
	CHECK(SG_GroundCapabilityPublicationOwnerCreate(&ground_owner));
	CHECK(SG_GroundCapabilityPublicationIssue(ground_owner, &fixture.source,
		candidate, &publication, &audit));
	CHECK(publication != NULL);
	CHECK(SG_GroundCapabilityPublicationFact(ground_owner, publication, 0U,
		&published_before));
	if (publication)
	{
		float saved_gravity = candidate->capabilities[0].gravity;

		candidate->capabilities[0].gravity = -1.0f;
		CHECK(SG_GroundCapabilityPublicationFact(ground_owner, publication, 0U,
			&published_after));
		CHECK(memcmp(&published_before, &published_after,
			sizeof(published_before)) == 0);
		CHECK(!SG_GroundCapabilityPublicationStorageOverlaps(ground_owner,
			publication, candidate->capabilities,
			(size_t)candidate->capability_count *
				sizeof(*candidate->capabilities)));
		candidate->capabilities[0].gravity = saved_gravity;
	}

	mutant = CloneCandidate(candidate, 0U);
	CHECK(mutant != NULL);
	if (mutant)
	{
		mutant->capability_count--;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_OMITTED_FACT);
		mutant->capability_count++;
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = CloneCandidate(candidate, 1U);
	CHECK(mutant != NULL);
	if (mutant)
	{
		mutant->capabilities[candidate->capability_count] =
			candidate->capabilities[candidate->capability_count - 1U];
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_DUPLICATE_FACT);
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = CloneCandidate(candidate, 1U);
	CHECK(mutant != NULL);
	if (mutant)
	{
		sg_ground_capability_t *invented =
			&mutant->capabilities[candidate->capability_count];
		uint32_t bits;

		*invented = candidate->capabilities[candidate->capability_count - 1U];
		bits = PublicationFloatBits(invented->source_witness.value[2]);
		bits++;
		memcpy(&invented->source_witness.value[2], &bits, sizeof(bits));
		CHECK(isfinite(invented->source_witness.value[2]));
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVENTED_FACT);
		CHECK(audit.invented_facts == 1U);
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = CloneCandidate(candidate, 0U);
	if (mutant)
	{
		sg_ground_capability_t swap = mutant->capabilities[0];

		mutant->capabilities[0] = mutant->capabilities[1];
		mutant->capabilities[1] = swap;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_NONCANONICAL_ORDER);
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = CloneCandidate(candidate, 0U);
	if (mutant)
	{
		mutant->capabilities[0].duration_ms.max_value += 0.125f;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = CloneCandidate(candidate, 0U);
	if (mutant)
	{
		mutant->pmove_frames++;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code ==
			SG_GROUND_CAPABILITY_AUDIT_COMPLETENESS_DISAGREEMENT);
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = calloc(1U, sizeof(*mutant));
	if (mutant)
	{
		mutant->identity = candidate->identity;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_OMITTED_FACT);
	}
	SG_GroundCapabilityDestroy(mutant);
	mutant = CloneCandidate(candidate, 0U);
	if (mutant)
	{
		mutant->identity.schema_id++;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, mutant, &audit));
		CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
	}
	SG_GroundCapabilityDestroy(mutant);
	{
		uint64_t saved_cell_id = fixture.configuration->cells[0].id.value.low;

		fixture.configuration->cells[0].id.value.low++;
		CHECK(!SG_GroundCapabilityAudit(&fixture.source, candidate, &audit));
		CHECK(audit.code ==
			SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED);
		fixture.configuration->cells[0].id.value.low = saved_cell_id;
	}

	saved_pmove = gi.Pmove;
	gi.Pmove = EmptyPmove;
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, candidate, &audit));
	CHECK(audit.code ==
		SG_GROUND_CAPABILITY_AUDIT_ENGINE_AUTHORITY_REJECTED);
	gi.Pmove = saved_pmove;

	CHECK(SG_PhaseCatalogPublicationOwnerCreate(&cross_phase_owner));
	saved_phase_owner = fixture.source.phase_catalog_owner;
	fixture.source.phase_catalog_owner = cross_phase_owner;
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, candidate, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED);
	fixture.source.phase_catalog_owner = saved_phase_owner;
	fixture.source.phase_catalog =
		(const sg_phase_catalog_publication_t *)(uintptr_t)UINT32_C(1);
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, candidate, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REJECTED);

done:
	SG_GroundCapabilityPublicationDestroy(ground_owner, publication);
	SG_GroundCapabilityPublicationOwnerDestroy(ground_owner);
	SG_PhaseCatalogPublicationOwnerDestroy(cross_phase_owner);
	SG_GroundCapabilityDestroy(candidate);
	PublicationFixtureDestroy(&fixture);
}

static void TestOwnerPublication(float gravity,
	uint64_t *fact_fingerprint_out)
{
	publication_fixture_t fixture;
	sg_ground_capability_publication_owner_t *owner = NULL;
	sg_ground_capability_publication_owner_t *cross_owner = NULL;
	sg_ground_capability_publication_t *publication = NULL;
	sg_ground_capability_publication_t *again = NULL;
	const sg_ground_capability_publication_t *stale;
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_publication_description_t description_again;
	sg_ground_capability_publication_fact_t fact;
	sg_ground_capability_publication_fact_t fact_again;
	sg_ground_capability_audit_result_t audit;
	uint64_t fingerprint = UINT64_C(1469598103934665603);
	uint32_t index;

	CHECK(PublicationFixtureInit(&fixture, gravity));
	if (!fixture.configuration)
		return;
	CHECK(SG_GroundCapabilityPublicationOwnerCreate(&owner));
	CHECK(SG_GroundCapabilityPublicationOwnerCreate(&cross_owner));
	CHECK(SG_GroundCapabilityPublicationBuild(owner, &fixture.source,
		&publication, &audit));
	if (!publication)
	{
		fprintf(stderr, "publication build failed: %s record=%u\n",
			SG_GroundCapabilityAuditCodeString(audit.code), audit.record);
		goto cleanup_sources;
	}
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_OK);
	CHECK(SG_GroundCapabilityPublicationDescribe(owner, publication,
		&description));
	CHECK(description.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE);
	CHECK(description.fact_count > 2U);
	CHECK(description.phase_count > 0U);
	CHECK(description.phase_transition_count > 0U);
	CHECK(description.pmove_abi.identity == SG_HOST_ENGINE_PMOVE_ABI_ID);
	CHECK(description.identity.physics.gravity == gravity);
	CHECK(description.host_pmove_frames > 0U);
	CHECK(description.proved_portals + description.proven_empty_portals ==
		description.portal_count);
	CHECK(description.proved_directions + description.proven_empty_directions ==
		description.portal_count * 2U);
	for (index = 0U; index < description.fact_count; index++)
	{
		CHECK(SG_GroundCapabilityPublicationFact(owner, publication, index,
			&fact));
		CHECK(fact.order == index);
		CHECK(fact.physics_abi_id == SG_HOST_ENGINE_PMOVE_ABI_ID);
		CHECK(fact.gravity == gravity);
		fingerprint ^= PublicationFloatBits(fact.observed_velocity.value[2]);
		fingerprint *= UINT64_C(1099511628211);
		fingerprint ^= (uint64_t)fact.kind;
		fingerprint *= UINT64_C(1099511628211);
	}
	CHECK(SG_GroundCapabilityPublicationBuild(owner, &fixture.source, &again,
		&audit));
	CHECK(publication != again);
	CHECK(SG_GroundCapabilityPublicationDescribe(owner, again,
		&description_again));
	CHECK(memcmp(&description, &description_again, sizeof(description)) == 0);
	for (index = 0U; index < description.fact_count; index++)
	{
		CHECK(SG_GroundCapabilityPublicationFact(owner, publication, index,
			&fact));
		CHECK(SG_GroundCapabilityPublicationFact(owner, again, index,
			&fact_again));
		CHECK(memcmp(&fact, &fact_again, sizeof(fact)) == 0);
	}
	CHECK(SG_GroundCapabilityPublicationFact(owner, publication, 0U, &fact));
	fact.gravity = -1.0f;
	CHECK(SG_GroundCapabilityPublicationFact(owner, publication, 0U,
		&fact_again));
	CHECK(fact_again.gravity == gravity);
	CHECK(!SG_GroundCapabilityPublicationDescribe(cross_owner, publication,
		&description_again));
	CHECK(!SG_GroundCapabilityPublicationDescribe(owner,
		(const sg_ground_capability_publication_t *)(uintptr_t)UINT32_C(1),
		&description_again));
	CHECK(!SG_GroundCapabilityPublicationStorageOverlaps(owner, publication,
		&fixture.source, sizeof(fixture.source)));

	SG_PhaseCatalogPublicationOwnerDestroy(fixture.phase_owner);
	fixture.phase_owner = NULL;
	fixture.phase_catalog = NULL;
	SG_HostLawPublicationOwnerDestroy(fixture.engine_authority);
	fixture.engine_authority = NULL;
	SG_ConfigurationSemanticsDestroy(fixture.semantics);
	fixture.semantics = NULL;
	SG_ConfigurationDestroy(fixture.configuration);
	fixture.configuration = NULL;
	DestroyFixture(&fixture.world);
	CHECK(SG_GroundCapabilityPublicationDescribe(owner, publication,
		&description_again));
	CHECK(memcmp(&description, &description_again, sizeof(description)) == 0);
	CHECK(SG_GroundCapabilityPublicationFact(owner, publication, 0U,
		&fact_again));
	CHECK(fact_again.gravity == gravity);

	stale = publication;
	SG_GroundCapabilityPublicationDestroy(owner, publication);
	publication = NULL;
	CHECK(!SG_GroundCapabilityPublicationDescribe(owner, stale,
		&description_again));
	SG_GroundCapabilityPublicationDestroy(owner,
		(sg_ground_capability_publication_t *)(uintptr_t)stale);
	SG_GroundCapabilityPublicationDestroy(owner, again);

cleanup_sources:
	SG_GroundCapabilityPublicationOwnerDestroy(cross_owner);
	SG_GroundCapabilityPublicationOwnerDestroy(owner);
	PublicationFixtureDestroy(&fixture);
	*fact_fingerprint_out = fingerprint;
}

static void TestInvalidArguments(void)
{
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_publication_t *publication = NULL;

	memset(&audit, 0xa5, sizeof(audit));
	CHECK(!SG_GroundCapabilityAudit(NULL, NULL, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	CHECK(!SG_GroundCapabilityPublicationIssue(NULL, NULL, NULL,
		&publication, &audit));
	CHECK(publication == NULL);
	CHECK(strcmp(SG_GroundCapabilityAuditCodeString(
		SG_GROUND_CAPABILITY_AUDIT_OK), "ok") == 0);
}

int main(void)
{
	uint64_t gravity_100_fingerprint = 0U;
	uint64_t gravity_800_fingerprint = 0U;

	if (RunGroundCapabilityTests() != 0)
		return 1;
	failures = 0;
	TestExactFactBits();
	TestInvalidArguments();
	TestAuditRejections();
	TestOwnerPublication(100.0f, &gravity_100_fingerprint);
	TestOwnerPublication(800.0f, &gravity_800_fingerprint);
	CHECK(gravity_100_fingerprint != gravity_800_fingerprint);
	if (failures)
	{
		fprintf(stderr, "%d ground publication test failure(s)\n", failures);
		return 1;
	}
	puts("ground capability publication checks passed at gravity 100 and 800");
	return 0;
}
