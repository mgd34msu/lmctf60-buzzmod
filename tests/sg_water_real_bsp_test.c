#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../g_local.h"
#undef world
#include "slipgate/sg_bsp_world.h"
#include "slipgate/sg_host_collision.h"
#include "slipgate/sg_host_law_publication.h"
#include "slipgate/sg_host_law_publication_private.h"
#include "slipgate/sg_host_hook_law.h"
#include "slipgate/sg_host_mechanism_law.h"
#include "slipgate/sg_host_pmove.h"

void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);
int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);

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

static sg_rune_model_identity_t Identity(float gravity);

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

static cvar_t *TestCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	return strcmp(name, "sv_airaccelerate") == 0 ?
		&airaccelerate_cvar : NULL;
}

int SG_HostEnginePmoveABI(sg_host_engine_pmove_abi_t *abi_out)
{
	if (!abi_out)
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

int SG_HostEnginePhysicsLaw(sg_rune_physics_parameters_t *law_out)
{
	if (!law_out)
		return 0;
	*law_out = Identity(800.0f).physics;
	return 1;
}

int SG_HostEngineHullProfiles(sg_rune_hull_profile_t *standing_out,
	sg_rune_hull_profile_t *crouching_out)
{
	sg_rune_model_identity_t identity = Identity(800.0f);

	if (!standing_out || !crouching_out)
		return 0;
	*standing_out = identity.standing_hull;
	*crouching_out = identity.crouching_hull;
	return 1;
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

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static sg_rune_model_identity_t Identity(float gravity)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x5741544552425350);
	identity.entity_semantics_id = UINT64_C(0x5741544552454e54);
	identity.physics_abi_id = UINT64_C(0x5741544552504859);
	identity.source_set_identity = UINT64_C(0x5741544552534f55);
	identity.schema_id = UINT64_C(0x5741544552534348);
	identity.producer_identity = UINT64_C(0x574154455250524f);
	Set3(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = gravity;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 25U;
	return identity;
}

static int AxialBrushBounds(const sg_bsp_world_t *world,
	const sg_bsp_brush_t *brush, float mins[3], float maxs[3])
{
	uint32_t axis;
	uint32_t side;

	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = -FLT_MAX;
		maxs[axis] = FLT_MAX;
	}
	for (side = 0U; side < brush->side_count; side++)
	{
		const sg_bsp_brush_side_t *brush_side =
			&world->brush_sides[brush->first_side + side];
		const sg_bsp_plane_t *plane = &world->planes[brush_side->plane];

		for (axis = 0U; axis < 3U; axis++)
		{
			uint32_t other = (axis + 1U) % 3U;
			uint32_t last = (axis + 2U) % 3U;

			if (plane->normal.value[other] != 0.0f ||
				plane->normal.value[last] != 0.0f)
				continue;
			if (plane->normal.value[axis] == 1.0f)
				maxs[axis] = fminf(maxs[axis], plane->distance);
			else if (plane->normal.value[axis] == -1.0f)
				mins[axis] = fmaxf(mins[axis], -plane->distance);
		}
	}
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
			mins[axis] >= maxs[axis])
			return 0;
	return 1;
}

static int FindHullValidWater(const sg_bsp_world_t *world,
	const sg_host_collision_authority_t *authority, float origin_out[3],
	sg_host_collision_pose_t *pose_out, uint32_t *medium_brushes_out)
{
	static const float fractions[] = { 0.25f, 0.5f, 0.75f };
	uint32_t brush;
	uint32_t medium_brushes = 0U;

	for (brush = 0U; brush < world->brush_count; brush++)
	{
		const sg_bsp_brush_t *record = &world->brushes[brush];
		float mins[3];
		float maxs[3];
		uint32_t x;
		uint32_t y;
		uint32_t z;

		if ((record->contents & SG_HOST_MASK_WATER) == 0)
			continue;
		medium_brushes++;
		if (!AxialBrushBounds(world, record, mins, maxs))
			continue;
		for (x = 0U; x < 3U; x++)
			for (y = 0U; y < 3U; y++)
				for (z = 0U; z < 3U; z++)
				{
					float origin[3] = {
						mins[0] + (maxs[0] - mins[0]) * fractions[x],
						mins[1] + (maxs[1] - mins[1]) * fractions[y],
						mins[2] + (maxs[2] - mins[2]) * fractions[z]
					};

					if (origin[0] < -4096.0f || origin[0] > 4095.875f ||
						origin[1] < -4096.0f || origin[1] > 4095.875f ||
						origin[2] < -4096.0f || origin[2] > 4095.875f)
						continue;
					if (SG_HostCollisionClassifyPose(authority, NULL, origin,
						SG_RUNE_STANCE_STANDING, pose_out) && pose_out->valid &&
						pose_out->water_level != 0U &&
						(pose_out->water_type & SG_HOST_MASK_WATER) != 0)
					{
						memcpy(origin_out, origin, sizeof(origin));
						*medium_brushes_out = medium_brushes;
						return 1;
					}
				}
	}
	*medium_brushes_out = medium_brushes;
	return 0;
}

static int RunGravityRegime(const sg_bsp_world_t *world,
	const float origin[3], int hook_zero_gravity)
{
	sg_rune_model_identity_t identity = Identity(800.0f);
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error;
	uint32_t axis;

	if (!SG_HostCollisionInit(&authority, world, &identity, &collision_error))
		return 0;
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.gravity = 800;
	for (axis = 0U; axis < 3U; axis++)
		request.state.origin[axis] = (short)lroundf(origin[axis] * 8.0f);
	request.previous_state = request.state;
	if (hook_zero_gravity)
	{
		request.hook_law_id = SG_HOST_PMOVE_HOOK_LAW_ID;
		request.hook_attached = 1U;
		request.hook_length =
			SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO - 1U;
	}
	if (!SG_HostPmoveEvaluateFrame(&authority, NULL, Pmove, &request,
		&result, &pmove_error))
		return 0;
	return result.evaluated_steps == 4U && result.elapsed_ms == 100U &&
		result.water_level != 0 &&
		(result.water_type & SG_HOST_MASK_WATER) != 0 &&
		(hook_zero_gravity ?
		 result.gravity == 0.0f &&
			result.gravity_law_id == SG_HOST_PMOVE_HOOK_LAW_ID :
			 result.gravity == 800.0f && result.gravity_law_id == 0U);
}

static sg_host_static_identity_t ConstructionIdentity(
	const sg_bsp_world_t *world)
{
	sg_rune_model_identity_t model = Identity(800.0f);
	sg_host_static_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_identity = world->content_identity;
	identity.bsp_bytes = (uint64_t)world->source_size;
	identity.engine_checksum = world->engine_checksum;
	identity.entity_crc32 = 1U;
	identity.host_physics_epoch = 1U;
	identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	identity.standing_hull = model.standing_hull;
	identity.crouching_hull = model.crouching_hull;
	identity.physics = model.physics;
	return identity;
}

static int RunConstructionGravity(
	const sg_host_law_construction_t *construction, const float origin[3],
	int hook_zero_gravity)
{
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error = SG_HOST_PMOVE_ERROR_NONE;
	uint32_t axis;

	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.gravity = 800;
	for (axis = 0U; axis < 3U; axis++)
		request.state.origin[axis] = (short)lroundf(origin[axis] * 8.0f);
	request.previous_state = request.state;
	if (hook_zero_gravity)
	{
		request.hook_law_id = SG_HOST_PMOVE_HOOK_LAW_ID;
		request.hook_attached = 1U;
		request.hook_length =
			SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO - 1U;
	}
	return SG_HostLawConstructionPmove(construction, NULL, &request, &result,
			&pmove_error).status == SG_HOST_LAW_OK &&
		result.evaluated_steps == 4U && result.elapsed_ms == 100U &&
		result.water_level != 0 &&
		(result.water_type & SG_HOST_MASK_WATER) != 0 &&
		(hook_zero_gravity ?
			result.gravity == 0.0f &&
				result.gravity_law_id == SG_HOST_PMOVE_HOOK_LAW_ID :
			result.gravity == 800.0f && result.gravity_law_id == 0U);
}

static int RunConstructionProof(const sg_bsp_world_t *world,
	const float origin[3])
{
	sg_rune_model_identity_t model_identity = Identity(800.0f);
	sg_host_static_identity_t static_identity = ConstructionIdentity(world);
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_construction_t *construction = NULL;
	sg_host_law_construction_view_t construction_view;
	sg_host_collision_pose_t direct_pose;
	sg_host_collision_pose_t pose;
	sg_host_collision_trace_t direct_trace;
	sg_host_collision_trace_t trace;
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	int success = 0;

	model_identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	if (!SG_HostCollisionInit(&authority, world, &model_identity,
			&collision_error) ||
		!SG_HostCollisionClassifyPose(&authority, NULL, origin,
			SG_RUNE_STANCE_STANDING, &direct_pose) ||
		!SG_HostCollisionTrace(&authority, NULL, origin, zero, zero, origin,
			SG_HOST_CONTENTS_SOLID, &direct_trace) ||
		SG_HostLawPublicationOwnerIssueStatic(&static_identity,
			&publication).status != SG_HOST_LAW_OK ||
		SG_HostLawPublicationOwnerConstructionIssue(publication, &authority,
			&construction).status != SG_HOST_LAW_OK ||
		SG_HostLawConstructionRead(construction, &construction_view).status !=
			SG_HOST_LAW_OK || construction_view.current != 1U ||
		construction_view.geometry.bsp_bytes != (uint64_t)world->source_size ||
		construction_view.geometry.engine_checksum != world->engine_checksum ||
		memcmp(&construction_view.geometry.bsp_identity,
			&world->content_identity, sizeof(world->content_identity)) != 0 ||
		SG_HostLawConstructionClassifyPose(construction, NULL, origin,
			SG_RUNE_STANCE_STANDING, &pose).status != SG_HOST_LAW_OK ||
		pose.valid != direct_pose.valid || pose.stance != direct_pose.stance ||
		memcmp(&pose.hull, &direct_pose.hull, sizeof(pose.hull)) != 0 ||
		memcmp(&pose.occupancy, &direct_pose.occupancy,
			sizeof(pose.occupancy)) != 0 ||
		memcmp(&pose.support, &direct_pose.support,
			sizeof(pose.support)) != 0 ||
		pose.supported != direct_pose.supported ||
		pose.support_is_mover != direct_pose.support_is_mover ||
		pose.water_level != direct_pose.water_level ||
		pose.water_type != direct_pose.water_type ||
		pose.gravity != direct_pose.gravity ||
		pose.physics_abi_id != direct_pose.physics_abi_id ||
		SG_HostLawConstructionCollisionTrace(construction, NULL, origin, zero,
			zero, origin, SG_HOST_CONTENTS_SOLID, &trace).status !=
			SG_HOST_LAW_OK ||
		memcmp(&trace, &direct_trace, sizeof(trace)) != 0 ||
		!RunConstructionGravity(construction, origin, 0) ||
		!RunConstructionGravity(construction, origin, 1))
		goto done;
	success = 1;

done:
	SG_HostLawConstructionDestroy(construction);
	SG_HostLawPublicationOwnerDestroy(publication);
	return success;
}

int main(int argc, char **argv)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_rune_model_identity_t identity = Identity(800.0f);
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_host_collision_pose_t pose;
	float origin[3];
	uint32_t medium_brushes = 0U;

	memset(&gi, 0, sizeof(gi));
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_gravity_cvar, 0, sizeof(funky_gravity_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	memset(&ctf_flags_cvar, 0, sizeof(ctf_flags_cvar));
	gi.Pmove = Pmove;
	gi.cvar = TestCvar;
	gravity_cvar.value = 800.0f;
	maxvelocity_cvar.value = 2000.0f;
	funky_gravity_cvar.value = 0.0f;
	airaccelerate_cvar.value = 0.0f;
	ctf_flags_cvar.value = 0.0f;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_gravity_cvar;
	ctfflags = &ctf_flags_cvar;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s WATER_MAP.bsp\n", argv[0]);
		return 2;
	}
	if (!SG_BspWorldLoadFile(argv[1], &world, &bsp_error))
	{
		fprintf(stderr, "real water BSP load failed: %s lump=%u record=%u\n",
			SG_BspWorldErrorString(bsp_error.code), (unsigned)bsp_error.lump,
			(unsigned)bsp_error.record);
		return 1;
	}
	if (!SG_HostCollisionInit(&authority, world, &identity, &collision_error) ||
		!FindHullValidWater(world, &authority, origin, &pose, &medium_brushes) ||
		!RunGravityRegime(world, origin, 0) ||
		!RunGravityRegime(world, origin, 1) ||
		!RunConstructionProof(world, origin))
	{
		fprintf(stderr,
			"real water BSP proof failed: brushes=%u collision_error=%d\n",
			medium_brushes, (int)collision_error);
		SG_BspWorldDestroy(world);
		return 1;
	}
	printf("real water BSP proof: %s brushes=%u witness=[%.3f %.3f %.3f] "
		"medium=%d level=%d gravity=800 hook_gravity=0 construction=bound\n",
		argv[1],
		medium_brushes, origin[0], origin[1], origin[2], pose.water_type,
		pose.water_level);
	SG_BspWorldDestroy(world);
	return 0;
}
