#include "g_local.h"
#include "g_ctffunc.h"

#include <stdio.h>
#include <string.h>

game_locals_t game;
game_import_t gi;
edict_t *g_edicts;

static edict_t edicts[6];
static gclient_t clients[6];
static cvar_t test_maxclients;
cvar_t *maxclients = &test_maxclients;
vec3_t vec3_origin = {0, 0, 0};
static int link_count;
static edict_t *sg_owner[6];
static unsigned long long sg_instance[6];
static int write_byte_count;
static int write_string_count;
static int unicast_count;
static int last_byte;
static char last_string[32];
static edict_t *last_unicast;
static qboolean last_reliable;
static char event_order[32];
static int event_order_count;
static edict_t *ordering_viewer;
static edict_t *ordering_target;
static qboolean copied_before_record;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "povlock_test:%d: %s\n", __LINE__, #condition); \
		return 1; \
	} \
} while (0)

static void TestLinkEntity(edict_t *ent)
{
	(void)ent;
	link_count++;
	if (event_order_count < (int)sizeof(event_order))
		event_order[event_order_count++] = 'L';
}

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

static void TestWriteByte(int value)
{
	write_byte_count++;
	last_byte = value;
	if (ordering_viewer && ordering_target && value == svc_stufftext)
		copied_before_record =
			ordering_viewer->client->ps.gunindex ==
			ordering_target->client->ps.gunindex;
	if (event_order_count < (int)sizeof(event_order))
		event_order[event_order_count++] = 'B';
}

static void TestWriteString(char *value)
{
	write_string_count++;
	strncpy(last_string, value, sizeof(last_string) - 1);
	last_string[sizeof(last_string) - 1] = '\0';
	if (event_order_count < (int)sizeof(event_order))
		event_order[event_order_count++] = 'S';
}

static void TestUnicast(edict_t *ent, qboolean reliable)
{
	unicast_count++;
	last_unicast = ent;
	last_reliable = reliable;
	if (event_order_count < (int)sizeof(event_order))
		event_order[event_order_count++] = 'U';
}

static trace_t TestTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end, edict_t *passedict, int contentmask)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)passedict;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	return trace;
}

static void ResetNetwork(void)
{
	write_byte_count = 0;
	write_string_count = 0;
	unicast_count = 0;
	last_byte = 0;
	last_string[0] = '\0';
	last_unicast = NULL;
	last_reliable = false;
	event_order_count = 0;
	memset(event_order, 0, sizeof(event_order));
	ordering_viewer = NULL;
	ordering_target = NULL;
	copied_before_record = false;
}

static void ResetFixture(void)
{
	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(sg_owner, 0, sizeof(sg_owner));
	memset(sg_instance, 0, sizeof(sg_instance));
	link_count = 0;
	ResetNetwork();
}

static edict_t *AdminClient(int index, const char *name, int team,
	qboolean spectator, int flags, uint64_t ctfid)
{
	edict_t *ent = &edicts[index];

	ent->inuse = true;
	ent->client = &clients[index - 1];
	ent->flags = flags;
	ent->client->pers.connected = true;
	strncpy(ent->client->pers.netname, name,
	        sizeof(ent->client->pers.netname) - 1);
	ent->client->ctf.teamnum = team;
	ent->client->ctf.ctfid = ctfid;
	ent->client->resp.spectator = spectator;
	return ent;
}

static void BindSG(int client_index, int slot, unsigned long long instance)
{
	sg_owner[slot] = &edicts[client_index];
	sg_instance[slot] = instance;
}

qboolean SG_BotPOVIdentity(edict_t *ent, int *slot_out,
	unsigned long long *instance_out)
{
	int slot;

	if (!ent || !ent->inuse || !ent->client || !(ent->flags & FL_BOT) ||
	    !ent->client->pers.connected)
		return false;
	for (slot = 0; slot < 6; slot++)
		if (sg_owner[slot] == ent && sg_instance[slot] != 0ULL)
		{
			if (slot_out)
				*slot_out = slot;
			if (instance_out)
				*instance_out = sg_instance[slot];
			return true;
		}
	return false;
}

edict_t *SG_BotPOVResolve(int slot, unsigned long long instance)
{
	edict_t *ent;

	if (slot < 0 || slot >= 6 || sg_instance[slot] != instance)
		return NULL;
	ent = sg_owner[slot];
	if (!ent || !ent->inuse || !ent->client || !(ent->flags & FL_BOT) ||
	    !ent->client->pers.connected)
		return NULL;
	return ent;
}

static void SetupClient(int index, const char *name, qboolean bot,
	qboolean spectator, int score, uint64_t ctfid)
{
	edict_t *ent = &edicts[index];

	ent->inuse = true;
	ent->client = &clients[index];
	ent->flags = bot ? FL_BOT : 0;
	ent->client->pers.connected = true;
	ent->client->resp.spectator = spectator;
	ent->client->ctf.teamnum = spectator ? CTF_TEAM_OBSERVER : CTF_TEAM_RED;
	ent->client->resp.score = score;
	ent->client->ctf.ctfid = ctfid;
	strncpy(ent->client->pers.netname, name,
		sizeof(ent->client->pers.netname) - 1);
}

static int TestSelectionAndIdentity(void)
{
	edict_t *viewer = &edicts[1];
	uint64_t alpha_life = UINT64_C(0x100000066);

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, alpha_life);
	SetupClient(3, "beta", true, false, 11, 103);
	SetupClient(4, "Human", false, false, 99, 104);
	SetupClient(5, "Legacy", true, false, 50, 105);
	BindSG(2, 0, 1001ULL);
	BindSG(3, 1, 1002ULL);

	CHECK(POVLock_CommandNameIs("PoVlOcK"));
	CHECK(!POVLock_CommandNameIs("chasecam"));
	CHECK(POVLock_Command(viewer, "aLpHa"));
	CHECK(viewer->client->povlock_target_index == 2);
	CHECK(viewer->client->povlock_target_ctfid == alpha_life);
	CHECK(viewer->client->pov_record_active);
	CHECK(viewer->client->pov_record_pending);
	CHECK(viewer->client->pov_record_sg_slot == 0);
	CHECK(viewer->client->pov_record_sg_instance == 1001ULL);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Once armed, a later mutable name cannot retarget the session. */
	CHECK(!POVLock_Command(viewer, "BETA"));
	CHECK(viewer->client->povlock_target_index == 2);
	CHECK(viewer->client->pov_record_sg_instance == 1001ULL);
	CHECK(POVLock_Command(viewer, "ALPHA"));
	CHECK(POVLock_Command(viewer, "off"));
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));
	ResetNetwork();
	CHECK(POVLock_Command(viewer, "BETA"));
	CHECK(viewer->client->povlock_target_index == 3);
	CHECK(POVLock_Command(viewer, "off"));
	ResetNetwork();

	strcpy(edicts[2].client->pers.netname, "same");
	strcpy(edicts[3].client->pers.netname, "SAME");
	CHECK(!POVLock_Command(viewer, "SaMe"));
	CHECK(!viewer->client->povlock_active);
	CHECK(!POVLock_Command(viewer, "missing"));
	CHECK(!POVLock_Command(viewer, "human"));
	CHECK(!POVLock_Command(viewer, "legacy"));
	CHECK(write_string_count == 0);

	strcpy(edicts[2].client->pers.netname, "Alpha");
	strcpy(edicts[3].client->pers.netname, "beta");
	edicts[2].client->resp.score = 11;
	edicts[3].client->resp.score = 11;
	SetupClient(5, "lower", true, false, 10, 105);
	BindSG(5, 2, 1003ULL);
	CHECK(POVLock_Command(viewer, NULL));
	CHECK(viewer->client->povlock_target_index == 3);
	CHECK(POVLock_Command(viewer, "off"));
	edicts[5].client->resp.score = 11;
	CHECK(POVLock_Command(viewer, NULL));
	CHECK(viewer->client->povlock_target_index == 5);
	CHECK(POVLock_Command(viewer, "off"));
	edicts[5].inuse = false;

	CHECK(POVLock_Command(viewer, "beta"));
	edicts[3].inuse = false;
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	edicts[3].inuse = true;
	edicts[3].client->pers.connected = true;
	edicts[3].flags = FL_BOT;
	edicts[3].client->resp.spectator = false;
	edicts[3].client->ctf.ctfid = 203;
	CHECK(POVLock_Command(viewer, "beta"));
	edicts[3].client->ctf.ctfid = 204;
	CHECK(!POVLock_Update(viewer));
	CHECK(viewer->client->pov_record_wait_respawn);
	CHECK(viewer->client->pov_record_active);
	POVLock_SGInstanceRetired(1, 1002ULL);
	CHECK(!viewer->client->pov_record_active);

	CHECK(POVLock_Command(viewer, "alpha"));
	POVLock_ClearTarget(&edicts[2]);
	CHECK(!viewer->client->povlock_active);
	CHECK(POVLock_Command(viewer, "alpha"));
	viewer->client->latched_buttons = BUTTON_ATTACK;
	viewer->client->ps.pmove.pm_flags = 0;
	POVLock_SuppressInput(viewer->client);
	CHECK(!(viewer->client->latched_buttons & BUTTON_ATTACK));
	CHECK(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD);
	POVLock_Clear(viewer);
	CHECK(!viewer->client->povlock_active);
	CHECK(!(viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD));
	CHECK(POVLock_Command(viewer, "alpha"));
	CHECK(POVLock_Command(viewer, "off"));
	CHECK(!viewer->client->povlock_active);

	viewer->client->resp.spectator = false;
	viewer->client->ctf.teamnum = CTF_TEAM_RED;
	CHECK(!POVLock_Command(viewer, "alpha"));
	viewer->client->ctf.teamnum = CTF_TEAM_UNDEFINED;
	CHECK(!POVLock_Command(viewer, "alpha"));
	viewer->client->resp.spectator = true;
	viewer->flags = FL_BOT;
	CHECK(!POVLock_Command(viewer, "alpha"));
	return 0;
}

static int TestOrdinaryChaseCamPOV(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *human = &edicts[2];
	edict_t *alpha = &edicts[3];
	edict_t *beta = &edicts[4];

	ResetFixture();
	SetupClient(1, "Observer", false, true, 0, 1);
	SetupClient(2, "Human", false, false, 0, 102);
	SetupClient(3, "Alpha", true, false, 0, 103);
	SetupClient(4, "Beta", true, false, 0, 104);
	BindSG(3, 0, 1001ULL);
	BindSG(4, 1, 1002ULL);

	/* The ordinary selector keeps human chases third-person and does not
	 * arm any client demo command. */
	GetChaseTarget(viewer);
	CHECK(viewer->client->chase_target == human);
	CHECK(!viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Cycling into a live SG bot changes only the view transport: exact
	 * player state, no `record pov` side effect. */
	alpha->client->ps.gunindex = 42;
	alpha->s.origin[0] = 99;
	ChaseNext(viewer);
	CHECK(viewer->client->chase_target == alpha);
	CHECK(viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	UpdateChaseCam(viewer);
	CHECK(viewer->client->ps.gunindex == 42 && viewer->s.origin[0] == 99);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	ChaseNext(viewer);
	CHECK(viewer->client->chase_target == beta);
	CHECK(viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);

	/* A dead or reused bot fails closed instead of falling through to a stale
	 * third-person camera or a replacement client. */
	beta->deadflag = DEAD_DEAD;
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->povlock_active && !viewer->client->chase_target);
	beta->deadflag = DEAD_NO;
	GetChaseTarget(viewer);
	CHECK(viewer->client->chase_target == human);
	ChaseNext(viewer);
	CHECK(viewer->client->chase_target == alpha);
	sg_instance[0] = 2001ULL;
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->povlock_active && !viewer->client->chase_target);
	GetChaseTarget(viewer);
	ChaseNext(viewer);
	CHECK(viewer->client->chase_target == alpha && viewer->client->povlock_active);
	POVLock_TargetRespawning(alpha);
	CHECK(!viewer->client->povlock_active && !viewer->client->chase_target);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Team observers retain their filter even when bots occupy earlier slots. */
	ResetFixture();
	SetupClient(1, "RedObserver", false, true, 0, 1);
	viewer->client->ctf.teamnum = CTF_TEAM_OBSERVER_RED;
	SetupClient(2, "BlueHuman", false, false, 0, 102);
	human->client->ctf.teamnum = CTF_TEAM_BLUE;
	SetupClient(3, "BlueBot", true, false, 0, 103);
	alpha->client->ctf.teamnum = CTF_TEAM_BLUE;
	SetupClient(4, "RedBot", true, false, 0, 104);
	beta->client->ctf.teamnum = CTF_TEAM_RED;
	BindSG(3, 0, 3001ULL);
	BindSG(4, 1, 3002ULL);
	GetChaseTarget(viewer);
	CHECK(viewer->client->chase_target == beta && viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);

	/* A second bot claiming an occupied SG slot cannot inherit that in-eyes
	 * identity; cycling skips the unowned duplicate. */
	ResetFixture();
	SetupClient(1, "Observer", false, true, 0, 1);
	SetupClient(2, "Human", false, false, 0, 102);
	SetupClient(3, "StaleDuplicate", true, false, 0, 103);
	SetupClient(4, "Owner", true, false, 0, 104);
	BindSG(3, 0, 4001ULL);
	BindSG(4, 0, 4001ULL);
	GetChaseTarget(viewer);
	CHECK(viewer->client->chase_target == human);
	ChaseNext(viewer);
	CHECK(viewer->client->chase_target == beta && viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);
	return 0;
}

static int TestInEyesState(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *target = &edicts[2];
	edict_t target_before;

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	target->s.origin[0] = 100;
	target->s.origin[1] = 200;
	target->s.origin[2] = 300;
	target->s.old_origin[0] = 99;
	target->velocity[0] = 13;
	target->velocity[1] = -17;
	target->s.angles[YAW] = 85;
	target->client->v_angle[PITCH] = -12;
	target->client->v_angle[YAW] = 85;
	target->client->ps.viewangles[PITCH] = -12;
	target->client->ps.viewangles[YAW] = 85;
	target->viewheight = 22;
	target->client->ps.fov = 105;
	target->client->ps.gunindex = 42;
	target->client->ps.gunframe = 7;
	target->client->ps.pmove.origin[0] = 800;
	target->client->ps.pmove.origin[2] = 2400;
	target->client->ps.pmove.velocity[1] = -136;
	target->client->ps.pmove.velocity[2] = 64;
	target->client->ps.viewoffset[0] = 3;
	target->client->ps.viewoffset[1] = -4;
	target->client->ps.viewoffset[2] = 22;
	target->client->ps.gunoffset[0] = 5;
	target->client->ps.gunoffset[2] = -6;
	target->client->ps.gunangles[PITCH] = -1;
	target->client->ps.gunangles[YAW] = 2;
	target->client->ps.rdflags = RDF_UNDERWATER | RDF_IRGOGGLES;
	target->client->ps.blend[0] = 0.2f;
	target->client->ps.blend[1] = 0.4f;
	target->client->ps.blend[2] = 0.6f;
	target->client->ps.blend[3] = 0.8f;
	target->client->ps.stats[STAT_HEALTH] = 87;
	target->client->ps.stats[STAT_AMMO] = 31;
	viewer->client->resp.cmd_angles[YAW] = 15;
	target_before = *target;

	CHECK(POVLock_Command(viewer, "alpha"));
	/* Simulate every spectator-local write ClientEndServerFrame makes before
	 * its final POVLock_EndFrame call. The helper must restore target state. */
	memset(&viewer->client->ps, 0, sizeof(viewer->client->ps));
	viewer->client->ps.pmove.origin[0] = -1;
	viewer->client->ps.pmove.velocity[1] = 1;
	viewer->client->ps.viewangles[YAW] = -1;
	viewer->client->ps.viewoffset[2] = -1;
	viewer->client->ps.gunindex = 1;
	viewer->client->ps.gunframe = 1;
	viewer->client->ps.rdflags = RDF_NOWORLDMODEL;
	viewer->client->ps.blend[3] = 1;
	viewer->client->ps.fov = 1;
	viewer->client->ps.stats[STAT_HEALTH] = 1;
	viewer->solid = SOLID_BBOX;
	viewer->movetype = MOVETYPE_WALK;
	viewer->s.modelindex = 255;
	/* The target's actual end-frame completion must win even when this
	 * recorder's lower slot was finalized first. */
	POVLock_EndFrame(target);
	CHECK(viewer->s.origin[0] == 100 && viewer->s.origin[1] == 200 &&
		viewer->s.origin[2] == 300);
	CHECK(viewer->s.old_origin[0] == 99);
	CHECK(viewer->velocity[0] == 13 && viewer->velocity[1] == -17);
	CHECK(viewer->client->v_angle[PITCH] == -12 &&
		viewer->client->ps.viewangles[YAW] == 85);
	CHECK(viewer->viewheight == 22);
	CHECK(viewer->client->ps.fov == 105 && viewer->client->ps.gunindex == 42);
	CHECK(viewer->client->ps.pmove.origin[0] == 800 &&
		viewer->client->ps.pmove.origin[2] == 2400 &&
		viewer->client->ps.pmove.velocity[1] == -136 &&
		viewer->client->ps.pmove.velocity[2] == 64);
	CHECK(memcmp(viewer->client->ps.viewoffset, target->client->ps.viewoffset,
		sizeof(viewer->client->ps.viewoffset)) == 0);
	CHECK(viewer->client->ps.gunindex == 42 && viewer->client->ps.gunframe == 7);
	CHECK(memcmp(viewer->client->ps.gunoffset, target->client->ps.gunoffset,
		sizeof(viewer->client->ps.gunoffset)) == 0);
	CHECK(memcmp(viewer->client->ps.gunangles, target->client->ps.gunangles,
		sizeof(viewer->client->ps.gunangles)) == 0);
	CHECK(viewer->client->ps.rdflags ==
		target->client->ps.rdflags);
	CHECK(memcmp(viewer->client->ps.blend, target->client->ps.blend,
		sizeof(viewer->client->ps.blend)) == 0);
	CHECK(viewer->client->ps.stats[STAT_HEALTH] == 87 &&
		viewer->client->ps.stats[STAT_AMMO] == 31);
	CHECK(viewer->client->ps.pmove.pm_type == PM_FREEZE);
	CHECK(viewer->client->ps.pmove.pm_flags & PMF_NO_PREDICTION);
	CHECK(viewer->movetype == MOVETYPE_NOCLIP && viewer->solid == SOLID_NOT &&
		viewer->s.modelindex == 0);
	CHECK(memcmp(target, &target_before, sizeof(*target)) == 0);
	CHECK(link_count > 0);
	return 0;
}

static int TestEndFrameFailsClosed(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *target = &edicts[2];
	player_state_t spectator_before;

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	viewer->client->ps.fov = 73;
	viewer->client->ps.stats[STAT_SPECTATOR] = 1;
	spectator_before = viewer->client->ps;
	POVLock_EndFrame(viewer);
	CHECK(memcmp(&viewer->client->ps, &spectator_before,
		sizeof(spectator_before)) == 0);

	CHECK(POVLock_Command(viewer, "alpha"));
	target->deadflag = DEAD_DEAD;
	POVLock_EndFrame(viewer);
	CHECK(!viewer->client->povlock_active);
	CHECK(viewer->client->pov_record_active);
	CHECK(viewer->client->pov_record_wait_respawn);

	target->deadflag = DEAD_NO;
	POVLock_TargetSpawned(target);
	CHECK(viewer->client->povlock_active);
	target->client->pers.connected = false;
	POVLock_EndFrame(viewer);
	CHECK(!viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_active);

	target->client->pers.connected = true;
	CHECK(POVLock_Command(viewer, "alpha"));
	target->client->ctf.ctfid++;
	POVLock_EndFrame(viewer);
	CHECK(!viewer->client->povlock_active);
	CHECK(viewer->client->pov_record_wait_respawn);
	return 0;
}

static int TestRecordOrderingAndRespawn(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *target = &edicts[2];

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	target->client->ps.gunindex = 42;

	CHECK(POVLock_Command(viewer, "alpha"));
	CHECK(viewer->client->pov_record_pending);
	CHECK(!viewer->client->pov_record_sent);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);
	POVLock_EndFrame(viewer);
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Only the watched target's completed end-frame follower refresh may arm
	 * the client demo command, and the copied playerstate must precede it. */
	ResetNetwork();
	ordering_viewer = viewer;
	ordering_target = target;
	POVLock_EndFrame(target);
	CHECK(write_byte_count == 1 && last_byte == svc_stufftext);
	CHECK(write_string_count == 1 && !strcmp(last_string, "record pov\n"));
	CHECK(unicast_count == 1 && last_unicast == viewer && last_reliable);
	CHECK(copied_before_record);
	CHECK(event_order_count == 4 && !memcmp(event_order, "LBSU", 4));
	CHECK(!viewer->client->pov_record_pending);
	CHECK(viewer->client->pov_record_sent);

	POVLock_EndFrame(target);
	CHECK(write_byte_count == 1 && write_string_count == 1 && unicast_count == 1);
	CHECK(POVLock_Command(viewer, "alpha"));
	CHECK(write_string_count == 1);

	/* An ordinary respawn clears only the instantaneous ctfid lock. */
	POVLock_TargetRespawning(target);
	CHECK(viewer->client->pov_record_active);
	CHECK(viewer->client->pov_record_wait_respawn);
	CHECK(!viewer->client->povlock_active);
	CHECK(viewer->client->ps.gunindex == 0);
	CHECK(write_string_count == 1);
	target->client->ctf.ctfid = 202;
	target->client->ps.gunindex = 84;
	POVLock_TargetSpawned(target);
	CHECK(viewer->client->povlock_active);
	CHECK(!viewer->client->pov_record_wait_respawn);
	CHECK(viewer->client->povlock_target_ctfid == 202);
	POVLock_EndFrame(target);
	CHECK(write_string_count == 1);

	POVLock_ClearTarget(target);
	CHECK(!viewer->client->pov_record_active);
	CHECK(viewer->client->pov_record_stop_sent);
	CHECK(write_string_count == 2 && !strcmp(last_string, "stop\n"));
	CHECK(unicast_count == 2 && last_reliable);
	POVLock_ClearTarget(target);
	CHECK(write_string_count == 2 && unicast_count == 2);
	return 0;
}

static int TestUnsafeReplacementAndViewerLifecycle(void)
{
	edict_t *viewer = &edicts[1];
	edict_t *target = &edicts[2];

	/* Same SG slot with a different immutable instance is terminal. */
	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	sg_instance[0] = 1002ULL;
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));

	/* Rebinding the process slot to a different edict is also terminal. */
	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	SetupClient(3, "Replacement", true, false, 0, 203);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	BindSG(3, 0, 2001ULL);
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));

	/* A human/legacy substitute cannot inherit SG recording authority. */
	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	target->flags &= ~FL_BOT;
	CHECK(!POVLock_Update(viewer));
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));

	/* Viewer conversion, reuse, and disconnect have no valid old endpoint. */
	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	viewer->client->resp.spectator = false;
	viewer->client->ctf.teamnum = CTF_TEAM_RED;
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->pov_record_active && write_string_count == 0);

	/* A conversion initiated while the old spectator endpoint is still valid
	 * receives its one terminal stop before the body is rebuilt. */
	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	POVLock_Clear(viewer);
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	viewer->client->ctf.ctfid++;
	CHECK(!POVLock_Update(viewer));
	CHECK(!viewer->client->pov_record_active && write_string_count == 0);

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	viewer->client->pers.connected = false;
	POVLock_ViewerDisconnected(viewer);
	CHECK(!viewer->client->pov_record_active && write_string_count == 0);
	CHECK(!viewer->client->pov_record_stop_sent);

	/* Retirement closes even while the instantaneous lock is waiting. */
	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	POVLock_TargetRespawning(target);
	ResetNetwork();
	POVLock_SGInstanceRetired(0, 1001ULL);
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));
	POVLock_SGInstanceRetired(0, 1001ULL);
	CHECK(write_string_count == 1);
	return 0;
}

static int TestStopAll(void)
{
	edict_t *viewer = &edicts[1];

	ResetFixture();
	SetupClient(1, "Recorder", false, true, 0, 1);
	SetupClient(2, "Alpha", true, false, 7, 102);
	BindSG(2, 0, 1001ULL);
	CHECK(POVLock_Command(viewer, "alpha"));
	ResetNetwork();
	POVLock_StopAll();
	CHECK(!viewer->client->pov_record_active);
	CHECK(write_string_count == 1 && !strcmp(last_string, "stop\n"));
	POVLock_StopAll();
	CHECK(write_string_count == 1);
	return 0;
}

static int TestAdminDirective(void)
{
	edict_t *viewer;
	edict_t *target;

	ResetFixture();
	viewer = AdminClient(1, "pov_s03", CTF_TEAM_OBSERVER, true, 0, 11);
	target = AdminClient(2, "[SG]Arach", CTF_TEAM_RED, false, FL_BOT, 12);
	BindSG(2, 0, 1001ULL);
	CHECK(POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	CHECK(write_byte_count == 1 && last_byte == svc_stufftext);
	CHECK(write_string_count == 1 &&
	      !strcmp(last_string, "cmd povlock [SG]Arach\n"));
	CHECK(unicast_count == 1 && last_unicast == viewer && last_reliable);
	ResetNetwork();
	CHECK(POVRecord_AdminDirective("pov_s03", NULL, true));
	CHECK(write_byte_count == 1 && write_string_count == 1 &&
	      !strcmp(last_string, "cmd povlock off\n"));
	CHECK(unicast_count == 1 && last_unicast == viewer && last_reliable);

	/* The fixed grammar cannot be widened into stufftext. */
	ResetNetwork();
	CHECK(!POVRecord_AdminDirective("pov_s03;quit", "[SG]Arach", false));
	CHECK(!POVRecord_AdminDirective("pov_s03", "Arach", false));
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach;quit", false));
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", true));
	CHECK(!POVRecord_AdminDirective("POV_s03", "[SG]Arach", false));
	CHECK(!POVRecord_AdminDirective("pov_s03_name_that_is_far_too_long", "[SG]Arach", false));
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Missing", false));
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);

	/* Duplicate endpoints and every non-engine endpoint shape fail closed. */
	AdminClient(3, "pov_s03", CTF_TEAM_OBSERVER, true, 0, 13);
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	edicts[3].inuse = false;
	viewer->inuse = false;
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	viewer->inuse = true;
	viewer->flags = FL_BOT;
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	viewer->flags = 0;
	viewer->client->resp.spectator = false;
	viewer->client->ctf.teamnum = CTF_TEAM_RED;
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	viewer->client->resp.spectator = true;
	viewer->client->ctf.teamnum = CTF_TEAM_OBSERVER;
	viewer->client->pers.connected = false;
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	viewer->client->pers.connected = true;
	viewer->client->ctf.ctfid = 0;
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	viewer->client->ctf.ctfid = 11;
	viewer->client = &clients[4];
	strcpy(viewer->client->pers.netname, "pov_s03");
	viewer->client->pers.connected = true;
	viewer->client->resp.spectator = true;
	viewer->client->ctf.teamnum = CTF_TEAM_OBSERVER;
	viewer->client->ctf.ctfid = 11;
	CHECK(!POVRecord_AdminDirective("pov_s03", NULL, true));
	viewer->client = &clients[0];

	/* A start needs one unique, live, playing SG-owned instance. */
	ResetNetwork();
	target->deadflag = DEAD_DEAD;
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	target->deadflag = 0;
	target->inuse = false;
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	target->inuse = true;
	target->client->resp.spectator = true;
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	target->client->resp.spectator = false;
	target->client->ctf.teamnum = CTF_TEAM_OBSERVER;
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	target->client->ctf.teamnum = CTF_TEAM_RED;
	sg_owner[0] = NULL;
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	BindSG(2, 0, 1001ULL);
	AdminClient(3, "[SG]Arach", CTF_TEAM_BLUE, false, FL_BOT, 13);
	BindSG(3, 1, 1002ULL);
	CHECK(!POVRecord_AdminDirective("pov_s03", "[SG]Arach", false));
	CHECK(write_byte_count == 0 && write_string_count == 0 && unicast_count == 0);
	return 0;
}

int main(void)
{
	g_edicts = edicts;
	game.maxclients = 5;
	game.clients = clients;
	test_maxclients.value = 5;
	gi.linkentity = TestLinkEntity;
	gi.trace = TestTrace;
	gi.WriteByte = TestWriteByte;
	gi.WriteString = TestWriteString;
	gi.unicast = TestUnicast;
	if (TestSelectionAndIdentity())
		return 1;
	if (TestOrdinaryChaseCamPOV())
		return 1;
	if (TestInEyesState())
		return 1;
	if (TestEndFrameFailsClosed())
		return 1;
	if (TestRecordOrderingAndRespawn())
		return 1;
	if (TestUnsafeReplacementAndViewerLifecycle())
		return 1;
	if (TestStopAll())
		return 1;
	if (TestAdminDirective())
		return 1;
	puts("povlock_test: ok");
	return 0;
}
