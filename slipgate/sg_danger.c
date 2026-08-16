/*
 * sg_danger.c -- graph-bound, team-local learned danger.
 *
 * Persistence policy and filesystem I/O live above this model.  This module
 * owns only the published runtime planes and the explicit-LE payload.
 */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_util.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define DANGER_TEAM_COUNT 2U
#define DANGER_WIRE_VALUE_BYTES 4U
#define DANGER_VALUE_MAX 8000
#define DANGER_LEARN_INCREMENT 1200
_Static_assert(CHAR_BIT == 8, "danger payload requires 8-bit bytes");
_Static_assert(INT_MAX >= DANGER_VALUE_MAX,
	"native danger cells cannot represent the payload range");
_Static_assert(SG_MAX_SEEDS == RUNE_MAX_SEEDS,
	"danger and rune seed limits must agree");

/* No publication escapes this file.  Danger_Field is a const pricing view. */
static int sg_danger[DANGER_TEAM_COUNT][SG_MAX_SEEDS];
static const rune_t *sg_danger_rune;
static rune_artifact_t sg_danger_artifact;
static size_t sg_danger_num_seeds;
static float sg_danger_decay_next;
static qboolean sg_danger_active;
static qboolean sg_danger_persistence_enabled;
static qboolean sg_danger_dirty;
static qboolean sg_danger_revision_exhausted;
static uint64_t sg_danger_revision;

static uint32_t Danger_GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] |
	       ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

static void Danger_PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static qboolean Danger_RangesOverlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	uintptr_t first_begin;
	uintptr_t second_begin;

	if (!first_size || !second_size)
		return false;
	if (!first || !second)
		return true;
	first_begin = (uintptr_t)first;
	second_begin = (uintptr_t)second;
	if (first_begin > UINTPTR_MAX - first_size ||
	    second_begin > UINTPTR_MAX - second_size)
		return true;
	return first_begin < second_begin + second_size &&
	       second_begin < first_begin + first_size;
}

static qboolean Danger_RuneShape(const rune_t *r)
{
	return r && r->seeds && r->linked_seed &&
	       SG_RunePublishedShapeValid(r);
}

static qboolean Danger_Compatible(void)
{
	rune_t *current;

	if (!sg_danger_active)
		return false;
	current = SG_Rune();
	return current && current == sg_danger_rune &&
	       (size_t)current->hdr.num_seeds == sg_danger_num_seeds &&
	       Danger_RuneShape(current) &&
	       SG_RuneArtifactsEqual(&current->artifact, &sg_danger_artifact) &&
	       SG_RunePhysicsCompatible(current);
}

static qboolean Danger_SeedMayOwnValue(const rune_t *r, size_t seed)
{
	return !(r->seeds[seed].flags & RSF_TOMBSTONE) &&
	       r->linked_seed[seed] != 0;
}

static void Danger_AdvanceRevision(void)
{
	if (sg_danger_revision == UINT64_MAX)
	{
		/* Never wrap and never acknowledge a capture after uniqueness is lost. */
		sg_danger_revision_exhausted = true;
		return;
	}
	sg_danger_revision++;
}

size_t Danger_PayloadBytes(const rune_t *r)
{
	if (!Danger_RuneShape(r))
		return 0;
	return DANGER_TEAM_COUNT * (size_t)r->hdr.num_seeds *
	       DANGER_WIRE_VALUE_BYTES;
}

qboolean Danger_DecodeCandidate(const rune_t *r,
	const unsigned char *payload, size_t payload_size, int *red_out,
	int *blue_out, size_t plane_capacity)
{
	size_t native_bytes;
	size_t expected;
	size_t seed;
	uint32_t red;
	uint32_t blue;

	expected = Danger_PayloadBytes(r);
	if (!expected || !payload || !red_out || !blue_out ||
	    payload_size != expected ||
	    plane_capacity < (size_t)r->hdr.num_seeds)
		return false;
	native_bytes = (size_t)r->hdr.num_seeds * sizeof(*red_out);
	if (Danger_RangesOverlap(red_out, native_bytes, blue_out, native_bytes) ||
	    Danger_RangesOverlap(payload, payload_size, red_out, native_bytes) ||
	    Danger_RangesOverlap(payload, payload_size, blue_out, native_bytes))
		return false;

	/* Validate both complete planes before modifying either caller output. */
	for (seed = 0; seed < (size_t)r->hdr.num_seeds; seed++)
	{
		red = Danger_GetU32(payload + seed * DANGER_WIRE_VALUE_BYTES);
		blue = Danger_GetU32(payload +
		    ((size_t)r->hdr.num_seeds + seed) * DANGER_WIRE_VALUE_BYTES);
		if (red > DANGER_VALUE_MAX || blue > DANGER_VALUE_MAX ||
		    (!Danger_SeedMayOwnValue(r, seed) && (red || blue)))
			return false;
	}
	for (seed = 0; seed < (size_t)r->hdr.num_seeds; seed++)
	{
		red_out[seed] = (int)Danger_GetU32(payload +
		    seed * DANGER_WIRE_VALUE_BYTES);
		blue_out[seed] = (int)Danger_GetU32(payload +
		    ((size_t)r->hdr.num_seeds + seed) * DANGER_WIRE_VALUE_BYTES);
	}
	return true;
}

qboolean Danger_Publish(const rune_t *r, const int *red, const int *blue,
	size_t plane_count, qboolean persistence_enabled)
{
	size_t native_bytes;
	size_t seed;
	qboolean neutral;

	neutral = !red && !blue && plane_count == 0;
	if (!r || r != SG_Rune() || !Danger_RuneShape(r) ||
	    !SG_RunePhysicsCompatible(r) ||
	    (!neutral && (!red || !blue ||
	        plane_count != (size_t)r->hdr.num_seeds)))
		return false;
	native_bytes = (size_t)r->hdr.num_seeds * sizeof(*red);
	if (!neutral &&
	    (Danger_RangesOverlap(red, native_bytes, sg_danger,
	         sizeof(sg_danger)) ||
	     Danger_RangesOverlap(blue, native_bytes, sg_danger,
	         sizeof(sg_danger))))
		return false;
	if (!neutral)
	{
		for (seed = 0; seed < (size_t)r->hdr.num_seeds; seed++)
		{
			if (red[seed] < 0 || red[seed] > DANGER_VALUE_MAX ||
			    blue[seed] < 0 || blue[seed] > DANGER_VALUE_MAX ||
			    (!Danger_SeedMayOwnValue(r, seed) &&
			        (red[seed] || blue[seed])))
				return false;
		}
	}

	memset(sg_danger, 0, sizeof(sg_danger));
	if (!neutral)
	{
		memcpy(sg_danger[0], red, native_bytes);
		memcpy(sg_danger[1], blue, native_bytes);
	}
	sg_danger_rune = r;
	sg_danger_artifact = r->artifact;
	sg_danger_num_seeds = (size_t)r->hdr.num_seeds;
	sg_danger_active = true;
	sg_danger_persistence_enabled = persistence_enabled ? true : false;
	sg_danger_dirty = false;
	SG_TimerArm(&sg_danger_decay_next, 1.0f);
	Danger_AdvanceRevision();
	return true;
}

void Danger_ResetLevel(void)
{
	memset(sg_danger, 0, sizeof(sg_danger));
	memset(&sg_danger_artifact, 0, sizeof(sg_danger_artifact));
	sg_danger_rune = NULL;
	sg_danger_num_seeds = 0;
	sg_danger_decay_next = 0.0f;
	sg_danger_active = false;
	sg_danger_persistence_enabled = false;
	sg_danger_dirty = false;
	Danger_AdvanceRevision();
}

void Danger_Learn(int team, int seed)
{
	int *cell;

	if (!Danger_Compatible() || team < 1 || team > 2 || seed < 0 ||
	    (size_t)seed >= sg_danger_num_seeds ||
	    !Danger_SeedMayOwnValue(sg_danger_rune, (size_t)seed))
		return;
	cell = &sg_danger[SG_TeamIdx(team)][seed];
	if (*cell >= DANGER_VALUE_MAX)
		return;
	if (*cell > DANGER_VALUE_MAX - DANGER_LEARN_INCREMENT)
		*cell = DANGER_VALUE_MAX;
	else
		*cell += DANGER_LEARN_INCREMENT;
	sg_danger_dirty = true;
	Danger_AdvanceRevision();
}

void Danger_Decay(void)
{
	qboolean changed = false;
	size_t team;
	size_t seed;

	if (!Danger_Compatible())
	{
		/* Incompatible time is not learned time.  A restored binding waits a
		 * fresh second rather than consuming an already-expired deadline. */
		if (sg_danger_active)
			SG_TimerArm(&sg_danger_decay_next, 1.0f);
		return;
	}
	if (SG_TimerPending(sg_danger_decay_next))
		return;
	SG_TimerArm(&sg_danger_decay_next, 1.0f);
	for (team = 0; team < DANGER_TEAM_COUNT; team++)
	{
		for (seed = 0; seed < sg_danger_num_seeds; seed++)
		{
			if (sg_danger[team][seed] > 0)
			{
				sg_danger[team][seed] -=
				    (sg_danger[team][seed] >> 6) + 1;
				changed = true;
			}
		}
	}
	if (changed)
	{
		sg_danger_dirty = true;
		Danger_AdvanceRevision();
	}
}

const int *Danger_Field(int team)
{
	if (team < 1 || team > 2)
		return NULL;
	return sg_danger[SG_TeamIdx(team)];
}

qboolean Danger_IsActive(void)
{
	return sg_danger_active;
}

qboolean Danger_PersistenceEnabled(void)
{
	return sg_danger_persistence_enabled;
}

qboolean Danger_IsDirty(void)
{
	return sg_danger_dirty;
}

uint64_t Danger_Revision(void)
{
	return sg_danger_revision;
}

qboolean Danger_CheckpointPending(void)
{
	return sg_danger_active && sg_danger_persistence_enabled &&
	       sg_danger_dirty && Danger_Compatible();
}

qboolean Danger_CapturePayload(unsigned char *payload,
	size_t payload_capacity, size_t *payload_size_out,
	uint64_t *revision_out)
{
	size_t payload_size;
	size_t seed;
	uint64_t revision;

	if (!payload || !payload_size_out || !revision_out ||
	    !Danger_Compatible())
		return false;
	payload_size = DANGER_TEAM_COUNT * sg_danger_num_seeds *
	    DANGER_WIRE_VALUE_BYTES;
	if (payload_capacity < payload_size ||
	    Danger_RangesOverlap(payload, payload_size, sg_danger,
	        sizeof(sg_danger)) ||
	    Danger_RangesOverlap(payload, payload_size, payload_size_out,
	        sizeof(*payload_size_out)) ||
	    Danger_RangesOverlap(payload, payload_size, revision_out,
	        sizeof(*revision_out)) ||
	    Danger_RangesOverlap(payload_size_out, sizeof(*payload_size_out),
	        revision_out, sizeof(*revision_out)))
		return false;
	revision = sg_danger_revision;
	for (seed = 0; seed < sg_danger_num_seeds; seed++)
	{
		Danger_PutU32(payload + seed * DANGER_WIRE_VALUE_BYTES,
		    (uint32_t)sg_danger[0][seed]);
		Danger_PutU32(payload +
		    (sg_danger_num_seeds + seed) * DANGER_WIRE_VALUE_BYTES,
		    (uint32_t)sg_danger[1][seed]);
	}
	*payload_size_out = payload_size;
	*revision_out = revision;
	return true;
}

qboolean Danger_MarkCommitted(uint64_t revision)
{
	if (!sg_danger_active || !sg_danger_persistence_enabled ||
	    sg_danger_revision_exhausted || revision != sg_danger_revision ||
	    !Danger_Compatible())
		return false;
	sg_danger_dirty = false;
	return true;
}
