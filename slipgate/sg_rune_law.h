/* Era-4 law: the numbers the RUNE was derived under.
 *
 * The body's hulls and eye heights, gravity, the movement constants, the
 * frame, the hook's speeds and reach, the jump, and what a rocket jump
 * launches: all from the engine facts and the live gravity.  The generator
 * writes the law into the artifact; the level owner builds it again at
 * level start and refuses an artifact whose law differs.  Nothing here is
 * measured or learned. */
#ifndef SG_RUNE_LAW_H
#define SG_RUNE_LAW_H

#include <stdint.h>

typedef struct sg_rune_law_s
{
	float standing_mins[3];
	float standing_maxs[3];
	float crouching_mins[3];
	float crouching_maxs[3];
	float standing_view;      /* eye over the origin */
	float crouching_view;
	float gravity;
	float ground_acceleration;
	float air_acceleration;
	float water_acceleration;
	float hook_acceleration;
	float water_drag;
	float max_velocity;       /* run speed */
	float water_velocity;     /* swim speed */
	float jump_velocity;
	float step_size;
	float hook_fire_speed;
	float hook_pull_speed;
	float hook_near_bite;     /* the pull slows within this */
	float hook_hold;          /* gravity is off within this */
	uint32_t frame_ms;
	uint32_t substep_ms;
	uint32_t reserved[2];
} sg_rune_law_t;

/* What one rocket jump launches: attack and jump in one command, aiming
 * straight down, from a stand. */
typedef struct sg_rune_rocket_jump_s
{
	float lead_seconds;       /* from the command to the blast */
	float pre_blast_rise;     /* height over the floor at the blast */
	float vertical_velocity;  /* after the blast's kick */
	float lateral_velocity;   /* the kick's sideways part */
	float rise;               /* the peak over the floor */
	float self_damage;        /* health the blast takes, before armor */
} sg_rune_rocket_jump_t;

/* The engine's constants with the live gravity. */
void SG_RuneLawEngine(sg_rune_law_t *law, float gravity);
int SG_RuneLawValid(const sg_rune_law_t *law);
int SG_RuneLawSame(const sg_rune_law_t *a, const sg_rune_law_t *b);
uint32_t SG_RuneLawCrc(const sg_rune_law_t *law);
/* The hull for a stance (0 standing, else crouching). */
void SG_RuneLawHull(const sg_rune_law_t *law, int crouching,
	const float **mins_out, const float **maxs_out, float *view_out);
/* The rocket jump under this law; 0 when gravity gives none. */
int SG_RuneLawRocketJump(const sg_rune_law_t *law, sg_rune_rocket_jump_t *out);
/* The pull's speed at a distance from the bite, by the hook's bands. */
float SG_RuneLawHookPullSpeed(const sg_rune_law_t *law, float distance);

#endif /* SG_RUNE_LAW_H */
