#include <stdio.h>
#include <string.h>

#include "sg_compound_hook_game_events_fixture.h"

sg_bot_t sg_bots[SG_MAXBOTS];
level_locals_t level;
vec3_t vec3_origin;
edict_t *g_edicts;

static int failures;
static int at_top;
static int linked_calls;
static int attached_calls;
static int pull_calls;
static int release_calls;
static int evicted_calls;
static sg_compound_guard_result_t evicted_result;
static sg_compound_guard_observation_t bolt_observation;
static edict_t *observed_bolt;
static int observe_calls;
static int non_unit_forward;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

int SG_MoverSubjectValid(const sg_mover_subject_t *subject)
{
	return subject && subject->kind >= SG_MOVER_SUBJECT_CLIENT &&
	       subject->kind <= SG_MOVER_SUBJECT_HOOK_BOLT &&
	       subject->edict_key > 0 && subject->generation != 0U;
}

sg_compound_guard_observation_t SG_CompoundGuardGameHookObserve(
	edict_t *client, const sg_mover_subject_t *subject, edict_t **current)
{
	CHECK(client == sg_bots[0].ent);
	CHECK(subject->edict_key == 17 && subject->generation == 29U);
	observe_calls++;
	if (bolt_observation == SG_COMPOUND_GUARD_YES &&
	    !client->client->hook)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	*current = bolt_observation == SG_COMPOUND_GUARD_YES ? observed_bolt : NULL;
	return bolt_observation;
}

sg_compound_guard_result_t SG_CompoundGuardGameBoltEvicted(
	edict_t *client, edict_t *bolt)
{
	CHECK(client == sg_bots[0].ent);
	CHECK(bolt == observed_bolt);
	evicted_calls++;
	return evicted_result;
}

qboolean SG_CompoundHookGameAtTop(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot)
{
	CHECK(bot == &sg_bots[0]);
	CHECK(snapshot == &bot->compound_hook_live.snapshot ||
	    memcmp(snapshot, &bot->compound_hook_live.snapshot,
	        sizeof(*snapshot)) == 0);
	return at_top;
}

qboolean SG_CompoundHookGamePose(const edict_t *entity,
	sg_replay_pose_t *pose)
{
	if (!entity || !pose)
		return false;
	memset(pose, 0, sizeof(*pose));
	VectorCopy(entity->s.origin, pose->origin);
	VectorCopy(entity->velocity, pose->velocity);
	return true;
}

qboolean SG_CompoundHookGameObservation(sg_bot_t *bot,
	const edict_t *entity, sg_replay_observation_t *observation)
{
	if (!bot || entity != bot->ent || !observation)
		return false;
	memset(observation, 0, sizeof(*observation));
	return true;
}

static sg_compound_hook_live_host_result_t AuthorizeEvent(void *context,
	const sg_compound_hook_live_snapshot_t *snapshot,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt)
{
	return SG_CompoundHookGameAuthorizeEvent(context, snapshot, event, bolt);
}

qboolean SG_CompoundHookGameHost(sg_bot_t *bot,
	sg_compound_hook_live_host_t *host)
{
	if (!bot || !host)
		return false;
	memset(host, 0, sizeof(*host));
	host->context = bot;
	host->event_authorize = AuthorizeEvent;
	return true;
}

void AngleVectors(vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	(void)angles;
	if (forward)
		VectorSet(forward, non_unit_forward ? 2.0f : 1.0f, 0.0f, 0.0f);
	if (right)
		VectorSet(right, 0.0f, 1.0f, 0.0f);
	if (up)
		VectorSet(up, 0.0f, 0.0f, 1.0f);
}

vec_t VectorNormalize(vec3_t value)
{
	vec_t length = sqrtf(DotProduct(value, value));

	if (length > 0.0f)
	{
		value[0] /= length;
		value[1] /= length;
		value[2] /= length;
	}
	return length;
}

void VectorScale(vec3_t in, vec_t scale, vec3_t out)
{
	out[0] = in[0] * scale;
	out[1] = in[1] * scale;
	out[2] = in[2] * scale;
}

void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	(void)viewheight;
	(void)hand;
	(void)forward;
	(void)right;
	VectorCopy(origin, start);
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	(void)start;
	(void)bite;
	VectorSet(velocity, 4.0f, 5.0f, 6.0f);
	return 120;
}

static sg_compound_hook_live_result_t PureEvent(
	sg_compound_hook_live_host_t const *host,
	sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt)
{
	sg_compound_hook_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = host->event_authorize(host->context, &state->snapshot,
	    event, bolt) == SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ?
	    SG_COMPOUND_HOOK_LIVE_RUNNING : SG_COMPOUND_HOOK_LIVE_RECOVERING;
	result.failure = result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING ?
	    SG_COMPOUND_HOOK_LIVE_FAILURE_NONE :
	    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY;
	return result;
}

sg_compound_hook_live_result_t SG_CompoundHookLiveLinked(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	(void)pose;
	(void)observation;
	CHECK(frame_serial == 40);
	linked_calls++;
	return PureEvent(host, state, SG_COMPOUND_HOOK_LIVE_EVENT_LINKED, bolt);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveAttached(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose)
{
	(void)pose;
	CHECK(frame_serial == 41);
	attached_calls++;
	return PureEvent(host, state, SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED, bolt);
}

sg_compound_hook_live_result_t SG_CompoundHookLivePullApplied(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose)
{
	(void)pose;
	CHECK(frame_serial == 41);
	pull_calls++;
	return PureEvent(host, state, SG_COMPOUND_HOOK_LIVE_EVENT_PULL, bolt);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveReleaseApplied(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial,
	const sg_replay_pose_t *pose)
{
	(void)pose;
	CHECK(frame_serial == 43);
	CHECK(evicted_calls == 1);
	release_calls++;
	return PureEvent(host, state, SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE, bolt);
}

static void Init(edict_t *client, gclient_t *game_client, edict_t *bolt,
	edict_t *target, sg_mover_subject_t *subject)
{
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(client, 0, sizeof(*client));
	memset(game_client, 0, sizeof(*game_client));
	memset(bolt, 0, sizeof(*bolt));
	memset(target, 0, sizeof(*target));
	memset(subject, 0, sizeof(*subject));
	level.framenum = 40;
	at_top = true;
	linked_calls = 0;
	attached_calls = 0;
	pull_calls = 0;
	release_calls = 0;
	evicted_calls = 0;
	observe_calls = 0;
	non_unit_forward = false;
	evicted_result = SG_COMPOUND_GUARD_OK;
	bolt_observation = SG_COMPOUND_GUARD_YES;
	observed_bolt = bolt;
	client->inuse = true;
	client->client = game_client;
	client->s.number = 3;
	bolt->inuse = true;
	bolt->owner = client;
	bolt->s.number = 17;
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	VectorSet(bolt->velocity, RUNE_HOOK_BOLT_SPEED, 0.0f, 0.0f);
	target->inuse = true;
	target->classname = "worldspawn";
	target->s.number = 1;
	g_edicts = target;
	subject->kind = SG_MOVER_SUBJECT_HOOK_BOLT;
	subject->edict_key = 17;
	subject->generation = 29U;
	sg_bots[0].active = true;
	sg_bots[0].ent = client;
	sg_bots[0].compound_hook_live.outer.phase = SG_COMPOUND_TOP;
	sg_bots[0].compound_hook_live.guard_owned = true;
	sg_bots[0].compound_hook_live.local_owned = true;
	VectorSet(sg_bots[0].compound_hook_live.hook_spec.bite,
	    10.0f, 20.0f, 30.0f);
}

static void LinkAndAttach(edict_t *client, edict_t *bolt, edict_t *target,
	const sg_mover_subject_t *subject)
{
	sg_compound_hook_live_result_t result;
	csurface_t surface;
	edict_t foreign;

	memset(&surface, 0, sizeof(surface));
	memset(&foreign, 0, sizeof(foreign));
	foreign.classname = "func_door";
	result = SG_CompoundHookGameLinked(client, bolt, subject);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(linked_calls == 1);
	CHECK(observe_calls == 0);
	CHECK(client->client->hook == NULL);
	CHECK(SG_CompoundHookGameAttachWillApply(bolt, target, &surface) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_DENIED);
	client->client->hook = bolt;
	client->client->hookstate = 1;
	VectorSet(bolt->s.origin, 10.0f, 20.0f, 30.0f);
	CHECK(SG_CompoundHookGameAttachWillApply(bolt, &foreign, &surface) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_DENIED);
	surface.flags = SURF_SKY;
	CHECK(SG_CompoundHookGameAttachWillApply(bolt, target, &surface) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_DENIED);
	surface.flags = 0;
	target->deadflag = DEAD_DEAD;
	CHECK(SG_CompoundHookGameAttachWillApply(bolt, target, &surface) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_DENIED);
	target->deadflag = DEAD_NO;
	CHECK(SG_CompoundHookGameAttachWillApply(bolt, target, &surface) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED);
	bolt->hook_target = target;
	VectorSet(bolt->hook_offset, 10.0f, 20.0f, 30.0f);
	bolt->solid = SOLID_TRIGGER;
	VectorClear(bolt->velocity);
	client->client->hookstate = 2;
	level.framenum = 41;
	result = SG_CompoundHookGameAttached(bolt);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(attached_calls == 1);
	CHECK(SG_CompoundHookGameAttachWillApply(bolt, target, &surface) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_DENIED);
}

static void TestLaunchProof(void)
{
	edict_t client, bolt, target;
	gclient_t game_client;
	sg_mover_subject_t subject;

	Init(&client, &game_client, &bolt, &target, &subject);
	bolt.s.origin[0] = 1.0f;
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(linked_calls == 0);
	Init(&client, &game_client, &bolt, &target, &subject);
	bolt.velocity[0] -= 1.0f;
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(linked_calls == 0);
	CHECK(!sg_bots[0].compound_hook_events.bolt_valid);
	Init(&client, &game_client, &bolt, &target, &subject);
	non_unit_forward = true;
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(linked_calls == 1);
	Init(&client, &game_client, &bolt, &target, &subject);
	at_top = false;
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(linked_calls == 0);
	CHECK(sg_bots[0].compound_hook_events.bolt_valid);
}

static void TestAuthorization(void)
{
	edict_t client, bolt, target, foreign;
	gclient_t game_client;
	sg_mover_subject_t subject;
	sg_compound_hook_live_bolt_t identity = { 17, 29U };
	sg_compound_hook_live_bolt_t wrong = { 17, 30U };

	Init(&client, &game_client, &bolt, &target, &subject);
	memset(&foreign, 0, sizeof(foreign));
	LinkAndAttach(&client, &bolt, &target, &subject);
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED, &wrong) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	at_top = false;
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	at_top = true;
	bolt.hook_offset[0] += 1.0f;
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	bolt.hook_offset[0] -= 1.0f;
	observed_bolt = &foreign;
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	observed_bolt = &bolt;
	VectorSet(client.velocity, 4.0f, 5.0f, 6.0f);
	VectorCopy(client.velocity, game_client.oldvelocity);
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_PULL, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	game_client.v_angle[YAW] = 1.0f;
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_PULL, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	game_client.v_angle[YAW] = 0.0f;
	game_client.oldvelocity[1] += 1.0f;
	CHECK(SG_CompoundHookGameAuthorizeEvent(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    SG_COMPOUND_HOOK_LIVE_EVENT_PULL, &identity) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
}

static void TestLifecycle(void)
{
	edict_t client, bolt, target;
	gclient_t game_client;
	sg_mover_subject_t subject;
	sg_compound_hook_live_result_t result;

	Init(&client, &game_client, &bolt, &target, &subject);
	LinkAndAttach(&client, &bolt, &target, &subject);
	VectorSet(client.velocity, 4.0f, 5.0f, 6.0f);
	VectorCopy(client.velocity, game_client.oldvelocity);
	result = SG_CompoundHookGamePullApplied(&client, &bolt);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(pull_calls == 1);
	game_client.oldvelocity[2] += 1.0f;
	result = SG_CompoundHookGamePullApplied(&client, &bolt);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(pull_calls == 1);
	game_client.oldvelocity[2] -= 1.0f;

	sg_bots[0].compound_hook_live.hook.release_requested = true;
	CHECK(SG_CompoundHookGameReleaseRequested(&client, &bolt) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED);
	CHECK(SG_CompoundHookGameAbortBegin(&client, &bolt) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED);
	bolt_observation = SG_COMPOUND_GUARD_NO;
	bolt.inuse = false;
	game_client.hook = NULL;
	game_client.hookstate = 0;
	level.framenum = 43;
	result = SG_CompoundHookGameAbortEnd(&client);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(evicted_calls == 1);
	CHECK(release_calls == 1);
	result = SG_CompoundHookGameAbortEnd(&client);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(evicted_calls == 1);
	CHECK(release_calls == 1);
}

static void TestAbortFailures(void)
{
	edict_t client, bolt, target;
	gclient_t game_client;
	sg_mover_subject_t subject;
	sg_compound_hook_live_result_t result;

	Init(&client, &game_client, &bolt, &target, &subject);
	LinkAndAttach(&client, &bolt, &target, &subject);
	CHECK(SG_CompoundHookGameAbortBegin(&client, &bolt) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED);
	bolt_observation = SG_COMPOUND_GUARD_NO;
	bolt.inuse = false;
	game_client.hook = NULL;
	game_client.hookstate = 0;
	evicted_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	result = SG_CompoundHookGameAbortEnd(&client);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(evicted_calls == 1);
	CHECK(release_calls == 0);
	CHECK(sg_bots[0].compound_hook_events.abort_bolt == NULL);
	result = SG_CompoundHookGameAbortEnd(&client);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(evicted_calls == 1);
	CHECK(release_calls == 0);

	Init(&client, &game_client, &bolt, &target, &subject);
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RUNNING);
	game_client.hook = &bolt;
	CHECK(SG_CompoundHookGameRecoveryAbortBegin(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot,
	    &sg_bots[0].compound_hook_live.bolt) == false);
	{
		sg_compound_hook_live_bolt_t identity = { 17, 29U };
		CHECK(SG_CompoundHookGameRecoveryAbortBegin(&sg_bots[0],
		    &sg_bots[0].compound_hook_live.snapshot, &identity));
		bolt_observation = SG_COMPOUND_GUARD_NO;
		bolt.inuse = false;
		game_client.hook = NULL;
		game_client.hookstate = 0;
		CHECK(SG_CompoundHookGameRecoveryAbortEnd(&sg_bots[0],
		    &sg_bots[0].compound_hook_live.snapshot, &identity));
		CHECK(evicted_calls == 1);
		CHECK(release_calls == 0);
	}
}

static void TestUnexpectedAbortRecoveryReceipt(void)
{
	edict_t client, bolt, target;
	gclient_t game_client;
	sg_mover_subject_t subject;
	sg_compound_hook_live_bolt_t identity = { 17, 29U };
	sg_compound_hook_live_result_t result;

	Init(&client, &game_client, &bolt, &target, &subject);
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RUNNING);
	game_client.hook = &bolt;
	CHECK(SG_CompoundHookGameAbortBegin(&client, &bolt) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED);
	bolt_observation = SG_COMPOUND_GUARD_NO;
	bolt.inuse = false;
	game_client.hook = NULL;
	game_client.hookstate = 0;
	level.framenum = 43;
	result = SG_CompoundHookGameAbortEnd(&client);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(evicted_calls == 1);
	CHECK(release_calls == 1);
	CHECK(SG_CompoundHookGameRecoveryAbortBegin(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot, &identity));
	CHECK(SG_CompoundHookGameRecoveryAbortEnd(&sg_bots[0],
	    &sg_bots[0].compound_hook_live.snapshot, &identity));
	CHECK(evicted_calls == 1);
}

static void TestOrdinaryHookBypass(void)
{
	edict_t client, bolt, target;
	gclient_t game_client;
	sg_mover_subject_t subject;

	Init(&client, &game_client, &bolt, &target, &subject);
	sg_bots[0].compound_hook_live.guard_owned = false;
	CHECK(SG_CompoundHookGameLinked(&client, &bolt, &subject).outcome ==
	    SG_COMPOUND_HOOK_LIVE_IDLE);
	CHECK(SG_CompoundHookGameAttachWillApply(&bolt, &target, NULL) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_BYPASS);
	CHECK(SG_CompoundHookGameReleaseRequested(&client, &bolt) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_BYPASS);
	CHECK(SG_CompoundHookGameAbortBegin(&client, &bolt) ==
	    SG_COMPOUND_HOOK_GAME_EVENT_BYPASS);
	CHECK(SG_CompoundHookGameAttached(&bolt).outcome ==
	    SG_COMPOUND_HOOK_LIVE_IDLE);
	CHECK(SG_CompoundHookGamePullApplied(&client, &bolt).outcome ==
	    SG_COMPOUND_HOOK_LIVE_IDLE);
	CHECK(SG_CompoundHookGameAbortEnd(&client).outcome ==
	    SG_COMPOUND_HOOK_LIVE_IDLE);
}

static void TestSafetyLatch(void)
{
	edict_t client, bolt, target;
	gclient_t game_client;
	sg_mover_subject_t subject;
	qboolean passed, contaminated;

	Init(&client, &game_client, &bolt, &target, &subject);
	CHECK(SG_CompoundHookGameEventsIdle(&sg_bots[0]));
	SG_CompoundHookGameObserveSafety(&sg_bots[0], true, false);
	SG_CompoundHookGameObserveSafety(&sg_bots[0], false, true);
	CHECK(!SG_CompoundHookGameEventsIdle(&sg_bots[0]));
	CHECK(SG_CompoundHookGamePeekSafety(&sg_bots[0], &passed,
	    &contaminated));
	CHECK(passed && contaminated);
	CHECK(SG_CompoundHookGameTakeSafety(&sg_bots[0], &passed,
	    &contaminated));
	CHECK(passed && contaminated);
	CHECK(SG_CompoundHookGamePeekSafety(&sg_bots[0], &passed,
	    &contaminated));
	CHECK(!passed && !contaminated);
	SG_CompoundHookGameEventsReset(&sg_bots[0]);
	CHECK(SG_CompoundHookGameEventsIdle(&sg_bots[0]));
}

int main(void)
{
	TestLifecycle();
	TestLaunchProof();
	TestAuthorization();
	TestAbortFailures();
	TestUnexpectedAbortRecoveryReceipt();
	TestOrdinaryHookBypass();
	TestSafetyLatch();
	if (failures)
		return 1;
	puts("sg_compound_hook_game_events_test: PASS");
	return 0;
}
