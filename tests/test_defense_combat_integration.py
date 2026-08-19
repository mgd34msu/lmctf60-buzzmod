#!/usr/bin/env python3
"""Source-order checks for the bounded live defender combat leg."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


# This compiles the production sg_move adapter with dead sections discarded.
# The probe supplies only the real-world facts consumed by DefenseCombatPlan:
# flag/stand, current enemy, current-life identity, support, and trace results.
# It therefore executes the production preserve-first planner and final command
# arbitration, without starting a game module or opening any network service.
PRODUCTION_ADAPTER_PROBE = r"""
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_util.h"

#include <stdio.h>
#include <string.h>

int SG_DefenseCombatTestAdapter(edict_t *e, sg_bot_t *bot, int team,
    int hold_post, int engaged, int duel_hold, short weave_side,
    int mutation_mask, usercmd_t *cmd);
int SG_SoundFireTestTeammateNear(edict_t *self, int team,
    const vec3_t target, float radius);
int SG_SoundFireTestImpactSafe(edict_t *self, int team,
    const vec3_t target);
int SG_AimedFireViewReadyTest(short actual_yaw, short actual_pitch,
    short expected_yaw, short expected_pitch);

static edict_t entities[4];
static gclient_t clients[3];
static edict_t flag;
static edict_t stand;
static edict_t *live_enemy;
static edict_t *live_stand;
static cvar_t defcombat_cvar;
static int trace_mode;
static int trace_calls;
static int failures;
static int flag_home;
static int carrying;
static int sound_trace_mode;
static csurface_t sky_surface;

game_locals_t game;
level_locals_t level;
game_export_t globals;
edict_t *g_edicts = entities;
sg_cvars_t sg_cv;
sg_host_t sg_host;

void G_ProjectSource(vec3_t point, vec3_t distance, vec3_t forward,
    vec3_t right, vec3_t result)
{
    result[0] = point[0] + forward[0] * distance[0] +
        right[0] * distance[1];
    result[1] = point[1] + forward[1] * distance[0] +
        right[1] * distance[1];
    result[2] = point[2] + forward[2] * distance[0] + distance[2];
}

enum {
    TRACE_BLOCKED, TRACE_CLEAR, TRACE_FINAL_BODY, TRACE_FINAL_FLOOR,
    TRACE_LONG_BLOCK_SHORT_CLEAR, TRACE_PLAYER_BLOCKED, TRACE_FLOOR_BLOCKED,
    TRACE_VERTICAL_BLOCKED, TRACE_RING_BODY_BLOCKED
};
enum {
    SOUND_TRACE_NONE, SOUND_TRACE_USEFUL, SOUND_TRACE_OPEN,
    SOUND_TRACE_MUZZLE_BLOCKED, SOUND_TRACE_NEAR_SELF,
    SOUND_TRACE_TEAMMATE, SOUND_TRACE_SKY, SOUND_TRACE_BEYOND_REGION
};
enum {
    MUT_CARRY = 1, MUT_FLAG_AWAY = 2, MUT_STAND = 4, MUT_HOLD = 8,
    MUT_ROLE = 16, MUT_DEFSTAND = 32, MUT_ENEMY = 64, MUT_CTFID = 128,
    MUT_DEAD = 256, MUT_BODY = 512, MUT_FLOOR = 1024, MUT_HOOK = 2048,
    MUT_NADE = 4096, MUT_DOOR = 8192, MUT_NAV = 16384, MUT_AS = 32768,
    MUT_BEAT = 65536, MUT_LINGER = 131072, MUT_BRAKE = 262144,
    MUT_PROVED = 524288
};

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "defense adapter probe failed at line %d: %s\\n", \
            __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static trace_t ProbeTrace(const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
    trace_t result;
    float dx, dy;
    int body_trace;

    (void)mins;
    (void)maxs;
    (void)start;
    (void)passent;
    (void)contentmask;
    memset(&result, 0, sizeof(result));
    result.fraction = 1.0f;
    VectorCopy(end, result.endpos);
    trace_calls++;
    if (contentmask == MASK_SHOT && sound_trace_mode != SOUND_TRACE_NONE) {
        int muzzle = (trace_calls & 1) != 0;

        if (muzzle) {
            if (sound_trace_mode == SOUND_TRACE_MUZZLE_BLOCKED) {
                result.fraction = 0.5f;
                result.endpos[0] = start[0] + 4.0f;
            }
            return result;
        }
        if (sound_trace_mode == SOUND_TRACE_OPEN)
            return result;
        result.fraction = 0.5f;
        if (sound_trace_mode == SOUND_TRACE_NEAR_SELF) {
            result.endpos[0] = entities[1].s.origin[0] + 80.0f;
            result.endpos[1] = entities[1].s.origin[1];
            result.endpos[2] = entities[1].s.origin[2];
        } else if (sound_trace_mode == SOUND_TRACE_BEYOND_REGION) {
            result.endpos[0] = end[0] + 400.0f;
        } else {
            /* Useful impact at the center of the heard region. */
            result.endpos[0] = 900.0f;
            result.endpos[1] = 40.0f;
            result.endpos[2] = 24.0f;
        }
        if (sound_trace_mode == SOUND_TRACE_TEAMMATE)
            result.ent = &entities[3];
        if (sound_trace_mode == SOUND_TRACE_SKY) {
            sky_surface.flags = SURF_SKY;
            result.surface = &sky_surface;
        }
        return result;
    }
    dx = end[0] - start[0];
    dy = end[1] - start[1];
    body_trace = (trace_calls & 1) != 0;
    if (trace_mode == TRACE_BLOCKED) {
        result.startsolid = true;
        return result;
    }
    if (trace_mode == TRACE_LONG_BLOCK_SHORT_CLEAR && body_trace &&
        dx * dx + dy * dy > 36.0f * 36.0f) {
        result.startsolid = true;
        return result;
    }
    if (trace_mode == TRACE_RING_BODY_BLOCKED && body_trace && end[1] < 0.0f) {
        result.startsolid = true;
        return result;
    }
    if (trace_mode == TRACE_PLAYER_BLOCKED && body_trace) {
        result.ent = &entities[2];
    }
    if (trace_mode == TRACE_FINAL_BODY && trace_calls == 3) {
        result.startsolid = true;
        return result;
    }
    if ((trace_mode == TRACE_FINAL_FLOOR && trace_calls == 4) ||
        (trace_mode == TRACE_FLOOR_BLOCKED && !body_trace)) {
        result.fraction = 1.0f;
        return result;
    }
    if (!body_trace) {
        /* The second trace is the floor probe, landing at the current body Z. */
        result.fraction = 1.0f / 3.0f;
        result.endpos[2] = trace_mode == TRACE_VERTICAL_BLOCKED
            ? entities[1].s.origin[2] + 25.0f : 24.0f;
    }
    return result;
}

qboolean ctf_flagathome(edict_t *whichflag)
{
    return whichflag == &flag && flag_home;
}

edict_t *ClientHasFlag(edict_t *ent)
{
    return ent == &entities[1] && carrying ? &flag : NULL;
}

edict_t *SG_OwnFlag(int team)
{
    return team == CTF_TEAM_RED ? &flag : NULL;
}

edict_t *SG_FlagStand(int team, qboolean own)
{
    return team == CTF_TEAM_RED && own ? live_stand : NULL;
}

edict_t *SG_CombatLiveEnemy(edict_t *self)
{
    return self == &entities[1] ? live_enemy : NULL;
}

float SG_PersonaCampScale(edict_t *ent)
{
    (void)ent;
    return 1.0f;
}

qboolean SG_ImmutableSupport(edict_t *ent)
{
    (void)ent;
    return true;
}

qboolean SG_TimerPending(float stamp)
{
    return level.time < stamp;
}

rune_t *SG_Rune(void)
{
    return NULL;
}

static void Setup(void)
{
    edict_t *self = &entities[1];
    edict_t *enemy = &entities[2];

    memset(entities, 0, sizeof(entities));
    memset(clients, 0, sizeof(clients));
    memset(&flag, 0, sizeof(flag));
    memset(&stand, 0, sizeof(stand));
    memset(&defcombat_cvar, 0, sizeof(defcombat_cvar));
    memset(&sg_cv, 0, sizeof(sg_cv));
    memset(&sg_host, 0, sizeof(sg_host));
    memset(&level, 0, sizeof(level));
    game.maxclients = 3;
    self->inuse = true;
    self->client = &clients[0];
    self->health = 100;
    self->deadflag = DEAD_NO;
    self->movetype = MOVETYPE_WALK;
    self->groundentity = g_edicts;
    self->mins[0] = self->mins[1] = -16.0f;
    self->mins[2] = -24.0f;
    self->maxs[0] = self->maxs[1] = 16.0f;
    self->maxs[2] = 32.0f;
    self->s.origin[0] = 72.0f;
    self->s.origin[2] = 24.0f;
    self->client->ctf.teamnum = CTF_TEAM_RED;
    enemy->inuse = true;
    enemy->client = &clients[1];
    enemy->health = 100;
    enemy->deadflag = DEAD_NO;
    enemy->s.origin[0] = 300.0f;
    enemy->s.origin[2] = 24.0f;
    enemy->client->ctf.teamnum = CTF_TEAM_BLUE;
    enemy->client->ctf.ctfid = 77;
    entities[3].inuse = true;
    entities[3].client = &clients[2];
    entities[3].health = 100;
    entities[3].deadflag = DEAD_NO;
    entities[3].client->ctf.teamnum = CTF_TEAM_BLUE;
    entities[3].client->ctf.ctfid = 78;
    flag.inuse = true;
    stand.inuse = true;
    defcombat_cvar.value = 1.0f;
    sg_cv.defcombat = &defcombat_cvar;
    sg_host.trace = ProbeTrace;
    live_enemy = enemy;
    live_stand = &stand;
    flag_home = 1;
    carrying = 0;
    level.time = 10.0f;
    trace_calls = 0;
}

/* This callback is deliberately after the production planner.  It models the
 * world changing between its accepted probe and the final ClientThink write. */
void SG_DefenseCombatTestPostPlan(edict_t *e, sg_bot_t *bot,
    sg_think_t *tc, int mutation_mask)
{
    (void)e;
    if (mutation_mask & MUT_CARRY) carrying = 1;
    if (mutation_mask & MUT_FLAG_AWAY) flag_home = 0;
    if (mutation_mask & MUT_STAND) live_stand = &entities[3];
    if (mutation_mask & MUT_HOLD) tc->hold_post = false;
    if (mutation_mask & MUT_ROLE) tc->role = SG_ROLE_CARRY;
    if (mutation_mask & MUT_DEFSTAND) bot->def_stand = false;
    if (mutation_mask & MUT_ENEMY) live_enemy = &entities[3];
    if (mutation_mask & MUT_CTFID) live_enemy->client->ctf.ctfid++;
    if (mutation_mask & MUT_DEAD) live_enemy->health = 0;
    if (mutation_mask & MUT_BODY) trace_mode = TRACE_FINAL_BODY;
    if (mutation_mask & MUT_FLOOR) trace_mode = TRACE_FINAL_FLOOR;
    if (mutation_mask & MUT_HOOK) bot->hook_phase = 1;
    if (mutation_mask & MUT_NADE) bot->nade_phase = 1;
    if (mutation_mask & MUT_DOOR) tc->door_hold = 1;
    if (mutation_mask & MUT_NAV) tc->have_move = true;
    if (mutation_mask & MUT_BEAT) bot->beat_until = level.time + 1.0f;
    if (mutation_mask & MUT_LINGER) bot->linger_hot = true;
    if (mutation_mask & MUT_BRAKE) bot->term_brake = 0.5f;
    if (mutation_mask & MUT_PROVED) bot->jump_started = true;
}

static void TestBlockedPostStaysStill(void)
{
    sg_bot_t bot;
    usercmd_t cmd;

    Setup();
    memset(&bot, 0, sizeof(bot));
    memset(&cmd, 0, sizeof(cmd));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    trace_mode = TRACE_BLOCKED;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 0);
    CHECK(trace_calls == 8);       /* long then short body+floor probes */
    CHECK(cmd.forwardmove == 0);
    CHECK(cmd.sidemove == 0);
    CHECK(cmd.upmove == 0);
}

static void TestSafePostOwnsTheFinalLeg(void)
{
    sg_bot_t bot;
    usercmd_t cmd;

    Setup();
    memset(&bot, 0, sizeof(bot));
    memset(&cmd, 0, sizeof(cmd));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    trace_mode = TRACE_CLEAR;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 1);
    CHECK(trace_calls == 4);       /* plan body+floor, final body+floor */
    CHECK(cmd.forwardmove == 0);
    CHECK(cmd.sidemove == -160);   /* exact 160u production tangent at yaw 0 */
    CHECK(cmd.upmove == 0);
}

static void TestShortPostFallbackOwnsTheFinalLeg(void)
{
    sg_bot_t bot;
    usercmd_t cmd;

    Setup();
    memset(&bot, 0, sizeof(bot));
    memset(&cmd, 0, sizeof(cmd));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    trace_mode = TRACE_LONG_BLOCK_SHORT_CLEAR;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 1);
    CHECK(trace_calls == 8);       /* 2 long + first short + final, body+floor */
    CHECK(cmd.forwardmove == 0);
    CHECK(cmd.sidemove == -80);    /* 24u horizon encodes the capped 80u leg */
    CHECK(cmd.upmove == 0);
}

static void TestUnsafePostProbesStayStill(void)
{
    const int modes[] = {
        TRACE_PLAYER_BLOCKED, TRACE_FLOOR_BLOCKED, TRACE_VERTICAL_BLOCKED
    };
    unsigned int i;

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        sg_bot_t bot;
        usercmd_t cmd;

        Setup();
        memset(&bot, 0, sizeof(bot));
        memset(&cmd, 0, sizeof(cmd));
        bot.def_stand = true;
        bot.term_brake = 1.0f;
        trace_mode = modes[i];
        CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
            1, 1, 1, 300, 0, &cmd) == 0);
        CHECK(trace_calls == 8);
        CHECK(cmd.forwardmove == 0);
        CHECK(cmd.sidemove == 0);
        CHECK(cmd.upmove == 0);
    }

    {
        sg_bot_t bot;
        usercmd_t cmd;

        Setup();
        memset(&bot, 0, sizeof(bot));
        memset(&cmd, 0, sizeof(cmd));
        bot.def_stand = true;
        bot.term_brake = 1.0f;
        stand.s.origin[0] = 72.0f;
        stand.s.origin[1] = -110.0f; /* first tangent stays off-ring at 48u and 24u */
        trace_mode = TRACE_RING_BODY_BLOCKED; /* opposite tangent remains a body veto */
        CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
            1, 1, 1, 300, 0, &cmd) == 0);
        CHECK(trace_calls == 8);
        CHECK(cmd.forwardmove == 0);
        CHECK(cmd.sidemove == 0);
        CHECK(cmd.upmove == 0);
    }
}

static void TestNonPostRetainsGenericWeave(void)
{
    sg_bot_t bot;
    usercmd_t cmd;

    Setup();
    memset(&bot, 0, sizeof(bot));
    memset(&cmd, 0, sizeof(cmd));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    trace_mode = TRACE_CLEAR;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        0, 1, 1, 300, 0, &cmd) == 0);
    CHECK(trace_calls == 0);
    CHECK(cmd.forwardmove == 0);
    CHECK(cmd.sidemove == 300);
}

static void TestPostPlanMutationIsDenied(void)
{
    const int mutations[] = {
        MUT_CARRY, MUT_FLAG_AWAY, MUT_STAND, MUT_HOLD, MUT_ROLE,
        MUT_DEFSTAND, MUT_ENEMY, MUT_CTFID, MUT_DEAD, MUT_BODY, MUT_FLOOR,
        MUT_HOOK, MUT_NADE, MUT_DOOR, MUT_NAV, MUT_AS, MUT_BEAT, MUT_LINGER,
        MUT_BRAKE, MUT_PROVED
    };
    unsigned int i;

    for (i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++) {
        sg_bot_t bot;
        usercmd_t cmd;

        Setup();
        memset(&bot, 0, sizeof(bot));
        memset(&cmd, 0, sizeof(cmd));
        bot.def_stand = true;
        bot.term_brake = 1.0f;
        trace_mode = TRACE_CLEAR;
        CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
            1, 1, 1, 300, mutations[i], &cmd) == 0);
        CHECK(cmd.forwardmove == 0);
        CHECK(cmd.sidemove == 0);
        CHECK(cmd.upmove == 0);
    }
}

static void TestLeaseTargetSwitchFailsClosed(void)
{
    sg_bot_t bot;
    usercmd_t cmd;

    Setup();
    memset(&bot, 0, sizeof(bot));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    bot.defcombat_enemy_slot = 2;
    bot.defcombat_enemy_ctfid = 77;
    bot.defcombat_tangent_sign = -1;
    bot.defcombat_tangent_until = level.time + 1.0f;
    trace_mode = TRACE_BLOCKED; /* B's two probes fail after the target switch. */
    live_enemy = &entities[3];
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 0);
    CHECK(bot.defcombat_enemy_slot == 0);
    CHECK(bot.defcombat_enemy_ctfid == 0);
    live_enemy = &entities[2];
    trace_mode = TRACE_CLEAR;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 1);
    CHECK(bot.defcombat_tangent_sign != -1); /* A did not inherit old lease. */
}

static void TestCorruptAndExpiredLeaseRemainSafe(void)
{
    sg_bot_t bot;
    usercmd_t cmd;

    Setup();
    memset(&bot, 0, sizeof(bot));
    memset(&cmd, 0, sizeof(cmd));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    bot.defcombat_enemy_slot = 2;
    bot.defcombat_enemy_ctfid = 77;
    bot.defcombat_tangent_sign = 9; /* corrupt preference is never emitted */
    bot.defcombat_tangent_until = level.time + 1.0f;
    trace_mode = TRACE_CLEAR;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 1);
    CHECK(bot.defcombat_tangent_sign == -1 || bot.defcombat_tangent_sign == 1);
    CHECK(cmd.forwardmove == 0);
    CHECK(cmd.sidemove == -160 || cmd.sidemove == 160);

    Setup();
    memset(&bot, 0, sizeof(bot));
    memset(&cmd, 0, sizeof(cmd));
    bot.def_stand = true;
    bot.term_brake = 1.0f;
    bot.defcombat_enemy_slot = 2;
    bot.defcombat_enemy_ctfid = 77;
    bot.defcombat_tangent_sign = -1;
    bot.defcombat_tangent_until = level.time - 0.1f; /* expired */
    trace_mode = TRACE_CLEAR;
    CHECK(SG_DefenseCombatTestAdapter(&entities[1], &bot, CTF_TEAM_RED,
        1, 1, 1, 300, 0, &cmd) == 1);
    CHECK(cmd.forwardmove == 0);
    CHECK(cmd.sidemove == -160 || cmd.sidemove == 160);
}

static void TestDeathLeaseReset(void)
{
    sg_bot_t bot;

    memset(&bot, 0, sizeof(bot));
    bot.defcombat_enemy_slot = 2;
    bot.defcombat_enemy_ctfid = 77;
    bot.defcombat_tangent_sign = -1;
    bot.defcombat_tangent_until = 12.5f;
    SG_DefenseCombatLeaseReset(&bot);
    CHECK(bot.defcombat_enemy_slot == 0);
    CHECK(bot.defcombat_enemy_ctfid == 0);
    CHECK(bot.defcombat_tangent_sign == 0);
    CHECK(bot.defcombat_tangent_until == 0.0f);
}

static void TestSoundFireProtectsHumanTeammates(void)
{
    vec3_t belief = { 900.0f, 40.0f, 24.0f };
    edict_t *human = &entities[3];

    Setup();
    human->client->ctf.teamnum = CTF_TEAM_RED;
    VectorCopy(belief, human->s.origin);
    CHECK(SG_SoundFireTestTeammateNear(&entities[1], CTF_TEAM_RED,
        belief, 250.0f) == 1);

    human->deadflag = DEAD_DEAD;
    CHECK(SG_SoundFireTestTeammateNear(&entities[1], CTF_TEAM_RED,
        belief, 250.0f) == 0);
    human->deadflag = DEAD_NO;
    human->health = 0;
    CHECK(SG_SoundFireTestTeammateNear(&entities[1], CTF_TEAM_RED,
        belief, 250.0f) == 0);
    human->health = 100;
    human->client->ctf.teamnum = CTF_TEAM_BLUE;
    CHECK(SG_SoundFireTestTeammateNear(&entities[1], CTF_TEAM_RED,
        belief, 250.0f) == 0);
    human->client->ctf.teamnum = CTF_TEAM_RED;
    human->s.origin[0] = belief[0] + 251.0f;
    CHECK(SG_SoundFireTestTeammateNear(&entities[1], CTF_TEAM_RED,
        belief, 250.0f) == 0);
}

static void TestSoundFireRequiresUsefulSafeImpact(void)
{
    vec3_t belief = { 900.0f, 40.0f, 24.0f };
    edict_t *human = &entities[3];

    Setup();
    human->client->ctf.teamnum = CTF_TEAM_BLUE;
    sound_trace_mode = SOUND_TRACE_USEFUL;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 1);

    sound_trace_mode = SOUND_TRACE_OPEN;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 0);
    sound_trace_mode = SOUND_TRACE_MUZZLE_BLOCKED;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 0);
    sound_trace_mode = SOUND_TRACE_NEAR_SELF;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 0);
    sound_trace_mode = SOUND_TRACE_SKY;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 0);
    sound_trace_mode = SOUND_TRACE_BEYOND_REGION;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 0);

    human->client->ctf.teamnum = CTF_TEAM_RED;
    VectorCopy(belief, human->s.origin);
    sound_trace_mode = SOUND_TRACE_TEAMMATE;
    trace_calls = 0;
    CHECK(SG_SoundFireTestImpactSafe(&entities[1], CTF_TEAM_RED,
        belief) == 0);
}

static void TestAimedFireRequiresTheExactValidatedView(void)
{
    CHECK(SG_AimedFireViewReadyTest(1200, -300, 1200, -300) == 1);
    CHECK(SG_AimedFireViewReadyTest(1199, -300, 1200, -300) == 0);
    CHECK(SG_AimedFireViewReadyTest(1200, -299, 1200, -300) == 0);
    CHECK(SG_AimedFireViewReadyTest(-32768, 32767, -32768, 32767) == 1);
}

int main(void)
{
	TestSoundFireProtectsHumanTeammates();
	TestSoundFireRequiresUsefulSafeImpact();
	TestAimedFireRequiresTheExactValidatedView();
    TestBlockedPostStaysStill();
    TestSafePostOwnsTheFinalLeg();
    TestShortPostFallbackOwnsTheFinalLeg();
    TestUnsafePostProbesStayStill();
    TestNonPostRetainsGenericWeave();
    TestPostPlanMutationIsDenied();
    TestLeaseTargetSwitchFailsClosed();
    TestCorruptAndExpiredLeaseRemainSafe();
    TestDeathLeaseReset();
    if (failures)
        return 1;
    puts("defense combat production adapter probe: ok");
    return 0;
}
"""


class DefenseCombatIntegrationTest(unittest.TestCase):
    def test_shared_sound_belief_uses_best_listener_and_private_randomness(self) -> None:
        caco = (ROOT / "slipgate/sg_caco.c").read_text()
        ear = (ROOT / "slipgate/sg_ear_random.h").read_text()
        note = caco[caco.index("void SG_NoteSound"):
                    caco.index("/* ------------------------------------------------------------ the hit sense */")]

        self.assertIn("best_listener[2]", note)
        self.assertIn("eteam != CTF_TEAM_RED && eteam != CTF_TEAM_BLUE", note)
        self.assertIn("team != CTF_TEAM_RED && team != CTF_TEAM_BLUE", note)
        self.assertIn("SG_EarCandidateBetter(frac, i", note)
        self.assertIn("for (t = 0; t < 2; t++)", note)
        self.assertEqual(note.count("Caco_EnemyPlace("), 1)
        self.assertNotIn("crandom()", note)
        self.assertEqual(note.count("SG_EarRandomNext(sg_ear_random[t])"), 3)
        self.assertIn("candidate_fraction < best_fraction", ear)
        self.assertIn("candidate_client < best_client", ear)

    def test_aimed_fire_survives_only_at_its_validated_command_view(self) -> None:
        move = (ROOT / "slipgate" / "sg_move.c").read_text()
        combat = move.index("SG_CombatFrame(e, cmd, &engaged)")
        beat = move.index("THE SPAWN BEAT'S EYES", combat)
        air = move.index("THE AIR-STRAFE CHAIN", beat)
        boundary = move.index("if (step == 0 && AimedFireViewReady(cmd, aimed_fire_yaw", air)
        think = move.index("ClientThink(e, cmd);", boundary)

        self.assertLess(combat, beat)
        self.assertLess(beat, air)
        self.assertLess(air, boundary)
        self.assertLess(boundary, think)
        self.assertIn("!aimed_fire_requested && !nade_release", move[beat:air])
        self.assertIn("!aimed_fire_requested && !proved_control", move[air:boundary])
        self.assertIn("cmd->buttons &= ~BUTTON_ATTACK;", move[boundary:think])
        self.assertIn("aimed_fire_view_admitted = true;", move[boundary:think])
        self.assertIn("SG_TimerArm(&bot->soundfire_next, 8.0f);",
                      move[boundary:think])

    def test_sound_fire_splash_veto_uses_the_complete_client_roster(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        helper = move[move.index("static qboolean SoundFireTeammateNear"):
                      move.index("static qboolean DefenseCombatEnemyCurrent")]
        sound = move[move.index("SOUND-DIRECTED FIRE"):
                     move.index("bot->engaged_last = engaged;")]

        self.assertIn("client_index <= game.maxclients", helper)
        self.assertIn("&g_edicts[client_index]", helper)
        self.assertIn("mate->client->ctf.teamnum != team", helper)
        self.assertIn("mate->deadflag", helper)
        self.assertIn("SoundFireImpactSafe(e, team", sound)
        self.assertIn("SoundFireTeammateNear(self, team", helper)
        self.assertIn("muzzle_trace.fraction < 1.0f", helper)
        self.assertIn("shot_trace.fraction >= 1.0f", helper)
        self.assertIn("VectorLength(delta) < 180.0f", helper)
        self.assertNotIn("SG_MAXBOTS", sound)

    def test_aimed_splash_veto_uses_the_complete_client_roster(self) -> None:
        combat = (ROOT / "slipgate/sg_combat.c").read_text()
        helper = combat[combat.index("static qboolean Combat_TeamSplashSafe"):
                        combat.index("static qboolean Combat_SplashSafe")]
        splash = combat[combat.index("static qboolean Combat_SplashSafe"):
                        combat.index("/* ------------------------------------------------------------- the ladders")]

        self.assertIn("client_index <= game.maxclients", helper)
        self.assertIn("&g_edicts[client_index]", helper)
        self.assertIn("mate->client->ctf.teamnum != team", helper)
        self.assertIn("mate->deadflag", helper)
        self.assertIn("mate->movetype == MOVETYPE_NOCLIP", helper)
        self.assertIn("VectorLength(delta) < dsafe", helper)
        self.assertIn("Combat_TeamSplashSafe(self, dsafe, impact)", splash)
        self.assertNotIn("SG_MAXBOTS", helper)

    def test_aimed_hitscan_veto_covers_the_physical_spread_envelope(self) -> None:
        combat = (ROOT / "slipgate/sg_combat.c").read_text()
        helper = combat[combat.index("static qboolean Combat_TeamHitscanSafe"):
                        combat.index("static qboolean Combat_SplashSafe")]
        frame = combat[combat.index("void SG_CombatFrame"):
                       combat.index("void SG_CombatHit")]

        self.assertIn("client_index <= game.maxclients", helper)
        self.assertIn("&g_edicts[client_index]", helper)
        self.assertIn("mate->client->ctf.teamnum != team", helper)
        self.assertIn("DEFAULT_BULLET_HSPREAD", helper)
        self.assertIn("DEFAULT_SHOTGUN_HSPREAD", helper)
        self.assertIn("yaw_angle = 5.0f", helper)
        self.assertIn("yaw_angle = 0.7f", helper)
        self.assertIn("scatter_scale = water_path ? 3.0f : 1.0f", helper)
        self.assertIn("1.0f - 2.0f * radial * radial", helper)
        self.assertIn("source_pad + along", helper)
        self.assertNotIn("SG_MAXBOTS", helper)
        veto = frame.index("if (!Combat_TeamHitscanSafe(self, inhand")
        trigger = frame.index("Cbt_Trigger(self, cmd, st, skill, inhand)")
        self.assertLess(veto, trigger)
        self.assertIn("sg_host.pointcontents(muzzle) & MASK_WATER", frame)
        self.assertIn("MASK_WATER", frame[veto - 500:veto])

    def test_seedless_enemy_observations_cannot_enter_route_belief(self) -> None:
        caco = (ROOT / "slipgate/sg_caco.c").read_text()
        writer = caco[caco.index("static void Caco_EnemyPlace"):
                      caco.index("static void Caco_ScanEnemies")]
        descend = (ROOT / "slipgate/sg_descend.c").read_text()
        carry = descend[descend.index("CARRIER COVER (sg_carrycover"):
                        descend.index("THE SWITCHING COST", descend.index(
                            "CARRIER COVER (sg_carrycover"))]

        self.assertIn("Caco_EnemyObservationValid(r, team1, client",
                      writer)
        self.assertLess(writer.index("Caco_EnemyObservationValid"),
                        writer.index("Caco_EnemySlot"))
        self.assertIn("Caco_EnemyPlace(r, SG_TeamIdx(viewer_team)", caco)
        self.assertIn("Caco_EnemyPlace(r, t, ecl, seed", caco)
        self.assertIn("en->seed >= 0", carry)
        self.assertIn("en->seed < SG_Rune()->hdr.num_seeds", carry)

    def test_cvar_and_static_hold_order(self) -> None:
        cvars = (ROOT / "slipgate/sg_cvars.h").read_text()
        move = (ROOT / "slipgate/sg_move.c").read_text()

        self.assertIn('X(defcombat, "sg_defcombat", "1")', cvars)
        hold = move.index("on post: whatever the descent wanted")
        combat = move.index("SG_CombatFrame(e, cmd, &engaged)")
        plan = move.index("defcombat_active = DefenseCombatPlan")
        self.assertLess(hold, combat)
        self.assertLess(combat, plan)

    def test_live_gates_and_hull_rejection(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        chooser = (ROOT / "slipgate/sg_defense_shift.c").read_text()

        for token in (
            "SG_FlagStand(team, true)", "ctf_flagathome(flag)",
            "SG_CombatLiveEnemy(e) == enemy", "role != SG_ROLE_DEFEND",
            "!bot->def_stand", "!hold_post", "MASK_PLAYERSOLID",
            "body.startsolid", "body.allsolid", "body.fraction >= 1.0f",
            "floor.fraction < 1.0f", "floor.ent && floor.ent->client",
            "SG_ImmutableSupport(e->groundentity)", "MOVETYPE_WALK",
            "PMF_DUCKED", "pm_time", "bot->hook_phase",
            "bot->rj_phase", "bot->nade_phase", "bot->drop_started",
            "tc->jump_launch", "proved_control",
        ):
            self.assertIn(token, move)
        for token in (
            "probe->stand_distance < 48.0f", "probe->stand_distance > 128.0f",
            "fabsf(probe->vertical_step) > 24.0f",
        ):
            self.assertIn(token, chooser)

    def test_final_leg_preserves_combat_ownership(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        final_leg = move.index("Convert the world-space tangent")
        swim = move.index("The shared feedback command is the final writer", final_leg)
        call = move[final_leg:swim]
        writer = move[move.index("static qboolean DefenseCombatWriteFinal"):
                      move.index("#ifdef SG_DEFENSE_COMBAT_TEST",
                                 move.index("static qboolean DefenseCombatWriteFinal"))]

        self.assertIn("DefenseCombatWriteFinal(e, bot, team", call)
        self.assertIn("bot->vy_cur", writer)
        self.assertIn("bot->vp_cur / 3.0f", writer)
        self.assertIn("160.0f", writer)
        self.assertIn("cmd->upmove = 0", writer)
        self.assertNotIn("cmd->buttons", writer)
        self.assertNotIn("cmd->angles", writer)
        self.assertNotIn("commit_link", writer)
        self.assertNotIn("goal_field", writer)

    def test_live_fire_does_not_revoke_a_safe_post_leg(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        plan = move[move.index("defcombat_active = DefenseCombatPlan"):
                    move.index("for (step = 0; step < sub; step++)",
                               move.index("defcombat_active = DefenseCombatPlan"))]
        weave = move.index("DefenseCombatApplyDuelWeave(hold_post, proved_control")
        final_leg = move.index("Convert the world-space tangent")

        # A live target is required by the planner, but the ordinary duel
        # weave must not pre-empt the independently trace-approved post leg.
        self.assertIn("tc, as_ok, engaged", plan)
        self.assertNotIn("!duel_hold", plan)
        self.assertLess(weave, final_leg)
        helper = move[move.index("static qboolean DefenseCombatApplyDuelWeave"):
                      move.index("static qboolean DefenseCombatWriteFinal")]
        self.assertIn("hold_post ||", helper)

    def test_trace_approved_direction_has_engagement_local_hysteresis(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()
        chooser = (ROOT / "slipgate/sg_defense_shift.c").read_text()

        for token in (
            "defcombat_enemy_slot", "defcombat_enemy_ctfid",
            "defcombat_tangent_sign", "defcombat_tangent_until",
            "enemy->client->ctf.ctfid", "level.time + 1.25f",
            "request.preferred_tangent_sign",
            "DefenseCombatCandidateSafe(e, stand, &move)",
        ):
            self.assertIn(token, move)
        self.assertIn("request->preferred_tangent_sign == -1", chooser)
        self.assertIn("request->preferred_tangent_sign == 1", chooser)
        arach = (ROOT / "slipgate/sg_arach.c").read_text()
        death_reset = arach[arach.index("static void Bot_ResetLifeActions"):]
        self.assertIn("SG_DefenseCombatLeaseReset(bot);", death_reset)

    def test_short_fallback_is_not_diagnostic_instrumentation(self) -> None:
        move = (ROOT / "slipgate/sg_move.c").read_text()

        self.assertIn("static const float scales[] = { 1.0f, 0.5f }", move)
        self.assertIn("tested.x *= scales[pass]", move)
        self.assertIn("tested.y *= scales[pass]", move)
        self.assertNotIn("DEFCOMBATWHY", move)
        self.assertNotIn("defense_combat_why", move)

    def test_production_adapter_arbitrates_post_tangent_and_weave(self) -> None:
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("cc is unavailable")
        with tempfile.TemporaryDirectory(prefix="sg-defense-combat-") as temporary:
            temporary_path = Path(temporary)
            probe = temporary_path / "defense_combat_probe.c"
            binary = temporary_path / "defense_combat_probe"
            probe.write_text(textwrap.dedent(PRODUCTION_ADAPTER_PROBE))
            command = [
                cc, "-std=c11", "-O0", "-g", "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined", "-fno-strict-aliasing",
                "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra",
                "-Werror", "-Wpedantic", "-Wno-strict-prototypes", "-I.",
                "-DSG_DEFENSE_COMBAT_TEST", "-DSG_SOUND_FIRE_TEST",
                "slipgate/sg_move.c",
                "slipgate/sg_defense_shift.c", "q_shared.c", str(probe),
                "-Wl,--gc-sections", "-lm", "-o", str(binary),
            ]
            compiled = subprocess.run(command, cwd=ROOT, text=True,
                                       capture_output=True, check=False)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(binary)], cwd=ROOT, text=True,
                                 capture_output=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn("defense combat production adapter probe: ok", ran.stdout)


if __name__ == "__main__":
    unittest.main()
