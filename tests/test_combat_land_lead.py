#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]

PROBE = r"""
#include "g_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_combat_land_lead.h"
#include "slipgate/sg_hooks.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

level_locals_t level;
game_locals_t game;
edict_t *g_edicts;
sg_host_t sg_host;
sg_cvars_t sg_cv;
cvar_t *sv_gravity;
#ifdef WEAP_BALANCE_OK
cvar_t *ctfflags;
#endif

static cvar_t landlead;
static cvar_t gravity;
static float floor_z;
static float floor_normal_z;
static float wall_x;
static int force_solid;
static int sky_impact;
static int foreign_impact;
static int block_visibility;
static csurface_t sky_surface;
static edict_t foreign_surface;
#ifdef WEAP_BALANCE_OK
static cvar_t balance;
#endif

void G_ProjectSource(vec3_t point, vec3_t distance, vec3_t forward,
    vec3_t right, vec3_t result)
{
    result[0] = point[0] + forward[0] * distance[0] +
        right[0] * distance[1];
    result[1] = point[1] + forward[1] * distance[0] +
        right[1] * distance[1];
    result[2] = point[2] + forward[2] * distance[0] + distance[2];
}

#include "slipgate/sg_combat.c"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "combat land lead failed at line %d: %s\n", \
            __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static trace_t floor_trace(const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
    trace_t result;
    float start_bottom;
    float end_bottom;
    float fraction;
    int axis;

    (void)maxs;
    (void)passent;
    (void)mask;
    memset(&result, 0, sizeof(result));
    result.fraction = 1.0f;
    VectorCopy(end, result.endpos);
    if (force_solid) {
        result.startsolid = true;
        result.allsolid = true;
        result.fraction = 0.0f;
        return result;
    }
    if (block_visibility && !mins && start[2] <= floor_z + 0.01f &&
        end[2] > start[2]) {
        result.fraction = 0.5f;
        result.ent = &g_edicts[0];
        return result;
    }
    start_bottom = start[2] + (mins ? mins[2] : 0.0f);
    end_bottom = end[2] + (mins ? mins[2] : 0.0f);
    if (start_bottom >= floor_z && end_bottom < floor_z) {
        result.fraction = (start_bottom - floor_z) /
            (start_bottom - end_bottom);
        result.plane.normal[2] = floor_normal_z;
        result.plane.dist = floor_z;
    }
    if (start[0] < wall_x && end[0] >= wall_x) {
        fraction = (wall_x - start[0]) / (end[0] - start[0]);
        if (fraction < result.fraction) {
            result.fraction = fraction;
            VectorClear(result.plane.normal);
            result.plane.normal[0] = -1.0f;
            result.plane.dist = -wall_x;
        }
    }
    if (result.fraction >= 1.0f)
        return result;
    for (axis = 0; axis < 3; ++axis)
        result.endpos[axis] = start[axis] +
            result.fraction * (end[axis] - start[axis]);
    result.ent = foreign_impact ? &foreign_surface : &g_edicts[0];
    if (sky_impact)
        result.surface = &sky_surface;
    return result;
}

static void airborne_player(edict_t *enemy, float x)
{
    static gclient_t client;

    memset(enemy, 0, sizeof(*enemy));
    memset(&client, 0, sizeof(client));
    enemy->client = &client;
    VectorSet(enemy->s.origin, x, 0.0f, 280.0f);
    VectorSet(enemy->mins, -16.0f, -16.0f, -24.0f);
    VectorSet(enemy->maxs, 16.0f, 16.0f, 32.0f);
    VectorAdd(enemy->s.origin, enemy->mins, enemy->absmin);
    VectorAdd(enemy->s.origin, enemy->maxs, enemy->absmax);
}

static int solve(edict_t *enemy, int weapon, vec3_t lead)
{
    vec3_t eye = { 0.0f, 0.0f, 38.0f };
    combat_solve_t result = Combat_Solve(enemy, weapon, eye, lead);

    return result.landing_splash ? 1 : 0;
}

static int landing_splash(edict_t *enemy, vec3_t lead, vec3_t impact,
    float *projectile_time)
{
    edict_t self = { 0 };
    gclient_t client = { 0 };
    usercmd_t cmd = { 0 };
    vec3_t origin = { 0.0f, 0.0f, 16.0f };
    vec3_t eye, aim, muzzle, shotdir;
    combat_solve_t result;
    float source_pad;

    self.client = &client;
    self.viewheight = 22.0f;
    VectorCopy(origin, self.s.origin);
    client.pers.hand = RIGHT_HANDED;
    VectorCopy(origin, eye);
    eye[2] += self.viewheight;
    result = Combat_Solve(enemy, SG_W_ROCKETLAUNCHER, eye, lead);
    VectorSubtract(lead, eye, aim);
    if (!result.landing_splash || VectorNormalize(aim) < 1.0f ||
        !Combat_FinalizePointRay(&self, SG_W_ROCKETLAUNCHER, lead, aim,
            &cmd, muzzle,
            shotdir, &source_pad) ||
        !SG_CombatLandingSplashClear(&self, enemy, muzzle, shotdir,
            result.landing, &result.projectile_time, impact))
        return 0;
    *projectile_time = result.projectile_time;
    return 1;
}

int main(void)
{
    edict_t enemy;
    edict_t edicts[1];
    vec3_t lead, impact;
    float projectile_time;

    memset(&landlead, 0, sizeof(landlead));
    memset(&gravity, 0, sizeof(gravity));
    landlead.value = 1.0f;
    gravity.value = 800.0f;
    sg_cv.landlead = &landlead;
    sv_gravity = &gravity;
#ifdef WEAP_BALANCE_OK
    memset(&balance, 0, sizeof(balance));
    balance.value = CTF_WEAP_BALANCE;
    ctfflags = &balance;
    CHECK(SG_CombatLandLeadTestSplashReaches(239.999f));
    CHECK(!SG_CombatLandLeadTestSplashReaches(240.0f));
#else
    CHECK(SG_CombatLandLeadTestSplashReaches(120.0f));
    CHECK(!SG_CombatLandLeadTestSplashReaches(
        nextafterf(120.0f, INFINITY)));
#endif
    sg_host.trace = floor_trace;
    memset(edicts, 0, sizeof(edicts));
    g_edicts = edicts;
    floor_z = 0.0f;
    floor_normal_z = 1.0f;
    wall_x = 100000.0f;
    sky_surface.flags = SURF_SKY;

    airborne_player(&enemy, 200.0f);
    CHECK(solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(fabsf(lead[2]) < 0.001f);
    CHECK(!landing_splash(&enemy, lead, impact, &projectile_time));

    airborne_player(&enemy, 510.0f);
    CHECK(solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(landing_splash(&enemy, lead, impact, &projectile_time));
    CHECK(fabsf(impact[2]) < 0.001f);
#ifdef WEAP_BALANCE_OK
    CHECK(projectile_time > 0.65f && projectile_time < 0.7f);
#else
    CHECK(projectile_time > 0.75f && projectile_time < 0.8f);
#endif

    sky_impact = 1;
    CHECK(!landing_splash(&enemy, lead, impact, &projectile_time));
    sky_impact = 0;
    foreign_impact = 1;
    CHECK(!landing_splash(&enemy, lead, impact, &projectile_time));
    foreign_impact = 0;
    block_visibility = 1;
    CHECK(!landing_splash(&enemy, lead, impact, &projectile_time));
    block_visibility = 0;

    wall_x = 450.0f;
    CHECK(landing_splash(&enemy, lead, impact, &projectile_time));
    CHECK(fabsf(impact[0] - wall_x) < 0.01f);
    wall_x = 100000.0f;

    airborne_player(&enemy, 800.0f);
    CHECK(solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(!landing_splash(&enemy, lead, impact, &projectile_time));

    airborne_player(&enemy, 135.0f);
    VectorSet(enemy.velocity, 200.0f, 0.0f, -600.0f);
    CHECK(landing_splash(&enemy, lead, impact, &projectile_time));
    CHECK(projectile_time < 0.35f);

#ifndef WEAP_BALANCE_OK
    floor_z = -8.0625f;
    airborne_player(&enemy, 536.0f);
    CHECK(solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(!landing_splash(&enemy, lead, impact, &projectile_time));
    floor_z = 0.0f;
#endif

    airborne_player(&enemy, 536.0f);
    floor_normal_z = 0.0f;
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(lead[2] > 250.0f);
    floor_normal_z = 1.0f;
    enemy.client->hookstate = 2;
    enemy.client->hook = &enemy;
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    enemy.client->hookstate = 0;
    enemy.client->hook = NULL;
    enemy.waterlevel = 2;
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    enemy.waterlevel = 0;
    force_solid = 1;
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(lead[2] > 250.0f);
    force_solid = 0;

    floor_z = -10000.0f;
    airborne_player(&enemy, 200.0f);
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(lead[2] > 250.0f);

    floor_z = 0.0f;
    landlead.value = 0.0f;
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(lead[2] > 250.0f);
    landlead.value = 1.0f;
    enemy.groundentity = &enemy;
    CHECK(!solve(&enemy, SG_W_ROCKETLAUNCHER, lead));
    CHECK(lead[2] > 250.0f);
    puts("combat land lead production probe: ok");
    return 0;
}
"""


class CombatLandLeadTest(unittest.TestCase):
    def test_landing_splash_requires_reachable_world_impact(self) -> None:
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("cc is unavailable")
        with tempfile.TemporaryDirectory(prefix="sg-combat-land-") as temp:
            root = Path(temp)
            source = root / "combat_land_lead_probe.c"
            source.write_text(textwrap.dedent(PROBE))
            for suffix, defines in (("default", []),
                                    ("balanced", ["-DWEAP_BALANCE_OK"])):
                binary = root / f"combat_land_lead_probe_{suffix}"
                command = [
                    cc, "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                    "-Wpedantic", "-ffunction-sections",
                    "-fdata-sections", "-I.", "-DSG_COMBAT_AIM_TEST",
                    *defines, "slipgate/sg_combat_land_lead.c", "q_shared.c",
                    str(source), "-Wl,--gc-sections", "-lm", "-o",
                    str(binary),
                ]
                compiled = subprocess.run(command, cwd=ROOT, text=True,
                                          capture_output=True, check=False)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                ran = subprocess.run([str(binary)], cwd=ROOT, text=True,
                                     capture_output=True, check=False)
                self.assertEqual(ran.returncode, 0, ran.stderr)
                self.assertIn("combat land lead production probe: ok",
                              ran.stdout)

if __name__ == "__main__":
    unittest.main()
