#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_host_law_publication.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"
#include "../slipgate/sg_hooks.h"
#include "../slipgate/sg_action_contract.generated.h"
#include "../slipgate/sg_weapon_host_constants.h"

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);

sg_host_t sg_host;
cvar_t *sv_gravity;
cvar_t *sv_maxvelocity;
cvar_t *want_funky_gravity;

static cvar_t gravity_cvar;
static cvar_t maxvelocity_cvar;
static cvar_t funky_gravity_cvar;
static cvar_t airaccelerate_cvar;
static sg_bsp_world_t world;
static int bad_standing_hull;
static int collision_calls;
static int pmove_calls;
static int hook_calls;
static const sg_host_collision_authority_t *last_collision_authority;

static cvar_t *TestCvar(const char *name, const char *value, int flags)
{
	(void)value;
	(void)flags;
	if (strcmp(name, "sv_airaccelerate") == 0)
		return &airaccelerate_cvar;
	return NULL;
}

static void TestPmove(pmove_t *pmove)
{
	const int crouching = (pmove->s.pm_flags & PMF_DUCKED) != 0;

	pmove_calls++;
	pmove->mins[0] = -16.0f;
	pmove->mins[1] = -16.0f;
	pmove->mins[2] = -24.0f;
	pmove->maxs[0] = bad_standing_hull && !crouching ? 17.0f : 16.0f;
	pmove->maxs[1] = 16.0f;
	pmove->maxs[2] = crouching ? 4.0f : 32.0f;
}

static void OtherPmove(pmove_t *pmove)
{
	TestPmove(pmove);
}

int SG_HostCollisionTrace(const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	(void)scene;
	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	(void)mask;
	collision_calls++;
	last_collision_authority = authority;
	memset(trace_out, 0, sizeof(*trace_out));
	trace_out->fraction = 1.0f;
	return 1;
}

int SG_HostPmoveEvaluateFrame(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, sg_host_pmove_function_t host_pmove,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	(void)scene;
	assert(authority != NULL);
	assert(host_pmove == TestPmove);
	assert(request != NULL);
	pmove_calls++;
	memset(result_out, 0, sizeof(*result_out));
	result_out->evaluated_steps = 4U;
	result_out->elapsed_ms = 100U;
	result_out->gravity = authority->identity.physics.gravity;
	result_out->physics_abi_id = authority->identity.physics_abi_id;
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return 1;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	(void)start;
	(void)bite;
	hook_calls++;
	VectorSet(velocity, 8.0f, 0.0f, 0.0f);
	return 321;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x101);
	identity.entity_semantics_id = UINT64_C(0x202);
	identity.physics_abi_id = UINT64_C(0x303);
	identity.source_set_identity = UINT64_C(0x404);
	identity.schema_id = UINT64_C(0x505);
	identity.producer_identity = UINT64_C(0x606);
	identity.standing_hull.mins.value[0] = -16.0f;
	identity.standing_hull.mins.value[1] = -16.0f;
	identity.standing_hull.mins.value[2] = -24.0f;
	identity.standing_hull.maxs.value[0] = 16.0f;
	identity.standing_hull.maxs.value[1] = 16.0f;
	identity.standing_hull.maxs.value[2] = 32.0f;
	identity.crouching_hull.mins = identity.standing_hull.mins;
	identity.crouching_hull.maxs = identity.standing_hull.maxs;
	identity.crouching_hull.maxs.value[2] = 4.0f;
	identity.physics.gravity = 800.0f;
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

static sg_host_collision_authority_t Authority(void)
{
	sg_host_collision_authority_t authority;

	authority.world = &world;
	authority.identity = Identity();
	return authority;
}

static sg_host_law_publication_t *IssueAtGravity(float gravity)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	gravity_cvar.value = gravity;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_OK);
	assert(publication != NULL);
	return publication;
}

static sg_host_law_view_t Read(const sg_host_law_publication_t *publication)
{
	sg_host_law_view_t view;
	sg_host_law_result_t result;

	memset(&view, 0xa5, sizeof(view));
	result = SG_HostLawPublicationRead(publication, &view);
	assert(result.status == SG_HOST_LAW_OK);
	return view;
}

static void ExpectMismatch(const sg_host_law_publication_t *publication,
	const sg_host_law_view_t *expected, sg_host_law_field_t field)
{
	const sg_host_law_result_t result =
		SG_HostLawPublicationMatch(publication, expected);

	assert(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	assert(result.field == field);
	assert(result.expected_bits != result.observed_bits);
}

static void TestIssueAndCanonicalIdentity(void)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_view_t first;
	sg_host_law_view_t second;
	sg_host_law_result_t result;

	gravity_cvar.value = 800.0f;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_OK);
	assert(publication != NULL);
	authority.identity.physics_abi_id = UINT64_C(0xffff);
	first = Read(publication);
	assert(first.version == SG_HOST_LAW_PUBLICATION_VERSION);
	assert(first.identity.bsp_content_id == UINT64_C(0x101));
	assert(first.identity.physics_abi_id == UINT64_C(0x303));
	assert(first.identity.producer_identity == UINT64_C(0x606));
	assert(first.identity.physics.gravity == 800.0f);
	assert(first.identity.physics.frame_ms == 100U);
	assert(first.identity.physics.substep_ms == 25U);
	assert(first.hook_fire_speed == SG_HOST_HOOK_FIRE_SPEED);
	assert(first.hook_pull_speed == SG_HOST_HOOK_PULL_SPEED);
	assert(first.hook_initial_damage == SG_HOST_HOOK_INITIAL_DAMAGE);
	assert(first.hook_attached_damage == SG_HOST_HOOK_ATTACHED_DAMAGE);
	assert(first.hook_health == SG_HOST_HOOK_HEALTH);
	assert(first.action_contract_crc32 == SG_RUNE_ACTION_CONTRACT_CRC32);
	assert(first.mechanism_contract_crc32 == SG_MECHANISM_CONTRACT_CRC32);
	assert(first.identity.standing_hull.mins.value[2] == -24.0f);
	assert(first.identity.standing_hull.maxs.value[2] == 32.0f);
	assert(first.identity.crouching_hull.maxs.value[2] == 4.0f);
	first.identity.source_set_identity = 0U;
	first.identity.physics.gravity = 777.0f;
	second = Read(publication);
	assert(second.identity.source_set_identity == UINT64_C(0x404));
	assert(second.identity.physics.gravity == 800.0f);
	assert(SG_HostLawPublicationRevalidateProduction(publication).status ==
		SG_HOST_LAW_OK);
	SG_HostLawPublicationDestroy(publication);
}

static void TestDriftAndRejectedLaws(void)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_view_t expected;
	sg_host_law_result_t result;

	gravity_cvar.value = 800.0f;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_OK);
	expected = Read(publication);
	expected.identity.physics.water_drag = 2.0f;
	ExpectMismatch(publication, &expected, SG_HOST_LAW_FIELD_WATER_DRAG);
	expected = Read(publication);
	expected.identity.bsp_content_id++;
	ExpectMismatch(publication, &expected, SG_HOST_LAW_FIELD_BSP_CONTENT);
	expected = Read(publication);
	expected.mechanism_contract_crc32++;
	ExpectMismatch(publication, &expected,
		SG_HOST_LAW_FIELD_MECHANISM_CONTRACT);
	gravity_cvar.value = 100.0f;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	assert(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	assert(result.field == SG_HOST_LAW_FIELD_GRAVITY);
	gravity_cvar.value = 800.0f;
	sg_host.pmove = OtherPmove;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	assert(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	assert(result.field == SG_HOST_LAW_FIELD_PMOVE_LAW);
	sg_host.pmove = TestPmove;
	SG_HostLawPublicationDestroy(publication);
	publication = NULL;

	authority = Authority();
	authority.identity.schema_id = 0U;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	assert(publication == NULL);
	authority = Authority();
	airaccelerate_cvar.value = 1.0f;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW);
	assert(result.field == SG_HOST_LAW_FIELD_AIRACCELERATE);
	airaccelerate_cvar.value = 0.0f;
	maxvelocity_cvar.value = 799.0f;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW);
	assert(result.field == SG_HOST_LAW_FIELD_MAXVELOCITY);
	maxvelocity_cvar.value = 2000.0f;
	bad_standing_hull = 1;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW);
	assert(result.field == SG_HOST_LAW_FIELD_STANDING_HULL_MAXS);
	assert(result.element == 0U);
	bad_standing_hull = 0;
	authority.identity.physics.gravity = 100.0f;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW);
	assert(result.field == SG_HOST_LAW_FIELD_GRAVITY);
}

static void TestCapturedLawExecution(void)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = NULL;
	sg_host_collision_trace_t trace;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	vec3_t start = { 0.0f, 0.0f, 0.0f };
	vec3_t bite = { 100.0f, 0.0f, 0.0f };
	vec3_t velocity;
	int rope_length;
	sg_host_law_result_t result;

	gravity_cvar.value = 800.0f;
	result = SG_HostLawPublicationIssue(&authority, &publication);
	assert(result.status == SG_HOST_LAW_OK);
	pmove_calls = 0;
	memset(&trace, 0xa5, sizeof(trace));
	result = SG_HostLawPublicationCollisionTrace(publication, NULL, start,
		start, start, bite, SG_HOST_MASK_PLAYER_SOLID, &trace);
	assert(result.status == SG_HOST_LAW_OK);
	assert(collision_calls == 1);
	assert(last_collision_authority != NULL);
	assert(last_collision_authority->world == &world);
	assert(last_collision_authority->identity.physics_abi_id ==
		UINT64_C(0x303));
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	assert(result.status == SG_HOST_LAW_OK);
	assert(pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	assert(pmove_result.elapsed_ms == 100U);
	assert(pmove_calls == 1);
	memset(velocity, 0, sizeof(velocity));
	result = SG_HostLawPublicationHookPullVelocity(publication, start, bite,
		velocity, &rope_length);
	assert(result.status == SG_HOST_LAW_OK);
	assert(hook_calls == 1);
	assert(rope_length == 321);
	assert(velocity[0] == 8.0f);
	SG_HostLawPublicationDestroy(publication);
}

static void TestBoundaryErrors(void)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = IssueAtGravity(800.0f);
	sg_host_law_publication_t *occupied = publication;
	sg_host_law_view_t view;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationIssue(&authority, &occupied);
	assert(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	result = SG_HostLawPublicationRead(NULL, &view);
	assert(result.status == SG_HOST_LAW_CORRUPT_PUBLICATION);
	assert(view.version == 0U);
	result = SG_HostLawPublicationRead(publication, NULL);
	assert(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	result = SG_HostLawPublicationMatch(publication, NULL);
	assert(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	result = SG_HostLawPublicationCollisionTrace(publication, NULL, NULL,
		NULL, NULL, NULL, 0U, NULL);
	assert(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	result = SG_HostLawPublicationHookPullVelocity(publication, NULL, NULL,
		NULL, NULL);
	assert(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	assert(strcmp(SG_HostLawStatusString(SG_HOST_LAW_PRODUCTION_DRIFT),
		"production law drift") == 0);
	assert(strcmp(SG_HostLawFieldString(SG_HOST_LAW_FIELD_SOURCE_SET),
		"source set") == 0);
	SG_HostLawPublicationDestroy(publication);
}

int main(void)
{
	memset(&sg_host, 0, sizeof(sg_host));
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_gravity_cvar, 0, sizeof(funky_gravity_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	sg_host.pmove = TestPmove;
	sg_host.cvar = TestCvar;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_gravity_cvar;
	maxvelocity_cvar.value = 2000.0f;
	airaccelerate_cvar.value = 0.0f;

	TestIssueAndCanonicalIdentity();
	TestDriftAndRejectedLaws();
	TestCapturedLawExecution();
	TestBoundaryErrors();
	puts("host-law publication tests passed");
	return 0;
}
