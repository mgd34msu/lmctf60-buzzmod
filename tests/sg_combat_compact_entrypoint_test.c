#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_belief_runtime.h"
#include "slipgate/sg_cvars.h"

game_locals_t game;
level_locals_t level;
game_export_t globals;
spawn_temp_t st;
sg_cvars_t sg_cv;

static edict_t entities[4];
static gclient_t clients[3];
edict_t *g_edicts = entities;

static sg_belief_runtime_particle_t current_particles[2];
static sg_belief_runtime_particle_t other_particles[2];
static sg_belief_runtime_view_t current_view;
static sg_belief_runtime_view_t other_view;

int SG_CacoCompactBeliefActive(void)
{
	return 1;
}

const sg_belief_runtime_view_t *SG_CacoCompactBeliefViewForClient(
	uint8_t audience_team, uint32_t client_id)
{
	if (audience_team != CTF_TEAM_RED)
		return NULL;
	if (client_id == 1U)
		return &current_view;
	if (client_id == 2U)
		return &other_view;
	return NULL;
}

qboolean SG_OwnsBot(edict_t *entity)
{
	return entity == &entities[1];
}

#define static
#include "slipgate/sg_combat.c"
#undef static

static void Setup(void)
{
	memset(entities, 0, sizeof(entities));
	memset(clients, 0, sizeof(clients));
	memset(current_particles, 0, sizeof(current_particles));
	memset(other_particles, 0, sizeof(other_particles));
	memset(&current_view, 0, sizeof(current_view));
	memset(&other_view, 0, sizeof(other_view));
	game.maxclients = 3;
	game.clients = clients;
	globals.num_edicts = 4;
	entities[1].inuse = true;
	entities[1].client = &clients[0];
	entities[1].health = 100;
	clients[0].ctf.teamnum = CTF_TEAM_RED;
	clients[0].ctf.ctfid = UINT64_C(101);

	current_view.audience_team = CTF_TEAM_RED;
	current_view.target_team = CTF_TEAM_BLUE;
	current_view.target_life.client_id = 1U;
	current_view.target_life.spawn_generation = UINT64_C(202);
	current_view.confidence = 1.0f;
	current_view.particles = current_particles;
	current_view.particle_count = 2U;
	current_particles[0].position[0] = 100.0f;
	current_particles[0].weight = 0.99f;
	current_particles[1].position[0] = 900.0f;
	current_particles[1].weight = 0.01f;

	other_view = current_view;
	other_view.target_life.client_id = 2U;
	other_view.target_life.spawn_generation = UINT64_C(303);
	other_view.particles = other_particles;
	other_view.particle_count = 2U;
	other_particles[0].position[0] = 500.0f;
	other_particles[0].weight = 0.99f;
	other_particles[1].position[0] = 900.0f;
	other_particles[1].weight = 0.01f;
	Combat_ResetLevel();
}

int main(void)
{
	const float expected = 108.0f;

	Setup();
	SG_CombatAlertFromBeliefs(&entities[1], NULL);
	assert(fabsf(sg_combat[0].alert_range - expected) < 0.01f);
	assert(sg_combat[0].alert_until > level.time);
	return 0;
}
