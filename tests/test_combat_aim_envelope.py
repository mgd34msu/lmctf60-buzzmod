#!/usr/bin/env python3
"""Focused executable checks for SLIPGATE's physical non-ballistic aim law."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "slipgate" / "sg_combat.c"
SOURCE = SOURCE_PATH.read_text()


# This links only the narrow SG_COMBAT_AIM_TEST seam plus G_ProjectSource and
# AngleVectors.  The probe calls the production pack -> muzzle -> constrain
# loop, while its ray/box predicate is deliberately independent.
PROBE = r"""
#include "g_local.h"

#include <math.h>
#include <stdio.h>

level_locals_t level;

int SG_CombatAimTestFinalize(int weapon, int hand, int machinegun_shots,
    int gunframe, float viewheight, const vec3_t origin, const vec3_t lead,
    const vec3_t mins, const vec3_t maxs, const vec3_t requested,
    float elapsed, vec3_t muzzle_out, vec3_t dir_out, float *source_pad_out);
int SG_CombatAimTestWeaponRay(int weapon, int hand, int machinegun_shots,
    int gunframe, float viewheight, const vec3_t origin, const vec3_t shot,
    vec3_t muzzle_out, vec3_t dir_out, float *source_pad_out);
int SG_CombatAimTestTraceClear(int weapon, int enemy_hit, int unobstructed,
    int teammate_hit);

enum {
    W_BLASTER = 0,
    W_SHOTGUN,
    W_SSHOTGUN,
    W_MACHINEGUN,
    W_CHAINGUN,
    W_GRENADE,
    W_ROCKET,
    W_HYPER,
    W_RAIL,
    W_BFG,
    W_PLASMA
};

enum { HAND_RIGHT = 0, HAND_LEFT = 1, HAND_CENTER = 2 };

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "combat aim probe failed at line %d: %s\\n", \
            __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int nearly(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

static int nearly_vec(const vec3_t a, const vec3_t b, float eps)
{
    return nearly(a[0], b[0], eps) && nearly(a[1], b[1], eps) &&
           nearly(a[2], b[2], eps);
}

static float normalize_vec(vec3_t v)
{
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (length > 0.0f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
    return length;
}

/* Independent slab predicate.  centre is the live absmin/absmax centre,
 * not the entity origin, so crouched asymmetric bounds are covered too. */
static int ray_hits_box(const vec3_t source, const vec3_t direction,
    const vec3_t centre, const vec3_t mins, const vec3_t maxs)
{
    float enter = 0.0f;
    float leave = 1000000.0f;
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        float local_centre = 0.5f * (mins[axis] + maxs[axis]);
        float low = centre[axis] + mins[axis] - local_centre;
        float high = centre[axis] + maxs[axis] - local_centre;
        float first, last;

        if (fabsf(direction[axis]) < 0.000001f) {
            if (source[axis] < low || source[axis] > high)
                return 0;
            continue;
        }
        first = (low - source[axis]) / direction[axis];
        last = (high - source[axis]) / direction[axis];
        if (first > last) {
            float swap = first;
            first = last;
            last = swap;
        }
        if (first > enter)
            enter = first;
        if (last < leave)
            leave = last;
        if (leave < enter)
            return 0;
    }
    return leave >= 0.0f;
}

static void expected_muzzle(int weapon, int hand, int gunframe,
    const vec3_t origin, vec3_t expected)
{
    const float pi = 3.14159265358979323846f;
    float forward_offset = 0.0f;
    float right_offset = 0.0f;
    float z_offset = 14.0f;       /* viewheight 22 minus 8 */

    switch (weapon) {
    case W_BLASTER:
        forward_offset = 24.0f;
        right_offset = 8.0f;
        break;
    case W_HYPER:
    {
        float rotation = ((float)gunframe - 5.0f) * 2.0f * pi / 6.0f;

        forward_offset = 24.0f - 4.0f * sinf(rotation);
        right_offset = 8.0f;
        z_offset += 4.0f * cosf(rotation);
        break;
    }
    case W_ROCKET:
    case W_BFG:
    case W_PLASMA:
        forward_offset = 8.0f;
        right_offset = 8.0f;
        break;
    case W_RAIL:
        right_offset = 7.0f;
        break;
    case W_SHOTGUN:
    case W_SSHOTGUN:
    case W_MACHINEGUN:
        right_offset = 8.0f;
        break;
    case W_CHAINGUN:
        right_offset = 7.0f;
        break;
    default:
        fprintf(stderr, "unexpected weapon %d in expected_muzzle\\n", weapon);
        return;
    }

    /* With a yaw-zero final view, forward=(1,0,0), right=(0,-1,0). */
    expected[0] = origin[0] + forward_offset;
    expected[1] = origin[1] + (hand == HAND_RIGHT ? -right_offset :
                                hand == HAND_LEFT ? right_offset : 0.0f);
    expected[2] = origin[2] + z_offset;
}

static int test_exact_muzzle_offsets(void)
{
    static const int weapons[] = {
        W_BLASTER, W_SHOTGUN, W_SSHOTGUN, W_MACHINEGUN, W_CHAINGUN,
        W_ROCKET, W_HYPER, W_RAIL, W_BFG, W_PLASMA
    };
    static const int hyper_frames[] = { 6, 7, 8, 9, 10, 11 };
    vec3_t origin = { 100.0f, 200.0f, 300.0f };
    vec3_t shot = { 1.0f, 0.0f, 0.0f };
    int hand, index;

    for (hand = HAND_RIGHT; hand <= HAND_CENTER; ++hand) {
        for (index = 0; index < (int)(sizeof(weapons) / sizeof(weapons[0]));
             ++index) {
            int weapon = weapons[index];
            int frames = weapon == W_HYPER
                ? (int)(sizeof(hyper_frames) / sizeof(hyper_frames[0])) : 1;
            int frame_index;

            for (frame_index = 0; frame_index < frames; ++frame_index) {
                int gunframe = weapon == W_HYPER ? hyper_frames[frame_index] : 0;
                vec3_t muzzle, direction, expected;
                float source_pad = -1.0f;

                CHECK(SG_CombatAimTestWeaponRay(weapon, hand, 0, gunframe,
                    22.0f, origin, shot, muzzle, direction, &source_pad));
                expected_muzzle(weapon, hand, gunframe, origin, expected);
                CHECK(nearly_vec(muzzle, expected, 0.03f));
                CHECK(nearly(direction[0], 1.0f, 0.0001f));
                CHECK(nearly(direction[1], 0.0f, 0.0001f));
                CHECK(nearly(direction[2], 0.0f, 0.0001f));
                CHECK(nearly(source_pad, weapon == W_CHAINGUN ? 4.0f : 0.0f,
                    0.0001f));
            }
        }
    }
    CHECK(!SG_CombatAimTestWeaponRay(W_GRENADE, HAND_RIGHT, 0, 0, 22.0f,
        origin, shot, origin, shot, NULL));
    return 0;
}

static int test_machinegun_deterministic_kick(void)
{
    vec3_t origin = { 0.0f, 0.0f, 0.0f };
    vec3_t desired = { 0.83f, -0.31f, 0.46f };
    vec3_t muzzle, actual;
    float source_pad = -1.0f;
    float dot;

    CHECK(normalize_vec(desired) > 0.0f);
    CHECK(SG_CombatAimTestWeaponRay(W_MACHINEGUN, HAND_RIGHT, 4, 0, 22.0f,
        origin, desired, muzzle, actual, &source_pad));
    dot = desired[0] * actual[0] + desired[1] * actual[1] +
          desired[2] * actual[2];
    CHECK(dot > 0.9999f);
    CHECK(nearly(source_pad, 0.0f, 0.0001f));
    return 0;
}

static int test_final_physical_rays(void)
{
    static const int weapons[] = {
        W_RAIL, W_BLASTER, W_HYPER, W_ROCKET, W_PLASMA, W_BFG
    };
    static const float ranges[] = { 400.0f, 800.0f, 1200.0f };
    static const float elapsed[] = { 0.0f, 0.20f };
    static const vec3_t boxes[][2] = {
        { { -16.0f, -16.0f, -24.0f }, { 16.0f, 16.0f, 32.0f } },
        { { -16.0f, -16.0f, -24.0f }, { 16.0f, 16.0f, 4.0f } }
    };
    vec3_t origin = { 0.0f, 0.0f, 0.0f };
    int range_index, box_index, pose, hand, weapon_index, elapsed_index;

    for (range_index = 0; range_index < 3; ++range_index)
        for (box_index = 0; box_index < 2; ++box_index)
            for (pose = 0; pose < 2; ++pose)
                for (hand = HAND_RIGHT; hand <= HAND_CENTER; ++hand)
                    for (weapon_index = 0; weapon_index < 6; ++weapon_index)
                        for (elapsed_index = 0; elapsed_index < 2;
                             ++elapsed_index) {
                            int weapon = weapons[weapon_index];
                            int gunframe = weapon == W_HYPER
                                ? (pose ? 9 : 6) : weapon == W_BFG ? 17 : 0;
                            vec3_t lead = {
                                ranges[range_index],
                                pose ? 73.0f : -61.0f,
                                pose ? 54.0f : -48.0f
                            };
                            vec3_t requested = {
                                lead[0], lead[1] + (pose ? -260.0f : 260.0f),
                                lead[2] + (pose ? 185.0f : -185.0f)
                            };
                            vec3_t muzzle, direction;
                            float source_pad = -1.0f;

                            CHECK(normalize_vec(requested) > 0.0f);
                            CHECK(SG_CombatAimTestFinalize(weapon, hand, 0,
                                gunframe, 22.0f, origin, lead,
                                boxes[box_index][0], boxes[box_index][1],
                                requested, elapsed[elapsed_index], muzzle,
                                direction, &source_pad));
                            CHECK(ray_hits_box(muzzle, direction, lead,
                                boxes[box_index][0], boxes[box_index][1]));
                            CHECK(nearly(source_pad, 0.0f, 0.0001f));
                        }
    return 0;
}

static int test_chaingun_source_envelope(void)
{
    static const float ranges[] = { 400.0f, 800.0f, 1200.0f };
    static const vec3_t boxes[][2] = {
        { { -16.0f, -16.0f, -24.0f }, { 16.0f, 16.0f, 32.0f } },
        { { -16.0f, -16.0f, -24.0f }, { 16.0f, 16.0f, 4.0f } }
    };
    vec3_t origin = { 0.0f, 0.0f, 0.0f };
    int range_index, box_index, hand, x, y, z;

    for (range_index = 0; range_index < 3; ++range_index)
        for (box_index = 0; box_index < 2; ++box_index)
            for (hand = HAND_RIGHT; hand <= HAND_CENTER; ++hand) {
                vec3_t lead = {
                    ranges[range_index],
                    range_index == 1 ? -63.0f : 58.0f,
                    box_index ? -45.0f : 52.0f
                };
                vec3_t requested = { lead[0], lead[1] + 240.0f,
                    lead[2] - 170.0f };
                vec3_t muzzle, direction;
                float source_pad = -1.0f;

                CHECK(normalize_vec(requested) > 0.0f);
                CHECK(SG_CombatAimTestFinalize(W_CHAINGUN, hand, 0, 0,
                    22.0f, origin, lead, boxes[box_index][0],
                    boxes[box_index][1], requested, 0.20f, muzzle, direction,
                    &source_pad));
                CHECK(nearly(source_pad, 4.0f, 0.0001f));

                /* The actual r/u choice lies in this stronger +/-4 source
                 * cube.  Every corner must retain a target-box intersection. */
                for (x = -1; x <= 1; x += 2)
                    for (y = -1; y <= 1; y += 2)
                        for (z = -1; z <= 1; z += 2) {
                            vec3_t actual_source = {
                                muzzle[0] + (float)x * source_pad,
                                muzzle[1] + (float)y * source_pad,
                                muzzle[2] + (float)z * source_pad
                            };

                            CHECK(ray_hits_box(actual_source, direction, lead,
                                boxes[box_index][0], boxes[box_index][1]));
                        }
            }
    return 0;
}

static int test_clear_shot_predicate(void)
{
    /* A near-muzzle wall is enemy_hit=0/unobstructed=0.  A teammate is a
     * distinct veto even if an otherwise direct projectile path is open. */
    CHECK(SG_CombatAimTestTraceClear(W_RAIL, 1, 0, 0));
    CHECK(!SG_CombatAimTestTraceClear(W_RAIL, 0, 1, 0));
    CHECK(!SG_CombatAimTestTraceClear(W_RAIL, 0, 0, 0));
    CHECK(!SG_CombatAimTestTraceClear(W_RAIL, 1, 0, 1));
    CHECK(SG_CombatAimTestTraceClear(W_ROCKET, 1, 0, 0));
    CHECK(SG_CombatAimTestTraceClear(W_ROCKET, 0, 1, 0));
    CHECK(!SG_CombatAimTestTraceClear(W_ROCKET, 0, 0, 0));
    CHECK(!SG_CombatAimTestTraceClear(W_ROCKET, 0, 1, 1));
    return 0;
}

int main(void)
{
    CHECK(!test_exact_muzzle_offsets());
    CHECK(!test_machinegun_deterministic_kick());
    CHECK(!test_final_physical_rays());
    CHECK(!test_chaingun_source_envelope());
    CHECK(!test_clear_shot_predicate());
    puts("combat aim production probe: ok");
    return 0;
}
"""


class CombatAimEnvelopeTest(unittest.TestCase):
    def test_strict_syntax_in_both_runtime_and_probe_modes(self) -> None:
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("cc is unavailable")
        base = [
            cc, "-std=c11", "-O0", "-fno-strict-aliasing", "-Wall",
            "-Wextra", "-Werror", "-Wpedantic", "-Wno-strict-prototypes",
            "-I.", "-fsyntax-only", "slipgate/sg_combat.c",
        ]
        for command in (base, [*base[:-1], "-DSG_COMBAT_AIM_TEST", base[-1]]):
            result = subprocess.run(command, cwd=ROOT, text=True,
                                    capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_production_muzzle_and_envelope_probe(self) -> None:
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("cc is unavailable")
        with tempfile.TemporaryDirectory(prefix="sg-combat-aim-") as temporary:
            temporary_path = Path(temporary)
            probe_path = temporary_path / "combat_aim_probe.c"
            binary_path = temporary_path / "combat_aim_probe"
            probe_path.write_text(textwrap.dedent(PROBE))
            command = [
                cc, "-std=c11", "-O0", "-fno-strict-aliasing",
                "-ffunction-sections", "-fdata-sections", "-I.",
                "-DSG_COMBAT_AIM_TEST", "slipgate/sg_combat.c", "g_utils.c",
                "q_shared.c", str(probe_path), "-Wl,--gc-sections", "-lm",
                "-o", str(binary_path),
            ]
            compiled = subprocess.run(command, cwd=ROOT, text=True,
                                       capture_output=True, check=False)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(binary_path)], cwd=ROOT, text=True,
                                 capture_output=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn("combat aim production probe: ok", ran.stdout)

    def test_source_preserves_physical_order_and_strict_clear_rule(self) -> None:
        frame = SOURCE[SOURCE.index("void SG_CombatFrame"):]
        finalise = frame.index("Combat_FinalizeMuzzleAim")
        trace = frame.index("sg_host.trace(muzzle, NULL, NULL, endp, self, MASK_SHOT)")
        ballistic = frame.index("if (ballistic)")
        no_clear = frame.index("if (!clear_shot)")
        splash = frame.index("if (!Combat_SplashSafe(self, inhand, impact))",
                             no_clear)

        self.assertLess(finalise, trace)
        self.assertLess(ballistic, trace)
        self.assertLess(no_clear, splash)
        self.assertNotIn("SG_HIT_SLOP", SOURCE)
        self.assertNotIn("sg_host.trace(eye, NULL, NULL, endp, self, MASK_SHOT)",
                         SOURCE)
        self.assertIn("Combat_RayHitsMuzzleEnvelope", SOURCE)
        self.assertIn("Combat_MachinegunPitchKick", SOURCE)
        self.assertIn("G_ProjectSource(self->s.origin, offset, forward, right, muzzle)",
                      SOURCE)
        self.assertIn("if (self->client->pers.hand == LEFT_HANDED)", SOURCE)
        self.assertIn("else if (self->client->pers.hand == CENTER_HANDED)",
                      SOURCE)
        self.assertIn("Combat_GrenadeImpact", frame)
        self.assertIn("return enemy_hit;", SOURCE)
        self.assertIn("return enemy_hit || unobstructed;", SOURCE)


if __name__ == "__main__":
    unittest.main()
