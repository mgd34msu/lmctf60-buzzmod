#include "g_local.h"
#include "g_ctffunc.h"
#include "g_tourney.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_host_law_owner.h"
#include "slipgate/sg_pov_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

game_locals_t game;
game_import_t gi;
game_export_t globals;
edict_t *g_edicts;
sg_bot_t sg_bots[SG_MAXBOTS];
int matchstate;

void ClientThink(edict_t *ent, usercmd_t *ucmd);

static edict_t edicts[5];
static gclient_t clients[5];
static cvar_t test_maxclients;
static cvar_t test_sv_gravity;
static cvar_t test_want_funky_gravity;
cvar_t *maxclients = &test_maxclients;
cvar_t *sv_gravity = &test_sv_gravity;
cvar_t *want_funky_gravity = &test_want_funky_gravity;
level_locals_t level;
vec3_t vec3_origin = {0, 0, 0};
static int write_byte_count;
static int last_byte;
static int write_string_count;
static int unicast_count;
static char last_string[32];
static edict_t *last_unicast;
static qboolean last_reliable;

qboolean SG_OwnsBot(edict_t *ent)
{
	return ent && (ent->flags & FL_BOT) != 0;
}

sg_host_law_result_t SG_HostLawProductionPmove(uint32_t subject_index,
	const sg_host_pmove_request_t *request, sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	sg_host_law_result_t unavailable;

	(void)subject_index;
	(void)request;
	if (result_out)
		memset(result_out, 0, sizeof(*result_out));
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
	memset(&unavailable, 0, sizeof(unavailable));
	unavailable.status = SG_HOST_LAW_HOST_UNAVAILABLE;
	return unavailable;
}

void SG_BotLocalizationObservePmove(edict_t *entity,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_result_t *result)
{
	(void)entity;
	(void)request;
	(void)result;
}

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "pov_session_production_test:%d: %s\n", \
			__LINE__, #condition); \
		return 1; \
	} \
} while (0)

static void TestLinkEntity(edict_t *ent)
{
	(void)ent;
}

qboolean GamePaused(void)
{
	return false;
}

void SG_HumanSpeedClientThinkBegin(edict_t *ent)
{
	(void)ent;
}

void SG_HumanSpeedPmoveBegin(edict_t *ent, pmove_state_t *pmove,
	unsigned command_msec)
{
	(void)ent;
	(void)pmove;
	(void)command_msec;
}

void SG_HumanSpeedPmoveEnd(edict_t *ent, const pmove_state_t *pmove,
	unsigned command_msec)
{
	(void)ent;
	(void)pmove;
	(void)command_msec;
}

void SG_HumanTracePmove(edict_t *ent, const pmove_state_t *before,
	const pmove_t *after)
{
	(void)ent;
	(void)before;
	(void)after;
}

void G_TouchTriggers(edict_t *ent)
{
	(void)ent;
}

void Think_Weapon(edict_t *ent)
{
	(void)ent;
}

void PlayerNoise(edict_t *who, vec3_t where, int type)
{
	(void)who;
	(void)where;
	(void)type;
}

void SG_NoteDropSolidContact(edict_t *source, edict_t *activator)
{
	(void)source;
	(void)activator;
}

void Cmd_Observe_f(edict_t *ent, int observer_type)
{
	(void)ent;
	(void)observer_type;
}

void ctf_SafePrint(edict_t *ent, long priority, char *buf)
{
	(void)ent;
	(void)priority;
	(void)buf;
}

edict_t *ctf_getteamflag(int team, int flag)
{
	(void)team;
	(void)flag;
	return NULL;
}

edict_t *ctf_findplayer(edict_t *after, edict_t *ignore, int team)
{
	(void)after;
	(void)ignore;
	(void)team;
	return NULL;
}

gitem_t itemlist[1];

void AngleVectors(vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	(void)angles;
	if (forward)
	{
		forward[0] = 1;
		forward[1] = 0;
		forward[2] = 0;
	}
	if (right)
		VectorClear(right);
	if (up)
		VectorClear(up);
}

void VectorMA(vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
{
	vecc[0] = veca[0] + scale * vecb[0];
	vecc[1] = veca[1] + scale * vecb[1];
	vecc[2] = veca[2] + scale * vecb[2];
}

vec_t VectorNormalize2(vec3_t v, vec3_t out)
{
	VectorCopy(v, out);
	return 1;
}

void VectorScale(vec3_t in, float scale, vec3_t out)
{
	out[0] = in[0] * scale;
	out[1] = in[1] * scale;
	out[2] = in[2] * scale;
}

void vectoangles(vec3_t value1, vec3_t angles)
{
	(void)value1;
	VectorClear(angles);
}

static void TestWriteByte(int value)
{
	write_byte_count++;
	last_byte = value;
}

static void TestWriteString(char *value)
{
	write_string_count++;
	strncpy(last_string, value, sizeof(last_string) - 1);
	last_string[sizeof(last_string) - 1] = '\0';
}

static void TestUnicast(edict_t *ent, qboolean reliable)
{
	unicast_count++;
	last_unicast = ent;
	last_reliable = reliable;
}

static void ResetNetwork(void)
{
	write_byte_count = 0;
	last_byte = 0;
	write_string_count = 0;
	unicast_count = 0;
	last_string[0] = '\0';
	last_unicast = NULL;
	last_reliable = false;
}

static void SetupClient(int index, const char *name, qboolean bot,
	qboolean spectator, uint64_t ctfid)
{
	edict_t *ent = &edicts[index];

	ent->inuse = true;
	ent->client = &clients[index];
	ent->flags = bot ? FL_BOT : 0;
	ent->client->pers.connected = true;
	ent->client->resp.spectator = spectator;
	ent->client->ctf.teamnum =
		spectator ? CTF_TEAM_OBSERVER : CTF_TEAM_RED;
	ent->client->ctf.ctfid = ctfid;
	strncpy(ent->client->pers.netname, name,
		sizeof(ent->client->pers.netname) - 1);
}

static void SetupLiveBot(int index, int slot, const char *name,
	uint64_t ctfid)
{
	SetupClient(index, name, true, false, ctfid);
	sg_bots[slot].ent = &edicts[index];
	if (!SG_BotPOVInstanceAssign(&sg_bots[slot]))
		abort();
	sg_bots[slot].active = true;
}

static void SeedBotPOV(edict_t *target, int gunindex, int gunframe,
	int health)
{
	target->client->ps.pmove.pm_type = PM_NORMAL;
	target->client->ps.pmove.pm_flags = PMF_JUMP_HELD | PMF_NO_PREDICTION;
	target->client->ps.pmove.origin[0] = 700;
	target->client->ps.pmove.velocity[1] = -300;
	target->client->ps.viewangles[PITCH] = -11;
	target->client->ps.viewangles[YAW] = 87;
	target->client->ps.viewoffset[2] = 22;
	target->client->ps.gunoffset[0] = 5;
	target->client->ps.gunangles[YAW] = 3;
	target->client->ps.gunindex = gunindex;
	target->client->ps.gunframe = gunframe;
	target->client->ps.fov = 107;
	target->client->ps.rdflags = RDF_UNDERWATER;
	target->client->ps.stats[STAT_HEALTH] = health;
	target->client->ps.stats[STAT_AMMO] = 31;
	VectorSet(target->s.origin, 100, 200, 300);
	VectorSet(target->s.old_origin, 99, 199, 299);
	VectorSet(target->velocity, 13, -17, 5);
	VectorSet(target->s.angles, 0, 87, 0);
	VectorSet(target->client->v_angle, -11, 87, 0);
	target->viewheight = 22;
}

static void ResetOrdinaryFixture(void)
{
	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&level, 0, sizeof(level));
	ResetNetwork();
	SetupClient(1, "Observer", false, true, 11);
	SetupLiveBot(2, 0, "Alpha", 102);
	SetupClient(3, "Human", false, false, 103);
	SetupLiveBot(4, 1, "Beta", 104);
	SeedBotPOV(&edicts[2], 42, 7, 87);
	SeedBotPOV(&edicts[4], 84, 9, 93);
	/* The first bot proves a target-held bit cannot suppress a fresh viewer
	 * edge; this second one proves an already-held viewer edge survives a
	 * target which does not hold it. */
	edicts[4].client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
}

static int CheckOrdinarySpectatorBaseline(const edict_t *viewer,
	qboolean jump_held)
{
	CHECK(!viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	CHECK(viewer->client->ps.pmove.pm_type == PM_SPECTATOR);
	CHECK((viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD) ==
		(jump_held ? PMF_JUMP_HELD : 0));
	CHECK(!(viewer->client->ps.pmove.pm_flags & ~PMF_JUMP_HELD));
	CHECK(viewer->client->ps.gunindex == 0 &&
		viewer->client->ps.gunframe == 0);
	CHECK(viewer->client->ps.fov == 90);
	CHECK(viewer->client->ps.stats[STAT_HEALTH] == 0 &&
		viewer->client->ps.stats[STAT_AMMO] == 0 &&
		viewer->client->ps.stats[STAT_SPECTATOR] == 1);
	CHECK(viewer->client->ps.viewangles[PITCH] == 0 &&
		viewer->client->ps.viewangles[YAW] == 0 &&
		viewer->client->ps.viewoffset[2] == 0 &&
		viewer->client->ps.gunoffset[0] == 0 &&
		viewer->client->ps.gunangles[YAW] == 0 &&
		viewer->client->ps.rdflags == 0);
	CHECK(viewer->client->v_angle[PITCH] == 0 &&
		viewer->client->v_angle[YAW] == 0 &&
		viewer->s.angles[YAW] == 0);
	CHECK(viewer->velocity[0] == 0 && viewer->velocity[1] == 0 &&
		viewer->velocity[2] == 0);
	CHECK(viewer->s.old_origin[0] == viewer->s.origin[0] &&
		viewer->s.old_origin[1] == viewer->s.origin[1] &&
		viewer->s.old_origin[2] == viewer->s.origin[2]);
	CHECK(viewer->viewheight == 0);
	CHECK(viewer->movetype == MOVETYPE_NOCLIP &&
		viewer->solid == SOLID_NOT && viewer->s.modelindex == 0);
	return 0;
}

static int TestProductionIdentityAndRespawnLifecycle(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *target = &edicts[2];
	unsigned long long first_token;
	unsigned long long queried_token = 0ULL;
	int queried_slot = -1;

	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(sg_bots, 0, sizeof(sg_bots));
	ResetNetwork();
	SetupClient(1, "Recorder", false, true, 11);
	SetupClient(2, "Alpha", true, false, 102);

	/* This is the production publish order used by sg_client: ent, immutable
	 * instance, then active ownership. Assignment is exactly once. */
	sg_bots[3].ent = target;
	CHECK(SG_BotPOVInstanceAssign(&sg_bots[3]));
	first_token = sg_bots[3].instance_token;
	CHECK(first_token != 0ULL);
	CHECK(!SG_BotPOVInstanceAssign(&sg_bots[3]));
	CHECK(sg_bots[3].instance_token == first_token);
	sg_bots[3].active = true;

	CHECK(SG_BotPOVIdentity(target, &queried_slot, &queried_token));
	CHECK(queried_slot == 3 && queried_token == first_token);
	CHECK(SG_BotPOVResolve(3, first_token) == target);
	CHECK(SG_BotPOVResolve(3, first_token + 1ULL) == NULL);
	target->flags &= ~FL_BOT;
	CHECK(!SG_BotPOVIdentity(target, NULL, NULL));
	CHECK(SG_BotPOVResolve(3, first_token) == NULL);
	target->flags |= FL_BOT;
	target->client->pers.connected = false;
	CHECK(!SG_BotPOVIdentity(target, NULL, NULL));
	target->client->pers.connected = true;

	CHECK(POVLock_Command(viewer, "alpha"));
	CHECK(viewer->client->pov_record_pending);
	CHECK(write_string_count == 0);
	POVLock_UpdateFollowers(target);
	CHECK(write_byte_count == 1 && last_byte == svc_stufftext &&
		write_string_count == 1 &&
		unicast_count == 1);
	CHECK(!strcmp(last_string, "record pov\n"));
	CHECK(last_unicast == viewer && last_reliable);

	/* Normal match death is non-terminal: WAIT, no stop, exact-instance
	 * reattach to the newly assigned ctfid, and no second record. */
	POVLock_TargetRespawning(target);
	ResetNetwork();
	matchstate = MATCH_INPLAY;
	CHECK(!POVLock_HandleRespawnTerminal(target));
	CHECK(viewer->client->pov_record_active);
	CHECK(viewer->client->pov_record_wait_respawn);
	CHECK(write_string_count == 0);
	target->client->ctf.ctfid = 202;
	POVLock_TargetSpawned(target);
	CHECK(viewer->client->povlock_active);
	CHECK(viewer->client->povlock_target_ctfid == 202);
	POVLock_UpdateFollowers(target);
	CHECK(write_string_count == 0);

	/* Railgun elimination has no PutClientInServer edge. The real production
	 * p_client predicate terminal-closes at the first death edge, before a
	 * follower refresh can enter WAIT and before respawn's early return. */
	CHECK(viewer->client->povlock_active);
	ResetNetwork();
	matchstate = MATCH_RAILGUN_INPLAY;
	CHECK(POVLock_HandleRespawnTerminal(target));
	CHECK(!viewer->client->pov_record_active);
	CHECK(!viewer->client->pov_record_wait_respawn);
	CHECK(write_byte_count == 1 && last_byte == svc_stufftext &&
		write_string_count == 1 &&
		unicast_count == 1);
	CHECK(!strcmp(last_string, "stop\n"));
	CHECK(last_unicast == viewer && last_reliable);
	CHECK(POVLock_HandleRespawnTerminal(target));
	target->client->ctf.ctfid = 302;
	POVLock_TargetSpawned(target);
	CHECK(write_string_count == 1 && unicast_count == 1);

	/* BotSlot_Reset is the sole production caller of this real reset seam. */
	sg_bots[3].active = false;
	SG_BotPOVInstanceReset(&sg_bots[3]);
	CHECK(sg_bots[3].instance_token == 0ULL);
	CHECK(SG_BotPOVResolve(3, first_token) == NULL);
	CHECK(SG_BotPOVInstanceAssign(&sg_bots[3]));
	CHECK(sg_bots[3].instance_token != 0ULL);
	CHECK(sg_bots[3].instance_token != first_token);
	return 0;
}

static int TestClientThinkOrdinaryAndRecordedControls(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *alpha = &edicts[2];
	edict_t *human = &edicts[3];
	edict_t *beta = &edicts[4];
	usercmd_t command;

	ResetOrdinaryFixture();

	/* Drive the production GetChaseTarget -> ClientThink path.  The first SG
	 * target is instantaneous/in-eyes, but it is not a recording session.  A
	 * bot's held-jump state must not suppress this viewer's first chase edge. */
	GetChaseTarget(viewer);
	CHECK(viewer->client->chase_target == alpha);
	CHECK(viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	CHECK(viewer->client->ps.gunindex == 42 &&
		viewer->client->ps.gunframe == 7);
	CHECK(viewer->client->ps.fov == 107 &&
		viewer->client->ps.stats[STAT_HEALTH] == 87 &&
		viewer->client->ps.viewangles[YAW] == 87);
	CHECK(!(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD));
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	memset(&command, 0, sizeof(command));
	command.msec = 10;
	command.upmove = 20;
	ClientThink(viewer, &command);
	CHECK(viewer->client->chase_target == human);
	if (CheckOrdinarySpectatorBaseline(viewer, true))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Holding the same jump cannot immediately cycle again after bot -> human;
	 * that latch belongs to the observer, not either bot's playerstate. */
	ClientThink(viewer, &command);
	CHECK(viewer->client->chase_target == human);

	/* Release then jump again: historical next-target cycling crosses the
	 * human -> SG boundary and restores exact in-eyes state. */
	command.upmove = 0;
	ClientThink(viewer, &command);
	CHECK(!(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD));
	command.upmove = 20;
	ClientThink(viewer, &command);
	CHECK(viewer->client->chase_target == beta);
	CHECK(viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	CHECK(POVLock_Update(viewer));
	CHECK(viewer->client->ps.gunindex == 84 &&
		viewer->client->ps.gunframe == 9);
	CHECK(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD);
	ClientThink(viewer, &command);
	CHECK(viewer->client->chase_target == beta);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Attack retains the historical chase toggle-off meaning for ordinary
	 * in-eyes chase and clears the copied state without a recording command. */
	command.upmove = 0;
	ClientThink(viewer, &command);
	CHECK(viewer->client->ps.gunindex == 84 &&
		viewer->client->ps.gunframe == 9);
	command.buttons = BUTTON_ATTACK;
	ClientThink(viewer, &command);
	CHECK(!viewer->client->chase_target);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Dedicated povrecord remains the sole frozen/input-suppressed mode. */
	ResetOrdinaryFixture();
	viewer->client->ctf.ctfid = 21;
	alpha->client->ctf.ctfid = 202;
	CHECK(POVLock_Command(viewer, "alpha"));
	CHECK(viewer->client->pov_record_active && viewer->client->povlock_active);
	CHECK(!viewer->client->chase_target);
	CHECK(viewer->client->ps.gunindex == 42 &&
		viewer->client->ps.gunframe == 7 &&
		viewer->client->ps.stats[STAT_HEALTH] == 87);
	CHECK(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD);
	memset(&command, 0, sizeof(command));
	command.msec = 10;
	command.buttons = BUTTON_ATTACK;
	command.upmove = 20;
	ClientThink(viewer, &command);
	CHECK(viewer->client->pov_record_active && viewer->client->povlock_active);
	CHECK(!viewer->client->chase_target);
	CHECK(!(viewer->client->latched_buttons & BUTTON_ATTACK));
	CHECK(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD);
	CHECK(viewer->client->ps.gunindex == 42 &&
		viewer->client->ps.gunframe == 7 &&
		viewer->client->ps.stats[STAT_HEALTH] == 87);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);
	return 0;
}

static int TestOrdinaryInEyesDepartureScrub(void)
{
	edict_t *viewer;
	edict_t *alpha;
	usercmd_t command;
	unsigned long long retired_instance;

	/* `chasecam off` clears the target first, then takes this same explicit
	 * ordinary clear path.  It must not leave an SG HUD/view/body behind. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	alpha = &edicts[2];
	GetChaseTarget(viewer);
	CHECK(viewer->client->povlock_active &&
		viewer->client->ps.gunindex == 42);
	viewer->client->chase_target = NULL;
	POVLock_Clear(viewer);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* A stale ctfid / invalid SG identity fails closed through the live
	 * ClientThink -> POVLock_Update path. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	alpha = &edicts[2];
	GetChaseTarget(viewer);
	alpha->client->ctf.ctfid++;
	memset(&command, 0, sizeof(command));
	command.msec = 10;
	ClientThink(viewer, &command);
	CHECK(!viewer->client->chase_target);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* A dead target takes that same ordinary update exit before it can become
	 * a stale third-person chase. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	alpha = &edicts[2];
	GetChaseTarget(viewer);
	alpha->deadflag = DEAD_DEAD;
	memset(&command, 0, sizeof(command));
	command.msec = 10;
	ClientThink(viewer, &command);
	CHECK(!viewer->client->chase_target);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* PutClientInServer announces this edge through TargetRespawning. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	alpha = &edicts[2];
	GetChaseTarget(viewer);
	POVLock_TargetRespawning(alpha);
	CHECK(!viewer->client->chase_target);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* ClientDisconnect clears ordinary followers before the target can be
	 * reused as another client. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	alpha = &edicts[2];
	GetChaseTarget(viewer);
	POVLock_ClearTarget(alpha);
	CHECK(!viewer->client->chase_target);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* BotSlot_Reset retires immutable SG ownership.  This path used to close
	 * recording sessions only, leaving ordinary in-eyes state copied forever. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	alpha = &edicts[2];
	GetChaseTarget(viewer);
	retired_instance = sg_bots[0].instance_token;
	CHECK(retired_instance != 0ULL);
	POVLock_SGInstanceRetired(0, retired_instance);
	CHECK(!viewer->client->chase_target);
	if (CheckOrdinarySpectatorBaseline(viewer, false))
		return 1;
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);
	/* Prove that an actual new slot generation cannot inherit the old view. */
	sg_bots[0].active = false;
	SG_BotPOVInstanceReset(&sg_bots[0]);
	CHECK(SG_BotPOVInstanceAssign(&sg_bots[0]));
	sg_bots[0].active = true;
	CHECK(sg_bots[0].instance_token != retired_instance);
	CHECK(!viewer->client->povlock_active && !viewer->client->chase_target);

	/* The scrub guard is spectator-only.  An erroneous lock bit on a playing
	 * client still clears authority, but never rewrites its live body/HUD. */
	ResetOrdinaryFixture();
	viewer = &edicts[1];
	viewer->client->resp.spectator = false;
	viewer->client->ctf.teamnum = CTF_TEAM_RED;
	viewer->client->povlock_active = true;
	viewer->client->ps.pmove.pm_type = PM_NORMAL;
	viewer->client->ps.gunindex = 19;
	viewer->client->ps.gunframe = 4;
	viewer->client->ps.stats[STAT_HEALTH] = 66;
	viewer->movetype = MOVETYPE_WALK;
	viewer->solid = SOLID_BBOX;
	viewer->s.modelindex = 255;
	POVLock_Clear(viewer);
	CHECK(!viewer->client->povlock_active);
	CHECK(viewer->client->ps.pmove.pm_type == PM_NORMAL &&
		viewer->client->ps.gunindex == 19 &&
		viewer->client->ps.gunframe == 4 &&
		viewer->client->ps.stats[STAT_HEALTH] == 66);
	CHECK(viewer->movetype == MOVETYPE_WALK &&
		viewer->solid == SOLID_BBOX && viewer->s.modelindex == 255);
	return 0;
}

static int TestProductionTeamFilteredOrdinaryChase(void)
{
	edict_t *viewer;

	ResetOrdinaryFixture();
	viewer = &edicts[1];
	viewer->client->ctf.teamnum = CTF_TEAM_OBSERVER_RED;
	edicts[2].client->ctf.teamnum = CTF_TEAM_BLUE;
	edicts[3].client->ctf.teamnum = CTF_TEAM_BLUE;
	edicts[4].client->ctf.teamnum = CTF_TEAM_RED;
	GetChaseTarget(viewer);
	CHECK(viewer->client->chase_target == &edicts[4]);
	CHECK(viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	CHECK(viewer->client->ps.gunindex == 84 &&
		viewer->client->ps.gunframe == 9);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);
	return 0;
}

int main(void)
{
	g_edicts = edicts;
	game.maxclients = 4;
	game.clients = clients;
	test_maxclients.value = 4;
	test_sv_gravity.value = 800;
	gi.linkentity = TestLinkEntity;
	gi.WriteByte = TestWriteByte;
	gi.WriteString = TestWriteString;
	gi.unicast = TestUnicast;
	if (TestProductionIdentityAndRespawnLifecycle())
		return 1;
	if (TestClientThinkOrdinaryAndRecordedControls())
		return 1;
	if (TestOrdinaryInEyesDepartureScrub())
		return 1;
	if (TestProductionTeamFilteredOrdinaryChase())
		return 1;
	puts("pov_session_production_test: ok");
	return 0;
}
